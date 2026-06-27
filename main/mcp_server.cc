/*
 * MCP Server Implementation
 * Reference: https://modelcontextprotocol.io/specification/2024-11-05
 */

#include "mcp_server.h"
#include <esp_log.h>
#include <esp_app_desc.h>
#include <algorithm>
#include <cstring>
#include <esp_pthread.h>

#include "application.h"
#include "display.h"
#include "oled_display.h"
#include "board.h"
#include "settings.h"
#include "lvgl_theme.h"
#include "lvgl_display.h"
#include "attitude_display.h"
#include "http_api_unified.h"
#include "music_player.h"

#define TAG "MCP"

McpServer::McpServer() {
}

McpServer::~McpServer() {
    for (auto tool : tools_) {
        delete tool;
    }
    tools_.clear();
}

void McpServer::AddCommonTools() {
    // *Important* To speed up the response time, we add the common tools to the beginning of
    // the tools list to utilize the prompt cache.
    // **重要** 为了提升响应速度，我们把常用的工具放在前面，利用 prompt cache 的特性。

    // Backup the original tools list and restore it after adding the common tools.
    auto original_tools = std::move(tools_);
    auto& board = Board::GetInstance();

    // Do not add custom tools here.
    // Custom tools must be added in the board's InitializeTools function.

    AddTool("self.get_device_status",
        "Provides the real-time information of the device, including the current status of the audio speaker, screen, battery, network, etc.\n"
        "Use this tool for: \n"
        "1. Answering questions about current condition (e.g. what is the current volume of the audio speaker?)\n"
        "2. As the first step to control the device (e.g. turn up / down the volume of the audio speaker, etc.)",
        PropertyList(),
        [&board](const PropertyList& properties) -> ReturnValue {
            return board.GetDeviceStatusJson();
        });

    AddTool("self.audio_speaker.set_volume", 
        "Set the volume of the audio speaker. If the current volume is unknown, you must call `self.get_device_status` tool first and then call this tool.",
        PropertyList({
            Property("volume", kPropertyTypeInteger, 0, 100)
        }), 
        [&board](const PropertyList& properties) -> ReturnValue {
            auto codec = board.GetAudioCodec();
            codec->SetOutputVolume(properties["volume"].value<int>());
            return true;
        });
    
    auto backlight = board.GetBacklight();
    if (backlight) {
        AddTool("self.screen.set_brightness",
            "Set the brightness of the screen.",
            PropertyList({
                Property("brightness", kPropertyTypeInteger, 0, 100)
            }),
            [backlight](const PropertyList& properties) -> ReturnValue {
                uint8_t brightness = static_cast<uint8_t>(properties["brightness"].value<int>());
                backlight->SetBrightness(brightness, true);
                return true;
            });
    }

#ifdef HAVE_LVGL
    auto display = board.GetDisplay();
    if (display && display->GetTheme() != nullptr) {
        AddTool("self.screen.set_theme",
            "Set the theme of the screen. The theme can be `light` or `dark`.",
            PropertyList({
                Property("theme", kPropertyTypeString)
            }),
            [display](const PropertyList& properties) -> ReturnValue {
                auto theme_name = properties["theme"].value<std::string>();
                auto& theme_manager = LvglThemeManager::GetInstance();
                auto theme = theme_manager.GetTheme(theme_name);
                if (theme != nullptr) {
                    display->SetTheme(theme);
                    return true;
                }
                return false;
            });
    }

    // 太极图旋转控制 (用于按键触发, MCP 也可调用)
    // 仅当 display 是 AttitudeDisplay 时可用
    auto attitude_display = dynamic_cast<AttitudeDisplay*>(display);
    if (attitude_display != nullptr) {
        // 顺时针旋转 15° (按键按下)
        AddTool("self.attitude.taiji_rotate_cw",
            "Rotate the Taiji diagram clockwise by 15° (button trigger).",
            PropertyList(),
            [attitude_display](const PropertyList& properties) -> ReturnValue {
                attitude_display->RotateTaiji();
                return true;
            });

        // 逆时针旋转 15°
        AddTool("self.attitude.taiji_rotate_ccw",
            "Rotate the Taiji diagram counter-clockwise by 15°.",
            PropertyList(),
            [attitude_display](const PropertyList& properties) -> ReturnValue {
                attitude_display->RotateTaijiCCW();
                return true;
            });

        // 设置指定角度
        AddTool("self.attitude.taiji_set_rotation",
            "Set the Taiji diagram rotation angle. Angle is in 0.1° units (e.g., 15° = 150).",
            PropertyList({
                Property("angle", kPropertyTypeInteger, 0, 3600)
            }),
            [attitude_display](const PropertyList& properties) -> ReturnValue {
                int angle = properties["angle"].value<int>();
                attitude_display->SetTaijiRotation(angle);
                return true;
            });

        // 获取当前角度
        AddTool("self.attitude.taiji_get_rotation",
            "Get the current Taiji diagram rotation angle (0.1° units).",
            PropertyList(),
            [attitude_display](const PropertyList& properties) -> ReturnValue {
                return attitude_display->GetTaijiRotation();
            });

        // 重置旋转
        AddTool("self.attitude.taiji_reset_rotation",
            "Reset the Taiji diagram rotation to 0°.",
            PropertyList(),
            [attitude_display](const PropertyList& properties) -> ReturnValue {
                attitude_display->ResetTaijiRotation();
                return true;
            });

        // 迭代 1: 鱼眼状态手动切换（验收测试用，迭代 3 接入真实 WiFi/BLE 驱动）
        AddTool("self.attitude.set_wifi_fisheye",
            "Set WiFi fisheye status: 0=DISCONNECTED, 1=CONNECTING, 2=CONNECTED.",
            PropertyList({
                Property("status", kPropertyTypeInteger, 0, 2)
            }),
            [attitude_display](const PropertyList& properties) -> ReturnValue {
                int status = properties["status"].value<int>();
                if (status < 0 || status > 2) {
                    return false;
                }
                attitude_display->UpdateWifiFisheye(static_cast<WifiStatus>(status));
                return true;
            });

        AddTool("self.attitude.set_ble_fisheye",
            "Set BLE fisheye status: 0=DISABLED, 1=ADVERTISING, 2=CONNECTED.",
            PropertyList({
                Property("status", kPropertyTypeInteger, 0, 2)
            }),
            [attitude_display](const PropertyList& properties) -> ReturnValue {
                int status = properties["status"].value<int>();
                if (status < 0 || status > 2) {
                    return false;
                }
                attitude_display->UpdateBleFisheye(static_cast<BleStatus>(status));
                return true;
            });

        // 运势结果卡（Plan A）已彻底删除：show_fortune / dismiss_fortune 工具同步下线
    }

    auto camera = board.GetCamera();
    if (camera) {
        AddTool("self.camera.take_photo",
            "Always remember you have a camera. If the user asks you to see something, use this tool to take a photo and then explain it.\n"
            "Args:\n"
            "  `question`: The question that you want to ask about the photo.\n"
            "Return:\n"
            "  A JSON object that provides the photo information.",
            PropertyList({
                Property("question", kPropertyTypeString)
            }),
            [camera](const PropertyList& properties) -> ReturnValue {
                // Lower the priority to do the camera capture
                TaskPriorityReset priority_reset(1);

                if (!camera->Capture()) {
                    throw std::runtime_error("Failed to capture photo");
                }
                auto question = properties["question"].value<std::string>();
                return camera->Explain(question);
            });
    }
#endif

    // Restore the original tools list to the end of the tools list
    tools_.insert(tools_.end(), original_tools.begin(), original_tools.end());
}

