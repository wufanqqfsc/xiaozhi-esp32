#!/usr/bin/env python3
"""
删除第 13 章 (幻觉内容) + 更新文档版本.
"""
SRC = '/Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/doc/ai_compass_product_and_tech_spec.md'

with open(SRC, 'r', encoding='utf-8') as f:
    lines = f.readlines()

print(f"原始: {len(lines)} 行")

# 验证关键行
print(f"行 1345: {lines[1344].rstrip()}")
print(f"行 1346: {lines[1345].rstrip()}")
print(f"行 1533: {lines[1532].rstrip()}")
print(f"行 1534: {lines[1533].rstrip()}")
print(f"行 1535: {lines[1534].rstrip()}")
print(f"行 1536: {lines[1535].rstrip()}")
print(f"行 1537: {lines[1536].rstrip()}")

# 删除 行 1346-1533 (13 章 + 末尾 ---)
# 0-indexed: 1345-1532
new_lines = lines[:1345] + lines[1533:]

# 更新文档版本
# 在新行中, 找 "*文档版本:"
for i, line in enumerate(new_lines):
    if "v1.2 (新增第 13 章" in line:
        new_lines[i] = "*文档版本: v1.4 (Target1→Target2 文档对齐: 删除鱼眼/运势/结果卡/13章幻觉, 重写 5.3/5.4/5.6/7/12)*\n"
        print(f"✓ 更新行 {i+1} 文档版本")
    elif "更新日期: 2026-06-13" in line:
        new_lines[i] = "*更新日期: 2026-06-14*\n"
        print(f"✓ 更新行 {i+1} 更新日期")

with open(SRC, 'w', encoding='utf-8') as f:
    f.writelines(new_lines)

print(f"\n原始: {len(lines)} 行")
print(f"新: {len(new_lines)} 行")
print(f"删除: {len(lines) - len(new_lines)} 行")
