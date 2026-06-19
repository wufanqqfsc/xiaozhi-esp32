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

// 启动时自动截图功能（默认关闭；启用方法：在 menuconfig / idf.py build -DXIAOZHI_ENABLE_BOOT_SCREENSHOT=1 显式打开）
#ifndef XIAOZHI_ENABLE_BOOT_SCREENSHOT
#define XIAOZHI_ENABLE_BOOT_SCREENSHOT 0
#endif

#include "application.h"
#if XIAOZHI_ENABLE_BOOT_SCREENSHOT
#include <snapshot_protocol.h>
#include "snapshot_service.h"
#endif
#include "board.h"
#include "attitude_display.h"

#define TAG "main"

#if XIAOZHI_ENABLE_BOOT_SCREENSHOT
// 定期截图任务 - 每5秒自动截图
static void screenshot_task(void* arg) {
    auto& svc = SnapshotService::GetInstance();
    int count = 0;

    ESP_LOGI(TAG, "[screenshot_task] started on core %d", xPortGetCoreID());
    fflush(stdout);

    // 短暂等待屏幕初始化
    vTaskDelay(pdMS_TO_TICKS(3000));

    ESP_LOGI(TAG, "[screenshot_task] delay 3s done, will take screenshots every 5s");
    fflush(stdout);

    while (true) {
        ESP_LOGI(TAG, "[screenshot_task] Taking screenshot #%d...", count + 1);
        printf("\n[SCREENSHOT #%d]\n", count + 1);
        fflush(stdout);

        // 执行截图
        esp_err_t ret = svc.TakeSnapshot();

        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "[screenshot_task] Screenshot #%d completed", count + 1);
        } else {
            ESP_LOGE(TAG, "[screenshot_task] Screenshot #%d failed: 0x%x", count + 1, ret);
        }

        count++;

        // 每2秒截图一次（加快以捕获快速变化的画面）
        ESP_LOGI(TAG, "[screenshot_task] Waiting 2s before next screenshot...");
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    vTaskDelete(NULL);
}
#endif

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

#if XIAOZHI_ENABLE_BOOT_SCREENSHOT
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

#if XIAOZHI_ENABLE_BOOT_SCREENSHOT
    BaseType_t task_ret = xTaskCreate(&screenshot_task, "screenshot_task", 8192, NULL, 3, NULL);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create screenshot task");
    } else {
        ESP_LOGI(TAG, "Screenshot task created successfully");
    }
#endif

    app.Run();  // This function runs the main event loop and never returns
}
