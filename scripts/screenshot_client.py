#!/usr/bin/env python3
# screenshot_client.py — ESP32 串口截图客户端
#
# 通过 USB-UART 模块连接到 ESP32 的 GPIO17/18，获取屏幕截图并保存。
#
# 硬件连接:
#   ESP32 GPIO17 (TX) ──── RX ──── CH340/FTDI USB-UART ──── Host USB
#   ESP32 GPIO18 (RX) ──── TX ──── CH340/FTDI USB-UART ──── Host USB
#   GND               ──── GND ──── CH340/FTDI USB-UART ──── Host USB
#
# 波特率: 115200
#
# 使用方法:
#   python3 screenshot_client.py --port /dev/cu.usbserial-xxx --output ./screenshots
#   python3 screenshot_client.py --port /dev/cu.usbserial-xxx --output ./screenshots --loop 5
#     (--loop N: 每隔 N 秒自动截图一次，共截图 N+1 次)
#   python3 screenshot_client.py --port /dev/cu.usbserial-xxx --output ./screenshots --watch
#     (--watch: 持续监控，每 3 秒截一张)
#   python3 screenshot_client.py --port /dev/cu.usbserial-xxx --output ./screenshots --count 10
#     (--count N: 连续截 N 张后退出)
#
# 依赖:
#   pip install pyserial

import argparse
import base64
import os
import struct
import sys
import time
from datetime import datetime

try:
    import serial
except ImportError:
    print("Error: pyserial not installed.")
    print("  Install with: pip install pyserial")
    sys.exit(1)

# ---------------------------------------------------------------------------
# 配置
# ---------------------------------------------------------------------------
BAUDRATE      = 115200
FRAME_HEADER1 = 0xAA
FRAME_HEADER2 = 0xBB
TIMEOUT_SEC   = 10       # 等待帧开始超时
CHUNK_READ_SIZE = 4096   # 每次读取的最大字节数
MAX_DATA_SIZE = 4096     # 最大数据包大小

# 命令定义
SNAPSHOT_CMD_SNAPSHOT_REQ = 0x01
SNAPSHOT_CMD_SNAPSHOT_DATA = 0x02
SNAPSHOT_CMD_ACK = 0x03
SNAPSHOT_CMD_NACK = 0x04
SNAPSHOT_CMD_PING = 0x05
SNAPSHOT_CMD_PONG = 0x06

# ---------------------------------------------------------------------------
# CRC16-CCITT 计算
# ---------------------------------------------------------------------------
def crc16_ccitt(data):
    crc = 0xFFFF
    poly = 0x1021
    for byte in data:
        crc ^= (byte << 8)
        for _ in range(8):
            crc = (crc << 1) ^ poly if (crc & 0x8000) else (crc << 1)
    return crc & 0xFFFF

# ---------------------------------------------------------------------------
# 帧解析器
# ---------------------------------------------------------------------------
class FrameParser:
    """从串口字节流中解析 Snapshot 帧"""

    def __init__(self):
        self.state = 'wait_header1'  # 状态机状态
        self.header_buf = b''
        self.cmd = 0
        self.payload_size = 0
        self.payload = b''
        self.crc_received = 0
        self.frame_start_time = 0

    def feed(self, data: bytes):
        """
        向解析器喂入数据，返回:
          - None: 帧未完成，继续喂
          - (cmd: int, payload: bytes): 完整帧的命令和数据
          - 'timeout': 接收超时，需要重新同步
          - 'error': 解析错误
        """
        for byte in data:
            if self.state == 'wait_header1':
                if byte == FRAME_HEADER1:
                    self.header_buf = bytes([byte])
                    self.state = 'wait_header2'

            elif self.state == 'wait_header2':
                self.header_buf += bytes([byte])
                if byte == FRAME_HEADER2:
                    self.state = 'read_cmd'
                    self.frame_start_time = time.time()
                else:
                    # 不是有效的 header，重新同步
                    self.state = 'wait_header1'
                    self.header_buf = b''

            elif self.state == 'read_cmd':
                self.cmd = byte
                self.state = 'read_len_high'

            elif self.state == 'read_len_high':
                self.payload_size = byte << 8
                self.state = 'read_len_low'

            elif self.state == 'read_len_low':
                self.payload_size |= byte
                if self.payload_size > MAX_DATA_SIZE:
                    print(f"[WARN] Suspicious payload size: {self.payload_size}, resetting")
                    self._reset()
                    return 'error'
                self.payload = b''
                if self.payload_size == 0:
                    self.state = 'read_crc_high'
                else:
                    self.state = 'read_payload'

            elif self.state == 'read_payload':
                self.payload += bytes([byte])
                if len(self.payload) >= self.payload_size:
                    self.payload = self.payload[:self.payload_size]
                    self.state = 'read_crc_high'

            elif self.state == 'read_crc_high':
                self.crc_received = byte << 8
                self.state = 'read_crc_low'

            elif self.state == 'read_crc_low':
                self.crc_received |= byte
                
                # 验证CRC
                crc_data = bytes([self.cmd]) + struct.pack('>H', self.payload_size) + self.payload
                crc_calculated = crc16_ccitt(crc_data)
                
                if self.crc_received == crc_calculated:
                    # 完整帧！返回命令和数据
                    result = (self.cmd, self.payload)
                    self._reset()
                    return result
                else:
                    print(f"[WARN] CRC mismatch: received 0x{self.crc_received:04X}, calculated 0x{crc_calculated:04X}")
                    self._reset()
                    return 'error'

        # 检查超时
        if time.time() - self.frame_start_time > TIMEOUT_SEC and self.state != 'wait_header1':
            print(f"[WARN] Frame receive timeout, resetting")
            self._reset()
            return 'timeout'

        return None

    def _reset(self):
        self.state = 'wait_header1'
        self.header_buf = b''
        self.cmd = 0
        self.payload_size = 0
        self.payload = b''
        self.crc_received = 0

