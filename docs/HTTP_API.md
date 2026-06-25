# ESP32 HTTP API 文档

> **版本**: v1.1  
> **更新日期**: 2026-06-25  
> **服务端口**: 8080

---

## 概述

ESP32 设备在 WiFi 连接成功后，自动启动 HTTP 服务（端口 8080），提供设备状态查询、日志管理、截图管理等功能。

### 基础 URL

```
http://<设备IP>:8080
```

---

## API 接口一览

| 接口 | 方法 | 说明 |
|------|------|------|
| `/api/device/status` | GET | 获取设备状态 |
| `/api/device/logs` | GET | 获取设备日志 |
| `/api/device/reboot` | POST | 重启设备 |
| `/api/device/ota-url` | GET | 查询 OTA/WS URL 配置（NVS vs 构建配置） |
| `/api/device/clear-nvs` | POST | 清除 NVS 中存储的 ota_url 和 websocket_url |
| `/api/sdcard/info` | GET | 获取 SD 卡信息 |
| `/api/sdcard/logs` | GET | 获取日志文件列表 |
| `/api/sdcard/logs/<filename>` | GET | 下载日志文件 |
| `/api/sdcard/logs/<filename>` | DELETE | 删除日志文件 |
| `/api/sdcard/shots` | GET | 获取截图文件列表 |
| `/api/sdcard/shots` | POST | 触发屏幕截图 |
| `/api/sdcard/shots/<filename>` | GET | 下载截图文件 |
| `/api/sdcard/shots/<filename>` | DELETE | 删除截图文件 |
| `/api/sdcard/files/<filename>` | DELETE | 删除 SD 卡任意文件 |
| `/` | GET | Web 管理界面 |

---

## 设备管理 API

### 1. 获取设备状态

**请求**
```
GET /api/device/status
```

**响应示例**
```json
{
  "wifi_connected": true,
  "sdcard_mounted": true,
  "log_active": true,
  "http_running": true,
  "http_port": 8080,
  "memory": {
    "free_heap": 123456,
    "min_free_heap": 100000
  },
  "uptime_seconds": 3600,
  "last_screenshot": "shot_20250624_143052.jpg",
  "last_error_screenshot": "error_OTA_20250624_143100.jpg"
}
```

**字段说明**

| 字段 | 类型 | 说明 |
|------|------|------|
| `wifi_connected` | bool | WiFi 是否连接 |
| `sdcard_mounted` | bool | SD 卡是否挂载 |
| `log_active` | bool | 日志记录是否激活 |
| `http_running` | bool | HTTP 服务是否运行 |
| `http_port` | number | HTTP 服务端口 |
| `memory.free_heap` | number | 当前可用堆内存（字节） |
| `memory.min_free_heap` | number | 最小可用堆内存（字节） |
| `uptime_seconds` | number | 设备运行时间（秒） |
| `last_screenshot` | string | 最后一次截图文件名 |
| `last_error_screenshot` | string | 最后一次错误截图文件名 |

---

### 2. 获取设备日志

**请求**
```
GET /api/device/logs
```

**响应**
- 返回最后 10KB 的日志内容（text/plain）
- 如果 SD 卡无日志文件，返回 404

**使用示例**
```bash
curl http://192.168.3.22:8080/api/device/logs
```

---

### 3. 重启设备

**请求**
```
POST /api/device/reboot
```

**响应示例**
```json
{
  "ok": true,
  "message": "Rebooting in 3 seconds..."
}
```

**说明**
- 设备将在 3 秒后重启
- HTTP 响应立即返回，之后设备断开连接

---

### 4. 查询 OTA / WebSocket URL 配置

**请求**
```
GET /api/device/ota-url
```

**响应示例**
```json
{
  "nvs_ota_url": "http://192.168.3.31:8003/xiaozhi/ota/",
  "nvs_websocket_url": "",
  "build_ota_url": "http://192.168.3.32:8091/api/device/ota",
  "build_websocket_url": "ws://192.168.3.32:8092/ws/xiaozhi/v1/",
  "nvs_ota_overridden": true,
  "nvs_ws_overridden": false
}
```

