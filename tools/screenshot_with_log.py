#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import serial
import threading
import time
import sys
import os
import argparse
import base64 as b64

# 添加项目路径
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from snapshot_receiver import pack_frame, parse_frame, SNAPSHOT_CMD_SNAPSHOT_REQ, SNAPSHOT_CMD_PING

MAX_SCREENSHOTS = 20  # 默认最大截图次数

def read_serial(ser, log_file):
    """读取串口数据并保存到日志"""
    buffer = bytearray()
    while True:
        try:
            if ser.in_waiting > 0:
                data = ser.read(ser.in_waiting)
                buffer.extend(data)

                # 尝试解析帧
                while len(buffer) >= 6:
                    frame, consumed = parse_frame(buffer)
                    if frame is None:
                        if consumed < 0:
                            buffer = buffer[1:]
                        break

                    buffer = buffer[consumed:]

                    # 只处理非SNAPSHOT帧（打印日志）
                    if frame['cmd'] not in [0x02]:  # 0x02 is SNAPSHOT_DATA
                        print(f"LOG: {data.decode('utf-8', errors='replace')}", end='')
                        if log_file:
                            log_file.write(data)
                            log_file.flush()
        except Exception as e:
            print(f"Read error: {e}")
            break
        time.sleep(0.01)

def send_command(ser, command):
    """发送命令到设备"""
    # 添加换行符（如果需要）
    if not command.endswith('\n'):
        command += '\n'
    
    # 发送命令
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
    baud = 115200  # 使用115200监控波特率
    max_screenshots = args.max_screenshots

    timestamp = time.strftime("%Y%m%d_%H%M%S")
    os.makedirs("screenshots/logs", exist_ok=True)
    log_file = open(f"screenshots/logs/run_{timestamp}.log", "wb")

    try:
        # 打开串口（用于监控）
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
        log_file.write(f"Connected to {port} at {baud} baud\n".encode())
        log_file.write(f"Max screenshots: {max_screenshots}\n".encode())

        # 启动读取线程
        reader_thread = threading.Thread(target=read_serial, args=(ser, log_file))
        reader_thread.daemon = True
        reader_thread.start()

        # 等待设备启动
        print("Waiting 5s for device to initialize...")
        time.sleep(5)

        # 处理命令行参数
        if args.click is not None:
            # 发送CLICK命令
            print(f"Sending CLICK command: index={args.click}")
            send_command(ser, f"CLICK:{args.click}")
            time.sleep(2)
            
            # 发送截图请求
            print("Sending snapshot request...")
            snapshot_frame = pack_frame(SNAPSHOT_CMD_SNAPSHOT_REQ)
            ser.write(snapshot_frame)
            
            # 等待响应
            print("Waiting for response (30s timeout)...")
            time.sleep(30)
            
            print("Done")
            return
        
        if args.snap:
            # 发送SNAP命令
            print("Sending SNAP command...")
            send_command(ser, "SNAP")
            time.sleep(10)
            
            # 发送截图请求
            print("Sending snapshot request...")
            snapshot_frame = pack_frame(SNAPSHOT_CMD_SNAPSHOT_REQ)
            ser.write(snapshot_frame)
            
            # 等待响应
            print("Waiting for response (30s timeout)...")
            time.sleep(30)
            
            print("Done")
            return
        
        # 发送PING测试连接
        print("Sending PING...")
        ping_frame = pack_frame(SNAPSHOT_CMD_PING)
        ser.write(ping_frame)

        time.sleep(2)

        # 连续发送截图请求，直到达到最大次数
        screenshot_count = 0
        while screenshot_count < max_screenshots:
            print(f"Sending snapshot request #{screenshot_count + 1}...")
            snapshot_frame = pack_frame(SNAPSHOT_CMD_SNAPSHOT_REQ)
            ser.write(snapshot_frame)
            
            # 等待响应（5秒间隔）
            time.sleep(5)
            
            screenshot_count += 1
            print(f"Screenshot #{screenshot_count} completed")

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