# ---------------------------------------------------------------------------
# 打包请求帧
# ---------------------------------------------------------------------------
def pack_request_frame(cmd, data=b''):
    """打包请求帧"""
    data_len = len(data)
    crc_data = bytes([cmd]) + struct.pack('>H', data_len) + data
    crc = crc16_ccitt(crc_data)
    
    frame = bytes([FRAME_HEADER1, FRAME_HEADER2])
    frame += bytes([cmd])
    frame += struct.pack('>H', data_len)
    frame += data
    frame += struct.pack('>H', crc)
    
    return frame

# ---------------------------------------------------------------------------
# 截图客户端
# ---------------------------------------------------------------------------
def list_serial_ports():
    """列出所有可用的串口"""
    import serial.tools.list_ports
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        print("No serial ports found.")
        return []
    for p in ports:
        print(f"  {p.device}  {p.description}")
    return [p.device for p in ports]


def wait_for_frame(ser, parser, timeout=10):
    """发送截图请求命令，等待并返回帧数据"""
    # 发送截图命令
    req_frame = pack_request_frame(SNAPSHOT_CMD_SNAPSHOT_REQ)
    ser.write(req_frame)
    ser.flush()
    print(f"[INFO] Snapshot request sent at {datetime.now().strftime('%H:%M:%S.%f')[:-3]}")

    # 接收所有数据帧（可能分多个包）
    all_data = b''
    start = time.time()
    
    while time.time() - start < timeout:
        data = ser.read(CHUNK_READ_SIZE)
        if data:
            result = parser.feed(data)
            while result is not None:
                if isinstance(result, tuple):
                    cmd, payload = result
                    print(f"[INFO] Received frame: cmd=0x{cmd:02X}, length={len(payload)}")
                    
                    if cmd == SNAPSHOT_CMD_SNAPSHOT_DATA:
                        all_data += payload
                    elif cmd == SNAPSHOT_CMD_ACK:
                        error_code = payload[0] if len(payload) > 0 else 0
                        if error_code == 0:
                            print("[INFO] ACK received, transfer complete")
                            return all_data if all_data else None
                        else:
                            print(f"[ERROR] NACK received with error code: {error_code}")
                            return None
                    elif cmd == SNAPSHOT_CMD_NACK:
                        print("[ERROR] NACK received")
                        return None
                else:
                    print(f"[INFO] Parser result: {result}, re-syncing...")
                    parser._reset()
        time.sleep(0.01)

    print(f"[ERROR] Timeout waiting for frame ({timeout}s)")
    return None


def decode_and_save(b64_data: bytes, output_dir: str, seq: int = 0) -> str | None:
    """Base64 解码并保存为 JPEG"""
    try:
        # 去除可能的换行符
        b64_clean = b64_data.strip()
        raw = base64.b64decode(b64_clean)

        timestamp = datetime.now().strftime('%Y%m%d_%H%M%S_%f')[:-3]
        filename = f"screenshot_{timestamp}_#{seq:03d}.jpg"
        filepath = os.path.join(output_dir, filename)

        with open(filepath, 'wb') as f:
            f.write(raw)

        print(f"[OK] Saved: {filepath} ({len(raw):,} bytes)")
        return filepath

    except base64.binascii.Error as e:
        print(f"[ERROR] Base64 decode error: {e}")
        print(f"  Received {len(b64_data)} bytes, first 64: {b64_data[:64]}")
        return None
    except Exception as e:
        print(f"[ERROR] Save error: {e}")
        return None


