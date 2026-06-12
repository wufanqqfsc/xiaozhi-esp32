# AI 姿态平衡仪 - 迭代验收标准

> **文档版本**: V2.0
> **定稿日期**: 2026-06-12
> **配套文档**: [ATTITUDE_DISPLAY_ITERATION_PLAN.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/doc/ATTITUDE_DISPLAY_ITERATION_PLAN.md)
> **设计依据**: [UI 设计文档](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/doc/ESP32-S3%20360%C3%97360%20%E5%9C%86%E5%BD%A2%E5%B1%8F%20%C2%B7%20%E5%A4%AA%E6%9E%81%E9%A3%8E%E6%B0%B4%E7%BD%97%E7%9B%98%20UI%20%E8%AE%BE%E8%AE%A1%E6%96%87%E6%A1%A3.md)

---

## 📊 迭代优先级追踪

| 迭代 | 状态 | 完成日期 | 验收人 | 备注 |
|------|------|----------|--------|------|
| 迭代1: 项目基础框架 | ✅ 已完成 | 2026-06-10 | - | AttitudeDisplay基础框架 |
| 迭代12: 串口截图功能 | ✅ 已完成 | 2026-06-12 | - | USB-Serial/JTAG截图 + Python接收脚本 |
| **迭代2: 基础UI骨架重构** | 🔴 **下一个迭代** | - | - | **按设计规范重建4层布局** |
| 迭代3: 核心信息区（层一） | 🔄 待开始 | - | - | - |
| 迭代4: 动态指示区（层二） | 🔄 待开始 | - | - | - |
| 迭代5: 状态进度区（层三） | 🔄 待开始 | - | - | - |
| 迭代6: 边界留白区（层四） | 🔄 待开始 | - | - | - |
| 迭代7: 方位圆点标识 | 🔄 待开始 | - | - | - |
| 迭代8: 主题与配色集成 | 🔄 待开始 | - | - | - |
| 迭代9: IMU数据接入 | 🔄 待开始 | - | - | - |
| 迭代10: 状态分级联动 | 🔄 待开始 | - | - | - |
| 迭代11: 动效与性能优化 | 🔄 待开始 | - | - | - |

---

## 📋 通用前置条件（每个迭代前必做）

| 步骤 | 操作 | 预期 |
|------|------|------|
| 0-1 | ESP-IDF 环境检查：`echo $IDF_PATH` | 有输出 |
| 0-2 | 设备连接检查：`ls /dev/cu.usbmodem*` | 至少 1 个设备 |
| 0-3 | 串口空闲检查：关闭 monitor | 端口可用 |
| 0-4 | 编译命令：`./build_and_flash.sh build` | 编译通过 |
| 0-5 | 烧录命令：`./build_and_flash.sh flash` | 烧录成功 |
| 0-6 | 截图脚本：`python3 scripts/save_screenshot.py` | 生成 screenshot.jpg |

---

## 🎯 迭代 2: 基础UI骨架重构（2-3天）

**目标**: 按 UI 设计文档规范，重建 4 层同心圆布局系统

### I2-1: 标准色值宏定义

