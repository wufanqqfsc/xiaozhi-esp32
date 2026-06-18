#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import serial
import time
import os
import base64 as b64
import sys

script_dir = os.path.dirname(os.path.abspath(__file__))
project_dir = os.path.dirname(script_dir)
os.chdir(project_dir)

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

        print(f"Connected. Listening for 90s...")

        start_time = time.time()
        last_progress = 0
        last_data_time = time.time()

        while (time.time() - start_time) < 90:
            if ser.in_waiting > 0:
                data = ser.read(ser.in_waiting)
                log_file.write(data)
                received_bytes += len(data)
                last_data_time = time.time()

                try:
                    text = data.decode('utf-8', errors='replace')
                    sys.stdout.write(text)
                    sys.stdout.flush()
                except:
                    pass

                text_str = data.decode('utf-8', errors='ignore')
                if '===SCREENSHOT_START===' in text_str:
                    in_screenshot = True
                    base64_data = []
                    screenshot_count += 1
                    print(f"\n[CAPTURING SCREENSHOT #{screenshot_count}...]")
                elif '===SCREENSHOT_END===' in text_str:
                    in_screenshot = False
                    print(f"[SCREENSHOT #{screenshot_count} END]")
                    if base64_data:
                        try:
                            jpeg_data = b64.b64decode(''.join(base64_data))
                            latest_path = "screenshots/screenshot_latest.jpg"
                            os.makedirs("screenshots/history", exist_ok=True)
                            # 保存到 history，不覆盖 latest
                            save_path = f"screenshots/history/screenshot_{timestamp}_n{screenshot_count}.jpg"
                            with open(save_path, 'wb') as f:
                                f.write(jpeg_data)
                            saved_files.append(save_path)
                            print(f"SAVED: {save_path} ({len(jpeg_data)} bytes)")
                        except Exception as e:
                            print(f"Save error: {e}")
                elif in_screenshot:
                    lines = text_str.strip().split('\n')
                    for line in lines:
                        line = line.strip()
                        if line and not line.startswith('==='):
                            base64_data.append(line)

            time.sleep(0.005)

            elapsed = int(time.time() - start_time)
            if elapsed - last_progress >= 10:
                idle = int(time.time() - last_data_time)
                print(f"\n[{elapsed}s elapsed, idle {idle}s, {received_bytes} bytes received, {screenshot_count} started, {len(saved_files)} saved]")
                last_progress = elapsed

        print(f"\nDone. Saved {len(saved_files)} screenshots:")
        for f in saved_files:
            print(f"  {f}")

        # 设置最新的 fortune 截图（最后一个有 fortune 显示的）
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