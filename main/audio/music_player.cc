// 音乐播放器核心实现
//   - 异步下载/解码/播放
//   - 状态机: Idle → Loading → Playing → Paused/Stopped/Error

#include "music_player.h"

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_audio_dec_default.h"
#include "esp_audio_simple_dec_default.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_pcm_dec.h"
#include "esp_task_wdt.h"

#include "audio_codec.h"
#include "board.h"
#include "wav_parser.h"

static const char* TAG = "MusicPlayer";

namespace {

constexpr const char* TMP_DIR = "/sdcard/tmp";

constexpr int HTTP_RECV_TIMEOUT_MS = 30000;
constexpr int HTTP_CONNECT_TIMEOUT_MS = 5000;
constexpr size_t DOWNLOAD_BUFFER_SIZE = 4096;

constexpr size_t PCM_FRAME_SIZE = 4096;

constexpr uint32_t PROGRESS_NOTIFY_MS = 250;

esp_audio_type_t DetectAudioType(const std::string& path) {
    if (path.size() < 4) return ESP_AUDIO_TYPE_UNSUPPORT;
    std::string ext = path.substr(path.size() - 4);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".mp3") return ESP_AUDIO_TYPE_MP3;
    if (ext == ".aac") return ESP_AUDIO_TYPE_AAC;
    if (ext == ".m4a") return ESP_AUDIO_TYPE_AAC;
    if (ext == ".flac") return ESP_AUDIO_TYPE_FLAC;
    if (ext == ".opus") return ESP_AUDIO_TYPE_OPUS;
    if (ext == ".wav") return ESP_AUDIO_TYPE_PCM;
    return ESP_AUDIO_TYPE_UNSUPPORT;
}

std::string UrlToTempPath(const std::string& url) {
    size_t last_slash = url.rfind('/');
    std::string name = (last_slash != std::string::npos) ? url.substr(last_slash + 1) : url;
    size_t q = name.find('?');
    if (q != std::string::npos) name = name.substr(0, q);
    if (name.empty() || name.find('.') == std::string::npos) {
        name = "download.mp3";
    }
    return std::string(TMP_DIR) + "/" + name;
}

bool DownloadUrlToFile(const std::string& url, const std::string& dest_path, int timeout_ms) {
    ESP_LOGI(TAG, "Downloading: %s -> %s", url.c_str(), dest_path.c_str());
    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.timeout_ms = timeout_ms;
    config.keep_alive_enable = true;
    config.skip_cert_common_name_check = true;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        ESP_LOGE(TAG, "esp_http_client_init failed");
        return false;
    }
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_http_client_open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }
    int content_length = esp_http_client_get_content_length(client);
    int status_code = esp_http_client_get_status_code(client);
    if (status_code < 200 || status_code >= 300) {
        ESP_LOGE(TAG, "HTTP status %d, abort", status_code);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }
    ESP_LOGI(TAG, "HTTP %d, content-length=%d", status_code, content_length);
    FILE* fp = fopen(dest_path.c_str(), "wb");
    if (fp == nullptr) {
        ESP_LOGE(TAG, "Cannot open dest file: %s", dest_path.c_str());
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }
    char buf[4096];
    int total_read = 0;
    int last_log_pct = -1;
    while (true) {
        int read = esp_http_client_read(client, buf, sizeof(buf));
        if (read < 0) {
            ESP_LOGE(TAG, "esp_http_client_read error: %d", read);
            fclose(fp);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return false;
        }
        if (read == 0) break;
        size_t written = fwrite(buf, 1, read, fp);
        if (written != (size_t)read) {
            ESP_LOGE(TAG, "fwrite failed");
            fclose(fp);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return false;
        }
        total_read += read;
        if (content_length > 0) {
            int pct = total_read * 100 / content_length;
            if (pct / 10 > last_log_pct / 10) {
                last_log_pct = pct;
                ESP_LOGI(TAG, "Download: %d%% (%d/%d)", pct, total_read, content_length);
            }
        }
    }
    fclose(fp);
    ESP_LOGI(TAG, "Download complete: %d bytes", total_read);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return true;
}

}  // namespace

MusicPlayer& MusicPlayer::GetInstance() {
    static MusicPlayer instance;
    return instance;
}

void MusicPlayer::RegisterDecoderTask(void* arg) {
    MusicPlayer* self = static_cast<MusicPlayer*>(arg);
    ESP_LOGI(TAG, "Decoder registration task started");
    esp_audio_err_t err = esp_audio_dec_register_default();
    if (err == ESP_AUDIO_ERR_OK) {
        ESP_LOGI(TAG, "esp_audio_dec_register_default OK");
    } else {
        ESP_LOGW(TAG, "esp_audio_dec_register_default: %d", err);
    }
    esp_audio_simple_dec_register_default();
    ESP_LOGI(TAG, "esp_audio_simple_dec_register_default done");
    self->decoder_registered_.store(true);
    vTaskDelete(nullptr);
}

