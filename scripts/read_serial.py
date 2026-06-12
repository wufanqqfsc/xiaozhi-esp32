#!/usr/bin/env python3
# 读取串口日志
import serial
import time
import sys

def read_serial(port, baud=115200, duration=10):
    try:
        ser = serial.Serial(
            port=port,
            baudrate=baud,
            timeout=1,
            write_timeout=2,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
        )
        print(f"[INFO] Opened {port} at {baud} baud")
        print(f"[INFO] Reading for {duration} seconds...")
        print("-" * 60)
        
        start = time.time()
        while time.time() - start < duration:
            if ser.in_waiting > 0:
                data = ser.read(ser.in_waiting)
                try:
                    print(data.decode('utf-8', errors='replace'), end='')
                except:
                    print(repr(data))
            time.sleep(0.1)
        
        print("\n" + "-" * 60)
        ser.close()
        
    except serial.SerialException as e:
        print(f"[ERROR] Serial error: {e}")
        return False
    
    return True

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: python read_serial.py <serial_port> [duration]")
        sys.exit(1)
    
    port = sys.argv[1]
    duration = int(sys.argv[2]) if len(sys.argv) > 2 else 10
    read_serial(port, duration=duration)