#include "http_api_unified.h"

#include <cstring>
#include <cstdlib>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstdio>
#include <cctype>
#include <memory>
#include <vector>

#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <esp_app_desc.h>

#include <cJSON.h>

#include "sdcard_log.h"
#include "sdcard_log_http.h"
#include "settings.h"
#include "board.h"
#include "display/display.h"
#include "display/lvgl_display/lvgl_image.h"
#include "wifi_config_backup.h"
#include <ssid_manager.h>

#define TAG "HttpApiUnified"

// =================================================================
// SD 卡路径辅助
// =================================================================

static char s_mount_point[64] = "/sdcard";

extern "C" void http_api_set_mount_point(const char* mp) {
    if (mp == nullptr) return;
    strncpy(s_mount_point, mp, sizeof(s_mount_point) - 1);
    s_mount_point[sizeof(s_mount_point) - 1] = '\0';
}

extern "C" const char* http_api_get_mount_point(void) {
    return s_mount_point;
}

// =================================================================
// 工具函数
// =================================================================

static bool is_safe_filename_local(const char* name) {
    if (name == nullptr || name[0] == '\0') return false;
    if (strlen(name) > 200) return false;
    if (strstr(name, "..") != nullptr) return false;
    if (strchr(name, '/') != nullptr) return false;
    if (name[0] == '.') return false;
    return true;
}

static bool is_safe_path_local(const char* path) {
    if (path == nullptr || path[0] == '\0') return false;
    if (strlen(path) > 200) return false;
    if (path[0] == '/') return false;
    if (strstr(path, "..") != nullptr) return false;
    if (strstr(path, "//") != nullptr) return false;
    const char* slash = path;
    while (slash != nullptr) {
        const char* next = strchr(slash, '/');
        size_t len = (next != nullptr) ? (size_t)(next - slash) : strlen(slash);
        if (len > 0 && slash[0] == '.') return false;
        slash = next;
        if (slash == nullptr) break;
        slash++;
    }
    return true;
}

static const char* get_content_type_local(const char* filename) {
    if (filename == nullptr) return "application/octet-stream";
    const char* dot = strrchr(filename, '.');
    if (dot == nullptr) return "application/octet-stream";
    char ext[16] = {0};
    size_t elen = strlen(dot);
    if (elen >= sizeof(ext)) return "application/octet-stream";
    for (size_t i = 0; i < elen && i < sizeof(ext) - 1; i++) {
        ext[i] = (char)tolower((uint8_t)dot[i]);
    }
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".png") == 0) return "image/png";
    if (strcmp(ext, ".gif") == 0) return "image/gif";
    if (strcmp(ext, ".webp") == 0) return "image/webp";
    if (strcmp(ext, ".bmp") == 0) return "image/bmp";
    if (strcmp(ext, ".svg") == 0) return "image/svg+xml";
    if (strcmp(ext, ".mp3") == 0) return "audio/mpeg";
    if (strcmp(ext, ".wav") == 0) return "audio/wav";
    if (strcmp(ext, ".ogg") == 0) return "audio/ogg";
    if (strcmp(ext, ".m4a") == 0) return "audio/mp4";
    if (strcmp(ext, ".aac") == 0) return "audio/aac";
    if (strcmp(ext, ".flac") == 0) return "audio/flac";
    if (strcmp(ext, ".mp4") == 0) return "video/mp4";
    if (strcmp(ext, ".avi") == 0) return "video/x-msvideo";
    if (strcmp(ext, ".mov") == 0) return "video/quicktime";
    if (strcmp(ext, ".mkv") == 0) return "video/x-matroska";
    if (strcmp(ext, ".webm") == 0) return "video/webm";
    if (strcmp(ext, ".txt") == 0 || strcmp(ext, ".log") == 0) return "text/plain";
    if (strcmp(ext, ".json") == 0) return "application/json";
    if (strcmp(ext, ".xml") == 0) return "application/xml";
    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) return "text/html";
    if (strcmp(ext, ".css") == 0) return "text/css";
    if (strcmp(ext, ".js") == 0) return "application/javascript";
    if (strcmp(ext, ".pdf") == 0) return "application/pdf";
    if (strcmp(ext, ".zip") == 0) return "application/zip";
    return "application/octet-stream";
}