MusicPlayer::MusicPlayer() {
    mkdir(TMP_DIR, 0755);
    esp_task_wdt_config_t wdt_cfg = {
        .timeout_ms = 120000,
        .idle_core_mask = (1 << 0) | (1 << 1),
        .trigger_panic = true,
    };
    esp_task_wdt_init(&wdt_cfg);
    decoder_registered_.store(false);
    xTaskCreatePinnedToCore(RegisterDecoderTask, "dec_reg", 4096, this, 5, nullptr, 1);
    ESP_LOGI(TAG, "MusicPlayer initialized, tmp_dir=%s (decoder in bg)", TMP_DIR);
}

MusicPlayer::~MusicPlayer() {
    Stop();
}

// ========== 公共接口（线程安全，直接调用 Handle*） ==========

void MusicPlayer::PlayFile(const std::string& path, bool loop) {
    if (state_.load() == State::Loading) {
        ESP_LOGW(TAG, "PlayFile ignored: already loading");
        return;
    }
    HandlePlayFile(path, loop);
}

void MusicPlayer::PlayUrl(const std::string& url, bool loop) {
    if (state_.load() == State::Loading) {
        ESP_LOGW(TAG, "PlayUrl ignored: already loading");
        return;
    }
    HandlePlayUrl(url, loop);
}

void MusicPlayer::PlayPlaylist(const std::vector<std::string>& paths, bool loop) {
    if (paths.empty()) {
        ESP_LOGW(TAG, "PlayPlaylist ignored: empty paths");
        return;
    }
    if (state_.load() == State::Loading) {
        ESP_LOGW(TAG, "PlayPlaylist ignored: already loading");
        return;
    }
    current_playlist_ = paths;
    current_playlist_index_ = 0;
    playlist_loop_ = loop;
    ESP_LOGI(TAG, "PlayPlaylist set: %zu tracks (loop=%d)", paths.size(), loop ? 1 : 0);
    HandlePlayFile(paths[0], false);
}

bool MusicPlayer::PlayNext() {
    if (current_playlist_.empty()) {
        ESP_LOGW(TAG, "PlayNext ignored: no playlist");
        return false;
    }
    int next_idx = (int)current_playlist_index_ + 1;
    if (next_idx >= (int)current_playlist_.size()) next_idx = 0;
    const std::string& path = current_playlist_[next_idx];
    ESP_LOGI(TAG, "PlayNext: [%d/%zu] %s", next_idx + 1, current_playlist_.size(), path.c_str());
    HandlePlayFile(path, false);
    return true;
}

bool MusicPlayer::PlayPrev() {
    if (current_playlist_.empty()) {
        ESP_LOGW(TAG, "PlayPrev ignored: no playlist");
        return false;
    }
    int prev_idx = (int)current_playlist_index_ - 1;
    if (prev_idx < 0) prev_idx = (int)current_playlist_.size() - 1;
    const std::string& path = current_playlist_[prev_idx];
    ESP_LOGI(TAG, "PlayPrev: [%d/%zu] %s", prev_idx + 1, current_playlist_.size(), path.c_str());
    HandlePlayFile(path, false);
    return true;
}

void MusicPlayer::Pause() {
    HandlePause();
}

void MusicPlayer::Resume() {
    HandleResume();
}

void MusicPlayer::Stop() {
    stop_requested_ = true;
    HandleStop();
}

std::string MusicPlayer::GetCurrentFile() const {
    return current_file_;
}

// ========== 内部实现 ==========

