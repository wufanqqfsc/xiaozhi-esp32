#!/usr/bin/env python3
"""
重构 doc/ai_compass_product_and_tech_spec.md:
Target1 → Target2 对齐
"""

import re

SRC = '/Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/doc/ai_compass_product_and_tech_spec.md'

with open(SRC, 'r', encoding='utf-8') as f:
    content = f.read()

original_len = len(content)
print(f"原始长度: {original_len} 字符, {content.count(chr(10))+1} 行")


def delete_section(content, section_header, next_header_prefix):
    start = content.find(section_header)
    if start == -1:
        print(f"  ⚠ 未找到 {section_header[:50]}")
        return content
    pos = start + len(section_header)
    end = content.find(next_header_prefix, pos)
    if end == -1:
        end = len(content)
    print(f"  - 删除 {section_header.split(chr(10))[0][:60]} ({end-start} 字符)")
    return content[:start] + content[end:]


# 1. 删除 2.2.3 + 子章节
content = delete_section(content, "#### 2.2.3 AI 运势引擎（新增核心功能）", "#### 2.2.4")
content = delete_section(content, "#### 2.2.3.1 AI 运势引擎 - UI 布局设计", "#### ")
content = delete_section(content, "#### 2.2.3.2 WiFi / 蓝牙状态标识", "#### ")

# 2. 删除 4.1 AI 运势功能
content = delete_section(content, "### 4.1 AI 运势功能（核心需求）", "### 4.2")

# 3. 删除 5.6 AI 运势引擎
content = delete_section(content, "### 5.6 AI 运势引擎 - 实现接口与状态机", "## 6.")

# 4. 删除第 13 章 (幻觉的 v1.2 验收清单)
content = delete_section(content, "## 13. 功能验收清单（审计追踪", "---")


# 5. 重写 2.2.2 罗盘显示系统
start = content.find("#### 2.2.2 罗盘显示系统 ⭐")
end = content.find("#### 2.2.3")
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
7. **旋转**: FreeRTOS 任务每 50ms 更新角度, 文档 30s/圈 (代码中 `StartAutoRotation(30000)`)

**罗盘组件层级 (真实代码结构, 521 行 attitude_display.cc)**:
```
screen (lv_screen_active()) ─ 360×360
├── round_mask (圆屏遮罩, 180 圆角, 背景色 bg_outer)
│
└── attitude_container_ (罗盘主容器, 360×360, 透明背景)
    ├── background_ (深色背景 0x0A0A0A)
    │   └── bg_layer_center_ (中心微亮层 0x121212, 300 圆)
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


# 6. 重写 5.2 罗盘 UI 组件层级
start = content.find("### 5.2 罗盘 UI 组件层级（真实坐标）")
end = content.find("### 5.3")
if start != -1 and end != -1:
    new_section_52 = '''### 5.2 罗盘 UI 组件层级（真实坐标 ⭐ Target2）

```
屏幕 360×360 (cx=180, cy=180)
└── attitude_container_ (罗盘主容器, 360×360)
    ├── background_ (深色背景, 360×360, theme_colors.bg_outer)
    │   └── bg_layer_center_ (中心微亮圆, 300×300, theme_colors.bg_inner, pos 30,30)
    │
    ├── [Layer0 中心太极图]  r=44  (88×88 canvas)
    │   └── compass_taiji_->canvas_ (ARGB8888 画布, 黑白阴阳鱼, 金色外环)
    │       └── 30s/圈逆时针自动旋转
    │
    ├── [Layer1 核心信息区]  r=0~54  (108×108, 透明容器, 无内容)
    │   └── layer1_container_ (lv_obj, transparent, no children)
    │
    ├── [Layer2 动态指示区]  r=54~90  (lv_arc 1px 鎏金细环)
    │   └── layer2_inner_ring_ (直径 160, r=80, arc_width=1, theme_colors.border_line)
    │
    ├── [Layer3 状态进度区]  r=90~144  (双 lv_arc 状态环)
    │   ├── layer3_bg_arc_      (直径 260, r=130, arc_width=4, theme_colors.card_bg)
    │   └── layer3_progress_arc_ (直径 280, r=140, arc_width=4, theme_colors.state_normal, 0~360° 进度)
    │
    ├── [Layer4 边界留白区]  r=144~178  (1px 鎏金外圆环)
    │   └── layer4_outer_ring_ (lv_obj, 356×356, border_width=1, theme_colors.border_line, radius=178)
    │
    └── [方位点]  r=72  (4 个 6×6 实心圆点, N/E/S/W)
        ├── dir_n_label_ (北, pos(177,106))   ←  cx-3, cy-72-3
        ├── dir_e_label_ (东, pos(249,177))   ←  cx+72-3, cy-3
        ├── dir_s_label_ (南, pos(177,249))   ←  cx-3, cy+72-3
        └── dir_w_label_ (西, pos(105,177))   ←  cx-72-3, cy-3