**字段说明**

| 字段 | 类型 | 说明 |
|------|------|------|
| `nvs_ota_url` | string | NVS 中存储的 OTA URL（如有覆盖） |
| `nvs_websocket_url` | string | NVS 中存储的 WebSocket URL（如有覆盖） |
| `build_ota_url` | string | 编译期固化的 OTA URL (`CONFIG_OTA_URL`) |
| `build_websocket_url` | string | 编译期固化的 WebSocket URL (`CONFIG_LOCAL_WEBSOCKET_URL`) |
| `nvs_ota_overridden` | bool | NVS 是否覆盖了 OTA URL |
| `nvs_ws_overridden` | bool | NVS 是否覆盖了 WebSocket URL |

**使用场景**: 排查 OTA 连接失败问题，确认 NVS 中是否存储了旧的错误 URL。

---

### 5. 清除 NVS 中存储的 URL

**请求**
```
POST /api/device/clear-nvs            # 清除所有 URL（ota_url + websocket_url）
POST /api/device/clear-nvs?key=ota_url     # 仅清除 OTA URL
POST /api/device/clear-nvs?key=websocket_url   # 仅清除 WebSocket URL
```

**响应示例**
```json
{
  "ok": true,
  "cleared": "ota_url,websocket_url"
}
```

**使用示例**
```bash
# 清除所有 NVS 中的 URL，下次启动将使用 CONFIG_OTA_URL / CONFIG_LOCAL_WEBSOCKET_URL
curl -X POST http://192.168.3.22:8080/api/device/clear-nvs
```

**安全约束**
- 仅允许清除 `ota_url` 和 `websocket_url` 两个 key
- 其他 NVS 数据（如 WiFi 凭据、设备配对信息）不受影响
- 清除后立即生效（下次设备启动时读取 `CONFIG_*_URL`）

---

## SD 卡管理 API

### 6. 获取 SD 卡信息

**请求**
```
GET /api/sdcard/info
```

**响应示例**
```json
{
  "mount_point": "/sdcard",
  "total_bytes": 0,
  "used_bytes": 0,
  "free_bytes": 0,
  "log_active": true
}
```

---

### 7. 获取日志文件列表

**请求**
```
GET /api/sdcard/logs
```

**响应示例**
```json
[
  {
    "name": "xiaozhi_boot_1.log",
    "size_bytes": 8646,
    "mtime": 315571736
  },
  {
    "name": "xiaozhi_boot_2.log",
    "size_bytes": 12048,
    "mtime": 315571850
  }
]
```

---

### 8. 下载日志文件

**请求**
```
GET /api/sdcard/logs/<filename>
```

**响应**
- 成功：返回日志文件内容（text/plain）
- 文件不存在：返回 404

**使用示例**
```bash
curl -o boot.log http://192.168.3.22:8080/api/sdcard/logs/xiaozhi_boot_1.log
```

---

### 9. 删除日志文件

**请求**
```
DELETE /api/sdcard/logs/<filename>
```

**响应示例**
```json
{
  "ok": true
}
```

---

### 10. 获取截图文件列表

**请求**
```
GET /api/sdcard/shots
```

**响应示例**
```json
[
  {
    "name": "shot_20250624_143052.jpg",
    "size_bytes": 22048,
    "mtime": 315582252,
    "is_last": true
  },
  {
    "name": "error_OTA_20250624_143100.jpg",
    "size_bytes": 18500,
    "mtime": 315582310,
    "is_last": false
  }
]
```

**字段说明**

| 字段 | 类型 | 说明 |
|------|------|------|
| `name` | string | 截图文件名 |
| `size_bytes` | number | 文件大小（字节） |
| `mtime` | number | 修改时间（Unix 时间戳） |
| `is_last` | bool | 是否为最后一次手动截图 |

---

### 11. 触发屏幕截图

