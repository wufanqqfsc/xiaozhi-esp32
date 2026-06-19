# ESP32 截图脚本使用文档

## 文件位置
`tools/screenshot_with_log.py`

## 功能概述

| 模式 | 说明 |
|------|------|
| 连续截图 | 默认模式，连续获取最多N张截图 |
| SNAP模式 | 发送SNAP命令触发单次截图 |
| CLICK模式 | 触发功能按钮点击并获取截图 |

## 命令行参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--max-screenshots` | 20 | 最大截图次数 |
| `--port` | `/dev/cu.usbmodem1101` | 串口设备路径 |
| `--click` | None | 触发功能按钮（与`snap`互斥） |
| `--snap` | False | 发送SNAP命令（与`click`互斥） |

## CLICK按钮索引

| 索引 | 功能 |
|------|------|
| 0 | 今日运势（综合运势） |
| 1 | 财富运势 |
| 2 | 健康运势 |
| 3 | 今日求财 |

## 使用示例

### 1. 连续截图（默认，最多20张）

```bash
python3 tools/screenshot_with_log.py
```

### 2. 指定串口和截图次数

```bash
python3 tools/screenshot_with_log.py --port /dev/cu.usbserial01 --max-screenshots 10
```

### 3. 触发"今日运势"按钮并截图

```bash
python3 tools/screenshot_with_log.py --click 0
```

### 4. 触发"财富运势"按钮并截图

```bash
python3 tools/screenshot_with_log.py --click 1
```

### 5. 发送SNAP命令触发单次截图

```bash
python3 tools/screenshot_with_log.py --snap
```

## 输出文件

| 文件 | 说明 |
|------|------|
| `screenshots/screenshot_latest.jpg` | 最新截图 |
| `screenshots/history/screenshot_<时间戳>_<序号>.jpg` | 历史截图 |
| `screenshots/logs/run_<时间戳>.log` | 运行日志 |

## 注意事项

1. 串口被占用时，先用 `lsof /dev/cu.usbmodem1101` 查找进程并 kill
2. CLICK命令会触发按钮事件并等待新界面截图
3. 连续截图模式每张间隔2秒
