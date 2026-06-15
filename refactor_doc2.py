#!/usr/bin/env python3
"""
第二轮重构:
1. 删除残留的 13.1-13.8 (line 908-1107)
2. 删除残留的 14 章 (line 1108-1163)
3. 重写 2.2.2 (Target1 描述仍残留)
4. 删除 5.4 残留的 "AI 运势引擎动画态速度参数"
5. 重写 12 总结
"""

import re

SRC = '/Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/doc/ai_compass_product_and_tech_spec.md'

with open(SRC, 'r', encoding='utf-8') as f:
    content = f.read()

original_len = len(content)
lines = content.split('\n')
print(f"原始: {len(content)} 字符, {len(lines)} 行")


# 1. 删除第 13 章 + 第 14 章 (从 line 908 到 1103/1163)
# 13.1 节标题在 908 行 (-----|------| 是 13.1 的破损失效行)
# 但实际 13.1 标题找不到, 13.2 在 line 914
# 14.1 估计在 1108 行

# 找到 line 908 (异常行), 删到 line 1103 之前 (line 1104 是 "AI运势引擎 ❌ 未实现" 表)
# 然后再删 14.* (line 1108 之后到文档末尾前)
# 但保留文档末尾的 ---分隔 + 文档版本信息

# 简单方案: 删除 line 908-1107 (13 章) + 14 章 (line 1108 到 line 1103 之前)
# 实际上 13.1 标题不存在, 我用正则匹配更稳

# 用 "13.1" 图例的破损失效行 (-----|------|) 之前
chap13_start = content.find("-----|")
if chap13_start == -1:
    # 备用: 找 "13.1" 的位置
    chap13_start = content.find("### 13.1")
    if chap13_start != -1:
        # 找上一个 "---" 分隔符
        prev_sep = content.rfind("---", 0, chap13_start)
        if prev_sep != -1:
            chap13_start = prev_sep

# 找 "### 14.1" 之前的  --- 分隔符 (即第 13 章的结束)
chap13_end = content.find("### 14.1")
if chap13_end == -1:
    chap13_end = content.find("## 14.")
if chap13_end != -1:
    prev_sep = content.rfind("---", 0, chap13_end)
    if prev_sep != -1 and prev_sep > chap13_start:
        chap13_end = prev_sep
    else:
        # 找 13.8 结束
        # 实际上 13.8 标题不完整, 找 14.1 位置
        chap13_end = chap13_end

# 找 14 章的结束: 找文档末尾的版本信息
# 文档末尾通常是 *文档版本: ...*
doc_tail_start = content.find("\n*文档版本:")
if doc_tail_start == -1:
    # 备用
    doc_tail_start = content.find("*文档版本:")
if doc_tail_start == -1:
    doc_tail_start = content.find("适用硬件: Waveshare")

# 找 14 章的起始 (跳过前面的 ---)
chap14_start = content.find("### 14.")
if chap14_start == -1:
    chap14_start = content.find("## 14.")
if chap14_start != -1:
    # 14 章的起始: 跳到上一个 --- 之后
    prev_sep = content.rfind("---", 0, chap14_start)
    if prev_sep != -1 and prev_sep > chap13_start + 100:
        chap14_start = prev_sep
    else:
        chap14_start = chap14_start

# 现在合并: 删除 chap13_start 到 doc_tail_start
# 但 chap13_end == chap14_start (它们之间只有 ---)
# 所以直接删除 [chap13_start, doc_tail_start) 即可
if chap13_start != -1 and doc_tail_start != -1 and chap13_start < doc_tail_start:
    # 找 chap13_start 之前最近的 ---
    pre_sep = content.rfind("---", 0, chap13_start)
    if pre_sep != -1 and pre_sep > 0:
        # 保留 --- 分隔符前面的内容, 删除 13/14 章, 保留 doc_tail
        before = content[:pre_sep]
        # 包含一个 --- 分隔符
        before_with_sep = before.rstrip() + "\n\n---\n\n"
        after = content[doc_tail_start:].lstrip()
        content = before_with_sep + after
        print(f"  ✓ 删除第 13+14 章 ({doc_tail_start - chap13_start} 字符)")


