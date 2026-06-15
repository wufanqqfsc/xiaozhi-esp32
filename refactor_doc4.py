#!/usr/bin/env python3
"""
第四轮: 彻底清理 5.3 之后到文档末尾的 Target1 残留,
       修复章节顺序.
策略: 找到 5.3 节末尾的 `| POINTS_RADIUS | 72 |` 行,
       删除到 "## 12. 总结与下一步AI 罗盘产品" 之前 (旧版),
       然后插入正确的 5.4-12 章.
"""

SRC = '/Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/doc/ai_compass_product_and_tech_spec.md'

with open(SRC, 'r', encoding='utf-8') as f:
    content = f.read()

# 找 5.3 节末尾锚点
anchor_53_end = "| `POINTS_RADIUS` | 72 |  // 4 方位点 r=72 圆周"
idx_anchor = content.find(anchor_53_end)
print(f"5.3 锚点位置: {idx_anchor}")

# 找旧 12 章位置 (有 "AI 罗盘产品基于成熟的 Xiaozhi")
old_12_anchor = "## 12. 总结与下一步\n\nAI 罗盘产品基于成熟的 Xiaozhi"
idx_old_12 = content.find(old_12_anchor)
print(f"旧 12 锚点位置: {idx_old_12}")

# 找文档末尾位置 (在版本信息之前)
tail_anchor = "\n*文档版本: v1.4"
idx_tail = content.find(tail_anchor)
if idx_tail == -1:
    idx_tail = content.find("*文档版本: v")
print(f"文档末尾位置: {idx_tail}")

# 找插入点: 5.3 节末尾的 "```\n" 之后
# 5.3 锚点之后的下两个换行
insert_pos = content.find("```\n", idx_anchor)
if insert_pos == -1:
    insert_pos = content.find("```\n\n", idx_anchor)
if insert_pos == -1:
    # 备用: 找 "**主题色" 表
    insert_pos = content.find("\n| `bg_outer`", idx_anchor)
if insert_pos == -1:
    insert_pos = idx_anchor + 200  # 兜底

# 调整 insert_pos 到 "```\n" 之后
if "```\n\n" in content[insert_pos:insert_pos+10]:
    insert_pos = content.find("```\n\n", insert_pos) + len("```\n\n")
else:
    insert_pos = insert_pos + 4

print(f"插入点: {insert_pos}")


