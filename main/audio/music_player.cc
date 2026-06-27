// 黑胶唱片音乐播放器核心实现
//   - 异步下载/解码/播放
//   - 状态机: Idle → Loading → Playing → Paused/Stopped/Error
//   - 所有 LVGL 操作在 LVGL 主线程（通过 OnMainThreadTick 调度）

#include "music_player.h"

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_audio_dec_default.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_pcm_dec.h"

#include "audio_codec.h"
#include "board.h"
#include "wav_parser.h"

static const char* TAG = "MusicPlayer";

namespace {

// 临时下载目录（SD 卡）
constexpr const char* TMP_DIR = "/sdcard/tmp";

// HTTP 下载配置
constexpr int HTTP_RECV_TIMEOUT_MS = 30000;
constexpr int HTTP_CONNECT_TIMEOUT_MS = 5000;
constexpr size_t DOWNLOAD_BUFFER_SIZE = 4096;

// 解码器输出 PCM 帧最大尺寸（采样率 44100、16bit、双声道 = 176KB/秒，单帧 ~10ms）
constexpr size_t PCM_FRAME_SIZE = 4096;

// 进度更新间隔（每 250ms 通知一次 UI）
constexpr uint32_t PROGRESS_NOTIFY_MS = 250;

// 按文件扩展名判断音频类型
// 注意：esp_audio_codec 不直接支持 WAV/OGG 容器
//   - WAV: esp_audio_codec 提供 ESP_AUDIO_TYPE_PCM + esp_pcm_dec
//          我们先用 wav_parser.h 解析 RIFF 头找到 data offset，再喂 PCM decoder
//   - OGG: 仍不支持（需要 OGG demuxer 解封装 Vorbis）
esp_audio_type_t DetectAudioType(const std::string& path) {
    if (path.size() < 4) return ESP_AUDIO_TYPE_UNSUPPORT;
    std::string ext = path.substr(path.size() - 4);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".mp3") return ESP_AUDIO_TYPE_MP3;
    if (ext == ".aac") return ESP_AUDIO_TYPE_AAC;
    if (ext == ".m4a") return ESP_AUDIO_TYPE_AAC;
    if (ext == ".flac") return ESP_AUDIO_TYPE_FLAC;
    if (ext == ".opus") return ESP_AUDIO_TYPE_OPUS;
    if (ext == ".wav") return ESP_AUDIO_TYPE_PCM;  // 容器 = PCM decoder + wav_parser
    return ESP_AUDIO_TYPE_UNSUPPORT;
}

// URL 转本地临时文件路径
std::string UrlToTempPath(const std::string& url) {
    // 用 URL 末尾路径做文件名（去协议和 query）
    size_t last_slash = url.rfind('/');
    std::string name = (last_slash != std::string::npos) ? url.substr(last_slash + 1) : url;
    // 去 query string
    size_t q = name.find('?');
    if (q != std::string::npos) name = name.substr(0, q);
    if (name.empty() || name.find('.') == std::string::npos) {
        name = "download.mp3";  // 默认
    }
    return std::string(TMP_DIR) + "/" + name;
}

// HTTP GET 下载到文件（同步调用，调用方必须在后台 task 中）
//   - url: HTTP/HTTPS URL
//   - dest_path: 目标文件绝对路径（建议 /sdcard/tmp/xxx.ext）
//   - timeout_ms: 总超时（connect + receive）
//   - 返回：true 成功；false 失败（看 ESP_LOG 输出）
bool DownloadUrlToFile(const std::string& url, const std::string& dest_path, int timeout_ms) {
    ESP_LOGI(TAG, "Downloading: %s -> %s", url.c_str(), dest_path.c_str());

    // 配置 HTTP client
    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.timeout_ms = timeout_ms;
    config.keep_alive_enable = true;
    config.skip_cert_common_name_check = true;  // 测试方便，正式可以验证证书

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

    // 打开目标文件
    FILE* fp = fopen(dest_path.c_str(), "wb");
    if (fp == nullptr) {
        ESP_LOGE(TAG, "Cannot open dest file: %s", dest_path.c_str());
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    // 读取 + 写入
    char buf[4096];
    int total_read = 0;
    int last_log_pct = -1;
    int64_t start = esp_timer_get_time();

    while (true) {
        int read = esp_http_client_read(client, buf, sizeof(buf));
        if (read < 0) {
            ESP_LOGE(TAG, "esp_http_client_read error: %d", read);
            fclose(fp);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return false;
        }
        if (read == 0) break;  // EOF

        size_t written = fwrite(buf, 1, read, fp);
        if (written != (size_t)read) {
            ESP_LOGE(TAG, "fwrite failed: %d != %d", (int)written, read);
            fclose(fp);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return false;
        }
        total_read += read;

        // 进度日志（每 10% 一次）
        if (content_length > 0) {
            int pct = total_read * 100 / content_length;
            if (pct / 10 > last_log_pct / 10) {
                last_log_pct = pct;
                ESP_LOGI(TAG, "Download progress: %d%% (%d/%d)", pct, total_read, content_length);
            }
        }
    }

    fclose(fp);
    int64_t elapsed_ms = (esp_timer_get_time() - start) / 1000;
    ESP_LOGI(TAG, "Download complete: %d bytes in %lld ms", total_read, (long long)elapsed_ms);

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return true;
}

}  // namespace

