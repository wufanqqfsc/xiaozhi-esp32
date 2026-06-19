#include "snapshot_service.h"
#include "display.h"
#include "board.h"
#include "jpg/image_to_jpeg.h"
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <driver/uart.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
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
    
    // 创建UART命令接收任务
    xTaskCreate(&SnapshotService::UARTTask, "snapshot_uart", 4096, NULL, 5, NULL);
    
    running_ = true;
    ESP_LOGI(TAG, "Snapshot service started (async mode with UART command receiver)");
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
    // 配置UART参数
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    
    // 使用console UART端口
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_0, &uart_config));
    
    // 设置TX引脚（不需要RX，因为console已经配置）
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_0, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    
    // 安装UART驱动（使用较大的接收缓冲区）
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 8192, 0, 0, NULL, 0));
    
    ESP_LOGI(TAG, "UART command receiver started");
    
    // 命令缓冲区
    uint8_t data[128];
    
    while (true) {
        // 读取UART数据
        int len = uart_read_bytes(UART_NUM_0, data, sizeof(data) - 1, 100 / portTICK_PERIOD_MS);
        
        if (len > 0) {
            data[len] = '\0';
            
            // 解析命令
            // 格式: CLICK:<index> (例如: CLICK:0 表示点击第一个按钮)
            if (strncmp((char*)data, "CLICK:", 6) == 0) {
                int index = atoi((char*)data + 6);
                ESP_LOGI(TAG, "[UART] Received CLICK command: index=%d", index);
                
                // 触发按钮点击
                SnapshotService::GetInstance().TriggerButtonClick(index);
                
                // 发送确认
                printf("[SNAPSHOT] CLICK_ACK: index=%d\n", index);
                fflush(stdout);
            }
            // 格式: SNAP (快捷命令，等同于SNAPSHOT_CMD_SNAPSHOT_REQ)
            else if (strncmp((char*)data, "SNAP", 4) == 0) {
                ESP_LOGI(TAG, "[UART] Received SNAP command");
                
                // 触发截图
                SnapshotService::GetInstance().TakeSnapshot();
                
                // 发送确认
                printf("[SNAPSHOT] SNAP_ACK\n");
                fflush(stdout);
            }
        }
        
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    
    // 永远不会执行到这里
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

    // 释放JPEG数据（image_to_jpeg 用 malloc 分配）
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

    // LVGL 快照和刷新必须在 LVGL 线程中执行（lvgl_port 互斥锁保护）
    lv_draw_buf_t* draw_buf = nullptr;

    if (lvgl_port_lock(2000)) {  // 等待最多2秒获取锁
        // 在LVGL线程中执行截图
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

    // 获取快照数据指针（RGB565格式，每像素2字节）
    uint8_t* src_data = (uint8_t*)draw_buf->data;
    size_t rgb_size = width * height * 2;  // RGB565: 2 bytes per pixel

    // 先复制数据，因为LVGL的draw_buf可能不能直接修改
    uint8_t* rgb_data = (uint8_t*)heap_caps_malloc(rgb_size, MALLOC_CAP_SPIRAM);
    if (!rgb_data) {
        ESP_LOGE(TAG, "Failed to allocate buffer for image processing");
        lv_draw_buf_destroy(draw_buf);
        return false;
    }
    memcpy(rgb_data, src_data, rgb_size);

    // 释放LVGL快照缓冲区（复制完后立即释放）
    lv_draw_buf_destroy(draw_buf);
    draw_buf = nullptr;

    // 转换为JPEG（不需要LVGL锁，可在当前线程执行）
    bool ret = image_to_jpeg(rgb_data, rgb_size, width, height,
                            V4L2_PIX_FMT_RGB565, 80, jpeg_data, jpeg_len);

    // 释放RGB数据（必须用 heap_caps_free 释放 PSRAM 内存）
    heap_caps_free(rgb_data);
    rgb_data = nullptr;

    if (!ret) {
        ESP_LOGE(TAG, "JPEG encoding failed");
        return false;
    }

    return true;
}

esp_err_t SnapshotService::SendData(const uint8_t* data, size_t len) {
    // 计算Base64编码后的长度
    size_t base64_len = Base64EncodeLength(len);
    // 使用 SPIRAM 分配大块内存（Base64 后约 17KB，普通 RAM 容易不足）
    uint8_t* base64_data = (uint8_t*)heap_caps_malloc(base64_len, MALLOC_CAP_SPIRAM);
    if (!base64_data) {
        // 回退到普通内存
        base64_data = (uint8_t*)malloc(base64_len);
        if (!base64_data) {
            ESP_LOGE(TAG, "Failed to allocate Base64 buffer");
            return ESP_ERR_NO_MEM;
        }
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
    while (offset < base64_len - 1) {  // -1 排除 null 终止符
        size_t remaining = base64_len - 1 - offset;
        size_t to_write = (remaining < chunk_size) ? remaining : chunk_size;

        printf("%.*s", (int)to_write, (char*)base64_data + offset);
        offset += to_write;

        // 每块后刷新
        if (offset % chunk_size == 0 || offset >= base64_len - 1) {
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

esp_err_t SnapshotService::TriggerButtonClick(int index) {
    ESP_LOGI(TAG, "[TriggerButtonClick] index=%d", index);

    // 功能区提示卡触发事件已全部移除：CLI CLICK 入口仅落日志，不弹任何卡片
    return ESP_OK;
}
