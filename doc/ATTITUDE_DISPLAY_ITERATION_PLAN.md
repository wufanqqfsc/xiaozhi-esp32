# AI 姿态平衡仪 - UI 迭代开发计划 (V3.0)

> **文档版本**: V3.0 (Target-aligned)
> **定稿日期**: 2026-06-12
> **目标设计**: [target.png](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/doc/target.png) - 传统太极风水罗盘
> **原设计文档**: [UI 设计文档 (极简版)](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/doc/ESP32-S3%20360%C3%97360%20%E5%9C%86%E5%BD%A2%E5%B1%8F%20%C2%B7%20%E5%A4%AA%E6%9E%81%E9%A3%8E%E6%B0%B4%E7%BD%97%E7%9B%98%20UI%20%E8%AE%BE%E8%AE%A1%E6%96%87%E6%A1%A3.md) (已废弃)
> **设计风格**: 传统太极风水罗盘·黑金国风
> **核心原则**: 信息密集、文化底蕴、多层罗盘结构

---

## ⚠️ 重要变更

**V3.0 重大变更**：
- **废弃** 原"极简 4 层同心圆"设计文档
- **采用** target.png 传统罗盘设计
- **保留** 截图功能、所有底层架构、主题系统
- **重构** UI 实现部分

---

## 📸 重要提示：截图功能（迭代 12）已保留并可用

> **⚠️ 所有后续 UI 迭代都依赖截图功能进行 UI 验证，请勿删除或禁用！**

### 截图功能快速使用

```bash
# 1. 烧录固件
./build_and_flash.sh flash

# 2. 获取截图（自动保存到 screenshots/ 目录）
python3 scripts/save_screenshot.py

# 3. 查看截图
open screenshots/screenshot.jpg
```

### 截图相关文件（全部保留）

| 文件 | 用途 |
|------|------|
| [main/display/snapshot/snapshot_service.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/snapshot/snapshot_service.cc) | ESP32 端截图服务 |
| [main/display/snapshot/snapshot_service.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/snapshot/snapshot_service.h) | 截图服务头文件 |
| [main/display/snapshot/snapshot_protocol.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/snapshot/snapshot_protocol.cc) | 串口协议实现 |
| [main/display/snapshot/snapshot_protocol.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/snapshot/snapshot_protocol.h) | 协议定义 |
| [main/main.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/main.cc) | 含截图任务入口 |
| [scripts/save_screenshot.py](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/scripts/save_screenshot.py) | **推荐使用** 截图接收脚本 |
| [scripts/receive_screenshot.py](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/scripts/receive_screenshot.py) | 备选脚本 |
| [scripts/screenshot_client.py](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/scripts/screenshot_client.py) | MCP 客户端脚本 |
| [tools/snapshot_receiver.py](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/tools/snapshot_receiver.py) | 工具脚本 |
| [doc/SNAPSHOT_USAGE.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/doc/SNAPSHOT_USAGE.md) | **完整使用文档** |
| `screenshots/` | 截图保存目录 |
| `screenshots/history/` | 历史截图归档 |

### 截图输出格式
```
===SCREENSHOT_START===
<Base64 编码的 JPEG 数据>
===SCREENSHOT_END===
```

### 关键参数
- **端口**: USB-Serial/JTAG (/dev/cu.usbmodem1101)
- **波特率**: 115200
- **触发方式**: 设备启动 2 秒后自动触发，共 3 次，每次间隔 2 秒
- **图片大小**: 约 5-20 KB (JPEG 质量 80)

---

## 🎯 Target.png 设计规范

### 罗盘层级结构（从内向外）

