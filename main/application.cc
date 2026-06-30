#include "application.h"
#include "board.h"
#include "display.h"
#include "attitude_display.h"
#include "system_info.h"
#include "audio_codec.h"
#include "mqtt_protocol.h"
#include "websocket_protocol.h"
#include "assets/lang_config.h"
#include "mcp_server.h"
#include "assets.h"
#include "settings.h"
#include "sdcard_log_http.h"
#include "lvgl_image.h"

#include <cstring>
#include <esp_log.h>
#include <cJSON.h>
#include <driver/gpio.h>
#include <arpa/inet.h>
#include <font_awesome.h>
#include <wifi_manager.h>

#if CONFIG_XIAOZHI_ENABLE_BLE_FISHEYE
#include "ble/ble_server.h"
#endif

#define TAG "Application"

namespace {

AttitudeDisplay* GetAttitudeDisplay()
{
    return dynamic_cast<AttitudeDisplay*>(Board::GetInstance().GetDisplay());
}

// 去除 LLM 输出中的 <think>...</think> 推理块
// - 支持多个块、按出现顺序剔除
// - 若只有开始标签（流式截断），则丢弃该标签到末尾的所有内容
// - 同步处理英文小写 "think" 与首字母大写 "Think"，并兼容 "</think>" 不带斜杠的写法
std::string StripThinkBlocks(const std::string& text)
{
    static const char* kStartTag = "<think>";
    static const char* kEndTag   = "</think>";
    const size_t start_len = strlen(kStartTag);
    const size_t end_len   = strlen(kEndTag);

    std::string out;
    out.reserve(text.size());

    size_t pos = 0;
    while (pos < text.size()) {
        // 不区分大小写查找开始标签
        size_t start = std::string::npos;
        for (size_t i = pos; i + start_len <= text.size(); ++i) {
            bool match = true;
            for (size_t k = 0; k < start_len; ++k) {
                char c = text[i + k];
                char s = kStartTag[k];
                if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
                if (s >= 'A' && s <= 'Z') s = s - 'A' + 'a';
                if (c != s) { match = false; break; }
            }
            if (match) { start = i; break; }
        }

        if (start == std::string::npos) {
            out.append(text, pos, std::string::npos);
            break;
        }

        out.append(text, pos, start - pos);
        size_t end = text.find(kEndTag, start + start_len);
        if (end == std::string::npos) {
            // 仅有开始标签（流式截断），丢弃到末尾
            break;
        }
        pos = end + end_len;
    }

    // 去除首尾空白字符
    auto is_ws = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    while (!out.empty() && is_ws((unsigned char)out.front())) out.erase(out.begin());
    while (!out.empty() && is_ws((unsigned char)out.back()))  out.pop_back();
    return out;
}

WifiStatus NetworkEventToWifiFisheyeStatus(NetworkEvent event)
{
    switch (event) {
    case NetworkEvent::Connecting:
        return WifiStatus::CONNECTING;
    case NetworkEvent::Connected:
        return WifiStatus::CONNECTED;
    default:
        return WifiStatus::DISCONNECTED;
    }
}

void ApplyWifiFisheyeStatus(WifiStatus status)
{
    if (auto* attitude = GetAttitudeDisplay()) {
        attitude->UpdateWifiFisheye(status);
    }
}

void ApplyBleFisheyeStatus(BleStatus status)
{
    if (auto* attitude = GetAttitudeDisplay()) {
        attitude->UpdateBleFisheye(status);
    }
}

void SyncWifiFisheyeFromNetwork()
{
    WifiStatus status = WifiStatus::DISCONNECTED;
    auto& wifi = WifiManager::GetInstance();
    if (wifi.IsInitialized() && wifi.IsConnected()) {
        status = WifiStatus::CONNECTED;
    }
    ApplyWifiFisheyeStatus(status);
}

}  // namespace


