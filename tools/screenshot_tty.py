#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import serial
import time
import os
import base64 as b64

script_dir = os.path.dirname(os.path.abspath(__file__))
project_dir = os.path.dirname(script_dir)
os.chdir(project_dir)

MAX_HISTORY = 20

def get_history_count():
    history_dir = "screenshots/history"
    if not os.path.exists(history_dir):
        return 0
    return len([f for f in os.listdir(history_dir) if f.endswith('.jpg')])

def backup_old_screenshot():
    latest_path = "screenshots/screenshot_latest.jpg"
    history_dir = "screenshots/history"
    if os.path.exists(latest_path):
        os.makedirs(history_dir, exist_ok=True)
        timestamp = time.strftime("%Y%m%d_%H%M%S")
        backup_path = f"{history_dir}/screenshot_{timestamp}.jpg"
        os.rename(latest_path, backup_path)
        print(f"Backed up old screenshot to: {backup_path}")

def main():
    port = '/dev/tty.usbmodem1101'
    baud = 115200

    timestamp = time.strftime("%Y%m%d_%H%M%S")
    os.makedirs(f"screenshots/logs", exist_ok=True)
    log_file = open(f"screenshots/logs/raw_{timestamp}.log", "wb")

    base64_data = []
    in_screenshot = False
    screenshot_count = 0

    print(f"Using port: {port}")
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
        print("Monitoring for 60s...")

        start_time = time.time()
        while time.time() - start_time < 60:
            if ser.in_waiting > 0:
                data = ser.read(ser.in_waiting)
                log_file.write(data)

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
                    print(f"[SCREENSHOT #{screenshot_count} CAPTURED]")
                    if base64_data:
                        try:
                            jpeg_data = b64.b64decode(''.join(base64_data))
                            backup_old_screenshot()
                            output_jpg = "screenshots/screenshot_latest.jpg"
                            with open(output_jpg, 'wb') as f:
                                f.write(jpeg_data)
                            print(f"Saved: {output_jpg} ({len(jpeg_data)} bytes)")
                            history_count = get_history_count()
                            print(f"History count: {history_count}/{MAX_HISTORY}")
                            break
                        except Exception as e:
                            print(f"Decode error: {e}")
                elif in_screenshot:
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

if __name__ == '__main__':
    import sys
    main()