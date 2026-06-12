#include <esp_log.h>
#include <esp_err.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <esp_event.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>
#include <snapshot_protocol.h>
#include <cstring>
#include <cstdio>

#include "application.h"
#include "snapshot_service.h"

#define TAG "main"

// 定期截图任务
static void screenshot_task(void* arg) {
    auto& svc = SnapshotService::GetInstance();
    int count = 0;
    const int max_screenshots = 3;
    
    ESP_LOGI(TAG, "Screenshot task started");
    fflush(stdout);
    
    // 短暂等待屏幕初始化
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    ESP_LOGI(TAG, "Taking first screenshot...");
    fflush(stdout);
    
    while (count < max_screenshots) {
        ESP_LOGI(TAG, "Taking scheduled screenshot #%d...", count + 1);
        printf("\n[SCREENSHOT #%d]\n", count + 1);
        fflush(stdout);
        
        // 执行截图
        esp_err_t ret = svc.TakeSnapshot();
        
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Screenshot #%d completed", count + 1);
        } else {
            ESP_LOGE(TAG, "Screenshot #%d failed: %d", count + 1, ret);
        }
        
        count++;
        
        // 等待下一个截图
        if (count < max_screenshots) {
            ESP_LOGI(TAG, "Waiting 2s before next screenshot...");
            fflush(stdout);
            vTaskDelay(pdMS_TO_TICKS(2000));  // 2秒后再次截图
        }
    }
    
    ESP_LOGI(TAG, "All scheduled screenshots completed");
    vTaskDelete(NULL);
}

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

    // Initialize snapshot service (for screen capture)
    auto& snapshot_service = SnapshotService::GetInstance();
    ret = snapshot_service.Initialize();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Snapshot service initialized");
        snapshot_service.Start();
    } else {
        ESP_LOGE(TAG, "Snapshot service initialization failed: %d", ret);
    }
    
    // Initialize and run the application
    auto& app = Application::GetInstance();
    app.Initialize();
    
    // 创建定期截图任务（在应用启动后执行）
    BaseType_t task_ret = xTaskCreate(&screenshot_task, "screenshot_task", 8192, NULL, 3, NULL);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create screenshot task");
    } else {
        ESP_LOGI(TAG, "Screenshot task created successfully");
    }
    
    app.Run();  // This function runs the main event loop and never returns
}