Application::Application() {
    event_group_ = xEventGroupCreate();

#if CONFIG_USE_DEVICE_AEC && CONFIG_USE_SERVER_AEC
#error "CONFIG_USE_DEVICE_AEC and CONFIG_USE_SERVER_AEC cannot be enabled at the same time"
#elif CONFIG_USE_DEVICE_AEC
    aec_mode_ = kAecOnDeviceSide;
#elif CONFIG_USE_SERVER_AEC
    aec_mode_ = kAecOnServerSide;
#else
    aec_mode_ = kAecOff;
#endif

    esp_timer_create_args_t clock_timer_args = {
        .callback = [](void* arg) {
            Application* app = (Application*)arg;
            xEventGroupSetBits(app->event_group_, MAIN_EVENT_CLOCK_TICK);
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "clock_timer",
        .skip_unhandled_events = true
    };
    esp_timer_create(&clock_timer_args, &clock_timer_handle_);
}

Application::~Application() {
    if (clock_timer_handle_ != nullptr) {
        esp_timer_stop(clock_timer_handle_);
        esp_timer_delete(clock_timer_handle_);
    }
    vEventGroupDelete(event_group_);
}

bool Application::SetDeviceState(DeviceState state) {
    return state_machine_.TransitionTo(state);
}

void Application::Initialize() {
    auto& board = Board::GetInstance();
    SetDeviceState(kDeviceStateStarting);

    // Setup the display
    auto display = board.GetDisplay();
    display->SetupUI();
    // Print board name/version info
    display->SetChatMessage("system", SystemInfo::GetUserAgent().c_str());

    // Setup the audio service
    auto codec = board.GetAudioCodec();
    audio_service_.Initialize(codec);
    audio_service_.Start();

    AudioServiceCallbacks callbacks;
    callbacks.on_send_queue_available = [this]() {
        xEventGroupSetBits(event_group_, MAIN_EVENT_SEND_AUDIO);
    };
    callbacks.on_wake_word_detected = [this](const std::string& wake_word) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_WAKE_WORD_DETECTED);
    };
    callbacks.on_vad_change = [this](bool speaking) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_VAD_CHANGE);
    };
    // 播放队列清空回调：勿在此隐藏提示卡——UI 短音效（POPUP/SUCCESS）结束会误关运势功能卡
    callbacks.on_playback_finished = nullptr;
    audio_service_.SetCallbacks(callbacks);

    // Add state change listeners
    state_machine_.AddStateChangeListener([this](DeviceState old_state, DeviceState new_state) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_STATE_CHANGED);
    });

    // Start the clock timer to update the status bar
    esp_timer_start_periodic(clock_timer_handle_, 1000000);

    // Add MCP common tools (only once during initialization)
    auto& mcp_server = McpServer::GetInstance();
    mcp_server.AddCommonTools();
    mcp_server.AddUserOnlyTools();

    // Set network event callback for UI updates and network state handling
    board.SetNetworkEventCallback([this](NetworkEvent event, const std::string& data) {
        auto display = Board::GetInstance().GetDisplay();
        
        switch (event) {
            case NetworkEvent::Scanning:
                display->ShowNotification(Lang::Strings::SCANNING_WIFI, 30000);
                xEventGroupSetBits(event_group_, MAIN_EVENT_NETWORK_DISCONNECTED);
                break;
            case NetworkEvent::Connecting: {
                if (data.empty()) {
                    // Cellular network - registering without carrier info yet
                    display->SetStatus(Lang::Strings::REGISTERING_NETWORK);
                } else {
                    // WiFi or cellular with carrier info
                    std::string msg = Lang::Strings::CONNECT_TO;
                    msg += data;
                    msg += "...";
                    display->ShowNotification(msg.c_str(), 30000);
                }
                break;
            }
            case NetworkEvent::Connected: {
                // 配网成功（WiFi 拿到 IP）：提示卡仅在每次断线→重连后显示一次
                internet_failed_shown_ = false;
                std::string msg = Lang::Strings::CONNECTED_TO;
                msg += data;
                display->ShowNotification(msg.c_str(), 30000);
                xEventGroupSetBits(event_group_, MAIN_EVENT_NETWORK_CONNECTED);
                if (!wifi_connected_debug_shown_ && GetAttitudeDisplay() != nullptr) {
                    wifi_connected_debug_shown_ = true;
                    std::string detail = data;
                    if (detail.size() > 24) detail = detail.substr(0, 24) + "...";
                    Schedule([this, detail]() {
                        if (auto* attitude = GetAttitudeDisplay()) {
                            attitude->ShowDebugInfo("WiFi 已连接", detail, 5000);
                            audio_service_.PlaySound(Lang::Sounds::OGG_SUCCESS);
                        }
                    });
                }
                {
                    std::string tts_text = std::string("WiFi 已连接：") + data;
                    RequestDebugTts(tts_text);
                }
                break;
            }
            case NetworkEvent::Disconnected:
                wifi_connected_debug_shown_ = false;
                internet_failed_shown_ = false;
                xEventGroupSetBits(event_group_, MAIN_EVENT_NETWORK_DISCONNECTED);
                break;
            case NetworkEvent::WifiConfigModeEnter:
                // WiFi config mode enter is handled by WifiBoard internally
                break;
            case NetworkEvent::WifiConfigModeExit:
                // WiFi config mode exit is handled by WifiBoard internally
                break;
            // Cellular modem specific events
            case NetworkEvent::ModemDetecting:
                display->SetStatus(Lang::Strings::DETECTING_MODULE);
                break;
            case NetworkEvent::ModemErrorNoSim:
                Alert(Lang::Strings::ERROR, Lang::Strings::PIN_ERROR, "triangle_exclamation", Lang::Sounds::OGG_ERR_PIN);
                break;
            case NetworkEvent::ModemErrorRegDenied:
                Alert(Lang::Strings::ERROR, Lang::Strings::REG_ERROR, "triangle_exclamation", Lang::Sounds::OGG_ERR_REG);
                break;
            case NetworkEvent::ModemErrorInitFailed:
                Alert(Lang::Strings::ERROR, Lang::Strings::MODEM_INIT_ERROR, "triangle_exclamation", Lang::Sounds::OGG_EXCLAMATION);
                break;
            case NetworkEvent::ModemErrorTimeout:
                display->SetStatus(Lang::Strings::REGISTERING_NETWORK);
                break;
        }

        // 迭代 3: WiFi 网络事件 → 鱼眼状态（与 MCP 手动切换可共存）
        const WifiStatus wifi_fisheye_status = NetworkEventToWifiFisheyeStatus(event);
        Schedule([wifi_fisheye_status]() {
            ApplyWifiFisheyeStatus(wifi_fisheye_status);
        });
    });

#if CONFIG_XIAOZHI_ENABLE_BLE_FISHEYE
    BleServer::GetInstance().SetStatusCallback([this](BleStatus status) {
        Schedule([status]() {
            ApplyBleFisheyeStatus(status);
        });
    });
    // BLE 在 StartNetwork 完成后由 WifiBoard::OnNetworkEvent(Connected) 启动，
    // 或者在 StartWifiConfigMode 时暂停。
    // 注意：如果要开机即启 BLE，需确保 WiFi 初始化成功，否则会因内存不足导致 ESP_ERR_NO_MEM。
#endif

    // Start network asynchronously
    board.StartNetwork();

    // 启动时根据当前 WiFi 状态初始化鱼眼
    Schedule([]() {
        SyncWifiFisheyeFromNetwork();
    });

    // Update the status bar immediately to show the network state
    display->UpdateStatusBar(true);
}

