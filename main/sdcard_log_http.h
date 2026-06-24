#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动 SD 卡日志 HTTP 服务
 *
 * @param mount_point  SD 卡挂载点（如 "/sdcard"）
 * @param port         HTTP 服务端口（默认 8080）
 * @return true        启动成功
 * @return false       启动失败
 */
bool SdCardLogHttpStart(const char* mount_point, uint16_t port);

/**
 * @brief 停止 SD 卡日志 HTTP 服务
 */
void SdCardLogHttpStop(void);

/**
 * @brief HTTP 服务是否正在运行
 */
bool SdCardLogHttpIsRunning(void);

/**
 * @brief 获取当前监听端口
 */
uint16_t SdCardLogHttpGetPort(void);

/**
 * @brief 触发一次屏幕截图，保存到 SD 卡
 *
 * @return true  截图成功
 * @return false 截图失败
 */
bool SdCardLogHttpTriggerSnapshot(void);

/**
 * @brief 错误/异常时自动截图（带错误信息前缀）
 *
 * @param error_tag   错误来源标签（如 "OTA", "WiFi"）
 * @param error_msg   错误信息
 * @return true       截图成功
 * @return false      截图失败
 */
bool SdCardLogHttpErrorSnapshot(const char* error_tag, const char* error_msg);

#ifdef __cplusplus
}
#endif
