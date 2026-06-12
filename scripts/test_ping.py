#!/usr/bin/env python3
# 测试 UART1 Ping 命令
import serial
import time
import struct
import sys

# 协议配置
FRAME_HEADER1 = 0xAA
FRAME_HEADER2 = 0xBB
SNAPSHOT_CMD_PING = 0x05
SNAPSHOT_CMD_PONG = 0x06
TIMEOUT_SEC = 5

def crc16_ccitt(data):
    crc = 0xFFFF
    poly = 0x1021
    for byte in data:
        crc ^= (byte << 8)
        for _ in range(8):
            crc = (crc << 1) ^ poly if (crc & 0x8000) else (crc << 1)
    return crc & 0xFFFF

def pack_frame(cmd, data=b''):
    data_len = len(data)
    crc_data = bytes([cmd]) + struct.pack('>H', data_len) + data
    crc = crc16_ccitt(crc_data)
    
    frame = bytes([FRAME_HEADER1, FRAME_HEADER2])
    frame += bytes([cmd])
    frame += struct.pack('>H', data_len)
    frame += data
    frame += struct.pack('>H', crc)
    
    return frame

def test_ping(port, baud=115200):
    try:
        ser = serial.Serial(
            port=port,
            baudrate=baud,
            timeout=2,
            write_timeout=2,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
        )
        print(f"[INFO] Opened {port} at {baud} baud")
        
        # 清空缓冲区
        ser.reset_input_buffer()
        time.sleep(1)
        
        # 发送 Ping 命令
        print("[INFO] Sending PING command...")
        ping_frame = pack_frame(SNAPSHOT_CMD_PING)
        ser.write(ping_frame)
        ser.flush()
        
        # 等待响应
        start = time.time()
        response = b''
        while time.time() - start < TIMEOUT_SEC:
            if ser.in_waiting > 0:
                response += ser.read(ser.in_waiting)
                # 检查是否收到完整响应
                if len(response) >= 6:  # 最小帧长度
                    if response[0] == FRAME_HEADER1 and response[1] == FRAME_HEADER2:
                        cmd = response[2]
                        if cmd == SNAPSHOT_CMD_PONG:
                            print("[OK] PONG response received!")
                            print(f"[INFO] Raw response: {response.hex()}")
                            ser.close()
                            return True
            time.sleep(0.1)
        
        print(f"[ERROR] No PONG response received within {TIMEOUT_SEC} seconds")
        print(f"[INFO] Received: {response.hex() if response else 'nothing'}")
        ser.close()
        return False
        
    except serial.SerialException as e:
        print(f"[ERROR] Serial error: {e}")
        return False

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: python test_ping.py <serial_port>")
        sys.exit(1)
    
    port = sys.argv[1]
    test_ping(port)