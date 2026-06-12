# AI 姿态平衡仪 - UI 迭代开发计划

> **文档版本**: V2.0
> **定稿日期**: 2026-06-12
> **设计依据**: [ESP32-S3 360×360 圆形屏 · 太极风水罗盘 UI 设计文档](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/doc/ESP32-S3%20360%C3%97360%20%E5%9C%86%E5%BD%A2%E5%B1%8F%20%C2%B7%20%E5%A4%AA%E6%9E%81%E9%A3%8E%E6%B0%B4%E7%BD%97%E7%9B%98%20UI%20%E8%AE%BE%E8%AE%A1%E6%96%87%E6%A1%A3.md)
> **设计风格**: 太极黑金·鎏金极简国风
> **核心原则**: 外静内动、阴阳平衡、圆中有圆、以色表意

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
- **端口**: USB-Serial/JTAG (/dev/cu.usbmodem101)
- **波特率**: 115200
- **触发方式**: 设备启动 2 秒后自动触发，共 3 次，每次间隔 2 秒
- **图片大小**: 约 5-20 KB (JPEG 质量 80)

### 截图功能状态
| 项目 | 状态 |
|------|------|
| 编译 | ✅ 正常 |
| 烧录 | ✅ 正常 |
| 截图接收 | ✅ 正常 |
| 存档 | ✅ 已有 `screenshots/history/screenshot_20260612_*.jpg` |

---

## 🎨 设计规范速查

### 屏幕核心参数
- **分辨率**: 360×360（纯圆形）
- **圆心**: (180, 180)
- **有效显示半径**: 178px
- **统一动画时长**: 300ms
- **触控**: LVGL 圆形裁剪，自动屏蔽四角

### 同心圆四层布局系统
| 层级 | 半径范围 | 占比 | 功能 | 控件 |
|------|----------|------|------|------|
| **层级一** | 0~54px | 0%~30% | 核心信息区 | lv_label（标题、倾角、提示） |
| **层级二** | 54~90px | 30%~50% | 动态指示区 | lv_line（角度指示线、装饰线） |
| **层级三** | 90~144px | 50%~80% | 状态进度区 | lv_arc（环形进度）、辅助文字 |
| **层级四** | 144~178px | 80%~100% | 边界留白区 | **禁放控件**，仅极细鎏金外圆环 |

### 完整配色规范
```
背景色:
  COLOR_BG_OUTER    = #0A0A0A  外圈玄黑
  COLOR_BG_CENTER   = #121212  中心深邃黑

文本色:
  COLOR_TEXT_MAIN   = #D4AF37  鎏金黄（标题、核心数据）
  COLOR_TEXT_SUB    = #C0C0C0  银灰（辅助文字）
  COLOR_TEXT_HIGH   = #FFFFFF  纯白（高亮警告）

装饰色:
  COLOR_BORDER_LINE = #D4AF37  鎏金（边框、分割线）
  COLOR_CARD_BG     = #1A1A1A  暗黑金（卡片底色）
  COLOR_POINT_DOT   = #D4AF37  鎏金（方位圆点）

五档状态色:
  COLOR_STATE_NORMAL  = #2E5E4E  古玉青（平衡）
  COLOR_STATE_LIGHT   = #4A6FA5  青花蓝（轻微）
  COLOR_STATE_MID     = #D4AF37  鎏金黄（中度）
  COLOR_STATE_HEAVY   = #E67E22  赭石橙（较大）
  COLOR_STATE_DANGER  = #B82601  朱砂红（严重）
```

### 姿态五档分级标准
| 倾角范围 | 状态色 | 中文文案 | 英文文案 |
|----------|--------|----------|----------|
| < 2° | 古玉青 | 平衡稳态 | Balance OK |
| 2° ~ 8° | 青花蓝 | 轻微倾斜 | Slight Tilt |
| 8° ~ 15° | 鎏金黄 | 中度倾斜 | Medium Tilt |
| 15° ~ 25° | 赭石橙 | 较大倾斜 | Heavy Tilt |
| > 25° | 朱砂红 | 严重倾斜 | Danger Tilt |

### 方位标识
- **上 / 下 / 左 / 右** 四个绝对方位
- 使用 **6×6 实心圆点**（LV_RADIUS_CIRCLE）作为标记
- 默认色：鎏金黄 #D4AF37
- 静态固定，不随姿态变化
- 边距：距边缘 18px

---

## 📊 迭代进度追踪