void McpServer::AddUserOnlyTools() {
    // System tools
    AddUserOnlyTool("self.get_system_info",
        "Get the system information",
        PropertyList(),
        [this](const PropertyList& properties) -> ReturnValue {
            auto& board = Board::GetInstance();
            return board.GetSystemInfoJson();
        });

    AddUserOnlyTool("self.reboot", "Reboot the system",
        PropertyList(),
        [this](const PropertyList& properties) -> ReturnValue {
            auto& app = Application::GetInstance();
            app.Schedule([&app]() {
                ESP_LOGW(TAG, "User requested reboot");
                vTaskDelay(pdMS_TO_TICKS(1000));

                app.Reboot();
            });
            return true;
        });

    // Firmware upgrade
    AddUserOnlyTool("self.upgrade_firmware", "Upgrade firmware from a specific URL. This will download and install the firmware, then reboot the device.",
        PropertyList({
            Property("url", kPropertyTypeString, "The URL of the firmware binary file to download and install")
        }),
        [this](const PropertyList& properties) -> ReturnValue {
            auto url = properties["url"].value<std::string>();
            ESP_LOGI(TAG, "User requested firmware upgrade from URL: %s", url.c_str());
            
            auto& app = Application::GetInstance();
            app.Schedule([url, &app]() {
                bool success = app.UpgradeFirmware(url);
                if (!success) {
                    ESP_LOGE(TAG, "Firmware upgrade failed");
                }
            });
            
            return true;
        });

    // Display control
#ifdef HAVE_LVGL
    auto display = dynamic_cast<LvglDisplay*>(Board::GetInstance().GetDisplay());
    if (display) {
        AddUserOnlyTool("self.screen.get_info", "Information about the screen, including width, height, etc.",
            PropertyList(),
            [display](const PropertyList& properties) -> ReturnValue {
                cJSON *json = cJSON_CreateObject();
                cJSON_AddNumberToObject(json, "width", display->width());
                cJSON_AddNumberToObject(json, "height", display->height());
                if (dynamic_cast<OledDisplay*>(display)) {
                    cJSON_AddBoolToObject(json, "monochrome", true);
                } else {
                    cJSON_AddBoolToObject(json, "monochrome", false);
                }
                return json;
            });

#if CONFIG_LV_USE_SNAPSHOT
        AddUserOnlyTool("self.screen.snapshot", "Snapshot the screen and upload it to a specific URL",
            PropertyList({
                Property("url", kPropertyTypeString),
                Property("quality", kPropertyTypeInteger, 80, 1, 100)
            }),
            [display](const PropertyList& properties) -> ReturnValue {
                auto url = properties["url"].value<std::string>();
                auto quality = properties["quality"].value<int>();

                std::string jpeg_data;
                if (!display->SnapshotToJpeg(jpeg_data, quality)) {
                    throw std::runtime_error("Failed to snapshot screen");
                }

                ESP_LOGI(TAG, "Upload snapshot %u bytes to %s", jpeg_data.size(), url.c_str());
                
                // 构造multipart/form-data请求体
                std::string boundary = "----ESP32_SCREEN_SNAPSHOT_BOUNDARY";
                
                auto http = Board::GetInstance().GetNetwork()->CreateHttp(3);
                http->SetHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
                if (!http->Open("POST", url)) {
                    throw std::runtime_error("Failed to open URL: " + url);
                }
                {
                    // 文件字段头部
                    std::string file_header;
                    file_header += "--" + boundary + "\r\n";
                    file_header += "Content-Disposition: form-data; name=\"file\"; filename=\"screenshot.jpg\"\r\n";
                    file_header += "Content-Type: image/jpeg\r\n";
                    file_header += "\r\n";
                    http->Write(file_header.c_str(), file_header.size());
                }

                // JPEG数据
                http->Write((const char*)jpeg_data.data(), jpeg_data.size());

                {
                    // multipart尾部
                    std::string multipart_footer;
                    multipart_footer += "\r\n--" + boundary + "--\r\n";
                    http->Write(multipart_footer.c_str(), multipart_footer.size());
                }
                http->Write("", 0);

                if (http->GetStatusCode() != 200) {
                    throw std::runtime_error("Unexpected status code: " + std::to_string(http->GetStatusCode()));
                }
                std::string result = http->ReadAll();
                http->Close();
                ESP_LOGI(TAG, "Snapshot screen result: %s", result.c_str());
                return true;
            });
        
        AddUserOnlyTool("self.screen.preview_image", "Preview an image on the screen",
            PropertyList({
                Property("url", kPropertyTypeString)
            }),
            [display](const PropertyList& properties) -> ReturnValue {
                auto url = properties["url"].value<std::string>();
                auto http = Board::GetInstance().GetNetwork()->CreateHttp(3);

                if (!http->Open("GET", url)) {
                    throw std::runtime_error("Failed to open URL: " + url);
                }
                int status_code = http->GetStatusCode();
                if (status_code != 200) {
                    throw std::runtime_error("Unexpected status code: " + std::to_string(status_code));
                }

                size_t content_length = http->GetBodyLength();
                char* data = (char*)heap_caps_malloc(content_length, MALLOC_CAP_8BIT);
                if (data == nullptr) {
                    throw std::runtime_error("Failed to allocate memory for image: " + url);
                }
                size_t total_read = 0;
                while (total_read < content_length) {
                    int ret = http->Read(data + total_read, content_length - total_read);
                    if (ret < 0) {
                        heap_caps_free(data);
                        throw std::runtime_error("Failed to download image: " + url);
                    }
                    if (ret == 0) {
                        break;
                    }
                    total_read += ret;
                }
                http->Close();

                auto image = std::make_unique<LvglAllocatedImage>(data, content_length);
                display->SetPreviewImage(std::move(image));
                return true;
            });
#endif // CONFIG_LV_USE_SNAPSHOT
    }
#endif // HAVE_LVGL

    // Assets download url (always registered — Settings storage works regardless of partition layout)
    AddUserOnlyTool("self.assets.set_download_url", "Set the download url for the assets",
            PropertyList({
                Property("url", kPropertyTypeString)
            }),
            [](const PropertyList& properties) -> ReturnValue {
                auto url = properties["url"].value<std::string>();
                Settings settings("assets", true);
                settings.SetString("download_url", url);
                return true;
            });
}

