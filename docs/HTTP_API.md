# ESP32 HTTP API 文档

> **版本**: v1.3  
> **更新日期**: 2026-06-26  
> **服务端口**: 8080

> 💡 **重要**：本文档中的 **所有 HTTP API 端点** 都已通过 [http_api_unified.h/cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/http_api_unified.h) 抽取出统一的业务逻辑层，并同时注册为 **MCP 工具**（供大模型调用）。详见 [MCP 工具对照表](#mcp-工具对照表)。

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
| `/api/sdcard/files` | GET | 列出 SD 卡任意目录的文件（支持 `?path=` 和 `?recursive=1`） |
| `/api/sdcard/files/<filename>` | GET | 下载 SD 卡任意文件（图片/GIF/音频/视频/任意二进制） |
| `/api/sdcard/files/` | POST | **上传**文件到 SD 卡（请求体为原始二进制，自动按扩展名识别类型，支持 `?display=1&...` 上传后自动显示） |
| `/api/sdcard/files/` | DELETE | 删除 SD 卡任意文件 |
| `/api/display/show` | POST | **显示** SD 卡上的资源（JSON body：`path`、`x`、`y`、`scale`、`duration_ms`、`loop`） |
| `/api/display/hide` | POST | 隐藏当前显示的资源 |
| `/api/audio/play` | POST | **播放音乐**（JSON body：`path` 或 `url` 或 `playlist` + `loop`；MP3/OGG/WAV/AAC/FLAC 全支持） |
| `/api/audio/control` | POST | 控制播放（JSON body：`action` = `pause`/`resume`/`stop`） |
| `/api/audio/status` | GET | 查询当前播放状态（state/progress/file/error） |
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
DELETE /api/sdcard/files/
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

## 通用文件管理 API（图片 / GIF / 音频 / 视频 / 任意二进制）

通用文件管理 API 允许外部通过 HTTP 推送或拉取 SD 卡上的任意文件（如资源、素材、固件附件、固件资源包等）。

与日志、截图 API 不同，这组 API 支持**任意扩展名**和**子目录**，可由调用方任意组织文件结构。

### 15. 列出 SD 卡文件

**请求**
```
GET /api/sdcard/files?path=<dir>&recursive=<0|1>
```

**查询参数**

| 参数 | 类型 | 必填 | 默认值 | 说明 |
|------|------|------|--------|------|
| `path` | string | 否 | `""`（根目录） | 相对 SD 卡挂载点的子目录路径，例如 `images/gif` |
| `recursive` | int | 否 | `0` | 是否递归列出子目录：`1`=递归，`0`=仅当前目录 |

**响应示例**
```json
[
  {
    "name": "boot.gif",
    "path": "images/boot.gif",
    "size_bytes": 245760,
    "mtime": 315582252,
    "is_dir": false,
    "content_type": "image/gif"
  },
  {
    "name": "voices",
    "path": "audio/voices",
    "size_bytes": 0,
    "mtime": 315582200,
    "is_dir": true,
    "content_type": null
  }
]
```

**字段说明**

| 字段 | 类型 | 说明 |
|------|------|------|
| `name` | string | 文件或目录名（不含路径） |
| `path` | string | 相对 SD 卡根的完整路径 |
| `size_bytes` | number | 大小（字节），目录为 0 |
| `mtime` | number | 修改时间（Unix 时间戳） |
| `is_dir` | bool | 是否为目录 |
| `content_type` | string\|null | 推测的 MIME 类型（目录为 null） |

**使用示例**
```bash
# 列出根目录
curl http://192.168.3.22:8080/api/sdcard/files

# 列出 images/ 子目录
curl "http://192.168.3.22:8080/api/sdcard/files?path=images"

# 递归列出全部文件
curl "http://192.168.3.22:8080/api/sdcard/files?recursive=1"
```

---

### 16. 上传文件到 SD 卡

**请求**
```
POST /api/sdcard/files/<path>
Content-Type: application/octet-stream
Content-Length: <size>

<raw binary data>
```

**URI 路径说明**

`<path>` 是文件相对 SD 卡挂载点的目标路径，可包含子目录。路径必须安全（不允许 `..`、不能以 `/` 开头）。父目录若不存在会自动创建。

**支持的文件类型（按扩展名识别）**

| 扩展名 | MIME | 用途 |
|--------|------|------|
| `.jpg` / `.jpeg` | `image/jpeg` | 静态图片 |
| `.png` | `image/png` | 静态图片 |
| `.gif` | `image/gif` | 动图 |
| `.webp` | `image/webp` | 静态图片 |
| `.bmp` | `image/bmp` | 静态图片 |
| `.mp3` | `audio/mpeg` | 音频 |
| `.wav` | `audio/wav` | 音频 |
| `.ogg` | `audio/ogg` | 音频 |
| `.m4a` | `audio/mp4` | 音频 |
| `.mp4` | `video/mp4` | 视频 |
| `.avi` | `video/x-msvideo` | 视频 |
| `.mov` | `video/quicktime` | 视频 |
| `.txt` / `.log` / `.json` / `.xml` | `text/plain` | 文本 |
| 其他 | `application/octet-stream` | 任意二进制 |

**响应示例（成功）**
```json
{
  "ok": true,
  "path": "images/boot.gif",
  "size_bytes": 245760,
  "content_type": "image/gif"
}
```

**响应示例（失败）**
```json
{
  "ok": false,
  "error": "unsafe path"
}
```

**使用示例**

```bash
# 上传一张 GIF 到 images/ 子目录
curl -X POST \
  --data-binary @boot.gif \
  http://192.168.3.22:8080/api/sdcard/files/images/boot.gif

# 上传一段 MP3 到 audio/voices/
curl -X POST \
  --data-binary @hello.mp3 \
  http://192.168.3.22:8080/api/sdcard/files/audio/voices/hello.mp3

# 上传视频到 videos/
curl -X POST \
  --data-binary @intro.mp4 \
  http://192.168.3.22:8080/api/sdcard/files/videos/intro.mp4
```

**安全约束**
- 路径必须安全（不含 `..`，不能以 `/` 开头）
- 路径长度上限 200 字节
- 上传大小受 HTTP server `max_upload_size` 限制（默认 32KB，可在 menuconfig 调整）
- 自动覆盖已有同名文件

---

### 17. 下载 SD 卡文件

**请求**
```
GET /api/sdcard/files/<path>
```

**响应**
- 成功：根据扩展名返回对应 MIME 类型 + 文件二进制内容
- 文件不存在：返回 404

**响应头**
```
Content-Type: <根据扩展名>
Content-Disposition: attachment; filename="<filename>"
Content-Length: <size>
```

**使用示例**

```bash
# 下载 GIF 并保存到本地
curl -o boot.gif http://192.168.3.22:8080/api/sdcard/files/images/boot.gif

# 下载音频
curl -o hello.mp3 http://192.168.3.22:8080/api/sdcard/files/audio/voices/hello.mp3

# 下载视频
curl -o intro.mp4 http://192.168.3.22:8080/api/sdcard/files/videos/intro.mp4

# 浏览器中直接预览（设置 inline）
curl -H "Accept: */*" --output - http://192.168.3.22:8080/api/sdcard/files/images/wallpaper.jpg | open -a Preview
```

---

### 18. 删除 SD 卡任意文件（增强版）

> 第 14 节描述的是根目录文件删除。此版本支持**任意子目录**和**任意文件名**。

**请求**
```
DELETE /api/sdcard/files/<path>
```

**响应示例**
```json
{
  "ok": true
}
```

**使用示例**
```bash
# 删除子目录文件
curl -X DELETE http://192.168.3.22:8080/api/sdcard/files/images/old.gif
```

---

## 资源显示 API（设备端加载 + 显示）

支持两种方式触发设备显示 SD 卡上的资源（图片、GIF 等）：

### 19. 独立触发显示：POST /api/display/show

**请求**
```
POST /api/display/show
Content-Type: application/json

{
  "path":        "images/boot.gif",
  "x":           0,
  "y":           0,
  "scale":       1.0,
  "duration_ms": 5000,
  "loop":        true
}
```

**字段说明**

| 字段 | 类型 | 必填 | 默认 | 说明 |
|------|------|------|------|------|
| `path` | string | ✅ | - | SD 卡上文件相对路径，如 `images/boot.gif` |
| `x` | int | 否 | 0 | X 偏移（像素） |
| `y` | int | 否 | 0 | Y 偏移（像素） |
| `scale` | float | 否 | 1.0 | 缩放比例（0.1~4.0） |
| `duration_ms` | int | 否 | 0 | 自动隐藏时长，0 = 永久 |
| `loop` | bool | 否 | false | 仅 GIF：是否循环播放 |

**响应示例（成功）**
```json
{
  "ok": true,
  "path": "images/boot.gif",
  "x": 0,
  "y": 0,
  "scale": 1.0,
  "duration_ms": 5000,
  "loop": true
}
```

**响应示例（失败）**
```json
{
  "ok": false,
  "error": "file not found"
}
```

**使用示例**

```bash
# 显示 GIF
curl -X POST -H "Content-Type: application/json" \
  -d '{"path":"images/boot.gif","loop":true}' \
  http://192.168.3.22:8080/api/display/show

# 显示 PNG，5 秒后自动消失
curl -X POST -H "Content-Type: application/json" \
  -d '{"path":"images/logo.png","duration_ms":5000}' \
  http://192.168.3.22:8080/api/display/show

# 指定位置和缩放
curl -X POST -H "Content-Type: application/json" \
  -d '{"path":"images/icon.jpg","x":100,"y":100,"scale":0.5}' \
  http://192.168.3.22:8080/api/display/show
```

---

### 20. 上传时自动显示：POST /api/sdcard/files/<path>?display=1

在上传文件时通过 query 参数触发自动显示，一步完成 **上传 + 显示**。

**请求**
```
POST /api/sdcard/files/images/boot.gif?display=1&loop=1&duration_ms=5000&x=0&y=0&scale=1.0
Content-Type: application/octet-stream

<raw binary data>
```

**Query 参数**

| 参数 | 类型 | 默认 | 说明 |
|------|------|------|------|
| `display` | 0\|1 | 0 | 是否在上传成功后自动显示 |
| `x` | int | 0 | X 偏移 |
| `y` | int | 0 | Y 偏移 |
| `scale` | float | 1.0 | 缩放比例 |
| `duration_ms` | int | 0 | 自动隐藏时长（毫秒），0 = 永久 |
| `loop` | 0\|1 | 0 | GIF 是否循环 |

**响应示例**
```json
{
  "ok": true,
  "path": "images/boot.gif",
  "size_bytes": 245760,
  "content_type": "image/gif",
  "displayed": true
}
```

如果 display 失败，会附 `display_error` 字段说明原因。

**使用示例**

```bash
# 上传并立即循环显示 GIF
curl -X POST --data-binary @boot.gif \
  "http://192.168.3.22:8080/api/sdcard/files/images/boot.gif?display=1&loop=1"

# 上传并显示 3 秒后自动隐藏
curl -X POST --data-binary @intro.png \
  "http://192.168.3.22:8080/api/sdcard/files/images/intro.png?display=1&duration_ms=3000"

# 上传到指定位置并缩放
curl -X POST --data-binary @icon.jpg \
  "http://192.168.3.22:8080/api/sdcard/files/images/icon.jpg?display=1&x=50&y=50&scale=0.3"
```

---

### 21. 隐藏当前资源：POST /api/display/hide

**请求**
```
POST /api/display/hide
```

**响应示例**
```json
{
  "ok": true,
  "note": "hide not fully implemented"
}
```

**使用示例**
```bash
curl -X POST http://192.168.3.22:8080/api/display/hide
```

---

## WiFi 配置备份管理 API（v2.2.6+）

为解决 **NVS 丢失 WiFi 凭据后需重新配网** 的问题（如：固件升级失败回滚、首次烧录、分区表变更、用户主动 `clear-nvs` 等），新增 **三级回退** 启动逻辑：

```
NVS 凭据存在  → 直接连接 WiFi  ✅
        ↓ (无)
SD 卡备份存在 → 从 /sdcard/wifi_config.json 恢复到 NVS → 自动重连  ✅
        ↓ (无)
AP 配网模式  → 用户手动配置  ✅
```

### 三级回退机制

**触发条件**：`WifiBoard::TryWifiConnect()` 检测 NVS 中无任何 SSID 时。

**SD 卡备份位置**：`/sdcard/wifi_config.json`，格式：

```json
{
  "version": 1,
  "saved_at": 1782514187,
  "networks": [
    {"ssid": "HUAWEI-9YQAVW", "password": "..."}
  ]
}
```

**关键日志时序**：

```
W (2322) WifiBoard: SD card not mounted yet, starting background retry task
W (2322) WifiBoard: No SSID in NVS or SD card (yet), entering WiFi config mode
I (3902) WifiBoard: WiFi config mode entered
I (4082) waveshare_lcd_1_85: SD已挂载 58.04 GB
[+4.214] W (4382) WifiBoard: SD card backup appeared after 1500 ms, found 1 networks, restoring
[+4.219] I (4382) WifiConfigBackup: Restored 1 networks from SD card
[+4.219] I (4392) WifiBoard: Restored 1 networks from SD card (delayed), retrying connection
[+4.322] I (4492) WifiBoard: WiFi config mode exited
[+4.322] I (4492) WifiBoard: Starting WiFi connection attempt
[+4.602] I (4767) WifiBoard: WiFi connecting to HUAWEI-9YQAVW
[+6.480] I (6647) WifiBoard: Connected to WiFi: HUAWEI-9YQAVW
[+6.536] I (6697) WifiConfigBackup: Synced 1 networks to /sdcard/wifi_config.json
```

### 22. 查看 WiFi 配置状态

```
GET /api/wifi/status
```

**响应示例**：

```json
{
  "nvs_count": 1,
  "sd_card_count": 1,
  "sd_card_has_backup": true,
  "sd_card_mounted": true,
  "backup_file": "/sdcard/wifi_config.json",
  "nvs_networks": [
    {"ssid": "HUAWEI-9YQAVW"}
  ]
}
```

**字段说明**：
- `nvs_count`：NVS 中保存的 SSID 数量
- `sd_card_count`：SD 卡备份文件中包含的 SSID 数量
- `sd_card_has_backup`：SD 卡上是否有备份文件
- `sd_card_mounted`：SD 卡是否已挂载
- `backup_file`：备份文件路径
- `nvs_networks`：NVS 中所有 SSID（不含密码，避免泄露）

### 23. 清空 NVS WiFi 凭据（保留 SD 卡备份）

```
POST /api/wifi/clear-nvs
```

调用后：
1. NVS 中所有 SSID + 密码被清空
2. SD 卡备份 `/sdcard/wifi_config.json` 保留不动
3. 设备当前 WiFi 连接继续维持（不重启）
4. 下次重启时自动从 SD 卡恢复

**响应示例**：

```json
{
  "ok": true,
  "note": "NVS cleared. SD card backup at /sdcard/wifi_config.json preserved. Reboot device or POST /api/wifi/restore to recover from SD card."
}
```

**用途**：
- 验证三级回退机制（模拟 NVS 丢失场景）
- 切换到其他 WiFi 网络
- 重置所有 WiFi 配置

### 24. 手动从 SD 卡恢复 WiFi 凭据

```
POST /api/wifi/restore
```

无需重启即可手动触发从 SD 卡恢复到 NVS。返回恢复的网络数量。

**响应示例**：

```json
{
  "restored": 1,
  "note": "SSID(s) restored from SD card to NVS. Device will reconnect on next WiFi scan."
}
```

**典型场景**：
- 测试 SD 卡恢复功能
- 在不掉电的情况下切换 WiFi 凭据源

### 自动备份时机

`WifiConfigBackup::SyncToSdCard()` 会在以下时机自动同步 NVS → SD 卡：

| 触发点 | 说明 |
|--------|------|
| WiFi 连接成功 | `WifiBoard::OnNetworkEvent(Connected)` 后立即同步 |
| 配网成功 | `WifiConfigUI::SaveWifiCredentials()` 调用 `AppendToSdCard()` |
| 删除已保存 WiFi | `WifiConfigUI::DeleteSavedWifi()` 后 `SyncToSdCard()` 重建备份 |

### 备份格式约束

- 最多保存 `WIFI_CONFIG_MAX_NETWORKS = 10` 个网络
- 同一 SSID 重复保存会更新密码
- 删除一个网络后会从备份中移除
- 密码以明文存储（仅本机访问，不外传）

---

## MCP 工具对照表

所有 HTTP API 端点均已通过统一业务逻辑层（[http_api_unified.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/http_api_unified.h) / [http_api_unified.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/http_api_unified.cc)）抽取，并作为 **MCP 工具** 在 [mcp_server.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/mcp_server.cc) 的 `AddUserOnlyTools()` 中注册，大模型可通过 MCP 协议调用本地 HTTP API 所做的一切操作。

### 完整对照表

| HTTP 端点 | 方法 | MCP 工具 | 说明 |
|-----------|------|----------|------|
| `/api/device/status` | GET | `self.device.get_status` | 设备状态（WiFi/SD/内存/uptime/版本/板型） |
| `/api/device/logs` | GET | `self.device.get_logs` | 最新日志（参数：`max_bytes`） |
| `/api/device/reboot` | POST | `self.reboot` ⚠️ | 重启设备（保留原有 Application 路径） |
| `/api/device/ota-url` | GET | `self.device.get_ota_url` | 查询 OTA/WS URL 配置（NVS vs build） |
| `/api/device/clear-nvs` | POST | `self.device.clear_nvs` | 清除 NVS 中的 OTA/WS URL（参数：`key`） |
| `/api/wifi/status` | GET | `self.wifi.get_status` | WiFi 配置状态（NVS + SD 卡备份） |
| `/api/wifi/clear-nvs` | POST | `self.wifi.clear_nvs` | 清空 NVS WiFi 凭据（保留 SD 卡备份） |
| `/api/wifi/restore` | POST | `self.wifi.restore_from_sd` | 从 SD 卡恢复 WiFi 凭据到 NVS |
| `/api/sdcard/info` | GET | `self.sdcard.get_info` | SD 卡信息 |
| `/api/sdcard/logs` | GET | `self.sdcard.list_logs` | 日志文件列表 |
| `/api/sdcard/logs/` | DELETE | `self.sdcard.delete_log` | 删除日志（参数：`name`） |
| `/api/sdcard/shots` | GET | `self.sdcard.list_shots` | 截图文件列表 |
| `/api/sdcard/shots` | POST | `self.sdcard.trigger_snapshot` | 触发屏幕截图 |
| `/api/sdcard/shots/` | DELETE | `self.sdcard.delete_shot` | 删除截图（参数：`name`） |
| `/api/sdcard/files?path=&recursive=` | GET | `self.files.list` | 列出 SD 卡任意目录（参数：`path`、`recursive`） |
| `/api/sdcard/files/` | POST | `self.files.upload` | 上传文件（参数：`path`、`data_base64`、`display` 等） |
| `/api/sdcard/files/` | DELETE | `self.files.delete` | 删除文件（参数：`path`） |
| `/api/display/show` | POST | `self.display.show` | 显示 SD 卡资源（参数：`path`、`x`、`y`、`scale`、`duration_ms`、`loop`） |
| `/api/display/hide` | POST | `self.display.hide` | 隐藏当前资源 |

> ⚠️ `self.reboot` 使用原有 Application::Reboot 路径（确保事件循环正确处理），其他工具复用统一 API 层。

### MCP 调用示例（通过 JSON-RPC）

**获取设备状态**：
```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "tools/call",
  "params": {
    "name": "self.device.get_status",
    "arguments": {}
  }
}
```

**列出日志**：
```json
{
  "method": "tools/call",
  "params": {
    "name": "self.sdcard.list_logs",
    "arguments": {}
  }
}
```

**触发截图**：
```json
{
  "method": "tools/call",
  "params": {
    "name": "self.sdcard.trigger_snapshot",
    "arguments": {}
  }
}
```

**上传 GIF 并自动显示**（`data_base64` 是文件内容的 Base64 编码）：
```json
{
  "method": "tools/call",
  "params": {
    "name": "self.files.upload",
    "arguments": {
      "path": "images/boot.gif",
      "data_base64": "R0lGODlhAQABAIAAAOLi4v///yH5BAAAAAAALAAAAAABAAEAAAICRAEAOw==",
      "display": true,
      "loop": true,
      "duration_ms": 5000,
      "scale": 100
    }
  }
}
```

**显示 SD 卡上的现有 GIF**：
```json
{
  "method": "tools/call",
  "params": {
    "name": "self.display.show",
    "arguments": {
      "path": "images/boot.gif",
      "loop": true
    }
  }
}
```

### HTTP API 与 MCP 工具的等价性

两者**共享同一份业务逻辑实现**（[http_api_unified.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/http_api_unified.cc)），保证行为一致：

- ✅ 路径安全校验（防 `..`、隐藏文件、绝对路径）
- ✅ MIME 类型识别（25+ 种扩展名）
- ✅ 自动创建父目录
- ✅ 错误信息格式统一

**HTTP 与 MCP 的差异**：

| 维度 | HTTP | MCP |
|------|------|-----|
| 调用方 | 任何 HTTP 客户端 | 大模型（通过 MCP 协议） |
| 数据格式 | JSON / 二进制 | JSON-RPC（binary 用 base64 编码） |
| 鉴权 | 无 | 走 MCP 鉴权协议 |
| 触发时机 | 用户手动 / 自动化脚本 | 大模型决策后自动 |
| 适用场景 | 调试 / 自动化 | 让 AI 直接操控设备 |

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