static bool join_full_path_local(char* dst, size_t dst_size,
                                  const char* mount, const char* rel) {
    if (dst == nullptr || mount == nullptr || rel == nullptr) return false;
    int n = snprintf(dst, dst_size, "%s/%s", mount, rel);
    return n > 0 && (size_t)n < dst_size;
}

static bool ensure_parent_dirs_local(const char* mount_point, const char* rel_path) {
    if (mount_point == nullptr || rel_path == nullptr) return false;
    const char* slash = strrchr(rel_path, '/');
    if (slash == nullptr) return true;
    size_t sub_len = (size_t)(slash - rel_path);
    if (sub_len == 0) return true;
    size_t mount_len = strlen(mount_point);
    char* parent = (char*)malloc(mount_len + 1 + sub_len + 1);
    if (parent == nullptr) return false;
    int n = snprintf(parent, mount_len + 1 + sub_len + 1,
                     "%s/%.*s", mount_point, (int)sub_len, rel_path);
    if (n < 0 || (size_t)n >= mount_len + 1 + sub_len + 1) {
        free(parent);
        return false;
    }
    char* p = parent + mount_len + 1;
    while (*p) {
        if (*p == '/') {
            *p = '\0';
            struct stat st;
            if (stat(parent, &st) != 0) {
                if (mkdir(parent, 0775) != 0 && errno != EEXIST) {
                    free(parent);
                    return false;
                }
            } else if (!S_ISDIR(st.st_mode)) {
                free(parent);
                return false;
            }
            *p = '/';
        }
        p++;
    }
    free(parent);
    return true;
}

static int compare_strings_asc(const void* a, const void* b) {
    return strcmp(*(const char**)a, *(const char**)b);
}

static void copy_err(char* dst, size_t size, const char* src) {
    if (dst == nullptr || size == 0) return;
    if (src == nullptr) src = "";
    strncpy(dst, src, size - 1);
    dst[size - 1] = '\0';
}

// =================================================================
// Device API 实现
// =================================================================

extern "C" cJSON* http_api_device_status(void) {
    cJSON* root = cJSON_CreateObject();
    struct stat st;
    bool sdcard_ok = (stat(s_mount_point, &st) == 0 && S_ISDIR(st.st_mode));
    cJSON_AddBoolToObject(root, "sdcard_mounted", sdcard_ok);
    cJSON_AddBoolToObject(root, "log_active", SdCardLogIsActive());
    cJSON_AddBoolToObject(root, "http_running", SdCardLogHttpIsRunning());
    cJSON_AddNumberToObject(root, "http_port", SdCardLogHttpGetPort());

    cJSON* mem = cJSON_CreateObject();
    cJSON_AddNumberToObject(mem, "free_heap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(mem, "min_free_heap", esp_get_minimum_free_heap_size());
    cJSON_AddItemToObject(root, "memory", mem);

    int64_t uptime_us = esp_timer_get_time();
    cJSON_AddNumberToObject(root, "uptime_seconds", (double)(uptime_us / 1000000));

    const esp_app_desc_t* app = esp_app_get_description();
    if (app) {
        cJSON_AddStringToObject(root, "app_version", app->version);
        cJSON_AddStringToObject(root, "idf_version", app->idf_ver);
    }
    cJSON_AddStringToObject(root, "board_type", Board::GetInstance().GetBoardType().c_str());
    return root;
}

extern "C" size_t http_api_device_logs(char* out_buf, size_t size) {
    if (out_buf == nullptr || size == 0) return 0;
    char filepath[320];
    snprintf(filepath, sizeof(filepath), "%s/xiaozhi_boot_ld.log", s_mount_point);

    int fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        DIR* d = opendir(s_mount_point);
        if (d) {
            struct dirent* entry;
            while ((entry = readdir(d)) != nullptr) {
                const char* name = entry->d_name;
                if (strncmp(name, "xiaozhi_boot_", 13) == 0 && strstr(name, ".log")) {
                    snprintf(filepath, sizeof(filepath), "%s/%s", s_mount_point, name);
                    fd = open(filepath, O_RDONLY);
                    if (fd >= 0) break;
                }
            }
            closedir(d);
        }
    }
    if (fd < 0) return 0;

    struct stat st;
    if (fstat(fd, &st) != 0) {
        close(fd);
        return 0;
    }
    off_t read_offset = 0;
    size_t read_size = (size_t)st.st_size;
    if (st.st_size > (off_t)size) {
        read_offset = st.st_size - (off_t)size;
        read_size = size;
    }
    lseek(fd, read_offset, SEEK_SET);
    size_t total = 0;
    while (total < read_size) {
        ssize_t n = read(fd, out_buf + total, read_size - total);
        if (n <= 0) break;
        total += n;
    }
    close(fd);
    return total;
}

