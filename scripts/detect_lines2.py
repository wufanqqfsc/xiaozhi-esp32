from PIL import Image
import math

img = Image.open('screenshots/screenshot.jpg')
w, h = img.size
cx, cy = w // 2, h // 2

# 用户提到"边缘还有横线和竖线" - 检查太极图外圈
# 太极图外圈应该是 80px 半径的圆, 但创建时是 80 + 4 = 84 (外发光) + 2 = 86

# 详细分析从 80px 到 90px 之间的像素
print('=' * 60)
print('太极图外圈细节 (r=80~100) 完整圆周扫描')
print('=' * 60)

# 输出每个角度的非黑像素
for r in range(78, 100):
    non_black_pixels = []
    for angle in range(0, 360):
        rad = math.radians(angle)
        x = int(cx + r * math.cos(rad))
        y = int(cy + r * math.sin(rad))
        if 0 <= x < w and 0 <= y < h:
            px = img.getpixel((x, y))
            r_c, g, b = px[:3]
            if not (r_c < 30 and g < 30 and b < 30):  # 非纯黑
                non_black_pixels.append((angle, f'#{r_c:02X}{g:02X}{b:02X}'))
    if non_black_pixels:
        # 只显示前 10 个和后 10 个
        print(f'\nr={r:3d}: 非黑像素总数 {len(non_black_pixels)}')
        for angle, color in non_black_pixels[:8]:
            print(f'  角度 {angle:3d}°: {color}')
        if len(non_black_pixels) > 16:
            print(f'  ... (省略 {len(non_black_pixels) - 16} 个) ...')
        for angle, color in non_black_pixels[-8:]:
            print(f'  角度 {angle:3d}°: {color}')

# 找水平/竖直方向的延伸线
print('\n\n=== 找水平/竖直方向的线 ===')
# 水平线 y = 180 (中心水平线)
print('\n水平扫描 y=180 (整行):')
non_black_x = []
for x in range(w):
    px = img.getpixel((x, 180))
    r_c, g, b = px[:3]
    if not (r_c < 30 and g < 30 and b < 30):
        non_black_x.append((x, f'#{r_c:02X}{g:02X}{b:02X}'))

# 找连续的非黑段
segments = []
if non_black_x:
    start = non_black_x[0][0]
    prev = start
    for x, c in non_black_x[1:]:
        if x - prev > 3:  # 间隔 >3 视为断开
            segments.append((start, prev))
            start = x
        prev = x
    segments.append((start, prev))

print(f'水平中线非黑段 (共 {len(segments)} 段):')
for s, e in segments:
    print(f'  x={s:3d}~{e:3d} (宽 {e-s+1}px)')

# 竖直线 x = 180
print('\n竖直扫描 x=180 (整列):')
non_black_y = []
for y in range(h):
    px = img.getpixel((180, y))
    r_c, g, b = px[:3]
    if not (r_c < 30 and g < 30 and b < 30):
        non_black_y.append((y, f'#{r_c:02X}{g:02X}{b:02X}'))

segments = []
if non_black_y:
    start = non_black_y[0][0]
    prev = start
    for y, c in non_black_y[1:]:
        if y - prev > 3:
            segments.append((start, prev))
            start = y
        prev = y
    segments.append((start, prev))

print(f'竖直中线非黑段 (共 {len(segments)} 段):')
for s, e in segments:
    print(f'  y={s:3d}~{e:3d} (高 {e-s+1}px)')