**请求**
```
POST /api/sdcard/shots
```

**响应示例**
```json
{
  "ok": true,
  "file": "pending..."
}
```

**说明**
- 截图在后台异步执行，HTTP 响应立即返回
- 截图完成后，可通过 `/api/sdcard/shots` 获取文件列表
- 截图文件名格式：`shot_YYYYMMDD_HHMMSS.jpg`

---

### 12. 下载截图文件

**请求**
```
GET /api/sdcard/shots/<filename>
```

**响应**
- 成功：返回 JPEG 图片（image/jpeg）
- 文件不存在：返回 404

**使用示例**
```bash
curl -o shot.jpg http://192.168.3.22:8080/api/sdcard/shots/shot_20250624_143052.jpg
```

---

### 13. 删除截图文件

**请求**
```
DELETE /api/sdcard/shots/<filename>
```

**响应示例**
```json
{
  "ok": true
}
```

---

### 14. 删除 SD 卡任意文件

**请求**
```
DELETE /api/sdcard/files/<filename>
```

**响应示例**
```json
{
  "ok": true
}
```

**说明**
- 支持删除 SD 卡上的任意文件（日志、截图等）
- 文件名必须安全（不含路径遍历字符）

---

## Web 管理界面

**请求**
```
GET /
```

**响应**
- 返回 HTML 页面，提供简洁的管理界面
- 支持查看设备状态、日志列表、截图列表
- 支持下载和删除操作

---

## 错误截图机制

当设备发生错误或异常时，自动触发截图并保存到 SD 卡：

- **文件名格式**: `error_<TAG>_YYYYMMDD_HHMMSS.jpg`
- **触发场景**: OTA 失败、网络连接失败、关键错误等
- **API 调用**: `SdCardLogHttpErrorSnapshot(tag, message)`

---

## 使用示例

### 命令行工具

```bash
# 查看设备状态
curl http://192.168.3.22:8080/api/device/status | jq

# 触发截图
curl -X POST http://192.168.3.22:8080/api/sdcard/shots

# 等待截图完成后下载
sleep 5
curl -o screenshot.jpg http://192.168.3.22:8080/api/sdcard/shots/shot_20250624_143052.jpg

# 查看日志
curl http://192.168.3.22:8080/api/device/logs

# 重启设备
curl -X POST http://192.168.3.22:8080/api/device/reboot
```

### Python 示例

```python
import requests

BASE_URL = "http://192.168.3.22:8080"

# 获取设备状态
status = requests.get(f"{BASE_URL}/api/device/status").json()
print(f"WiFi: {status['wifi_connected']}")
print(f"Free heap: {status['memory']['free_heap']}")

# 触发截图
result = requests.post(f"{BASE_URL}/api/sdcard/shots").json()
print(f"Screenshot triggered: {result}")

# 获取截图列表
shots = requests.get(f"{BASE_URL}/api/sdcard/shots").json()
for shot in shots:
    print(f"  {shot['name']}: {shot['size_bytes']} bytes")

# 下载截图
if shots:
    url = f"{BASE_URL}/api/sdcard/shots/{shots[0]['name']}"
    with open(shots[0]['name'], 'wb') as f:
        f.write(requests.get(url).content)
```

---

## 注意事项

1. **IP 地址变化**: 设备 IP 可能因重启改变，建议在路由器设置静态 IP
2. **访问时机**: 需要设备已连接 WiFi 后才能访问 HTTP 服务
3. **截图异步**: POST 截图请求立即返回，实际截图在后台执行
4. **文件名安全**: 所有文件操作都会检查文件名安全性，防止路径遍历攻击
5. **日志大小**: `/api/device/logs` 只返回最后 10KB 日志

---

## 相关文件

- [main/sdcard_log_http.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/sdcard_log_http.h) - HTTP 服务接口声明
- [main/sdcard_log_http.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/sdcard_log_http.cc) - HTTP 服务实现
- [CODE_WIKI.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/CODE_WIKI.md) - 项目技术文档