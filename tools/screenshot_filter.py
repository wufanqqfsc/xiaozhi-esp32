#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import serial
import time
import os
import base64 as b64
import sys
import re

script_dir = os.path.dirname(os.path.abspath(__file__))
project_dir = os.path.dirname(script_dir)
os.chdir(project_dir)

def is_base64_line(line):
    """检查一行是否像 base64 数据（只包含 base64 字符和空白）"""
    stripped = line.strip()
    if not stripped:
        return False
    if stripped.startswith('==='):
        return False
    if stripped.startswith('[') and 'SCREENSHOT' in stripped:
        return False
    if re.match(r'^[IWDE] \(\d+\)', stripped):  # ESP_LOG 日志
        return False
    # 检查是否全是 base64 字符或空白
    return bool(re.match(r'^[A-Za-z0-9+/=\s]+$', stripped))

def main():
    port = '/dev/tty.usbmodem1101'
    baud = 115200

    timestamp = time.strftime("%Y%m%d_%H%M%S")
    os.makedirs(f"screenshots/logs", exist_ok=True)
    log_file = open(f"screenshots/logs/raw_{timestamp}.log", "wb")

    base64_data = []
    in_screenshot = False
    screenshot_count = 0
    saved_files = []
    received_bytes = 0

    print(f"Using port: {port}")

    try:
        ser = serial.Serial(
            port=port,
            baudrate=baud,
            timeout=0.1,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            bytesize=serial.EIGHTBITS
        )

        print(f"Connected. Listening for 90s with base64 filtering...")

        start_time = time.time()
        last_progress = 0

        while (time.time() - start_time) < 90:
            if ser.in_waiting > 0:
                data = ser.read(ser.in_waiting)
                log_file.write(data)
                received_bytes += len(data)

                text_str = data.decode('utf-8', errors='ignore')

                if '===SCREENSHOT_START===' in text_str:
                    in_screenshot = True
                    base64_data = []
                    screenshot_count += 1
                    print(f"\n[CAPTURING #{screenshot_count}]")
                elif '===SCREENSHOT_END===' in text_str:
                    in_screenshot = False
                    full_b64 = ''.join(base64_data)
                    print(f"[END #{screenshot_count}] b64={len(full_b64)} chars")
                    if full_b64:
                        try:
                            jpeg_data = b64.b64decode(full_b64)
                            os.makedirs("screenshots/history", exist_ok=True)
                            save_path = f"screenshots/history/screenshot_{timestamp}_n{screenshot_count}.jpg"
                            with open(save_path, 'wb') as f:
                                f.write(jpeg_data)
                            saved_files.append(save_path)
                            print(f"[SAVED] {save_path} ({len(jpeg_data)} bytes)")
                        except Exception as e:
                            print(f"[Decode error] {e}")
                elif in_screenshot:
                    # 仅添加看起来像 base64 的行
                    for line in text_str.split('\n'):
                        if is_base64_line(line):
                            stripped = line.strip()
                            if stripped:
                                base64_data.append(stripped)

            time.sleep(0.005)

            elapsed = int(time.time() - start_time)
            if elapsed - last_progress >= 15:
                print(f"\n[{elapsed}s elapsed, {received_bytes} bytes, {screenshot_count} started, {len(saved_files)} saved]")
                last_progress = elapsed

        print(f"\nDone. Saved {len(saved_files)} screenshots:")
        for f in saved_files:
            print(f"  {f}")

        if saved_files:
            latest = saved_files[-1]
            with open(latest, 'rb') as src:
                data = src.read()
            with open("screenshots/screenshot_latest.jpg", 'wb') as dst:
                dst.write(data)
            print(f"Latest set to: {latest}")

    except serial.SerialException as e:
        print(f"Serial error: {e}")
    except KeyboardInterrupt:
        print("\nInterrupted")
    finally:
        if 'ser' in locals():
            ser.close()
        log_file.close()

if __name__ == '__main__':
    main()