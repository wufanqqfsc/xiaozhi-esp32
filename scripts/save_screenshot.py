#!/usr/bin/env python3
import serial
import time
import base64
import re

print('等待设备启动和截图...')

ser = serial.Serial('/dev/cu.usbmodem101', 115200, timeout=10)
ser.reset_input_buffer()

time.sleep(12)

data = ser.read(200000)
ser.close()

text = data.decode('utf-8', errors='replace')

match = re.search(r'/9j/([A-Za-z0-9+/=]+)', text)
if match:
    base64_data = '9j/' + match.group(1)
    
    end_idx = text.find('===SCREENSHOT_END===', text.find('/9j/'))
    if end_idx > 0:
        start_idx = text.find('/9j/')
        base64_data = text[start_idx:end_idx]
    else:
        base64_data = text[text.find('/9j/'):text.find('/9j/')+17060]
    
    print(f'提取到 Base64 数据，长度: {len(base64_data)}')
    
    try:
        padding = (4 - len(base64_data) % 4) % 4
        base64_data += '=' * padding
        jpg = base64.b64decode(base64_data)
        print(f'JPEG 大小: {len(jpg)} 字节')
        
        if jpg[:2] == b'\xff\xd8':
            with open('screenshots/screenshot.jpg', 'wb') as f:
                f.write(jpg)
            print('截图已保存到: screenshots/screenshot.jpg')
        else:
            print('文件不是有效的 JPEG')
    except Exception as e:
        print(f'解码失败: {e}')
else:
    print('未找到 JPEG 数据')