# 2. 重写 2.2.2 罗盘显示系统 (Target1 残留)
start = content.find("#### 2.2.2 罗盘显示系统 ⭐")
end = content.find("#### 2.2.4")
if start != -1 and end != -1:
    new_section_222 = '''#### 2.2.2 罗盘显示系统 ⭐

**视觉层次** (Target2 风格, 360×360 圆形屏幕, 从内到外密度梯度 1→4→10→12):

| 层级 | 元素 | 半径范围 | 行为 | 密度 | 技术实现 |
|-----|-----|---------|-----|-----|---------|
| **L0** 中心 | 太极图 | r=44 | **30秒逆时针旋转一圈** | 1 (极低) | LVGL canvas (88×88) + ARGB8888 |
| **L1** 核心信息区 | (空容器) | 0-54 | 固定不动, 透明 | - | LVGL obj 容器, 占位用 |
| **L2** 动态指示区 | 内圈装饰细线 | 54-90 | 固定不动 | 4 方位点 r=72 | lv_arc 1px 鎏金细环 r=80 |
| **L3** 状态进度区 | 背景环 + 进度环 | 90-144 | 进度环按 state_level 着色 | - | 背景环 r=130, 进度环 r=140 |
| **L4** 边界留白区 | 1px 鎏金外圆环 | 144-178 | 固定不动 | - | LVGL obj + 1px 金色边 |
| **方位点** | 4 个 6×6 圆点 | r=72 | 固定不动 | 4 | LVGL obj (N/E/S/W) |

> **设计变更历史 (迭代18-20 密度梯度重构)**:
> - 迭代18: 64 卦/10 天干/12 地支全部移除, 中心太极半径 80→44
> - 迭代19: 移除 64 卦符号层 (r=160)
> - 迭代20: Target2 风格最终定型, 4 层同心圆 + 状态进度环
> - **4 方位点 6×6 圆点** (替代原 60×60 大字方案, 与太极图 r=44 紧邻构成最简视觉)

**核心约束：所有 UI 元素创建在 attitude_container_ 内 (4 层同心圆都在容器内), 太极图 r=44 不可与状态环 r=130 互相覆盖.**

**太极图绘制算法** ([compass_taiji.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/compass_taiji.cc)):
1. **内存分配**: 优先 PSRAM 分配 canvas buffer (ARGB8888, 4BPP) — 88×88 ≈ 31KB
2. **阳鱼绘制**: 填充白色大圆 (左半)
3. **阴鱼绘制**: 右半圆填充黑色
4. **阴阳互抱**: 上半黑圆内绘制白色小圆, 下半白圆内绘制黑色小圆
5. **点睛**: 阴阳鱼各点一个反向色小圆
6. **鎏金环**: 绘制圆周 1px 宽的金色环 (`#D4AF37`)
7. **旋转**: FreeRTOS 任务每 50ms 更新角度, 代码中 `StartAutoRotation(30000)` (30s/圈)

**罗盘组件层级 (真实代码结构, attitude_display.cc 521 行)**:
```
screen (lv_screen_active()) ─ 360×360
├── round_mask (圆屏遮罩, 180 圆角, 背景色 bg_outer)
│
└── attitude_container_ (罗盘主容器, 360×360, 透明背景)
    ├── background_ (深色背景 theme_colors.bg_outer)
    │   └── bg_layer_center_ (中心微亮层 theme_colors.bg_inner, 300 圆)
    │
    ├── [Layer0] CompassTaiji::Create (太极图, r=44, 88×88 canvas)
    │   └── canvas (ARGB8888, 黑白阴阳鱼, 金色外环, 30s/圈旋转)
    │
    ├── [Layer1] layer1_container_ (0~54px, 108×108 空容器, 透明)
    │
    ├── [Layer2] layer2_inner_ring_ (54~90px, lv_arc 1px 鎏金细环 r=80)
    │
    ├── [Layer3] layer3_bg_arc_ + layer3_progress_arc_ (90~144px)
    │   ├── bg_arc (lv_arc, r=130, 4px card_bg 颜色, 装饰背景)
    │   └── progress_arc (lv_arc, r=140, 4px state_normal 颜色, 状态指示)
    │
    ├── [Layer4] layer4_outer_ring_ (144~178px, 1px 鎏金外圆环 r=178)
    │
    └── [方位点] dir_n_label_ / dir_e_label_ / dir_s_label_ / dir_w_label_
        └── 4 个 6×6 圆点 (lv_obj, radius=LV_RADIUS_CIRCLE), r=72 圆周上
```

**实现文件**:
- [compass_taiji.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/compass_taiji.cc) — 太极图组件 (自动旋转 30s/圈)
- [attitude_display.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.h) — AttitudeDisplay 类声明 (主题参数)
- [attitude_display.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc) — 4 层同心圆 + 主题切换 + 状态进度 API
- [attitude_theme.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_theme.h) — 主题色值定义 (5 档状态色)
- [lvgl_display.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/lvgl_display/lvgl_display.cc) — LVGL 显示基类

'''
    content = content[:start] + new_section_222 + content[end:]
    print(f"  ✓ 重写 2.2.2 ({len(new_section_222)} 字符)")


