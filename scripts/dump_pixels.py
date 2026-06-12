from PIL import Image
import math

img = Image.open('screenshots/screenshot.jpg')
w, h = img.size
cx, cy = w // 2, h // 2

# 在 (cx, cy) 周围画 9x9 网格，输出每个像素的字符表示
print('中心 161x161 区域字符图 (B=黑 W=白 .=灰):')
for y in range(cy - 80, cy + 81, 8):
    line = ''
    for x in range(cx - 80, cx + 81, 4):
        px = img.getpixel((x, y))
        r, g, b = px[:3]
        if r < 30 and g < 30 and b < 30:
            line += 'B'
        elif r > 200 and g > 200 and b > 200:
            line += 'W'
        elif abs(r - 0xD4) < 30 and abs(g - 0xAF) < 30 and abs(b - 0x37) < 30:
            line += 'G'
        else:
            line += '.'
    print(line)
