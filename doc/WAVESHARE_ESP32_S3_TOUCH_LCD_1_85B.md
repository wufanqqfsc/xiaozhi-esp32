# Waveshare ESP32-S3-Touch-LCD-1.85B 官方文档

---

## 概述

ESP32-S3-Touch-LCD-1.85B 是一款由微雪 (Waveshare) 设计的高性能、高集成度微控制器开发板。

### 核心特性

| 项目 | 规格 |
| --- | --- |
| 主控芯片 | ESP32-S3R8 双核 LX7 处理器 |
| 运行频率 | 240MHz |
| 无线通信 | 2.4GHz Wi-Fi (802.11 b/g/n) + Bluetooth 5 (LE) |
| 内存配置 | 512KB SRAM + 8MB PSRAM + 16MB Flash |
| 屏幕规格 | 1.85英寸电容触摸 LCD (360×360) |

### 应用场景

- AI 语音交互
- 智能硬件
- 人机交互界面
- 产品原型验证

---

## 板载资源

### 资源布局图

![ESP32-S3-Touch-LCD-1.85B 资源简介](https://www.waveshare.net/photo/development-board/ESP32-S3-Touch-LCD-1.85B/ESP32-S3-Touch-LCD-1.85B-details-intro.jpg)

### 资源列表

1. **ESP32-S3R8** Wi-Fi 和蓝牙 SoC，240MHz 运行频率，叠封 8MB PSRAM
2. **16MB NOR Flash**
3. **QMI8658** 六轴惯性测量单元 (IMU)，包含一个 3 轴陀螺仪和一个 3 轴加速度计
4. **双麦克风设计** 配合回声消除电路，能够更高质量地采集音频
5. **板载贴片天线** 支持 2.4GHz Wi-Fi 和蓝牙 5 (LE)
6. **屏幕接口**
7. **ES7210 回声消除算法芯片** 可用于消除回声，提高音频采集准度
8. **ES8311 音频编解码芯片**
9. **BQ27220** 电量检测芯片
10. **BOOT 按键** 用于设备启动和功能调试
11. **PWR 电源按键** 可控制电源通断，支持自定义功能
12. **Type-C 接口** ESP32-S3 USB 接口，可烧录程序及打印日志
13. **MX1.25 锂电池接口** MX1.25 2PIN 连接器，可用于接入 3.7V 锂电池，支持充放电
14. **板载扬声器焊盘**
15. **串口通信接口** SH1.0 4PIN 连接器
16. **I2C 接口** SH1.0 4PIN 连接器

---

## 外设速查

| 模块 | 器件 / 功能 | 接口 | 地址 / 参数 | GPIO / 信号 |
| --- | --- | --- | --- | --- |
| LCD | ST77916 | QSPI | 360×360，常用 RGB565，16-bit 色深 | CS=GPIO21，PCLK=GPIO40，DATA0=GPIO46，DATA1=GPIO45，DATA2=GPIO42，DATA3=GPIO41，RST=GPIO3，BL=GPIO5 |
| 触摸 | CST816S 电容触摸 | I2C | 7-bit 地址 `0x15` | SCL=GPIO10，SDA=GPIO11，RST=GPIO1，INT=GPIO4 |
| IMU | QMI8658 6 轴传感器 | I2C | 7-bit 地址 `0x6B` | SCL=GPIO10，SDA=GPIO11 |
| 音频输出 | ES8311 音频编解码 | I2C + I2S | 7-bit 地址 `0x30` | I2C SCL=GPIO10，SDA=GPIO11；MCLK=GPIO2，BCLK=GPIO48，LRCK=GPIO38，DOUT=GPIO47，DIN=GPIO39，PA=GPIO9 |
| 音频输入 | ES7210 回声消除算法芯片 / 双麦克风 | I2C + I2S | 7-bit 地址 `0x80` | I2C SCL=GPIO10，SDA=GPIO11；MCLK=GPIO2，BCLK=GPIO48，LRCK=GPIO38，DIN=GPIO39 |
| 电量检测 | BQ27220 | I2C | 7-bit 地址 `0x55` | SCL=GPIO10，SDA=GPIO11 |
| RTC | PCF85063 实时时钟 | I2C | 7-bit 地址 `0x51` | SCL=GPIO10，SDA=GPIO11，INT=GPIO6 |
| Micro SD | SDMMC | SDMMC 4-bit | 支持 SD_MMC | CLK=GPIO15，CMD=GPIO14，D0=GPIO16，D1=GPIO17，D2=GPIO12，D3=GPIO13 |
| USB Type-C | ESP32-S3 原生 USB | USB | 下载、日志 | USB_N=GPIO19，USB_P=GPIO20 |
| UART0 | 串口通信接口 | UART | SH1.0 4PIN 连接器 | U0TXD=GPIO43，U0RXD=GPIO44 |
| BOOT 按键 | BOOT / 下载模式 | GPIO | 上拉，按下拉低 | GPIO0 |

---

## 引脚定义

### 扩展接口

| 类型 | 信号 |
| --- | --- |
| 电源 | `5V` / `3V3` / `GND` |
| I2C | `SCL(GPIO10)` / `SDA(GPIO11)` |
| UART | `TXD(GPIO43)` / `RXD(GPIO44)` |
| USB 焊盘 | `DN(GPIO19)` / `DP(GPIO20)` |

### GPIO 完整分配

| GPIO | 信号名 | 连接到 | 备注 |
| --- | --- | --- | --- |
| GPIO0 | BOOT | BOOT 按键 | Strapping pin；长按上电再松开，进入下载模式 |
| GPIO1 | TP_RST | CST816S 触摸复位 | - |
| GPIO2 | I2S_MCLK | ES8311 / ES7210 音频时钟 | - |
| GPIO3 | LCD_RST | ST77916 LCD 复位 | - |
| GPIO4 | TP_INT | CST816S 触摸中断 | - |
| GPIO5 | LCD_BL | LCD 背光控制 | PWM 调光 |
| GPIO6 | RTC_INT | PCF85063 实时时钟中断 | - |
| GPIO9 | PA_CTRL | 功放控制 | 音频输出功放使能 |
| GPIO10 | I2C_SCL | 触摸、IMU、音频芯片、电量检测共享 I2C SCL；也接出到 I2C 接口 | I2C 设备见外设速查表 |
| GPIO11 | I2C_SDA | 触摸、IMU、音频芯片、电量检测共享 I2C SDA；也接出到 I2C 接口 | I2C 设备见外设速查表 |
| GPIO12 | SD_D2 | Micro SD D2 | SDMMC 4-bit |
| GPIO13 | SD_D3 | Micro SD D3 | SDMMC 4-bit |
| GPIO14 | SD_CMD | Micro SD CMD | SDMMC |
| GPIO15 | SD_CLK | Micro SD CLK | SDMMC |
| GPIO16 | SD_D0 | Micro SD D0 | SDMMC |
| GPIO17 | SD_D1 | Micro SD D1 | SDMMC |
| GPIO19 | USB_N | USB Type-C D- | ESP32-S3 原生 USB |
| GPIO20 | USB_P | USB Type-C D+ | ESP32-S3 原生 USB |
| GPIO21 | LCD_CS | ST77916 QSPI 片选 | - |
| GPIO38 | I2S_LRCK | ES8311 / ES7210 左右声道时钟 | - |
| GPIO39 | I2S_DIN | ES8311 / ES7210 I2S 输入数据 | 录音数据输入 |
| GPIO40 | LCD_PCLK | ST77916 QSPI 时钟 | - |
| GPIO41 | LCD_DATA3 | ST77916 QSPI 数据线 3 | - |
| GPIO42 | LCD_DATA2 | ST77916 QSPI 数据线 2 | - |
| GPIO43 | U0TXD | 串口通信接口 TXD | 调试 / 扩展 |
| GPIO44 | U0RXD | 串口通信接口 RXD | 调试 / 扩展 |
| GPIO45 | LCD_DATA1 | ST77916 QSPI 数据线 1 | - |
| GPIO46 | LCD_DATA0 | ST77916 QSPI 数据线 0 | - |
| GPIO47 | I2S_DOUT | ES8311 I2S 输出数据 | 播放数据输出 |
| GPIO48 | I2S_BCLK | ES8311 / ES7210 I2S 位时钟 | - |

### 使用注意

* 外接 I2C 设备时，需要避开 `0x15`、`0x6B`、`0x30`、`0x80`、`0x51`、`0x55` 等板载 I2C 地址冲突。
* `GPIO19/GPIO20` 已连接 USB Type-C，不建议当普通 GPIO 使用。
* `GPIO0` 是 BOOT 引脚，不建议作为普通用户输入。
* LCD、触摸、音频、SDMMC 等板载外设已占用的 GPIO 不建议复用，否则可能导致对应功能异常。

---

## LCD 屏幕参数

| 参数名称 | 参数 |
| --- | --- |
| 显示面板 | LCD |
| 显示尺寸 | 1.85 英寸 |
| 分辨率 | 360 × 360 |
| 显示色彩 | 262K 彩色 |
| 显示接口 | QSPI |
| 显示驱动 | ST77916 |
| 触控芯片 | CST816S |
| 触控接口 | I2C |

---

## 产品参数

| 参数名称 | 参数 |
| --- | --- |
| 接口 | USB Type-C |
| 主控芯片 | ESP32-S3R8 |
| SRAM / ROM | 512KB SRAM / 384KB ROM |
| PSRAM / Flash | 8MB PSRAM / 16MB Flash |
| 屏幕类型 | TFT LCD |
| 屏幕控制芯片 | Display: ST77916 Touch: CST816S |
| 板载设备 | QMI8658 六轴传感器 ES8311 音频编解码芯片 ES7210 回声消除算法芯片 BQ27220 电量检测芯片 PCF85063 实时时钟 双麦克风阵列 板载扬声器焊盘 Micro SD 锂电池充放电接口 |
| 引出接口 | I2C、UART、USB 焊盘 |
| 供电接口 | USB Type-C / 3.7V 锂电池接口 |

---

## 产品尺寸

![ESP32-S3-Touch-LCD-1.85B 产品尺寸](https://www.waveshare.net/photo/development-board/ESP32-S3-Touch-LCD-1.85B/ESP32-S3-Touch-LCD-1.85B-details-size.jpg)

---

## 开发方式

ESP32-S3-Touch-LCD-1.85B 支持 Arduino IDE 和 ESP-IDF 两种开发框架。

### Arduino IDE

**优势**：简单易学、上手快，适合初学者和非专业人士。

**特点**：
- 无需太多基础知识，简单学习后即可快速开发
- 庞大的全球用户社区，提供海量开源代码、项目示例和教程
- 丰富的库资源，封装了复杂功能

**参考文档**：[Arduino IDE 开发环境搭建教程](https://docs.waveshare.net/ESP32-S3-Touch-LCD-1.85B/Development-Environment-Setup-Arduino)

### ESP-IDF

**优势**：提供更高级的开发工具和更强的控制能力，适合有专业背景或对性能要求较高的开发者。

**特点**：
- 基于 C 语言开发
- 包含编译器、调试器、烧录工具等
- 支持命令行或集成开发环境（如 Visual Studio Code 配合 Espressif IDF 插件）开发

**推荐工具**：VS Code + Espressif IDF 插件

**参考文档**：[ESP-IDF (VS Code) 开发环境搭建教程](https://docs.waveshare.net/ESP32-S3-Touch-LCD-1.85B/Development-Environment-Setup-ESPIDF)

---

## 相关资源

| 资源类型 | 链接 |
| --- | --- |
| 产品购买 | [ESP32-S3-Touch-LCD-1.85B](https://www.waveshare.net/shop/ESP32-S3-Touch-LCD-1.85B-EN.htm) |
| Arduino 开发 | [开发环境搭建 - Arduino](https://docs.waveshare.net/ESP32-S3-Touch-LCD-1.85B/Development-Environment-Setup-Arduino) |
| ESP-IDF 开发 | [开发环境搭建 - ESP-IDF](https://docs.waveshare.net/ESP32-S3-Touch-LCD-1.85B/Development-Environment-Setup-ESPIDF) |
| 资料下载 | [原理图、规格书、示例代码](https://docs.waveshare.net/ESP32-S3-Touch-LCD-1.85B/Resources-And-Documents) |
| FAQ | [产品常见问题](https://docs.waveshare.net/ESP32-S3-Touch-LCD-1.85B/FAQ) |
| 技术支持 | [联系技术支持](https://docs.waveshare.net/ESP32-S3-Touch-LCD-1.85B/Technical-Support) |

---

*文档来源：[Waveshare 官方文档](https://docs.waveshare.net/ESP32-S3-Touch-LCD-1.85B)*
