#ifndef _SDCARD_LOG_H
#define _SDCARD_LOG_H

#include <stddef.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 初始化 SD 卡日志功能（C 接口）
 * - 仅在 mount_point (默认 /sdcard) 已挂载的前提下生效；否则静默 no-op
 * - 重定向 ESP_LOG 到 <mount>/xiaozhi_<boottime>.log（按启动时间命名，避免覆盖历史）
 * - 同一文件互斥写入，多任务/中断安全
 *
 * @return true=已成功重定向；false=未挂载/未使能（不影响正常 ESP_LOG）
 */
bool SdCardLogStart(const char* mount_point);

/**
 * 显式停止日志重定向。拔卡前调用，避免写未挂载路径。
 */
void SdCardLogStop();

/**
 * 当前是否已重定向到 SD 卡。
 */
bool SdCardLogIsActive();

/**
 * 获取当前日志文件路径，未启动时返回空串。
 */
const char* SdCardLogGetPath();

/**
 * 强制把缓冲刷入磁盘（fsync）。
 */
void SdCardLogFlush();

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // _SDCARD_LOG_H