★ 主题: AttitudeTheme 单例提供 5 档状态色 (state_normal/light/mid/heavy/danger)
★ 状态进度: layer3_progress_arc_ 的 INDICATOR 颜色随 current_state_level_ 切换
★ 公开 API: UpdateStateColor(int level) 修改状态等级, SwitchTheme(AttitudeThemeType) 切换主题
★ 太极图控制: RotateTaiji() / RotateTaijiCCW() / StartTaijiAutoRotation(period) / StopTaijiAutoRotation()
★ IMU 接口: SetAttitudeData(pitch, roll, yaw) (UI 端未消费, 预留)
```

'''
    content = content[:start] + new_section_52 + content[end:]
    print(f"  ✓ 重写 5.2 ({len(new_section_52)} 字符)")


# 7. 重写 5.3 罗盘参数宏
start = content.find("### 5.3 罗盘参数宏（")
end = content.find("### 5.4")
if start != -1 and end != -1:
    new_section_53 = '''### 5.3 罗盘参数宏（[attitude_display.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.h)）

> **当前实现 (Target2 风格)**: 头文件 5848 字节, 极简定义, 不再维护专用参数宏.
> 实际坐标常量硬编码在 `attitude_display.cc` 各 Create 函数中.
> 主题色值通过 `AttitudeTheme::GetInstance().GetColors()` 读取.

**实际坐标常量 (attitude_display.cc):**

```cpp
// 屏幕中心
constexpr int CENTER_X = 180;    // 360/2
constexpr int CENTER_Y = 180;    // 360/2

// Layer0 太极图 (CreateLayer0Taiji)
constexpr int TAIJI_RADIUS = 44;  // 88×88 画布, 直径 88, 半径 44

// Layer1 核心信息区 (CreateLayer1CoreInfo)
constexpr int LAYER1_SIZE = 108;  // 透明容器
constexpr int LAYER1_RADIUS = 54;

// Layer2 动态指示区 (CreateLayer2DynamicIndicator)
constexpr int LAYER2_RING_DIAM = 160;
constexpr int LAYER2_RING_R = 80;
constexpr int LAYER2_ARC_WIDTH = 1;

// Layer3 状态进度区 (CreateLayer3StatusProgress)
constexpr int LAYER3_BG_DIAM = 260;       // 背景环
constexpr int LAYER3_BG_R = 130;
constexpr int LAYER3_PROGRESS_DIAM = 280;  // 进度环
constexpr int LAYER3_PROGRESS_R = 140;
constexpr int LAYER3_ARC_WIDTH = 4;

// Layer4 边界留白区 (CreateLayer4Boundary)
constexpr int LAYER4_OUTER_SIZE = 356;
constexpr int LAYER4_OUTER_R = 178;
constexpr int LAYER4_BORDER_WIDTH = 1;

// 方位点 (CreateCompassPoints)
constexpr int POINT_SIZE = 6;
constexpr int POINTS_RADIUS = 72;  // 4 方位点 r=72 圆周
```

**主题色 (attitude_theme.h, 单例 AttitudeTheme):**

| 名称 | 用途 | 默认值 (Dark Gold) |
|-----|-----|------------------|
| `bg_outer` | 屏幕外圈背景 | `#1A1A1A` |
| `bg_inner` | 中心微亮层 | `#2A2A2A` |
| `text_main` | 主文字 | `#FFFFFF` |
| `border_line` | 边界线 (Layer2/4) | `#D4AF37` (鎏金) |
| `card_bg` | 卡片背景 (Layer3 bg arc) | `#3A3A3A` |
| `point_default` | 方位点 | `#D4AF37` (鎏金) |
| `state_normal` | 状态等级 0 颜色 | `#2E5E4E` (绿) |
| `state_light` | 状态等级 1 颜色 | `#4A6FA5` (蓝) |
| `state_mid` | 状态等级 2 颜色 | `#D4AF37` (金) |
| `state_heavy` | 状态等级 3 颜色 | `#E67E22` (橙) |
| `state_danger` | 状态等级 4 颜色 | `#B82601` (红) |

'''
    content = content[:start] + new_section_53 + content[end:]
    print(f"  ✓ 重写 5.3 ({len(new_section_53)} 字符)")


# 8. 重写 7 路线图
start = content.find("## 7. 开发路线图（基于最新产品定义 ⭐）")
end = content.find("## 8.")
if start != -1 and end != -1:
    new_section_7 = '''## 7. 开发路线图（Target2 实际状态 ⭐）

> **更新说明 (2026-06-14)**: 经过代码审计，文档原 5.2/5.3/5.6 节描述的 Target1 (罗盘+鱼眼+运势) 实际未实现。当前代码为 Target2 (4 层同心圆 + 状态进度)。本路线图基于实际代码状态制定。

### 当前实现状态

