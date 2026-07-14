---
name: "xiaozhi-e2e-debug"
description: "小智 ESP32 E2E 语音交互全链路调试手册。Invoke when device has no response, STT fails, TTS stucks, or VAD/LLM/TTS pipeline issues occur."
---

# 小智 ESP32 端到端语音交互调试手册

## 架构概览

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              ESP32 设备端                                     │
│  ┌─────────┐    ┌──────────┐    ┌─────────────┐    ┌──────────────────┐  │
│  │ 麦克风   │───►│ AFE 前端   │───►│ Opus 编码   │───►│ WebSocket 发送   │  │
│  └─────────┘    └──────────┘    └─────────────┘    └────────┬─────────┘  │
│                                                                             │
│  ┌─────────┐    ┌──────────┐    ┌─────────────┐    ┌──────────────────┐  │
│  │ Opus 解码│◄───│ TTS 音频  │◄───│ WebSocket  │◄───│ 接收服务端消息    │  │
│  └────┬────┘    └──────────┘    └─────────────┘    └──────────────────┘  │
│       │                                                                    │
│       ▼                                                                    │
│  ┌─────────┐    ┌──────────┐    ┌─────────────┐                           │
│  │ DAC/I2S │◄───│ 音频播放  │◄───│ 状态机控制  │                           │
│  └─────────┘    └──────────┘    └─────────────┘                           │
└─────────────────────────────────────────────────────────────────────────────┘
                                    │
                          WebSocket (ws://server:8092)
                                    │
┌─────────────────────────────────────────────────────────────────────────────┐
│                            Java 后端 (xiaozhi-dialogue)                    │
│                                                                             │
│  ┌────────────┐    ┌────────────┐    ┌────────────┐    ┌────────────┐    │
│  │ WebSocket  │───►│  VAD 检测   │───►│  STT 识别  │───►│  LLM 对话  │    │
│  │ Handler    │    │ (Silero)   │    │ (FunASR)  │    │            │    │
│  └────────────┘    └────────────┘    └────────────┘    └─────┬──────┘    │
│                                                               │            │
│  ┌────────────┐    ┌────────────┐    ┌────────────┐         │            │
│  │ TTS 合成   │◄───│ 句子切分   │◄───│ 流式输出   │◄────────┘            │
│  │ (Edge/Aliyun) │    │            │    │            │                     │
│  └──────┬─────┘    └────────────┘    └────────────┘                     │
│         │                                                                  │
│         ▼                                                                  │
│  ┌────────────┐    ┌────────────┐                                        │
│  │Opus 编码   │───►│ WS 发送设备│                                        │
│  └────────────┘    └────────────┘                                        │
└─────────────────────────────────────────────────────────────────────────────┘
```

## 阶段划分与日志关键词

### 阶段 1: 唤醒词检测

| 组件 | 设备端日志关键词 | 服务端日志关键词 |
|------|-----------------|-----------------|
| **唤醒词检测** | `Wake word detected` | — |
| **唤醒响应** | `Wake word detected: Jarvis` | — |
| **状态变化** | `State: idle -> connecting` | — |
| **WebSocket 连接** | `WebSocket connecting` | — |
| **连接成功** | `WebSocket connected` | `会话已注册` |

**设备端关键代码**：
- 唤醒词检测：`main/application.cc:1254` (`HandleWakeWordDetectedEvent`)
- 发送唤醒词：`protocol_->SendWakeWordDetected()`
- 状态转换：`SetDeviceState(kDeviceStateConnecting)`

**正常日志示例**：
```
[E][app] Wake word detected: Jarvis (state: 1)
[I][app] State: idle -> connecting
[D][app] WebSocket connecting to wss://192.168.x.x:8092/xiaozhi/ws
[I][app] WebSocket connected
```

---

### 阶段 2: WebSocket 握手

| 组件 | 设备端日志关键词 | 服务端日志关键词 |
|------|-----------------|-----------------|
| **发送 hello** | `Sending hello` | — |
| **收到 hello** | — | `收到hello消息` |
| **状态变化** | `State: connecting -> speaking` | `状态转换: IDLE -> SPEAKING` |
| **LATENCY** | — | `[LATENCY] LLM_FIRST_TOKEN START` |

**设备端关键代码**：
- 发送 hello：`websocket_protocol.cc:83` (`OpenAudioChannel`)
- 接收 hello 响应：`application.cc:883` (`OnIncomingJson`)

**正常日志示例（设备端）**：
```
[I][app] Sending hello message to server
[D][ws] WS TX: {"type":"hello","features":{"mcp":true}}
```

**正常日志示例（服务端）**：
```
INFO  c.x.c.s.w.WebSocketHandler:55 - WebSocket连接建立成功
INFO  c.x.c.s.w.WebSocketHandler:154 - 收到hello消息
INFO  c.x.c.common.MessageHandler:357 - 收到listen消息 - State: Detect, Mode: null
DEBUG c.x.c.common.ChatSession:73 - 状态转换: IDLE -> SPEAKING
INFO  LatencyTracer:75 - [LATENCY][sessionId] LLM_FIRST_TOKEN START total=0ms
```

---

### 阶段 3: 聆听模式 (Listening)

| 组件 | 设备端日志关键词 | 服务端日志关键词 |
|------|-----------------|-----------------|
| **状态变化** | `State: speaking -> listening` | `状态转换: SPEAKING -> LISTENING` |
| **VAD 初始化** | — | `VAD会话已初始化` |
| **LISTEN-DBG** | `LISTEN-DBG clock=X started=Y diff=Z` | — |
| **发送音频** | `WS TX: binary (Opus)` | — |
| **接收音频** | — | `WS IN  BIN  size=X` |

**设备端关键代码**：
- 状态变化：`application.cc:910` (TTS stop → listening)
- 发送 listen start：`protocol_->SendStartListening()`
- 开启麦克风：`audio_service_.EnableVoiceProcessing(true)`
- Listening 超时：`application.cc:445` (20秒超时)

**Listening 超时保护机制**：
```cpp
// application.cc:436
constexpr int LISTENING_TIMEOUT_SEC = 20;

// application.cc:445-451
if (clock_ticks_ >= LISTENING_TIMEOUT_SEC) {
    ESP_LOGW(TAG, "Listening timeout (no TTS response in %ds), back to idle");
    SetDeviceState(kDeviceStateIdle);
    audio_service_.EnableWakeWordDetection(true);
}
```

**正常日志示例（设备端）**：
```
[I][app] State: speaking -> listening
[D][app] SendStartListening mode=Auto
[D][app] WS TX: {"type":"listen","state":"start","mode":"auto"}
[I][app] [LISTEN-DBG] clock=0 started=0 diff=0 timeout=20
[I][app] [LISTEN-DBG] clock=5 started=0 diff=5 timeout=20
...
```

**正常日志示例（服务端）**：
```
INFO  c.x.c.common.MessageHandler:357 - 收到listen消息 - State: Start, Mode: Auto
INFO  c.x.dialogue.audio.VadService:208 - VAD会话已初始化
DEBUG c.x.dialogue.audio.VadService:316 - 检测到语音开始 - 概率: 0.9991
DEBUG c.x.dialogue.audio.VadService:343 - 语音结束: 静音: 1248ms
```

---

### 阶段 4: STT 语音识别

| 组件 | 服务端日志关键词 |
|------|-----------------|
| **开始接收** | `STT_RECV START` |
| **FunASR 连接** | `FunASR WebSocket连接已打开` |
| **FunASR 在线结果** | `FunASR 在线结果: XXX` |
| **FunASR 离线结果** | `FunASR 离线修正结果: XXX` |
| **STT 完成** | `STT_DONE delta=Xms` |
| **STT 超时** | `FunASR识别超时` |
| **STT 结果为空** | `FunASR识别结果为空` |

**关键代码**：
- STT 开始：`FunASRSttService.java:51` (`LatencyTracer.start("STT_RECV")`)
- FunASR 响应处理：`FunASRSttService.java:106-125` (`onMessage`)
- STT 完成：`FunASRSttService.java:129` (`LatencyTracer.mark("STT_DONE")`)

**FunASR 2pass 模式说明**：
- `2pass-online`：实时在线识别，边识别边返回
- `2pass-offline`：流结束后，离线修正返回最准确结果

**正常日志示例**：
```
INFO  c.x.common.utils.LatencyTracer:75 - [LATENCY][sessionId] STT_RECV START total=0ms
DEBUG c.x.a.s.p.FunASRSttService:70 - FunASR WebSocket连接已打开
DEBUG c.x.a.s.p.FunASRSttService:111 - FunASR 离线修正结果: 你好
INFO  c.x.common.utils.LatencyTracer:101 - [LATENCY][sessionId] STT_DONE delta=3000ms total=3000ms
```

**异常日志示例**：
```
WARN  c.x.a.s.p.FunASRSttService:149 - FunASR识别超时，等待音频发送线程结束...
WARN  c.x.a.s.p.FunASRSttService:167 - FunASR识别结果为空
```

---

### 阶段 5: LLM 对话生成

| 组件 | 服务端日志关键词 |
|------|-----------------|
| **LLM 首 token** | `LLM_FIRST_TOKEN delta=Xms` |
| **LLM 完成** | `LLM_DONE delta=Xms` |
| **对话历史** | `加载对话历史: size=X` |

**关键代码**：
- LLM 开始：`Persona.java:123` (`LatencyTracer.start("LLM_FIRST_TOKEN")`)
- 首 token：`Persona.java:165` (`LatencyTracer.mark("LLM_FIRST_TOKEN")`)
- 完成：`Persona.java:160` (`LatencyTracer.mark("LLM_DONE")`)

**正常日志示例**：
```
INFO  c.x.common.utils.LatencyTracer:75 - [LATENCY][sessionId] LLM_FIRST_TOKEN START total=0ms
INFO  c.x.common.utils.LatencyTracer:101 - [LATENCY][sessionId] LLM_FIRST_TOKEN delta=1500ms total=1500ms
INFO  c.x.common.utils.LatencyTracer:95 - [LATENCY][sessionId] LLM_DONE ORPHAN (no start)
```

---

### 阶段 6: TTS 语音合成

| 组件 | 设备端日志关键词 | 服务端日志关键词 |
|------|-----------------|-----------------|
| **TTS 开始** | `tts start` | `sendTtsMessage发送消息 - state=start` |
| **TTS 句子开始** | `tts sentence_start` | `sendTtsMessage发送消息 - state=sentence_start` |
| **TTS 停止** | `tts stop` | `sendTtsMessage发送消息 - state=stop` |
| **LATENCY** | — | `TTS_FIRST_CHUNK delta=Xms` |
| **状态变化** | `State: listening -> speaking` | `状态转换: LISTENING -> SPEAKING` |

**关键代码**：
- TTS 开始：`ScheduledPlayer.java` (发送 `{"type":"tts","state":"start"}`)
- TTS 句子：`FileSynthesizer.java:65` (`LatencyTracer.start("TTS_FIRST_CHUNK")`)
- TTS 完成：`FileSynthesizer.java:89` (`LatencyTracer.mark("TTS_FIRST_CHUNK")`)

**正常日志示例（服务端）**：
```
INFO  c.x.c.message.MessageSender:41 - sendTtsMessage发送消息 - state=start
DEBUG c.x.c.common.ChatSession:73 - 状态转换: LISTENING -> SPEAKING
INFO  c.x.common.utils.LatencyTracer:101 - [LATENCY][sessionId] TTS_FIRST_CHUNK delta=3000ms total=3000ms
INFO  c.x.c.message.MessageSender:41 - sendTtsMessage发送消息 - state=sentence_start
INFO  c.x.c.message.MessageSender:41 - sendTtsMessage发送消息 - state=stop
```

**正常日志示例（设备端）**：
```
[D][app] OnIncomingJson: {"type":"tts","state":"start"}
[I][app] State: listening -> speaking
[D][app] OnIncomingJson: {"type":"tts","state":"sentence_start","text":"你好"}
[D][app] OnIncomingJson: {"type":"tts","state":"stop"}
[I][app] State: speaking -> listening
```

---

### 阶段 7: 二次唤醒（多轮对话）

| 组件 | 设备端日志关键词 | 服务端日志关键词 |
|------|-----------------|-----------------|
| **TTS 停止** | `State: speaking -> listening` | `状态转换: SPEAKING -> LISTENING` |
| **LISTEN-DBG** | `clock=X started=Y diff=Z` | — |
| **用户说话** | — | `检测到语音开始 - 概率: 0.9991` |
| **语音结束** | — | `语音结束: 静音: 1248ms` |
| **状态转换** | — | `状态转换: LISTENING -> THINKING` |

**关键机制**：
- TTS 停止后，设备进入 listening 状态，`clock_ticks_` 重置为 0
- 20秒内若收到 TTS 响应，设备转为 speaking
- 若 20秒内无响应，设备超时回 idle

---

## 快速诊断命令

### 1. 检查后端服务状态

```bash
cd xiaozhi-esp32-server-java
./start.sh status
```

### 2. 查看设备状态

```bash
curl http://<DEVICE_IP>:8080/api/device/status
```

### 3. 查看设备日志

```bash
curl http://<DEVICE_IP>:8080/api/device/logs | tail -100
```

### 4. 查看 SD 卡日志文件

```bash
curl http://<DEVICE_IP>:8080/api/sdcard/logs
curl "http://<DEVICE_IP>:8080/api/sdcard/logs/xiaozhi_YYYY-MM-DD_HH-MM-SS_XXXX.log" | head -100
```

### 5. 实时追踪服务端对话日志

```bash
cd xiaozhi-esp32-server-java
tail -f logs/xiaozhi-dialogue.log | grep -E "sessionId|LATENCY|检测到|VAD|STT|LLM|TTS|FunASR"
```

### 6. 追踪 LATENCY 日志（延迟分析）

```bash
grep "\[LATENCY\]" logs/xiaozhi-dialogue.log
```

**LATENCY 日志格式**：
```
[LATENCY][sessionId] STAGE START delta=0ms total=0ms
[LATENCY][sessionId] STAGE delta=Xms total=Yms
[LATENCY][sessionId] STAGE ORPHAN (no start)
```

### 7. 检查 FunASR 容器状态

```bash
docker ps | grep funasr
docker logs xiaozhi-funasr --tail 20
```

---

## 常见问题诊断

### 问题 1: 唤醒词检测后无响应

**可能原因**：
1. WebSocket 连接失败
2. 服务端未收到 hello
3. LLM 配置错误

**诊断命令**：
```bash
# 设备端
grep -E "Wake word|WebSocket|connecting|hello" logs/xiaozhi_*.log

# 服务端
grep -E "hello|会话已注册|状态转换" logs/xiaozhi-dialogue.log
```

### 问题 2: TTS 开始后设备无声音

**可能原因**：
1. Opus 解码失败
2. I2S/DAC 配置错误
3. TTS 音频帧未发送

**诊断命令**：
```bash
# 服务端
grep -E "TTS_FIRST_CHUNK|sendTtsMessage|FIRST_AUDIO_SENT" logs/xiaozhi-dialogue.log

# 设备端
grep -E "tts.*start|Opus|decode|audio" logs/xiaozhi_*.log
```

### 问题 3: STT 识别结果为空

**可能原因**：
1. FunASR 服务未运行
2. 音频格式不匹配
3. VAD 未检测到语音
4. FunASR 超时

**诊断命令**：
```bash
# 服务端
grep -E "FunASR|离线修正|STT_DONE|识别超时" logs/xiaozhi-dialogue.log

# FunASR 容器
docker logs xiaozhi-funasr --tail 20
```

### 问题 4: Listening 超时（设备提前回 idle）

**可能原因**：
1. VAD 检测延迟
2. STT 处理慢
3. LLM 响应慢

**诊断命令**：
```bash
# 设备端
grep -E "LISTEN-DBG|Listening timeout" logs/xiaozhi_*.log

# 服务端
grep -E "检测到语音开始|状态转换.*THINKING|LLM_FIRST_TOKEN" logs/xiaozhi-dialogue.log
```

---

## LatencyTracer 阶段说明

| 阶段 | 含义 | 正常延迟 |
|------|------|---------|
| `STT_RECV` | 开始接收音频数据 | — |
| `STT_DONE` | STT 识别完成 | 1-3s（FunASR 2pass） |
| `LLM_FIRST_TOKEN` | LLM 输出首个字符 | 1-3s（网络 LLM） |
| `LLM_DONE` | LLM 生成完成 | 3-10s |
| `TTS_FIRST_CHUNK` | TTS 首个音频块发送 | 0.5-2s（Edge TTS） |
| `FIRST_AUDIO_SENT` | 首个 Opus 帧发送到设备 | — |

---

## 文件路径索引

### ESP32 设备端

| 功能 | 文件 | 关键行号 |
|------|------|---------|
| 主循环/事件处理 | `main/application.cc` | 339-368, 403-458 |
| 唤醒词检测 | `main/application.cc` | 1254-1370 |
| 状态变化处理 | `main/application.cc` | 1373-1480 |
| Listening 超时 | `main/application.cc` | 436-451 |
| WebSocket 协议 | `main/protocols/websocket_protocol.cc` | 83-200 |
| TTS/JSON 接收 | `main/application.cc` | 883-910 |

### Java 后端

| 功能 | 文件 | 关键行号 |
|------|------|---------|
| WebSocket 入口 | `xiaozhi-dialogue/.../websocket/WebSocketHandler.java` | 55-154 |
| 消息路由 | `xiaozhi-dialogue/.../common/MessageHandler.java` | 357-430 |
| VAD 服务 | `xiaozhi-dialogue/.../audio/VadService.java` | 208, 316, 343 |
| STT 服务 | `xiaozhi-ai/.../stt/providers/FunASRSttService.java` | 51-170 |
| Persona 聚合 | `xiaozhi-dialogue/.../runtime/Persona.java` | 123, 160, 165 |
| TTS 合成器 | `xiaozhi-dialogue/.../playback/FileSynthesizer.java` | 65, 89 |
| 延迟追踪 | `xiaozhi-common/.../utils/LatencyTracer.java` | 75, 95, 101 |

---

## JARVIS HUD / LVGL 设备问题

本技能覆盖**语音 E2E 链路**（STT/LLM/TTS/WebSocket）。若问题是 JARVIS 界面冻结、联网后重启、`Guru Meditation`、LVGL 锁超时等**显示层/固件崩溃**，请改用：

- `.trae/skills/jarvis-device-debug/SKILL.md` — 编译烧录、串口 monitor、backtrace 解码、`FortuneWatchfaceView` 修复 playbook