# 准备新内容: 5.4 - 12 章
NEW_CONTENT = '''### 5.4 旋转速度参数

| 旋转对象 | 周期 | 每 50ms 步进 (0.1°单位) | 说明 |
|---------|------|------------------------|------|
| 太极图 | **30 秒/圈** | 3600 / (30000/50) = **6** (0.6°/step) | `compass_taiji.cc` 内的 FreeRTOS 任务 |
| 八卦名 + 卦象 | **45 秒/圈** | 3600 / (45000/50) = **4** (0.4°/step) | `attitude_display.cc` 的 `UpdateBaguaPositions()` |
| 方位大字 | **固定不动** | - | 四个方向大字始终指向屏幕四边 |

> **注意**: 当前代码 (Target2) 中 `UpdateBaguaPositions()` 未被调用, 实际只有太极图旋转. 八卦名/卦象在 5.3 重构时已移除 (迭代18-20 密度梯度重构).

**自动旋转实现细节** ([compass_taiji.cc:262-283](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/compass_taiji.cc#L262-L283)):
```cpp
// 每 50ms 执行一次: 角度步进 = 3600 / (period_ms / 50)
// 例: 30秒 = 3600 / 600 = 6 units/step = 0.6°/step
// 注意: lv_image_set_rotation() 以 0.1° 为单位, 范围 0~3600
```

---

### 5.5 主题系统（默认: 暗色+金色）

**[attitude_theme.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_theme.h) 单例 AttitudeTheme 字段**:

| 字段 | 用途 | 默认值 (Dark Gold) |
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

**主题切换**: `SwitchTheme(AttitudeThemeType)` → 递归更新 `attitude_container_` 内所有 LVGL 对象的颜色/透明度.

---

## 6. 构建与烧录

### 6.1 依赖与 SDK 版本

| 组件 | 版本要求 |
|-----|---------|
| **ESP-IDF** | **v5.5.2 或更高** (官方 `~/.espressif/v5.5.4/esp-idf` 路径) |
| **LVGL** | v9.5.0+ (通过 IDF 组件管理器) |
| **esp_lvgl_port** | v2.7.2+ |
| **CMake** | 3.20+ |
| **Python** | 3.8+ (esptool) |
| **目标芯片** | esp32s3 |
| **目标板配置** | `CONFIG_BOARD_TYPE_WAVESHARE_ESP32_S3_TOUCH_LCD_1_85B=y` |

**IDF 组件管理** ([idf_component.yml](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/idf_component.yml)):
```yaml
dependencies:
  lvgl/lvgl: ~9.5.0           # UI 引擎
  esp_lvgl_port: ~2.7.2       # ESP-LVGL 适配层
  espressif/esp_lcd_ili9341: ==1.2.0  # LCD 驱动
  espressif/button: ~4.1.5    # 按键驱动
  espressif/esp_codec_dev: ~1.5.6  # 音频编解码
```

### 6.2 板级配置 (Waveshare ESP32-S3-Touch-LCD-1.85B)

**关键配置** ([main/CMakeLists.txt](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/CMakeLists.txt)):
```cmake
elseif(CONFIG_BOARD_TYPE_WAVESHARE_ESP32_S3_TOUCH_LCD_1_85B)
    set(BOARD_TYPE "esp32-s3-touch-lcd-1.85b")
    set(BUILTIN_TEXT_FONT font_puhui_basic_30_4)
    set(BUILTIN_ICON_FONT font_awesome_16_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_64)
```

**关键配置项** ([main/Kconfig.projbuild](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/Kconfig.projbuild)):

| 配置 | 含义 | 本板值 |
|-----|-----|-------|
| `CONFIG_USE_WECHAT_MESSAGE_STYLE` | 启用气泡消息 (大屏) | n (圆屏不优化气泡) |

### 6.3 烧录步骤

```bash
# 1. 激活 IDF 环境
source $HOME/.espressif/v5.5.4/esp-idf/export.sh

# 2. 编译 (Waveshare 1.85B 板)
cd /path/to/xiaozhi-esp32
python3 $IDF_PATH/tools/idf.py -DBOARD_TYPE_WAVESHARE_ESP32_S3_TOUCH_LCD_1_85B=y build

# 3. 烧录 (macOS 端口)
PORT=/dev/cu.usbmodem101  # 或 cu.usbmodem1101
python3 $IDF_PATH/tools/idf.py -p $PORT flash

# 4. 串口 Monitor
python3 $IDF_PATH/tools/idf.py -p $PORT monitor

# 5. 擦除 (如需完全重置)
python3 $IDF_PATH/tools/idf.py -p $PORT erase-flash
```

### 6.4 资产/资源处理

**字体/图像资产**:
- 字体文件: 通过 `LV_FONT_DECLARE(BUILTIN_TEXT_FONT)` 方式使用 — font_puhui_basic_30_4
- 图像资产: 通过 `scripts/build_default_assets.py` 打包为 SPIFFS 镜像
- 多语言提示音: `main/assets/locales/xx-XX/*.ogg` → 编译进固件

**脚本工具**:
- `scripts/Image_Converter/lvgl_tools_gui.py` - LVGL 图像转换工具
- `scripts/spiffs_assets/build.py` - SPIFFS 资产打包

---

## 7. 开发路线图（Target2 实际状态 ⭐）

> **更新说明 (2026-06-14)**: 经过代码审计，文档原 5.2/5.3/5.6 节描述的 Target1 (罗盘+鱼眼+运势) 实际未实现。当前代码为 Target2 (4 层同心圆 + 状态进度)。本路线图基于实际代码状态制定。

### 7.1 当前实现状态

| 阶段 | 内容 | 状态 | 证据 |
|------|------|------|------|
| **阶段一 (Target2)** | 4 层同心圆基础罗盘 | ✅ 已完成 | attitude_display.cc 521 行, SetupUI 包含 CreateLayer0~4 + CreateCompassPoints |
| **阶段二 (Target2)** | 太极图自动旋转 | ✅ 已完成 | compass_taiji.cc, StartAutoRotation(30000), 30s/圈逆时针 |
| **阶段三 (Target2)** | 主题系统 (暗色/其他) | ✅ 已完成 | attitude_theme.h, SwitchTheme() API |
| **阶段四 (Target2)** | 状态进度颜色 (5 档) | ✅ 已完成 | UpdateStateColor(int level), state_normal/light/mid/heavy/danger |
| **阶段五 (Target2)** | IMU 姿态数据接口 | 🟡 API 已存在未联动 | SetAttitudeData(pitch,roll,yaw), UI 端未消费 |
| **阶段六 (Target2)** | 视觉增强 (动效/平滑) | ❌ 未开始 | 进度环无补间动画, 主题切换瞬变 |

### 7.2 迭代计划（建议）

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

### 7.3 不再实现的功能（已从产品中移除）

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

---

## 8. 性能与资源评估

### 8.1 资源估算（本板: Waveshare ESP32-S3-Touch-LCD-1.85B）

| 资源 | 占用 | 可用总量 | 利用率 | 说明 |
|-----|-----|--------|------|-----|
| Flash (固件) | ~2.5MB | 16MB | **~15%** | 主程序+字体+图像资产 |
| Flash (OTA/数据分区) | ~1MB | 6MB+ | **~17%** | 图像/字体/提示音/NVS/OTA 数据 |
| RAM (内部 SRAM) | ~200KB | 512KB | **~39%** | 运行时对象栈 + heap |
| RAM (PSRAM) | ~500KB | 8MB | **~6%** | LVGL 图像缓冲 + Canvas (太极图 ~31KB) |
| CPU 占用 | ~15-25% | 240MHz 双核 | **低** | LVGL 刷新(30fps) + 旋转任务(20Hz) + 音频处理 |

### 8.2 关键性能优化点

1. **PSRAM 优先分配**:
   ```cpp
   // 在 compass_taiji.cc Canvas 分配时:
   uint32_t* buf = (uint32_t*)heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
   if (!buf) buf = (uint32_t*)malloc(size);  // fallback 内部 RAM
   ```

2. **LVGL 图像缓存** (2MB):
   - PSRAM 8MB 可用 → 分配 2MB 缓存
   - 可显著加速图像 (卦象/表情/自定义 PNG) 首次绘制

3. **任务优先级设计** (`FreeRTOS`):
   - **AudioTask** (最高): 确保音频流不中断
   - **LVGL Task** (中): 刷新 UI (30fps 刷新)
   - **AutoRotation Task** (最低): 旋转动画，20Hz 位置更新
   - **Main Event Loop** (中): 事件分发/协议处理

4. **显示锁机制** (`DisplayLockGuard`):
   - 任何修改 LVGL 对象的操作必须持有锁
   - 避免任务并发导致 UI 崩溃

5. **文字标签置顶策略**:
   - 文字标签 (方位点) 直接创建在 `lv_screen_active()` 上
   - 创建后立即调用 `lv_obj_move_foreground()`

### 8.3 低功耗策略

- **待机模式**: 无交互 60 秒后进入省电
  - 降低屏幕亮度
  - 减慢太极旋转 (30s → 60s/圈)
  - `esp_pm_configure()` 设置 APB 降频
- **深度睡眠**: 无交互 10 分钟 + 电量低 (可选, BQ27220 提供电量数据)
- **电源管理锁**: 语音/更新显示期间持有锁, 防止 CPU 降频

---

## 9. I2C 设备地址参考（本板）

所有传感器/触控/RTC/电量共享同一 I2C 总线

| 设备 | I2C 地址 | 连接 | 用途 |
|-----|---------|-----|-----|
| **CST816S** | **0x15** (7-bit) | I2C SCL=GPIO10, SDA=GPIO11 | 电容触控 |
| **QMI8658** | **0x6B** (7-bit) | I2C SCL=GPIO10, SDA=GPIO11 | 六轴 IMU (加速度+陀螺仪) |
| **ES8311** | **0x18** (7-bit, 音频 codec) | I2C (音频侧I2C) | 播放音频编码 |
| **ES7210** | **0x20** (7-bit, AEC codec) | I2C | 录音/回声消除 |
| **BQ27220** | **0x55** (7-bit) | I2C | 电量检测 |
| **PCF85363** | **0x51** (7-bit) | I2C | RTC 实时时钟 |

---

## 10. 错误处理与稳定性

### 10.1 常见故障场景

| 故障 | 表现 | 处理策略 |
|-----|-----|---------|
| WiFi 连接失败 | 状态显示"连接失败"图标 | 3次重试 → 进入配网模式 |
| 服务器超时 | 对话卡住 >5s | `IsTimeout()` 检测 → 关闭音频通道 → 提示"网络异常" |
| 内存不足 | 图像/Canvas 分配失败 | 回退到简化 UI (无动画) + 输出错误日志 |
| 音频流中断 | 说话突然中断 | 重新打开通道 → 恢复对话上下文 (session_id 保持) |
| OTA 失败 | 升级中死机 | 回滚到旧分区 (ESP-IDF OTA 自带) + 下次重新尝试 |
| 触控失效 | 点屏幕无反应 | CST816S 复位 (GPIO1 控制复位脚) → 重新初始化 |
| IMU 失效 | 无姿态数据 | QMI8658 复位 → 降级为"无IMU 简化模式" |

### 10.2 Watchdog / 看门狗

- 启用 FreeRTOS Task Watchdog (`CONFIG_ESP_TASK_WDT_INIT=y`)
- LVGL 刷新任务注册到 WDT
- 严重崩溃: 核心转储到 Flash (如启用 `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH`)

### 10.3 日志策略

- 默认 INFO 级别 (可通过 `idf.py monitor` 查看)
- 关键路径: `Application` / `Protocol` / `McpServer` / `Display` 四个 TAG
- 生产环境可降级到 WARN 级别减少串口输出

---

## 11. 开发与调试工具链

| 工具 | 用途 | 命令/路径 |
|-----|-----|---------|
| ESP-IDF v5.5.4 | 构建 + 烧录 | `idf.py build/flash/monitor` |
| 串口 Monitor | 日志查看 | `idf.py -p /dev/cu.usbmodem101 monitor` (macOS) |
| LVGL Image Converter | PNG → C 数组 | `scripts/Image_Converter/lvgl_tools_gui.py` |
| SPIFFS Asset Builder | 资源打包 | `scripts/spiffs_assets/build.py` |
| JTAG 调试 (可选) | 单步调试 | 需要官方调试器 + OpenOCD |
| 通用 BLE 调试 App | BLE 测试 | iOS/Android 商店搜索 "BLE Scanner" |

---

## 12. 总结与下一步

AI 罗盘产品基于成熟的 Xiaozhi AI 开源项目, 针对 **Waveshare ESP32-S3-Touch-LCD-1.85B** 优化.

**✅ 已有能力 (Target2 已完成)**:
- 完善的语音交互 (唤醒/录音/AI 对话/TTS 播报, 双麦克风+AEC)
- 360×360 QSPI LCD 支持 + LVGL UI 引擎
- 4 层同心圆罗盘 + 太极图自动旋转
- 5 档状态颜色 + 主题系统
- MCP 工具链基础框架
- OTA + 多语言 (25+)

**🔧 下一步迭代 (按 7.2 路线图)**:
- **阶段甲 (P0)**: 状态进度环动效 (lv_anim_t 补间)
- **阶段乙 (P1)**: IMU (QMI8658) 数据接入
- **阶段丙 (P1)**: 主题切换 UI 入口
- **阶段丁 (P2)**: 多卦象扩展 (可选)
- **阶段戊 (P2)**: 触摸手势/截图分享/历史记录

**📅 项目周期**: 现有 4 层同心圆 + 主题已稳定, 上述迭代可在 1-5 周内分批完成.

**✅ 优先下一步**:
1. **真机验证** - 确认当前 4 层同心圆罗盘可正常显示 (太极图旋转、Layer1~4 可见、4 方位点)
2. **阶段甲动效** - 实现 `UpdateStateColor()` 补间动画
3. **阶段乙 IMU** - 接入 QMI8658 真实驱动

---'''

