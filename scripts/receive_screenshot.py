#!/usr/bin/env python3
"""接收串口截图数据"""
import serial
import time
import base64
import sys
import os
import re

def receive_screenshot(port, timeout=30, output_dir='screenshots'):
    ser = serial.Serial(port, 115200, timeout=timeout)
    ser.reset_input_buffer()
    
    print(f'等待截图数据 ({timeout}秒)...')
    time.sleep(timeout)
    
    data = ser.read_all()
    ser.close()
    
    text = data.decode('utf-8', errors='replace')
    
    # 方法1: 查找带标记的数据
    start = text.find('===SCREENSHOT_START===')
    end = text.find('===SCREENSHOT_END===')
    
    if start != -1 and end != -1:
        content = text[start+len('===SCREENSHOT_START==='):end].strip()
        print(f'方法1成功: 找到带标记的 Base64 数据，长度: {len(content)}')
    else:
        # 方法2: 查找原始 Base64 数据 (JPEG 文件头 /9j/ 开头)
        match = re.search(r'/9j/([A-Za-z0-9+/=]+)', text)
        if match:
            content = '9j/' + match.group(1)
            # 找到数据的结束位置
            end_match = re.search(r'([A-Za-z0-9+/]{4})[^A-Za-z0-9+/]', content)
            if end_match:
                content = content[:end_match.end()-1]
            print(f'方法2成功: 找到原始 Base64 数据，长度: {len(content)}')
        else:
            print('错误: 未找到截图数据')
            print('最后500字符:')
            print(text[-500:])
            return False
    
    # 解码
    try:
        padding = (4 - len(content) % 4) % 4
        content += '=' * padding
        jpg = base64.b64decode(content)
        print(f'JPEG 大小: {len(jpg)} 字节')
        
        # 验证 JPEG 文件头
        if jpg[:2] == b'\xff\xd8':
            print('文件验证: 是有效的 JPEG 文件')
        else:
            print('文件验证: 可能不是有效的 JPEG 文件')
        
        # 保存
        os.makedirs(output_dir, exist_ok=True)
        
        # 生成带时间戳的文件名
        from datetime import datetime
        timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
        filepath = os.path.join(output_dir, f'screenshot_{timestamp}.jpg')
        
        with open(filepath, 'wb') as f:
            f.write(jpg)
        print(f'已保存到: {filepath}')
        return True
    except Exception as e:
        print(f'解码失败: {e}')
        return False

if __name__ == '__main__':
    port = sys.argv[1] if len(sys.argv) > 1 else '/dev/cu.usbmodem101'
    receive_screenshot(port)
