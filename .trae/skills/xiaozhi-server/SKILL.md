---
name: "xiaozhi-server"
description: "Reference and operations for xiaozhi-esp32-server-java backend (HTTP API 8091 + WebSocket 8092). Invoke when user asks about backend service status, OTA activation endpoints, device binding, STT/LLM/TTS config, MySQL/Redis data, or troubleshooting device-server connectivity."
---

# Xiaozhi ESP32 Server Java

Backend service reference for the [xiaozhi-esp32-server-java](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32-server-java) project that powers the Xiaozhi ESP32 AI device.

## Project Layout

```
/Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32-server-java/
├── start.sh                    # 一键启动/停止所有服务
├── bin/                        # 启动脚本
├── xiaozhi-server/             # 主 API 服务 (port 8091)
├── xiaozhi-dialogue/           # WebSocket 语音对话服务 (port 8092)
├── docs/                       # 项目文档
├── docker-compose.yml          # DB + Redis Docker 配置
└── ESP32与JavaServer技术方案.md  # 集成方案
```

## Running Services

| Service | Port | Protocol | Purpose |
|---------|------|----------|---------|
| `xiaozhi-server` | **8091** | HTTP REST | OTA 激活 / 设备管理 / 用户/角色/AI 配置 |
| `xiaozhi-dialogue` | **8092** | WebSocket | 实时语音对话 (STT/LLM/TTS/MCP/IoT) |
| 管理后台 Web | 8084 | HTTP | Web UI (admin / 123456) |
| MySQL | 3306 | MySQL | 持久化 (Flyway 自动迁移 V0-V10) |
| Redis | 6379 | Redis | Sa-Token 会话 / 分布式锁 / 工具注册 |

## Quick Reference

### OTA Activation (ESP32 设备调用)

**Request**:
```bash
curl -X POST http://<SERVER_IP>:8091/api/device/ota \
  -H "Content-Type: application/json" \
  -H "Device-Id: aa:bb:cc:dd:ee:ff" \
  -H "Client-Id: <client-uuid>" \
  -H "Activation-Version: 1" \
  -H "User-Agent: esp32-client/2.2.6" \
  -d '{"application":{"version":"2.2.6"},"board":{"type":"waveshare-esp32-s3-touch-lcd-1.85b","mac":"aa:bb:cc:dd:ee:ff"}}'
```

**Response**:
```json
{
  "server_time": {"timestamp": 1234567890, "timezone_offset": 480},
  "websocket": {
    "url": "ws://<SERVER_IP>:8092/ws/xiaozhi/v1/",
    "token": ""
  },
  "firmware": {
    "version": "1.0.0",
    "url": "http://<SERVER_IP>:8091/api/device/ota"
  },
  "activation": {
    "code": "123456",
    "message": "激活验证码",
    "challenge": "<device-mac>"
  }
}
```

### Service Health Check

```bash
# 检查服务进程
lsof -i :8091   # xiaozhi-server
lsof -i :8092   # xiaozhi-dialogue
lsof -i :8084   # 管理后台

# 测试 OTA 端点
curl -s -X POST http://127.0.0.1:8091/api/device/ota \
  -H "Device-Id: test:00:00:00:00" -d '{}'

# 测试 WebSocket (Python)
python3 -c "
import websocket, json
ws = websocket.create_connection('ws://127.0.0.1:8092/ws/xiaozhi/v1/', timeout=3,
  header=['Device-Id: test:00:00:00:00'])
ws.send(json.dumps({'type':'hello','version':1}))
print(ws.recv())
ws.close()
"
```

### Start / Stop / Status

```bash
cd /Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32-server-java
./start.sh all       # 启动所有服务 (Docker DB + 后端 + 前端)
./start.sh stop      # 停止所有服务
./start.sh status    # 查看服务状态
./start.sh logs      # 跟踪日志
```

## Key API Endpoints

