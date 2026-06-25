# ESP32 固件 + xiaozhi-esp32-server-java 技术方案

> **文档版本**: v1.0
> **更新日期**: 2026-06-23
> **项目**: xiaozhi-esp32（固件） + xiaozhi-esp32-server-java（Java 服务端）

---

## 一、总体架构

```
┌─────────────────────────────────────────────────────────┐
│                     ESP32 设备端                          │
│  ┌─────────┐  ┌─────────┐  ┌──────────┐  ┌────────┐│
│  │ 唤醒词   │→ │ 音频采集  │→ │ VAD 检测  │→ │ Opus  ││
│  │ 检测     │  │ (16kHz)  │  │          │  │ 编码   ││
│  └─────────┘  └─────────┘  └──────────┘  └───┬────┘│
│                                               │      │
└───────────────────────────────────────────────│──────│
                                                │      │
                    WebSocket                   │      │
                    Binary                      │      │
                    (Opus 音频帧)               │      │
                        │                        │      │
                        ▼                        │      │
┌───────────────────────────────────────────────│──────│
│                                             │      │
│  ┌─────────────┐  ┌─────────────────────┐  │      │
│  │  xiaozhi-  │  │   xiaozhi-dialogue  │  │      │
│  │  server     │  │   (Java)            │  │      │
│  │  (Java)    │  │   Port: 8092        │  │      │
│  │  Port:8091 │  │                     │  │      │
│  └──────┬──────┘  └──────────┬──────────┘  │      │
│         │                       │              │      │
│         │  MySQL / Redis       │              │      │
│         ▼                       ▼              │      │
│  ┌─────────────┐  ┌─────────────────────┐   │      │
│  │  数据持久化  │  │   语音对话管道         │   │      │
│  │  设备管理    │  │   STT → LLM → TTS    │   │      │
│  │  OTA 激活    │  │   VAD / AEC          │   │      │
│  └─────────────┘  └─────────────────────┘   │      │
│                                              │      │
└───────────────────────────────────────────────│──────│
                                               │      │
                   HTTP REST                    │      │
                   (OTA 激活)                   │      │
                       │                        │      │
                       ▼                        ▼      ▼
              ┌─────────────────────────────────┐
              │      MySQL 8.0 + Redis 7       │
              └─────────────────────────────────┘
```

---

## 二、协议兼容性分析

### 2.1 消息类型对照表

