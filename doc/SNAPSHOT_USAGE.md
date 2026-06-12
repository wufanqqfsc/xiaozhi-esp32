# 截图功能使用说明

## 概述

xiaozhi-esp32 项目支持通过 USB-Serial/JTAG 端口获取屏幕截图。截图数据通过 Base64 编码后传输，可保存为 JPEG 图片文件。

## 功能特性

- 通过 USB-Serial/JTAG 端口传输（无需额外硬件）
- JPEG 压缩编码，减少数据量
- Base64 编码确保数据传输可靠性
- 自动截图任务，开机后自动执行

---

## 使用方法

### 1. 烧录固件

```bash
cd /Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32
./build_and_flash.sh flash
```

### 2. 运行截图接收脚本

```bash
# 方法一：使用 save_screenshot.py 脚本（推荐）
python3 scripts/save_screenshot.py

# 方法二：使用 receive_screenshot.py 脚本
python3 scripts/receive_screenshot.py /dev/cu.usbmodem101
```

### 3. 截图保存位置

截图文件保存到项目目录下的 `screenshots` 文件夹：

```
/Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/screenshots/
```

默认文件名：`screenshot.jpg`

每次运行脚本会覆盖之前的截图，如需保留历史截图，请手动重命名文件。

---

## 工作原理

### 数据流程

```
ESP32 屏幕
    ↓ (LVGL 快照)
RGB565 数据
    ↓ (JPEG 编码)
JPEG 二进制数据 (约 12KB)
    ↓ (Base64 编码)
Base64 文本数据 (约 17KB)
    ↓ (printf 通过 UART0 输出)
USB-Serial/JTAG 端口
    ↓ (Python 脚本接收)
Base64 解码
    ↓
JPEG 文件保存
```

### 输出格式

截图数据通过串口输出，格式如下：

```
===SCREENSHOT_START===
<Base64 编码的 JPEG 数据>
===SCREENSHOT_END===
```

### ESP32 端配置

- **截图触发**：开机后 2 秒自动截图，共 3 次，每次间隔 2 秒
- **波特率**：115200
- **端口**：USB-Serial/JTAG (UART0)

---

## 相关文件

### 源代码

| 文件 | 说明 |
|------|------|
| `main/main.cc` | 截图任务入口，自动触发截图 |
| `main/display/snapshot/snapshot_service.cc` | 截图服务实现 |
| `main/display/snapshot/snapshot_service.h` | 截图服务头文件 |
| `main/display/snapshot/snapshot_protocol.h` | 协议定义 |

### Python 脚本

| 文件 | 说明 |
|------|------|
| `scripts/save_screenshot.py` | 截图接收脚本（已验证可用） |
| `scripts/receive_screenshot.py` | 截图接收脚本（备选） |

### 截图保存

| 路径 | 说明 |
|------|------|
| `screenshots/` | 截图保存目录 |
| `screenshots/screenshot.jpg` | 默认截图文件 |

---

## 截图输出示例

设备启动后，串口日志会显示：

```
I (5003) main: Taking first screenshot...
I (5003) main: Taking scheduled screenshot #1...
[SCREENSHOT #1]
I (5003) SnapshotService: Executing snapshot...
I (5153) SnapshotService: JPEG encoded, size: 12791 bytes
I (5153) SnapshotService: JPEG size: 12791 bytes, Base64 size: 17057 bytes
===SCREENSHOT_START===
/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAAYEBQYFBAYGBQYHBwYIChAKCgkJChQODwwQFxQYGBcUFhYaHSUfGhsjHBYWICwgIyYnKSopGR8tMC0oMCUoKSj/2wBDAQcHBwoIChMKChMoGhYaKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCj/wAARCAFoAWgDASIAAhEBAxEB/8QAHwAAAQUBAQEBAQEAAAAAAAAAAAECAwQFBgcICQoL/8QAtRAAAgEDAwIEAwUFBAQAAAF9AQIDAAQRBRIhMUEGE1FhByJxFDKBkaEII0KxwRVS0fAkM2JyggkKFhcYGRolJicoKSo0NTY3ODk6Q0RFRkdISUpTVFVWV1hZWmNkZWZnaGlqc3R1dnd4eXqDhIWGh4iJipKTlJWWl5iZmqKjpKWmp6ipqrKztLW2t7i5usLDxMXGx8jJytLT1NXW19jZ2uHi4+Tl5ufo6erx8vP09fb3+Pn6/8QAHwEAAwEBAQEBAQEBAQAAAAAAAAECAwQFBgcICQoL/8QAtREAAgECBAQDBAcFBAQAAQJ3AAECAxEEBSExBhJBUQdhcRMiMoEIFEKRobHBCSMzUvAVYnLRChYkNOEl8RcYGRomJygpKjU2Nzg5OkNERUZHSElKU1RVVldYWVpjZGV...
===SCREENSHOT_END===
```

---

## 故障排除

### 常见问题

| 问题 | 可能原因 | 解决方法 |
|------|----------|----------|
| 未找到截图数据 | 设备未重启 | 重启设备或重新烧录固件 |
| JPEG 解码失败 | 数据不完整 | 尝试增大接收缓冲区 |
| 波特率不匹配 | 默认 115200 | 检查串口配置 |

### 验证截图文件

```bash
# 检查文件类型
file screenshots/screenshot.jpg

# 查看文件大小
ls -la screenshots/screenshot.jpg
```

---

## 版本历史

| 版本 | 日期 | 说明 |
|------|------|------|
| 1.0 | 2026-06-10 | 初始版本 |
| 1.1 | 2026-06-12 | 添加截图保存位置说明，完善使用文档 |
