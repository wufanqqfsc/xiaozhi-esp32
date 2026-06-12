from PIL import Image
import math

img = Image.open('screenshots/screenshot.jpg')
w, h = img.size
cx, cy = w // 2, h // 2

print('=' * 60)
print('迭代14清理验证: BALANCE 字样和相关参考线移除')
print('=' * 60)

# 输出中心 161x161 区域字符图
print('\n中心 161x161 区域字符图 (B=黑 W=白 G=鎏金 .=其他):')
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

# 检查是否还有 "BALANCE" 字样的特征 (背景+文字对比)
print('\n=== 关键位置颜色采样 (验证参考线/文字移除) ===')
positions = {
    '中心 (太极图内)': (cx, cy),
    '上参考线 (cx, cy-70)': (cx, cy - 70),  # 原 layer2_indicator_line_ 位置
    '上参考线 (cx, cy-60)': (cx, cy - 60),  # 原参考线中部
    'BALANCE字位置 (cx, cy-90)': (cx, cy - 90),  # 原 layer3_state_label_ 位置
    'BALANCE字位置 (cx+20, cy-90)': (cx + 20, cy - 90),
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
        print(f'  {name:35s} ({x:3d},{y:3d}) {hex_c} -> {color_name}')

# 整体颜色统计
white_count = 0
black_count = 0
gold_count = 0
text_count = 0  # 非黑非白的中间色像素 (文字/参考线残留)
for y in range(0, h, 2):
    for x in range(0, w, 2):
        px = img.getpixel((x, y))
        r, g, b = px[:3]
        if r > 200 and g > 200 and b > 200:
            white_count += 1
        elif r < 30 and g < 30 and b < 30:
            black_count += 1
        elif abs(r - 0xD4) < 30 and abs(g - 0xAF) < 30 and abs(b - 0x37) < 30:
            gold_count += 1
        elif 30 <= r < 200 or 30 <= g < 200 or 30 <= b < 200:
            text_count += 1

print(f'\n=== 整体颜色统计 ===')
print(f'  白色像素:   {white_count}')
print(f'  黑色像素:   {black_count}')
print(f'  鎏金像素:   {gold_count}')
print(f'  文字/残留:  {text_count}  (数值越低,清理效果越好)')
