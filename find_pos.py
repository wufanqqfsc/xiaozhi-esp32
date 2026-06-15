with open('/Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/doc/ai_compass_product_and_tech_spec.md', 'r', encoding='utf-8') as f:
    c = f.read()

# 5.3 节末尾
i_5_3_anchor = c.find('// 4 方位点 r=72 圆周')
print(f'5.3 主题色表 锚点: {i_5_3_anchor}')

# 5.3 后的 第一个 ```
ck1 = c.find('```\n', i_5_3_anchor)
print(f'5.3 后第一个 ```: {ck1}')

# 5.4 标题
i_5_4 = c.find('### 5.4', ck1)
print(f'5.4 标题: {i_5_4}')

# 6 章 Target1 旧
i_6_old = c.find('## 6. 构建与烧录\n\n### 6.1', ck1)
print(f'6 章 Target1 旧: {i_6_old}')

# 7 章 Target2
i_7_t2 = c.find('## 7. 开发路线图（Target2')
print(f'7 章 Target2: {i_7_t2}')

# 文档版本
i_tail = c.find('*文档版本')
print(f'文档版本: {i_tail}')

# 5.4 后面到 6 之间的内容片段
if i_5_4 > 0 and i_6_old > 0:
    print()
    print('=== 5.4 后到 6 之间片段 (前 300 字符) ===')
    print(c[i_5_4:i_5_4+300])
