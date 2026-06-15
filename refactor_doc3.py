#!/usr/bin/env python3
"""
第三轮重构 - 基于精确行号, 安全删除/重写:
- 165-183: 删除 2.2.3
- 184-471: 删除 2.2.3.1 + 2.2.3.2
- 700-733: 删除 4.1
- 1012-1195: 删除 5.6 (含子节)
- 1288-1458: 重写 7
- 1601-1789: 删除 13 (整章)
- 110-164: 重写 2.2.2

每步独立执行, 不依赖上一步的位置。
"""

SRC = '/Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/doc/ai_compass_product_and_tech_spec.md'

with open(SRC, 'r', encoding='utf-8') as f:
    lines = f.readlines()

print(f"原始: {len(lines)} 行")


def delete_lines(lines, start, end):
    """删除 lines[start-1:end] (1-indexed inclusive)"""
    print(f"  删除 行 {start}-{end} ({end-start+1} 行)")
    return lines[:start-1] + lines[end:]


def replace_lines(lines, start, end, new_text):
    """替换 lines[start-1:end] (1-indexed inclusive)"""
    new_lines = new_text.split('\n')
    # 末尾加换行 (除非 new_text 已经以 \n 结尾)
    if not new_text.endswith('\n'):
        new_lines = [l + '\n' for l in new_lines]
    print(f"  重写 行 {start}-{end} → {len(new_lines)} 行")
    return lines[:start-1] + new_lines + lines[end:]


# ===== 安全删除顺序: 从后往前, 行号不会因为前面的删除而偏移 =====
# 1. 删除第 13 章 (1601-1789)
lines = delete_lines(lines, 1601, 1789)

# 2. 重写 7 路线图 (1288-1458)
NEW_SECTION_7 = '''## 7. 开发路线图（Target2 实际状态 ⭐）

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

**目标**: 让 `layer3_progress_arc_` 的进度变化 (UpdateStateColor) 带有平滑补间动画

- 进度从 0° 到 360° 的过渡用 lv_anim_t 补间 (200ms 缓动)
- 颜色变化使用插值过渡 (RGB 通道线性)
- 验证: `UpdateStateColor(0)` → `UpdateStateColor(4)` 颜色平滑切换

#### 阶段乙：IMU 姿态数据接入 (中期, 3-5 天)

**目标**: 真实 IMU 传感器 (QMI8658) 数据驱动进度环

- 接入 I2C QMI8658 驱动 (主控 ESP32-S3 + 1.85B 板载)
- 读取 pitch/roll/yaw 数据
- 计算"姿态平衡分" (0-100) → 映射到 state_level (0-4) → 进度环角度 (0-360)
- 验证: 设备倾斜 → 进度环实时变化

#### 阶段丙：主题切换 UI 入口 (中期, 2-3 天)

**目标**: 暴露主题切换给用户 (触摸/按键/语音)

- 长按屏幕中心 → 主题选择菜单
- 或: AI 语音指令 "切换到夜光主题" → SwitchTheme(LIGHT)
- 验证: 4 种主题间循环切换

#### 阶段丁：多卦象扩展 (长期, 1-2 周, 视需求)

**目标**: 从 4 层同心圆扩展为"4 层 + 64 卦" (target1 风格的折中)

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
lines = replace_lines(lines, 1288, 1458, NEW_SECTION_7)

# 3. 删除 5.6 (1012-1195)
lines = delete_lines(lines, 1012, 1195)

# 4. 删除 4.1 (700-733)
lines = delete_lines(lines, 700, 733)

# 5. 删除 2.2.3.2 (366-471) — 注意顺序: 先删后面的
lines = delete_lines(lines, 366, 471)

# 6. 删除 2.2.3.1 (184-365)
lines = delete_lines(lines, 184, 365)

# 7. 删除 2.2.3 (165-183)
lines = delete_lines(lines, 165, 183)

# 8. 重写 2.2.2 (110-164)
NEW_SECTION_222 = '''#### 2.2.2 罗盘显示系统 ⭐

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

