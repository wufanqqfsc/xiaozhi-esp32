#include "drum/drum_synth.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "application.h"
#include "audio/audio_service.h"
#include "drum/drum_assets.h"

#define TAG "DrumSynth"

namespace drum {

DrumSynth& DrumSynth::GetInstance() {
    static DrumSynth instance;
    return instance;
}

void DrumSynth::Init() {
    if (initialized_) {
        return;
    }
    initialized_ = true;
    ESP_LOGI(TAG, "DrumSynth initialized: %d pieces, total %u bytes",
             static_cast<int>(Piece::COUNT),
             (unsigned)(kKICK_ogg.size() + kSNARE_ogg.size() + kHIHAT_CLOSED_ogg.size() +
                        kHIHAT_OPEN_ogg.size() + kTOM_HI_ogg.size() + kTOM_MID_ogg.size() +
                        kCRASH_ogg.size() + kRIDE_ogg.size()));
}

bool DrumSynth::EnsureOggAvailable(Piece piece) {
    if (!active_) {
        ESP_LOGD(TAG, "Trigger ignored: drum mode inactive");
        return false;
    }
    // 仅 Idle 态允许触发（不打断语音/连接/激活）
    auto& app = Application::GetInstance();
    if (app.GetDeviceState() != kDeviceStateIdle) {
        ESP_LOGD(TAG, "Trigger ignored: device busy (state=%d)",
                 static_cast<int>(app.GetDeviceState()));
        return false;
    }
    int idx = static_cast<int>(piece);
    if (idx < 0 || idx >= static_cast<int>(Piece::COUNT)) {
        return false;
    }
    return true;
}

bool DrumSynth::Trigger(Piece piece, uint8_t velocity) {
    (void)velocity;  // 预留
    if (!initialized_) {
        return false;
    }
    if (!EnsureOggAvailable(piece)) {
        return false;
    }

    int idx = static_cast<int>(piece);
    // 复用 AudioService::PlaySound：自动完成 OggDemux → OpusDecode → PlaybackQueue
    // 鼓声样本在 drum_assets.h 中以 string_view 形式嵌入
    auto& app = Application::GetInstance();
    auto& audio = app.GetAudioService();
    audio.PlaySound(kPieceOgg[idx]);

    ESP_LOGD(TAG, "Trigger piece=%d (%s)", idx, kPieceName[idx]);
    // 触发 UI 视觉反馈（异步通知主循环，避免在 LVGL 任务外操作 UI）
    CallTriggerCallback(piece);
    return true;
}

void DrumSynth::StopAll() {
    if (!initialized_) {
        return;
    }
    auto& audio = Application::GetInstance().GetAudioService();
    audio.WaitForPlaybackQueueEmpty();
    ESP_LOGD(TAG, "StopAll: drained playback queue");
}

void DrumSynth::SetActive(bool active) {
    if (active_ == active) {
        return;
    }
    active_ = active;
    ESP_LOGI(TAG, "Active -> %d", active ? 1 : 0);
    if (!active) {
        StopAll();
    }
}

void DrumSynth::CallTriggerCallback(Piece p) {
    if (trigger_callback_) {
        // 直接在调用线程触发，UI 任务会处理（LVGL 内部已加锁）
        trigger_callback_(p);
    }
}

}  // namespace drum
