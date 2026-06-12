#include "snapshot_service.h"
#include "display.h"
#include "board.h"
#include "jpg/image_to_jpeg.h"
#include <esp_log.h>
#include <cstdio>
#include <cstring>

#define TAG "SnapshotService"

// Base64编码表
static const char base64_chars[] = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

SnapshotService& SnapshotService::GetInstance() {
    static SnapshotService instance;
    return instance;
}

SnapshotService::SnapshotService() {
    // 构造函数 - 无需额外初始化
}

SnapshotService::~SnapshotService() {
    Stop();
}

esp_err_t SnapshotService::Initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (initialized_) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }
    
    // 使用 printf 进行输出（通过 console UART）
    // 不直接打开 UART，因为 console 已经在使用它
    
    initialized_ = true;
    ESP_LOGI(TAG, "Snapshot service initialized (console-based)");
    return ESP_OK;
}

esp_err_t SnapshotService::Start() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (running_) {
        ESP_LOGW(TAG, "Already running");
        return ESP_OK;
    }
    
    if (!initialized_) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 不需要创建任务，使用同步方式通过 printf 输出
    running_ = true;
    ESP_LOGI(TAG, "Snapshot service started (synchronous mode)");
    return ESP_OK;
}

esp_err_t SnapshotService::Stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!running_) {
        return ESP_OK;
    }
    
    running_ = false;
    initialized_ = false;
    
    ESP_LOGI(TAG, "Snapshot service stopped");
    return ESP_OK;
}

esp_err_t SnapshotService::TakeSnapshot() {
    if (!running_) {
        ESP_LOGE(TAG, "Service not running");
        return ESP_ERR_INVALID_STATE;
    }
    
    return ExecuteSnapshot();
}

void SnapshotService::UARTTask(void* arg) {
    // 空实现 - 使用同步模式
    vTaskDelete(NULL);
}

esp_err_t SnapshotService::ExecuteSnapshot() {
    ESP_LOGI(TAG, "Executing snapshot...");
    
    uint8_t* jpeg_data = nullptr;
    size_t jpeg_len = 0;
    
    // 捕获并编码
    if (!CaptureAndEncode(&jpeg_data, &jpeg_len)) {
        ESP_LOGE(TAG, "Failed to capture and encode");
        SendACK(SNAPSHOT_ERR_NO_MEM);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "JPEG encoded, size: %d bytes", (int)jpeg_len);
    
    // 发送数据
    esp_err_t ret = SendData(jpeg_data, jpeg_len);
    
    // 释放内存
    free(jpeg_data);
    
    return ret;
}

bool SnapshotService::CaptureAndEncode(uint8_t** jpeg_data, size_t* jpeg_len) {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    
    if (!display) {
        ESP_LOGE(TAG, "Display not available");
        return false;
    }
    
    // 获取屏幕尺寸
    int width = display->width();
    int height = display->height();
    
    // 使用LVGL快照功能
    lv_draw_buf_t* draw_buf = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);
    if (!draw_buf) {
        ESP_LOGE(TAG, "Failed to take snapshot");
        return false;
    }
    
    // 获取快照数据指针（RGB565格式，每像素2字节）
    uint8_t* rgb_data = (uint8_t*)draw_buf->data;
    size_t rgb_size = width * height * 2;  // RGB565: 2 bytes per pixel
    
    // 转换为JPEG
    bool ret = image_to_jpeg(rgb_data, rgb_size, width, height, 
                            V4L2_PIX_FMT_RGB565, 80, jpeg_data, jpeg_len);
    
    // 释放快照缓冲区
    lv_draw_buf_destroy(draw_buf);
    
    if (!ret) {
        ESP_LOGE(TAG, "JPEG encoding failed");
        return false;
    }
    
    return true;
}

esp_err_t SnapshotService::SendData(const uint8_t* data, size_t len) {
    // 计算Base64编码后的长度
    size_t base64_len = Base64EncodeLength(len);
    uint8_t* base64_data = (uint8_t*)malloc(base64_len);
    if (!base64_data) {
        ESP_LOGE(TAG, "Failed to allocate Base64 buffer");
        return ESP_ERR_NO_MEM;
    }
    
    // 进行Base64编码
    Base64Encode(data, len, base64_data);
    
    ESP_LOGI(TAG, "JPEG size: %d bytes, Base64 size: %d bytes", (int)len, (int)base64_len);
    
    // 使用 printf 直接输出 Base64 数据，分块输出避免缓冲问题
    printf("\n===SCREENSHOT_START===\n");
    fflush(stdout);
    
    // 分块输出，每块 500 字符
    const size_t chunk_size = 500;
    size_t offset = 0;
    while (offset < base64_len) {
        size_t remaining = base64_len - offset;
        size_t to_write = (remaining < chunk_size) ? remaining : chunk_size;
        
        printf("%.*s", (int)to_write, (char*)base64_data + offset);
        offset += to_write;
        
        // 每块后刷新
        if (offset % chunk_size == 0 || offset >= base64_len) {
            fflush(stdout);
            vTaskDelay(pdMS_TO_TICKS(10));  // 短暂延迟
        }
    }
    
    printf("\n===SCREENSHOT_END===\n");
    fflush(stdout);
    ESP_LOGI(TAG, "Screenshot data sent");
    
    free(base64_data);
    
    ESP_LOGI(TAG, "Data send complete");
    return ESP_OK;
}

size_t SnapshotService::Base64Encode(const uint8_t* src, size_t src_len, uint8_t* dst) {
    size_t i = 0, j = 0;
    size_t len = Base64EncodeLength(src_len);
    
    while (i < src_len) {
        uint32_t octet_a = i < src_len ? src[i++] : 0;
        uint32_t octet_b = i < src_len ? src[i++] : 0;
        uint32_t octet_c = i < src_len ? src[i++] : 0;
        
        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;
        
        dst[j++] = base64_chars[(triple >> 18) & 0x3F];
        dst[j++] = base64_chars[(triple >> 12) & 0x3F];
        dst[j++] = base64_chars[(triple >> 6) & 0x3F];
        dst[j++] = base64_chars[triple & 0x3F];
    }
    
    // 添加填充
    if (src_len % 3 == 1) {
        dst[j - 2] = '=';
        dst[j - 1] = '=';
    } else if (src_len % 3 == 2) {
        dst[j - 1] = '=';
    }
    
    dst[j] = '\0';
    return len;
}

size_t SnapshotService::Base64EncodeLength(size_t src_len) {
    return ((src_len + 2) / 3) * 4 + 1;  // +1 for null terminator
}

void SnapshotService::SendACK(snapshot_error_t error) {
    // 简化为 printf 输出
    printf("[SNAPSHOT] ACK: error=%d\n", error);
    fflush(stdout);
}

void SnapshotService::SendPONG() {
    // 简化为 printf 输出
    printf("[SNAPSHOT] PONG\n");
    fflush(stdout);
}