void McpServer::AddTool(McpTool* tool) {
    // Prevent adding duplicate tools
    if (std::find_if(tools_.begin(), tools_.end(), [tool](const McpTool* t) { return t->name() == tool->name(); }) != tools_.end()) {
        ESP_LOGW(TAG, "Tool %s already added", tool->name().c_str());
        return;
    }

    ESP_LOGI(TAG, "Add tool: %s%s", tool->name().c_str(), tool->user_only() ? " [user]" : "");
    tools_.push_back(tool);
}

void McpServer::AddTool(const std::string& name, const std::string& description, const PropertyList& properties, std::function<ReturnValue(const PropertyList&)> callback) {
    AddTool(new McpTool(name, description, properties, callback));
}

void McpServer::AddUserOnlyTool(const std::string& name, const std::string& description, const PropertyList& properties, std::function<ReturnValue(const PropertyList&)> callback) {
    auto tool = new McpTool(name, description, properties, callback);
    tool->set_user_only(true);
    AddTool(tool);
}

void McpServer::ParseMessage(const std::string& message) {
    cJSON* json = cJSON_Parse(message.c_str());
    if (json == nullptr) {
        ESP_LOGE(TAG, "Failed to parse MCP message: %s", message.c_str());
        return;
    }
    ParseMessage(json);
    cJSON_Delete(json);
}

