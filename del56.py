#!/usr/bin/env python3
"""
安全删除 5.6 整章 (行 939-1120 = ### 5.6 ... 包含末尾 ```).
保留 1121 (\n---\n) + 1122 (## 6. 构建与烧录) 及之后.
"""

SRC = '/Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/doc/ai_compass_product_and_tech_spec.md'

with open(SRC, 'r', encoding='utf-8') as f:
    lines = f.readlines()

# 验证关键行
print(f"行 938: {lines[937].rstrip()}")
print(f"行 939: {lines[938].rstrip()}")
print(f"行 1120: {lines[1119].rstrip()}")
print(f"行 1121: {lines[1120].rstrip()}")
print(f"行 1122: {lines[1121].rstrip()}")
print(f"行 1123: {lines[1122].rstrip()}")

# 删除 行 939-1120 (0-indexed: 938-1119)
# 即删除 lines[938] 到 lines[1119] 共 182 行
# 保留 lines[1120] (---) 和 lines[1121] (## 6. 构建与烧录)
new_lines = lines[:938] + lines[1120:]

with open(SRC, 'w', encoding='utf-8') as f:
    f.writelines(new_lines)

print(f"\n原始: {len(lines)} 行")
print(f"新: {len(new_lines)} 行")
print(f"删除: {len(lines) - len(new_lines)} 行")
