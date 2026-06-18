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

// 启动时自动截图功能
#ifndef XIAOZHI_ENABLE_BOOT_SCREENSHOT
#define XIAOZHI_ENABLE_BOOT_SCREENSHOT 1
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

// 自动触发 fortune 演示任务（仅用于截图验证）
#if XIAOZHI_ENABLE_BOOT_SCREENSHOT
static void fortune_demo_task(void* arg) {
    ESP_LOGI(TAG, "[fortune_demo_task] waiting 25s for UI to stabilize...");
    vTaskDelay(pdMS_TO_TICKS(25000));

    auto* display = Board::GetInstance().GetDisplay();
    if (display == nullptr) {
        ESP_LOGE(TAG, "[fortune_demo_task] display is null");
        vTaskDelete(NULL);
        return;
    }

    auto* attitude = dynamic_cast<AttitudeDisplay*>(display);
    if (attitude == nullptr) {
        ESP_LOGE(TAG, "[fortune_demo_task] display is not AttitudeDisplay");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "[fortune_demo_task] triggering fortune demo...");
    attitude->ShowFortune(
        "今日运势 ☀", "乾为天", "今日宜进取，顺势而行。",
        "宜：签约、出行", "忌：熬夜、口舌", 63, 0);
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

    // 启动自动 fortune 演示任务，方便截图验证布局
    BaseType_t fortune_ret = xTaskCreate(&fortune_demo_task, "fortune_demo_task", 4096, NULL, 4, NULL);
    if (fortune_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create fortune demo task");
    } else {
        ESP_LOGI(TAG, "Fortune demo task created successfully");
    }
#endif

    app.Run();  // This function runs the main event loop and never returns
}
