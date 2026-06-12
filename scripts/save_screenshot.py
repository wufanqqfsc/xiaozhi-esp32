#!/usr/bin/env python3
"""
ESP32 截图接收脚本
- 监听 USB-Serial/JTAG 端口，等待截图数据
- 10秒超时自动退出，保存日志到本地用于诊断
- 完整截图保存到 screenshots/screenshot.jpg
"""
import serial
import time
import base64
import os
import sys
import shutil
from datetime import datetime

# ====================== 可配置参数 ======================
SERIAL_PORT = '/dev/cu.usbmodem1101'
BAUDRATE = 115200
TIMEOUT_SECONDS = 10  # 超过10秒未完成则主动退出
LOG_DIR = 'screenshots/logs'
SCREENSHOT_DIR = 'screenshots'
HISTORY_DIR = 'screenshots/history'

# ====================== 准备目录 ======================
os.makedirs(SCREENSHOT_DIR, exist_ok=True)
os.makedirs(HISTORY_DIR, exist_ok=True)
os.makedirs(LOG_DIR, exist_ok=True)

# 生成本次运行的日志文件名
run_timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
log_path = f'{LOG_DIR}/run_{run_timestamp}.log'
raw_log_path = f'{LOG_DIR}/raw_{run_timestamp}.log'

# 备份旧截图
screenshot_path = f'{SCREENSHOT_DIR}/screenshot.jpg'
if os.path.exists(screenshot_path):
    backup_path = f'{HISTORY_DIR}/screenshot_{run_timestamp}.jpg'
    shutil.copy2(screenshot_path, backup_path)
    os.remove(screenshot_path)

print('=' * 60)
print('ESP32 截图接收工具 (v2.0 - 带超时诊断)')
print('=' * 60)
print(f'串口:        {SERIAL_PORT}')
print(f'波特率:      {BAUDRATE}')
print(f'超时时长:    {TIMEOUT_SECONDS}秒')
print(f'日志保存到:  {log_path}')
print('=' * 60)

# ====================== 打开串口 ======================
try:
    ser = serial.Serial(SERIAL_PORT, BAUDRATE, timeout=2)
    ser.reset_input_buffer()
    print(f'\n[✓] 串口已打开: {SERIAL_PORT}')
except serial.SerialException as e:
    print(f'\n[✗] 串口打开失败: {e}')
    print('\n可能的解决方法:')
    print('  1. 确认设备已通过 USB 连接')
    print('  2. 关闭其他占用串口的程序 (如 idf.py monitor)')
    print(f'  3. 执行: ls /dev/cu.usbmodem* 查看可用设备')
    sys.exit(1)

# ====================== 接收数据 ======================
print(f'\n[*] 等待截图数据 (最多 {TIMEOUT_SECONDS} 秒)...\n')

start_marker = '===SCREENSHOT_START==='
end_marker = '===SCREENSHOT_END==='
found_start = False
found_end = False
buffer = ''
raw_data = b''
start_time = time.time()
last_data_time = start_time
last_heartbeat = start_time
HEARTBEAT_INTERVAL = 3  # 每3秒打印一次心跳

try:
    while not found_end:
        elapsed = time.time() - start_time

        # 检查总超时
        if elapsed > TIMEOUT_SECONDS:
            print(f'\n[!] 超过 {TIMEOUT_SECONDS} 秒未完成，主动退出')
            break

        # 读取数据
        try:
            data = ser.read(10000)
        except serial.SerialException as e:
            print(f'\n[✗] 串口读取错误: {e}')
            break

        if data:
            raw_data += data
            decoded = data.decode('utf-8', errors='replace')
            buffer += decoded
            last_data_time = time.time()

            # 检查开始标记
            if start_marker in buffer and not found_start:
                found_start = True
                # 打印找到开始标记前的日志（用于诊断启动情况）
                pre_log = buffer[:buffer.find(start_marker)]
                if pre_log.strip():
                    print('[启动日志]')
                    print(pre_log.strip())
                    print('---')
                print(f'[✓] 找到截图开始标记 (已等待 {elapsed:.1f}秒)')

            # 检查结束标记 - 只在第一次完整传输后退出
            if end_marker in buffer and found_start:
                # 找到当前截图的结束标记位置（第一个）
                first_end_idx = buffer.find(end_marker)
                # 确认开始标记在结束标记之前
                first_start_idx = buffer.find(start_marker)
                if first_start_idx < first_end_idx:
                    found_end = True
                    print(f'[✓] 找到截图结束标记 (总耗时 {elapsed:.1f}秒)')
                    # 截断 buffer 到第一个完整的截图数据
                    buffer = buffer[:first_end_idx + len(end_marker)]
                    break  # 立即退出接收循环

        # 心跳输出（用于显示脚本还活着，且能看到实时日志）
        elif time.time() - last_heartbeat > HEARTBEAT_INTERVAL:
            elapsed = time.time() - start_time
            print(f'[*] 等待中... 已用 {elapsed:.1f}/{TIMEOUT_SECONDS}秒, 已接收 {len(buffer)} 字符')
            last_heartbeat = time.time()