void Application::Run() {
    // Set the priority of the main task to 10
    vTaskPrioritySet(nullptr, 10);

    const EventBits_t ALL_EVENTS = 
        MAIN_EVENT_SCHEDULE |
        MAIN_EVENT_SEND_AUDIO |
        MAIN_EVENT_WAKE_WORD_DETECTED |
        MAIN_EVENT_VAD_CHANGE |
        MAIN_EVENT_CLOCK_TICK |
        MAIN_EVENT_ERROR |
        MAIN_EVENT_NETWORK_CONNECTED |
        MAIN_EVENT_NETWORK_DISCONNECTED |
        MAIN_EVENT_TOGGLE_CHAT |
        MAIN_EVENT_START_LISTENING |
        MAIN_EVENT_STOP_LISTENING |
        MAIN_EVENT_ACTIVATION_DONE |
        MAIN_EVENT_STATE_CHANGED;

    while (true) {
        auto bits = xEventGroupWaitBits(event_group_, ALL_EVENTS, pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & MAIN_EVENT_ERROR) {
            SetDeviceState(kDeviceStateIdle);
            Alert(Lang::Strings::ERROR, last_error_message_.c_str(), "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
        }

        if (bits & MAIN_EVENT_NETWORK_CONNECTED) {
            HandleNetworkConnectedEvent();
        }

        if (bits & MAIN_EVENT_NETWORK_DISCONNECTED) {
            HandleNetworkDisconnectedEvent();
        }

        if (bits & MAIN_EVENT_ACTIVATION_DONE) {
            HandleActivationDoneEvent();
        }

        if (bits & MAIN_EVENT_STATE_CHANGED) {
            HandleStateChangedEvent();
        }

        if (bits & MAIN_EVENT_TOGGLE_CHAT) {
            HandleToggleChatEvent();
        }

        if (bits & MAIN_EVENT_START_LISTENING) {
            HandleStartListeningEvent();
        }

        if (bits & MAIN_EVENT_STOP_LISTENING) {
            HandleStopListeningEvent();
        }

        if (bits & MAIN_EVENT_SEND_AUDIO) {
            while (auto packet = audio_service_.PopPacketFromSendQueue()) {
                if (protocol_ && !protocol_->SendAudio(std::move(packet))) {
                    break;
                }
            }
        }

        if (bits & MAIN_EVENT_WAKE_WORD_DETECTED) {
            HandleWakeWordDetectedEvent();
        }

        if (bits & MAIN_EVENT_VAD_CHANGE) {
            if (GetDeviceState() == kDeviceStateListening) {
                auto led = Board::GetInstance().GetLed();
                led->OnStateChanged();
                // VAD 检测到用户说话：刷新"唤醒成功"调试卡计时器，保持显示
                if (audio_service_.IsVoiceDetected()) {
                    if (auto* attitude = GetAttitudeDisplay()) {
                        attitude->RefreshDebugInfoTimer(30000);
                    }
                }
            }
        }

        if (bits & MAIN_EVENT_SCHEDULE) {
            std::unique_lock<std::mutex> lock(mutex_);
            auto tasks = std::move(main_tasks_);
            lock.unlock();
            for (auto& task : tasks) {
                task();
            }
        }

        if (bits & MAIN_EVENT_CLOCK_TICK) {
            clock_ticks_++;
            auto display = Board::GetInstance().GetDisplay();
            display->UpdateStatusBar();

            // Listening 超时保护：进入 listening 状态后 5s 内若没有 TTS 响应
            // （服务端 STT 持续超时或网络异常），自动回 Idle 并恢复唤醒词监听
            constexpr int LISTENING_TIMEOUT_SEC = 5;
            DeviceState curr_state = GetDeviceState();
            if (curr_state == kDeviceStateListening) {
                // 诊断日志：每 5 个 tick（5s）在 listening 时打印状态
                if (clock_ticks_ % 5 == 0) {
                    ESP_LOGI(TAG, "[LISTEN-DBG] state=listening clock=%d started=%d diff=%d timeout=%d",
                             clock_ticks_, listening_started_ticks_,
                             clock_ticks_ - listening_started_ticks_, LISTENING_TIMEOUT_SEC);
                }
                if (listening_started_ticks_ > 0 &&
                    clock_ticks_ - listening_started_ticks_ > LISTENING_TIMEOUT_SEC) {
                    ESP_LOGW(TAG, "Listening timeout (no TTS response in %ds), back to idle",
                             LISTENING_TIMEOUT_SEC);
                    listening_started_ticks_ = 0;
                    SetDeviceState(kDeviceStateIdle);
                    audio_service_.EnableWakeWordDetection(true);
                }
            }

            // Print debug info every 10 seconds
            if (clock_ticks_ % 10 == 0) {
                SystemInfo::PrintHeapStats();
            }
        }
    }
}

void Application::HandleNetworkConnectedEvent() {
    ESP_LOGI(TAG, "Network connected");
    auto state = GetDeviceState();

    if (!SdCardLogHttpIsRunning()) {
        struct stat st;
        if (stat("/sdcard", &st) == 0 && S_ISDIR(st.st_mode)) {
            SdCardLogHttpStart("/sdcard", 8080);
        } else {
            SdCardLogHttpStart("/tmp", 8080);
        }
    }

    if (state == kDeviceStateStarting || state == kDeviceStateWifiConfiguring) {
        // Network is ready, start activation
        SetDeviceState(kDeviceStateActivating);
        if (activation_task_handle_ != nullptr) {
            ESP_LOGW(TAG, "Activation task already running");
            return;
        }

        xTaskCreate([](void* arg) {
            Application* app = static_cast<Application*>(arg);
            app->ActivationTask();
            app->activation_task_handle_ = nullptr;
            vTaskDelete(NULL);
        }, "activation", 4096 * 2, this, 2, &activation_task_handle_);
    }

    // Update the status bar immediately to show the network state
    auto display = Board::GetInstance().GetDisplay();
    display->UpdateStatusBar(true);
}

void Application::HandleNetworkDisconnectedEvent() {
    // Close current conversation when network disconnected
    auto state = GetDeviceState();
    if (state == kDeviceStateConnecting || state == kDeviceStateListening || state == kDeviceStateSpeaking) {
        ESP_LOGI(TAG, "Closing audio channel due to network disconnection");
        protocol_->CloseAudioChannel();
    }

    // 网络断开时停止 SD 卡日志 HTTP 服务
    if (SdCardLogHttpIsRunning()) {
        SdCardLogHttpStop();
    }

    // Update the status bar immediately to show the network state
    auto display = Board::GetInstance().GetDisplay();
    display->UpdateStatusBar(true);
}

void Application::HandleActivationDoneEvent() {
    ESP_LOGI(TAG, "Activation done");

    SystemInfo::PrintHeapStats();
    SetDeviceState(kDeviceStateIdle);

    has_server_time_ = ota_->HasServerTime();

    auto display = Board::GetInstance().GetDisplay();
    std::string message = std::string(Lang::Strings::VERSION) + ota_->GetCurrentVersion();
    display->ShowNotification(message.c_str());
    display->SetChatMessage("system", "");

    // Release OTA object after activation is complete
    ota_.reset();
    auto& board = Board::GetInstance();
    board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);

    Schedule([this]() {
        PlayUiSound(Lang::Sounds::OGG_SUCCESS);
    });
}

void Application::ActivationTask() {
    // Create OTA object for activation process
    ota_ = std::make_unique<Ota>();

    // Check for new assets version
    CheckAssetsVersion();

    // Check for new firmware version
    CheckNewVersion();

    // Initialize the protocol
    InitializeProtocol();

    // Signal completion to main loop
    xEventGroupSetBits(event_group_, MAIN_EVENT_ACTIVATION_DONE);
}

void Application::CheckAssetsVersion() {
    // Only allow CheckAssetsVersion to be called once
    if (assets_version_checked_) {
        return;
    }
    assets_version_checked_ = true;

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto& assets = Assets::GetInstance();

    if (!assets.partition_valid()) {
        ESP_LOGW(TAG, "Assets partition is disabled for board %s", BOARD_NAME);
        return;
    }
    
    Settings settings("assets", true);
    // Check if there is a new assets need to be downloaded
    std::string download_url = settings.GetString("download_url");

    if (!download_url.empty()) {
        settings.EraseKey("download_url");

        char message[256];
        snprintf(message, sizeof(message), Lang::Strings::FOUND_NEW_ASSETS, download_url.c_str());
        Alert(Lang::Strings::LOADING_ASSETS, message, "cloud_arrow_down", Lang::Sounds::OGG_UPGRADE);
        
        // Wait for the audio service to be idle for 3 seconds
        vTaskDelay(pdMS_TO_TICKS(3000));
        SetDeviceState(kDeviceStateUpgrading);
        board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
        display->SetChatMessage("system", Lang::Strings::PLEASE_WAIT);

        bool success = assets.Download(download_url, [this, display](int progress, size_t speed) -> void {
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, speed / 1024);
            Schedule([display, message = std::string(buffer)]() {
                display->SetChatMessage("system", message.c_str());
            });
        });

        board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (!success) {
            Alert(Lang::Strings::ERROR, Lang::Strings::DOWNLOAD_ASSETS_FAILED, "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
            vTaskDelay(pdMS_TO_TICKS(2000));
            SetDeviceState(kDeviceStateActivating);
            return;
        }
    }

    // Apply assets
    assets.Apply();
    display->SetChatMessage("system", "");
    display->SetEmotion("microchip_ai");
}