| 消息方向 | type | ESP32 固件 | Java Server | 状态 |
|---------|------|-----------|-------------|------|
| **设备 → 服务端** | `hello` | ✅ [ws_protocol.cc#L203](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/protocols/websocket_protocol.cc#L203) | ✅ `HelloMessage` | ✅ |
| | `listen` | ✅ [protocol.cc#L57](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/protocols/protocol.cc#L57) | ✅ `ListenMessage` | ✅ |
| | `abort` | ✅ [protocol.cc#L41](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/protocols/protocol.cc#L41) | ✅ `AbortMessage` | ✅ |
| | `goodbye` | ✅ | ✅ `GoodbyeMessage` | ✅ |
| | `mcp` | ✅ MCP JSON-RPC | ✅ `DeviceMcpMessage` | ✅ |
| | `iot` | ✅ | ✅ `IotMessage` | ✅ |
| | Binary (Opus) | ✅ 音频帧上传 | ✅ `handleBinaryMessage()` | ✅ |
| **服务端 → 设备** | `hello` (resp) | ✅ | ✅ `HelloMessageResp` | ✅ |
| | `stt` | ✅ 屏幕显示文本 | ✅ DialogueService | ✅ |
| | `tts` (start/stop/sentence_start) | ✅ | ✅ `MessageSender` | ✅ |
| | Binary (Opus TTS) | ✅ Opus 解码播放 | ✅ `Synthesizer` | ✅ |
| | `llm` (emotion) | ✅ 表情更新 | ✅ `PersonaListener` | ✅ |
| | `mcp` (tool call) | ✅ 执行工具 | ✅ `DeviceMcpService` | ✅ |
| | `system` (reboot) | ✅ | ❓ 待验证 | — |
| | `custom` | ✅ | ❓ 待验证 | — |

### 2.2 结论

**WebSocket 实时语音交互协议：完全兼容 ✅**

Java Server 完整实现了 xiaozhi 固件通信协议的所有核心消息类型，无需修改 ESP32 固件代码即可对接。

---

## 三、ESP32 固件配置方案

### 3.1 当前配置 vs 目标配置

| 配置项 | 当前（Python Server） | 目标（Java Server） |
|--------|---------------------|---------------------|
| OTA URL | `http://192.168.3.32:8003/xiaozhi/ota/` | `http://<server-ip>:8091/api/device/ota` |
| WebSocket URL | `ws://192.168.3.32:8000/xiaozhi/v1/` | `ws://<server-ip>:8092/ws/xiaozhi/v1/` |
| 协议版本 | V1/V2/V3 | V1/V2/V3 兼容 |

### 3.2 固件修改（Kconfig）

文件：`main/Kconfig.projbuild`

```kconfig
CONFIG_OTA_URL="http://<SERVER_IP>:8091/api/device/ota"
CONFIG_LOCAL_WEBSOCKET_URL="ws://<SERVER_IP>:8092/ws/xiaozhi/v1/"
```

或修改代码运行时覆盖：

文件：`main/ota.cc` → `Ota::GetCheckVersionUrl()`

```cpp
// OTA URL 优先级：NVS > 编译时 CONFIG > 默认官方地址
Settings settings("wifi", false);
String url = settings.getString("ota_url");
if (url.isEmpty()) {
    url = CONFIG_OTA_URL;
}
if (url.isEmpty()) {
    url = "http://api.xiaozhi.me/xiaozhi/ota/";
}
return url;
```

### 3.3 部署场景

| 场景 | OTA URL 示例 | WebSocket URL 示例 | 说明 |
|------|-------------|-------------------|------|
| **局域网（推荐开发）** | `http://192.168.x.x:8091/api/device/ota` | `ws://192.168.x.x:8092/ws/xiaozhi/v1/` | ESP32 和 Java Server 同局域网 |
| **公网部署** | `https://your-domain.com/api/device/ota` | `wss://your-domain.com/ws/xiaozhi/v1/` | 需域名 + HTTPS/WSS |
| **内网穿透** | `http://内网穿透域名:8091/...` | `ws://内网穿透域名:8092/...` | frp / ngrok 等 |

---

## 四、Java Server 关键配置

### 4.1 地址自动检测

`ServerAddressProvider.java` 根据 `xiaozhi.server.domain` 配置决定返回地址：

```java
// application.yml 配置
xiaozhi:
  server:
    domain: ""                    # 留空则自动获取本机 IP
    # domain: "xiaozhi.example.com"  # 配置域名则使用 https://xiaozhi.example.com
    port: 8091
  dialogue:
    port: 8092
```

**当前本机检测到的公网 IP**：`ws://192.168.3.32:8092/ws/xiaozhi/v1/`

**局域网地址**（需修改 `application-dev.yml`）：
```yaml
xiaozhi:
  server:
    domain: ""       # 留空，ServerAddressProvider 自动获取本机局域网 IP
```

### 4.2 OTA 激活流程（已实现）

```
ESP32 发送 HTTP POST /api/device/ota
    ↓
Java Server 返回：
{
  "firmware":  { "url": "...", "version": "1.0.0" },
  "websocket": { "url": "ws://<IP>:8092/ws/xiaozhi/v1/", "token": "" },
  "activation": { "code": "...", "message": "...", "challenge": "..." },
  "server_time": { "timestamp": ..., "timezone_offset": 480 }
}
    ↓
ESP32 解析 websocket.url 存入 NVS
    ↓
ESP32 唤醒 → 连接 WebSocket
```

### 4.3 OTA 响应字段说明

| 字段 | 值 | 说明 |
|------|-----|------|
| `firmware.url` | `http://<IP>:8091/api/device/ota` | 固件下载地址（当前固定版本 1.0.0） |
| `firmware.version` | `1.0.0` | 固件版本（可配置） |
| `websocket.url` | `ws://<IP>:8092/ws/xiaozhi/v1/` | WebSocket 地址 |
| `websocket.token` | `""` | Bearer Token（当前为空，依赖设备 MAC 认证） |
| `activation.code` | 6位数字 | 未绑定设备时返回验证码（需在前端输入绑定） |

---

## 五、协议流程详解

### 5.1 完整时序

```
┌──────────┐                    ┌──────────────┐                  ┌───────────────┐
│  ESP32   │                    │  xiaozhi-   │                  │ xiaozhi-      │
│  设备     │                    │  server      │                  │ dialogue       │
└─────┬────┘                    │  (8091)     │                  │ (8092)         │
      │                         └──────┬───────┘                  └───────┬───────┘
      │ HTTP POST /api/device/ota       │                              │
      │ Device-Id: AA:BB:CC:DD:EE:FF   │                              │
      │ POST body: {model, version...}   │                              │
      │ ─────────────────────────────────►                              │
      │                                 │  验证 MAC                      │
      │                                 │  设备已绑定？                  │
      │                                 │                               │
      │  ←─────────────────────────────────                            │
      │  { firmware, websocket, activation }                             │
      │                                                                  │
      │  存储 websocket.url 到 NVS                                     │
      └──────────────────────────────────────────────────────────────────
      │                                                                  │
      │  用户说唤醒词 → 唤醒                                             │
      │  WebSocket CONNECT ws://IP:8092/ws/xiaozhi/v1/                  │
      │  Header: Device-Id: AA:BB:CC:DD:EE:FF                           │
      │  Header: Authorization: Bearer <token>                            │
      │ ──────────────────────────────────────────────────────────────►
      │                                    Session 创建                  │
      │                                    设备验证                     │
      │                                    注册 Session                  │
      │                                                                  │
      │  Text: {type:"hello", version:1, features:{mcp:true,aec:true}}  │
      │  ───────────────────────────────────────────────────────────►
      │                                    handleHelloMessage()          │
      │                                    返回 hello(session_id)         │
      │  ←───────────────────────────────────────────────────────────
      │                                                                  │
      │  Text: {type:"listen", state:"start"}                          │
      │  ───────────────────────────────────────────────────────────►
      │                                    音频开始接收                   │
      │                                    VAD 检测                       │
      │                                                                  │
      │  Binary: [Opus 音频帧 ~60ms/帧]                                  │
      │  ───────────────────────────────────────────────────────────►
      │                                    processAudioData()            │
      │                                       ↓                         │
      │                                    STT (Vosk/FunASR)            │
      │                                    LLM (OpenAI/智谱/...)        │
      │                                    TTS (sherpa-onnx/EdgeTTS)   │
      │                                                                  │
      │  ← Text: {type:"stt", text:"今天天气如何"}                     │
      │  ← Binary: [Opus TTS 音频帧]                                   │
      │  ← Text: {type:"tts", state:"stop"}                          │
      │  ← Text: {type:"llm", emotion:"happy"}                       │
      │                                                                  │
      │  TTS 播放结束                                                   │
      │  WebSocket Close                                                │
      │  ←───────────────────────────────────────────────────────────
      │                                                                  │
      │  回到 Idle 态                                                   │
      └──────────────────────────────────────────────────────────────────
```

### 5.2 设备认证机制

Java Server 通过 `Device-Id` Header 识别设备：
- Header 名称：`device-id`（支持大小写变体：`Device-Id`、`device-id`、`mac_address`）
- 设备表：`sys_device`（按 `deviceId` 主键）
- 认证逻辑：MAC 地址在 `sys_device` 中存在则认证通过，未绑定则返回验证码

---

## 六、已验证功能清单

### 6.1 xiaozhi-server (8091) ✅

| 功能 | 状态 | 代码位置 |
|------|------|---------|
| OTA 激活接口 | ✅ | `DeviceController.java` → `/api/device/ota` |
| OTA 激活检查 | ✅ | `DeviceController.java` → `/api/device/ota/activate` |
| 设备管理 CRUD | ✅ | `DeviceController.java` |
| 角色管理 | ✅ | `RoleController.java` |
| 用户认证 | ✅ | `UserController.java` → `/api/user/login` |
| AI 配置管理 | ✅ | `ConfigController.java` |
| Flyway 迁移 | ✅ V0-V10 | 数据库已就绪 |
| Redis 分布式会话 | ✅ | Sa-Token + Redis |

### 6.2 xiaozhi-dialogue (8092) ✅

| 功能 | 状态 | 代码位置 |
|------|------|---------|
| WebSocket 握手 | ✅ | `WebSocketHandler.java` |
| hello 消息处理 | ✅ | `WebSocketHandler.handleHelloMessage()` |
| listen 消息处理 | ✅ | `MessageHandler` |
| 音频帧接收 (Binary) | ✅ | `handleBinaryMessage()` |
| VAD 语音活动检测 | ✅ | `SileroVadModel` |
| STT 语音识别 | ⚠️ 本地 Vosk 失败 | 需配置云端 ASR（阿里云/腾讯云/讯飞）|
| LLM 对话 | ✅ 可配置 | OpenAI / 智谱 / 讯飞星火 / Ollama / Dify / Coze |
| TTS 语音合成 | ✅ | sherpa-onnx / Edge TTS / 阿里云 |
| MCP 工具协议 | ✅ | `DeviceMcpService` |
| 表情/角色管理 | ✅ | `PersonaFactory` |
| IoT 设备控制 | ✅ | `IotService` |
| AEC 回声消除 | ✅ | `AecService` |
| Redis 全局工具注册 | ✅ | `GlobalToolRedisRegistry` |

---

## 七、待完善问题

### 7.1 已知问题

| 问题 | 影响 | 解决方案 |
|------|------|---------|
| **Vosk STT 初始化失败** (`vosk_recognizer_set_grm` symbol not found) | 本地 ASR 不可用 | 改用云端 ASR（阿里云 FunASR / 腾讯云） |
| **OTA firmware.version 固定 1.0.0** | 无法真正下载固件 | 需实现固件上传 + 下载接口 |
| **WebSocket token 为空** | 依赖 MAC 地址认证 | 如需更安全，改为 JWT Token 认证 |
| **ServerAddressProvider IP 识别为公网 IP** | 局域网 ESP32 无法连接 | 配置 `xiaozhi.server.domain` 或内网 IP |

### 7.2 建议补充

1. **固件 OTA 升级**：实现 `firmware.url` 对应的固件下载接口
2. **Token 认证**：在 WebSocket 握手时验证设备合法性
3. **云端 STT 配置**：在 `sys_config` 中配置阿里云 ASR / 腾讯云 ASR
4. **HTTPS/WSS 支持**：公网部署需配置反向代理（Nginx/Caddy）

---

## 八、ESP32 固件修改步骤

### 8.1 修改 OTA 和 WebSocket 地址

编辑 `main/Kconfig.projbuild`：

```kconfig
# 修改默认 OTA URL（指向 Java Server）
CONFIG_OTA_URL="http://192.168.x.x:8091/api/device/ota"

# 修改默认 WebSocket URL（指向 Java Dialogue）
CONFIG_LOCAL_WEBSOCKET_URL="ws://192.168.x.x:8092/ws/xiaozhi/v1/"
```

### 8.2 重新编译烧录

```bash
# 1. 配置目标芯片
idf.py set-target esp32s3

# 2. 修改 Kconfig 后重新配置
idf.py reconfigure

# 3. 编译
idf.py build

# 4. 烧录（使用项目脚本）
./build_and_flash.sh
```

### 8.3 验证对接

1. ESP32 上电 → 串口日志显示 OTA 激活成功，返回 WebSocket 地址
2. 说唤醒词 → ESP32 连接 `ws://<IP>:8092/ws/xiaozhi/v1/`
3. Java dialogue 日志显示 `WebSocket连接建立成功 - SessionId: xxx, DeviceId: xx:xx:xx:xx:xx:xx`
4. 说一句话 → 对话正常响应

---

## 九、快速开始

### 9.1 启动 Java Server

```bash
cd /Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32-server-java
./start.sh all     # 启动全部服务（DB + 后端 + 前端）
```

### 9.2 修改 ESP32 配置

```bash
cd /Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32
# 编辑 main/Kconfig.projbuild，修改：
CONFIG_OTA_URL="http://192.168.x.x:8091/api/device/ota"
CONFIG_LOCAL_WEBSOCKET_URL="ws://192.168.x.x:8092/ws/xiaozhi/v1/"

# 重新配置并烧录
idf.py reconfigure && idf.py build && ./build_and_flash.sh
```

### 9.3 访问管理平台

- 地址：http://localhost:8084
- 用户名：`admin`
- 密码：`123456`

---

## 十、相关文档

| 文档 | 说明 |
|------|------|
| [小智AI与后台服务器交互协议汇总](小智AI与后台服务器交互协议汇总.md) | 完整 WebSocket 协议说明 |
| [SERVER_INFO.md](../xiaozhi-esp32-server-java/SERVER_INFO.md) | Java Server 服务信息与默认凭据 |
| [start.sh](../xiaozhi-esp32-server-java/start.sh) | 一键启动脚本 |
| [docs/websocket_zh.md](websocket_zh.md) | WebSocket 协议详细说明 |

---

*本文档由 AI 助手自动整理生成*