| 迭代 | 状态 | 完成日期 | 备注 |
|------|------|----------|------|
| 迭代1: 项目基础框架 | ✅ 已完成 | 2026-06-10 | AttitudeDisplay基础框架 |
| 迭代12: 串口截图功能 | ✅ 已完成 | 2026-06-12 | USB-Serial/JTAG截图 + Python接收脚本 |
| **迭代2: 基础UI骨架重构** | 🔴 **下一个迭代** | - | 按设计规范重新构建UI骨架（4层布局） |
| 迭代3: 核心信息区（层一） | 🔄 待开始 | - | 标题/倾角/提示文字 |
| 迭代4: 动态指示区（层二） | 🔄 待开始 | - | 角度指示线、装饰细线 |
| 迭代5: 状态进度区（层三） | 🔄 待开始 | - | 环形进度环、五档变色 |
| 迭代6: 边界留白区（层四） | 🔄 待开始 | - | 1px鎏金外圆环边框 |
| 迭代7: 方位圆点标识 | 🔄 待开始 | - | 4个6×6实心圆点 |
| 迭代8: 主题与配色集成 | 🔄 待开始 | - | 标准色值宏定义 |
| 迭代9: IMU数据接入 | 🔄 待开始 | - | 陀螺仪驱动 + 数据流 |
| 迭代10: 状态分级联动 | 🔄 待开始 | - | 倾角→颜色/文案/进度 |
| 迭代11: 动效与性能优化 | 🔄 待开始 | - | 300ms统一动画 |

---

## 🎯 详细迭代计划

### 🔴 迭代 2: 基础UI骨架重构（2-3天） 【下一个迭代】

**目标**: 抛弃旧UI布局，按设计文档规范重建 4 层同心圆布局系统

**关键文件**:
- `main/display/attitude_display.h` - 类定义重构
- `main/display/attitude_display.cc` - 实现文件
- `main/display/lcd_display.h` - 基类接口

**技术要点**:
- 删除旧的"装饰圆 + 顶部信息栏 + 底部解读区"布局
- 重建为"四层同心圆"布局：核心信息区 / 动态指示区 / 状态进度区 / 边界留白区
- 集成标准色值宏定义（COLOR_BG_OUTER 等）
- 屏幕中心对齐：所有元素基于 (180, 180) 圆心对称
- LVGL 圆形裁剪开启（屏蔽四角）

**交付物**:
- AttitudeDisplay 类重构后的 4 层布局
- 标准色值宏定义
- 编译烧录后屏幕显示空白（仅背景色 + 外圆环）

**详细步骤**:
1. 在头文件添加标准色值宏定义
2. 重构 `SetupUI()` 方法，按设计文档的 4 层布局调用子方法
3. 创建 `CreateBackground()` - 玄黑径向渐变背景
4. 创建 `CreateCompassFrame()` - 1px 鎏金外圆环（半径178）
5. 移除旧的 `CreateTopInfoRing`、`CreateBottomInterpretation`、`CreateDecorationCircles`
6. 编译烧录验证（应只看到纯黑背景 + 鎏金外圆环）

**验收标准**:
- ✅ 屏幕显示纯黑背景
- ✅ 边缘有 1px 鎏金细圆环
- ✅ 屏幕外四角的元素（如有）被裁剪不可见
- ✅ 编译无错误
- ✅ 无内存泄漏

---

### 迭代 3: 核心信息区 - 层级一（3-4天）

**目标**: 实现中心核心信息区（0~54px 半径范围）

**关键文件**:
- `main/display/attitude_display.cc`

**技术要点**:
- 三层文本布局：主标题 / 副标题 / 倾角数值
- 使用 LVGL lv_label 控件
- 字体规范：主标题用大字号（lv_font_montserrat_20），副标题用中字号（lv_font_montserrat_16）
- 居中对齐于 (180, 180)

**交付物**:
- 主标题"姿态平衡仪" - 鎏金黄 #D4AF37
- 副标题"Balance OK" - 银灰 #C0C0C0
- 倾角数值"0.00°" - 银灰 #C0C0C0

**详细步骤**:
1. 创建 `CreateCoreInfoArea()` 方法
2. 添加 `ui_main_text_` 主标题 label
3. 添加 `ui_sub_text_` 副标题 label
4. 添加 `ui_angle_value_` 倾角数值 label
5. 垂直排列：主标题上、副标题中、倾角下
6. 编译烧录验证

**验收标准**:
- ✅ 中心区域显示三行文本
- ✅ 主标题鎏金黄、副标题银灰
- ✅ 文本不重叠，居中对齐
- ✅ 文字清晰可读，不超出半径 54px 范围

---

### 迭代 4: 动态指示区 - 层级二（3-4天）