void Application::CheckNewVersion() {
    const int MAX_RETRY = 10;
    int retry_count = 0;
    int retry_delay = 10; // Initial retry delay in seconds

    auto& board = Board::GetInstance();
    while (true) {
        auto display = board.GetDisplay();
        display->SetStatus(Lang::Strings::CHECKING_NEW_VERSION);

        esp_err_t err = ota_->CheckVersion();
        if (err != ESP_OK) {
            retry_count++;
            // WiFi 已拿到 IP，但 OTA/服务端不可达 → 弹"联网失败"提示卡（仅一次）
            // 优先使用 Ota 上一次失败原因（细分到 HTTP_OPEN_FAIL / STATUS xxx / URL_INVALID / JSON_PARSE_FAIL）
            const std::string& reason = ota_->GetLastErrorMessage();
            if (!reason.empty()) {
                ShowInternetFailedNotification(reason.c_str());
            } else {
                char fb_reason[40];
                snprintf(fb_reason, sizeof(fb_reason), "ERR 0x%x", err);
                ShowInternetFailedNotification(fb_reason);
            }
            if (retry_count >= MAX_RETRY) {
                ESP_LOGE(TAG, "Too many retries, exit version check");
                return;
            }

            char error_message[128];
            snprintf(error_message, sizeof(error_message), "code=%d, url=%s", err, ota_->GetCheckVersionUrl().c_str());
            char buffer[256];
            snprintf(buffer, sizeof(buffer), Lang::Strings::CHECK_NEW_VERSION_FAILED, retry_delay, error_message);
            Alert(Lang::Strings::ERROR, buffer, "cloud_slash", Lang::Sounds::OGG_EXCLAMATION);

            ESP_LOGW(TAG, "Check new version failed, retry in %d seconds (%d/%d)", retry_delay, retry_count, MAX_RETRY);
            for (int i = 0; i < retry_delay; i++) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                if (GetDeviceState() == kDeviceStateIdle) {
                    break;
                }
            }
            retry_delay *= 2; // Double the retry delay
            continue;
        }
        retry_count = 0;
        retry_delay = 10; // Reset retry delay

        if (ota_->HasNewVersion()) {
            if (UpgradeFirmware(ota_->GetFirmwareUrl(), ota_->GetFirmwareVersion())) {
                return; // This line will never be reached after reboot
            }
            // If upgrade failed, continue to normal operation
        }

        // No new version, mark the current version as valid
        ota_->MarkCurrentVersionValid();
        if (!ota_->HasActivationCode() && !ota_->HasActivationChallenge()) {
            // Exit the loop if done checking new version
            break;
        }

        display->SetStatus(Lang::Strings::ACTIVATION);
        // Activation code is shown to the user and waiting for the user to input
        if (ota_->HasActivationCode()) {
            ShowActivationCode(ota_->GetActivationCode(), ota_->GetActivationMessage());
        }

        // This will block the loop until the activation is done or timeout
        for (int i = 0; i < 10; ++i) {
            ESP_LOGI(TAG, "Activating... %d/%d", i + 1, 10);
            esp_err_t err = ota_->Activate();
            if (err == ESP_OK) {
                break;
            } else if (err == ESP_ERR_TIMEOUT) {
                vTaskDelay(pdMS_TO_TICKS(3000));
            } else {
                vTaskDelay(pdMS_TO_TICKS(10000));
            }
            if (GetDeviceState() == kDeviceStateIdle) {
                break;
            }
        }
    }
}

