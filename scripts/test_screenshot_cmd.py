#!/usr/bin/env python3
# 测试截图命令
import serial
import time
import sys

def test_screenshot_cmd(port, baud=115200):
    try:
        ser = serial.Serial(
            port=port,
            baudrate=baud,
            timeout=3,
            write_timeout=3,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
        )
        print(f"[INFO] Opened {port} at {baud} baud")
        print(f"[INFO] Resetting buffers...")
        
        # 清空缓冲区
        ser.reset_input_buffer()
        ser.reset_output_buffer()
        time.sleep(0.5)
        
        # 发送命令
        print("[INFO] Sending 'screenshot' command...")
        ser.write(b'screenshot\r\n')
        ser.flush()
        
        # 等待响应
        time.sleep(2)
        
        # 读取响应
        response = b''
        while ser.in_waiting > 0:
            response += ser.read(ser.in_waiting)
            time.sleep(0.1)
        
        print(f"[INFO] Response ({len(response)} bytes):")
        print(repr(response))
        
        # 尝试解码
        try:
            print(f"[INFO] Decoded response:")
            print(response.decode('utf-8', errors='replace'))
        except:
            pass
        
        # 检查是否有预期响应
        if b'Taking screenshot' in response:
            print("[OK] Screenshot command recognized!")
        elif b'xiaozhi>' in response:
            print("[OK] Console prompt detected, but no command response")
        else:
            print("[WARN] Unexpected response")
            
        ser.close()
        
    except serial.SerialException as e:
        print(f"[ERROR] Serial error: {e}")
        return False
    
    return True

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: python test_screenshot_cmd.py <serial_port>")
        sys.exit(1)
    
    port = sys.argv[1]
    test_screenshot_cmd(port)