# 3. 删除 5.4 中残留的 "AI 运势引擎动画态速度参数"
section_54_start = content.find("### 5.4 旋转速度参数")
section_54_end = content.find("### 5.5")
if section_54_start != -1 and section_54_end != -1:
    # 找 5.4 中的 "AI 运势引擎动画态速度参数"
    anim_section_start = content.find("**★ AI 运势引擎动画态速度参数", section_54_start, section_54_end)
    anim_section_end_marker = content.find("**★", anim_section_start + 1 if anim_section_start != -1 else section_54_end, section_54_end)
    if anim_section_start != -1 and anim_section_end_marker != -1:
        # 删除从 anim_section_start 到 anim_section_end_marker + 整个 "**★ ..." 段
        # 找 anim_section_end_marker 后的下一个 "**★" 段
        next_section = content.find("\n- ", anim_section_end_marker, section_54_end)
        if next_section == -1:
            next_section = section_54_end
        # 调整到 anim_section_end_marker 所在行的开头
        line_start = content.rfind("\n", 0, anim_section_start) + 1
        # 找到 anim_section_end_marker 所在段结束
        # 这一段通常到下一个 "**★" 之前
        block_end = content.find("\n- ", anim_section_end_marker, section_54_end)
        if block_end == -1:
            block_end = section_54_end
        # 调整到 block_end 之前最近的 \n
        block_end_line = content.rfind("\n", 0, block_end) + 1
        # 删除 line_start 到 block_end_line
        content = content[:line_start] + content[block_end_line:]
        print(f"  ✓ 删除 5.4 中 AI 运势动画态速度参数段")


# 4. 重写 12 总结与下一步 (Target1 残留)
start_12 = content.find("## 12. 总结与下一步")
end_12 = content.find("## 13.")
if start_12 == -1:
    end_12 = len(content)
if start_12 != -1 and end_12 != -1:
    new_section_12 = '''## 12. 总结与下一步

### 12.1 当前实现状态 (Target2 实际)

| 模块 | 状态 | 备注 |
|------|------|------|
| 4 层同心圆罗盘 | ✅ 已完成 | 太极图 + Layer1~4 + 4 方位点 |
| 太极图自动旋转 | ✅ 已完成 | 30s/圈逆时针 |
| 主题系统 (暗色+金色) | ✅ 已完成 | AttitudeTheme 单例 |
| 5 档状态颜色 | ✅ 已完成 | state_normal/light/mid/heavy/danger |
| 状态进度 API | ✅ 已完成 | UpdateStateColor(int level) |
| IMU 数据接口 | 🟡 API 已存在未联动 | SetAttitudeData(pitch,roll,yaw) |
| 截图服务 | ✅ 已存在 | snapshot_service |
| 主题切换 UI 入口 | ❌ 未实现 | 需用户交互入口 |
| 状态进度环动效 | ❌ 未实现 | 进度环补间动画 |
| 鱼眼状态图标 | ❌ **已废弃** | Target1 设计, Target2 不需要 |
| AI 运势引擎 | ❌ **已废弃** | Target1 设计, Target2 不需要 |
| 200×240 结果卡 | ❌ **已废弃** | Target1 设计, Target2 不需要 |
| 64 卦/天干/地支 | ❌ **已废弃** | 迭代18-20 移除 |
| 4 方位大字 (60×60) | ❌ **已废弃** | 改为 6×6 圆点 |

### 12.2 建议迭代路径 (Target2 增强)

**P0 (1-2 周)**:
- 阶段甲: 状态进度环动效 (lv_anim_t 补间)
- 阶段丙: 主题切换 UI 入口 (触摸/语音)

**P1 (3-5 周)**:
- 阶段乙: IMU (QMI8658) 数据接入, 真实驱动进度环

**P2 (长期, 视需求)**:
- 阶段丁: 多卦象扩展 (8 卦名 + 12 地支 + 4 方位大字)
- 阶段戊: 触摸手势/截图分享/历史记录等可选增强

### 12.3 已废弃功能 (Target1 → Target2 转移)

详见 [第 7 章 路线图](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/doc/ai_compass_product_and_tech_spec.md#7-开发路线图target2-实际状态-) "不再实现的功能" 表.

主要废弃项:
- 鱼眼状态图标 (WiFi/BLE) — Target2 不需要
- AI 运势引擎 (三态状态机 + 结果卡) — Target2 不需要
- 8 卦名大字 48×48 — 密度梯度重构移除
- 4 方位大字 60×60 — 改为 6×6 圆点
- 10 天干 / 12 地支 / 64 卦 — 全部移除

'''
    content = content[:start_12] + new_section_12 + content[end_12:]
    print(f"  ✓ 重写 12 总结 ({len(new_section_12)} 字符)")


# 保存
with open(SRC, 'w', encoding='utf-8') as f:
    f.write(content)

new_lines = content.split('\n')
print()
print(f"原始: {original_len} 字符, {len(lines)} 行")
print(f"新:   {len(content)} 字符, {len(new_lines)} 行")
print(f"减少: {original_len - len(content)} 字符")
