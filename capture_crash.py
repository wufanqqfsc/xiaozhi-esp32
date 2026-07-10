import serial
import time
import sys

def main():
    try:
        ser = serial.Serial('COM9', 115200, timeout=1)
        print('Connected to COM9. Waiting for crash... Please reproduce the issue now.')
        end_time = time.time() + 120
        with open('crash_log.txt', 'w', encoding='utf-8') as f:
            while time.time() < end_time:
                line = ser.readline()
                if line:
                    text = line.decode('utf-8', errors='replace')
                    sys.stdout.write(text)
                    f.write(text)
        ser.close()
        print('Finished reading.')
    except Exception as e:
        print(f'Failed to open COM9: {e}')

if __name__ == '__main__':
    main()