**核心约束**：所有 UI 元素创建在 attitude_container_ 内 (4 层同心圆都在容器内), 太极图 r=44 不可与状态环 r=130 互相覆盖。

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
lines = replace_lines(lines, 110, 164, NEW_SECTION_222)


# 9. 重写 5.2/5.3 (Target2 实际坐标)
# 5.2 行号
start_52 = 110 + len(NEW_SECTION_222.split('\n'))  # 这是 2.2.2 末尾
# 找 5.2 的实际行号
# 由于前面的删除, 行号变了. 重新查找
content = ''.join(lines)
idx_52 = content.find("### 5.2 罗盘 UI 组件层级")
idx_53 = content.find("### 5.3 罗盘参数宏")
idx_54 = content.find("### 5.4")

# 算行号
def line_num(content, pos):
    return content[:pos].count('\n') + 1

ln_52 = line_num(content, idx_52)
ln_53 = line_num(content, idx_53)
ln_54 = line_num(content, idx_54)
print(f"  5.2 在行 {ln_52}, 5.3 在行 {ln_53}, 5.4 在行 {ln_54}")

# 重写 5.2 (从 ln_52 到 ln_53-1)
NEW_52 = '''### 5.2 罗盘 UI 组件层级（真实坐标 ⭐ Target2）

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
lines = replace_lines(lines, ln_52, ln_53 - 1, NEW_52)

# 重写 5.3
NEW_53 = '''### 5.3 罗盘参数宏（[attitude_display.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.h)）

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
lines = replace_lines(lines, ln_53, ln_54 - 1, NEW_53)


# 10. 简化 12 总结与下一步 (从 4.1/5.6/13 章删去 Target1 相关引用)
# 12 章在文件末尾附近
content = ''.join(lines)
idx_12 = content.find("## 12. 总结与下一步")
idx_doc_version = content.find("\n*文档版本:")
if idx_12 != -1 and idx_doc_version != -1:
    ln_12 = line_num(content, idx_12)
    ln_doc = line_num(content, idx_doc_version)
    print(f"  12 章在行 {ln_12}, 文档版本在行 {ln_doc}")

    NEW_12 = '''## 12. 总结与下一步

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
    lines = replace_lines(lines, ln_12, ln_doc - 1, NEW_12)


# 11. 删除 5.4 中残留的 AI 运势动画态速度参数
content = ''.join(lines)
idx_54 = content.find("### 5.4")
idx_55 = content.find("### 5.5")
if idx_54 != -1 and idx_55 != -1:
    anim_idx = content.find("**★ AI 运势引擎动画态速度参数", idx_54, idx_55)
    if anim_idx != -1:
        # 找这一段的结束: 下一个 "**★" 段
        next_star = content.find("**★", anim_idx + 50, idx_55)
        if next_star != -1:
            # 找上一行 \n
            ln_anim = line_num(content, anim_idx)
            ln_next = line_num(content, next_star)
            lines = delete_lines(lines, ln_anim, ln_next - 1)
            print("  ✓ 删除 5.4 中 AI 运势动画态参数")


# 12. 更新文档版本
content = ''.join(lines)
if "v1.2 (新增第 13 章功能验收清单" in content:
    content = content.replace(
        "v1.2 (新增第 13 章功能验收清单, 移除非实现/已废弃功能定义)",
        "v1.4 (Target1→Target2 文档对齐: 删除鱼眼/运势/结果卡/13 章幻觉内容, 重写 2.2.2/5.2/5.3/7/12)"
    )
    content = content.replace(
        "更新日期: 2026-06-13",
        "更新日期: 2026-06-14"
    )
    lines = content.split('\n')
    print("  ✓ 更新文档版本 v1.2 → v1.4")


# 保存
with open(SRC, 'w', encoding='utf-8') as f:
    f.writelines(lines)

print()
print(f"原始: 1792 行")
print(f"新:   {len(lines)} 行")
