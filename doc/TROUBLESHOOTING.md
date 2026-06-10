# Waveshare ESP32-S3-Touch-LCD-1.85B 问题排查与解决方案

---

## 概述

本文档记录了 Waveshare ESP32-S3-Touch-LCD-1.85B 开发板在固件编译和烧录过程中遇到的问题及其解决方案。

---

## 问题列表

### 问题1: LCD屏幕黑屏（最常见问题）

**现象**: 设备启动成功，但LCD屏幕无显示

**原因分析**: 

| 原因类型 | 具体问题 | 影响 |
| --- | --- | --- |
| GPIO配置错误 | LCD_RST未配置（使用GPIO_NUM_NC） | LCD无法正确复位初始化 |
| GPIO配置错误 | 触摸控制器引脚配置错误 | 触摸功能异常，可能影响显示 |
| GPIO冲突 | 电源按钮使用GPIO6与RTC_INT冲突 | 可能导致系统不稳定 |

**解决方案**:

根据官方文档修正 `main/boards/waveshare/esp32-s3-touch-lcd-1.85b/config.h`:

```c
// 修正前
#define QSPI_PIN_NUM_LCD_RST    GPIO_NUM_NC  // 错误：未配置
#define TP_PORT          (I2C_NUM_1)         // 错误：使用I2C1
#define TP_PIN_NUM_SDA   (GPIO_NUM_1)        // 错误：应为GPIO11
#define TP_PIN_NUM_SCL   (GPIO_NUM_3)        // 错误：应为GPIO10
#define TP_PIN_NUM_RST   (GPIO_NUM_NC)       // 错误：应为GPIO1
#define PWR_BUTTON_GPIO  GPIO_NUM_6          // 错误：与RTC_INT冲突

// 修正后
#define QSPI_PIN_NUM_LCD_RST    GPIO_NUM_3   // 正确：LCD复位引脚
#define TP_PORT          (I2C_NUM_0)         // 正确：使用I2C0与LCD共享
#define TP_PIN_NUM_SDA   (GPIO_NUM_11)       // 正确：触摸SDA
#define TP_PIN_NUM_SCL   (GPIO_NUM_10)       // 正确：触摸SCL
#define TP_PIN_NUM_RST   (GPIO_NUM_1)        // 正确：触摸复位
#define PWR_BUTTON_GPIO  GPIO_NUM_7          // 正确：避免冲突
```

**GPIO配置对照表**（官方文档标准）:

| 信号 | 官方GPIO | 错误配置 | 修正后 |
| --- | --- | --- | --- |
| LCD_CS | GPIO21 | GPIO21 | GPIO21 ✓ |
| LCD_PCLK | GPIO40 | GPIO40 | GPIO40 ✓ |
| LCD_DATA0-3 | GPIO46/45/42/41 | GPIO46/45/42/41 | GPIO46/45/42/41 ✓ |
| LCD_RST | GPIO3 | GPIO_NUM_NC | GPIO3 ✓ |
| LCD_BL | GPIO5 | GPIO5 | GPIO5 ✓ |
| TP_SCL | GPIO10 | GPIO3 | GPIO10 ✓ |
| TP_SDA | GPIO11 | GPIO1 | GPIO11 ✓ |
| TP_RST | GPIO1 | GPIO_NUM_NC | GPIO1 ✓ |
| TP_INT | GPIO4 | GPIO4 | GPIO4 ✓ |

---

### 问题2: 开发板类型配置错误

**现象**: 烧录后日志显示 `SKU=bread-compact-wifi` 而非目标开发板

**原因**: Kconfig配置未正确选择目标开发板

**解决方案**:

1. 在 `Kconfig.projbuild` 中添加配置选项
2. 在 `CMakeLists.txt` 中添加对应的条件分支
3. 修改 `sdkconfig` 文件:
   ```
   # CONFIG_BOARD_TYPE_BREAD_COMPACT_WIFI is not set
   CONFIG_BOARD_TYPE_WAVESHARE_ESP32_S3_TOUCH_LCD_1_85B=y
   ```
4. 在编译脚本中强制指定开发板类型:
   ```bash
   idf.py -DBOARD_TYPE_WAVESHARE_ESP32_S3_TOUCH_LCD_1_85B=y build
   ```

---

### 问题3: TCA9554 IO扩展器初始化失败

**现象**: 设备不断重启，日志显示TCA9554初始化失败

**原因**: ESP32-S3-Touch-LCD-1.85B 版本**没有** TCA9554 IO扩展器，但代码尝试初始化它

**解决方案**:

修改 `esp32-s3-touch-lcd-1.85b.cc` 中的 `InitializeTca9554` 函数:

```cpp
void InitializeTca9554(void) {
    printf("[1.85B] TCA9554 not present, skipping initialization\n");
    printf("[1.85B] Waiting for LCD power up...\n");
    vTaskDelay(pdMS_TO_TICKS(500));  // 添加电源稳定延时
}
```