**目标**: 实现动态指示区（54~90px 半径范围）

**关键文件**:
- `main/display/attitude_display.cc`

**技术要点**:
- 使用 lv_line 绘制角度指示线
- 内圈装饰细线（半径 ~60px 鎏金细线）
- 动态指示线实时跟随陀螺仪角度旋转
- 纯扁平化、纯色绘制

**交付物**:
- 内圈装饰细线（半径60px，1px宽，鎏金 #D4AF37）
- 动态角度指示线（从圆心向外，长度36px）
- 装饰线 4 个方位上的小刻度标记

**详细步骤**:
1. 创建 `CreateDynamicIndicatorArea()` 方法
2. 使用 lv_line 绘制半径60的圆弧装饰
3. 绘制中心向外的指示线段
4. 添加 8 个方位小刻度点
5. 编译烧录验证

**验收标准**:
- ✅ 在 54~90px 半径范围内有装饰线和指示线
- ✅ 线条为鎏金纯色扁平
- ✅ 不超出层级二范围
- ✅ 静态布局正确

---

### 迭代 5: 状态进度区 - 层级三（4-5天）

**目标**: 实现状态进度区（90~144px 半径范围）

**关键文件**:
- `main/display/attitude_display.cc`

**技术要点**:
- 使用 lv_arc 绘制环形进度环
- 半径约 120px（直径 240），宽度 4px
- 进度环动画时长固定 300ms
- 端点圆角、线条粗细统一
- 五档状态色动态切换

**交付物**:
- 外层姿态进度环（背景环 + 进度环）
- 进度环颜色根据倾角五档分级

**详细步骤**:
1. 创建 `CreateStatusProgressArea()` 方法
2. 添加 `ui_state_arc_` 状态进度环（lv_arc）
3. 设置背景环颜色为 #1A1A1A
4. 设置指示环颜色默认为 #2E5E4E（平衡态）
5. 实现 `UpdateStateArcColor(color)` 方法
6. 编译烧录验证

**验收标准**:
- ✅ 在 90~144px 范围内有进度环
- ✅ 进度环宽度一致（4px）
- ✅ 端点圆角
- ✅ 颜色切换接口已实现

---

### 迭代 6: 边界留白区 - 层级四（1-2天）

**目标**: 实现边界留白区（144~178px 半径范围）

**关键文件**:
- `main/display/attitude_display.cc`

**技术要点**:
- 极细鎏金外圆环边框（1px 宽，#D4AF37）
- 半径 178px（紧贴屏幕边缘）
- 不放置任何其他 UI 控件
- 符合"呼吸留白"设计理念

**交付物**:
- 1px 鎏金外圆环边框

**详细步骤**:
1. 创建 `CreateBoundaryArea()` 方法
2. 绘制半径 178px 的圆环（1px 宽）
3. 或使用 lv_obj 配合 border 属性
4. 编译烧录验证

**验收标准**:
- ✅ 屏幕边缘有 1px 鎏金圆环
- ✅ 环线粗细一致
- ✅ 144~178px 范围内无其他控件
- ✅ 外环与屏幕物理边缘完全贴合

---

### 迭代 7: 方位圆点标识（1-2天）

**目标**: 添加 4 个方位的实心圆点标识

**关键文件**:
- `main/display/attitude_display.cc`

**技术要点**:
- 4 个 6×6 的实心圆点（lv_obj + LV_RADIUS_CIRCLE）
- 位置：上 / 下 / 左 / 右 4 个绝对方位
- 边距 18px（距屏幕边缘）
- 颜色：鎏金黄 #D4AF37
- 静态固定，不随姿态变化

**交付物**:
- 上方位圆点 (180, 18)
- 下方位圆点 (180, 342)
- 左方位圆点 (18, 180)
- 右方位圆点 (342, 180)

**详细步骤**:
1. 创建 `CreateCompassPoints()` 方法
2. 创建 4 个 lv_obj 圆点
3. 使用 lv_obj_align 定位到 4 个方位
4. 设置背景色为 #D4AF37
5. 编译烧录验证

**验收标准**:
- ✅ 4 个方位圆点位置正确
- ✅ 圆点大小一致（6×6）
- ✅ 圆点颜色为鎏金黄
- ✅ 圆点不随 UI 变化

---

### 迭代 8: 主题与配色集成（2-3天）

**目标**: 集成所有标准色值宏定义，建立统一主题系统

**关键文件**:
- `main/display/attitude_display.h` - 添加色值宏定义
- `main/display/attitude_display.cc` - 引用色值
- 新建 `main/display/attitude_theme.h` - 主题封装

