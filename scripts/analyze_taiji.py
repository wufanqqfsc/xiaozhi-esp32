from PIL import Image
import math

img = Image.open('screenshots/screenshot.jpg')
w, h = img.size
cx, cy = w // 2, h // 2

# 输出太极图特征
print('=' * 60)
print('迭代 13 太极图 验证报告')
print('=' * 60)

# 检查右上角和左下角是否有黑白
print('\n关键位置像素采样:')
positions = {
    '太极左上 (阳鱼白)': (cx - 40, cy - 10),
    '太极右上 (阴鱼黑)': (cx + 40, cy - 10),
    '太极左下 (阴鱼黑)': (cx - 40, cy + 10),
    '太极右下 (阳鱼白)': (cx + 40, cy + 10),
    '阳中黑点': (cx, cy - 40),
    '阴中白点': (cx, cy + 40),
    '外圆环 (r=78)': (cx + 78, cy),
    '外圆环发光 (r=82)': (cx + 82, cy),
    '鎏金位置 (r=160)': (cx + 160, cy),
}
for name, (x, y) in positions.items():
    if 0 <= x < w and 0 <= y < h:
        px = img.getpixel((x, y))
        r, g, b = px[:3]
        hex_c = f'#{r:02X}{g:02X}{b:02X}'
        if r > 200 and g > 200 and b > 200:
            color_name = '白色'
        elif r < 30 and g < 30 and b < 30:
            color_name = '黑色'
        elif abs(r - 0xD4) < 30 and abs(g - 0xAF) < 30 and abs(b - 0x37) < 30:
            color_name = '鎏金黄'
        else:
            color_name = '其他'
        print(f'  {name:25s} ({x:3d},{y:3d}) {hex_c} -> {color_name}')

# 扫描角度验证 S 形分界线
print('\n=== S 形分界线验证 (中心区域 30px 半径) ===')
print('注: 0度=右, 90度=下, 180度=左, 270度=上')
for angle in [0, 30, 60, 90, 120, 150, 180, 210, 240, 270, 300, 330]:
    rad = math.radians(angle)
    x = int(cx + 30 * math.cos(rad))
    y = int(cy + 30 * math.sin(rad))
    px = img.getpixel((x, y))
    r, g, b = px[:3]
    color = '黑' if r < 30 and g < 30 and b < 30 else ('白' if r > 200 and g > 200 and b > 200 else '灰')
    print(f'  {angle:3d}度: {color}')
