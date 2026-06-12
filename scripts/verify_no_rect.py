from PIL import Image
import math

img = Image.open('screenshots/screenshot.jpg')
w, h = img.size
cx, cy = w // 2, h // 2

# 验证之前的"矩形边框"位置 (y=100~110, y=250~260, x=100~110, x=250~260)
# 现在应该是黑的

# 检查之前的水平"线段"位置
print('=' * 60)
print('矩形边框修复验证')
print('=' * 60)

# 1. 水平中线 y=180 检查 (之前的 x=100~179 是阳鱼右半)
print('\n水平 y=180 (中心水平线) 非黑段:')
non_black_x = []
for x in range(w):
    px = img.getpixel((x, 180))
    r_c, g, b = px[:3]
    if not (r_c < 30 and g < 30 and b < 30):
        non_black_x.append(x)

if non_black_x:
    segments = []
    start = non_black_x[0]
    prev = start
    for x in non_black_x[1:]:
        if x - prev > 3:
            segments.append((start, prev))
            start = x
        prev = x
    segments.append((start, prev))
    print(f'  共 {len(segments)} 段:')
    for s, e in segments:
        print(f'    x={s:3d}~{e:3d} (宽 {e-s+1}px)')

# 2. 之前的"矩形顶边" y=100 位置
print('\n水平 y=100 (原矩形顶边位置) 非黑段:')
non_black_x = []
for x in range(w):
    px = img.getpixel((x, 100))
    r_c, g, b = px[:3]
    if not (r_c < 30 and g < 30 and b < 30):
        non_black_x.append(x)

if non_black_x:
    segments = []
    start = non_black_x[0]
    prev = start
    for x in non_black_x[1:]:
        if x - prev > 3:
            segments.append((start, prev))
            start = x
        prev = x
    segments.append((start, prev))
    print(f'  共 {len(segments)} 段:')
    for s, e in segments:
        print(f'    x={s:3d}~{e:3d} (宽 {e-s+1}px)')
else:
    print('  ✅ y=100 整行全黑!')

# 3. 之前的"矩形底边" y=260 位置
print('\n水平 y=260 (原矩形底边位置) 非黑段:')
non_black_x = []
for x in range(w):
    px = img.getpixel((x, 260))
    r_c, g, b = px[:3]
    if not (r_c < 30 and g < 30 and b < 30):
        non_black_x.append(x)

if non_black_x:
    segments = []
    start = non_black_x[0]
    prev = start
    for x in non_black_x[1:]:
        if x - prev > 3:
            segments.append((start, prev))
            start = x
        prev = x
    segments.append((start, prev))
    print(f'  共 {len(segments)} 段:')
    for s, e in segments:
        print(f'    x={s:3d}~{e:3d} (宽 {e-s+1}px)')
else:
    print('  ✅ y=260 整行全黑!')

# 4. 之前的"矩形左边" x=100 位置
print('\n竖直 x=100 (原矩形左边位置) 非黑段:')
non_black_y = []
for y in range(h):
    px = img.getpixel((100, y))
    r_c, g, b = px[:3]
    if not (r_c < 30 and g < 30 and b < 30):
        non_black_y.append(y)

if non_black_y:
    segments = []
    start = non_black_y[0]
    prev = start
    for y in non_black_y[1:]:
        if y - prev > 3:
            segments.append((start, prev))
            start = y
        prev = y
    segments.append((start, prev))
    print(f'  共 {len(segments)} 段:')
    for s, e in segments:
        print(f'    y={s:3d}~{e:3d} (高 {e-s+1}px)')
else:
    print('  ✅ x=100 整列全黑!')

# 5. 之前的"矩形右边" x=260 位置
print('\n竖直 x=260 (原矩形右边位置) 非黑段:')
non_black_y = []
for y in range(h):
    px = img.getpixel((260, y))
    r_c, g, b = px[:3]
    if not (r_c < 30 and g < 30 and b < 30):
        non_black_y.append(y)

if non_black_y:
    segments = []
    start = non_black_y[0]
    prev = start
    for y in non_black_y[1:]:
        if y - prev > 3:
            segments.append((start, prev))
            start = y
        prev = y
    segments.append((start, prev))
    print(f'  共 {len(segments)} 段:')
    for s, e in segments:
        print(f'    y={s:3d}~{e:3d} (高 {e-s+1}px)')
else:
    print('  ✅ x=260 整列全黑!')

# 鎏金外圈验证 - 在 (cx, cy + 80) 应该看到鎏金
print('\n\n=== 鎏金外圈 (r=80) 验证 ===')
for angle in [0, 45, 90, 135, 180, 225, 270, 315]:
    rad = math.radians(angle)
    x = int(cx + 80 * math.cos(rad))
    y = int(cy + 80 * math.sin(rad))
    if 0 <= x < w and 0 <= y < h:
        px = img.getpixel((x, y))
        r_c, g, b = px[:3]
        is_gold = abs(r_c - 0xD4) < 40 and abs(g - 0xAF) < 40 and abs(b - 0x37) < 40
        marker = 'G' if is_gold else ('W' if r_c > 200 and g > 200 and b > 200 else 'B')
        print(f'  {angle:3d}° ({x:3d},{y:3d}): #{r_c:02X}{g:02X}{b:02X} {marker}')
