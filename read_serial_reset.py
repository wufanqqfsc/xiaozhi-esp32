import serial
import sys
import time
import os

try:
    ser = serial.Serial('COM9', 115200, timeout=1)
    print("Connected to COM9. Restarting device...")
    # Trigger a hard reset via DTR/RTS
    ser.setDTR(False)
    ser.setRTS(True)
    time.sleep(0.1)
    ser.setDTR(True)
    ser.setRTS(False)
    time.sleep(0.1)
    ser.setDTR(False)
    ser.setRTS(False)
    
    print("Reading logs for 25 seconds...")
    end_time = time.time() + 25
    with open('boot_log.txt', 'w', encoding='utf-8') as f:
        while time.time() < end_time:
            line = ser.readline()
            if line:
                try:
                    text = line.decode('utf-8', errors='replace')
                    sys.stdout.write(text)
                    f.write(text)
                except Exception as e:
                    pass
    ser.close()
    print("Finished reading.")
except Exception as e:
    print(f"Failed to open COM9: {e}")