#include "sdcard_log_http.h"
#include "sdcard_log.h"
#include "display/snapshot/snapshot_service.h"
#include "display/display.h"
#include "display/lvgl_display/jpg/image_to_jpeg.h"
#include "board.h"
#include "settings.h"

#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_log.h>
#include <esp_http_server.h>
#include <esp_lvgl_port.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <lvgl.h>
#include <cJSON.h>

#define TAG "SdCardLogHttp"

static httpd_handle_t g_server = nullptr;
static char g_mount_point[64] = {0};
static uint16_t g_port = 0;
static char g_last_screenshot[128] = {0};  // 最后一次截图文件名
static char g_last_error_screenshot[128] = {0};  // 最后一次错误截图文件名

// 生成截图文件名（格式：<mount_point>/shot_YYYYMMDD_HHMMSS.jpg）
static void make_screenshot_filename(char* buf, size_t buflen) {
    time_t now = time(nullptr);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    // 先复制挂载点路径
    strncpy(buf, g_mount_point, buflen - 32);
    buf[buflen - 32] = '\0';
    // 追加截图文件名
    char* append = buf + strlen(buf);
    strftime(append, 32, "/shot_%Y%m%d_%H%M%S.jpg", &tm_info);
}

// 生成错误截图文件名（格式：<mount_point>/error_<tag>_YYYYMMDD_HHMMSS.jpg）
static void make_error_screenshot_filename(char* buf, size_t buflen, const char* error_tag) {
    time_t now = time(nullptr);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    // 先复制挂载点路径
    strncpy(buf, g_mount_point, buflen - 64);
    buf[buflen - 64] = '\0';
    // 追加错误截图文件名
    char* append = buf + strlen(buf);
    char tag_safe[16] = {0};
    // 只保留字母数字
    for (int i = 0; i < 15 && error_tag && error_tag[i]; i++) {
        if (isalnum(error_tag[i])) tag_safe[i] = error_tag[i];
    }
    if (tag_safe[0] == '\0') strcpy(tag_safe, "ERR");
    strftime(append, 64, "/error_%s_%Y%m%d_%H%M%S.jpg", &tm_info);
    // 替换 %s 为 tag_safe
    char* pct_s = strstr(append, "%s");
    if (pct_s) {
        char temp[64];
        snprintf(temp, sizeof(temp), "/error_%s_", tag_safe);
        strftime(temp + strlen(temp), 32, "%Y%m%d_%H%M%S.jpg", &tm_info);
        strcpy(append, temp);
    }
}

// LVGL截图并保存为JPEG到SD卡
static bool take_screenshot_sync(const char* filepath) {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();

    if (!display) {
        ESP_LOGE(TAG, "Display not available");
        return false;
    }

    int width = display->width();
    int height = display->height();

    // LVGL 快照必须在 LVGL 线程中执行
    lv_draw_buf_t* draw_buf = nullptr;

    if (lvgl_port_lock(5000)) {
        lv_obj_invalidate(lv_screen_active());
        lv_refr_now(lv_display_get_default());
        draw_buf = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);
        lvgl_port_unlock();
    } else {
        ESP_LOGE(TAG, "Failed to acquire LVGL lock");
        return false;
    }

    if (!draw_buf) {
        ESP_LOGE(TAG, "Failed to take snapshot");
        return false;
    }

    // 复制数据
    uint8_t* src_data = (uint8_t*)draw_buf->data;
    size_t rgb_size = width * height * 2;
    uint8_t* rgb_data = (uint8_t*)heap_caps_malloc(rgb_size, MALLOC_CAP_SPIRAM);
    if (!rgb_data) {
        ESP_LOGE(TAG, "Failed to allocate RGB buffer");
        lv_draw_buf_destroy(draw_buf);
        return false;
    }
    memcpy(rgb_data, src_data, rgb_size);
    lv_draw_buf_destroy(draw_buf);

    // 转换为 JPEG
    uint8_t* jpeg_data = nullptr;
    size_t jpeg_len = 0;
    bool ret = image_to_jpeg(rgb_data, rgb_size, width, height,
                             V4L2_PIX_FMT_RGB565, 80, &jpeg_data, &jpeg_len);
    heap_caps_free(rgb_data);

    if (!ret || !jpeg_data) {
        ESP_LOGE(TAG, "JPEG encoding failed");
        return false;
    }

    // 保存到 SD 卡
    int fd = open(filepath, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        ESP_LOGE(TAG, "Failed to create file: %s", filepath);
        free(jpeg_data);
        return false;
    }

    ssize_t written = write(fd, jpeg_data, jpeg_len);
    close(fd);
    free(jpeg_data);

    if (written != (ssize_t)jpeg_len) {
        ESP_LOGE(TAG, "Write incomplete: %d/%d", (int)written, (int)jpeg_len);
        return false;
    }

    ESP_LOGI(TAG, "Screenshot saved: %s (%d bytes)", filepath, (int)jpeg_len);
    return true;
}