void Application::InitializeProtocol() {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto codec = board.GetAudioCodec();

    display->SetStatus(Lang::Strings::LOADING_PROTOCOL);

    if (ota_->HasMqttConfig()) {
        protocol_ = std::make_unique<MqttProtocol>();
    } else if (ota_->HasWebsocketConfig()) {
        protocol_ = std::make_unique<WebsocketProtocol>();
    } else {
        ESP_LOGW(TAG, "No protocol specified in the OTA config, using MQTT");
        protocol_ = std::make_unique<MqttProtocol>();
    }

    protocol_->OnConnected([this]() {
        DismissAlert();
    });

    protocol_->OnNetworkError([this](const std::string& message) {
        last_error_message_ = message;
        xEventGroupSetBits(event_group_, MAIN_EVENT_ERROR);
    });
    
    protocol_->OnIncomingAudio([this](std::unique_ptr<AudioStreamPacket> packet) {
        audio_service_.PushPacketToDecodeQueue(std::move(packet));
    });
    
    protocol_->OnAudioChannelOpened([this, codec, &board]() {
        board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
        if (protocol_->server_sample_rate() != codec->output_sample_rate()) {
            ESP_LOGW(TAG, "Server sample rate %d does not match device output sample rate %d, resampling may cause distortion",
                protocol_->server_sample_rate(), codec->output_sample_rate());
        }
        // 调试卡 + 音频播报：WebSocket 握手成功，音频通道已建立
        if (auto* attitude = GetAttitudeDisplay()) {
            char detail[32];
            snprintf(detail, sizeof(detail), "SR=%dHz", protocol_->server_sample_rate());
            Schedule([this, attitude, detail]() {
                // 短促本地提示音 + 显示卡
                attitude->ShowDebugInfo("握手成功", detail, 5000);
                audio_service_.PlaySound(Lang::Sounds::OGG_POPUP);
            });
        }
        // 服务端 TTS 朗读（已存在通道，复用）
        {
            char tts_text[64];
            snprintf(tts_text, sizeof(tts_text), "握手成功，采样率 %d 赫兹",
                     protocol_->server_sample_rate());
            RequestDebugTts(tts_text);
        }
    });
    
    protocol_->OnAudioChannelClosed([this, &board]() {
        board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
        Schedule([this]() {
            auto display = Board::GetInstance().GetDisplay();
            display->SetChatMessage("system", "");
            SetDeviceState(kDeviceStateIdle);
        });
    });
    
    protocol_->OnIncomingJson([this, display](const cJSON* root) {
        // Parse JSON data
        auto type = cJSON_GetObjectItem(root, "type");
        if (strcmp(type->valuestring, "tts") == 0) {
            auto state = cJSON_GetObjectItem(root, "state");
            if (strcmp(state->valuestring, "start") == 0) {
            Schedule([this]() {
                aborted_ = false;
                // TTS 开始时自动把音量调到最大，保证回放声音足够响
                Board::GetInstance().GetAudioCodec()->SetOutputVolume(100);
                // 若"唤醒成功"调试卡仍可见，重置其隐藏计时器（持续显示）
                if (auto* attitude = GetAttitudeDisplay()) {
                    attitude->RefreshDebugInfoTimer(30000);
                }
                // TTS 响应到达，清除 listening 超时监控
                listening_started_ticks_ = 0;
                SetDeviceState(kDeviceStateSpeaking);
            });
        } else if (strcmp(state->valuestring, "stop") == 0) {
                Schedule([this]() {
                    if (GetDeviceState() == kDeviceStateSpeaking) {
                        if (listening_mode_ == kListeningModeManualStop) {
                            SetDeviceState(kDeviceStateIdle);
                            listening_started_ticks_ = 0;
                        } else {
                            SetDeviceState(kDeviceStateListening);
                            // 进入 listening，启动超时监控（30s 内若无新 TTS 则回 idle）
                            listening_started_ticks_ = clock_ticks_;
                        }
                    }
                    // TTS 完整播放结束：隐藏"识别到"/"AI 回复"调试卡
                    // （这是用户要求的：回复完毕之后才消失）
                    if (auto* attitude = GetAttitudeDisplay()) {
                        attitude->HideDebugInfo();
                    }
                });
            } else if (strcmp(state->valuestring, "sentence_start") == 0) {
                auto text = cJSON_GetObjectItem(root, "text");
                if (cJSON_IsString(text)) {
                    std::string raw(text->valuestring);
                    ESP_LOGI(TAG, "<< %s", raw.c_str());
                    // 过滤掉 <think>...</think> 等推理块，避免在屏幕上泄漏给用户
                    std::string cleaned = StripThinkBlocks(raw);
                    Schedule([display, message = cleaned]() {
                        display->SetChatMessage("assistant", message.c_str());
                    });
                    // 调试卡：完整显示 LLM 回复文本（已被 LVGL 自动换行至 5 行）
                    // 标题用单调递增序号绕过 ShowDebugInfo 的 1.5s 同标题去重
                    // 让流式到达的每一句都能被看到
                    if (!cleaned.empty()) {
                        if (auto* attitude = GetAttitudeDisplay()) {
                            static uint32_t s_llm_seq = 0;
                            uint32_t seq = ++s_llm_seq;
                            Schedule([attitude, cleaned, seq]() {
                                char title[24];
                                snprintf(title, sizeof(title), "AI 回复 #%u", (unsigned)seq);
                                // hold_ms 较长以覆盖整段朗读
                                attitude->ShowDebugInfo(title, cleaned, 8000);
                            });
                        }
                    }
                }
            }
        } else if (strcmp(type->valuestring, "stt") == 0) {
            auto text = cJSON_GetObjectItem(root, "text");
            if (cJSON_IsString(text)) {
                ESP_LOGI(TAG, ">> %s", text->valuestring);
                Schedule([display, message = std::string(text->valuestring)]() {
                    display->SetChatMessage("user", message.c_str());
                });
                // 调试卡：显示 ASR 识别结果。
                // 不在这里 HideDebugInfo，改为：tts:start 重置计时器，tts:stop 才隐藏。
                // 这样设备会一直展示用户的话，直到 LLM 完整回答完。
                if (auto* attitude = GetAttitudeDisplay()) {
                    std::string preview = text->valuestring;
                    if (preview.size() > 40) preview = preview.substr(0, 40) + "...";
                    Schedule([attitude, preview]() {
                        attitude->ShowDebugInfo("识别到", preview, 5000);
                    });
                }
            }
        } else if (strcmp(type->valuestring, "llm") == 0) {
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(emotion)) {
                Schedule([display, emotion_str = std::string(emotion->valuestring)]() {
                    display->SetEmotion(emotion_str.c_str());
                });
            }
        } else if (strcmp(type->valuestring, "mcp") == 0) {
            auto payload = cJSON_GetObjectItem(root, "payload");
            if (cJSON_IsObject(payload)) {
                McpServer::GetInstance().ParseMessage(payload);
                // 调试卡：MCP 工具调用
                if (auto* attitude = GetAttitudeDisplay()) {
                    auto method = cJSON_GetObjectItem(payload, "method");
                    if (cJSON_IsString(method)) {
                        std::string method_str = method->valuestring;
                        if (method_str.size() > 30) method_str = method_str.substr(0, 30) + "...";
                        Schedule([attitude, method_str]() {
                            attitude->ShowDebugInfo("工具调用", method_str, 2500);
                        });
                    }
                }
            }
        } else if (strcmp(type->valuestring, "system") == 0) {
            auto command = cJSON_GetObjectItem(root, "command");
            if (cJSON_IsString(command)) {
                ESP_LOGI(TAG, "System command: %s", command->valuestring);
                if (strcmp(command->valuestring, "reboot") == 0) {
                    // Do a reboot if user requests a OTA update
                    Schedule([this]() {
                        Reboot();
                    });
                } else {
                    ESP_LOGW(TAG, "Unknown system command: %s", command->valuestring);
                }
            }
        } else if (strcmp(type->valuestring, "alert") == 0) {
            auto status = cJSON_GetObjectItem(root, "status");
            auto message = cJSON_GetObjectItem(root, "message");
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(status) && cJSON_IsString(message) && cJSON_IsString(emotion)) {
                Alert(status->valuestring, message->valuestring, emotion->valuestring, Lang::Sounds::OGG_VIBRATION);
            } else {
                ESP_LOGW(TAG, "Alert command requires status, message and emotion");
            }
#if CONFIG_RECEIVE_CUSTOM_MESSAGE
        } else if (strcmp(type->valuestring, "custom") == 0) {
            auto payload = cJSON_GetObjectItem(root, "payload");
            ESP_LOGI(TAG, "Received custom message: %s", cJSON_PrintUnformatted(root));
            if (cJSON_IsObject(payload)) {
                Schedule([this, display, payload_str = std::string(cJSON_PrintUnformatted(payload))]() {
                    display->SetChatMessage("system", payload_str.c_str());
                });
            } else {
                ESP_LOGW(TAG, "Invalid custom message format: missing payload");
            }
#endif
        } else {
            ESP_LOGW(TAG, "Unknown message type: %s", type->valuestring);
        }
    });
    
    protocol_->Start();
}

void Application::ShowActivationCode(const std::string& code, const std::string& message) {
    struct digit_sound {
        char digit;
        const std::string_view& sound;
    };
    static const std::array<digit_sound, 10> digit_sounds{{
        digit_sound{'0', Lang::Sounds::OGG_0},
        digit_sound{'1', Lang::Sounds::OGG_1}, 
        digit_sound{'2', Lang::Sounds::OGG_2},
        digit_sound{'3', Lang::Sounds::OGG_3},
        digit_sound{'4', Lang::Sounds::OGG_4},
        digit_sound{'5', Lang::Sounds::OGG_5},
        digit_sound{'6', Lang::Sounds::OGG_6},
        digit_sound{'7', Lang::Sounds::OGG_7},
        digit_sound{'8', Lang::Sounds::OGG_8},
        digit_sound{'9', Lang::Sounds::OGG_9}
    }};

    // This sentence uses 9KB of SRAM, so we need to wait for it to finish
    Alert(Lang::Strings::ACTIVATION, message.c_str(), "link", Lang::Sounds::OGG_ACTIVATION);

    for (const auto& digit : code) {
        auto it = std::find_if(digit_sounds.begin(), digit_sounds.end(),
            [digit](const digit_sound& ds) { return ds.digit == digit; });
        if (it != digit_sounds.end()) {
            audio_service_.PlaySound(it->sound);
        }
    }
}

void Application::Alert(const char* status, const char* message, const char* emotion, const std::string_view& sound) {
    ESP_LOGW(TAG, "Alert [%s] %s: %s", emotion, status, message);
    auto display = Board::GetInstance().GetDisplay();
    display->SetStatus(status);
    display->SetEmotion(emotion);
    display->SetChatMessage("system", message);
    if (!sound.empty()) {
        audio_service_.PlaySound(sound);
    }
}

