#include <esp_log.h>
#include <esp_err.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <esp_event.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>
#include <cstring>
#include <cstdio>

// 串口截图服务（默认关闭；可通过串口命令 SNAP 手动触发截图，截图保存到 SD 卡）
#ifndef XIAOZHI_ENABLE_SNAPSHOT_SERVICE
#define XIAOZHI_ENABLE_SNAPSHOT_SERVICE 0
#endif

#include "application.h"
#include "board.h"
#include "attitude_display.h"

#if XIAOZHI_ENABLE_SNAPSHOT_SERVICE
#include <snapshot_protocol.h>
#include "snapshot_service.h"
#endif

#define TAG "main"

extern "C" void app_main(void)
{
    // Initialize NVS flash for WiFi configuration
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS flash to fix corruption");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

#if XIAOZHI_ENABLE_SNAPSHOT_SERVICE
    // 初始化串口截图服务（可通过串口命令 SNAP 手动触发）
    auto& snapshot_service = SnapshotService::GetInstance();
    ret = snapshot_service.Initialize();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Snapshot service initialized");
        snapshot_service.Start();
    } else {
        ESP_LOGE(TAG, "Snapshot service initialization failed: %d", ret);
    }
#endif

    // Initialize and run the application
    auto& app = Application::GetInstance();
    app.Initialize();

    app.Run();  // This function runs the main event loop and never returns
}