extern "C" cJSON* http_api_device_ota_url(void) {
    Settings nvs_settings("wifi", false);
    Settings ws_settings("app", false);
    std::string nvs_ota_url = nvs_settings.GetString("ota_url", "");
    std::string nvs_ws_url = ws_settings.GetString("websocket_url", "");

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "nvs_ota_url", nvs_ota_url.c_str());
    cJSON_AddStringToObject(root, "nvs_websocket_url", nvs_ws_url.c_str());
#ifdef CONFIG_OTA_URL
    cJSON_AddStringToObject(root, "build_ota_url", CONFIG_OTA_URL);
#endif
#ifdef CONFIG_LOCAL_WEBSOCKET_URL
    cJSON_AddStringToObject(root, "build_websocket_url", CONFIG_LOCAL_WEBSOCKET_URL);
#endif
    cJSON_AddBoolToObject(root, "nvs_ota_overridden", !nvs_ota_url.empty());
    cJSON_AddBoolToObject(root, "nvs_ws_overridden", !nvs_ws_url.empty());
    return root;
}

extern "C" bool http_api_device_clear_nvs(const char* key, char* err_buf, size_t err_size) {
    if (key != nullptr && key[0] != '\0') {
        if (strcmp(key, "ota_url") != 0 && strcmp(key, "websocket_url") != 0) {
            copy_err(err_buf, err_size, "Only 'ota_url' or 'websocket_url' allowed");
            return false;
        }
        Settings settings("wifi", true);
        if (strcmp(key, "ota_url") == 0) settings.EraseKey("ota_url");
        else settings.EraseKey("websocket_url");
        ESP_LOGW(TAG, "NVS key '%s' erased via API", key);
    } else {
        Settings settings("wifi", true);
        settings.EraseKey("ota_url");
        settings.EraseKey("websocket_url");
        ESP_LOGW(TAG, "All NVS URL keys erased via API");
    }
    return true;
}

extern "C" void http_api_device_reboot(void) {
    ESP_LOGW(TAG, "Reboot requested via API");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

// =================================================================
// SdCard API 实现
// =================================================================

extern "C" cJSON* http_api_sdcard_info(void) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "mount_point", s_mount_point);
    cJSON_AddNumberToObject(root, "total_bytes", 0);
    cJSON_AddNumberToObject(root, "used_bytes", 0);
    cJSON_AddNumberToObject(root, "free_bytes", 0);
    cJSON_AddBoolToObject(root, "log_active", SdCardLogIsActive());
    cJSON_AddBoolToObject(root, "http_running", SdCardLogHttpIsRunning());
    cJSON_AddNumberToObject(root, "http_port", SdCardLogHttpGetPort());
    return root;
}

static cJSON* list_files_filter(const char* mount, const char* prefix,
                                 const char* suffix) {
    cJSON* arr = cJSON_CreateArray();
    DIR* d = opendir(mount);
    if (d == nullptr) return arr;
    std::vector<char*> names;
    struct dirent* entry;
    while ((entry = readdir(d)) != nullptr) {
        const char* name = entry->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        if (prefix && strncmp(name, prefix, strlen(prefix)) != 0) continue;
        if (suffix && !strstr(name, suffix)) continue;
        names.push_back(strdup(name));
    }
    closedir(d);
    qsort(names.data(), names.size(), sizeof(char*), compare_strings_asc);
    for (char* name : names) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name", name);
        char fullpath[400];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", mount, name);
        struct stat st;
        if (stat(fullpath, &st) == 0) {
            cJSON_AddNumberToObject(item, "size_bytes", (double)st.st_size);
            cJSON_AddNumberToObject(item, "mtime", (double)st.st_mtime);
        } else {
            cJSON_AddNumberToObject(item, "size_bytes", 0);
            cJSON_AddNumberToObject(item, "mtime", 0);
        }
        cJSON_AddItemToArray(arr, item);
        free(name);
    }
    return arr;
}

