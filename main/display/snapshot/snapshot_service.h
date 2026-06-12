#pragma once

#include "snapshot_protocol.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mutex>

class SnapshotService {
public:
    // 获取单例实例
    static SnapshotService& GetInstance();
    
    // 初始化服务
    esp_err_t Initialize();
    
    // 启动服务
    esp_err_t Start();
    
    // 停止服务
    esp_err_t Stop();
    
    // 手动触发截图
    esp_err_t TakeSnapshot();
    
    // 检查服务是否运行
    bool IsRunning() const { return running_; }
    
private:
    SnapshotService();
    ~SnapshotService();
    
    // 禁止拷贝和赋值
    SnapshotService(const SnapshotService&) = delete;
    SnapshotService& operator=(const SnapshotService&) = delete;
    
    // 任务函数
    static void UARTTask(void* arg);
    
    // 执行截图并发送
    esp_err_t ExecuteSnapshot();
    
    // LVGL截图并转换为JPEG
    bool CaptureAndEncode(uint8_t** jpeg_data, size_t* jpeg_len);
    
    // 发送数据（带Base64编码）
    esp_err_t SendData(const uint8_t* data, size_t len);
    
    // Base64编码
    size_t Base64Encode(const uint8_t* src, size_t src_len, uint8_t* dst);
    
    // 计算Base64编码后的长度
    size_t Base64EncodeLength(size_t src_len);
    
    // 发送ACK响应
    void SendACK(snapshot_error_t error);
    
    // 发送PONG响应
    void SendPONG();
    
    // 成员变量
    bool initialized_ = false;
    bool running_ = false;
    std::mutex mutex_;
};