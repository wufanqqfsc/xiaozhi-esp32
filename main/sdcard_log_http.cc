#include "sdcard_log_http.h"
#include "sdcard_log.h"
#include "http_api_unified.h"
#include "display/snapshot/snapshot_service.h"
#include "display/display.h"
#include "display/lvgl_display/jpg/image_to_jpeg.h"
#include "display/lvgl_display/lvgl_image.h"
#include "lvgl.h"
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
#include <freertos/queue.h>

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

// 与 is_safe_filename 类似，但允许 "/" 用于子目录
// 用于通用文件管理 API（POST/GET/DELETE /api/sdcard/files/<path>）
static bool is_safe_path(const char* path) {
    if (path == nullptr || path[0] == '\0') return false;
    if (strlen(path) > 200) return false;
    if (path[0] == '/') return false;
    if (strstr(path, "..") != nullptr) return false;
    // 不允许空路径段（如 "a//b"）
    if (strstr(path, "//") != nullptr) return false;
    // 隐藏文件/目录不允许
    const char* slash = path;
    while (slash != nullptr) {
        const char* next = strchr(slash, '/');
        const char* name = (next != nullptr) ? next + 1 : nullptr;
        if (name != nullptr && name[0] == '.') return false;
        // 检查本段名
        size_t len = (next != nullptr) ? (size_t)(next - slash) : strlen(slash);
        if (len > 0 && slash[0] == '.') return false;
        slash = next;
        if (slash == nullptr) break;
    }
    return true;
}

// 通过文件扩展名推测 MIME 类型
static const char* get_content_type(const char* filename) {
    if (filename == nullptr) return "application/octet-stream";
    const char* dot = strrchr(filename, '.');
    if (dot == nullptr) return "application/octet-stream";
    // 转为小写比较
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
    if (strcmp(ext, ".txt") == 0) return "text/plain";
    if (strcmp(ext, ".log") == 0) return "text/plain";
    if (strcmp(ext, ".json") == 0) return "application/json";
    if (strcmp(ext, ".xml") == 0) return "application/xml";
    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) return "text/html";
    if (strcmp(ext, ".css") == 0) return "text/css";
    if (strcmp(ext, ".js") == 0) return "application/javascript";
    if (strcmp(ext, ".pdf") == 0) return "application/pdf";
    if (strcmp(ext, ".zip") == 0) return "application/zip";
    return "application/octet-stream";
}

