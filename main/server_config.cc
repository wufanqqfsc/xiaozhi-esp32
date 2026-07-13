#include "server_config.h"

#include "settings.h"

#include <esp_log.h>

#include <cctype>
#include <cstdio>
#include <algorithm>
#include <vector>

#define TAG "ServerConfig"

namespace ServerConfig {

namespace {

bool IsDigit(char c) {
    return std::isdigit(static_cast<unsigned char>(c)) != 0;
}

bool ParseIpv4(const std::string& ip, std::vector<int>* octets) {
    if (ip.empty() || octets == nullptr) {
        return false;
    }
    octets->clear();
    int value = -1;
    for (size_t i = 0; i <= ip.size(); ++i) {
        if (i < ip.size() && IsDigit(ip[i])) {
            int digit = ip[i] - '0';
            if (value < 0) {
                value = digit;
            } else {
                value = value * 10 + digit;
            }
            if (value > 255) {
                return false;
            }
        } else if (i < ip.size() && ip[i] != '.') {
            return false;
        } else {
            if (value < 0) {
                return false;
            }
            octets->push_back(value);
            value = -1;
        }
    }
    return octets->size() == 4;
}

}  // namespace

bool NormalizeIp(const std::string& input, std::string* out_ip, std::string* err) {
    if (out_ip == nullptr) {
        if (err) {
            *err = "out_ip is null";
        }
        return false;
    }

    std::string trimmed;
    trimmed.reserve(input.size());
    for (char c : input) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            trimmed.push_back(c);
        }
    }
    if (trimmed.empty()) {
        if (err) {
            *err = "ip is empty";
        }
        return false;
    }

    std::vector<int> octets;
    if (ParseIpv4(trimmed, &octets)) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d.%d.%d.%d",
                 octets[0], octets[1], octets[2], octets[3]);
        *out_ip = buf;
        return true;
    }

    // 简写：0.198 -> 192.168.0.198
    const size_t dot = trimmed.find('.');
    if (dot != std::string::npos && dot + 1 < trimmed.size()) {
        std::string prefix = trimmed.substr(0, dot);
        std::string suffix = trimmed.substr(dot + 1);
        if (!prefix.empty() && !suffix.empty() &&
            std::all_of(prefix.begin(), prefix.end(), IsDigit) &&
            std::all_of(suffix.begin(), suffix.end(), IsDigit)) {
            std::string candidate = "192.168." + prefix + "." + suffix;
            if (ParseIpv4(candidate, &octets)) {
                char buf[16];
                snprintf(buf, sizeof(buf), "%d.%d.%d.%d",
                         octets[0], octets[1], octets[2], octets[3]);
                *out_ip = buf;
                return true;
            }
        }
    }

    if (err) {
        *err = "invalid ip, expected IPv4 like 192.168.0.198 or 0.198";
    }
    return false;
}

std::string BuildOtaUrl(const std::string& ip) {
    return "http://" + ip + ":" + std::to_string(kOtaPort) + "/api/device/ota";
}

std::string BuildWebsocketUrl(const std::string& ip) {
    return "ws://" + ip + ":" + std::to_string(kWebsocketPort) + "/ws/xiaozhi/v1/";
}

std::string GetEffectiveOtaUrl() {
    Settings wifi_settings("wifi", false);
    std::string nvs_url = wifi_settings.GetString("ota_url", "");
    if (!nvs_url.empty()) {
        return nvs_url;
    }
#ifdef CONFIG_OTA_URL
    return CONFIG_OTA_URL;
#else
    return "";
#endif
}

std::string GetEffectiveWebsocketUrl() {
    Settings ws_settings("websocket", false);
    std::string nvs_url = ws_settings.GetString("url", "");
    if (!nvs_url.empty()) {
        return nvs_url;
    }
#ifdef CONFIG_LOCAL_WEBSOCKET_URL
    return CONFIG_LOCAL_WEBSOCKET_URL;
#else
    return "";
#endif
}

bool SetServerIp(const std::string& ip, std::string* ota_url, std::string* ws_url, std::string* err) {
    std::string normalized;
    if (!NormalizeIp(ip, &normalized, err)) {
        return false;
    }

    const std::string new_ota = BuildOtaUrl(normalized);
    const std::string new_ws = BuildWebsocketUrl(normalized);

    Settings wifi_settings("wifi", true);
    wifi_settings.SetString("ota_url", new_ota);

    Settings ws_settings("websocket", true);
    ws_settings.SetString("url", new_ws);

    ESP_LOGI(TAG, "Server IP updated: %s", normalized.c_str());
    ESP_LOGI(TAG, "  OTA URL: %s", new_ota.c_str());
    ESP_LOGI(TAG, "  WS  URL: %s", new_ws.c_str());

    if (ota_url) {
        *ota_url = new_ota;
    }
    if (ws_url) {
        *ws_url = new_ws;
    }
    return true;
}

void SyncBuildEndpointsToNvs() {
#if CONFIG_SERVER_MODE_LOCAL
#ifdef CONFIG_OTA_URL
    std::string url = CONFIG_OTA_URL;
    size_t host_start = url.find("://");
    if (host_start == std::string::npos) {
        return;
    }
    host_start += 3;
    const size_t host_end = url.find(':', host_start);
    if (host_end == std::string::npos) {
        return;
    }
    std::string host = url.substr(host_start, host_end - host_start);
    std::string err;
    if (!SetServerIp(host, nullptr, nullptr, &err)) {
        ESP_LOGW(TAG, "SyncBuildEndpointsToNvs failed: %s", err.c_str());
    } else {
        ESP_LOGI(TAG, "Synced server endpoints to NVS for host %s", host.c_str());
    }
#endif
#endif
}

}  // namespace ServerConfig
