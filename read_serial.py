import serial
import sys
import time

try:
    ser = serial.Serial('COM9', 115200, timeout=1)
    print("Connected to COM9")
    end_time = time.time() + 15
    while time.time() < end_time:
        line = ser.readline()
        if line:
            try:
                sys.stdout.write(line.decode('utf-8', errors='replace'))
            except Exception as e:
                print(f"Error decoding: {e}")
    ser.close()
    print("Finished reading.")
except Exception as e:
    print(f"Failed to open COM9: {e}")