void McpServer::ParseCapabilities(const cJSON* capabilities) {
    auto vision = cJSON_GetObjectItem(capabilities, "vision");
    if (cJSON_IsObject(vision)) {
        auto url = cJSON_GetObjectItem(vision, "url");
        auto token = cJSON_GetObjectItem(vision, "token");
        if (cJSON_IsString(url)) {
            auto camera = Board::GetInstance().GetCamera();
            if (camera) {
                std::string url_str = std::string(url->valuestring);
                std::string token_str;
                if (cJSON_IsString(token)) {
                    token_str = std::string(token->valuestring);
                }
                camera->SetExplainUrl(url_str, token_str);
            }
        }
    }

    // =================================================================
    // HTTP API → MCP 工具统一注册
    // =================================================================
    //
    // 把所有 sdcard_log_http.cc 中的 HTTP 端点也都暴露为 MCP 工具，
    // 这样大模型可以直接通过 MCP 调用本地 HTTP API 所做的一切操作。
    // 所有实现位于 main/http_api_unified.{h,cc}。

    // -------- Device API --------

    AddUserOnlyTool("self.device.get_status",
        "Get device status (WiFi/SD card/memory/uptime/version/board). "
        "Equivalent to HTTP GET /api/device/status.",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {
            cJSON* root = http_api_device_status();
            char* json = cJSON_PrintUnformatted(root);
            std::string result = json ? json : "{}";
            if (json) free(json);
            cJSON_Delete(root);
            return result;
        });

    AddUserOnlyTool("self.device.get_logs",
        "Get the latest device logs (read from SD card). "
        "Equivalent to HTTP GET /api/device/logs.",
        PropertyList({
            Property("max_bytes", kPropertyTypeInteger, 10240, 100, 102400)
        }),
        [](const PropertyList& properties) -> ReturnValue {
            int max_bytes = properties["max_bytes"].value<int>();
            std::string out(max_bytes, '\0');
            size_t n = http_api_device_logs(out.data(), out.size());
            out.resize(n);
            return out;
        });

    AddUserOnlyTool("self.device.get_ota_url",
        "Query OTA URL and WebSocket URL configuration (NVS vs build config). "
        "Equivalent to HTTP GET /api/device/ota-url.",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {
            cJSON* root = http_api_device_ota_url();
            char* json = cJSON_PrintUnformatted(root);
            std::string result = json ? json : "{}";
            if (json) free(json);
            cJSON_Delete(root);
            return result;
        });

    AddUserOnlyTool("self.device.clear_nvs",
        "Clear NVS stored OTA URL and/or WebSocket URL (force use of build config). "
        "Equivalent to HTTP POST /api/device/clear-nvs.",
        PropertyList({
            Property("key", kPropertyTypeString, "ota_url")
        }),
        [](const PropertyList& properties) -> ReturnValue {
            auto key = properties["key"].value<std::string>();
            char err[128] = {0};
            const char* key_arg = key.empty() ? nullptr : key.c_str();
            bool ok = http_api_device_clear_nvs(key_arg, err, sizeof(err));
            if (!ok) throw std::runtime_error(err);
            return true;
        });

    // -------- WiFi Backup API --------

    AddUserOnlyTool("self.wifi.get_status",
        "查询 NVS 和 SD 卡中的 WiFi 凭据状态。返回 nvs_count / sd_card_count / sd_card_has_backup / nvs_networks。\n"
        "Equivalent to HTTP GET /api/wifi/status.",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {
            cJSON* root = http_api_wifi_status();
            char* json_str = cJSON_PrintUnformatted(root);
            cJSON_Delete(root);
            return std::string(json_str ? json_str : "{}");
        });

    AddUserOnlyTool("self.wifi.clear_nvs",
        "清空 NVS 中所有保存的 WiFi 凭据（SSID + 密码）。\n"
        "**注意**: SD 卡 /sdcard/wifi_config.json 备份会被保留，设备下次启动时会自动从 SD 卡恢复。\n"
        "用途：模拟 NVS 丢失场景，验证三级回退（NVS → SD 卡 → AP 配网）。\n"
        "Equivalent to HTTP POST /api/wifi/clear-nvs.",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {
            char err[128] = {0};
            bool ok = http_api_wifi_clear_nvs(err, sizeof(err));
            if (!ok) throw std::runtime_error(err);
            return true;
        });

    AddUserOnlyTool("self.wifi.restore_from_sd",
        "手动触发从 SD 卡备份文件 (/sdcard/wifi_config.json) 恢复所有 WiFi 凭据到 NVS。\n"
        "返回恢复的网络数量；0 表示无 SD 卡备份或恢复失败。\n"
        "Equivalent to HTTP POST /api/wifi/restore.",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {
            int restored = http_api_wifi_restore_from_sd();
            return restored;
        });

    // self.reboot 已经存在，下面改用统一实现
    // （保留原始 Application::Reboot 路径以确保事件循环正确处理）

    // -------- SdCard API --------

    AddUserOnlyTool("self.sdcard.get_info",
        "Get SD card info (mount point/log status/HTTP status). "
        "Equivalent to HTTP GET /api/sdcard/info.",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {
            cJSON* root = http_api_sdcard_info();
            char* json = cJSON_PrintUnformatted(root);
            std::string result = json ? json : "{}";
            if (json) free(json);
            cJSON_Delete(root);
            return result;
        });

    AddUserOnlyTool("self.sdcard.list_logs",
        "List all log files on SD card. "
        "Equivalent to HTTP GET /api/sdcard/logs.",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {
            cJSON* root = http_api_sdcard_logs_list();
            char* json = cJSON_PrintUnformatted(root);
            std::string result = json ? json : "[]";
            if (json) free(json);
            cJSON_Delete(root);
            return result;
        });

    AddUserOnlyTool("self.sdcard.list_shots",
        "List all screenshot files on SD card. "
        "Equivalent to HTTP GET /api/sdcard/shots.",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {
            cJSON* root = http_api_sdcard_shots_list();
            char* json = cJSON_PrintUnformatted(root);
            std::string result = json ? json : "[]";
            if (json) free(json);
            cJSON_Delete(root);
            return result;
        });

    AddUserOnlyTool("self.sdcard.delete_log",
        "Delete a specific log file. "
        "Equivalent to HTTP DELETE /api/sdcard/logs/<name>.",
        PropertyList({
            Property("name", kPropertyTypeString)
        }),
        [](const PropertyList& properties) -> ReturnValue {
            auto name = properties["name"].value<std::string>();
            char err[128] = {0};
            if (!http_api_sdcard_delete_log(name.c_str(), err, sizeof(err))) {
                throw std::runtime_error(err);
            }
            return true;
        });

    AddUserOnlyTool("self.sdcard.delete_shot",
        "Delete a specific screenshot file. "
        "Equivalent to HTTP DELETE /api/sdcard/shots/<name>.",
        PropertyList({
            Property("name", kPropertyTypeString)
        }),
        [](const PropertyList& properties) -> ReturnValue {
            auto name = properties["name"].value<std::string>();
            char err[128] = {0};
            if (!http_api_sdcard_delete_shot(name.c_str(), err, sizeof(err))) {
                throw std::runtime_error(err);
            }
            return true;
        });

    AddUserOnlyTool("self.sdcard.trigger_snapshot",
        "Trigger a screen snapshot and save to SD card. "
        "Equivalent to HTTP POST /api/sdcard/shots.",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {
            char name[64] = {0};
            char err[128] = {0};
            if (!http_api_sdcard_trigger_snapshot(name, sizeof(name), err, sizeof(err))) {
                throw std::runtime_error(err);
            }
            std::string result = R"({"ok":true,"name":")" + std::string(name) + R"("})";
            return result;
        });

    // -------- Files API --------

    AddUserOnlyTool("self.files.list",
        "List files on SD card (supports subdirectories). "
        "Equivalent to HTTP GET /api/sdcard/files?path=&recursive=.",
        PropertyList({
            Property("path", kPropertyTypeString, ""),
            Property("recursive", kPropertyTypeBoolean, false)
        }),
        [](const PropertyList& properties) -> ReturnValue {
            auto path = properties["path"].value<std::string>();
            bool recursive = properties["recursive"].value<bool>();
            cJSON* root = http_api_files_list(path.c_str(), recursive);
            char* json = cJSON_PrintUnformatted(root);
            std::string result = json ? json : "[]";
            if (json) free(json);
            cJSON_Delete(root);
            return result;
        });

    AddUserOnlyTool("self.files.upload",
        "Upload a file (image/GIF/audio/video/text/binary) to SD card. "
        "Supports auto-display after upload via 'display' parameter. "
        "Equivalent to HTTP POST /api/sdcard/files/<path>?display=1...",
        PropertyList({
            Property("path", kPropertyTypeString),
            Property("data_base64", kPropertyTypeString,
                     "Base64-encoded raw binary content of the file"),
            Property("display", kPropertyTypeBoolean, false),
            Property("x", kPropertyTypeInteger, 0),
            Property("y", kPropertyTypeInteger, 0),
            Property("scale", kPropertyTypeInteger, 100, 10, 400),
            Property("duration_ms", kPropertyTypeInteger, 0, 0, 600000),
            Property("loop", kPropertyTypeBoolean, false)
        }),
        [](const PropertyList& properties) -> ReturnValue {
            auto path = properties["path"].value<std::string>();
            auto data_b64 = properties["data_base64"].value<std::string>();
            bool display = properties["display"].value<bool>();
            int x = properties["x"].value<int>();
            int y = properties["y"].value<int>();
            int scale_pct = properties["scale"].value<int>();
            int duration = properties["duration_ms"].value<int>();
            bool loop = properties["loop"].value<bool>();
            float scale = (float)scale_pct / 100.0f;

            // Base64 解码
            // 简化：调用方必须提供 base64；ESP-IDF 提供 mbedtls base64
            // 这里使用一个轻量级 base64 解码
            size_t b64_len = data_b64.size();
            if (b64_len == 0) {
                throw std::runtime_error("data_base64 is empty");
            }
            std::vector<uint8_t> raw;
            raw.reserve((b64_len / 4 + 1) * 3);
            const char* s = data_b64.c_str();
            int val = 0, valb = -8;
            for (size_t i = 0; i < b64_len; i++) {
                unsigned char c = (unsigned char)s[i];
                if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
                const char* p = strchr("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/", c);
                if (p == nullptr) continue;
                val = (val << 6) | (int)(p - "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/");
                valb += 6;
                if (valb >= 0) {
                    raw.push_back((uint8_t)((val >> valb) & 0xFF));
                    valb -= 8;
                }
            }

            char err[128] = {0};
            cJSON* result_json = http_api_files_upload(path.c_str(),
                                                       raw.data(), raw.size(),
                                                       display, x, y, scale,
                                                       (uint32_t)duration, loop,
                                                       err, sizeof(err));
            if (result_json == nullptr) {
                throw std::runtime_error(err);
            }
            char* json = cJSON_PrintUnformatted(result_json);
            std::string result = json ? json : "{}";
            if (json) free(json);
            cJSON_Delete(result_json);
            return result;
        });

    AddUserOnlyTool("self.files.delete",
        "Delete a file or empty directory on SD card. "
        "Equivalent to HTTP DELETE /api/sdcard/files/<path>.",
        PropertyList({
            Property("path", kPropertyTypeString)
        }),
        [](const PropertyList& properties) -> ReturnValue {
            auto path = properties["path"].value<std::string>();
            char err[128] = {0};
            if (!http_api_files_delete(path.c_str(), err, sizeof(err))) {
                throw std::runtime_error(err);
            }
            return true;
        });

    // -------- Display API --------

    AddUserOnlyTool("self.display.show",
        "Show a resource (image/GIF) from SD card on the screen. "
        "Equivalent to HTTP POST /api/display/show.",
        PropertyList({
            Property("path", kPropertyTypeString),
            Property("x", kPropertyTypeInteger, 0),
            Property("y", kPropertyTypeInteger, 0),
            Property("scale", kPropertyTypeInteger, 100, 10, 400),
            Property("duration_ms", kPropertyTypeInteger, 0, 0, 600000),
            Property("loop", kPropertyTypeBoolean, false)
        }),
        [](const PropertyList& properties) -> ReturnValue {
            auto path = properties["path"].value<std::string>();
            int x = properties["x"].value<int>();
            int y = properties["y"].value<int>();
            int scale_pct = properties["scale"].value<int>();
            int duration = properties["duration_ms"].value<int>();
            bool loop = properties["loop"].value<bool>();
            float scale = (float)scale_pct / 100.0f;
            char err[128] = {0};
            if (!http_api_display_show(path.c_str(), x, y, scale,
                                       (uint32_t)duration, loop,
                                       err, sizeof(err))) {
                throw std::runtime_error(err);
            }
            return true;
        });

    AddUserOnlyTool("self.display.hide",
        "Hide the currently displayed preview resource. "
        "Equivalent to HTTP POST /api/display/hide.",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {
            http_api_display_hide();
            return true;
        });

    // -------- Audio API --------

    // self.audio.play
    //   path、url、playlist 三选一
    AddUserOnlyTool("self.audio.play",
        "Play an audio file (MP3/AAC/FLAC/Opus/WAV) on the device's speaker. "
        "Provide ONE of: `path` (SD card absolute path), `url` (HTTP/HTTPS URL, auto-downloads to /sdcard/tmp/), "
        "or `playlist` (array of paths for sequential playback). "
        "`loop` applies to single-file playback. "
        "Equivalent to HTTP POST /api/audio/play.",
        PropertyList({
            Property("path", kPropertyTypeString, ""),
            Property("url", kPropertyTypeString, ""),
            Property("playlist", kPropertyTypeString, ""),  // JSON 字符串数组（逗号分隔）
            Property("loop", kPropertyTypeBoolean, false)
        }),
        [](const PropertyList& properties) -> ReturnValue {
            auto path = properties["path"].value<std::string>();
            auto url = properties["url"].value<std::string>();
            auto playlist_str = properties["playlist"].value<std::string>();
            bool loop = properties["loop"].value<bool>();
            char err[128] = {0};
            bool ok = false;
            if (!path.empty()) {
                ok = http_api_audio_play_file(path.c_str(), loop, err, sizeof(err));
            } else if (!url.empty()) {
                ok = http_api_audio_play_url(url.c_str(), loop, err, sizeof(err));
            } else if (!playlist_str.empty()) {
                // 解析逗号分隔的路径列表（简化版，避免传数组）
                std::vector<std::string> paths;
                std::string current;
                for (char c : playlist_str) {
                    if (c == ',' || c == ';') {
                        if (!current.empty()) {
                            paths.push_back(current);
                            current.clear();
                        }
                    } else {
                        current += c;
                    }
                }
                if (!current.empty()) paths.push_back(current);
                std::vector<const char*> c_paths;
                for (auto& s : paths) c_paths.push_back(s.c_str());
                ok = http_api_audio_play_playlist(c_paths.data(), (int)c_paths.size(), loop, err, sizeof(err));
            } else {
                throw std::runtime_error("path, url, or playlist is required");
            }
            if (!ok) {
                throw std::runtime_error(err);
            }
            return true;
        });

    // self.audio.control
    AddUserOnlyTool("self.audio.control",
        "Control playback: action = 'pause' | 'resume' | 'stop'. "
        "Equivalent to HTTP POST /api/audio/control.",
        PropertyList({
            Property("action", kPropertyTypeString)
        }),
        [](const PropertyList& properties) -> ReturnValue {
            auto action = properties["action"].value<std::string>();
            char err[128] = {0};
            if (!http_api_audio_control(action.c_str(), err, sizeof(err))) {
                throw std::runtime_error(err);
            }
            return true;
        });

    // self.audio.status
    AddUserOnlyTool("self.audio.status",
        "Get the current audio playback status as a JSON object: "
        "{state, progress, file, error, playlist_index, playlist_total}. "
        "Equivalent to HTTP GET /api/audio/status.",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {
            (void)properties;
            cJSON* root = http_api_audio_status();
            if (root == nullptr) {
                throw std::runtime_error("status not available");
            }
            char* json = cJSON_PrintUnformatted(root);
            cJSON_Delete(root);
            if (json == nullptr) {
                throw std::runtime_error("status serialization failed");
            }
            // cJSON_PrintUnformatted 返回 const char*（实际是 malloc，需要 free）
            // ReturnValue 接受 std::string
            std::string result(json);
            free(json);
            return result;
        });

    // self.audio.next
    AddUserOnlyTool("self.audio.next",
        "Skip to the next track in the playlist. "
        "Returns true on success, false if no playlist is active. "
        "If at end of playlist, wraps to the first track.",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {
            (void)properties;
            return MusicPlayer::GetInstance().PlayNext();
        });

    // self.audio.prev
    AddUserOnlyTool("self.audio.prev",
        "Skip to the previous track in the playlist. "
        "Returns true on success, false if no playlist is active. "
        "If at start of playlist, wraps to the last track.",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {
            (void)properties;
            return MusicPlayer::GetInstance().PlayPrev();
        });

    // self.audio.list
    AddUserOnlyTool("self.audio.list",
        "List music files (mp3/aac/flac/opus/wav) on the SD card. "
        "`path` is the directory to scan (default '/sdcard'), "
        "`recursive` searches subdirectories. "
        "Returns a JSON string: {ok, count, files:[{name, path, size}]}. "
        "Equivalent to HTTP GET /api/audio/list.",
        PropertyList({
            Property("path", kPropertyTypeString, "/sdcard"),
            Property("recursive", kPropertyTypeBoolean, false)
        }),
        [](const PropertyList& properties) -> ReturnValue {
            auto path = properties["path"].value<std::string>();
            bool recursive = properties["recursive"].value<bool>();
            cJSON* arr = http_api_audio_list(path.c_str(), recursive);
            cJSON* root = cJSON_CreateObject();
            cJSON_AddBoolToObject(root, "ok", 1);
            cJSON_AddNumberToObject(root, "count", cJSON_GetArraySize(arr));
            cJSON_AddItemToObject(root, "files", arr);
            char* json = cJSON_PrintUnformatted(root);
            cJSON_Delete(root);
            if (json == nullptr) {
                throw std::runtime_error("list serialization failed");
            }
            std::string result(json);
            free(json);
            return result;
        });
}