MusicPlayer& MusicPlayer::GetInstance() {
    static MusicPlayer instance;
    return instance;
}

MusicPlayer::MusicPlayer() {
    // 注册所有 esp_audio_codec 默认 decoder（mp3/wav/aac/ogg/opus/flac/...）
    esp_audio_err_t reg_err = esp_audio_dec_register_default();
    if (reg_err != ESP_AUDIO_ERR_OK) {
        ESP_LOGE(TAG, "esp_audio_dec_register_default failed: %d", reg_err);
    }
    // 创建请求队列（容量 4）
    request_queue_ = xQueueCreate(4, sizeof(Request));
    // 确保临时目录存在
    mkdir(TMP_DIR, 0755);
    ESP_LOGI(TAG, "MusicPlayer initialized, queue=4 slots, tmp_dir=%s, dec_reg=%d",
             TMP_DIR, (int)reg_err);
}

MusicPlayer::~MusicPlayer() {
    Stop();
    if (request_queue_) {
        vQueueDelete(request_queue_);
        request_queue_ = nullptr;
    }
}

// ========== 公共接口（线程安全） ==========

void MusicPlayer::PlayFile(const std::string& path, bool loop) {
    if (state_.load() == State::Loading) {
        ESP_LOGW(TAG, "PlayFile ignored: already loading");
        return;
    }
    Request req = {};
    req.type = RequestType::Play;
    req.loop = loop;
    snprintf(req.param, sizeof(req.param), "%s", path.c_str());
    req.param[sizeof(req.param) - 1] = '\0';

    if (xQueueSend(request_queue_, &req, 0) != pdTRUE) {
        ESP_LOGW(TAG, "PlayFile queue full, dropping");
        return;
    }
    ESP_LOGI(TAG, "PlayFile queued: %s (loop=%d)", path.c_str(), loop ? 1 : 0);
}

void MusicPlayer::PlayUrl(const std::string& url, bool loop) {
    if (state_.load() == State::Loading) {
        ESP_LOGW(TAG, "PlayUrl ignored: already loading");
        return;
    }
    Request req = {};
    req.type = RequestType::Play;
    req.loop = loop;
    // 用 "url:" 前缀区分本地/URL
    std::string payload = "url:" + url;
    snprintf(req.param, sizeof(req.param), "%s", payload.c_str());
    req.param[sizeof(req.param) - 1] = '\0';

    if (xQueueSend(request_queue_, &req, 0) != pdTRUE) {
        ESP_LOGW(TAG, "PlayUrl queue full, dropping");
        return;
    }
    ESP_LOGI(TAG, "PlayUrl queued: %s (loop=%d)", url.c_str(), loop ? 1 : 0);
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
    // 设置 playlist 状态（主线程访问），由 HandlePlayFile 时投递第一首
    current_playlist_ = paths;
    current_playlist_index_ = 0;
    playlist_loop_ = loop;
    ESP_LOGI(TAG, "PlayPlaylist set: %zu tracks (loop=%d)", paths.size(), loop ? 1 : 0);

    // 投递第一首到 queue
    Request req = {};
    req.type = RequestType::Play;
    req.loop = false;  // 单曲不循环（playlist_loop_ 控制整列表）
    snprintf(req.param, sizeof(req.param), "%s", paths[0].c_str());
    req.param[sizeof(req.param) - 1] = '\0';
    if (xQueueSend(request_queue_, &req, 0) != pdTRUE) {
        ESP_LOGW(TAG, "PlayPlaylist: first track queue full, dropping");
    }
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
    PlayFile(path, false);
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
    PlayFile(path, false);
    return true;
}

