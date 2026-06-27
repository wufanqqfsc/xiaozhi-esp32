#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
struct cJSON;
extern "C" {
#endif

// =================================================================
// 统一 HTTP + MCP API 业务逻辑层
// =================================================================
//
// 把 HTTP 处理器的业务逻辑抽出来，对外暴露为 C 兼容的统一接口，
// 便于同时被 HTTP 处理器（sdcard_log_http.cc）和 MCP 工具（mcp_server.cc）调用。
//
// 所有返回 cJSON* 的函数由调用者负责 cJSON_Delete。
// 所有返回字符串的函数（out_buf）由调用者分配缓冲区。
// 所有失败返回 false，err_buf 填充错误描述（调用者分配）。

// ---------------- Device API ----------------

// 获取设备状态（WiFi/SD卡/内存/uptime/屏幕/电池等）
cJSON* http_api_device_status(void);

// 获取设备日志（从 SD 卡读取最新日志文件）
// out_buf: 调用者分配，size bytes
// 返回实际写入字节数，0 表示无日志或失败
size_t http_api_device_logs(char* out_buf, size_t size);

// 获取 OTA/WS URL 配置（NVS vs build config）
cJSON* http_api_device_ota_url(void);

// 清除 NVS 中存储的 ota_url 和 websocket_url
// key: "ota_url" | "websocket_url" | NULL（全部）
bool http_api_device_clear_nvs(const char* key, char* err_buf, size_t err_size);

// 重启设备（不返回）
void http_api_device_reboot(void);

// ---------------- SdCard API ----------------

// SD 卡信息（容量/挂载状态/日志状态/HTTP 状态）
cJSON* http_api_sdcard_info(void);

// 列出 SD 卡上的日志文件
cJSON* http_api_sdcard_logs_list(void);

// 列出 SD 卡上的截图文件
cJSON* http_api_sdcard_shots_list(void);

// 删除日志文件（name 为 basename，如 "xiaozhi_boot_1.log"）
bool http_api_sdcard_delete_log(const char* name, char* err_buf, size_t err_size);

// 删除截图文件（name 为 basename）
bool http_api_sdcard_delete_shot(const char* name, char* err_buf, size_t err_size);

// 触发一次屏幕截图，保存到 SD 卡
// out_name: 填充截图文件名（如 "shot_20250624_143052.jpg"），调用者分配 64 字节
bool http_api_sdcard_trigger_snapshot(char* out_name, size_t out_size,
                                       char* err_buf, size_t err_size);

// ---------------- Files API ----------------

// 列出 SD 卡任意目录的文件
// path: 相对路径（如 "images"，空表示根目录）
// recursive: 是否递归
cJSON* http_api_files_list(const char* path, bool recursive);

// 上传文件到 SD 卡（raw binary data）
// path: 相对路径（如 "images/boot.gif"）
// data: 二进制内容，size 字节
// auto_display: 上传后是否自动显示
// 其他显示参数仅在 auto_display=true 时生效
// 返回 cJSON 对象（包含 ok/path/size_bytes/content_type/displayed）
cJSON* http_api_files_upload(const char* path,
                             const uint8_t* data, size_t size,
                             bool auto_display, int x, int y,
                             float scale, uint32_t duration_ms, bool loop,
                             char* err_buf, size_t err_size);

// 删除 SD 卡上任意文件或空目录
bool http_api_files_delete(const char* path, char* err_buf, size_t err_size);

// ---------------- Display API ----------------

// 显示 SD 卡上的资源（JPG/PNG/GIF/RAW）
bool http_api_display_show(const char* path, int x, int y,
                           float scale, uint32_t duration_ms, bool loop,
                           char* err_buf, size_t err_size);

// 隐藏当前显示的资源
void http_api_display_hide(void);

// ---------------- Audio API ----------------

// 播放本地音乐文件（path 是绝对路径或相对挂载点的路径）
//   - loop: 是否循环播放
//   - err_buf: 失败时填充错误描述
// 返回：true 表示请求已受理（异步播放），false 表示参数错误或播放器不可用
bool http_api_audio_play_file(const char* path, bool loop, char* err_buf, size_t err_size);

// 播放远程音乐（url 是 http:// 或 https://，设备会先下载到 /sdcard/tmp/ 再播放）
bool http_api_audio_play_url(const char* url, bool loop, char* err_buf, size_t err_size);

// 播放播放列表（多首本地文件连续播放）
//   - paths: 路径数组（绝对路径）
//   - count: 数组长度
//   - loop: 整个列表播完后是否从头重新开始
bool http_api_audio_play_playlist(const char* const* paths, int count, bool loop,
                                  char* err_buf, size_t err_size);

// 控制播放器：action = "pause" | "resume" | "stop"
bool http_api_audio_control(const char* action, char* err_buf, size_t err_size);

// 获取当前播放状态（cJSON 包含 state/progress/file/error 字段）
cJSON* http_api_audio_status(void);

// 列出 SD 卡上的音乐文件
//   - path: 要扫描的目录（如 /sdcard、/sdcard/music）
//   - recursive: 是否递归子目录（默认 false）
//   - 返回: cJSON 数组，每个元素是 {name, path, size}
//   - 失败/空目录：返回空数组
cJSON* http_api_audio_list(const char* path, bool recursive);

// ---------------- WiFi API ----------------

// 清空 NVS 中所有 WiFi 凭据（保留 SD 卡备份）
// 调用后设备下次启动会从 SD 卡自动恢复，无需重新配网
bool http_api_wifi_clear_nvs(char* err_buf, size_t err_size);

// 强制触发从 SD 卡恢复 WiFi 配置到 NVS
// 返回恢复的网络数量；0 表示无备份或失败
int http_api_wifi_restore_from_sd(void);

// 查看 WiFi 配置状态（NVS 条数 + SD 卡备份条数 + 备份路径）
cJSON* http_api_wifi_status(void);

// ---------------- Helpers ----------------

// 检查文件路径是否安全（防路径遍历）
bool http_api_is_safe_path(const char* path);

// 设置/获取 SD 卡挂载点（由 HTTP 服务启动时调用）
void http_api_set_mount_point(const char* mp);
const char* http_api_get_mount_point(void);

#ifdef __cplusplus
}
#endif