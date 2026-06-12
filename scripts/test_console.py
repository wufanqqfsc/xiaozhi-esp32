#!/usr/bin/env python3
# 测试 console 命令
import serial
import time
import sys

def test_console(port, baud=115200):
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
        
        # 发送回车获取提示符
        ser.write(b'\r\n')
        time.sleep(0.5)
        
        # 读取响应
        response = ser.read(1024)
        print(f"[INFO] Response after enter:")
        print(repr(response))
        
        if b'xiaozhi>' in response:
            print("[OK] Console prompt detected!")
            
            # 发送 screenshot 命令
            print("[INFO] Sending 'screenshot' command...")
            ser.write(b'screenshot\r\n')
            time.sleep(3)
            
            # 读取响应
            response = ser.read(4096)
            print(f"[INFO] Response to screenshot:")
            print(repr(response))
            
            if b'Taking screenshot' in response:
                print("[OK] Screenshot command executed!")
            else:
                print("[WARN] Unexpected response")
        
        else:
            print("[WARN] No console prompt detected")
            
        ser.close()
        
    except serial.SerialException as e:
        print(f"[ERROR] Serial error: {e}")
        return False
    
    return True

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: python test_console.py <serial_port>")
        sys.exit(1)
    
    port = sys.argv[1]
    test_console(port)