extern "C" cJSON* http_api_sdcard_logs_list(void) {
    return list_files_filter(s_mount_point, "xiaozhi_boot_", ".log");
}

extern "C" cJSON* http_api_sdcard_shots_list(void) {
    return list_files_filter(s_mount_point, "shot_", ".jpg");
}

extern "C" bool http_api_sdcard_delete_log(const char* name, char* err_buf, size_t err_size) {
    if (name == nullptr || !is_safe_filename_local(name)) {
        copy_err(err_buf, err_size, "invalid or unsafe name");
        return false;
    }
    if (strncmp(name, "xiaozhi_boot_", 13) != 0 || !strstr(name, ".log")) {
        copy_err(err_buf, err_size, "only xiaozhi_boot_*.log allowed");
        return false;
    }
    char path[320];
    if (!join_full_path_local(path, sizeof(path), s_mount_point, name)) {
        copy_err(err_buf, err_size, "path too long");
        return false;
    }
    if (unlink(path) != 0) {
        copy_err(err_buf, err_size, strerror(errno));
        return false;
    }
    return true;
}

extern "C" bool http_api_sdcard_delete_shot(const char* name, char* err_buf, size_t err_size) {
    if (name == nullptr || !is_safe_filename_local(name)) {
        copy_err(err_buf, err_size, "invalid or unsafe name");
        return false;
    }
    char path[320];
    if (!join_full_path_local(path, sizeof(path), s_mount_point, name)) {
        copy_err(err_buf, err_size, "path too long");
        return false;
    }
    if (unlink(path) != 0) {
        copy_err(err_buf, err_size, strerror(errno));
        return false;
    }
    return true;
}

extern "C" bool http_api_sdcard_trigger_snapshot(char* out_name, size_t out_size,
                                                  char* err_buf, size_t err_size) {
    if (!SdCardLogHttpTriggerSnapshot()) {
        copy_err(err_buf, err_size, "snapshot failed");
        return false;
    }
    if (out_name != nullptr && out_size > 0) {
        // 直接生成默认文件名，实际文件名由后台任务写入 NVS
        snprintf(out_name, out_size, "shot_unknown.jpg");
    }
    return true;
}

// =================================================================
// Files API 实现
// =================================================================

extern "C" bool http_api_is_safe_path(const char* path) {
    return is_safe_path_local(path);
}

extern "C" cJSON* http_api_files_list(const char* path, bool recursive) {
    cJSON* arr = cJSON_CreateArray();
    const char* subpath = (path != nullptr) ? path : "";
    if (subpath[0] != '\0' && !is_safe_path_local(subpath)) {
        return arr;
    }
    char base[320];
    if (!join_full_path_local(base, sizeof(base), s_mount_point, subpath)) {
        return arr;
    }
    DIR* dir = opendir(base);
    if (dir == nullptr) return arr;

    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name", ent->d_name);
        char fullpath[600];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", base, ent->d_name);
        struct stat st;
        bool is_dir = false;
        if (stat(fullpath, &st) == 0) {
            is_dir = S_ISDIR(st.st_mode);
            cJSON_AddNumberToObject(item, "size_bytes", is_dir ? 0 : (double)st.st_size);
            cJSON_AddNumberToObject(item, "mtime", (double)st.st_mtime);
        } else {
            cJSON_AddNumberToObject(item, "size_bytes", 0);
            cJSON_AddNumberToObject(item, "mtime", 0);
        }
        cJSON_AddBoolToObject(item, "is_dir", is_dir);
        cJSON_AddStringToObject(item, "content_type",
                                is_dir ? nullptr : get_content_type_local(ent->d_name));
        char relpath[500];
        if (subpath[0] == '\0') {
            snprintf(relpath, sizeof(relpath), "%s", ent->d_name);
        } else {
            snprintf(relpath, sizeof(relpath), "%s/%s", subpath, ent->d_name);
        }
        cJSON_AddStringToObject(item, "path", relpath);
        cJSON_AddItemToArray(arr, item);
    }
    closedir(dir);
    return arr;
}