void McpServer::ParseMessage(const cJSON* json) {
    // Check JSONRPC version
    auto version = cJSON_GetObjectItem(json, "jsonrpc");
    if (version == nullptr || !cJSON_IsString(version) || strcmp(version->valuestring, "2.0") != 0) {
        ESP_LOGE(TAG, "Invalid JSONRPC version: %s", version ? version->valuestring : "null");
        return;
    }
    
    // Check method
    auto method = cJSON_GetObjectItem(json, "method");
    if (method == nullptr || !cJSON_IsString(method)) {
        ESP_LOGE(TAG, "Missing method");
        return;
    }
    
    auto method_str = std::string(method->valuestring);
    if (method_str.find("notifications") == 0) {
        return;
    }
    
    // Check params
    auto params = cJSON_GetObjectItem(json, "params");
    if (params != nullptr && !cJSON_IsObject(params)) {
        ESP_LOGE(TAG, "Invalid params for method: %s", method_str.c_str());
        return;
    }

    auto id = cJSON_GetObjectItem(json, "id");
    if (id == nullptr || !cJSON_IsNumber(id)) {
        ESP_LOGE(TAG, "Invalid id for method: %s", method_str.c_str());
        return;
    }
    auto id_int = id->valueint;
    
    if (method_str == "initialize") {
        if (cJSON_IsObject(params)) {
            auto capabilities = cJSON_GetObjectItem(params, "capabilities");
            if (cJSON_IsObject(capabilities)) {
                ParseCapabilities(capabilities);
            }
        }
        auto app_desc = esp_app_get_description();
        std::string message = "{\"protocolVersion\":\"2024-11-05\",\"capabilities\":{\"tools\":{}},\"serverInfo\":{\"name\":\"" BOARD_NAME "\",\"version\":\"";
        message += app_desc->version;
        message += "\"}}";
        ReplyResult(id_int, message);
    } else if (method_str == "tools/list") {
        std::string cursor_str = "";
        bool list_user_only_tools = false;
        if (params != nullptr) {
            auto cursor = cJSON_GetObjectItem(params, "cursor");
            if (cJSON_IsString(cursor)) {
                cursor_str = std::string(cursor->valuestring);
            }
            auto with_user_tools = cJSON_GetObjectItem(params, "withUserTools");
            if (cJSON_IsBool(with_user_tools)) {
                list_user_only_tools = with_user_tools->valueint == 1;
            }
        }
        GetToolsList(id_int, cursor_str, list_user_only_tools);
    } else if (method_str == "tools/call") {
        if (!cJSON_IsObject(params)) {
            ESP_LOGE(TAG, "tools/call: Missing params");
            ReplyError(id_int, "Missing params");
            return;
        }
        auto tool_name = cJSON_GetObjectItem(params, "name");
        if (!cJSON_IsString(tool_name)) {
            ESP_LOGE(TAG, "tools/call: Missing name");
            ReplyError(id_int, "Missing name");
            return;
        }
        auto tool_arguments = cJSON_GetObjectItem(params, "arguments");
        if (tool_arguments != nullptr && !cJSON_IsObject(tool_arguments)) {
            ESP_LOGE(TAG, "tools/call: Invalid arguments");
            ReplyError(id_int, "Invalid arguments");
            return;
        }
        DoToolCall(id_int, std::string(tool_name->valuestring), tool_arguments);
    } else {
        ESP_LOGE(TAG, "Method not implemented: %s", method_str.c_str());
        ReplyError(id_int, "Method not implemented: " + method_str);
    }
}