**技术要点**:
- 集中管理所有色值（与设计文档 1:1 对应）
- 命名规范：COLOR_BG_OUTER / COLOR_TEXT_MAIN / COLOR_STATE_NORMAL 等
- 支持主题切换（深色 / 浅色 / 自定义）

**交付物**:
- 完整色值宏定义头文件
- AttitudeTheme 主题类
- 编译烧录验证

**验收标准**:
- ✅ 所有色值与设计文档完全一致
- ✅ 主题切换接口可用
- ✅ 代码中无散落的硬编码颜色

---

### 迭代 9: IMU 数据接入（3-4天）

**目标**: 集成 QMI8658 陀螺仪驱动，实时获取姿态数据

**关键文件**:
- `main/imu/qmi8658.h` / `qmi8658.cc` - IMU 驱动
- `main/display/attitude_display.cc` - 接收数据

**技术要点**:
- I2C 通信读取加速度计和陀螺仪原始数据
- 互补滤波或 Mahony 算法融合计算 Pitch / Roll / Yaw
- 50Hz 数据更新频率
- 共享数据结构 + 互斥锁保护

**交付物**:
- QMI8658 驱动初始化和数据读取
- 姿态解算模块
- AttitudeDisplay::SetAttitudeData() 接口对接

**验收标准**:
- ✅ IMU 初始化成功
- ✅ 50Hz 数据更新
- ✅ 静止时 Pitch/Roll 接近 0°
- ✅ 倾角变化时数据平滑跟随

---

### 迭代 10: 状态分级联动（4-5天）

**目标**: 实现倾角→状态色/文案/进度的完整联动

**关键文件**:
- `main/display/attitude_display.cc`

**技术要点**:
- 五档分级逻辑（与设计文档 9 节一致）
- 自动更新进度环颜色
- 自动更新中心文本
- 自动更新进度环角度（0~360° 映射）
- 300ms 平滑过渡动画

**交付物**:
- `UpdateCompassState(pitch, roll)` 核心方法
- 状态切换时 300ms 动画
- 完整五档分级演示

**验收标准**:
- ✅ 倾角 < 2° 显示"Balance OK"古玉青
- ✅ 2-8° 显示"Slight Tilt"青花蓝
- ✅ 8-15° 显示"Medium Tilt"鎏金黄
- ✅ 15-25° 显示"Heavy Tilt"赭石橙
- ✅ > 25° 显示"Danger Tilt"朱砂红
- ✅ 颜色切换有 300ms 过渡

---

### 迭代 11: 动效与性能优化（2-3天）

**目标**: 优化动画流畅度，确保 30FPS+ 渲染

**关键文件**:
- `main/display/attitude_display.cc`
- `main/display/snapshot/snapshot_service.cc`

**技术要点**:
- 角度平滑插值（避免跳变）
- 颜色过渡动画统一 300ms
- 帧率监控与优化
- 内存使用优化
- LVGL task 优先级调整

**交付物**:
- 平滑的角度插值算法
- 颜色淡入淡出动画
- 性能监控日志

**验收标准**:
- ✅ 动画流畅无卡顿（30FPS+）
- ✅ 颜色切换平滑无闪烁
- ✅ 角度变化平滑无跳变
- ✅ 内存占用稳定

---

## 📁 项目文件结构

```
xiaozhi-esp32/
├── main/
│   ├── display/
│   │   ├── attitude_display.h           ← AttitudeDisplay 类定义
│   │   ├── attitude_display.cc          ← 4层同心圆布局实现
│   │   ├── attitude_theme.h             ← 主题与色值宏定义
│   │   └── snapshot/                    ← 截图服务
│   ├── imu/
│   │   ├── qmi8658.h                    ← IMU 驱动
│   │   └── qmi8658.cc
│   └── main.cc                          ← 入口（含截图任务）
├── scripts/
│   ├── save_screenshot.py               ← 截图接收脚本
│   └── receive_screenshot.py            ← 截图接收脚本（备选）
└── doc/
    ├── ESP32-S3 360×360 圆形屏 · 太极风水罗盘 UI 设计文档.md   ← 设计规范
    ├── ATTITUDE_DISPLAY_ITERATION_PLAN.md                       ← 迭代计划
    ├── ATTITUDE_DISPLAY_ACCEPTANCE_CRITERIA.md                  ← 验收标准
    └── SNAPSHOT_USAGE.md                                        ← 截图使用说明
```

---

## 🎯 下一步行动

**立即开始迭代 2: 基础UI骨架重构**

按设计文档规范重建 4 层同心圆布局系统，删除旧UI元素，集成标准色值宏定义。