void MusicPlayer::HandlePlayFile(const std::string& path, bool loop) {
    if (play_task_) {
        stop_requested_ = true;
        for (int i = 0; i < 200; ++i) {
            if (!play_task_) break;
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    if (DetectAudioType(path) == ESP_AUDIO_TYPE_UNSUPPORT) {
        HandleError("unsupported format");
        return;
    }

    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        HandleError("file not found");
        return;
    }

    current_file_ = path;
    loop_ = loop;
    stop_requested_ = false;
    paused_ = false;
    progress_ = 0.0f;
    source_type_ = SourceType::File;

    if (!current_playlist_.empty()) {
        for (size_t i = 0; i < current_playlist_.size(); i++) {
            if (current_playlist_[i] == path) {
                current_playlist_index_ = i;
                ESP_LOGI(TAG, "Playlist position: %zu/%zu", i + 1, current_playlist_.size());
                break;
            }
        }
    }

    NotifyState(State::Loading, 0.0f);
    xTaskCreate(PlayTask, "music_play", 8192, this, 5, &play_task_);
    ESP_LOGI(TAG, "PlayTask started for file: %s", path.c_str());
}

void MusicPlayer::HandlePlayUrl(const std::string& url, bool loop) {
    if (play_task_) {
        stop_requested_ = true;
        for (int i = 0; i < 200; ++i) {
            if (!play_task_) break;
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    current_url_ = url;
    current_file_ = UrlToTempPath(url);
    loop_ = loop;
    stop_requested_ = false;
    paused_ = false;
    progress_ = 0.0f;
    source_type_ = SourceType::Url;

    NotifyState(State::Loading, 0.0f);
    xTaskCreate(PlayTask, "music_play", 8192, this, 5, &play_task_);
    ESP_LOGI(TAG, "PlayTask started for URL: %s -> %s", url.c_str(), current_file_.c_str());
}

void MusicPlayer::HandlePause() {
    if (state_.load() == State::Playing) {
        paused_ = true;
        NotifyState(State::Paused, progress_.load());
    }
}

void MusicPlayer::HandleResume() {
    if (state_.load() == State::Paused) {
        paused_ = false;
        NotifyState(State::Playing, progress_.load());
    }
}

void MusicPlayer::HandleStop() {
    stop_requested_ = true;
    paused_ = false;
    current_playlist_.clear();
    current_playlist_index_ = 0;
    playlist_loop_ = false;
    NotifyState(State::Idle, 0.0f);
    current_file_.clear();
    progress_ = 0.0f;
}

void MusicPlayer::HandleError(const char* msg) {
    last_error_ = msg ? msg : "";
    ESP_LOGE(TAG, "Error: %s", last_error_.c_str());
    NotifyState(State::Error, 0.0f);
}

void MusicPlayer::NotifyState(State state, float progress) {
    state_.store(state);
    progress_.store(progress);
    std::lock_guard<std::mutex> lock(listener_mutex_);
    if (state_listener_) {
        state_listener_(state, progress);
    }
}

// ========== 后台任务：解码 + 播放 ==========

void MusicPlayer::PlayTask(void* arg) {
    MusicPlayer* self = static_cast<MusicPlayer*>(arg);
    self->stop_requested_ = false;

    auto finalize = [self]() {
        if (!self->stop_requested_ && !self->current_playlist_.empty()) {
            size_t next_idx = self->current_playlist_index_ + 1;
            if (next_idx >= self->current_playlist_.size()) {
                if (self->playlist_loop_) {
                    next_idx = 0;
                } else {
                    self->current_playlist_.clear();
                    self->current_playlist_index_ = 0;
                    self->NotifyState(State::Idle, 1.0f);
                    self->play_task_ = nullptr;
                    vTaskDelete(nullptr);
                    return;
                }
            }
            const std::string& next_path = self->current_playlist_[next_idx];
            ESP_LOGI(TAG, "Playlist: next track [%zu/%zu]: %s",
                     next_idx + 1, self->current_playlist_.size(), next_path.c_str());
            self->HandlePlayFile(next_path, false);
            self->NotifyState(State::Idle, 1.0f);
        } else if (self->stop_requested_) {
            self->NotifyState(State::Idle, 0.0f);
        } else {
            self->NotifyState(State::Idle, 1.0f);
        }
        self->play_task_ = nullptr;
        vTaskDelete(nullptr);
    };

    AudioCodec* codec = Board::GetInstance().GetAudioCodec();
    if (codec == nullptr) {
        self->HandleError("no audio codec");
        finalize();
        return;
    }

    ESP_LOGI(TAG, "Waiting for decoder registration...");
    int wait_ms = 0;
    while (!self->decoder_registered_.load()) {
        vTaskDelay(pdMS_TO_TICKS(50));
        wait_ms += 50;
        if (wait_ms >= 30000) {
            self->HandleError("decoder registration timeout");
            finalize();
            return;
        }
    }
    ESP_LOGI(TAG, "Decoder ready (waited %d ms)", wait_ms);

    if (self->source_type_.load() == SourceType::Url) {
        if (self->current_url_.empty()) {
            self->HandleError("empty url");
            finalize();
            return;
        }
        ESP_LOGI(TAG, "URL mode: downloading to %s", self->current_file_.c_str());
        bool ok = DownloadUrlToFile(self->current_url_, self->current_file_, 60000);
        if (!ok) {
            self->HandleError("download failed");
            finalize();
            return;
        }
        if (self->stop_requested_) {
            finalize();
            return;
        }
        self->source_type_.store(SourceType::File);
        ESP_LOGI(TAG, "Download done, switching to File mode");
    }

    FILE* fp = fopen(self->current_file_.c_str(), "rb");
    if (fp == nullptr) {
        self->HandleError("cannot open file");
        finalize();
        return;
    }
    fseek(fp, 0, SEEK_END);
    size_t full_file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    WavHeaderInfo wav_info;
    esp_audio_type_t type_detected = DetectAudioType(self->current_file_);
    size_t data_start_offset = 0;
    if (type_detected == ESP_AUDIO_TYPE_PCM) {
        if (!WavParseHeader(fp, wav_info)) {
            fclose(fp);
            self->HandleError("invalid WAV header");
            finalize();
            return;
        }
        data_start_offset = wav_info.data_offset;
        ESP_LOGI(TAG, "WAV: %lu Hz, %u ch, %u bits, data_offset=%lu, data_size=%lu",
                 (unsigned long)wav_info.sample_rate, (unsigned int)wav_info.channels,
                 (unsigned int)wav_info.bits_per_sample,
                 (unsigned long)wav_info.data_offset, (unsigned long)wav_info.data_size);
    }
    size_t file_size = (type_detected == ESP_AUDIO_TYPE_PCM) ? wav_info.data_size : full_file_size;

    std::vector<uint8_t> in_buf(DOWNLOAD_BUFFER_SIZE);
    std::vector<int16_t> pcm_buf(PCM_FRAME_SIZE / sizeof(int16_t));

    int loop_round = 0;
    esp_audio_dec_handle_t decoder = nullptr;
    while (!self->stop_requested_) {
        loop_round++;
        fseek(fp, data_start_offset, SEEK_SET);
        size_t total_consumed = 0;

        esp_audio_dec_cfg_t dec_cfg = {};
        dec_cfg.type = DetectAudioType(self->current_file_);
        esp_pcm_dec_cfg_t pcm_cfg = {};
        if (dec_cfg.type == ESP_AUDIO_TYPE_PCM) {
            pcm_cfg.sample_rate = (int32_t)wav_info.sample_rate;
            pcm_cfg.channel = (uint8_t)wav_info.channels;
            pcm_cfg.bits_per_sample = (uint8_t)wav_info.bits_per_sample;
            dec_cfg.cfg = &pcm_cfg;
            dec_cfg.cfg_sz = sizeof(pcm_cfg);
        } else {
            dec_cfg.cfg = nullptr;
            dec_cfg.cfg_sz = 0;
        }

        decoder = nullptr;
        esp_audio_err_t err = esp_audio_dec_open(&dec_cfg, &decoder);
        if (err != ESP_AUDIO_ERR_OK || decoder == nullptr) {
            fclose(fp);
            self->HandleError("decoder open failed");
            finalize();
            return;
        }

        if (loop_round == 1) {
            self->NotifyState(State::Playing, 0.0f);
        } else {
            self->progress_.store(0.0f);
        }
        uint32_t last_progress_notify = 0;

        bool eof_reached = false;
        while (!self->stop_requested_ && !eof_reached) {
            while (self->paused_ && !self->stop_requested_) {
                vTaskDelay(pdMS_TO_TICKS(50));
            }
            if (self->stop_requested_) break;

            size_t nread = fread(in_buf.data(), 1, in_buf.size(), fp);
            if (nread == 0) {
                eof_reached = true;
                break;
            }

            size_t pos = 0;
            while (pos < nread && !self->stop_requested_) {
                esp_audio_dec_in_raw_t in_raw = {};
                in_raw.buffer = in_buf.data() + pos;
                in_raw.len = nread - pos;

                esp_audio_dec_out_frame_t out_frame = {};
                out_frame.buffer = (uint8_t*)pcm_buf.data();
                out_frame.len = pcm_buf.size() * sizeof(int16_t);

                err = esp_audio_dec_process(decoder, &in_raw, &out_frame);
                if (err != ESP_AUDIO_ERR_OK) {
                    ESP_LOGW(TAG, "decode err=%d, skipping frame", err);
                    pos += in_raw.consumed;
                    continue;
                }

                pos += in_raw.consumed;
                total_consumed += in_raw.consumed;

                if (out_frame.decoded_size > 0) {
                    pcm_buf.resize(out_frame.decoded_size / sizeof(int16_t));
                    codec->OutputData(pcm_buf);
                }

                uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
                if (now - last_progress_notify >= PROGRESS_NOTIFY_MS) {
                    last_progress_notify = now;
                    float prog = (file_size > 0) ? (float)total_consumed / file_size : 0.0f;
                    self->progress_.store(prog);
                }
            }
        }

        esp_audio_dec_close(decoder);

        if (self->stop_requested_) break;
        if (!self->loop_) break;
        ESP_LOGI(TAG, "Loop round %d complete, restarting", loop_round);
    }

    esp_audio_dec_close(decoder);
    fclose(fp);
    finalize();
}