| 字段 | 内容 |
|------|------|
| **操作步骤** | 检查 `attitude_display.h` 中色值宏定义 |
| **预期结果** | 包含 COLOR_BG_OUTER (#0A0A0A)、COLOR_BG_CENTER (#121212)、COLOR_TEXT_MAIN (#D4AF37)、COLOR_BORDER_LINE (#D4AF37) 等所有标准色值 |
| **失败判定** | 缺失任何色值宏；色值与设计文档不一致 |
| **回归风险** | 所有 UI 元素颜色显示错误 |

### I2-2: 玄黑径向渐变背景

| 字段 | 内容 |
|------|------|
| **操作步骤** | 编译烧录后用 `python3 scripts/save_screenshot.py` 截图查看 |
| **预期结果** | 屏幕显示外圈 #0A0A0A → 中心 #121212 的径向渐变 |
| **失败判定** | 背景为纯色无渐变；颜色与规范不符 |
| **回归风险** | 视觉风格偏离黑金主题 |

### I2-3: 1px 鎏金外圆环边框

| 字段 | 内容 |
|------|------|
| **操作步骤** | 截图后查看屏幕边缘 |
| **预期结果** | 边缘有 1px 鎏金 #D4AF37 圆环，半径 178px |
| **失败判定** | 无圆环；圆环粗细不均；颜色错误 |
| **回归风险** | 失去边界留白区的"呼吸感" |

### I2-4: 四层同心圆布局

| 字段 | 内容 |
|------|------|
| **操作步骤** | 在 `SetupUI()` 中确认子方法调用顺序 |
| **预期结果** | 按层级一→二→三→四顺序创建：核心信息区、动态指示区、状态进度区、边界留白区 |
| **失败判定** | 层级顺序错误；控件超出层级范围 |
| **回归风险** | 视觉层级混乱 |

### I2-5: 旧UI元素已移除

| 字段 | 内容 |
|------|------|
| **操作步骤** | `grep -n "CreateTopInfoRing\|CreateBottomInterpretation\|CreateDecorationCircles" main/display/attitude_display.cc` |
| **预期结果** | 无输出（旧方法已删除） |
| **失败判定** | 仍存在旧 UI 方法 |
| **回归风险** | 代码冗余，UI 风格冲突 |

### I2-6: 圆形屏幕裁剪

| 字段 | 内容 |
|------|------|
| **操作步骤** | 检查 `lv_disp_set_round` 是否启用 |
| **预期结果** | 屏幕四角被裁剪，元素超出圆外不可见 |
| **失败判定** | 四角矩形元素仍可见 |
| **回归风险** | 圆形屏显示溢出 |

### I2-7: 编译无错误

| 字段 | 内容 |
|------|------|
| **操作步骤** | `./build_and_flash.sh build` |
| **预期结果** | 编译通过，无 error |
| **失败判定** | 编译失败；存在未定义引用 |
| **回归风险** | 无法烧录 |

### I2-8: 内存稳定

| 字段 | 内容 |
|------|------|
| **操作步骤** | 设备运行 5 分钟后查看 `free sram` 日志 |
| **预期结果** | SRAM 稳定在 100KB+ free |
| **失败判定** | 内存持续下降；< 50KB free |
| **回归风险** | 长时间运行崩溃 |

### I2-9: 烧录与重启

| 字段 | 内容 |
|------|------|
| **操作步骤** | `./build_and_flash.sh flash` |
| **预期结果** | 烧录成功，设备自动重启 |
| **失败判定** | 烧录失败；设备无响应 |
| **回归风险** | 无法验证 UI |

### I2-10: 截图保存路径

| 字段 | 内容 |
|------|------|
| **操作步骤** | `python3 scripts/save_screenshot.py` 后 `ls -la screenshots/` |
| **预期结果** | 生成 `screenshots/screenshot.jpg`，大小 5-20KB |
| **失败判定** | 文件未生成；< 1KB |
| **回归风险** | 无法进行 UI 验证 |

---

## 🎯 迭代 3-11: 验收标准（精简版）

### 迭代 3: 核心信息区（层一）

| 编号 | 验收项 | 预期 |
|------|--------|------|
| I3-1 | 主标题"姿态平衡仪" | 鎏金 #D4AF37，大字号，居中 |
| I3-2 | 副标题"Balance OK" | 银灰 #C0C0C0，中字号，居中 |
| I3-3 | 倾角数值"0.00°" | 银灰 #C0C0C0，居中 |
| I3-4 | 三行文本不重叠 | 垂直排列清晰 |
| I3-5 | 文本在 0~54px 半径内 | 不超出层级一范围 |

### 迭代 4: 动态指示区（层二）

| 编号 | 验收项 | 预期 |
|------|--------|------|
| I4-1 | 内圈装饰细线 | 半径 60px，1px 鎏金 |
| I4-2 | 中心指示线段 | 从圆心向外 36px |
| I4-3 | 8 个方位刻度点 | 鎏金小点 |
| I4-4 | 元素在 54~90px 内 | 不超出层级二范围 |
| I4-5 | 纯扁平化无渐变 | 符合设计规范 |

### 迭代 5: 状态进度区（层三）

| 编号 | 验收项 | 预期 |
|------|--------|------|
| I5-1 | 背景环 | 颜色 #1A1A1A，半径 ~120px |
| I5-2 | 进度环宽度 | 4px，端点圆角 |
| I5-3 | 进度环默认色 | 古玉青 #2E5E4E |
| I5-4 | UpdateStateArcColor 接口 | 可动态更新颜色 |
| I5-5 | 元素在 90~144px 内 | 不超出层级三范围 |

### 迭代 6: 边界留白区（层四）

| 编号 | 验收项 | 预期 |
|------|--------|------|
| I6-1 | 1px 鎏金外圆环 | 颜色 #D4AF37，半径 178px |
| I6-2 | 144~178px 范围无其他控件 | 符合"呼吸留白" |
| I6-3 | 圆环与屏幕边缘贴合 | 误差 < 2px |
| I6-4 | 圆环粗细均匀 | 全周一致 |

### 迭代 7: 方位圆点标识

| 编号 | 验收项 | 预期 |
|------|--------|------|
| I7-1 | 上方位圆点 | 位置 (180, 18)，6×6 鎏金 |
| I7-2 | 下方位圆点 | 位置 (180, 342)，6×6 鎏金 |
| I7-3 | 左方位圆点 | 位置 (18, 180)，6×6 鎏金 |
| I7-4 | 右方位圆点 | 位置 (342, 180)，6×6 鎏金 |
| I7-5 | 圆点静态固定 | 不随姿态变化 |

### 迭代 8: 主题与配色集成

| 编号 | 验收项 | 预期 |
|------|--------|------|
| I8-1 | 所有色值宏已定义 | 18 个色值与设计文档一致 |
| I8-2 | 无散落硬编码颜色 | `grep "lv_color_hex"` 全部使用宏 |
| I8-3 | AttitudeTheme 类可用 | 支持主题切换 |
| I8-4 | 编译通过 | 无未定义引用 |

### 迭代 9: IMU 数据接入

| 编号 | 验收项 | 预期 |
|------|--------|------|
| I9-1 | QMI8658 初始化成功 | I2C 通信正常 |
| I9-2 | 50Hz 数据更新 | 实际 45-55Hz |
| I9-3 | 静止 Pitch/Roll < 1° | 误差符合预期 |
| I9-4 | 数据平滑无跳变 | 卡尔曼/互补滤波生效 |
| I9-5 | SetAttitudeData() 接口对接 | 数据正确传递 |

### 迭代 10: 状态分级联动

| 编号 | 验收项 | 预期 |
|------|--------|------|
| I10-1 | < 2° 显示古玉青 | "Balance OK" |
| I10-2 | 2-8° 显示青花蓝 | "Slight Tilt" |
| I10-3 | 8-15° 显示鎏金黄 | "Medium Tilt" |
| I10-4 | 15-25° 显示赭石橙 | "Heavy Tilt" |
| I10-5 | > 25° 显示朱砂红 | "Danger Tilt" |
| I10-6 | 颜色切换 300ms 过渡 | 平滑无闪烁 |
| I10-7 | 进度环角度映射 | 0-360° 跟随倾角 |

### 迭代 11: 动效与性能优化

| 编号 | 验收项 | 预期 |
|------|--------|------|
| I11-1 | 动画 30FPS+ | 流畅无卡顿 |
| I11-2 | 颜色淡入淡出 | 300ms 平滑过渡 |
| I11-3 | 角度插值平滑 | 无跳变 |
| I11-4 | 内存稳定 | 长时间运行不泄漏 |
| I11-5 | 帧率监控日志 | 可查看性能数据 |

---

## 📐 设计规范速查

### 配色宏定义（必须与设计文档一致）

```cpp
// 背景色
#define COLOR_BG_OUTER      lv_color_hex(0x0A0A0A)
#define COLOR_BG_CENTER     lv_color_hex(0x121212)

// 文本色
#define COLOR_TEXT_MAIN     lv_color_hex(0xD4AF37)
#define COLOR_TEXT_SUB      lv_color_hex(0xC0C0C0)
#define COLOR_TEXT_HIGH     lv_color_hex(0xFFFFFF)

// 装饰色
#define COLOR_BORDER_LINE   lv_color_hex(0xD4AF37)
#define COLOR_CARD_BG       lv_color_hex(0x1A1A1A)
#define COLOR_POINT_DOT     lv_color_hex(0xD4AF37)

// 状态色
#define COLOR_STATE_NORMAL  lv_color_hex(0x2E5E4E)
#define COLOR_STATE_LIGHT   lv_color_hex(0x4A6FA5)
#define COLOR_STATE_MID     lv_color_hex(0xD4AF37)
#define COLOR_STATE_HEAVY   lv_color_hex(0xE67E22)
#define COLOR_STATE_DANGER  lv_color_hex(0xB82601)
```

### 屏幕参数

```cpp
#define SCREEN_W        360
#define SCREEN_H        360
#define CENTER_X        180
#define CENTER_Y        180
#define VALID_RADIUS    178
#define ANIM_DURATION   300
```

### 姿态分级

```cpp
if (fabs(angle) < 2.0f)        // 古玉青 平衡
else if (fabs(angle) < 8.0f)   // 青花蓝 轻微
else if (fabs(angle) < 15.0f)  // 鎏金黄 中度
else if (fabs(angle) < 25.0f)  // 赭石橙 较大
else                           // 朱砂红 严重
```

---

## 📁 截图使用方法

### 烧录固件
```bash
./build_and_flash.sh flash
```

### 获取截图
```bash
python3 scripts/save_screenshot.py
```

### 截图保存位置
```
/Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/screenshots/screenshot.jpg
```

详细使用方法参见 [SNAPSHOT_USAGE.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/doc/SNAPSHOT_USAGE.md)

---

## 🚨 常见问题排查

| 问题 | 可能原因 | 解决方法 |
|------|----------|----------|
| 编译失败 | 色值宏未定义 | 检查 attitude_display.h |
| 屏幕全黑 | 圆形裁剪未启用 | 检查 lv_disp_set_round |
| 元素溢出屏幕 | 坐标计算错误 | 使用 CENTER_X/CENTER_Y 对齐 |
| 颜色与规范不符 | 硬编码颜色 | 替换为 COLOR_* 宏 |
| 截图失败 | 串口被占用 | 关闭 monitor 后重试 |
| IMU 无数据 | I2C 地址错误 | 检查 QMI8658 地址 0x6A |