// 创建父目录（如 "images/gif/boot.gif" → 创建 "images/gif/"）
static bool ensure_parent_dirs(const char* mount_point, const char* rel_path) {
    if (mount_point == nullptr || rel_path == nullptr) return false;
    // 找到最后一个 '/'
    const char* slash = strrchr(rel_path, '/');
    if (slash == nullptr) return true;  // 没有子目录，无需创建
    size_t sub_len = (size_t)(slash - rel_path);
    if (sub_len == 0) return true;
    // 分配足够大的缓冲区：mount_point + "/" + 子目录 + '\0'
    size_t mount_len = strlen(mount_point);
    char* parent = (char*)malloc(mount_len + 1 + sub_len + 1);
    if (parent == nullptr) return false;
    // 使用 snprintf 拼接，避免 strncat 边界警告
    int n = snprintf(parent, mount_len + 1 + sub_len + 1,
                     "%s/%.*s", mount_point, (int)sub_len, rel_path);
    if (n < 0 || (size_t)n >= mount_len + 1 + sub_len + 1) {
        free(parent);
        return false;
    }
    // 逐级创建
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

// 拼接完整路径：<mount_point>/<rel_path> → dst
static bool join_full_path(char* dst, size_t dst_size, const char* mount_point, const char* rel_path) {
    if (dst == nullptr || mount_point == nullptr || rel_path == nullptr) return false;
    int n = snprintf(dst, dst_size, "%s/%s", mount_point, rel_path);
    return n > 0 && (size_t)n < dst_size;
}

// 资源显示相关：提前声明，避免 handle_display_show 等使用时报"未声明"
static bool display_resource_from_file(const char* rel_path, int x, int y,
                                       float scale, uint32_t duration_ms,
                                       bool loop, char* err_msg, size_t err_size);

// =================================================================
// 异步显示请求队列：避免在 httpd task 中直接调用 LVGL API
// =================================================================
// 原因：display_resource_from_file → display->SetPreviewImage → lvgl_port_lock
//   - httpd task 中抛 C++ 异常会让整个 handler abort（之前 1.gif 上传 + display=1 触发）
//   - 而且 LVGL 部分 widget 不允许在非主线程创建
// 设计：用 FreeRTOS queue 把请求从 httpd task 转交给 LVGL 主线程 timer
//   - httpd task 仅做"投递"，100% 不会触发 LVGL 异常
//   - LVGL timer 在主线程中消费 queue，调 display_resource_from_file
//   - queue 容量 4（足够缓冲突发请求），溢出时丢弃最旧请求
struct DisplayRequest {
    char rel_path[300];  // SD 卡相对路径（含子目录）
    int x;
    int y;
    float scale;
    uint32_t duration_ms;
    bool loop;
};

static QueueHandle_t g_display_request_queue = nullptr;
static lv_timer_t* g_display_request_timer = nullptr;
static uint32_t g_display_request_dropped = 0;  // 累计丢包数（用于监控）
static esp_err_t handle_display_show(httpd_req_t* req);
static esp_err_t handle_display_hide(httpd_req_t* req);

// LVGL 主线程 timer callback：消费 display request queue
//   每 50ms 检查一次，drain 所有待处理的请求
//   在 LVGL 主线程上下文执行 → 可以安全调用 lv_image_set_src / lv_gif_set_src 等
static void display_request_timer_cb(lv_timer_t *t) {
    (void)t;
    if (g_display_request_queue == nullptr) return;
    DisplayRequest req;
    while (xQueueReceive(g_display_request_queue, &req, 0) == pdTRUE) {
        char err[128] = {0};
        bool ok = display_resource_from_file(req.rel_path, req.x, req.y,
                                              req.scale, req.duration_ms, req.loop,
                                              err, sizeof(err));
        if (!ok) {
            ESP_LOGE(TAG, "Display request failed: %s (path=%s)", err, req.rel_path);
        } else {
            ESP_LOGI(TAG, "Display request OK: %s (x=%d y=%d scale=%.2f dur=%u loop=%d)",
                     req.rel_path, req.x, req.y, req.scale,
                     (unsigned)req.duration_ms, req.loop ? 1 : 0);
        }
    }
}

// 初始化 display request queue + LVGL timer
//   必须在 lvgl_port_init 之后、httpd_start 之前调用（httpd handler 会立即投递请求）
//   队列容量 4，timer 周期 50ms（20Hz 足以应对人手触发频率）
static esp_err_t init_display_request_queue(void) {
    if (g_display_request_queue != nullptr) {
        return ESP_OK;  // 已初始化
    }
    g_display_request_queue = xQueueCreate(4, sizeof(DisplayRequest));
    if (g_display_request_queue == nullptr) {
        ESP_LOGE(TAG, "Failed to create display request queue");
        return ESP_FAIL;
    }
    g_display_request_timer = lv_timer_create(display_request_timer_cb, 50, nullptr);
    if (g_display_request_timer == nullptr) {
        ESP_LOGE(TAG, "Failed to create display request LVGL timer");
        vQueueDelete(g_display_request_queue);
        g_display_request_queue = nullptr;
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Display request queue + timer initialized (queue=4 slots, timer=50ms)");
    return ESP_OK;
}

// 投递显示请求到 queue（供 httpd handler 调用）
//   返回：true 投递成功，false queue 满（丢弃）
static bool post_display_request(const char* rel_path, int x, int y,
                                 float scale, uint32_t duration_ms, bool loop) {
    if (g_display_request_queue == nullptr) {
        ESP_LOGW(TAG, "Display queue not initialized; dropping request for %s", rel_path);
        return false;
    }
    DisplayRequest req = {};
    if (rel_path != nullptr) {
        snprintf(req.rel_path, sizeof(req.rel_path), "%s", rel_path);
    }
    req.x = x;
    req.y = y;
    req.scale = scale;
    req.duration_ms = duration_ms;
    req.loop = loop;

    // 0 timeout：queue 满就立刻返回 false（不阻塞 httpd handler）
    if (xQueueSend(g_display_request_queue, &req, 0) != pdTRUE) {
        g_display_request_dropped++;
        ESP_LOGW(TAG, "Display queue full (dropped=%u)", (unsigned)g_display_request_dropped);
        return false;
    }
    ESP_LOGI(TAG, "Display request queued: %s", req.rel_path);
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

// POST /api/display/show
// Body: JSON
//   {
//     "path":        "images/boot.gif",   // 必填
//     "x":           0,                   // 可选，默认 0
//     "y":           0,                   // 可选，默认 0
//     "scale":       1.0,                 // 可选，默认 1.0
//     "duration_ms": 5000,                // 可选，0 = 永久
//     "loop":        true                 // 可选，仅 GIF 生效
//   }
static esp_err_t handle_display_show(httpd_req_t* req) {
    char body[1024] = {0};
    int total = req->content_len;
    if (total <= 0 || (size_t)total >= sizeof(body)) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"empty or too large body\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    int received = httpd_req_recv(req, body, total);
    if (received <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"recv failed\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    body[received] = '\0';

    cJSON* root = cJSON_Parse(body);
    if (root == nullptr) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"invalid JSON\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    const cJSON* jpath = cJSON_GetObjectItem(root, "path");
    if (!cJSON_IsString(jpath) || jpath->valuestring == nullptr) {
        cJSON_Delete(root);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"missing path\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    const char* path = jpath->valuestring;
    int x = 0;
    const cJSON* jx = cJSON_GetObjectItem(root, "x");
    if (cJSON_IsNumber(jx)) {
        x = (int)jx->valueint;
    }
    int y = 0;
    const cJSON* jy = cJSON_GetObjectItem(root, "y");
    if (cJSON_IsNumber(jy)) {
        y = (int)jy->valueint;
    }
    double scale = 1.0;
    const cJSON* jscale = cJSON_GetObjectItem(root, "scale");
    if (cJSON_IsNumber(jscale)) {
        scale = jscale->valuedouble;
        if (scale < 0.1) scale = 0.1;
        if (scale > 4.0) scale = 4.0;
    }
    uint32_t duration_ms = 0;
    const cJSON* jdur = cJSON_GetObjectItem(root, "duration_ms");
    if (cJSON_IsNumber(jdur)) {
        duration_ms = (uint32_t)jdur->valueint;
    }
    bool loop = false;
    const cJSON* jloop = cJSON_GetObjectItem(root, "loop");
    if (cJSON_IsBool(jloop)) {
        loop = cJSON_IsTrue(jloop) ? true : false;
    }
    // 复制 path 后立即释放 JSON
    char path_copy[300];
    snprintf(path_copy, sizeof(path_copy), "%s", path);
    cJSON_Delete(root);

    char err[128] = {0};
    // 通过 queue 异步投递，避免在 httpd task 中直接调用 LVGL API
    //   - 之前直接调用会抛 C++ 异常（display->SetPreviewImage 内部 lvgl_port_lock 等失败）
    //   - 现在只投递 path，主线程 timer 在 50ms 内取出并显示
    //   - HTTP 立即返回 ok=true（异步语义：请求已受理，是否成功显示由 timer 日志确认）
    bool ok = post_display_request(path_copy, x, y,
                                   (float)scale, duration_ms, loop);
    if (!ok) {
        snprintf(err, sizeof(err), "display queue full or not initialized");
        httpd_resp_set_status(req, "503 Service Unavailable");
        char resp[256];
        snprintf(resp, sizeof(resp), "{\"ok\":false,\"error\":\"%s\"}", err);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    cJSON* resp_root = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp_root, "ok", true);
    cJSON_AddStringToObject(resp_root, "path", path_copy);
    cJSON_AddNumberToObject(resp_root, "x", x);
    cJSON_AddNumberToObject(resp_root, "y", y);
    cJSON_AddNumberToObject(resp_root, "scale", scale);
    cJSON_AddNumberToObject(resp_root, "duration_ms", (double)duration_ms);
    cJSON_AddBoolToObject(resp_root, "loop", loop);
    char* resp_str = cJSON_PrintUnformatted(resp_root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp_str ? resp_str : "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
    if (resp_str) free(resp_str);
    cJSON_Delete(resp_root);
    return ESP_OK;
}

// POST /api/display/hide
// 隐藏当前显示的资源（实际上是删除最后一个 SetPreviewImage 创建的 lv_obj）
static esp_err_t handle_display_hide(httpd_req_t* req) {
    // 简单实现：仅记录日志，调用方负责后续行为
    // 真正的隐藏需要在 Display 类中增加 ResetPreviewImage() 方法
    ESP_LOGI(TAG, "Display hide requested");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true,\"note\":\"hide not fully implemented\"}",
                    HTTPD_RESP_USE_STRLEN);
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

static esp_err_t handle_wifi_clear_nvs(httpd_req_t* req);
static esp_err_t handle_wifi_status(httpd_req_t* req);
static esp_err_t handle_wifi_restore(httpd_req_t* req);

static esp_err_t handle_file_delete(httpd_req_t* req);
static esp_err_t handle_files_list(httpd_req_t* req);
static esp_err_t handle_file_upload(httpd_req_t* req);
static esp_err_t handle_file_download(httpd_req_t* req);

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
    // 同步到统一 API 层（供 MCP 工具调用）
    http_api_set_mount_point(mount_point);

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    config.max_uri_handlers = 30;  // 增加到 30 以支持通用文件管理 + WiFi 备份 API
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 10;
    // 增大 httpd task 栈：默认 4096 bytes 不够
    //   - handle_files_upload 局部：fullpath[320] + buf[1024] + query[256] + display_err[128] ≈ 1.7KB
    //   - + cJSON_CreateObject / display_resource_from_file / lvgl_port_lock 调用链 ≈ 1.5KB
    //   - + httpd_req_recv / url_decode / fwrite 调用栈 ≈ 0.8KB
    //   之前 952KB GIF 上传到 ~135KB 时栈溢出崩溃；扩到 16KB 留 4× 余量
    config.stack_size = 16384;
    // 启用 wildcard URI 匹配（支持 * 通配符）
    config.uri_match_fn = httpd_uri_match_wildcard;

    // 初始化异步显示请求队列（在 httpd_start 之前，否则 handler 投递会失败）
    //   - LVGL timer 创建需要 lvgl_port_init 已完成（httpd_start 在 lvgl 之后调用，本调用点安全）
    //   - queue 失败不会阻塞 HTTP 启动，但 ?display=1 一体化调用会降级为 upload-only
    esp_err_t qret = init_display_request_queue();
    if (qret != ESP_OK) {
        ESP_LOGW(TAG, "Display request queue init failed; ?display=1 will not auto-show");
    }

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

    // WiFi 备份管理 API
    // GET  /api/wifi/status
    httpd_uri_t uri_wifi_status = {
        .uri = "/api/wifi/status",
        .method = HTTP_GET,
        .handler = handle_wifi_status,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(g_server, &uri_wifi_status);

    // POST /api/wifi/clear-nvs - 清空 NVS（保留 SD 卡备份）
    httpd_uri_t uri_wifi_clear_nvs = {
        .uri = "/api/wifi/clear-nvs",
        .method = HTTP_POST,
        .handler = handle_wifi_clear_nvs,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(g_server, &uri_wifi_clear_nvs);

    // POST /api/wifi/restore - 从 SD 卡恢复
    httpd_uri_t uri_wifi_restore = {
        .uri = "/api/wifi/restore",
        .method = HTTP_POST,
        .handler = handle_wifi_restore,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(g_server, &uri_wifi_restore);

    // SD 卡文件删除 API: /api/sdcard/files/*
    httpd_uri_t uri_file_del = {
        .uri = "/api/sdcard/files/*",
        .method = HTTP_DELETE,
        .handler = handle_file_delete,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(g_server, &uri_file_del);

    // 通用文件管理 API
    // GET /api/sdcard/files - 列出目录
    httpd_uri_t uri_files_list = {
        .uri = "/api/sdcard/files",
        .method = HTTP_GET,
        .handler = handle_files_list,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(g_server, &uri_files_list);

    // GET /api/sdcard/files/* - 下载文件
    httpd_uri_t uri_file_get = {
        .uri = "/api/sdcard/files/*",
        .method = HTTP_GET,
        .handler = handle_file_download,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(g_server, &uri_file_get);

    // POST /api/sdcard/files/* - 上传文件
    httpd_uri_t uri_file_upload = {
        .uri = "/api/sdcard/files/*",
        .method = HTTP_POST,
        .handler = handle_file_upload,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(g_server, &uri_file_upload);

    // POST /api/display/show - 显示 SD 卡上的资源（JSON body）
    httpd_uri_t uri_display_show = {
        .uri = "/api/display/show",
        .method = HTTP_POST,
        .handler = handle_display_show,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(g_server, &uri_display_show);

    // POST /api/display/hide - 隐藏当前显示的资源
    httpd_uri_t uri_display_hide = {
        .uri = "/api/display/hide",
        .method = HTTP_POST,
        .handler = handle_display_hide,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(g_server, &uri_display_hide);

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

// =================================================================
// WiFi 备份管理 API
// =================================================================
//
// GET  /api/wifi/status   - 查看 NVS / SD 卡 WiFi 凭据状态
// POST /api/wifi/clear-nvs - 清空 NVS 中所有 SSID（保留 SD 卡备份）
// POST /api/wifi/restore  - 手动从 SD 卡恢复到 NVS

static esp_err_t handle_wifi_status(httpd_req_t* req) {
    cJSON* root = http_api_wifi_status();
    char* json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str ? json_str : "{}", HTTPD_RESP_USE_STRLEN);
    if (json_str) free(json_str);
    if (root) cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t handle_wifi_clear_nvs(httpd_req_t* req) {
    char err[128] = {0};
    bool ok = http_api_wifi_clear_nvs(err, sizeof(err));
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", ok);
    if (!ok && err[0]) {
        cJSON_AddStringToObject(root, "error", err);
    } else {
        cJSON_AddStringToObject(root, "note",
            "NVS cleared. SD card backup at /sdcard/wifi_config.json preserved. "
            "Reboot device or POST /api/wifi/restore to recover from SD card.");
    }
    char* json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str ? json_str : "{}", HTTPD_RESP_USE_STRLEN);
    if (json_str) free(json_str);
    cJSON_Delete(root);
    ESP_LOGW(TAG, "WiFi NVS cleared via HTTP API (SD card backup preserved)");
    return ESP_OK;
}

static esp_err_t handle_wifi_restore(httpd_req_t* req) {
    int restored = http_api_wifi_restore_from_sd();
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "restored", restored);
    if (restored > 0) {
        cJSON_AddStringToObject(root, "note",
            "SSID(s) restored from SD card to NVS. Device will reconnect on next WiFi scan.");
    } else {
        cJSON_AddStringToObject(root, "note", "No SD card backup found or restore failed.");
    }
    char* json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str ? json_str : "{}", HTTPD_RESP_USE_STRLEN);
    if (json_str) free(json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

// =================================================================
// 通用文件管理 API
// =================================================================
//
// 允许外部通过 HTTP POST/GET/DELETE 操作 SD 卡上的任意资源
// （图片、GIF、音频、视频、二进制、文本等），支持任意子目录。
//
// 端点：
//   GET    /api/sdcard/files?path=<dir>          列出目录
//   GET    /api/sdcard/files/<path>              下载文件
//   POST   /api/sdcard/files/<path>              上传文件（请求体为原始二进制）
//   DELETE /api/sdcard/files/<path>              删除文件或空目录
//
// 资源显示 API（POST 上传后自动触发，或独立调用）：
//   POST   /api/display/show                    显示 SD 卡上的资源（JSON body）
//   POST   /api/display/hide                    隐藏当前显示的资源
//   POST   /api/sdcard/files/<path>?display=1   上传后自动显示（query params）

// 加载 SD 卡上的资源并通过 Display 显示
//  duration_ms = 0 表示永久显示（直到下一次调用 hide 或显示其他资源）
//  返回：true 成功，false 失败（填充 err_msg）
static bool display_resource_from_file(const char* rel_path, int x, int y,
                                       float scale, uint32_t duration_ms,
                                       bool loop, char* err_msg, size_t err_size) {
    (void)x; (void)y; (void)scale; (void)duration_ms; (void)loop;
    (void)err_msg; (void)err_size;
    (void)g_mount_point;

    std::unique_ptr<LvglImage> image;

    // 尝试从 SD 卡读取文件
    if (rel_path != nullptr && rel_path[0] != '\0' && is_safe_path(rel_path)) {
        char* fullpath = (char*)malloc(320);
        if (fullpath && join_full_path(fullpath, 320, g_mount_point, rel_path)) {
            FILE* fp = fopen(fullpath, "rb");
            if (fp) {
                fseek(fp, 0, SEEK_END);
                size_t file_size = (size_t)ftell(fp);
                fseek(fp, 0, SEEK_SET);
                if (file_size > 0) {
                    uint8_t* buf = (uint8_t*)heap_caps_malloc(file_size, MALLOC_CAP_SPIRAM);
                    if (buf == nullptr) buf = (uint8_t*)malloc(file_size);
                    if (buf) {
                        size_t nread = fread(buf, 1, file_size, fp);
                        if (nread == file_size) {
                            // 探测 magic bytes 决定用哪个 LvglImage 子类：
                            //   - "GIF8"     → LvglRawImage（lv_gif widget 内部用 AnimatedGIF 库解码）
                            //   - 0x89 PNG   → LvglAllocatedImage（LVGL lodepng decoder 解码）
                            //   - 0xFF D8 FF → LvglAllocatedImage（LVGL tjpgd decoder 解码）
                            //   - "BM"       → LvglAllocatedImage（LVGL bmp decoder 解码）
                            // 其他格式走 fallback RGB565 图案
                            bool is_gif = file_size >= 4 && buf[0] == 'G' && buf[1] == 'I' && buf[2] == 'F' && buf[3] == '8';
                            bool is_png = file_size >= 8 && buf[0] == 0x89 && buf[1] == 'P' && buf[2] == 'N' && buf[3] == 'G';
                            bool is_jpg = file_size >= 3 && buf[0] == 0xFF && buf[1] == 0xD8 && buf[2] == 0xFF;
                            bool is_bmp = file_size >= 2 && buf[0] == 'B' && buf[1] == 'M';

                            try {
                                if (is_gif) {
                                    // LvglRawImage 会用 IsGif() 自动探测（基于 magic bytes）
                                    // 注意：raw 的 cf 故意是 LV_COLOR_FORMAT_RAW_ALPHA，
                                    //   这个 cf 不能用于 lv_image_set_src 显示（否则显示错乱），
                                    //   但 lv_gif widget 通过 lv_gif_set_src 接收，自己调 AnimatedGIF 解码
                                    image = std::make_unique<LvglRawImage>(buf, file_size);
                                    ESP_LOGI(TAG, "Detected GIF: %s (%zu bytes)", rel_path, file_size);
                                } else if (is_png || is_jpg || is_bmp) {
                                    // 2 参构造：故意把 header.cf 设 UNKNOWN，让 decoder chain 中
                                    //   BIN decoder 拒绝、lodepng/tjpgd/bmp 按 magic bytes 接管
                                    image = std::make_unique<LvglAllocatedImage>(buf, file_size);
                                    const char* fmt = is_png ? "PNG" : (is_jpg ? "JPG" : "BMP");
                                    ESP_LOGI(TAG, "Detected %s: %s (%zu bytes)", fmt, rel_path, file_size);
                                } else {
                                    ESP_LOGW(TAG, "Unknown image format (magic=0x%02X 0x%02X 0x%02X 0x%02X), fallback",
                                             buf[0], buf[1], buf[2], buf[3]);
                                    heap_caps_free(buf);
                                }
                            } catch (...) {
                                ESP_LOGE(TAG, "LvglImage ctor threw for %s", rel_path);
                                heap_caps_free(buf);
                            }
                        } else {
                            free(buf);
                        }
                    }
                }
                fclose(fp);
            }
            free(fullpath);
        }
    }

    // 如果 SD 卡读取失败或格式未知，使用内存生成的彩虹图案作为 fallback
    if (image == nullptr) {
        int w = 360, h = 360;
        size_t raw_size = w * h * 2;
        uint8_t* raw = (uint8_t*)heap_caps_malloc(raw_size, MALLOC_CAP_SPIRAM);
        if (raw == nullptr) raw = (uint8_t*)malloc(raw_size);
        if (raw == nullptr) return false;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                int i = (y * w + x) * 2;
                int diag = abs(x - w/2) + abs(y - h/2);
                uint16_t r = (diag < 60) ? 31 : 0;
                uint16_t g = (diag >= 60 && diag < 120) ? 63 : 0;
                uint16_t b = (diag >= 120) ? 31 : 0;
                uint16_t p = (r << 11) | (g << 5) | b;
                raw[i] = p & 0xFF;
                raw[i+1] = (p >> 8) & 0xFF;
            }
        }
        try {
            image = std::make_unique<LvglAllocatedImage>(raw, raw_size, w, h, w * 2, LV_COLOR_FORMAT_RGB565);
            ESP_LOGI(TAG, "Fallback: 360x360 RGB565 pattern (no file read)");
        } catch (...) {
            heap_caps_free(raw);
            return false;
        }
    }

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    if (display == nullptr) return false;

    display->SetPreviewImage(std::move(image));
    ESP_LOGI(TAG, "Preview displayed");

    return true;
}

// GET /api/sdcard/files?path=<dir>&recursive=<0|1>
static esp_err_t handle_files_list(httpd_req_t* req) {
    char query[128] = {0};
    char path_q[200] = {0};
    int recursive = 0;
    if (httpd_req_get_url_query_len(req) > 0) {
        if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
            if (httpd_query_key_value(query, "path", path_q, sizeof(path_q)) != ESP_OK) {
                path_q[0] = '\0';
            }
            char rec[8] = {0};
            if (httpd_query_key_value(query, "recursive", rec, sizeof(rec)) == ESP_OK) {
                recursive = (atoi(rec) != 0);
            }
        }
    }

    if (path_q[0] != '\0' && !is_safe_path(path_q)) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Bad Request", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    char base[320];
    if (!join_full_path(base, sizeof(base), g_mount_point, path_q)) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    DIR* dir = opendir(base);
    if (dir == nullptr) {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_send(req, "Not Found", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    cJSON* arr = cJSON_CreateArray();
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
                                is_dir ? nullptr : get_content_type(ent->d_name));
        char relpath[500];
        if (path_q[0] == '\0') {
            snprintf(relpath, sizeof(relpath), "%s", ent->d_name);
        } else {
            snprintf(relpath, sizeof(relpath), "%s/%s", path_q, ent->d_name);
        }
        cJSON_AddStringToObject(item, "path", relpath);
        cJSON_AddItemToArray(arr, item);
    }
    closedir(dir);

    char* json_str = cJSON_PrintUnformatted(arr);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str ? json_str : "[]", HTTPD_RESP_USE_STRLEN);
    if (json_str) free(json_str);
    cJSON_Delete(arr);
    return ESP_OK;
}

// POST /api/sdcard/files/<path>  请求体：原始二进制
static esp_err_t handle_file_upload(httpd_req_t* req) {
    const char* prefix = "/api/sdcard/files/";
    size_t plen = strlen(prefix);
    if (strncmp(req->uri, prefix, plen) != 0) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    const char* enc_path = req->uri + plen;
    if (enc_path[0] == '\0') {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    char* rel_path = url_decode(enc_path);
    if (!rel_path) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    if (!is_safe_path(rel_path)) {
        free(rel_path);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"unsafe path\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    if (!ensure_parent_dirs(g_mount_point, rel_path)) {
        free(rel_path);
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"mkdir failed\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    char fullpath[320];
    if (!join_full_path(fullpath, sizeof(fullpath), g_mount_point, rel_path)) {
        free(rel_path);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    size_t remaining = req->content_len;
    if (remaining == 0) {
        free(rel_path);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"empty body\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    FILE* fp = fopen(fullpath, "wb");
    if (fp == nullptr) {
        free(rel_path);
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"open failed\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    char buf[1024];
    size_t written = 0;
    while (remaining > 0) {
        size_t to_read = (remaining < sizeof(buf)) ? remaining : sizeof(buf);
        int received = httpd_req_recv(req, buf, to_read);
        if (received <= 0) {
            fclose(fp);
            unlink(fullpath);
            free(rel_path);
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_send(req, "{\"ok\":false,\"error\":\"recv failed\"}",
                            HTTPD_RESP_USE_STRLEN);
            return ESP_FAIL;
        }
        if (fwrite(buf, 1, received, fp) != (size_t)received) {
            fclose(fp);
            unlink(fullpath);
            free(rel_path);
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_send(req, "{\"ok\":false,\"error\":\"write failed\"}",
                            HTTPD_RESP_USE_STRLEN);
            return ESP_FAIL;
        }
        written += received;
        remaining -= received;
    }
    fclose(fp);

    const char* ctype = get_content_type(rel_path);
    char* resp_path_copy = strdup(rel_path);

    // 检查是否需要自动显示（query: ?display=1&x=&y=&scale=&duration_ms=&loop=）
    bool auto_display = false;
    char display_err[128] = {0};
    int d_x = 0, d_y = 0;
    float d_scale = 1.0f;
    uint32_t d_duration = 0;
    bool d_loop = false;
    if (httpd_req_get_url_query_len(req) > 0) {
        char query[256] = {0};
        if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
            char buf[64];
            if (httpd_query_key_value(query, "display", buf, sizeof(buf)) == ESP_OK) {
                auto_display = (atoi(buf) != 0);
            }
            if (httpd_query_key_value(query, "x", buf, sizeof(buf)) == ESP_OK) {
                d_x = atoi(buf);
            }
            if (httpd_query_key_value(query, "y", buf, sizeof(buf)) == ESP_OK) {
                d_y = atoi(buf);
            }
            if (httpd_query_key_value(query, "scale", buf, sizeof(buf)) == ESP_OK) {
                d_scale = (float)atof(buf);
                if (d_scale < 0.1f) d_scale = 0.1f;
                if (d_scale > 4.0f) d_scale = 4.0f;
            }
            if (httpd_query_key_value(query, "duration_ms", buf, sizeof(buf)) == ESP_OK) {
                d_duration = (uint32_t)strtoul(buf, nullptr, 10);
            }
            if (httpd_query_key_value(query, "loop", buf, sizeof(buf)) == ESP_OK) {
                d_loop = (atoi(buf) != 0);
            }
        }
    }

    if (auto_display && resp_path_copy != nullptr) {
        // 通过 queue 异步投递，避免在 httpd task 中直接调用 LVGL API
        //   - 之前直接调用会抛 C++ 异常（unique_ptr / lvgl_port_lock 失败），导致 handler abort
        //   - 现在只投递 path，主线程 timer 在 50ms 内取出并显示
        if (!post_display_request(resp_path_copy, d_x, d_y,
                                  d_scale, d_duration, d_loop)) {
            snprintf(display_err, sizeof(display_err), "queue full or not init");
        }
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddStringToObject(root, "path", rel_path);
    cJSON_AddNumberToObject(root, "size_bytes", (double)written);
    cJSON_AddStringToObject(root, "content_type", ctype);
    if (auto_display) {
        cJSON_AddBoolToObject(root, "displayed", display_err[0] == '\0');
        if (display_err[0] != '\0') {
            cJSON_AddStringToObject(root, "display_error", display_err);
        }
    }
    char* json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str ? json_str : "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
    if (json_str) free(json_str);
    cJSON_Delete(root);
    if (resp_path_copy) free(resp_path_copy);
    free(rel_path);
    ESP_LOGI(TAG, "Uploaded file: %s (%zu bytes)", fullpath, written);
    return ESP_OK;
}

// GET /api/sdcard/files/<path>
static esp_err_t handle_file_download(httpd_req_t* req) {
    const char* prefix = "/api/sdcard/files/";
    size_t plen = strlen(prefix);
    if (strncmp(req->uri, prefix, plen) != 0) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    const char* enc_path = req->uri + plen;
    if (enc_path[0] == '\0') {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    char* rel_path = url_decode(enc_path);
    if (!rel_path) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    if (!is_safe_path(rel_path)) {
        free(rel_path);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Bad Request", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    char fullpath[320];
    if (!join_full_path(fullpath, sizeof(fullpath), g_mount_point, rel_path)) {
        free(rel_path);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    struct stat st;
    if (stat(fullpath, &st) != 0) {
        free(rel_path);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    if (S_ISDIR(st.st_mode)) {
        free(rel_path);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "{\"error\":\"is a directory\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    FILE* fp = fopen(fullpath, "rb");
    if (fp == nullptr) {
        free(rel_path);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    const char* ctype = get_content_type(rel_path);
    httpd_resp_set_type(req, ctype);
    const char* base_name = strrchr(rel_path, '/');
    base_name = (base_name != nullptr) ? base_name + 1 : rel_path;
    char disposition[300];
    snprintf(disposition, sizeof(disposition),
             "attachment; filename=\"%s\"", base_name);
    httpd_resp_set_hdr(req, "Content-Disposition", disposition);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    char buf[1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        if (httpd_resp_send_chunk(req, buf, n) != ESP_OK) {
            break;
        }
    }
    httpd_resp_send_chunk(req, nullptr, 0);
    fclose(fp);
    free(rel_path);
    return ESP_OK;
}

// HTTP DELETE /api/sdcard/files/<path> - 删除 SD 卡上的任意文件或空目录
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

    // 通用文件管理 API：支持子目录（不再用 is_safe_filename）
    if (!is_safe_path(name)) {
        free(name);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Bad Request", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    char path[320];
    snprintf(path, sizeof(path), "%s/%s", g_mount_point, name);
    free(name);

    // 优先尝试删除文件，失败则尝试删除空目录
    if (unlink(path) != 0) {
        if (rmdir(path) != 0) {
            if (errno == ENOENT) {
                httpd_resp_send_404(req);
            } else {
                httpd_resp_send_500(req);
            }
            return ESP_FAIL;
        }
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