extern "C" cJSON* http_api_files_upload(const char* path,
                                        const uint8_t* data, size_t size,
                                        bool auto_display, int x, int y,
                                        float scale, uint32_t duration_ms, bool loop,
                                        char* err_buf, size_t err_size) {
    if (path == nullptr || path[0] == '\0') {
        copy_err(err_buf, err_size, "missing path");
        return nullptr;
    }
    if (!is_safe_path_local(path)) {
        copy_err(err_buf, err_size, "unsafe path");
        return nullptr;
    }
    if (!ensure_parent_dirs_local(s_mount_point, path)) {
        copy_err(err_buf, err_size, "mkdir failed");
        return nullptr;
    }
    char fullpath[320];
    if (!join_full_path_local(fullpath, sizeof(fullpath), s_mount_point, path)) {
        copy_err(err_buf, err_size, "path too long");
        return nullptr;
    }
    FILE* fp = fopen(fullpath, "wb");
    if (fp == nullptr) {
        copy_err(err_buf, err_size, "open failed");
        return nullptr;
    }
    size_t written = fwrite(data, 1, size, fp);
    fclose(fp);
    if (written != size) {
        unlink(fullpath);
        copy_err(err_buf, err_size, "write failed");
        return nullptr;
    }
    const char* ctype = get_content_type_local(path);
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddStringToObject(root, "path", path);
    cJSON_AddNumberToObject(root, "size_bytes", (double)written);
    cJSON_AddStringToObject(root, "content_type", ctype);
    if (auto_display) {
        char display_err[128] = {0};
        bool ok = http_api_display_show(path, x, y, scale, duration_ms, loop,
                                         display_err, sizeof(display_err));
        cJSON_AddBoolToObject(root, "displayed", ok);
        if (!ok) {
            cJSON_AddStringToObject(root, "display_error", display_err);
        }
    }
    return root;
}

extern "C" bool http_api_files_delete(const char* path, char* err_buf, size_t err_size) {
    if (path == nullptr || path[0] == '\0') {
        copy_err(err_buf, err_size, "missing path");
        return false;
    }
    if (!is_safe_path_local(path)) {
        copy_err(err_buf, err_size, "unsafe path");
        return false;
    }
    char fullpath[320];
    if (!join_full_path_local(fullpath, sizeof(fullpath), s_mount_point, path)) {
        copy_err(err_buf, err_size, "path too long");
        return false;
    }
    if (unlink(fullpath) != 0) {
        if (rmdir(fullpath) != 0) {
            copy_err(err_buf, err_size, errno == ENOENT ? "not found" : strerror(errno));
            return false;
        }
    }
    return true;
}

// =================================================================
// Display API 实现
// =================================================================

extern "C" bool http_api_display_show(const char* path, int x, int y,
                                       float scale, uint32_t duration_ms, bool loop,
                                       char* err_buf, size_t err_size) {
    if (path == nullptr || path[0] == '\0') {
        copy_err(err_buf, err_size, "missing path");
        return false;
    }
    if (!is_safe_path_local(path)) {
        copy_err(err_buf, err_size, "unsafe path");
        return false;
    }
    char fullpath[320];
    if (!join_full_path_local(fullpath, sizeof(fullpath), s_mount_point, path)) {
        copy_err(err_buf, err_size, "path too long");
        return false;
    }
    struct stat st;
    if (stat(fullpath, &st) != 0) {
        copy_err(err_buf, err_size, "file not found");
        return false;
    }
    if (S_ISDIR(st.st_mode)) {
        copy_err(err_buf, err_size, "is a directory");
        return false;
    }
    size_t file_size = (size_t)st.st_size;
    uint8_t* buf = (uint8_t*)heap_caps_malloc(file_size, MALLOC_CAP_SPIRAM);
    if (buf == nullptr) buf = (uint8_t*)malloc(file_size);
    if (buf == nullptr) {
        copy_err(err_buf, err_size, "out of memory");
        return false;
    }
    FILE* fp = fopen(fullpath, "rb");
    if (fp == nullptr) {
        free(buf);
        copy_err(err_buf, err_size, "open failed");
        return false;
    }
    size_t nread = fread(buf, 1, file_size, fp);
    fclose(fp);
    if (nread != file_size) {
        free(buf);
        copy_err(err_buf, err_size, "read incomplete");
        return false;
    }

    std::unique_ptr<LvglImage> image;
    try {
        image = std::make_unique<LvglAllocatedImage>(buf, file_size);
    } catch (const std::exception& e) {
        free(buf);
        snprintf(err_buf, err_size, "decode failed: %s", e.what());
        return false;
    }

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    if (display == nullptr) {
        copy_err(err_buf, err_size, "display not available");
        return false;
    }
    display->SetPreviewImage(std::move(image));
    ESP_LOGI(TAG, "Displayed resource: %s (%zu bytes, scale=%.2f, x=%d, y=%d, duration=%ums, loop=%d)",
             path, file_size, scale, x, y, (unsigned)duration_ms, (int)loop);
    (void)duration_ms;
    (void)loop;
    return true;
}