void MusicPlayer::Pause() {
    Request req = {};
    req.type = RequestType::Pause;
    xQueueSend(request_queue_, &req, 0);
    ESP_LOGI(TAG, "Pause queued");
}

void MusicPlayer::Resume() {
    Request req = {};
    req.type = RequestType::Resume;
    xQueueSend(request_queue_, &req, 0);
    ESP_LOGI(TAG, "Resume queued");
}

void MusicPlayer::Stop() {
    stop_requested_ = true;
    Request req = {};
    req.type = RequestType::Stop;
    xQueueSend(request_queue_, &req, 0);
    ESP_LOGI(TAG, "Stop queued");
}

std::string MusicPlayer::GetCurrentFile() const {
    return current_file_;
}

// ========== LVGL 主线程 tick ==========

void MusicPlayer::OnMainThreadTick() {
    Request req;
    while (xQueueReceive(request_queue_, &req, 0) == pdTRUE) {
        switch (req.type) {
            case RequestType::Play: {
                std::string param(req.param);
                if (param.rfind("url:", 0) == 0) {
                    HandlePlayUrl(param.substr(4), req.loop);
                } else {
                    HandlePlayFile(param, req.loop);
                }
                break;
            }
            case RequestType::Pause:  HandlePause();  break;
            case RequestType::Resume: HandleResume(); break;
            case RequestType::Stop:   HandleStop();   break;
        }
    }
}

// ========== 内部实现（在主线程调用） ==========