// 异步截图任务
static void screenshot_task(void* arg) {
    char filepath[320];
    strncpy(filepath, (const char*)arg, sizeof(filepath) - 1);
    filepath[sizeof(filepath) - 1] = '\0';
    free(arg);

    bool ok = take_screenshot_sync(filepath);

    if (ok) {
        const char* fname = strrchr(filepath, '/');
        if (fname) fname++; else fname = filepath;
        strncpy(g_last_screenshot, fname, sizeof(g_last_screenshot) - 1);
        g_last_screenshot[sizeof(g_last_screenshot) - 1] = '\0';
    }
    ESP_LOGI(TAG, "Async screenshot: %s -> %s", ok ? "OK" : "FAILED", g_last_screenshot);

    vTaskDelete(NULL);
}

static char* url_decode(const char* src) {
    size_t len = strlen(src);
    char* dst = (char*)malloc(len + 1);
    if (dst == nullptr) return nullptr;
    size_t j = 0;
    for (size_t i = 0; i < len; i++, j++) {
        if (src[i] == '%' && i + 2 < len && isxdigit((uint8_t)src[i+1]) && isxdigit((uint8_t)src[i+2])) {
            char hex[3] = {src[i+1], src[i+2], 0};
            dst[j] = (char)strtol(hex, nullptr, 16);
            i += 2;
        } else if (src[i] == '+') {
            dst[j] = ' ';
        } else {
            dst[j] = src[i];
        }
    }
    dst[j] = '\0';
    return dst;
}

static bool is_safe_filename(const char* name) {
    if (name == nullptr || name[0] == '\0') return false;
    if (strlen(name) > 200) return false;
    if (strstr(name, "..") != nullptr) return false;
    if (strchr(name, '/') != nullptr) return false;
    if (name[0] == '.') return false;
    return true;
}