extern "C" void http_api_display_hide(void) {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    if (display != nullptr) {
        ESP_LOGI(TAG, "Display hide requested");
    }
}

// ---------------- WiFi API ----------------

extern "C" bool http_api_wifi_clear_nvs(char* err_buf, size_t err_size) {
    WifiConfigBackup::GetInstance().ClearNvs();
    return true;
}

extern "C" int http_api_wifi_restore_from_sd(void) {
    return WifiConfigBackup::GetInstance().RestoreFromSdCard();
}

extern "C" cJSON* http_api_wifi_status(void) {
    auto& backup = WifiConfigBackup::GetInstance();
    auto& ssid_manager = SsidManager::GetInstance();

    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "nvs_count", (int)ssid_manager.GetSsidList().size());
    cJSON_AddNumberToObject(root, "sd_card_count", backup.GetSdCardNetworkCount());
    cJSON_AddBoolToObject(root, "sd_card_has_backup", backup.HasSdCardBackup());
    cJSON_AddBoolToObject(root, "sd_card_mounted", backup.GetSdCardNetworkCount() >= 0);
    cJSON_AddStringToObject(root, "backup_file", backup.GetSdCardPath());

    cJSON* networks = cJSON_CreateArray();
    for (const auto& item : ssid_manager.GetSsidList()) {
        cJSON* net = cJSON_CreateObject();
        cJSON_AddStringToObject(net, "ssid", item.ssid.c_str());
        cJSON_AddItemToArray(networks, net);
    }
    cJSON_AddItemToObject(root, "nvs_networks", networks);
    return root;
}

// =================================================================
// Audio API
// =================================================================
//
// 通过 MusicPlayer 单例控制异步播放：
//   - 接受 path（本地）或 url（远程 HTTP 下载）
//   - PlayFile/PlayUrl 都是 O(1) 投递到内部 queue，由 LVGL 主线程消费
//   - HTTP 立即返回 200，状态查询走 /api/audio/status
//
// 错误码格式：err_buf 填充英文短字符串（如 "path required" / "url must start with http"）

#include "audio/music_player.h"

extern "C" bool http_api_audio_play_file(const char* path, bool loop, char* err_buf, size_t err_size) {
    if (path == nullptr || path[0] == '\0') {
        if (err_buf && err_size > 0) snprintf(err_buf, err_size, "path required");
        return false;
    }
    ESP_LOGI(TAG, "Audio play file: %s (loop=%d)", path, loop ? 1 : 0);
    MusicPlayer::GetInstance().PlayFile(path, loop);
    return true;
}

extern "C" bool http_api_audio_play_url(const char* url, bool loop, char* err_buf, size_t err_size) {
    if (url == nullptr || url[0] == '\0') {
        if (err_buf && err_size > 0) snprintf(err_buf, err_size, "url required");
        return false;
    }
    // 简单检查前缀（避免无效 URL）
    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
        if (err_buf && err_size > 0) snprintf(err_buf, err_size, "url must start with http:// or https://");
        return false;
    }
    ESP_LOGI(TAG, "Audio play url: %s (loop=%d)", url, loop ? 1 : 0);
    MusicPlayer::GetInstance().PlayUrl(url, loop);
    return true;
}