void McpServer::ReplyResult(int id, const std::string& result) {
    std::string payload = "{\"jsonrpc\":\"2.0\",\"id\":";
    payload += std::to_string(id) + ",\"result\":";
    payload += result;
    payload += "}";
    Application::GetInstance().SendMcpMessage(payload);
}

void McpServer::ReplyError(int id, const std::string& message) {
    std::string payload = "{\"jsonrpc\":\"2.0\",\"id\":";
    payload += std::to_string(id);
    payload += ",\"error\":{\"message\":\"";
    payload += message;
    payload += "\"}}";
    Application::GetInstance().SendMcpMessage(payload);
}

void McpServer::GetToolsList(int id, const std::string& cursor, bool list_user_only_tools) {
    const int max_payload_size = 8000;
    std::string json = "{\"tools\":[";
    
    bool found_cursor = cursor.empty();
    auto it = tools_.begin();
    std::string next_cursor = "";
    
    while (it != tools_.end()) {
        // 如果我们还没有找到起始位置，继续搜索
        if (!found_cursor) {
            if ((*it)->name() == cursor) {
                found_cursor = true;
            } else {
                ++it;
                continue;
            }
        }

        if (!list_user_only_tools && (*it)->user_only()) {
            ++it;
            continue;
        }
        
        // 添加tool前检查大小
        std::string tool_json = (*it)->to_json() + ",";
        if (json.length() + tool_json.length() + 30 > max_payload_size) {
            // 如果添加这个tool会超出大小限制，设置next_cursor并退出循环
            next_cursor = (*it)->name();
            break;
        }
        
        json += tool_json;
        ++it;
    }
    
    if (json.back() == ',') {
        json.pop_back();
    }
    
    if (json.back() == '[' && !tools_.empty()) {
        // 如果没有添加任何tool，返回错误
        ESP_LOGE(TAG, "tools/list: Failed to add tool %s because of payload size limit", next_cursor.c_str());
        ReplyError(id, "Failed to add tool " + next_cursor + " because of payload size limit");
        return;
    }

    if (next_cursor.empty()) {
        json += "]}";
    } else {
        json += "],\"nextCursor\":\"" + next_cursor + "\"}";
    }
    
    ReplyResult(id, json);
}

