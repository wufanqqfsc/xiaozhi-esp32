from PIL import Image
import math

img = Image.open('screenshots/screenshot.jpg')
w, h = img.size

# 寻找矩形的精确边界
# 扫描每一行, 找非黑段
print('=' * 60)
print('矩形边框精确定位 (扫描每行非黑段)')
print('=' * 60)

# 收集所有非黑像素的坐标
print('\n--- 扫描 y=100~260 范围 (太极图上下边界) ---')
for y in [98, 99, 100, 101, 102, 110, 120, 130, 150, 170, 180, 200, 230, 250, 255, 256, 257, 258, 259, 260, 261]:
    non_black_x = []
    for x in range(w):
        px = img.getpixel((x, y))
        r_c, g, b = px[:3]
        if not (r_c < 30 and g < 30 and b < 30):
            non_black_x.append(x)
    if non_black_x:
        # 找连续段
        segments = []
        start = non_black_x[0]
        prev = start
        for x in non_black_x[1:]:
            if x - prev > 3:
                segments.append((start, prev))
                start = x
            prev = x
        segments.append((start, prev))
        seg_str = ', '.join([f'{s}-{e}({e-s+1}px)' for s, e in segments])
        print(f'y={y:3d}: {len(segments)} 段 - {seg_str}')

# 找矩形的角点 - 检查矩形 4 条边
print('\n\n--- 找矩形 4 条边 ---')
# 顶边: 扫描 y=98~103
print('\n顶边 (扫描 y=95~105):')
for y in range(95, 110):
    counts = 0
    for x in range(50, 320):
        px = img.getpixel((x, y))
        r_c, g, b = px[:3]
        if not (r_c < 30 and g < 30 and b < 30):
            counts += 1
    if counts > 0:
        print(f'  y={y:3d}: {counts} 个非黑像素')

# 底边
print('\n底边 (扫描 y=255~270):')
for y in range(255, 270):
    counts = 0
    for x in range(50, 320):
        px = img.getpixel((x, y))
        r_c, g, b = px[:3]
        if not (r_c < 30 and g < 30 and b < 30):
            counts += 1
    if counts > 0:
        print(f'  y={y:3d}: {counts} 个非黑像素')

# 左边
print('\n左边 (扫描 x=95~110):')
for x in range(95, 115):
    counts = 0
    for y in range(50, 320):
        px = img.getpixel((x, y))
        r_c, g, b = px[:3]
        if not (r_c < 30 and g < 30 and b < 30):
            counts += 1
    if counts > 0:
        print(f'  x={x:3d}: {counts} 个非黑像素')

# 右边
print('\n右边 (扫描 x=250~270):')
for x in range(250, 270):
    counts = 0
    for y in range(50, 320):
        px = img.getpixel((x, y))
        r_c, g, b = px[:3]
        if not (r_c < 30 and g < 30 and b < 30):
            counts += 1
    if counts > 0:
        print(f'  x={x:3d}: {counts} 个非黑像素')
