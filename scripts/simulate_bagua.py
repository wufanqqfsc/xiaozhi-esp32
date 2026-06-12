"""
模拟 64 卦符号绘制算法 - Python 验证
对照 compass_bagua.cc 中的逻辑
"""
import math
from PIL import Image, ImageDraw

# 64 卦爻表 (与 compass_bagua.cc 一致)
hexagrams = [
    # 0-7: 坤上 + (坤艮坎巽震离兑乾)下
    0b000000, 0b000001, 0b000010, 0b000011, 0b000100, 0b000101, 0b000110, 0b000111,
    # 8-15: 艮上 + (坤艮坎巽震离兑乾)下
    0b001000, 0b001001, 0b001010, 0b001011, 0b001100, 0b001101, 0b001110, 0b001111,
    # 16-23: 坎上 + (坤艮坎巽震离兑乾)下
    0b010000, 0b010001, 0b010010, 0b010011, 0b010100, 0b010101, 0b010110, 0b010111,
    # 24-31: 巽上 + (坤艮坎巽震离兑乾)下
    0b011000, 0b011001, 0b011010, 0b011011, 0b011100, 0b011101, 0b011110, 0b011111,
    # 32-39: 震上 + (坤艮坎巽震离兑乾)下
    0b100000, 0b100001, 0b100010, 0b100011, 0b100100, 0b100101, 0b100110, 0b100111,
    # 40-47: 离上 + (坤艮坎巽震离兑乾)下
    0b101000, 0b101001, 0b101010, 0b101011, 0b101100, 0b101101, 0b101110, 0b101111,
    # 48-55: 兑上 + (坤艮坎巽震离兑乾)下
    0b110000, 0b110001, 0b110010, 0b110011, 0b110100, 0b110101, 0b110110, 0b110111,
    # 56-63: 乾上 + (坤艮坎巽震离兑乾)下
    0b111000, 0b111001, 0b111010, 0b111011, 0b111100, 0b111101, 0b111110, 0b111111
]

main_gua_indices = [0, 9, 18, 27, 36, 45, 54, 63]

# 创建画布
W, H = 360, 360
img = Image.new('RGB', (W, H), (10, 10, 10))
draw = ImageDraw.Draw(img)

# 中心点
cx, cy = 180, 180

# 鎏金颜色
GOLD = (212, 175, 55)
SILVER = (192, 192, 192)

# 画太极图背景 (中心白色+黑色圆)
draw.ellipse([cx-80, cy-80, cx+80, cy+80], fill=(255, 255, 255))
# 阴阳鱼 (简化)
draw.pieslice([cx-80, cy-80, cx+80, cy+80], 270, 90, fill=(0, 0, 0))
draw.ellipse([cx-40, cy-80, cx+40, cy], fill=(255, 255, 255))
draw.ellipse([cx-40, cy, cx+40, cy+80], fill=(0, 0, 0))
draw.ellipse([cx-12, cy-32, cx+12, cy-8], fill=(0, 0, 0))
draw.ellipse([cx-12, cy+8, cx+12, cy+32], fill=(255, 255, 255))

# 画 64 卦
radius = 140
angle_step = 2 * math.pi / 64
hex_size = 22

for i in range(64):
    # 角度: -90° (顶部) + i * 5.625° (顺时针)
    angle = -math.pi / 2 + i * angle_step
    px = cx + int(radius * math.cos(angle))
    py = cy + int(radius * math.sin(angle))

    is_main = i in main_gua_indices
    color = GOLD if is_main else SILVER
    size = 28 if is_main else 20

    # 画 6 个爻
    hexagram = hexagrams[i]
    yao_height = 3
    yao_gap = 1
    total_height = (yao_height + yao_gap) * 6 - yao_gap
    start_y = py + total_height // 2

    for j in range(6):
        is_yang = (hexagram >> j) & 1
        yao_y = start_y - j * (yao_height + yao_gap) - yao_height // 2
        if is_yang:
            # 阳爻: 实线
            draw.line([(px - 5, yao_y), (px + 5, yao_y)], fill=color, width=yao_height)
        else:
            # 阴爻: 虚线 (两段)
            draw.line([(px - 7, yao_y), (px - 2, yao_y)], fill=color, width=yao_height)
            draw.line([(px + 2, yao_y), (px + 7, yao_y)], fill=color, width=yao_height)

img.save('screenshots/sim_bagua_64.jpg', quality=85)
print(f'✓ 模拟图已保存: screenshots/sim_bagua_64.jpg')
print(f'  64 卦沿 r={radius}px 圆周分布')
print(f'  8 个主卦: {main_gua_indices} (鎏金)')
print(f'  56 个变卦: 其他 (白银)')