extern "C" bool http_api_audio_play_playlist(const char* const* paths, int count, bool loop,
                                              char* err_buf, size_t err_size) {
    if (paths == nullptr || count <= 0) {
        if (err_buf && err_size > 0) snprintf(err_buf, err_size, "playlist empty");
        return false;
    }
    if (count > 100) {
        if (err_buf && err_size > 0) snprintf(err_buf, err_size, "playlist too long (max 100)");
        return false;
    }
    // 转成 std::vector<std::string> 调 MusicPlayer
    std::vector<std::string> path_vec;
    path_vec.reserve(count);
    for (int i = 0; i < count; i++) {
        if (paths[i] != nullptr) {
            path_vec.emplace_back(paths[i]);
        }
    }
    ESP_LOGI(TAG, "Audio play playlist: %zu tracks (loop=%d)", path_vec.size(), loop ? 1 : 0);
    MusicPlayer::GetInstance().PlayPlaylist(path_vec, loop);
    return true;
}

extern "C" bool http_api_audio_control(const char* action, char* err_buf, size_t err_size) {
    if (action == nullptr || action[0] == '\0') {
        if (err_buf && err_size > 0) snprintf(err_buf, err_size, "action required");
        return false;
    }
    auto& mp = MusicPlayer::GetInstance();
    if (strcmp(action, "pause") == 0) {
        mp.Pause();
        ESP_LOGI(TAG, "Audio pause");
    } else if (strcmp(action, "resume") == 0) {
        mp.Resume();
        ESP_LOGI(TAG, "Audio resume");
    } else if (strcmp(action, "stop") == 0) {
        mp.Stop();
        ESP_LOGI(TAG, "Audio stop");
    } else {
        if (err_buf && err_size > 0) snprintf(err_buf, err_size, "unknown action: %s", action);
        return false;
    }
    return true;
}

extern "C" cJSON* http_api_audio_status(void) {
    auto& mp = MusicPlayer::GetInstance();
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "state", MusicPlayerStateToString(mp.GetState()));
    cJSON_AddNumberToObject(root, "progress", mp.GetProgress());
    std::string file = mp.GetCurrentFile();
    cJSON_AddStringToObject(root, "file", file.c_str());
    cJSON_AddStringToObject(root, "error", mp.GetLastError().c_str());
    // 播放列表信息（如果有）
    const auto& pl = mp.GetCurrentPlaylist();
    if (!pl.empty()) {
        cJSON_AddNumberToObject(root, "playlist_index", (double)mp.GetCurrentPlaylistIndex());
        cJSON_AddNumberToObject(root, "playlist_total", (double)pl.size());
    }
    return root;
}

// 扫描目录中的音乐文件
//   - path: 绝对路径
//   - out: cJSON 数组（追加文件对象）
//   - recursive: 是否递归
//   - 返回: 添加的文件数
namespace {
int ScanDirForMusic(const std::string& path, cJSON* out, bool recursive) {
    DIR* dir = opendir(path.c_str());
    if (dir == nullptr) return 0;

    struct dirent* entry;
    int count = 0;
    while ((entry = readdir(dir)) != nullptr) {
        // 跳过 . ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        std::string full_path = path + "/" + entry->d_name;
        struct stat st;
        if (stat(full_path.c_str(), &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            if (recursive) {
                count += ScanDirForMusic(full_path, out, recursive);
            }
            continue;
        }

        // 文件：检查扩展名
        std::string name = entry->d_name;
        if (name.size() < 4) continue;
        std::string ext = name.substr(name.size() - 4);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext != ".mp3" && ext != ".aac" && ext != ".flac" &&
            ext != ".opus" && ext != ".wav") {
            continue;
        }

        cJSON* file_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(file_obj, "name", name.c_str());
        cJSON_AddStringToObject(file_obj, "path", full_path.c_str());
        cJSON_AddNumberToObject(file_obj, "size", (double)st.st_size);
        cJSON_AddItemToArray(out, file_obj);
        count++;
    }
    closedir(dir);
    return count;
}
}  // namespace

extern "C" cJSON* http_api_audio_list(const char* path, bool recursive) {
    cJSON* arr = cJSON_CreateArray();
    // 默认路径
    std::string scan_path = (path == nullptr || path[0] == '\0') ? "/sdcard" : path;
    int count = ScanDirForMusic(scan_path, arr, recursive);
    ESP_LOGI(TAG, "Audio list: %s (recursive=%d) -> %d files",
             scan_path.c_str(), recursive ? 1 : 0, count);
    return arr;
}