void Application::DismissAlert() {
    if (GetDeviceState() == kDeviceStateIdle) {
        auto display = Board::GetInstance().GetDisplay();
        display->SetStatus(Lang::Strings::STANDBY);
        display->SetEmotion("neutral");
        display->SetChatMessage("system", "");
    }
}

void Application::ShowInternetFailedNotification(const char* reason) {
    // 由 CheckNewVersion 在 OTA/服务端不可达时调用；每次 WiFi 连接周期只提示一次
    if (internet_failed_shown_) {
        return;
    }
    internet_failed_shown_ = true;

    // 解析原因：默认从 Ota 取最新错误，便于上层不传参时也能展示
    const std::string& ota_reason = (ota_ != nullptr) ? ota_->GetLastErrorMessage() : std::string();
    const char* why = (reason != nullptr && reason[0] != '\0') ? reason
                     : (ota_reason.empty() ? "UNKNOWN" : ota_reason.c_str());

    // 写一行可检索的日志（按规范使用 LOG_TAG=Internet，便于后期检索）
    ESP_LOGE(TAG, "[InternetFailed] reason=%s, url=%s",
             why,
             (ota_ != nullptr ? ota_->GetCheckVersionUrl().c_str() : ""));

    auto display = Board::GetInstance().GetDisplay();
    // 顶部通知（兼容 LcdDisplay，与"已连接"同条状态栏）
    display->ShowNotification(Lang::Strings::INTERNET_FAILED, 30000);

    if (GetAttitudeDisplay() != nullptr) {
        // 调试信息卡：罗盘 UI 替代基类表情聊天弹窗，必须走 ShowDebugInfo 才能落地
        Schedule([this, why_str = std::string(why)]() {
            if (auto* attitude = GetAttitudeDisplay()) {
                attitude->ShowDebugInfo(
                    Lang::Strings::INTERNET_FAILED,
                    why_str,
                    5000);
            }
        });
    }
    // 用失败提示音而非默认成功音，避免与配网成功音效混淆
    audio_service_.PlaySound(Lang::Sounds::OGG_EXCLAMATION);
}

void Application::ToggleChatState() {
    xEventGroupSetBits(event_group_, MAIN_EVENT_TOGGLE_CHAT);
}

bool Application::HandleFortuneBootKey() {
    if (auto* attitude = GetAttitudeDisplay()) {
        return attitude->HandleBootKey();
    }
    return false;
}

bool Application::HandleFortuneBootLongPress() {
    if (auto* attitude = GetAttitudeDisplay()) {
        return attitude->HandleFortuneBootLongPress();
    }
    return false;
}

bool Application::HandlePowerKey() {
    if (auto* attitude = GetAttitudeDisplay()) {
        return attitude->HandlePowerKey();
    }
    return false;
}

void Application::StartListening() {
    xEventGroupSetBits(event_group_, MAIN_EVENT_START_LISTENING);
}

void Application::StopListening() {
    xEventGroupSetBits(event_group_, MAIN_EVENT_STOP_LISTENING);
}

void Application::HandleToggleChatEvent() {
    auto state = GetDeviceState();
    
    if (state == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (state == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    } else if (state == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }

    if (state == kDeviceStateIdle) {
        ListeningMode mode = GetDefaultListeningMode();
        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            // Schedule to let the state change be processed first (UI update)
            Schedule([this, mode]() {
                ContinueOpenAudioChannel(mode);
            });
            return;
        }
        SetListeningMode(mode);
    } else if (state == kDeviceStateSpeaking) {
        AbortSpeaking(kAbortReasonNone);
    } else if (state == kDeviceStateListening) {
        protocol_->CloseAudioChannel();
    }
}

void Application::ContinueOpenAudioChannel(ListeningMode mode) {
    // Check state again in case it was changed during scheduling
    if (GetDeviceState() != kDeviceStateConnecting) {
        return;
    }

    // Switch to performance mode before connecting to reduce latency
    auto& board = Board::GetInstance();
    board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);

    if (!protocol_->IsAudioChannelOpened()) {
        if (!protocol_->OpenAudioChannel()) {
            return;
        }
    }

    SetListeningMode(mode);
}

void Application::HandleStartListeningEvent() {
    auto state = GetDeviceState();
    
    if (state == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (state == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }
    
    if (state == kDeviceStateIdle) {
        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            // Schedule to let the state change be processed first (UI update)
            Schedule([this]() {
                ContinueOpenAudioChannel(kListeningModeManualStop);
            });
            return;
        }
        SetListeningMode(kListeningModeManualStop);
    } else if (state == kDeviceStateSpeaking) {
        AbortSpeaking(kAbortReasonNone);
        SetListeningMode(kListeningModeManualStop);
    }
}

void Application::HandleStopListeningEvent() {
    auto state = GetDeviceState();
    
    if (state == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    } else if (state == kDeviceStateListening) {
        if (protocol_) {
            protocol_->SendStopListening();
        }
        SetDeviceState(kDeviceStateIdle);
    }
}