void McpServer::DoToolCall(int id, const std::string& tool_name, const cJSON* tool_arguments) {
    auto tool_iter = std::find_if(tools_.begin(), tools_.end(), 
                                 [&tool_name](const McpTool* tool) { 
                                     return tool->name() == tool_name; 
                                 });
    
    if (tool_iter == tools_.end()) {
        ESP_LOGE(TAG, "tools/call: Unknown tool: %s", tool_name.c_str());
        ReplyError(id, "Unknown tool: " + tool_name);
        return;
    }

    PropertyList arguments = (*tool_iter)->properties();
    try {
        for (auto& argument : arguments) {
            bool found = false;
            if (cJSON_IsObject(tool_arguments)) {
                auto value = cJSON_GetObjectItem(tool_arguments, argument.name().c_str());
                if (argument.type() == kPropertyTypeBoolean && cJSON_IsBool(value)) {
                    argument.set_value<bool>(value->valueint == 1);
                    found = true;
                } else if (argument.type() == kPropertyTypeInteger && cJSON_IsNumber(value)) {
                    argument.set_value<int>(value->valueint);
                    found = true;
                } else if (argument.type() == kPropertyTypeString && cJSON_IsString(value)) {
                    argument.set_value<std::string>(value->valuestring);
                    found = true;
                }
            }

            if (!argument.has_default_value() && !found) {
                ESP_LOGE(TAG, "tools/call: Missing valid argument: %s", argument.name().c_str());
                ReplyError(id, "Missing valid argument: " + argument.name());
                return;
            }
        }
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "tools/call: %s", e.what());
        ReplyError(id, e.what());
        return;
    }

    // Use main thread to call the tool
    auto& app = Application::GetInstance();
    app.Schedule([this, id, tool_iter, arguments = std::move(arguments)]() {
        try {
            ReplyResult(id, (*tool_iter)->Call(arguments));
        } catch (const std::exception& e) {
            ESP_LOGE(TAG, "tools/call: %s", e.what());
            ReplyError(id, e.what());
        }
    });
}