| 阶段 | 内容 | 状态 | 证据 |
|------|------|------|------|
| **阶段一 (Target2)** | 4 层同心圆基础罗盘 | ✅ 已完成 | attitude_display.cc 521 行, SetupUI 包含 CreateLayer0~4 + CreateCompassPoints |
| **阶段二 (Target2)** | 太极图自动旋转 | ✅ 已完成 | compass_taiji.cc, StartAutoRotation(30000), 30s/圈逆时针 |
| **阶段三 (Target2)** | 主题系统 (暗色/其他) | ✅ 已完成 | attitude_theme.h, SwitchTheme() API |
| **阶段四 (Target2)** | 状态进度颜色 (5 档) | ✅ 已完成 | UpdateStateColor(int level), state_normal/light/mid/heavy/danger |
| **阶段五 (Target2)** | IMU 姿态数据接口 | 🟡 API 已存在未联动 | SetAttitudeData(pitch,roll,yaw), UI 端未消费 |
| **阶段六 (Target2)** | 视觉增强 (动效/平滑) | ❌ 未开始 | 进度环无补间动画, 主题切换瞬变 |

### 迭代计划（建议）

#### 阶段甲：状态进度环动效 (短期, 1-2 天)

**目标**: 让 `layer3_progress_arc_` 的进度变化 (UpdateStateColor) 带有平滑补间动画, 提升视觉品质

**子任务**:
- 进度从 0° 到 360° 的过渡用 lv_anim_t 补间 (200ms 缓动)
- 颜色变化使用插值过渡 (RGB 通道线性, 参考鱼眼脉冲实现)
- 验证: `UpdateStateColor(0)` → `UpdateStateColor(4)` 颜色平滑切换

#### 阶段乙：IMU 姿态数据接入 (中期, 3-5 天)

**目标**: 真实 IMU 传感器 (QMI8658) 数据驱动进度环

**子任务**:
- 接入 I2C QMI8658 驱动 (主控 ESP32-S3 + 1.85B 板载)
- 读取 pitch/roll/yaw 数据
- 计算"姿态平衡分" (0-100) → 映射到 state_level (0-4) → 进度环角度 (0-360)
- 验证: 设备倾斜 → 进度环实时变化

#### 阶段丙：主题切换 UI 入口 (中期, 2-3 天)

**目标**: 暴露主题切换给用户 (触摸/按键/语音)

**子任务**:
- 长按屏幕中心 → 主题选择菜单
- 或: AI 语音指令 "切换到夜光主题" → SwitchTheme(LIGHT)
- 验证: 4 种主题间循环切换

#### 阶段丁：多卦象扩展 (长期, 1-2 周, 视需求)

**目标**: 从 4 层同心圆扩展为"4 层 + 64 卦" (target1 风格的折中)

**子任务**:
- 在 r=44 (中心太极) 与 r=80 (Layer2) 之间, 插入 8 卦名大字
- 在 r=80 (Layer2) 与 r=130 (Layer3) 之间, 插入 12 地支
- Layer4 之外, 插入 4 方位大字 (60×60, 北/东/南/西)
- 注意: 这会增加密度, 需要权衡视觉可读性

#### 阶段戊：可选增强 (无限, P2)

- 屏幕截图保存 (已有 SnapshotToJpeg 组件)
- 触摸滑动关闭手势 (CST816S)
- BLE 配网与状态显示
- 历史数据记录 (NVS 存储状态历史)

### 不再实现的功能（已从产品中移除）

| 废弃项 | 原计划 | 移除原因 |
|-------|-------|---------|
| 8 卦名大字 48×48 | 阶段一 | 迭代18 密度梯度重构, 移除避免视觉过载 |
| 10 天干 | 阶段一 | 迭代20 Target2 风格定型, 不需要 |
| 12 地支 | 阶段一 | 迭代20 Target2 风格定型, 不需要 |
| 64 卦符号 | 阶段一 | 迭代19 移除 |
| 4 方位大字 60×60 | 阶段一 | 迭代18 改为 6×6 圆点, 保持密度梯度 |
| 鱼眼状态图标 (WiFi/BLE) | 阶段二 (原) | Target2 无此概念, 阶段二已废弃 |
| AI 运势引擎三态状态机 | 阶段三 (原) | Target2 无此概念, 阶段三已废弃 |
| 200×240 结果卡 | 阶段三 (原) | Target2 无此概念 |
| HighlightDirection/Gua 脉冲 | 阶段三 (原) | Target2 无此概念 |

'''
    content = content[:start] + new_section_7 + content[end:]
    print(f"  ✓ 重写 7 路线图 ({len(new_section_7)} 字符)")


# 保存
with open(SRC, 'w', encoding='utf-8') as f:
    f.write(content)

new_len = len(content)
print()
print(f"原始长度: {original_len}")
print(f"新长度:   {new_len}")
print(f"减少:     {original_len - new_len} 字符")
print(f"行数:     {content.count(chr(10))+1}")