---

### 问题4: Python环境不一致

**现象**: 编译时提示:
```
'/Users/sfan/.espressif/python_env/idf5.5_py3.14_env/bin/python' is currently active 
while the project was configured with '/Users/sfan/.espressif/python_env/idf5.5_py3.14_env/bin/python3'
```

**原因**: ESP-IDF环境变量配置不一致

**解决方案**:

```bash
# 清理项目配置
idf.py fullclean

# 重新配置环境
source ~/.espressif/v5.5.4/esp-idf/export.sh

# 使用python3明确调用
python3 $IDF_PATH/tools/idf.py build
```

---

### 问题5: 串口权限问题

**现象**: 烧录时提示 `Operation not permitted`

**原因**: 当前用户对串口设备没有读写权限

**解决方案**:

```bash
# 查看设备权限
ls -la /dev/cu.usbmodem*

# 修改权限（临时）
sudo chmod 777 /dev/cu.usbmodem101

# 永久解决方案：将用户加入dialout组（Linux）
sudo usermod -aG dialout $USER
```

---

### 问题6: 编译脚本缺少进度显示

**现象**: 编译和烧录过程无进度反馈，用户无法判断进度

**解决方案**:

使用改进的 `build_and_flash.sh` 脚本，提供：
- 完整的步骤进度指示
- 彩色输出区分不同状态
- 编译和烧录的完整控制台输出
- 设备检测和连接测试
- 固件大小和产物信息显示

---

## 诊断流程

### 第一步: 检查启动日志

```bash
# 使用idf.py monitor查看日志
idf.py -p /dev/cu.usbmodem101 monitor

# 或使用Python脚本
python3 -c "
import serial
import time
ser = serial.Serial('/dev/cu.usbmodem101', 115200, timeout=1)
ser.setDTR(False); time.sleep(0.1); ser.setDTR(True); time.sleep(3)
for _ in range(100):
    line = ser.readline().decode('utf-8', errors='ignore').strip()
    if line:
        print(line)
ser.close()
"
```

### 第二步: 验证关键初始化

检查日志中是否包含以下关键信息:

| 关键字 | 说明 | 正常输出示例 |
| --- | --- | --- |
| `SKU=esp32-s3-touch-lcd-1.85b` | 开发板识别 | `I (209) Board: SKU=esp32-s3-touch-lcd-1.85b` |
| `ST77916` | LCD驱动安装 | `I (739) waveshare_lcd_1_85: Install ST77916 panel driver` |
| `LCD panel create success` | LCD初始化成功 | `I (759) st77916: LCD panel create success` |
| `Backlight` | 背光设置 | `I (1069) Backlight: Set brightness to 75` |
| `LVGL` | 图形库初始化 | `I (1059) LcdDisplay: Initialize LVGL library` |

### 第三步: 硬件检查清单

```
□ USB线支持数据传输（非充电线）
□ 设备已正确连接并通电
□ LCD排线连接牢固
□ 无明显硬件损坏
□ 背光是否亮起（黑暗环境下观察）
```

---

## 常用命令

```bash
# 检测设备
ls /dev/cu.usbmodem*

# 检查芯片信息
python3 -m esptool -p /dev/cu.usbmodem101 chip_id

# 完整清理和重新编译
idf.py fullclean
idf.py -DBOARD_TYPE_WAVESHARE_ESP32_S3_TOUCH_LCD_1_85B=y build

# 烧录固件
idf.py -p /dev/cu.usbmodem101 flash

# 监视串口
idf.py -p /dev/cu.usbmodem101 monitor

# 使用一键脚本
./build_and_flash.sh all
```

---

## 问题解决流程图

```
设备黑屏?
    │
    ▼
┌─────────────────────┐
│ 检查启动日志        │
└─────────┬───────────┘
          │
          ▼
┌─────────────────────┐    否
│ SKU=esp32-s3-touch- │───────► 开发板配置错误
│ lcd-1.85b?         │
└─────────┬───────────┘
          │ 是
          ▼
┌─────────────────────┐    否
│ ST77916初始化成功?  │───────► LCD驱动问题
└─────────┬───────────┘
          │ 是
          ▼
┌─────────────────────┐    否
│ Backlight设置成功?  │───────► 背光电路问题
└─────────┬───────────┘
          │ 是
          ▼
┌─────────────────────┐
│ 检查硬件连接        │
│ - LCD排线          │
│ - 背光电路         │
│ - 屏幕本身         │
└─────────────────────┘
```

---

## 更新记录

| 日期 | 版本 | 更新内容 |
| --- | --- | --- |
| 2026-06-10 | v1.0 | 初始版本，记录GPIO配置问题及解决方案 |

---

**文档状态**: 活跃维护中
