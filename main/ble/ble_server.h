#pragma once

#include <functional>
#include <esp_err.h>
#include "display/attitude_display.h"

/**
 * 迭代 3: 轻量 NimBLE 外设，驱动 BLE 鱼眼状态（广播 / 连接 / 关闭）。
 * 与 WiFi 语音业务共存；BluFi 配网前需 Stop()，配网结束后可 Start()。
 */
class BleServer {
public:
    using StatusCallback = std::function<void(BleStatus status)>;

    static BleServer& GetInstance();

    esp_err_t Start();
    esp_err_t Stop();

    void SetStatusCallback(StatusCallback callback);
    BleStatus GetStatus() const { return status_; }
    bool IsRunning() const { return running_; }

    /** Called from NimBLE GAP callbacks (not for external use). */
    void NotifyStatus(BleStatus status);

    BleServer(const BleServer&) = delete;
    BleServer& operator=(const BleServer&) = delete;

private:
    BleServer() = default;
    ~BleServer() = default;

    StatusCallback status_callback_;
    BleStatus status_ = BleStatus::DISABLED;
    bool running_ = false;
};