void Application::HandleWakeWordDetectedEvent() {
    if (!protocol_) {
        return;
    }

    auto state = GetDeviceState();
    auto wake_word = audio_service_.GetLastWakeWord();
    ESP_LOGI(TAG, "Wake word detected: %s (state: %d)", wake_word.c_str(), (int)state);

    // 调试卡 + 音频播报：检测到唤醒词
    if (auto* attitude = GetAttitudeDisplay()) {
        std::string detail = wake_word.empty() ? std::string("(无)") : wake_word;
        Schedule([this, attitude, detail]() {
            // 短促本地提示音 + 显示卡（默认 30s，有语音交互则由 RefreshDebugInfoTimer 重计时）
            attitude->ShowDebugInfo("唤醒成功", detail, 30000);
            audio_service_.PlaySound(Lang::Sounds::OGG_POPUP);

            // 显示 GIF 背景（ark-reactor-normal.gif）
            void* gif_data = nullptr;
            size_t gif_size = 0;
            if (Assets::GetInstance().GetAssetData("ark-reactor-normal.gif", gif_data, gif_size)) {
                auto gif_image = std::make_unique<LvglRawImage>(gif_data, gif_size);
                if (gif_image->IsGif()) {
                    attitude->SetPreviewImage(std::move(gif_image));
                    ESP_LOGI(TAG, "Wakeup GIF displayed: %zu bytes", gif_size);
                } else {
                    ESP_LOGW(TAG, "Wakeup GIF has invalid magic bytes");
                }
            } else {
                ESP_LOGW(TAG, "Wakeup GIF not found in assets");
            }
        });
    }
    // 不在此处触发服务端 TTS：唤醒词后 LLM 即将开始接管对话，避免双声道冲突

    if (state == kDeviceStateIdle) {
        if (!protocol_->IsAudioChannelOpened()) {
            // 首次唤醒：channel 未打开，走 OpenAudioChannel + Listen 完整路径
            audio_service_.EncodeWakeWord();
            auto wake_word = audio_service_.GetLastWakeWord();

            SetDeviceState(kDeviceStateConnecting);
            // Schedule to let the state change be processed first (UI update),
            // then continue with OpenAudioChannel which may block for ~1 second
            Schedule([this, wake_word]() {
                ContinueWakeWordInvoke(wake_word);
            });
            return;
        }
        // 二次/连续唤醒：channel 仍处于打开状态（典型场景是 listening 超时后回到 idle，
        // 此时 ContinueWakeWordInvoke 内部会因为 state != kDeviceStateConnecting 而提前 return，
        // 导致 ESP32 检测到唤醒词后什么也不做）。这里直接走协议层的 SendWakeWordDetected，
        // 让服务端 handleWakeWord 路径生成问候 + TTS，再切到 listening 继续接收用户后续话语。
        audio_service_.EncodeWakeWord();
        const std::string& wake_word_text = audio_service_.GetLastWakeWord();
        if (!wake_word_text.empty()) {
            protocol_->SendWakeWordDetected(wake_word_text);
        }
        SetListeningMode(GetDefaultListeningMode());
        play_popup_on_listening_ = true;
        audio_service_.EnableWakeWordDetection(true);
        return;
    } else if (state == kDeviceStateSpeaking || state == kDeviceStateListening) {
        // 后续唤醒：先打断当前对话，再通知服务端触发 LLM 问候 + TTS
        // 不切换状态机（kDeviceStateListening/Speaking 无合法路径到 Connecting），
        // 改用协议层的 SendWakeWordDetected 让服务端走 handleWakeWord 路径
        AbortSpeaking(kAbortReasonWakeWordDetected);
        // Clear send queue to avoid sending residues to server
        while (audio_service_.PopPacketFromSendQueue());

        // 显式停止本地 TTS 播放，确保新的语音能立即开始
        audio_service_.Stop();

        // 通知服务端检测到唤醒词：服务端会调 handleWakeWord → LLM 问候 + TTS
        audio_service_.EncodeWakeWord();
        const std::string& wake_word_text = audio_service_.GetLastWakeWord();
        if (!wake_word_text.empty() && protocol_->IsAudioChannelOpened()) {
            protocol_->SendWakeWordDetected(wake_word_text);
        }

        if (state == kDeviceStateListening) {
            audio_service_.ResetDecoder();
            audio_service_.PlaySound(Lang::Sounds::OGG_POPUP);
            // Re-enable wake word detection as it was stopped by the detection itself
            audio_service_.EnableWakeWordDetection(true);
        } else {
            // speaking 状态：标记在回 listening 时播放提示音
            play_popup_on_listening_ = true;
            audio_service_.Start();
            audio_service_.EnableWakeWordDetection(true);
        }
    } else if (state == kDeviceStateActivating) {
        // Restart the activation check if the wake word is detected during activation
        SetDeviceState(kDeviceStateIdle);
    }
}

void Application::ContinueWakeWordInvoke(const std::string& wake_word) {
    // Check state again in case it was changed during scheduling
    if (GetDeviceState() != kDeviceStateConnecting) {
        return;
    }

    // Switch to performance mode before connecting to reduce latency
    auto& board = Board::GetInstance();
    board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);

    if (!protocol_->IsAudioChannelOpened()) {
        if (!protocol_->OpenAudioChannel()) {
            audio_service_.EnableWakeWordDetection(true);
            return;
        }
    }

    ESP_LOGI(TAG, "Wake word detected: %s", wake_word.c_str());
#if CONFIG_SEND_WAKE_WORD_DATA
    // Encode and send the wake word data to the server
    while (auto packet = audio_service_.PopWakeWordPacket()) {
        protocol_->SendAudio(std::move(packet));
    }
    // Set the chat state to wake word detected
    protocol_->SendWakeWordDetected(wake_word);
    SetListeningMode(GetDefaultListeningMode());
#else
    // Set flag to play popup sound after state changes to listening
    // (PlaySound here would be cleared by ResetDecoder in EnableVoiceProcessing)
    play_popup_on_listening_ = true;
    SetListeningMode(GetDefaultListeningMode());
#endif
}

void Application::HandleStateChangedEvent() {
    DeviceState new_state = state_machine_.GetState();
    clock_ticks_ = 0;

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto led = board.GetLed();
    led->OnStateChanged();
    
    switch (new_state) {
        case kDeviceStateUnknown:
        case kDeviceStateIdle:
            display->SetStatus(Lang::Strings::STANDBY);
            display->ClearChatMessages();  // Clear messages first
            display->SetEmotion("neutral"); // Then set emotion (wechat mode checks child count)
            audio_service_.EnableVoiceProcessing(false);
            audio_service_.EnableWakeWordDetection(true);
            break;
        case kDeviceStateConnecting:
            display->SetStatus(Lang::Strings::CONNECTING);
            display->SetEmotion("neutral");
            display->SetChatMessage("system", "");
            break;
        case kDeviceStateListening:
            display->SetStatus(Lang::Strings::LISTENING);
            display->SetEmotion("neutral");

            // Make sure the audio processor is running
            if (play_popup_on_listening_ || !audio_service_.IsAudioProcessorRunning()) {
                // For auto mode, wait for playback queue to be empty before enabling voice processing
                // This prevents audio truncation when STOP arrives late due to network jitter
                if (listening_mode_ == kListeningModeAutoStop) {
                    audio_service_.WaitForPlaybackQueueEmpty();
                }
                
                // Send the start listening command
                protocol_->SendStartListening(listening_mode_);
                audio_service_.EnableVoiceProcessing(true);
            }

#ifdef CONFIG_WAKE_WORD_DETECTION_IN_LISTENING
            // Enable wake word detection in listening mode (configured via Kconfig)
            audio_service_.EnableWakeWordDetection(audio_service_.IsAfeWakeWord());
#else
            // Disable wake word detection in listening mode
            audio_service_.EnableWakeWordDetection(false);
#endif
            
            // Play popup sound after ResetDecoder (in EnableVoiceProcessing) has been called
            if (play_popup_on_listening_) {
                play_popup_on_listening_ = false;
                audio_service_.PlaySound(Lang::Sounds::OGG_POPUP);
            }
            break;
        case kDeviceStateSpeaking:
            display->SetStatus(Lang::Strings::SPEAKING);

            if (!audio_service_.IsRunning()) {
                audio_service_.Start();
            }
            if (listening_mode_ != kListeningModeRealtime) {
                audio_service_.EnableVoiceProcessing(false);
                // Only AFE wake word can be detected in speaking mode
                audio_service_.EnableWakeWordDetection(audio_service_.IsAfeWakeWord());
            }
            audio_service_.ResetDecoder();
            break;
        case kDeviceStateWifiConfiguring:
            audio_service_.EnableVoiceProcessing(false);
            audio_service_.EnableWakeWordDetection(false);
            break;
        default:
            // Do nothing
            break;
    }
}

void Application::Schedule(std::function<void()>&& callback) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        main_tasks_.push_back(std::move(callback));
    }
    xEventGroupSetBits(event_group_, MAIN_EVENT_SCHEDULE);
}

