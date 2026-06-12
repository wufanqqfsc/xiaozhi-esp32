#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import serial
import argparse
import time
import base64
import os
import crcmod

# 协议定义
SNAPSHOT_FRAME_HEADER_1 = 0xAA
SNAPSHOT_FRAME_HEADER_2 = 0xBB

# 命令定义
SNAPSHOT_CMD_SNAPSHOT_REQ = 0x01
SNAPSHOT_CMD_SNAPSHOT_DATA = 0x02
SNAPSHOT_CMD_ACK = 0x03
SNAPSHOT_CMD_PING = 0x04
SNAPSHOT_CMD_PONG = 0x05

# 错误码定义
SNAPSHOT_ERR_NONE = 0x00
SNAPSHOT_ERR_INVALID_CMD = 0x01
SNAPSHOT_ERR_INVALID_LEN = 0x02
SNAPSHOT_ERR_CRC_ERROR = 0x03
SNAPSHOT_ERR_NO_MEM = 0x04

# 最大数据大小
SNAPSHOT_MAX_DATA_SIZE = 4096
SNAPSHOT_MAX_FRAME_SIZE = SNAPSHOT_MAX_DATA_SIZE + 6

# CRC16-CCITT
crc16_ccitt = crcmod.predefined.Crc('crc-ccitt-false')

def calculate_crc(data):
    """计算CRC16-CCITT校验"""
    crc16_ccitt.reset()
    crc16_ccitt.update(data)
    return crc16_ccitt.digest()

def pack_frame(cmd, data=None):
    """打包帧"""
    data_len = len(data) if data else 0
    frame = bytearray()
    frame.append(SNAPSHOT_FRAME_HEADER_1)
    frame.append(SNAPSHOT_FRAME_HEADER_2)
    frame.append(cmd)
    frame.append((data_len >> 8) & 0xFF)
    frame.append(data_len & 0xFF)
    if data:
        frame.extend(data)
    
    # 计算CRC
    crc = calculate_crc(frame[2:])  # 从cmd开始计算
    frame.append((crc[0] >> 8) & 0xFF if len(crc) > 1 else 0)
    frame.append(crc[0] & 0xFF)
    
    return frame

def parse_frame(data):
    """解析帧"""
    if len(data) < 6:
        return None, 0  # 需要更多数据
    
    # 检查帧头
    if data[0] != SNAPSHOT_FRAME_HEADER_1 or data[1] != SNAPSHOT_FRAME_HEADER_2:
        return None, -1  # 无效帧头
    
    cmd = data[2]
    data_len = (data[3] << 8) | data[4]
    
    # 检查长度是否合理
    if data_len > SNAPSHOT_MAX_DATA_SIZE:
        return None, -2  # 无效长度
    
    # 检查是否有足够的数据
    expected_len = data_len + 6
    if len(data) < expected_len:
        return None, expected_len - len(data)  # 需要更多数据
    
    # 验证CRC
    crc = (data[expected_len - 2] << 8) | data[expected_len - 1]
    calc_crc = calculate_crc(data[2:expected_len - 2])
    calc_crc_val = (calc_crc[0] << 8) | calc_crc[1] if len(calc_crc) > 1 else calc_crc[0]
    
    if crc != calc_crc_val:
        return None, -3  # CRC错误
    
    # 提取数据
    frame_data = data[5:5 + data_len] if data_len > 0 else None
    
    return {'cmd': cmd, 'len': data_len, 'data': frame_data}, expected_len

def send_ping(ser):
    """发送PING命令"""
    frame = pack_frame(SNAPSHOT_CMD_PING)
    ser.write(frame)
    print("Sent PING")

def send_snapshot_request(ser):
    """发送截图请求"""
    frame = pack_frame(SNAPSHOT_CMD_SNAPSHOT_REQ)
    ser.write(frame)
    print("Sent snapshot request")

def receive_snapshot(ser, output_file):
    """接收截图数据"""
    buffer = bytearray()
    base64_data = bytearray()
    start_time = time.time()
    
    print("Waiting for response...")
    
    while True:
        try:
            # 读取串口数据
            if ser.in_waiting > 0:
                data = ser.read(ser.in_waiting)
                buffer.extend(data)
                
                # 尝试解析帧
                while len(buffer) >= 6:
                    frame, consumed = parse_frame(buffer)
                    
                    if frame is None:
                        if consumed < 0:
                            # 帧错误，丢弃第一个字节
                            buffer = buffer[1:]
                        break
                    
                    # 处理帧
                    buffer = buffer[consumed:]
                    
                    if frame['cmd'] == SNAPSHOT_CMD_SNAPSHOT_DATA:
                        if frame['data']:
                            base64_data.extend(frame['data'])
                            print(f"Received data chunk: {len(frame['data'])} bytes, total: {len(base64_data)} bytes")
                    
                    elif frame['cmd'] == SNAPSHOT_CMD_ACK:
                        error = frame['data'][0] if frame['data'] else SNAPSHOT_ERR_NONE
                        if error == SNAPSHOT_ERR_NONE:
                            print("Received ACK - transfer complete")
                            
                            # 解码Base64并保存JPEG
                            try:
                                jpeg_data = base64.b64decode(base64_data)
                                with open(output_file, 'wb') as f:
                                    f.write(jpeg_data)
                                print(f"Snapshot saved to {output_file}")
                                print(f"Total time: {time.time() - start_time:.2f} seconds")
                                print(f"JPEG size: {len(jpeg_data)} bytes")
                                return True
                            except Exception as e:
                                print(f"Error decoding Base64: {e}")
                                return False
                        else:
                            print(f"Received ACK with error: 0x{error:02X}")
                            return False
                    
                    elif frame['cmd'] == SNAPSHOT_CMD_PONG:
                        print("Received PONG")
                    
                    else:
                        print(f"Unknown command: 0x{frame['cmd']:02X}")
            
            time.sleep(0.01)
            
            # 超时检测
            if time.time() - start_time > 30:
                print("Timeout waiting for response")
                return False
                
        except KeyboardInterrupt:
            print("User interrupted")
            return False
        except Exception as e:
            print(f"Error receiving data: {e}")
            return False

def main():
    parser = argparse.ArgumentParser(description='ESP32 Snapshot Receiver')
    parser.add_argument('-p', '--port', required=True, help='Serial port (e.g., /dev/ttyUSB0 or COM3)')
    parser.add_argument('-b', '--baud', type=int, default=921600, help='Baud rate (default: 921600)')
    parser.add_argument('-o', '--output', default='snapshot.jpg', help='Output file (default: snapshot.jpg)')
    parser.add_argument('-t', '--timeout', type=int, default=5, help='Serial timeout in seconds (default: 5)')
    parser.add_argument('--ping', action='store_true', help='Send PING command')
    parser.add_argument('--snapshot', action='store_true', help='Request snapshot')
    
    args = parser.parse_args()
    
    try:
        # 打开串口
        ser = serial.Serial(
            port=args.port,
            baudrate=args.baud,
            timeout=args.timeout,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_1,
            bytesize=serial.EIGHTBITS
        )
        
        print(f"Connected to {args.port} at {args.baud} baud")
        
        if args.ping:
            send_ping(ser)
            time.sleep(1)
            
        if args.snapshot:
            send_snapshot_request(ser)
            receive_snapshot(ser, args.output)
        
        ser.close()
        
    except serial.SerialException as e:
        print(f"Serial error: {e}")
    except Exception as e:
        print(f"Error: {e}")

if __name__ == '__main__':
    main()
