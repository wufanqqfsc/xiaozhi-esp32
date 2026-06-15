#!/usr/bin/env python3
"""
清理 5.3 节末尾的鱼眼宏残留 (5.3 节是坐标宏+主题色表, 应该以 ``` 结束, 然后 5.4).
实际 5.3 节后立即接了鱼眼宏段 (Target1 残留), 紧跟 5.4 标题.

策略: 删除 17594 到 24468 之间的所有内容, 插入正确的 5.4 章节.
"""

SRC = '/Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/doc/ai_compass_product_and_tech_spec.md'

with open(SRC, 'r', encoding='utf-8') as f:
    c = f.read()

# 5.3 锚点 (POINTS_RADIUS 行)
i_5_3_anchor = c.find('// 4 方位点 r=72 圆周')
# 7 章 Target2 位置
i_7_t2 = c.find('## 7. 开发路线图（Target2')
# 文档版本
i_tail = c.find('*文档版本')

print(f"5.3 锚点: {i_5_3_anchor}")
print(f"7 章 Target2: {i_7_t2}")
print(f"文档版本: {i_tail}")

# 5.3 节应该在锚点后是 ``` 结束, 然后是 5.4 标题
# 但实际上后面跟了鱼眼宏 + 5.4 旧 + 5.5 + 6 + 旧 12, 然后才是 7 章
# 所以正确做法: 保留 5.3 节 (17594 之前 + 锚点+后面的 ``` ), 删 17594 -> 24468, 补 5.4/5.5/6

# 5.3 节真正的末尾: 锚点 + 后面的 ```  - 但锚点后面是 "```**主题色", 错乱!
# 实际 5.3 节被分割:
#   17594 (// 4 方位点 r=72 圆周)
#   紧跟着 "```**主题色 (attitude_theme.h..."   <- 这是 5.3 主题色表
#   表结束后应该 ```  -> 但实际是鱼眼宏段 -> 然后是 5.4 标题

# 看锚点后 500 字符确认结构
print()
print("=== 锚点后 800 字符 ===")
print(c[i_5_3_anchor:i_5_3_anchor+800])