# 现在执行替换:
# 1. 保留 insert_pos 之前的内容
# 2. 插入 NEW_CONTENT
# 3. 删除 insert_pos 到 idx_old_12 之间所有 Target1 残留
# 4. 保留 idx_old_12 之后到 idx_tail 的内容 (文档版本信息)

if idx_anchor != -1 and idx_old_12 != -1 and idx_tail != -1:
    # 找 idx_anchor 之后的 "```\n" 边界
    boundary = content.find("```\n", idx_anchor)
    if boundary == -1:
        boundary = idx_anchor
    # 调到 boundary + 4 (跳过 ```\n)
    insert_at = boundary + 4
    # 找 insert_at 之后的下一个 \n (确保我们在行首)
    if insert_at < len(content) and content[insert_at] == '\n':
        insert_at += 1

    before = content[:insert_at]
    after = content[idx_old_12:idx_tail]  # idx_old_12 之后是 Target1 旧 12, 删
    # 实际 after 是 "## 12. 总结与下一步AI 罗盘产品..." 到 "*文档版本" 之前

    new_content = before + "\n" + NEW_CONTENT + after
    print(f"before 长度: {len(before)}")
    print(f"NEW_CONTENT 长度: {len(NEW_CONTENT)}")
    print(f"after 长度: {len(after)}")
    print(f"新文档总长度: {len(new_content)}")

    with open(SRC, 'w', encoding='utf-8') as f:
        f.write(new_content)
    print("✓ 文档已重写")
else:
    print(f"错误: idx_anchor={idx_anchor}, idx_old_12={idx_old_12}, idx_tail={idx_tail}")
