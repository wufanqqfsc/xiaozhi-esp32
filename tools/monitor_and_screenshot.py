#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import serial
import time
import re
import os
import base64 as b64

script_dir = os.path.dirname(os.path.abspath(__file__))
project_dir = os.path.dirname(script_dir)
os.chdir(project_dir)

MAX_HISTORY = 20  # 最多保留20个历史截图

def get_history_count():
    """获取history文件夹中的截图数量"""
    history_dir = "screenshots/history"
    if not os.path.exists(history_dir):
        return 0
    return len([f for f in os.listdir(history_dir) if f.endswith('.jpg')])

def backup_old_screenshot():
    """将旧截图备份到history文件夹"""
    latest_path = "screenshots/screenshot_latest.jpg"
    history_dir = "screenshots/history"

    if os.path.exists(latest_path):
        os.makedirs(history_dir, exist_ok=True)
        timestamp = time.strftime("%Y%m%d_%H%M%S")
        backup_path = f"{history_dir}/screenshot_{timestamp}.jpg"
        os.rename(latest_path, backup_path)
        print(f"Backed up old screenshot to: {backup_path}")

def main():
    port = '/dev/cu.usbmodem1101'
    baud = 115200

    timestamp = time.strftime("%Y%m%d_%H%M%S")
    os.makedirs(f"screenshots/logs", exist_ok=True)
    log_file = open(f"screenshots/logs/raw_{timestamp}.log", "wb")

    base64_data = []
    in_screenshot = False
    screenshot_count = 0

    print(f"History limit: {MAX_HISTORY} screenshots")
    print(f"Current history count: {get_history_count()}")

    try:
        ser = serial.Serial(
            port=port,
            baudrate=baud,
            timeout=1,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            bytesize=serial.EIGHTBITS
        )

        print(f"Connected to {port} at {baud} baud")
        print("Monitoring... Press Ctrl+C to stop")
        print("Screenshots will be saved every 5 seconds")
        print("Latest: screenshots/screenshot_latest.jpg")
        print("History: screenshots/history/")

        while True:
            if ser.in_waiting > 0:
                data = ser.read(ser.in_waiting)
                log_file.write(data)

                # 解码并打印日志
                try:
                    text = data.decode('utf-8', errors='replace')
                    print(text, end='')
                except:
                    pass

                # 检测截图数据
                text_str = data.decode('utf-8', errors='ignore')
                if '===SCREENSHOT_START===' in text_str:
                    in_screenshot = True
                    base64_data = []
                    screenshot_count += 1
                    print(f"\n[CAPTURING SCREENSHOT #{screenshot_count}...]")
                elif '===SCREENSHOT_END===' in text_str:
                    in_screenshot = False
                    print(f"[SCREENSHOT #{screenshot_count} CAPTURED]")

                    # 解码Base64
                    if base64_data:
                        try:
                            jpeg_data = b64.b64decode(''.join(base64_data))

                            # 备份旧截图
                            backup_old_screenshot()

                            # 保存最新截图
                            output_jpg = "screenshots/screenshot_latest.jpg"
                            with open(output_jpg, 'wb') as f:
                                f.write(jpeg_data)
                            print(f"Saved: {output_jpg} ({len(jpeg_data)} bytes)")

                            # 检查history数量
                            history_count = get_history_count()
                            print(f"History count: {history_count}/{MAX_HISTORY}")
                            if history_count >= MAX_HISTORY:
                                print(f"History limit reached ({MAX_HISTORY}), exiting...")
                                raise KeyboardInterrupt

                        except Exception as e:
                            print(f"Decode error: {e}")
                elif in_screenshot:
                    # 收集Base64数据行
                    lines = text_str.strip().split('\n')
                    for line in lines:
                        line = line.strip()
                        if line and not line.startswith('==='):
                            base64_data.append(line)

            time.sleep(0.01)

    except serial.SerialException as e:
        print(f"Serial error: {e}")
    except KeyboardInterrupt:
        print("\nInterrupted")
    finally:
        if 'ser' in locals():
            ser.close()
        log_file.close()
        print(f"\nLog saved to screenshots/logs/raw_{timestamp}.log")

if __name__ == '__main__':
    main()