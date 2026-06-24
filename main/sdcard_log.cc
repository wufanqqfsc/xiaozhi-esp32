#include "sdcard_log.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <esp_log.h>
#include <esp_vfs.h>
#include <esp_timer.h>

#define TAG "SdCardLog"

namespace {

constexpr int kFlushIntervalMs = 1000;   // 1s 刷盘一次

char g_log_path[160] = {0};
SemaphoreHandle_t g_mutex = nullptr;
int g_fd = -1;
volatile bool g_active = false;

// ESP-IDF vprintf 重定向：每次 ESP_LOGx 都调用本函数一次。
// 极简实现：line 用 static + mutex 保护，避免 sys_evt 等低栈 task 栈溢出。
// 同时把同一行镜像到 UART (fd 1)，便于串口调试；write 是 syscall 不占用户栈
int VprintfRedirect(const char* fmt, va_list args) {
    if (g_fd < 0 || g_mutex == nullptr) {
        return 0;
    }
    // non-blocking：sys_evt 等低栈 task 不会因取不到锁而 hang
    if (xSemaphoreTakeRecursive(g_mutex, 0) != pdTRUE) {
        return 0;
    }
    // 静态 buffer（不在 sys_evt 4KB 栈上分配）
    static char line[512];
    uint32_t ms = (uint32_t)(esp_timer_get_time() / 1000);
    int prefix = snprintf(line, sizeof(line), "[+%lu.%03lu] ",
                          (unsigned long)(ms / 1000), (unsigned long)(ms % 1000));
    int body = vsnprintf(line + prefix, sizeof(line) - prefix, fmt, args);
    int total = (body < 0) ? prefix : prefix + body;
    if (total >= (int)sizeof(line)) {
        total = sizeof(line) - 1;
        line[total] = '\0';
    }
    (void)write(g_fd, line, total);  // 写 SD 卡
    // 镜像到 UART（fd 1 = stdout），让串口调试更直观
    // 用裸 write() syscall，不调 printf/vprintf，避开潜在递归
    (void)write(1, line, total);
    xSemaphoreGiveRecursive(g_mutex);
    return total;
}

// 周期刷盘任务
void FlushTimerCb(void* arg) {
    (void)arg;
    SdCardLogFlush();
}

bool EnsureDirectory(const char* mount_point) {
    // 挂载点已存在即可（esp_vfs_fat_sdmmc_mount 创建过）
    struct stat st;
    if (stat(mount_point, &st) == 0 && S_ISDIR(st.st_mode)) {
        return true;
    }
    ESP_LOGW(TAG, "mount_point %s not present, skip", mount_point);
    return false;
}

void MakeFileName(char* out, size_t out_size) {
    time_t now = time(nullptr);
    struct tm tm;
    bool time_ok = (now > 100000);  // 跳过 1970 epoch，避免未校准 RTC 写出 1970 时间
    if (time_ok) {
        localtime_r(&now, &tm);
    } else {
        memset(&tm, 0, sizeof(tm));
    }
    uint32_t ms = (uint32_t)(esp_timer_get_time() / 1000);
    // 默认走 LFN 长文件名（需要 sdkconfig 启用 CONFIG_FATFS_LFN_HEAP/STACK）
    // 若 LFN 未启用会返回 FR_INVALID_NAME=22，fallback 见 SdCardLogStart
    if (time_ok) {
        snprintf(out, out_size, "xiaozhi_%04d-%02d-%02d_%02d-%02d-%02d_%05lu.log",
                 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                 tm.tm_hour, tm.tm_min, tm.tm_sec,
                 (unsigned long)(ms % 100000));
    } else {
        // 没校准 RTC 时用 uptime 作后缀，仍是合法 LFN
        snprintf(out, out_size, "xiaozhi_boot_%lu.log", (unsigned long)(ms / 1000));
    }
}

void ListRecentLogs(const char* mount_point) {
    // 仅打印一次最近 3 个日志文件名，方便人工核查
    DIR* d = opendir(mount_point);
    if (d == nullptr) {
        return;
    }
    struct dirent* ent;
    int shown = 0;
    while ((ent = readdir(d)) != nullptr && shown < 3) {
        if (strstr(ent->d_name, "xiaozhi_") == ent->d_name &&
            strstr(ent->d_name, ".log") != nullptr) {
            ESP_LOGI(TAG, "  log history: /sdcard/%s", ent->d_name);
            shown++;
        }
    }
    closedir(d);
}

}  // namespace

