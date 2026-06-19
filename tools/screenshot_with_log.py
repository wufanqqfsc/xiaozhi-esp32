#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import serial
import threading
import time
import sys
import os
import argparse
import base64 as b64

MAX_SCREENSHOTS = 20  # 默认最大截图次数

def receive_all_data(ser, buffer):
    """在后台线程中接收所有串口数据"""
    while True:
        try:
            if ser.in_waiting > 0:
                data = ser.read(ser.in_waiting)
                buffer.extend(data)
        except Exception as e:
            print(f"Receive error: {e}")
            break
        time.sleep(0.01)

def send_command(ser, command):
    """发送命令到设备"""
    if not command.endswith('\n'):
        command += '\n'
    ser.write(command.encode('utf-8'))
    print(f"Sent command: {command.strip()}")

def main():
    # 解析命令行参数
    parser = argparse.ArgumentParser(description='ESP32截图工具')
    parser.add_argument('--max-screenshots', type=int, default=MAX_SCREENSHOTS,
                        help=f'最大截图次数（默认: {MAX_SCREENSHOTS}）')
    parser.add_argument('--port', type=str, default='/dev/cu.usbmodem1101',
                        help='串口设备（默认: /dev/cu.usbmodem1101）')
    parser.add_argument('--click', type=int, default=None,
                        help='触发功能按钮点击（0=今日运势, 1=今日指南, 2=出门看卦, 3=今日求财）')
    parser.add_argument('--snap', action='store_true',
                        help='发送SNAP命令触发截图')
    args = parser.parse_args()
    
    port = args.port
    baud = 115200
    max_screenshots = args.max_screenshots

    timestamp = time.strftime("%Y%m%d_%H%M%S")
    os.makedirs("screenshots/logs", exist_ok=True)
    os.makedirs("screenshots/history", exist_ok=True)
    log_file = open(f"screenshots/logs/run_{timestamp}.log", "wb")

    try:
        # 打开串口
        ser = serial.Serial(
            port=port,
            baudrate=baud,
            timeout=1,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            bytesize=serial.EIGHTBITS
        )

        print(f"Connected to {port} at {baud} baud")
        print(f"Max screenshots: {max_screenshots}")

        # 等待设备启动
        print("Waiting 2s for device to initialize...")
        time.sleep(2)

        # 处理命令行参数
        if args.click is not None:
            # 创建一个共享buffer用于接收数据
            buffer = bytearray()
            
            # 在后台启动一个线程来接收数据
            import threading
            def receive_all():
                while True:
                    try:
                        if ser.in_waiting > 0:
                            data = ser.read(ser.in_waiting)
                            buffer.extend(data)
                    except Exception as e:
                        print(f"Receive error: {e}")
                        break
                    time.sleep(0.01)
            
            receive_thread = threading.Thread(target=receive_all)
            receive_thread.daemon = True
            receive_thread.start()
            
            # 等待一小段时间确保线程开始接收
            time.sleep(0.5)
            
            # 发送CLICK命令
            print(f"Sending CLICK command: index={args.click}")
            send_command(ser, f"CLICK:{args.click}")
            
            # 等待一小段时间让设备处理命令并开始发送截图数据
            time.sleep(0.5)
            
            # 接收截图数据
            print("Waiting for screenshot...")
            
            # 等待SCREENSHOT_START
            start_time = time.time()
            screenshot_start_pos = -1
            while time.time() - start_time < 30:
                buffer_str = buffer.decode('utf-8', errors='replace')
                if '===SCREENSHOT_START===' in buffer_str:
                    # 找到最后一个START（因为可能有多个）
                    screenshot_start_pos = buffer_str.rfind('===SCREENSHOT_START===')
                    print("SCREENSHOT_START received")
                    break
                time.sleep(0.1)
            
            if screenshot_start_pos < 0:
                print("No SCREENSHOT_START received")
                ser.close()
                return
            
            # 继续等待SCREENSHOT_END
            start_time = time.time()
            while time.time() - start_time < 30:
                buffer_str = buffer.decode('utf-8', errors='replace')
                end_pos = buffer_str.find('===SCREENSHOT_END===')
                if end_pos > screenshot_start_pos:
                    print("SCREENSHOT_END received")
                    break
                time.sleep(0.1)
            
            # 解析数据
            buffer_str = buffer.decode('utf-8', errors='replace')
            
            start_marker = '===SCREENSHOT_START==='
            end_marker = '===SCREENSHOT_END==='
            
            # 找最后一个END
            last_end = buffer_str.rfind(end_marker)
            if last_end < 0:
                print("No END marker found")
                ser.close()
                return
            
            # 在last_end之前找START（确保START在END之前）
            search_area = buffer_str[:last_end]
            last_start = search_area.rfind(start_marker)
            
            if last_start < 0:
                print(f"START not found before END at {last_end}")
                print(f"Buffer length: {len(buffer_str)}")
                ser.close()
                return
            
            # 提取Base64数据
            start_idx = last_start + len(start_marker)
            b64_data = buffer_str[start_idx:last_end].strip()
            
            if len(b64_data) < 100:
                print(f"Data too short: {len(b64_data)} chars")
                ser.close()
                return
            
            print(f"Screenshot data extracted: {len(b64_data)} chars")
            
            if b64_data:
                try:
                    jpeg_data = b64.b64decode(b64_data)
                    output_file = 'screenshots/screenshot_latest.jpg'
                    with open(output_file, 'wb') as f:
                        f.write(jpeg_data)
                    print(f"Snapshot saved to {output_file}")
                except Exception as e:
                    print(f"Error decoding: {e}")
            
            print("Done")
            ser.close()
            return
        
        if args.snap:
            # 创建一个共享buffer用于接收数据
            buffer = bytearray()
            
            # 在后台启动一个线程来接收数据
            import threading
            def receive_all():
                while True:
                    try:
                        if ser.in_waiting > 0:
                            data = ser.read(ser.in_waiting)
                            buffer.extend(data)
                    except Exception as e:
                        print(f"Receive error: {e}")
                        break
                    time.sleep(0.01)
            
            receive_thread = threading.Thread(target=receive_all)
            receive_thread.daemon = True
            receive_thread.start()
            
            # 等待一小段时间确保线程开始接收
            time.sleep(0.5)
            
            # 发送SNAP命令
            print("Sending SNAP command...")
            send_command(ser, "SNAP")
            
            # 接收截图数据
            print("Waiting for screenshot...")
            
            # 等待最多30秒
            start_time = time.time()
            while time.time() - start_time < 30:
                buffer_str = buffer.decode('utf-8', errors='replace')
                if '===SCREENSHOT_END===' in buffer_str:
                    break
                time.sleep(0.1)
            
            # 解析数据
            buffer_str = buffer.decode('utf-8', errors='replace')
            
            start_marker = '===SCREENSHOT_START==='
            end_marker = '===SCREENSHOT_END==='
            
            if start_marker not in buffer_str or end_marker not in buffer_str:
                print("Screenshot markers not found")
                ser.close()
                return
            
            # 提取Base64数据
            start_idx = buffer_str.find(start_marker) + len(start_marker)
            end_idx = buffer_str.find(end_marker)
            b64_data = buffer_str[start_idx:end_idx].strip()
            
            print(f"Screenshot data extracted: {len(b64_data)} chars")
            
            if b64_data:
                try:
                    jpeg_data = b64.b64decode(b64_data)
                    output_file = 'screenshots/screenshot_latest.jpg'
                    with open(output_file, 'wb') as f:
                        f.write(jpeg_data)
                    print(f"Snapshot saved to {output_file}")
                except Exception as e:
                    print(f"Error decoding: {e}")
            
            print("Done")
            ser.close()
            return
        
        # 默认行为：连续截图
        print("Starting continuous screenshot mode...")
        
        # 创建一个共享buffer用于接收数据
        buffer = bytearray()
        
        # 在后台启动一个线程来接收数据
        import threading
        def receive_all():
            while True:
                try:
                    if ser.in_waiting > 0:
                        data = ser.read(ser.in_waiting)
                        buffer.extend(data)
                except Exception as e:
                    print(f"Receive error: {e}")
                    break
                time.sleep(0.01)
        
        receive_thread = threading.Thread(target=receive_all)
        receive_thread.daemon = True
        receive_thread.start()
        
        # 先发送一个空行确保设备就绪
        send_command(ser, "")
        time.sleep(1)
        
        screenshot_count = 0
        while screenshot_count < max_screenshots:
            print(f"Sending snapshot request #{screenshot_count + 1}...")
            
            # 清空buffer
            buffer.clear()
            
            # 发送空命令触发截图
            send_command(ser, "")
            
            # 等待最多15秒
            start_time = time.time()
            while time.time() - start_time < 15:
                buffer_str = buffer.decode('utf-8', errors='replace')
                if '===SCREENSHOT_END===' in buffer_str:
                    break
                time.sleep(0.1)
            
            # 解析数据
            buffer_str = buffer.decode('utf-8', errors='replace')
            
            start_marker = '===SCREENSHOT_START==='
            end_marker = '===SCREENSHOT_END==='
            
            if start_marker in buffer_str and end_marker in buffer_str:
                # 提取Base64数据
                start_idx = buffer_str.find(start_marker) + len(start_marker)
                end_idx = buffer_str.find(end_marker)
                b64_data = buffer_str[start_idx:end_idx].strip()
                
                if b64_data:
                    try:
                        jpeg_data = b64.b64decode(b64_data)
                        output_file = f'screenshots/history/screenshot_{timestamp}_{screenshot_count:03d}.jpg'
                        with open(output_file, 'wb') as f:
                            f.write(jpeg_data)
                        print(f"Screenshot #{screenshot_count + 1} saved to {output_file}")
                    except Exception as e:
                        print(f"Error decoding screenshot #{screenshot_count + 1}: {e}")
                else:
                    print(f"Screenshot #{screenshot_count + 1} timeout or no data")
            else:
                print(f"Screenshot #{screenshot_count + 1} markers not found")
            
            screenshot_count += 1
            time.sleep(2)

        print(f"Done. Total screenshots: {screenshot_count}")

    except serial.SerialException as e:
        print(f"Serial error: {e}")
    except Exception as e:
        print(f"Error: {e}")
    finally:
        if 'ser' in locals():
            ser.close()
        if log_file:
            log_file.close()

if __name__ == '__main__':
    main()