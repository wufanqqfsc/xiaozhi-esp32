#!/usr/bin/env python3
"""
通过 USB-Serial/JTAG 端口测试截图功能
"""
import serial
import time
import struct
import sys
import base64
import os

# 协议配置
FRAME_HEADER1 = 0xAA
FRAME_HEADER2 = 0xBB
SNAPSHOT_CMD_SNAPSHOT_REQ = 0x01
SNAPSHOT_CMD_SNAPSHOT_DATA = 0x02
SNAPSHOT_CMD_ACK = 0x03
SNAPSHOT_CMD_NACK = 0x04
SNAPSHOT_CMD_PING = 0x05
SNAPSHOT_CMD_PONG = 0x06
TIMEOUT_SEC = 30

def crc16_ccitt(data):
    crc = 0xFFFF
    poly = 0x1021
    for byte in data:
        crc ^= (byte << 8)
        for _ in range(8):
            crc = (crc << 1) ^ poly if (crc & 0x8000) else (crc << 1)
    crc &= 0xFFFF
    return crc

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

def parse_frame(data):
    """解析帧，返回 (cmd, payload, remaining) 或 None"""
    if len(data) < 6:
        return None
    
    if data[0] != FRAME_HEADER1 or data[1] != FRAME_HEADER2:
        return None
    
    cmd = data[2]
    length = struct.unpack('>H', data[3:5])[0]
    
    if len(data) < 6 + length:
        return None
    
    payload = data[5:5+length]
    crc = struct.unpack('>H', data[5+length:7+length])[0]
    
    # 验证 CRC
    crc_data = bytes([cmd]) + struct.pack('>H', length) + payload
    calculated_crc = crc16_ccitt(crc_data)
    if crc != calculated_crc:
        print(f"[WARN] CRC mismatch: expected {calculated_crc:04X}, got {crc:04X}")
    
    return (cmd, payload, data[7+length:])

def test_screenshot(port, baud=115200):
    try:
        ser = serial.Serial(
            port=port,
            baudrate=baud,
            timeout=2,
            write_timeout=30,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
        )
        print(f"[INFO] Opened {port} at {baud} baud")
        
        # 清空缓冲区
        ser.reset_input_buffer()
        time.sleep(0.5)
        
        # 发送截图请求
        print("[INFO] Sending screenshot request...")
        req_frame = pack_frame(SNAPSHOT_CMD_SNAPSHOT_REQ)
        ser.write(req_frame)
        ser.flush()
        print(f"[INFO] Sent: {req_frame.hex()}")
        
        # 收集数据
        start = time.time()
        buffer = b''
        base64_data = []
        received_cmd = None
        
        while time.time() - start < TIMEOUT_SEC:
            if ser.in_waiting > 0:
                buffer += ser.read(ser.in_waiting)
                
                # 处理缓冲区中的数据
                while True:
                    result = parse_frame(buffer)
                    if result is None:
                        break
                    
                    cmd, payload, buffer = result
                    received_cmd = cmd
                    
                    if cmd == SNAPSHOT_CMD_SNAPSHOT_DATA:
                        base64_data.append(payload.decode('ascii', errors='ignore'))
                        print(f"[INFO] Received data chunk, total chunks: {len(base64_data)}")
                    elif cmd == SNAPSHOT_CMD_ACK:
                        error_code = payload[0] if payload else 0
                        if error_code == 0:
                            print("[OK] Transfer complete, ACK received")
                        else:
                            print(f"[ERROR] NACK received, error code: {error_code}")
                        break
            
            time.sleep(0.01)
        
        ser.close()
        
        # 解码并保存
        if base64_data:
            full_base64 = ''.join(base64_data)
            jpeg_data = base64.b64decode(full_base64)
            
            # 确保输出目录存在
            output_dir = './screenshots'
            os.makedirs(output_dir, exist_ok=True)
            
            # 保存文件
            import datetime
            timestamp = datetime.datetime.now().strftime('%Y%m%d_%H%M%S')
            output_path = os.path.join(output_dir, f'screenshot_{timestamp}.jpg')
            
            with open(output_path, 'wb') as f:
                f.write(jpeg_data)
            
            print(f"[OK] Screenshot saved to: {output_path}")
            print(f"[INFO] File size: {len(jpeg_data)} bytes")
            return True
        else:
            print("[ERROR] No screenshot data received")
            return False
        
    except serial.SerialException as e:
        print(f"[ERROR] Serial error: {e}")
        return False
    except Exception as e:
        print(f"[ERROR] Error: {e}")
        import traceback
        traceback.print_exc()
        return False

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: python test_screenshot_via_usb.py <serial_port> [baud]")
        print("Example: python test_screenshot_via_usb.py /dev/cu.usbmodem101 115200")
        sys.exit(1)
    
    port = sys.argv[1]
    baud = int(sys.argv[2]) if len(sys.argv) > 2 else 115200
    test_screenshot(port, baud)
