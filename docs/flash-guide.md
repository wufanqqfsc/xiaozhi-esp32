# Waveshare ESP32-S3-Touch-LCD-1.85B 固件烧录指南

## 开发板信息

| 项目 | 信息 |
|------|------|
| **开发板型号** | Waveshare ESP32-S3-Touch-LCD-1.85B |
| **芯片型号** | ESP32-S3 (QFN56) revision v0.2 |
| **特性** | Wi-Fi, BT 5 (LE), Dual Core + LP Core, 240MHz |
| **内存** | Embedded PSRAM 8MB (AP_3v3) |
| **显示屏** | 1.85寸LCD触摸屏 |
| **项目版本** | 小智AI聊天机器人 v2.2.6 |

---

## 快速开始

### 一键编译烧录

项目根目录提供了 `build_and_flash.sh` 脚本：

```bash
# 编译并烧录（推荐）
./build_and_flash.sh

# 仅编译固件
./build_and_flash.sh build

# 仅烧录固件（需先编译）
./build_and_flash.sh flash

# 仅监视串口输出
./build_and_flash.sh monitor

# 查看帮助
./build_and_flash.sh --help
```

### 前置要求

ESP-IDF环境已安装完成（v5.5.4），脚本会自动加载环境。

如需手动设置环境：

```bash
# 加载ESP-IDF环境
source ~/.espressif/v5.5.4/esp-idf/export.sh

# 或使用脚本自动加载（推荐）
./build_and_flash.sh
```

> **注意**: 脚本会自动检测并加载已安装的ESP-IDF环境，无需手动设置。

---

## 开发环境信息

### 主机环境

| 项目 | 信息 |
|------|------|
| **操作系统** | macOS 15.6.1 (BuildVersion: 24G90) |
| **Python版本** | Python 3.14.5 |
| **esptool版本** | v5.3.0 |
| **ESP-IDF版本** | v5.5.4 ✅ 已安装 |
| **ESP-IDF路径** | `~/.espressif/v5.5.4/esp-idf` |

### ESP32设备信息

| 项目 | 信息 |
|------|------|
| **芯片型号** | ESP32-S3 (QFN56) revision v0.2 |
| **特性** | Wi-Fi, BT 5 (LE), Dual Core + LP Core, 240MHz |
| **内存** | Embedded PSRAM 8MB (AP_3v3) |
| **晶振频率** | 40MHz |
| **USB模式** | USB-Serial/JTAG |
| **串口设备** | `/dev/cu.usbmodem1101` (macOS) |
| **MAC地址** | a0:f2:62:e4:3a:40 |

---

## 固件烧录方式

### 方式一：源码编译烧录（推荐）

ESP-IDF v5.5.4 已安装，脚本会自动加载环境：

```bash
# 一键编译烧录
./build_and_flash.sh
```

### 方式二：预编译固件烧录

无需ESP-IDF环境，直接下载预编译固件烧录：

```bash
# 1. 安装esptool
pip3 install esptool --break-system-packages

# 2. 下载预编译固件
cd /tmp
curl -LO https://github.com/78/xiaozhi-esp32/releases/download/v2.2.6/v2.2.6_waveshare-esp32-s3-touch-lcd-1.85.zip
unzip -o v2.2.6_waveshare-esp32-s3-touch-lcd-1.85.zip

# 3. 检测设备
PORT=$(ls /dev/cu.usbmodem* | head -1)

# 4. 烧录固件
python3 -m esptool -p $PORT write-flash 0x0 merged-binary.bin
```

---

## 固件烧录脚本详解

### build_and_flash.sh 脚本功能

| 功能 | 命令 | 说明 |
|------|------|------|
| **一键编译烧录** | `./build_and_flash.sh` | 编译固件、烧录、启动监视 |
| **仅编译** | `./build_and_flash.sh build` | 编译固件，不烧录 |
| **仅烧录** | `./build_and_flash.sh flash` | 烧录已编译的固件 |
| **仅监视** | `./build_and_flash.sh monitor` | 监视串口输出 |
| **查看帮助** | `./build_and_flash.sh --help` | 显示使用说明 |

### 脚本特性

- ✅ 自动检测ESP32设备（macOS/Linux）
- ✅ 自动加载ESP-IDF环境（已配置路径：`~/.espressif/v5.5.4/esp-idf`）
- ✅ 固定开发板类型：`waveshare/esp32-s3-touch-lcd-1.85`
- ✅ 编译完成后自动合并固件
- ✅ 烧录后自动启动串口监视
- ✅ 彩色输出，清晰显示状态

---

## 常见问题

### 1. ESP-IDF环境加载失败

**解决方案：**

ESP-IDF v5.5.4 已安装在 `~/.espressif/v5.5.4/esp-idf`，脚本会自动加载。

如需手动加载：
```bash
source ~/.espressif/v5.5.4/esp-idf/export.sh
```

### 2. 无法连接ESP32设备

**错误信息：**
```
[ERROR] 未检测到ESP32设备!
```

**解决方案：**
- 确保USB线支持数据传输（不是仅充电线）
- 检查串口设备是否存在：`ls /dev/cu.usb*`
- 尝试手动进入烧录模式：按住BOOT按钮，按RESET按钮，松开BOOT

### 3. 编译失败

**常见原因：**
- 项目依赖缺失
- 编译缓存问题

**解决方案：**
```bash
# 清理编译缓存
rm -rf build

# 重新编译
./build_and_flash.sh build
```

### 4. 烧录失败

**解决方案：**
```bash
# 先擦除Flash再烧录
python3 -m esptool -p $PORT erase-flash
./build_and_flash.sh flash
```

---

## 串口监视

烧录完成后，设备会自动启动。可以通过串口监视查看运行状态：

```bash
# 使用脚本监视
./build_and_flash.sh monitor

# 或使用idf.py监视
idf.py -p /dev/cu.usbmodem1101 monitor

# 或使用Python监视
python3 -c "import serial; s=serial.Serial('/dev/cu.usbmodem1101',115200); while True: print(s.read(100).decode('utf-8',errors='ignore'),end='')"

# 按 Ctrl+] 退出监视
```

---

## 参考链接

- [小智AI官方文档](https://ccnphfhqs21z.feishu.cn/wiki/Zpz4wXBtdimBrLk25WdcXzxcnNS)
- [GitHub仓库](https://github.com/78/xiaozhi-esp32)
- [Waveshare ESP32-S3-Touch-LCD-1.85B文档](https://docs.waveshare.net/ESP32-S3-Touch-LCD-1.85B/)
- [ESP-IDF编程指南](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/)
- [esptool文档](https://docs.espressif.com/projects/esptool/en/latest/)