def capture_once(ser, parser, output_dir: str, seq: int = 0) -> bool:
    """执行一次截图"""
    print("-" * 60)
    print(f"[{datetime.now().strftime('%H:%M:%S')}] Capturing screenshot #{seq} ...")

    b64_data = wait_for_frame(ser, parser)
    if b64_data is None:
        print("[ERROR] Failed to capture frame")
        return False

    filepath = decode_and_save(b64_data, output_dir, seq)
    return filepath is not None


# ---------------------------------------------------------------------------
# 主函数
# ---------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(
        description='ESP32 Screen Capture Client via UART',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
Examples:
  %(prog)s --port /dev/cu.usbserial-001 --output ./shots
  %(prog)s --port /dev/cu.usbserial-001 --output ./shots --loop 5
  %(prog)s --port /dev/cu.usbserial-001 --output ./shots --count 20
  %(prog)s --port /dev/cu.usbserial-001 --output ./shots --watch
  %(prog)s --list
'''
    )

    parser.add_argument(
        '--port', '-p',
        type=str,
        default=None,
        help='Serial port device (e.g. /dev/cu.usbserial-xxx on macOS, '
             '/dev/ttyUSB0 on Linux, COM3 on Windows)'
    )
    parser.add_argument(
        '--output', '-o',
        type=str,
        default='./screenshots',
        help='Output directory for JPEG files (default: ./screenshots)'
    )
    parser.add_argument(
        '--baud',
        type=int,
        default=BAUDRATE,
        help=f'Baud rate (default: {BAUDRATE})'
    )
    parser.add_argument(
        '--loop', '-l',
        type=int,
        default=0,
        metavar='SECONDS',
        help='Interval in seconds between screenshots (0=once, use with --count)'
    )
    parser.add_argument(
        '--count', '-c',
        type=int,
        default=1,
        metavar='N',
        help='Number of screenshots to capture (default: 1)'
    )
    parser.add_argument(
        '--watch',
        action='store_true',
        help='Continuous mode: capture every 3 seconds until Ctrl+C'
    )
    parser.add_argument(
        '--list', '-L',
        action='store_true',
        help='List available serial ports and exit'
    )
    parser.add_argument(
        '--timeout', '-t',
        type=int,
        default=TIMEOUT_SEC,
        metavar='SEC',
        help=f'Frame receive timeout in seconds (default: {TIMEOUT_SEC})'
    )

    args = parser.parse_args()

    # 列出端口
    if args.list:
        print("Available serial ports:")
        list_serial_ports()
        return 0

    if args.port is None:
        print("Error: --port is required (or use --list to see available ports)")
        print("Example: python3 screenshot_client.py --port /dev/cu.usbserial-001 -o ./screenshots")
        return 1

    # 创建输出目录
    os.makedirs(args.output, exist_ok=True)
    print(f"Output directory: {os.path.abspath(args.output)}")

    # 打开串口
    try:
        ser = serial.Serial(
            port=args.port,
            baudrate=args.baud,
            timeout=1,
            write_timeout=5,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
        )
        print(f"[INFO] Opened {args.port} at {args.baud} baud")
        # 清空接收缓冲区
        ser.reset_input_buffer()
        ser.reset_output_buffer()
        time.sleep(0.2)  # 等待 ESP32 UART 稳定
    except serial.SerialException as e:
        print(f"[ERROR] Cannot open {args.port}: {e}")
        return 1

    parser = FrameParser()
    success_count = 0
    fail_count = 0

    try:
        if args.watch:
            print("[INFO] Watch mode: Ctrl+C to stop")
            seq = 0
            while True:
                if capture_once(ser, parser, args.output, seq):
                    success_count += 1
                else:
                    fail_count += 1
                seq += 1
                time.sleep(3)

        else:
            total = args.count
            for i in range(total):
                interval = args.loop if args.loop > 0 else 0
                if capture_once(ser, parser, args.output, i):
                    success_count += 1
                else:
                    fail_count += 1

                if interval > 0 and i < total - 1:
                    print(f"[INFO] Waiting {interval}s before next capture...")
                    time.sleep(interval)

    except KeyboardInterrupt:
        print("\n[INFO] Interrupted by user")

    finally:
        ser.close()
        print("-" * 60)
        print(f"Summary: {success_count} saved, {fail_count} failed")

    return 0 if fail_count == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
