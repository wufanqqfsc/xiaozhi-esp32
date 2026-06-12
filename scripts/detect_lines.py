from PIL import Image
import math

img = Image.open('screenshots/screenshot.jpg')
w, h = img.size
cx, cy = w // 2, h // 2

# 输出太极图边缘区域的细密字符图 (60x60 中心区域)
print('=' * 60)
print('阴阳鱼边缘细密字符图 (每2像素一个字符)')
print('=' * 60)

# 半径 80~100px 的范围
print('\n--- 太极图外圈 (r=78~85) 详细扫描 ---')
for r in [78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88]:
    line = f'r={r:3d}: '
    for angle in range(0, 360, 4):
        rad = math.radians(angle)
        x = int(cx + r * math.cos(rad))
        y = int(cy + r * math.sin(rad))
        if 0 <= x < w and 0 <= y < h:
            px = img.getpixel((x, y))
            r_c, g, b = px[:3]
            if r_c < 30 and g < 30 and b < 30:
                line += 'B'
            elif r_c > 200 and g > 200 and b > 200:
                line += 'W'
            elif abs(r_c - 0xD4) < 30 and abs(g - 0xAF) < 30 and abs(b - 0x37) < 30:
                line += 'G'
            elif 30 <= r_c < 100:
                line += 'g'  # 暗鎏金
            else:
                line += '.'
    print(line)

# 寻找水平横线和竖线
print('\n=== 检测水平/竖直方向的异常线 ===')

# 检查 y=cy (水平中线) 在太极图区域外的颜色
print('\n水平中线 (y=cy=180) 在 r=80~180 范围:')
for r in range(75, 185, 2):
    x = cx + r
    if 0 <= x < w:
        px = img.getpixel((x, 180))
        r_c, g, b = px[:3]
        marker = ''
        # 标记非背景的像素
        if not (r_c < 30 and g < 30 and b < 30):  # 非纯黑
            if r_c > 200 and g > 200 and b > 200:
                color = 'W'
            elif abs(r_c - 0xD4) < 30:
                color = 'G'
            else:
                color = f'?#{r_c:02X}{g:02X}{b:02X}'
            marker = f'  <- {color}'
        print(f'  r={r:3d} x={x:3d}: #{r_c:02X}{g:02X}{b:02X}{marker}')

# 检查 x=cx (竖直中线) 在太极图区域外的颜色
print('\n竖直中线 (x=cx=180) 在 r=80~180 范围:')
for r in range(75, 185, 2):
    y = cy + r
    if 0 <= y < h:
        px = img.getpixel((180, y))
        r_c, g, b = px[:3]
        marker = ''
        if not (r_c < 30 and g < 30 and b < 30):
            if r_c > 200 and g > 200 and b > 200:
                color = 'W'
            elif abs(r_c - 0xD4) < 30:
                color = 'G'
            else:
                color = f'?#{r_c:02X}{g:02X}{b:02X}'
            marker = f'  <- {color}'
        print(f'  r={r:3d} y={y:3d}: #{r_c:02X}{g:02X}{b:02X}{marker}')