| 层级 | 半径范围 | 占比 | 内容 | 设计要求 |
|------|----------|------|------|----------|
| **中心** | 0~80px | 0~22% | **太极图**（阴阳鱼+两点） | 高亮发光，黑白对撞 |
| **第一圈** | 80~120px | 22~33% | 圆形高亮圈 + 装饰 | 鎏金细线 |
| **第二圈** | 120~200px | 33~56% | **64 卦符号** + **12 地支** | 白银 + 鎏金文字 |
| **第三圈** | 200~280px | 56~78% | **天干** + **地支** | 鎏金大字 |
| **第四圈** | 280~340px | 78~94% | **12 地支**（外圈） | 鎏金小字 |
| **最外圈** | 340~360px | 94~100% | **360 刻度** + **方位标识** | 极细鎏金刻度 |

### 配色规范（沿用 V2.0 黑金国风）

| 元素 | 颜色 |
|------|------|
| **背景** | 玄黑 #0A0A0A → 深邃黑 #121212 径向渐变 |
| **卦象** | 白银 #C0C0C0 / 鎏金 #D4AF37 |
| **天干地支** | 鎏金 #D4AF37 |
| **360 刻度** | 鎏金 #D4AF37 (细) |
| **方位标识** | 鎏金 #D4AF37 (菱形 ♦) |
| **中心高亮** | 纯白 #FFFFFF (太极阳鱼) + 玄黑 #0A0A0A (阴鱼) |

### 字体规范

| 元素 | 字号 |
|------|------|
| 卦象符号 | 16-20px |
| 12 地支 | 14-18px |
| 天干 | 16-20px |
| 360 刻度数字 | 8-10px (每个30°显示) |

---

## 📊 迭代进度追踪 (V3.0)

| 迭代 | 状态 | 完成日期 | 备注 |
|------|------|----------|------|
| 迭代1: 项目基础框架 | ✅ 已完成 | 2026-06-10 | AttitudeDisplay基础框架 |
| 迭代12: 串口截图功能 | ✅ 已完成 | 2026-06-12 | USB-Serial/JTAG截图 + Python接收脚本 |
| 迭代2: V2.0 极简4层骨架 | ✅ 已完成 (V2.0) | 2026-06-12 | V2.0 极简骨架 (与 target.png 不匹配，**待重构**) |
| **迭代13: Target 中心太极图** | ✅ **已完成** | **2026-06-12** | **中心太极图（阴阳鱼）已实现并验证** |
| **迭代14a: 清理 + 太极图旋转** | ✅ **已完成** | **2026-06-12** | **修复矩形边框/鎏金1px/自动旋转1分钟/圈** |
| **迭代14b: 64 卦符号层** | ✅ **已完成** | **2026-06-12** | **64 卦排布 (r=140) - 但 target.png 中 r=60-72，位置需调整** |
| **迭代15: 64 卦位置内移 + 太极图缩小** | ✅ **已完成** | **2026-06-12** | **64 卦 r=70, 太极图 r=48 (60%)** |
| **迭代16: 12 地支大字** | ✅ **已完成** | **2026-06-12** | **子丑寅... r=110 (鎏金大字)** |
| **迭代17: 天干** | ✅ **已完成** | **2026-06-12** | **甲乙丙... r=140 (鎏金小字)** |
| **迭代18: 密度梯度重构** | ✅ **已完成** | **2026-06-12** | **由内到外密度由低到高 (1→4→10→12→64)** |
| ~~迭代19: 4 方位黑色标牌~~ | ❌ **已移除** | - | ~~N/E/S/W 黑色梯形 + 鎏金文字 "天/地/字/宙"~~ (难度太高) |
| ~~迭代20: 360 极细刻度 + 4 大菱形~~ | ❌ **已移除** | - | ~~最外圈 360 刻度 + 4 大菱形方位标识~~ (难度太高) |
| 迭代21: 24 节气数字 | 🔄 待开始 | - | 内圈 1-24 节气数字 (低难度) |
| 迭代22: 8 节文字 | 🔄 待开始 | - | 立春/雨水... 8 个中文 label (中等难度) |
| 迭代9: IMU数据接入 | 🔄 待开始 | - | 陀螺仪驱动 + 数据流 |
| 迭代10: 状态分级联动 | 🔄 待开始 | - | 倾角→颜色/文案/进度 |
| 迭代11: 动效与性能优化 | 🔄 待开始 | - | 300ms统一动画 |