static esp_err_t handle_root(httpd_req_t* req) {
    const char* html =
        "<!DOCTYPE html><html><head><meta charset='utf-8'><title>Xiaozhi SD Card Logs</title>"
        "<style>body{font-family:system-ui;margin:20px;background:#1a1a1a;color:#e0e0e0;}"
        "h1{color:#D4AF37;}"
        ".card{background:#2a2a2a;padding:16px;border-radius:8px;margin-bottom:16px;}"
        "table{border-collapse:collapse;width:100%;margin-top:16px;}"
        "th,td{text-align:left;padding:10px 12px;border-bottom:1px solid #3a3a3a;}"
        "th{color:#D4AF37;}"
        "a.btn{display:inline-block;padding:6px 12px;margin-right:8px;"
        "background:#D4AF37;color:#000;text-decoration:none;border-radius:4px;font-size:13px;font-weight:600;}"
        "a.btn.del{background:#c62828;color:#fff;}"
        ".bar{height:10px;background:#3a3a3a;border-radius:5px;overflow:hidden;margin-top:8px;}"
        ".bar>div{height:100%;background:#D4AF37;}"
        "#info{font-size:14px;color:#aaa;}"
        "</style></head><body>"
        "<h1>Xiaozhi ESP32 SD Card Logs</h1>"
        "<div class='card'>"
        "<div id='info'>Loading...</div>"
        "<div class='bar' style='width:100%'><div id='usage' style='width:0%'></div>"
        "</div>"
        "<div class='card'>"
        "<h2 style='color:#D4AF37;margin-top:0;'>Log Files</h2>"
        "<table><thead><tr><th>Name</th><th>Size</th><th>Actions</th></tr></thead>"
        "<tbody id='files'><tr><td colspan='3' style='color:#888;'>Loading...</td></tr></tbody>"
        "</table></div>"
        "<script>"
        "async function load(){try{"
        "const r=await fetch('/api/sdcard/info');const d=await r.json();"
        "document.getElementById('info').textContent="
        "  `Mount: ${d.mount_point} | Used: ${(d.used_bytes/1024/1024).toFixed(1)} MB / ${(d.total_bytes/1024/1024).toFixed(1)} MB | Log Active: ${d.log_active}; "
        "document.getElementById('usage').style.width=`${(d.used_bytes/d.total_bytes*100).toFixed(1)}%`;"
        "const r2=await fetch('/api/sdcard/logs');const f=await r2.json();"
        "const tb=document.getElementById('files');tb.innerHTML='';"
        "if(f.length===0){tb.innerHTML='<tr><td colspan=3 style=\\\"text-align:center;color:#888;'>No log files</td></tr>';return;}"
        "f.forEach(x=>{const tr=document.createElement('tr');"
        "tr.innerHTML=`<td>${x.name}</td><td>${(x.size_bytes/1024).toFixed(1)} KB</td>"
        "<td><a class='btn' href='/api/sdcard/logs/${encodeURIComponent(x.name)}'>Download</a>"
        "<a class='btn del' href='#' onclick=\\\"del('${x.name}');return false;\\\">Delete</a></td>`;"
        "tb.appendChild(tr);});"
        "}catch(e){document.getElementById('info').textContent='Error: '+e.message;}}"
        "async function del(name){if(!confirm('Delete '+name+'?'))return;"
        "await fetch('/api/sdcard/logs/'+encodeURIComponent(name),{method:'DELETE'});"
        "load();}"
        "load();"
        "</script></body></html>";
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t handle_sdcard_info(httpd_req_t* req) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "mount_point", g_mount_point);
    cJSON_AddNumberToObject(root, "total_bytes", 0);
    cJSON_AddNumberToObject(root, "used_bytes", 0);
    cJSON_AddNumberToObject(root, "free_bytes", 0);
    cJSON_AddBoolToObject(root, "log_active", SdCardLogIsActive());

    char* json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str ? json_str : "{}", HTTPD_RESP_USE_STRLEN);
    if (json_str) free(json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t handle_logs_list(httpd_req_t* req) {
    DIR* d = opendir(g_mount_point);
    if (d == nullptr) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    cJSON* root = cJSON_CreateArray();
    struct dirent* entry;
    char path[320];
    struct stat st;
    while ((entry = readdir(d)) != nullptr) {
        const char* name = entry->d_name;
        if (name[0] == '.') continue;
        size_t nlen = strlen(name);
        if (nlen < 4 || strcmp(name + nlen - 4, ".log") != 0) continue;
        snprintf(path, sizeof(path), "%s/%s", g_mount_point, name);
        if (stat(path, &st) != 0) continue;
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name", name);
        cJSON_AddNumberToObject(item, "size_bytes", (double)st.st_size);
        cJSON_AddNumberToObject(item, "mtime", (double)st.st_mtime);
        cJSON_AddItemToArray(root, item);
    }
    closedir(d);
    char* json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str ? json_str : "[]", HTTPD_RESP_USE_STRLEN);
    if (json_str) free(json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

// 从 uri 中提取文件名（前缀 /api/sdcard/logs/ 之后的部分）
static const char* extract_filename(const char* uri) {
    const char* prefix = "/api/sdcard/logs/";
    size_t plen = strlen(prefix);
    if (strncmp(uri, prefix, plen) != 0) return nullptr;
    const char* name = uri + plen;
    if (name[0] == '\0') return nullptr;
    return name;
}

static esp_err_t handle_log_download(httpd_req_t* req) {
    const char* enc_name = extract_filename(req->uri);
    if (enc_name == nullptr) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    char* name = url_decode(enc_name);
    if (name == nullptr) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    if (!is_safe_filename(name)) {
        free(name);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Bad Request", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    char path[320];
    snprintf(path, sizeof(path), "%s/%s", g_mount_point, name);
    free(name);

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    struct stat st;
    if (fstat(fd, &st) != 0) {
        close(fd);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    char disp[256];
    snprintf(disp, sizeof(disp), "attachment; filename=\"%s\"", enc_name);
    httpd_resp_set_hdr(req, "Content-Disposition", disp);

    const int CHUNK = 8192;
    char* buf = (char*)malloc(CHUNK);
    if (buf == nullptr) {
        close(fd);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    esp_err_t ret = ESP_OK;
    ssize_t n;
    while ((n = read(fd, buf, CHUNK)) > 0) {
        if (httpd_resp_send_chunk(req, buf, n) != ESP_OK) {
            ret = ESP_FAIL;
            break;
        }
    }
    free(buf);
    close(fd);
    httpd_resp_send_chunk(req, nullptr, 0);
    return ret;
}

static esp_err_t handle_log_delete(httpd_req_t* req) {
    const char* enc_name = extract_filename(req->uri);
    if (enc_name == nullptr) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    char* name = url_decode(enc_name);
    if (name == nullptr) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    if (!is_safe_filename(name)) {
        free(name);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Bad Request", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    char path[320];
    snprintf(path, sizeof(path), "%s/%s", g_mount_point, name);
    free(name);

    if (unlink(path) != 0) {
        if (errno == ENOENT) {
            httpd_resp_send_404(req);
        } else {
            httpd_resp_send_500(req);
        }
        return ESP_FAIL;
    }
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    char* json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str ? json_str : "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
    if (json_str) free(json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

// 截图相关 handler 声明
static esp_err_t handle_shots_list(httpd_req_t* req);
static esp_err_t handle_shots_capture(httpd_req_t* req);
static esp_err_t handle_shot_download(httpd_req_t* req);
static esp_err_t handle_shot_delete(httpd_req_t* req);
// 设备相关 handler 声明
static esp_err_t handle_device_status(httpd_req_t* req);
static esp_err_t handle_device_reboot(httpd_req_t* req);
static esp_err_t handle_device_logs(httpd_req_t* req);
static esp_err_t handle_device_ota_url(httpd_req_t* req);
static esp_err_t handle_device_clear_nvs(httpd_req_t* req);
static esp_err_t handle_file_delete(httpd_req_t* req);

bool SdCardLogHttpStart(const char* mount_point, uint16_t port) {
    if (g_server != nullptr) {
        ESP_LOGW(TAG, "server already running on port %u", g_port);
        return true;
    }
    if (mount_point == nullptr || mount_point[0] == '\0') {
        ESP_LOGE(TAG, "invalid mount_point");
        return false;
    }
    strncpy(g_mount_point, mount_point, sizeof(g_mount_point) - 1);
    g_mount_point[sizeof(g_mount_point) - 1] = '\0';

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    config.max_uri_handlers = 16;  // 增加以支持所有 API
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 10;
    // 启用 wildcard URI 匹配（支持 * 通配符）
    config.uri_match_fn = httpd_uri_match_wildcard;

    esp_err_t ret = httpd_start(&g_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s (port=%u)", esp_err_to_name(ret), port);
        g_server = nullptr;
        return false;
    }
    g_port = port;

    httpd_uri_t uri_root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = handle_root,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(g_server, &uri_root);

    httpd_uri_t uri_info = {
        .uri = "/api/sdcard/info",
        .method = HTTP_GET,
        .handler = handle_sdcard_info,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(g_server, &uri_info);

    httpd_uri_t uri_list = {
        .uri = "/api/sdcard/logs",
        .method = HTTP_GET,
        .handler = handle_logs_list,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(g_server, &uri_list);

    // wildcard: /api/sdcard/logs/<filename>
    httpd_uri_t uri_get = {
        .uri = "/api/sdcard/logs/*",
        .method = HTTP_GET,
        .handler = handle_log_download,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(g_server, &uri_get);

    httpd_uri_t uri_del = {
        .uri = "/api/sdcard/logs/*",
        .method = HTTP_DELETE,
        .handler = handle_log_delete,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(g_server, &uri_del);

    // 截图 API: /api/sdcard/shots
    httpd_uri_t uri_shots_list = {
        .uri = "/api/sdcard/shots",
        .method = HTTP_GET,
        .handler = handle_shots_list,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(g_server, &uri_shots_list);

    httpd_uri_t uri_shots_capture = {
        .uri = "/api/sdcard/shots",
        .method = HTTP_POST,
        .handler = handle_shots_capture,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(g_server, &uri_shots_capture);

    httpd_uri_t uri_shot_get = {
        .uri = "/api/sdcard/shots/*",
        .method = HTTP_GET,
        .handler = handle_shot_download,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(g_server, &uri_shot_get);

    httpd_uri_t uri_shot_del = {
        .uri = "/api/sdcard/shots/*",
        .method = HTTP_DELETE,
        .handler = handle_shot_delete,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(g_server, &uri_shot_del);

    // 设备状态 API: /api/device/status
    httpd_uri_t uri_device_status = {
        .uri = "/api/device/status",
        .method = HTTP_GET,
        .handler = handle_device_status,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(g_server, &uri_device_status);

    // 设备重启 API: /api/device/reboot
    httpd_uri_t uri_device_reboot = {
        .uri = "/api/device/reboot",
        .method = HTTP_POST,
        .handler = handle_device_reboot,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(g_server, &uri_device_reboot);

    // 设备日志 API: /api/device/logs
    httpd_uri_t uri_device_logs = {
        .uri = "/api/device/logs",
        .method = HTTP_GET,
        .handler = handle_device_logs,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(g_server, &uri_device_logs);

    // OTA URL 查询 API: GET /api/device/ota-url
    httpd_uri_t uri_device_ota_url = {
        .uri = "/api/device/ota-url",
        .method = HTTP_GET,
        .handler = handle_device_ota_url,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(g_server, &uri_device_ota_url);

    // 清除 NVS ota_url API: POST /api/device/clear-nvs
    httpd_uri_t uri_device_clear_nvs = {
        .uri = "/api/device/clear-nvs",
        .method = HTTP_POST,
        .handler = handle_device_clear_nvs,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(g_server, &uri_device_clear_nvs);

    // SD 卡文件删除 API: /api/sdcard/files/*
    httpd_uri_t uri_file_del = {
        .uri = "/api/sdcard/files/*",
        .method = HTTP_DELETE,
        .handler = handle_file_delete,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(g_server, &uri_file_del);

    ESP_LOGI(TAG, "HTTP server started on port %u, mount=%s", port, g_mount_point);
    return true;
}

void SdCardLogHttpStop(void) {
    if (g_server == nullptr) return;
    httpd_stop(g_server);
    g_server = nullptr;
    g_port = 0;
    ESP_LOGI(TAG, "HTTP server stopped");
}

bool SdCardLogHttpIsRunning(void) {
    return g_server != nullptr;
}

uint16_t SdCardLogHttpGetPort(void) {
    return g_port;
}

// ============= 截图相关 API =============

// HTTP GET /api/sdcard/shots - 获取截图列表
static esp_err_t handle_shots_list(httpd_req_t* req) {
    DIR* d = opendir(g_mount_point);
    if (d == nullptr) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    cJSON* root = cJSON_CreateArray();
    struct dirent* entry;
    char path[320];
    struct stat st;
    while ((entry = readdir(d)) != nullptr) {
        const char* name = entry->d_name;
        if (name[0] == '.') continue;
        size_t nlen = strlen(name);
        if (nlen < 4 || strcmp(name + nlen - 4, ".jpg") != 0) continue;
        if (strncmp(name, "shot_", 5) != 0) continue;
        snprintf(path, sizeof(path), "%s/%s", g_mount_point, name);
        if (stat(path, &st) != 0) continue;
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name", name);
        cJSON_AddNumberToObject(item, "size_bytes", (double)st.st_size);
        cJSON_AddNumberToObject(item, "mtime", (double)st.st_mtime);
        cJSON_AddBoolToObject(item, "is_last", strcmp(name, g_last_screenshot) == 0);
        cJSON_AddItemToArray(root, item);
    }
    closedir(d);
    char* json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str ? json_str : "[]", HTTPD_RESP_USE_STRLEN);
    if (json_str) free(json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

// HTTP POST /api/sdcard/shots - 触发截图（异步执行）
static esp_err_t handle_shots_capture(httpd_req_t* req) {
    char filepath[192];
    make_screenshot_filename(filepath, sizeof(filepath));

    // 创建异步任务执行截图
    char* filepath_arg = strdup(filepath);
    if (filepath_arg) {
        xTaskCreate(&screenshot_task, "screenshot", 8192, filepath_arg, 3, NULL);
    }

    // 立即返回成功（截图在后台执行）
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddStringToObject(root, "file", "pending...");
    char* json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str ? json_str : "{}", HTTPD_RESP_USE_STRLEN);
    if (json_str) free(json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

// 从 uri 中提取截图文件名
static const char* extract_shot_filename(const char* uri) {
    const char* prefix = "/api/sdcard/shots/";
    size_t plen = strlen(prefix);
    if (strncmp(uri, prefix, plen) != 0) return nullptr;
    const char* name = uri + plen;
    if (name[0] == '\0') return nullptr;
    return name;
}

// HTTP GET /api/sdcard/shots/<filename> - 下载截图
static esp_err_t handle_shot_download(httpd_req_t* req) {
    const char* enc_name = extract_shot_filename(req->uri);
    if (enc_name == nullptr) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    char* name = url_decode(enc_name);
    if (name == nullptr) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    if (!is_safe_filename(name)) {
        free(name);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Bad Request", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    char path[320];
    snprintf(path, sizeof(path), "%s/%s", g_mount_point, name);
    free(name);

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    struct stat st;
    if (fstat(fd, &st) != 0) {
        close(fd);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "image/jpeg");
    char disp[256];
    snprintf(disp, sizeof(disp), "attachment; filename=\"%s\"", enc_name);
    httpd_resp_set_hdr(req, "Content-Disposition", disp);

    const int CHUNK = 8192;
    char* buf = (char*)malloc(CHUNK);
    if (buf == nullptr) {
        close(fd);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    esp_err_t ret = ESP_OK;
    ssize_t n;
    while ((n = read(fd, buf, CHUNK)) > 0) {
        if (httpd_resp_send_chunk(req, buf, n) != ESP_OK) {
            ret = ESP_FAIL;
            break;
        }
    }
    free(buf);
    close(fd);
    httpd_resp_send_chunk(req, nullptr, 0);
    return ret;
}

// HTTP DELETE /api/sdcard/shots/<filename> - 删除截图
static esp_err_t handle_shot_delete(httpd_req_t* req) {
    const char* enc_name = extract_shot_filename(req->uri);
    if (enc_name == nullptr) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    char* name = url_decode(enc_name);
    if (name == nullptr) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    if (!is_safe_filename(name)) {
        free(name);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Bad Request", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    char path[320];
    snprintf(path, sizeof(path), "%s/%s", g_mount_point, name);
    free(name);

    if (unlink(path) != 0) {
        if (errno == ENOENT) {
            httpd_resp_send_404(req);
        } else {
            httpd_resp_send_500(req);
        }
        return ESP_FAIL;
    }
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    char* json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str ? json_str : "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
    if (json_str) free(json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

bool SdCardLogHttpTriggerSnapshot(void) {
    char filepath[192];
    make_screenshot_filename(filepath, sizeof(filepath));

    // 同步执行截图（供外部调用）
    bool ok = take_screenshot_sync(filepath);
    if (ok) {
        const char* fname = strrchr(filepath, '/');
        if (fname) fname++; else fname = filepath;
        strncpy(g_last_screenshot, fname, sizeof(g_last_screenshot) - 1);
        g_last_screenshot[sizeof(g_last_screenshot) - 1] = '\0';
    }
    return ok;
}

// ============= 设备状态 API =============

// HTTP GET /api/device/status - 获取设备状态
static esp_err_t handle_device_status(httpd_req_t* req) {
    cJSON* root = cJSON_CreateObject();

    // WiFi 状态 - 通过 HTTP 服务运行状态间接判断
    cJSON_AddBoolToObject(root, "wifi_connected", g_server != nullptr);

    // SD 卡状态
    struct stat st;
    bool sdcard_ok = (stat(g_mount_point, &st) == 0 && S_ISDIR(st.st_mode));
    cJSON_AddBoolToObject(root, "sdcard_mounted", sdcard_ok);

    // 日志状态
    cJSON_AddBoolToObject(root, "log_active", SdCardLogIsActive());

    // HTTP 服务状态
    cJSON_AddBoolToObject(root, "http_running", g_server != nullptr);
    cJSON_AddNumberToObject(root, "http_port", g_port);

    // 内存信息
    cJSON* mem = cJSON_CreateObject();
    cJSON_AddNumberToObject(mem, "free_heap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(mem, "min_free_heap", esp_get_minimum_free_heap_size());
    cJSON_AddItemToObject(root, "memory", mem);

    // 运行时间
    int64_t uptime_us = esp_timer_get_time();
    cJSON_AddNumberToObject(root, "uptime_seconds", (double)(uptime_us / 1000000));

    // 最后截图
    if (g_last_screenshot[0]) {
        cJSON_AddStringToObject(root, "last_screenshot", g_last_screenshot);
    }
    if (g_last_error_screenshot[0]) {
        cJSON_AddStringToObject(root, "last_error_screenshot", g_last_error_screenshot);
    }

    char* json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str ? json_str : "{}", HTTPD_RESP_USE_STRLEN);
    if (json_str) free(json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

// HTTP POST /api/device/reboot - 重启设备
static esp_err_t handle_device_reboot(httpd_req_t* req) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddStringToObject(root, "message", "Rebooting in 3 seconds...");

    char* json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str ? json_str : "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
    if (json_str) free(json_str);
    cJSON_Delete(root);

    ESP_LOGI(TAG, "Reboot requested via HTTP API");
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();

    return ESP_OK;
}

// HTTP GET /api/device/logs - 获取设备日志（实时串口日志）
static esp_err_t handle_device_logs(httpd_req_t* req) {
    // 返回最后一段日志（从 SD 卡读取）
    char filepath[320];
    snprintf(filepath, sizeof(filepath), "%s/xiaozhi_boot_ld.log", g_mount_point);

    int fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        // 尝试其他日志文件
        DIR* d = opendir(g_mount_point);
        if (d) {
            struct dirent* entry;
            while ((entry = readdir(d)) != nullptr) {
                const char* name = entry->d_name;
                if (strncmp(name, "xiaozhi_boot_", 13) == 0 && strstr(name, ".log")) {
                    snprintf(filepath, sizeof(filepath), "%s/%s", g_mount_point, name);
                    fd = open(filepath, O_RDONLY);
                    if (fd >= 0) break;
                }
            }
            closedir(d);
        }
    }

    if (fd < 0) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    // 获取文件大小，读取最后 10KB
    struct stat st;
    if (fstat(fd, &st) != 0) {
        close(fd);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    off_t read_offset = 0;
    size_t read_size = st.st_size;
    if (st.st_size > 10240) {
        read_offset = st.st_size - 10240;
        read_size = 10240;
    }

    lseek(fd, read_offset, SEEK_SET);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");

    const int CHUNK = 4096;
    char* buf = (char*)malloc(CHUNK);
    if (!buf) {
        close(fd);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    ssize_t n;
    while ((n = read(fd, buf, CHUNK)) > 0) {
        if (httpd_resp_send_chunk(req, buf, n) != ESP_OK) break;
    }
    free(buf);
    close(fd);
    httpd_resp_send_chunk(req, nullptr, 0);
    return ESP_OK;
}

// HTTP GET /api/device/ota-url - 查询当前 OTA URL 配置（用于诊断 NVS 覆盖）
static esp_err_t handle_device_ota_url(httpd_req_t* req) {
    Settings nvs_settings("wifi", false);
    Settings ws_settings("app", false);
    std::string nvs_ota_url = nvs_settings.GetString("ota_url", "");
    std::string nvs_ws_url = ws_settings.GetString("websocket_url", "");

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "nvs_ota_url", nvs_ota_url.c_str());
    cJSON_AddStringToObject(root, "nvs_websocket_url", nvs_ws_url.c_str());
    cJSON_AddStringToObject(root, "build_ota_url", CONFIG_OTA_URL);
    cJSON_AddStringToObject(root, "build_websocket_url", CONFIG_LOCAL_WEBSOCKET_URL);
    cJSON_AddBoolToObject(root, "nvs_ota_overridden", !nvs_ota_url.empty());
    cJSON_AddBoolToObject(root, "nvs_ws_overridden", !nvs_ws_url.empty());

    char* json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str ? json_str : "{}", HTTPD_RESP_USE_STRLEN);
    if (json_str) free(json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

// HTTP POST /api/device/clear-nvs - 清除 NVS 中存储的 ota_url 和 websocket_url
// 用法: POST /api/device/clear-nvs   (清除所有 URL 覆盖)
//       POST /api/device/clear-nvs?key=ota_url     (清除单个 key)
static esp_err_t handle_device_clear_nvs(httpd_req_t* req) {
    // 解析 query 参数 (?key=ota_url)
    std::string requested_key = "";
    if (req->uri[0] != '\0') {
        const char* q = strchr(req->uri, '?');
        if (q) {
            q++;
            if (strncmp(q, "key=", 4) == 0) {
                const char* k = q + 4;
                char* decoded = url_decode(k);
                if (decoded) {
                    requested_key = std::string(decoded);
                    free(decoded);
                }
            }
        }
    }

    // 安全检查: 只允许清除 ota_url 和 websocket_url
    auto is_allowed_key = [](const std::string& key) -> bool {
        return key == "ota_url" || key == "websocket_url";
    };

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);

    if (!requested_key.empty()) {
        // 清除单个 key
        if (!is_allowed_key(requested_key)) {
            httpd_resp_set_status(req, "400 Bad Request");
            cJSON_Delete(root);
            cJSON* err = cJSON_CreateObject();
            cJSON_AddBoolToObject(err, "ok", false);
            cJSON_AddStringToObject(err, "error", "Only 'ota_url' or 'websocket_url' allowed");
            char* ej = cJSON_PrintUnformatted(err);
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, ej ? ej : "{}", HTTPD_RESP_USE_STRLEN);
            if (ej) free(ej);
            cJSON_Delete(err);
            return ESP_FAIL;
        }

        Settings settings("wifi", true);
        if (requested_key == "ota_url") {
            settings.EraseKey("ota_url");
        } else if (requested_key == "websocket_url") {
            settings.EraseKey("websocket_url");
        }
        cJSON_AddStringToObject(root, "cleared", requested_key.c_str());
        ESP_LOGW(TAG, "NVS key '%s' erased via HTTP API (next reboot will use CONFIG_xxx_URL)", requested_key.c_str());
    } else {
        // 清除所有 URL keys
        Settings settings("wifi", true);
        settings.EraseKey("ota_url");
        settings.EraseKey("websocket_url");
        cJSON_AddStringToObject(root, "cleared", "ota_url,websocket_url");
        ESP_LOGW(TAG, "All NVS URL keys erased via HTTP API (next reboot will use CONFIG_xxx_URL)");
    }

    char* json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str ? json_str : "{}", HTTPD_RESP_USE_STRLEN);
    if (json_str) free(json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

// HTTP DELETE /api/sdcard/files/<filename> - 删除 SD 卡上的任意文件
static esp_err_t handle_file_delete(httpd_req_t* req) {
    const char* prefix = "/api/sdcard/files/";
    size_t plen = strlen(prefix);
    if (strncmp(req->uri, prefix, plen) != 0) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    const char* enc_name = req->uri + plen;
    if (enc_name[0] == '\0') {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    char* name = url_decode(enc_name);
    if (!name) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    if (!is_safe_filename(name)) {
        free(name);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Bad Request", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    char path[320];
    snprintf(path, sizeof(path), "%s/%s", g_mount_point, name);
    free(name);

    if (unlink(path) != 0) {
        if (errno == ENOENT) {
            httpd_resp_send_404(req);
        } else {
            httpd_resp_send_500(req);
        }
        return ESP_FAIL;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    char* json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str ? json_str : "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
    if (json_str) free(json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

bool SdCardLogHttpErrorSnapshot(const char* error_tag, const char* error_msg) {
    if (g_mount_point[0] == '\0') {
        ESP_LOGW(TAG, "SD card not mounted, cannot save error screenshot");
        return false;
    }

    char filepath[192];
    make_error_screenshot_filename(filepath, sizeof(filepath), error_tag);

    ESP_LOGI(TAG, "Error screenshot: tag=%s, msg=%s, path=%s", error_tag, error_msg, filepath);

    bool ok = take_screenshot_sync(filepath);
    if (ok) {
        const char* fname = strrchr(filepath, '/');
        if (fname) fname++; else fname = filepath;
        strncpy(g_last_error_screenshot, fname, sizeof(g_last_error_screenshot) - 1);
        g_last_error_screenshot[sizeof(g_last_error_screenshot) - 1] = '\0';

        // 记录错误信息到日志
        ESP_LOGI(TAG, "Error screenshot saved: %s (tag=%s, msg=%s)", fname, error_tag, error_msg);
    }
    return ok;
}
