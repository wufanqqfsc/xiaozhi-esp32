#pragma once

#include <stdint.h>
#include <stddef.h>

// 串口截图协议定义

// 协议版本
#define SNAPSHOT_PROTOCOL_VERSION 0x01

// 串口配置 - 使用 USB-Serial/JTAG (UART0)
#define SNAPSHOT_UART_PORT 0                      // 使用 UART0 (USB-Serial/JTAG)
#define SNAPSHOT_UART_BAUD_RATE 115200            // 波特率

// 协议帧格式
// +--------+--------+--------+--------+---------+--------+
// | 0xAA   | 0xBB   | cmd    | len    | data    | crc    |
// | (1B)   | (1B)   | (1B)   | (2B)   | (len B) | (2B)   |
// +--------+--------+--------+--------+---------+--------+

// 帧头
#define SNAPSHOT_FRAME_HEADER_1 0xAA
#define SNAPSHOT_FRAME_HEADER_2 0xBB

// 最大数据包大小
#define SNAPSHOT_MAX_DATA_SIZE 4096
#define SNAPSHOT_MAX_FRAME_SIZE (SNAPSHOT_MAX_DATA_SIZE + 6)  // 包头(2) + cmd(1) + len(2) + crc(2)

// 命令定义
typedef enum {
    SNAPSHOT_CMD_SNAPSHOT_REQ = 0x01,    // 主机请求截图
    SNAPSHOT_CMD_SNAPSHOT_DATA = 0x02,   // ESP32发送截图数据
    SNAPSHOT_CMD_ACK = 0x03,             // 确认
    SNAPSHOT_CMD_NACK = 0x04,            // 否定确认
    SNAPSHOT_CMD_PING = 0x05,            // 心跳检测
    SNAPSHOT_CMD_PONG = 0x06,            // 心跳响应
} snapshot_cmd_t;

// 错误码定义
typedef enum {
    SNAPSHOT_ERR_NONE = 0x00,            // 无错误
    SNAPSHOT_ERR_INVALID_CMD = 0x01,     // 无效命令
    SNAPSHOT_ERR_INVALID_LEN = 0x02,     // 无效长度
    SNAPSHOT_ERR_CRC_ERROR = 0x03,       // CRC校验失败
    SNAPSHOT_ERR_BUSY = 0x04,            // 设备忙
    SNAPSHOT_ERR_NO_MEM = 0x05,          // 内存不足
    SNAPSHOT_ERR_TIMEOUT = 0x06,         // 超时
} snapshot_error_t;

// 帧结构
typedef struct {
    uint8_t cmd;
    uint16_t len;
    uint8_t data[SNAPSHOT_MAX_DATA_SIZE];
} snapshot_frame_t;

// 计算CRC16
uint16_t snapshot_crc16(const uint8_t* data, size_t len);

// 打包帧数据
size_t snapshot_pack_frame(uint8_t* buf, size_t buf_len, uint8_t cmd, const uint8_t* data, uint16_t data_len);

// 解析帧数据
// 返回值: 0=成功, <0=错误码, >0=需要更多数据
int snapshot_parse_frame(const uint8_t* buf, size_t buf_len, snapshot_frame_t* frame);

// 生成ACK帧
size_t snapshot_pack_ack(uint8_t* buf, size_t buf_len, snapshot_error_t error);

// 生成Ping帧
size_t snapshot_pack_ping(uint8_t* buf, size_t buf_len);

// 生成Pong帧
size_t snapshot_pack_pong(uint8_t* buf, size_t buf_len);