---

## 🎯 详细迭代计划 (V3.0 Target-aligned)

### ✅ 迭代 13: Target 中心太极图（已完成 2026-06-12）

**目标**: 在屏幕中心绘制传统太极图（阴阳鱼），是 target.png 的核心视觉

**关键文件**:
- [main/display/attitude_display.h/cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.h) - AttitudeDisplay 类
- [main/display/compass_taiji.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/compass_taiji.h) - 太极图模块头 (新)
- [main/display/compass_taiji.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/compass_taiji.cc) - 太极图模块实现 (新)

**技术要点**:
- LVGL `lv_canvas` 绘制 360x360 全图
- 优化算法: 扫描线填充圆 + 中点圆算法
- 阴阳鱼由两个半圆（黑/白）+ 两个小圆（黑/白点）组成
- 外圈鎏金高亮环 + 半透明发光环

**交付物**:
- ✅ 中心太极图（直径 160px，半径 80px）
- ✅ 黑色阴鱼（带白点）
- ✅ 白色阳鱼（带黑点）
- ✅ S 形分界线
- ✅ 鎏金外圈高亮环
- ✅ 半透明鎏金发光环

**绘制步骤**:
1. ✅ 创建 `compass_taiji.cc/h` 模块
2. ✅ 实现 `CompassTaiji::Create(parent, cx, cy, radius)` 静态方法
3. ✅ 使用 `lv_canvas` + `FillCircle()` 辅助函数绘制阴阳鱼
4. ✅ 添加鎏金外圈高亮环 + 发光环
5. ✅ 在 `AttitudeDisplay::SetupUI()` 中调用 `CreateLayer0Taiji()`

