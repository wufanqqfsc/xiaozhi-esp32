#pragma once

#include <string>

namespace ServerConfig {

constexpr int kOtaPort = 8091;
constexpr int kWebsocketPort = 8092;

// 将 "192.168.0.198" / "0.198" 规范化为完整 IPv4
bool NormalizeIp(const std::string& input, std::string* out_ip, std::string* err);

std::string BuildOtaUrl(const std::string& ip);
std::string BuildWebsocketUrl(const std::string& ip);

// NVS 优先，其次编译期 CONFIG_*
std::string GetEffectiveOtaUrl();
std::string GetEffectiveWebsocketUrl();

// 写入 NVS 并返回生效 URL；失败时 err 非空
bool SetServerIp(const std::string& ip, std::string* ota_url, std::string* ws_url, std::string* err);

// 将编译期 OTA/WS 地址同步到 NVS（覆盖旧错误 IP，确保与 sdkconfig 一致）
void SyncBuildEndpointsToNvs();

}  // namespace ServerConfig
