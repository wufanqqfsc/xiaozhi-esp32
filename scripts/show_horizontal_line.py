from PIL import Image
import math

img = Image.open('screenshots/screenshot.jpg')
w, h = img.size
cx, cy = w // 2, h // 2

# 详细查看水平线段 x=100~179 在 y=180
# 它从哪开始? 到哪结束? 颜色如何变化?
print('水平线段详细分析 (y=180, x=100~180):')
for x in range(95, 185, 1):
    px = img.getpixel((x, 180))
    r_c, g, b = px[:3]
    color = f'#{r_c:02X}{g:02X}{b:02X}'
    if r_c < 30 and g < 30 and b < 30:
        marker = 'B'
    elif r_c > 200 and g > 200 and b > 200:
        marker = 'W'
    elif abs(r_c - 0xD4) < 30 and abs(g - 0xAF) < 30 and abs(b - 0x37) < 30:
        marker = 'G'
    else:
        marker = '.'
    print(f'  x={x:3d}: {color} {marker}')

# 现在检查所有非黑像素在 y=180 水平线上 (整行)
print('\n\n水平 y=180 完整扫描 (整行 360px):')
prev_color = None
runs = []
start = 0
for x in range(w):
    px = img.getpixel((x, 180))
    r_c, g, b = px[:3]
    if r_c < 30 and g < 30 and b < 30:
        cur = 'B'
    elif r_c > 200 and g > 200 and b > 200:
        cur = 'W'
    elif abs(r_c - 0xD4) < 30 and abs(g - 0xAF) < 30 and abs(b - 0x37) < 30:
        cur = 'G'
    else:
        cur = f'?'
    if cur != prev_color:
        if prev_color is not None:
            runs.append((start, x-1, prev_color))
        start = x
        prev_color = cur
runs.append((start, w-1, prev_color))

for s, e, c in runs:
    if c != 'B':
        print(f'  x={s:3d}~{e:3d} (宽 {e-s+1}px): {c}')