except KeyboardInterrupt:
    print('\n[!] 用户中断 (Ctrl+C)')

# 关闭串口
try:
    ser.close()
    print('[✓] 串口已关闭')
except:
    pass

# ====================== 保存诊断日志 ======================
print(f'\n[*] 保存诊断日志...')

# 保存原始日志（包含所有设备输出）
with open(raw_log_path, 'w', encoding='utf-8') as f:
    f.write(buffer)
print(f'[✓] 原始日志:   {raw_log_path} ({len(buffer)} 字符)')

# 提取关键错误信息
error_lines = []
for line in buffer.split('\n'):
    line_lower = line.lower()
    if any(kw in line_lower for kw in [
        'error', 'fail', 'abort', 'crash', 'assert',
        'gbort', 'panic', 'tlsf', 'heap',
        '错误', '失败', '崩溃', '断言'
    ]):
        error_lines.append(line)

if error_lines:
    with open(log_path, 'w', encoding='utf-8') as f:
        f.write(f'=== 关键错误日志 ===\n')
        f.write(f'运行时间: {datetime.now().strftime("%Y-%m-%d %H:%M:%S")}\n')
        f.write(f'耗时:     {time.time() - start_time:.1f} 秒\n')
        f.write(f'接收:     {len(buffer)} 字符\n')
        f.write(f'状态:     {"完成" if found_end else "超时/失败"}\n')
        f.write('=' * 60 + '\n\n')
        for line in error_lines:
            f.write(line + '\n')
    print(f'[✓] 错误摘要:   {log_path} ({len(error_lines)} 条)')

# 打印最后的设备输出（最后20行）作为实时诊断
print('\n' + '=' * 60)
print('设备最后输出 (用于诊断):')
print('=' * 60)
last_lines = buffer.strip().split('\n')[-20:]
for line in last_lines:
    print(line)
print('=' * 60)

# ====================== 处理截图数据 ======================
print()

if found_start and found_end:
    # 提取 Base64 数据
    start_idx = buffer.find(start_marker) + len(start_marker)
    end_idx = buffer.find(end_marker)

    base64_data = buffer[start_idx:end_idx].strip()
    print(f'[*] 提取到 Base64 数据，长度: {len(base64_data)}')

    try:
        padding = (4 - len(base64_data) % 4) % 4
        base64_data += '=' * padding
        jpg = base64.b64decode(base64_data)
        print(f'[*] JPEG 解码后大小: {len(jpg)} 字节')

        if jpg[:2] == b'\xff\xd8':
            with open(screenshot_path, 'wb') as f:
                f.write(jpg)
            print(f'\n[✓] 截图已保存: {screenshot_path}')
            print(f'    文件大小: {len(jpg)} 字节')
        else:
            print(f'\n[✗] 数据不是有效的 JPEG (文件头: {jpg[:4].hex()})')
            print(f'    完整 Base64 数据已保存到: {raw_log_path}')
    except Exception as e:
        print(f'\n[✗] 解码失败: {e}')
elif found_start and not found_end:
    print('[✗] 收到开始标记但未收到结束标记 - 传输被中断')
    print(f'    完整日志已保存到: {raw_log_path}')
    print(f'    可能原因: 设备在传输过程中崩溃或看门狗重启')
else:
    print(f'[✗] 未找到截图开始标记 (等待了 {time.time() - start_time:.1f} 秒)')
    print(f'    完整日志已保存到: {raw_log_path}')
    print(f'\n    常见原因:')
    print(f'    1. 设备未启动截图任务 (查看 main.cc 中 screenshot_task 是否注册)')
    print(f'    2. 设备启动后崩溃 (查看 {log_path} 中的错误信息)')
    print(f'    3. 串口连接断开 (重新插拔 USB)')

# ====================== 退出建议 ======================
if not found_end:
    print(f'\n' + '=' * 60)
    print('建议下一步操作:')
    print('=' * 60)
    print(f'  1. 查看错误摘要:  cat {log_path}')
    print(f'  2. 查看完整日志:  cat {raw_log_path}')
    print(f'  3. 重新烧录:     ./build_and_flash.sh flash')
    print(f'  4. 增加超时时间: 编辑本脚本的 TIMEOUT_SECONDS = 20')
    print('=' * 60)

print()