void MusicPlayer::HandlePlayFile(const std::string& path, bool loop) {
    // 停止当前播放
    if (play_task_) {
        stop_requested_ = true;
        // 等待旧任务结束（最多 2 秒）
        for (int i = 0; i < 200; ++i) {
            if (!play_task_) break;
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    if (DetectAudioType(path) == ESP_AUDIO_TYPE_UNSUPPORT) {
        HandleError("unsupported format");
        return;
    }

    // 检查文件存在
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

    // 同步 playlist 索引（如果当前 file 属于 playlist）
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

    // 启动后台任务：解码 + 播放
    xTaskCreate(PlayTask, "music_play", 8192, this, 5, &play_task_);
    ESP_LOGI(TAG, "PlayTask started for file: %s", path.c_str());
}

void MusicPlayer::HandlePlayUrl(const std::string& url, bool loop) {
    // 停止当前播放
    if (play_task_) {
        stop_requested_ = true;
        // 等待旧任务结束（最多 2 秒）
        for (int i = 0; i < 200; ++i) {
            if (!play_task_) break;
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    // 设置 URL 模式
    current_url_ = url;
    current_file_ = UrlToTempPath(url);   // 下载目标路径
    loop_ = loop;
    stop_requested_ = false;
    paused_ = false;
    progress_ = 0.0f;
    source_type_ = SourceType::Url;

    NotifyState(State::Loading, 0.0f);

    // 启动后台任务：下载 → 解码 → 播放（一个 task 全包）
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
    // 清空 playlist（用户主动停止 → 放弃整个列表）
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

    // 提前捕获 codec 指针；用 lambda 收尾
    auto finalize = [self]() {
        // 播放完成（非 stop）的处理
        if (!self->stop_requested_ && !self->current_playlist_.empty()) {
            // 有 playlist，尝试播放下一首
            size_t next_idx = self->current_playlist_index_ + 1;
            if (next_idx >= self->current_playlist_.size()) {
                if (self->playlist_loop_) {
                    next_idx = 0;
                } else {
                    // 列表播完
                    self->current_playlist_.clear();
                    self->current_playlist_index_ = 0;
                    self->NotifyState(State::Idle, 1.0f);
                    self->play_task_ = nullptr;
                    vTaskDelete(nullptr);
                    return;
                }
            }
            // 投递下一首到 request_queue（主线程 tick 会消费）
            Request req = {};
            req.type = RequestType::Play;
            req.loop = false;
            const std::string& next_path = self->current_playlist_[next_idx];
            snprintf(req.param, sizeof(req.param), "%s", next_path.c_str());
            req.param[sizeof(req.param) - 1] = '\0';
            if (xQueueSend(self->request_queue_, &req, 0) == pdTRUE) {
                ESP_LOGI(TAG, "Playlist: queued next track [%zu/%zu]: %s",
                         next_idx + 1, self->current_playlist_.size(), next_path.c_str());
            } else {
                ESP_LOGW(TAG, "Playlist: queue full, lost next track");
            }
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

    // 0. 如果是 URL 模式，先下载到 temp 文件
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
            ESP_LOGI(TAG, "Stop requested during download");
            finalize();
            return;
        }
        // 下载成功，切到 File 模式
        self->source_type_.store(SourceType::File);
        ESP_LOGI(TAG, "Download done, switching to File mode");
    }

    // 1. 打开文件
    FILE* fp = fopen(self->current_file_.c_str(), "rb");
    if (fp == nullptr) {
        self->HandleError("cannot open file");
        finalize();
        return;
    }
    fseek(fp, 0, SEEK_END);
    size_t full_file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    // 0.1 WAV 解析：如果文件是 WAV，先解析 RIFF 头
    //   - 找到 PCM data 偏移和 sample_rate/channels/bits
    //   - 每轮 fseek 到 data 起点
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
    // 解码范围内的"文件大小"（用于进度计算）
    size_t file_size = (type_detected == ESP_AUDIO_TYPE_PCM) ? wav_info.data_size : full_file_size;

    // 准备 buffer（loop 内外都用）
    std::vector<uint8_t> in_buf(DOWNLOAD_BUFFER_SIZE);
    std::vector<int16_t> pcm_buf(PCM_FRAME_SIZE / sizeof(int16_t));

    // 4. 主循环：循环播放（loop_=true 时从头重新播放）
    //   每轮：重建 decoder（保证状态干净）→ fseek(0) → 解码输出
    int loop_round = 0;
    esp_audio_dec_handle_t decoder = nullptr;
    while (!self->stop_requested_) {
        loop_round++;
        // 每轮重新 seek：WAV 跳到 data 起点；其它从头
        fseek(fp, data_start_offset, SEEK_SET);
        size_t total_consumed = 0;

        // 2.1 创建/重建 decoder（每轮 fresh 状态）
        esp_audio_dec_cfg_t dec_cfg = {};
        dec_cfg.type = DetectAudioType(self->current_file_);
        // PCM 类型需要传 cfg（sample_rate/channels/bits_per_sample）
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

        // 首次进入 playing 状态；后续轮只重置 progress
        if (loop_round == 1) {
            self->NotifyState(State::Playing, 0.0f);
        } else {
            self->progress_.store(0.0f);
        }
        uint32_t last_progress_notify = 0;

        bool eof_reached = false;
        while (!self->stop_requested_ && !eof_reached) {
            // 暂停循环
            while (self->paused_ && !self->stop_requested_) {
                vTaskDelay(pdMS_TO_TICKS(50));
            }
            if (self->stop_requested_) break;

            // 读一段 encoded data
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

                // 输出 PCM 到 I2S
                if (out_frame.decoded_size > 0) {
                    pcm_buf.resize(out_frame.decoded_size / sizeof(int16_t));
                    codec->OutputData(pcm_buf);
                }

                // 进度通知
                uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
                if (now - last_progress_notify >= PROGRESS_NOTIFY_MS) {
                    last_progress_notify = now;
                    float prog = (file_size > 0) ? (float)total_consumed / file_size : 0.0f;
                    self->progress_.store(prog);
                }
            }
        }

        // 每轮结束关闭 decoder
        esp_audio_dec_close(decoder);

        // EOF 后判断是否循环
        if (self->stop_requested_) break;
        if (!self->loop_) break;
        ESP_LOGI(TAG, "Loop round %d complete, restarting", loop_round);
    }

    // 5. 清理
    esp_audio_dec_close(decoder);
    fclose(fp);
    finalize();
}
