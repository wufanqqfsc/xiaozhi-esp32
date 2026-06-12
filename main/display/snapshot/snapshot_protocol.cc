#include "snapshot_protocol.h"
#include <cstring>

// CRC16-CCITT 多项式
#define CRC16_CCITT_POLY 0x1021
#define CRC16_CCITT_INIT 0xFFFF

uint16_t snapshot_crc16(const uint8_t* data, size_t len) {
    uint16_t crc = CRC16_CCITT_INIT;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            crc = (crc << 1) ^ ((crc & 0x8000) ? CRC16_CCITT_POLY : 0);
        }
    }
    return crc;
}

size_t snapshot_pack_frame(uint8_t* buf, size_t buf_len, uint8_t cmd, const uint8_t* data, uint16_t data_len) {
    if (buf_len < data_len + 6) {
        return 0;
    }
    
    size_t pos = 0;
    buf[pos++] = SNAPSHOT_FRAME_HEADER_1;
    buf[pos++] = SNAPSHOT_FRAME_HEADER_2;
    buf[pos++] = cmd;
    buf[pos++] = (data_len >> 8) & 0xFF;
    buf[pos++] = data_len & 0xFF;
    
    if (data && data_len > 0) {
        memcpy(buf + pos, data, data_len);
        pos += data_len;
    }
    
    uint16_t crc = snapshot_crc16(buf + 2, pos - 2);  // 从cmd开始计算
    buf[pos++] = (crc >> 8) & 0xFF;
    buf[pos++] = crc & 0xFF;
    
    return pos;
}

int snapshot_parse_frame(const uint8_t* buf, size_t buf_len, snapshot_frame_t* frame) {
    if (buf_len < 6) {
        return 1;  // 需要更多数据
    }
    
    // 检查帧头
    if (buf[0] != SNAPSHOT_FRAME_HEADER_1 || buf[1] != SNAPSHOT_FRAME_HEADER_2) {
        return -1;  // 无效帧头
    }
    
    uint8_t cmd = buf[2];
    uint16_t len = (buf[3] << 8) | buf[4];
    
    // 检查长度是否合理
    if (len > SNAPSHOT_MAX_DATA_SIZE) {
        return -2;  // 无效长度
    }
    
    // 检查是否有足够的数据
    size_t expected_len = len + 6;
    if (buf_len < expected_len) {
        return expected_len - buf_len;  // 需要更多数据
    }
    
    // 验证CRC
    uint16_t crc = (buf[expected_len - 2] << 8) | buf[expected_len - 1];
    uint16_t calc_crc = snapshot_crc16(buf + 2, expected_len - 4);
    if (crc != calc_crc) {
        return -3;  // CRC错误
    }
    
    // 填充帧结构
    frame->cmd = cmd;
    frame->len = len;
    if (len > 0) {
        memcpy(frame->data, buf + 5, len);
    }
    
    return 0;  // 成功
}

size_t snapshot_pack_ack(uint8_t* buf, size_t buf_len, snapshot_error_t error) {
    uint8_t data = (uint8_t)error;
    return snapshot_pack_frame(buf, buf_len, SNAPSHOT_CMD_ACK, &data, 1);
}

size_t snapshot_pack_ping(uint8_t* buf, size_t buf_len) {
    return snapshot_pack_frame(buf, buf_len, SNAPSHOT_CMD_PING, NULL, 0);
}

size_t snapshot_pack_pong(uint8_t* buf, size_t buf_len) {
    return snapshot_pack_frame(buf, buf_len, SNAPSHOT_CMD_PONG, NULL, 0);
}