### Device Management (`xiaozhi-server` 8091)
| Endpoint | Method | Auth | Purpose |
|----------|--------|------|---------|
| `/api/device/ota` | POST | Device-Id | OTA 激活 (设备调用) |
| `/api/device/ota/activate` | POST | User | 提交激活码绑定设备 |
| `/api/device/page` | GET | Admin | 分页查询设备 |
| `/api/device/unbind` | POST | User | 解绑设备 |
| `/api/device/delete/{id}` | DELETE | Admin | 删除设备 |

### User & Role
| Endpoint | Method | Purpose |
|----------|--------|---------|
| `/api/user/login` | POST | 登录 (默认 admin/123456) |
| `/api/user/register` | POST | 注册用户 |
| `/api/user/info` | GET | 当前用户信息 |
| `/api/role/page` | GET | 角色列表 |
| `/api/role/save` | POST | 保存角色 |

### AI Configuration
| Endpoint | Method | Purpose |
|----------|--------|---------|
| `/api/config/page` | GET | AI 配置列表 (STT/LLM/TTS) |
| `/api/config/save` | POST | 保存 AI 配置 |
| `/api/config/enable/{id}` | POST | 启用某个配置 |
| `/api/config/test` | POST | 测试连通性 |

### WebSocket (`xiaozhi-dialogue` 8092)
- **Endpoint**: `/ws/xiaozhi/v1/`
- **Required Header**: `Device-Id: aa:bb:cc:dd:ee:ff`
- **Optional Header**: `Authorization: Bearer <token>`
- **Messages**: `hello`, `listen`, `stt`, `tts`, `llm`, `abort`, `mcp`, `iot`, `goodbye`, binary(opus)

## Configuration

### Main Config: `xiaozhi-server/src/main/resources/application.yml`

**关键配置项**:
```yaml
server:
  port: 8091                       # HTTP 端口

spring:
  profiles:
    active: dev                    # dev / prod
  datasource:                       # MySQL (HikariCP)
    url: jdbc:mysql://localhost:3306/xiaozhi
    username: root
    password: root
  data.redis:                      # Redis (Lettuce + Redisson)
    host: localhost
    port: 6379
  flyway:
    enabled: true                  # 自动迁移 db/migration/V0-V10

xiaozhi:
  runtime:
    native-lib-dir: lib            # 本地库 (sherpa-onnx, vosk)
    vosk-model-dir: models/vosk-model
    tts-models-dir: models/tts
  vad:
    model:
      path: models/silero_vad.onnx
  check:
    inactive:
      session: true                # 检查不活跃会话

sa-token:
  timeout: 2592000                 # Token 有效期 30 天
  token-prefix: Bearer
```

### LAN IP 自动检测

**OTA 响应中的 `websocket.url` 来源**:
`ServerAddressProvider` 根据 `xiaozhi.server.domain` 配置返回:
- `domain: ""` (留空) → 自动检测本机 LAN IP (推荐开发)
- `domain: "xiaozhi.example.com"` → 使用 `wss://xiaozhi.example.com/...`

如果 LAN IP 错误,设备无法连接。修改后需重启 `xiaozhi-server`。

## Common Operations

### Database Queries (MySQL)

```sql
-- 查看所有设备
SELECT id, device_id, name, mac_address, last_active_at, is_banned
FROM sys_device ORDER BY last_active_at DESC;

-- 按 MAC 查询
SELECT * FROM sys_device WHERE device_id = 'a0:f2:62:e4:3a:40';

-- 查看 AI 配置
SELECT id, config_type, provider, model, api_key, enabled
FROM sys_config;

-- 查看角色
SELECT id, name, description, system_prompt FROM sys_role;

-- 查看用户
SELECT id, username, role, created_at FROM sys_user;
```

### Redis Queries

```bash
# Sa-Token 会话
redis-cli KEYS 'satoken:*'

# 全局 MCP 工具注册
redis-cli HGETALL 'xiaozhi:global:tools'

# 分布式锁状态
redis-cli KEYS '*lock*'

# 活跃 session
redis-cli KEYS 'xiaozhi:session:*'
```