extern "C" bool SdCardLogStart(const char* mount_point) {
    if (g_active) {
        return true;
    }
    if (mount_point == nullptr) {
        mount_point = "/sdcard";
    }
    if (!EnsureDirectory(mount_point)) {
        return false;
    }

    // 创建互斥量
    if (g_mutex == nullptr) {
        g_mutex = xSemaphoreCreateRecursiveMutex();
        if (g_mutex == nullptr) {
            ESP_LOGE(TAG, "create mutex failed");
            return false;
        }
    }

    char filename[64];
    MakeFileName(filename, sizeof(filename));
    snprintf(g_log_path, sizeof(g_log_path), "%s/%s", mount_point, filename);

    g_fd = open(g_log_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (g_fd < 0) {
        int open_errno = errno;
        ESP_LOGE(TAG, "open %s failed: errno=%d (%s)", g_log_path, open_errno, strerror(open_errno));
        // FR_INVALID_NAME=22 → FAT 拒绝 LFN，fallback 到 8.3 短名（每次启动会覆盖）
        if (open_errno == EINVAL) {
            ESP_LOGW(TAG, "fallback to 8.3 short name (LFN disabled?)");
            snprintf(g_log_path, sizeof(g_log_path), "%s/xzhi.log", mount_point);
            g_fd = open(g_log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (g_fd < 0) {
                int e2 = errno;
                ESP_LOGE(TAG, "open %s failed: errno=%d (%s)", g_log_path, e2, strerror(e2));
                g_log_path[0] = '\0';
                return false;
            }
            ESP_LOGW(TAG, "using short name %s (overwritten on every boot)", g_log_path);
        } else {
            g_log_path[0] = '\0';
            return false;
        }
    }

    // 写文件头：boot 时间戳
    uint32_t ms = (uint32_t)(esp_timer_get_time() / 1000);
    char header[200];
    // 直接 hardcode ESP-IDF 版本字符串（避免 IDF_VER 宏可能未定义的坑）
    int n = snprintf(header, sizeof(header),
                     "==== xiaozhi boot log, uptime=%lu.%03lus, sdk=v5.5.4 ====\n",
                     (unsigned long)(ms / 1000), (unsigned long)(ms % 1000));
    (void)write(g_fd, header, n);

    // 接管 ESP_LOG 输出
    esp_log_set_vprintf(&VprintfRedirect);
    g_active = true;
    ESP_LOGI(TAG, "SD card log redirect started, file=%s", g_log_path);

    // 启动 1s 周期刷盘定时器
    const esp_timer_create_args_t flush_timer_args = {
        .callback = &FlushTimerCb,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "sdlog_flush",
        .skip_unhandled_events = true,
    };
    esp_timer_handle_t flush_timer = nullptr;
    if (esp_timer_create(&flush_timer_args, &flush_timer) == ESP_OK && flush_timer != nullptr) {
        esp_timer_start_periodic(flush_timer, kFlushIntervalMs * 1000);
    }

    // 打印历史日志
    ListRecentLogs(mount_point);
    return true;
}

extern "C" void SdCardLogStop() {
    if (!g_active) {
        return;
    }
    SdCardLogFlush();
    esp_log_set_vprintf(nullptr);
    if (g_fd >= 0) {
        close(g_fd);
        g_fd = -1;
    }
    g_active = false;
    ESP_LOGI(TAG, "SD card log redirect stopped");
}

extern "C" bool SdCardLogIsActive() {
    return g_active;
}

extern "C" const char* SdCardLogGetPath() {
    return g_log_path;
}

extern "C" void SdCardLogFlush() {
    if (g_fd < 0) {
        return;
    }
    if (g_mutex != nullptr && xSemaphoreTakeRecursive(g_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        fsync(g_fd);
        xSemaphoreGiveRecursive(g_mutex);
    }
}