**验收结果** (基于 `screenshots/screenshot.jpg` 像素分析):
- ✅ 屏幕中心显示完整的太极图（半径 80px）
- ✅ 阴鱼（黑）+ 阳鱼（白）S 形对撞（中心 30px 半径采样 12 个角度黑白各半）
- ✅ 阳中黑点位于上方中央（"BBBBB" 区域确认）
- ✅ 阴中白点位于下方中央（"WWWWW" 区域确认）
- ✅ S 形分界线平滑（字符图清晰可见黑白对撞）
- ✅ 外圆环鎏金黄 (#BCAF3D)
- ✅ 背景 88.8% 黑色（玄黑）
- ⚠️ 发光环效果不明显（需要迭代 18 进一步增强）

**截图证据**:
- 文件: `screenshots/screenshot.jpg`
- 尺寸: 360x360 JPEG
- 大小: 15095 字节

**已知问题**:
- 鎏金外环只有 1 像素可见（其他被压缩），需要后续增强
- 中心发光环效果不够明显（迭代 18 处理）

**Git 提交**:
- Commit: `4cd0a79` (feat(迭代13): Target 中心太极图 + 迭代计划 V3.0)
- 14 个文件变更 (+806 / -770)

---

### 迭代 14: 64 卦符号层（4-5天）

**目标**: 在太极图外圈绘制 64 卦符号

**技术要点**:
- 64 卦由 6 个爻（⚊ ⚋）组成，分为八卦
- 使用 UTF-8 字符：☰☱☲☳☴☵☶☷
- 沿圆周均匀分布 64 个卦象（每 5.625° 一个）
- 字体：lv_font_montserrat_14

**交付物**:
- 64 个卦象符号排布在半径 120~180px 圆周上
- 8 个主卦（八卦）用鎏金 #D4AF37
- 56 个变卦用白银 #C0C0C0

---

### 迭代 15: 12 地支层（2-3天）

**目标**: 添加 12 地支（子丑寅卯辰巳午未申酉戌亥）

**技术要点**:
- 12 个地支沿圆周均匀分布（每 30° 一个）
- 子（北）、卯（东）、午（南）、酉（西）为四正
- 字体：lv_font_montserrat_16，鎏金 #D4AF37
- 半径 200~240px

---

### 迭代 16: 天干 + 外圈 12 地支（2-3天）

**目标**: 添加天干（甲乙丙丁戊己庚辛壬癸）+ 外圈 12 地支

**技术要点**:
- 10 个天干沿圆周均匀分布（每 36° 一个）
- 字体大号：lv_font_montserrat_20
- 外圈 12 地支使用小号字体
- 鎏金 #D4AF37

---

### 迭代 17: 360 刻度 + 方位标识（3-4天）

**目标**: 添加 360° 极细刻度 + 4 个方位菱形 ♦

**技术要点**:
- 360 条短刻度线（每 1° 一条）
- 每 30° 加粗刻度
- 每 90° 显示方位数字（90/180/270/360）
- 4 个方位的菱形 ♦ 标识（北/东/南/西）
- 半径 340~360px

---

### 迭代 18: 中心高亮发光（1-2天）

**目标**: 为太极图添加发光效果

**技术要点**:
- 太极图外圈添加半透明鎏金圆环（rgba 50%）
- 使用 lv_obj_set_style_shadow 模拟发光
- 圆心白色高亮点

---

## 📁 项目文件结构

```
xiaozhi-esp32/
├── main/
│   ├── display/
│   │   ├── attitude_display.h           ← AttitudeDisplay 类定义
│   │   ├── attitude_display.cc          ← SetupUI + 4层布局
│   │   ├── attitude_theme.h             ← 主题与色值宏定义
│   │   ├── attitude_theme.cc
│   │   ├── compass_taiji.h              ← 迭代13: 太极图模块 (新)
│   │   ├── compass_taiji.cc             ← 迭代13: 太极图实现 (新)
│   │   ├── compass_bagua.h              ← 迭代14: 64卦模块 (新)
│   │   ├── compass_bagua.cc
│   │   ├── compass_dizhi.h              ← 迭代15: 12地支 (新)
│   │   ├── compass_dizhi.cc
│   │   ├── compass_tiangan.h            ← 迭代16: 天干 (新)
│   │   ├── compass_tiangan.cc
│   │   ├── compass_scale.h              ← 迭代17: 360刻度 (新)
│   │   ├── compass_scale.cc
│   │   └── snapshot/                    ← 截图服务
│   ├── imu/
│   │   ├── qmi8658.h                    ← IMU 驱动
│   │   └── qmi8658.cc
│   └── main.cc                          ← 入口（含截图任务）
├── scripts/
│   ├── save_screenshot.py               ← 截图接收脚本
│   └── receive_screenshot.py            ← 截图接收脚本（备选）
├── doc/
│   ├── target.png                       ← 最终目标设计 (2048x2048)
│   ├── ATTITUDE_DISPLAY_ITERATION_PLAN.md   ← 本文档 (V3.0)
│   ├── ATTITUDE_DISPLAY_ACCEPTANCE_CRITERIA.md  ← 验收标准
│   └── SNAPSHOT_USAGE.md                ← 截图使用说明
└── screenshots/                         ← 截图存档
    ├── screenshot.jpg
    ├── history/
    └── logs/
```

---

## 🎯 下一步行动

**立即开始迭代 14: 64 卦符号层**

在太极图外圈绘制 64 卦符号（每 5.625° 一个），与 target.png 中圈的 64 卦符号层对应。

### 迭代 14 准备清单

| 任务 | 文件 |
|------|------|
| 创建模块 | `main/display/compass_bagua.h` (新) |
| 实现模块 | `main/display/compass_bagua.cc` (新) |
| 注册到 CMake | `main/CMakeLists.txt` |
| 在 SetupUI 调用 | `main/display/attitude_display.cc` |
| 64 卦符号表 | 8 卦 + 56 变卦 (UTF-8 字符) |
| 半径范围 | 120~180px (太极图外圈) |
| 颜色 | 8 主卦=鎏金, 56 变卦=白银 |