### Log Locations

```bash
tail -f xiaozhi-server/logs/xiaozhi-server.log        # 主 API 日志
tail -f xiaozhi-dialogue/logs/xiaozhi-dialogue.log    # WebSocket 日志
```

### Admin Web UI

- URL: `http://localhost:8084`
- Default: `admin / 123456`
- 功能: 设备管理 / 用户管理 / 角色管理 / AI 配置 / 实时对话监控

## Troubleshooting

### ESP32 OTA 连接失败

```bash
# 1. 验证 xiaozhi-server 运行
lsof -i :8091 || echo "✗ 服务未启动"

# 2. 从 ESP32 设备网络测试连通性
nc -zv <SERVER_IP> 8091

# 3. 检查 ESP32 端配置
curl http://<ESP32_IP>:8080/api/device/ota-url | python3 -m json.tool

# 4. 查看 xiaozhi-server 收到的请求日志
grep "OTA" xiaozhi-server/logs/*.log | tail -20
```

**NVS 覆盖问题**: ESP32 可能存了旧的 URL,通过 `POST /api/device/clear-nvs` 清除。

### WebSocket 连接失败

```bash
# 1. 验证 xiaozhi-dialogue 运行
lsof -i :8092

# 2. 测试 WebSocket 握手
curl -i -H "Connection: Upgrade" -H "Upgrade: websocket" \
  -H "Sec-WebSocket-Version: 13" \
  -H "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==" \
  -H "Device-Id: a0:f2:62:e4:3a:40" \
  http://127.0.0.1:8092/ws/xiaozhi/v1/

# 3. 查看 dialogue 日志
grep "WebSocket" xiaozhi-dialogue/logs/*.log | tail -20
```

### 设备激活卡住

1. **获取激活码**: ESP32 设备日志中 `[+X.XXX] Application: Alert [link] 激活设备: 123456`
2. **Web 端绑定**: 浏览器打开 `http://localhost:8084` → 设备管理 → 找到待激活设备 → 输入 6 位激活码
3. **设备重新 OTA**: 设备 5 秒后自动重试 OTA,会通过认证

### STT / LLM / TTS 不工作

```bash
# 1. 查看 dialogue 日志中具体错误
grep -E "STT|LLM|TTS|ERROR" xiaozhi-dialogue/logs/*.log | tail -30

# 2. 测试 AI 配置
curl -X POST http://127.0.0.1:8091/api/config/test \
  -H "Authorization: Bearer <admin-token>" \
  -d '{"configType":"llm","provider":"openai","model":"gpt-3.5-turbo"}'

# 3. 已知问题: Vosk STT 初始化失败 (vosk_recognizer_set_grm symbol not found)
# 解决: 改用云端 ASR (阿里云 FunASR / 腾讯云)
```

## Notes

- **首次启动**: 必须运行 `./start.sh all`,会通过 docker-compose 启动 MySQL/Redis 并自动 Flyway 迁移数据库
- **IP 变更**: 如果本机 IP 变化,需修改 `application.yml` 中 `xiaozhi.server.domain` 或留空让 `ServerAddressProvider` 自动检测
- **Token 认证**: 当前 WebSocket 握手依赖 MAC 地址 (Device-Id Header),token 字段保留为空
- **Vosk 本地 STT**: 当前已知有兼容性问题,推荐配置云端 ASR
- **OTA firmware.version 固定为 1.0.0**: 暂未实现真正的固件下载接口

## Related Documentation

- [ESP32与JavaServer技术方案.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/doc/ESP32与JavaServer技术方案.md) - 集成方案
- [小智AI与后台服务器交互协议汇总.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/doc/小智AI与后台服务器交互协议汇总.md) - WebSocket 协议详细说明
- [SERVER_INFO.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32-server-java/SERVER_INFO.md) - 服务运行信息