void Application::AbortSpeaking(AbortReason reason) {
    ESP_LOGI(TAG, "Abort speaking");
    aborted_ = true;
    if (protocol_) {
        protocol_->SendAbortSpeaking(reason);
    }
}

void Application::SetListeningMode(ListeningMode mode) {
    listening_mode_ = mode;
    // 只有在之前不在 listening 状态时才启动超时计时
    // 避免 EnableWakeWordDetection 触发 SetListeningMode 时重复重置计时器
    if (GetDeviceState() != kDeviceStateListening) {
        listening_started_ticks_ = clock_ticks_;
    }
    SetDeviceState(kDeviceStateListening);
}

ListeningMode Application::GetDefaultListeningMode() const {
    return aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime;
}

void Application::Reboot() {
    ESP_LOGI(TAG, "Rebooting...");
    // Disconnect the audio channel
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        protocol_->CloseAudioChannel();
    }
    protocol_.reset();
    audio_service_.Stop();

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

bool Application::UpgradeFirmware(const std::string& url, const std::string& version) {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();

    std::string upgrade_url = url;
    std::string version_info = version.empty() ? "(Manual upgrade)" : version;

    // Close audio channel if it's open
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        ESP_LOGI(TAG, "Closing audio channel before firmware upgrade");
        protocol_->CloseAudioChannel();
    }
    ESP_LOGI(TAG, "Starting firmware upgrade from URL: %s", upgrade_url.c_str());

    Alert(Lang::Strings::OTA_UPGRADE, Lang::Strings::UPGRADING, "download", Lang::Sounds::OGG_UPGRADE);
    vTaskDelay(pdMS_TO_TICKS(3000));

    SetDeviceState(kDeviceStateUpgrading);

    std::string message = std::string(Lang::Strings::NEW_VERSION) + version_info;
    display->SetChatMessage("system", message.c_str());

    board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
    audio_service_.Stop();
    vTaskDelay(pdMS_TO_TICKS(1000));

    bool upgrade_success = Ota::Upgrade(upgrade_url, [this, display](int progress, size_t speed) {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, speed / 1024);
        Schedule([display, message = std::string(buffer)]() {
            display->SetChatMessage("system", message.c_str());
        });
    });

    if (!upgrade_success) {
        // Upgrade failed, restart audio service and continue running
        ESP_LOGE(TAG, "Firmware upgrade failed, restarting audio service and continuing operation...");
        audio_service_.Start(); // Restart audio service
        board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER); // Restore power save level
        Alert(Lang::Strings::ERROR, Lang::Strings::UPGRADE_FAILED, "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
        vTaskDelay(pdMS_TO_TICKS(3000));
        return false;
    } else {
        // Upgrade success, reboot immediately
        ESP_LOGI(TAG, "Firmware upgrade successful, rebooting...");
        display->SetChatMessage("system", "Upgrade successful, rebooting...");
        vTaskDelay(pdMS_TO_TICKS(1000)); // Brief pause to show message
        Reboot();
        return true;
    }
}

void Application::WakeWordInvoke(const std::string& wake_word) {
    if (!protocol_) {
        return;
    }

    auto state = GetDeviceState();
    
    if (state == kDeviceStateIdle) {
        audio_service_.EncodeWakeWord();

        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            // Schedule to let the state change be processed first (UI update)
            Schedule([this, wake_word]() {
                ContinueWakeWordInvoke(wake_word);
            });
            return;
        }
        // Channel already opened, continue directly
        ContinueWakeWordInvoke(wake_word);
    } else if (state == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
        });
    } else if (state == kDeviceStateListening) {   
        Schedule([this]() {
            if (protocol_) {
                protocol_->CloseAudioChannel();
            }
        });
    }
}

bool Application::CanEnterSleepMode() {
    if (GetDeviceState() != kDeviceStateIdle) {
        return false;
    }

    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        return false;
    }

    if (!audio_service_.IsIdle()) {
        return false;
    }

    // Now it is safe to enter sleep mode
    return true;
}

void Application::RegisterMcpBroadcastCallback(std::function<void(const std::string&)> callback) {
    mcp_broadcast_callback_ = std::move(callback);
}

void Application::SendMcpMessage(const std::string& payload) {
    // Always schedule to run in main task for thread safety
    Schedule([this, payload](){ 
        if (protocol_) {
            protocol_->SendMcpMessage(payload);
        }
        if (mcp_broadcast_callback_) {
            mcp_broadcast_callback_(payload);
        }
    });
}

void Application::SetAecMode(AecMode mode) {
    aec_mode_ = mode;
    Schedule([this]() {
        auto& board = Board::GetInstance();
        auto display = board.GetDisplay();
        switch (aec_mode_) {
        case kAecOff:
            audio_service_.EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_OFF);
            break;
        case kAecOnServerSide:
            audio_service_.EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        case kAecOnDeviceSide:
            audio_service_.EnableDeviceAec(true);
            display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        }

        // If the AEC mode is changed, close the audio channel
        if (protocol_ && protocol_->IsAudioChannelOpened()) {
            protocol_->CloseAudioChannel();
        }
    });
}

void Application::PlaySound(const std::string_view& sound) {
    audio_service_.PlaySound(sound);
}

void Application::PlayUiSound(const std::string_view& sound) {
    if (sound.empty()) {
        return;
    }
    Schedule([this, sound]() {
        ESP_LOGI(TAG, "PlayUiSound (%zu bytes)", sound.size());
        audio_service_.PlaySound(sound);
    });
}

void Application::RequestDebugTts(const std::string& text) {
    if (text.empty()) {
        return;
    }
    if (GetDeviceState() != kDeviceStateIdle) {
        ESP_LOGD(TAG, "RequestDebugTts skipped (not idle): %s", text.c_str());
        return;
    }
    if (!protocol_) {
        return;
    }
    ESP_LOGI(TAG, "RequestDebugTts: %s", text.c_str());
    Schedule([this, prompt = text]() {
        // 1. 尝试复用已有通道；否则建立新通道
        if (!protocol_->IsAudioChannelOpened()) {
            if (!protocol_->OpenAudioChannel()) {
                ESP_LOGW(TAG, "RequestDebugTts: OpenAudioChannel failed");
                return;
            }
        }
        // 2. 主动触发 listen.start，让服务端进入可接收消息的状态
        //    mode=manual：仅响应 stop，避免 server 主动超时结束
        protocol_->SendStartListening(kListeningModeManualStop);
        // 3. 主动发送 user_prompt，让服务端处理（具体由服务端决定是否 TTS）
        protocol_->SendUserPrompt(prompt);
    });
}

void Application::DismissDebugInfo() {
    Schedule([]() {
        if (auto* attitude = GetAttitudeDisplay()) {
            attitude->HideDebugInfo();
        }
    });
}

void Application::ResetProtocol() {
    Schedule([this]() {
        // Close audio channel if opened
        if (protocol_ && protocol_->IsAudioChannelOpened()) {
            protocol_->CloseAudioChannel();
        }
        // Reset protocol
        protocol_.reset();
    });
}

