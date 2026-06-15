#!/usr/bin/env python3
"""
修复 5.3 节末尾 \`\`\` 丢失, 清除锚点后到 7 章 Target2 之间的 Target1 残留,
插入正确的 5.4/5.5/6 章节.
"""

SRC = '/Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/doc/ai_compass_product_and_tech_spec.md'

with open(SRC, 'r', encoding='utf-8') as f:
    c = f.read()

# 锚点
i_anchor = c.find('// 4 方位点 r=72 圆周')
i_7_t2 = c.find('## 7. 开发路线图（Target2')
i_tail = c.find('*文档版本')

# 1. 修复 5.3 末尾 ``` 丢失: 圆周``` -> 圆周\n```\n\n
old = 'constexpr int POINTS_RADIUS = 72;  // 4 方位点 r=72 圆周```**主题色'
new = 'constexpr int POINTS_RADIUS = 72;  // 4 方位点 r=72 圆周\n```\n\n**主题色'
if old in c:
    c = c.replace(old, new)
    print('✓ 修复 5.3 末尾 ``` 丢失')
else:
    print('⚠ 5.3 末尾未找到匹配, 跳过')

# 2. 重新定位锚点 (修复后)
i_anchor = c.find('// 4 方位点 r=72 圆周\n```\n\n**主题色')
i_7_t2 = c.find('## 7. 开发路线图（Target2')
i_tail = c.find('*文档版本')
print(f'修复后锚点: {i_anchor}, 7 章: {i_7_t2}, 文档版本: {i_tail}')

# 3. 找主题色表结束位置 (state_danger 行后的 \n)
state_danger_pos = c.find('| `state_danger` |', i_anchor)
table_end = c.find('\n', state_danger_pos)
print(f'主题色表结束: {table_end}')

# 4. 准备删除范围: table_end 之后, 到 7 章 Target2 之前
# 保留锚点之前的 5.3 节 + 主题色表 + 修复的 \`\`\`
# 删 table_end 之后到 7 章 Target2 之前

# 新内容: 5.4 + 5.5 + 6 (Target2 实际)
NEW_SECTIONS = '''

### 5.4 旋转速度参数

| 旋转对象 | 周期 | 每 50ms 步进 (0.1°单位) | 说明 |
|---------|------|------------------------|------|
| 太极图 | **30 秒/圈** | 3600 / (30000/50) = **6** (0.6°/step) | `compass_taiji.cc` 内的 FreeRTOS 任务 |
| 八卦名 + 卦象 | **45 秒/圈** (代码未调用) | 3600 / (45000/50) = **4** (0.4°/step) | 已移除, 仅作历史参考 |
| 方位大字 | **固定不动** | - | 四个 6×6 圆点始终在 r=72 圆周上 |

> **注**: 当前 Target2 实现中, `UpdateBaguaPositions()` 未被调用. 实际只有太极图旋转. 八卦名/卦象在 5.3 重构时已移除 (迭代18-20 密度梯度重构).

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
'''

# 拼接
before = c[:table_end+1]  # 包含主题色表最后行 + \n
after = c[i_7_t2:]        # 7 章 Target2 + 8/9/10/11/12 + 文档版本

new_content = before + NEW_SECTIONS + '\n' + after

with open(SRC, 'w', encoding='utf-8') as f:
    f.write(new_content)

print()
print(f"原始: {len(c)} 字符")
print(f"新:   {len(new_content)} 字符")
print(f"减少: {len(c) - len(new_content)} 字符")
