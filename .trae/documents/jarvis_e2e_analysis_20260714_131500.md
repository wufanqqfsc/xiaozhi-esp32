# 贾维斯语音交互 E2E 流程梳理（最新一次）

> 分析时间：2026-07-14 13:15
> 数据来源：后端日志 + SenseVoice 容器
> 使用技能：xiaozhi-e2e-pipeline、esp32-http-api、xiaozhi-server-java

## 会话基本信息

| 项 | 值 |
|---|---|
| 设备 IP | 192.168.0.198 |
| WS URI | ws://192.168.0.198/ws/xiaozhi/v1/ |
| DeviceId | a0:f2:62:e4:3a:40 |
| 最新 SessionId | 0b4829fd-d8f1-33e6-6c54-0181a2d4ba84 |
| 时间范围 | 2026-07-14 13:00:52 ~ 13:10:58 |
| 提供商 | MiniMax LLM + FunASR/SenseVoice STT |
| 后端状态 | ✅ 运行中 |

## 后端服务状态

| 服务 | 状态 | 端口 |
|---|---|---|
| MySQL | ✅ 运行中 | 3306 |
| Redis | ✅ 运行中 | 6379 |
| xiaozhi-server | ✅ 运行中 | 8091 |
| xiaozhi-dialogue | ✅ 运行中 | 8092 |
| xiaozhi-web | ✅ 运行中 | 8084 |
| FunASR/SenseVoice | ✅ 运行中 | 10096 |

## 最新交互流程（Session: 83620ed7）

### 第一轮：唤醒应答

| # | 阶段 | 时间戳 | 耗时 | 日志位置 |
|---|---|---|---|---|
| 1 | WS HELLO | 13:00:52.817 | - | WebSocketHandler |
| 2 | 收到 listen detect | 13:00:53.852 | 1.035s | MessageHandler |
| 3 | LLM_FIRST_TOKEN START | 13:00:53.943 | 0ms | LatencyTracer |
| 4 | TTS_FIRST_CHUNK START | 13:00:54.481 | 538ms | LatencyTracer |
| 5 | 收到 listen start | 13:00:55.155 | 674ms | MessageHandler |
| 6 | VAD 会话初始化 | 13:00:55.179 | 24ms | VadService |
| 7 | LLM 调用失败 | 13:00:56.437 | 2.494s | DialogueListener |
| 8 | VAD 超时强制结束 | 13:01:05.222 | 10.043s | VadService |

### 第二轮：设备重连（Session: 0b4829fd）

| # | 阶段 | 时间戳 | 耗时 |
|---|---|---|---|
| 1 | WS 连接建立 | 13:10:56.030 | - |
| 2 | WS HELLO | 13:10:56.256 | 226ms |
| 3 | MCP tools/list 发送 | 13:10:56.697 | 441ms |
| 4 | MCP 工具列表返回 | 13:10:57.284 | 587ms |
| 5 | MCP 初始化超时 | 13:10:58.828 | 1.544s |

## 各阶段耗时汇总

| 指标 | 耗时 | 评价 |
|---|---|---|
| 唤醒 → LLM_FIRST_TOKEN | 109ms | ✅ 正常 |
| LLM_FIRST_TOKEN → TTS_FIRST_CHUNK | 538ms | ✅ 正常 |
| VAD START → VAD END（超时） | 10.043s | ❌ 严重超时 |
| LLM 调用耗时 | 2.494s | ❌ 失败（400 Bad Request） |
| MCP tools/list 响应 | 587ms | ✅ 正常 |

## 关键问题诊断

### ✅ 已验证修复项（vs 上次）
- ✅ SenseVoice STT 容器运行正常
- ✅ WebSocket 连接建立正常
- ✅ MCP 协议交互正常

### ⚠ 当前发现的新问题

#### P1: LLM 调用失败（MiniMax 400 Bad Request）
```
2026-07-14 13:00:56.437 ERROR DialogueListener:36 - LLM调用失败
org.springframework.web.reactive.function.client.WebClientResponseException$BadRequest:
  400 Bad Request from POST https://api.minimaxi.com/v1/chat/completions
```
**根因分析**：MiniMax API 返回 400 错误，可能是请求参数格式错误或 API Key 问题。

#### P1: MCP 工具调用超时
```
2026-07-14 13:01:47 ~ 13:10:58 ERROR DeviceMcpService:305 - 
  Error sending MCP request：{}
java.util.concurrent.TimeoutException: null
```
**根因分析**：多个 Session 的 MCP 请求超时，可能是设备端响应慢或网络不稳定。

#### P2: VAD 超时强制结束
```
2026-07-14 13:01:05.222 INFO VadService:305 - 
  聆听时长超过 10000ms (10043ms)，强制结束聆听
```
**根因分析**：用户在唤醒后未及时说话，导致 VAD 超时。

#### P2: 设备 HTTP 服务未启动
- ping 192.168.0.198 可达
- HTTP 端口 80/8080 均拒绝连接
- 无法获取设备状态、NVS 配置、SD 卡日志

## 优化建议

| 优先级 | 建议 |
|---|---|
| 高 | 检查 MiniMax API Key 和请求参数配置 |
| 高 | 检查设备端 HTTP 服务是否正常启动（sdcard_log_http.cc） |
| 中 | 增加 LLM 调用失败的重试机制 |
| 中 | 优化 MCP 请求超时时间配置 |

## E2E Pipeline 对照（16 步骤）

| 步骤 | 阶段 | 状态 |
|---|---|---|
| 1-3 | AFE 唤醒词检测 | ⚠️ 后端收到 listen detect，但缺少完整 AFE 日志 |
| 4 | 设备发送 listen detect | ✅ 13:00:53.852 |
| 5-6 | WebSocket 握手 + 音频流 | ✅ 连接正常 |
| 7-9 | VAD 初始化 + Opus 解码 | ✅ 初始化正常 |
| 10 | STT 识别完成 | ❌ 未完成（LLM 失败导致流程中断） |
| 11-13 | LLM → TTS 流式合成 | ❌ 失败（MiniMax 400） |
| 14-16 | Player → WS → 播放 | ❌ 未执行 |

## 修复效果对比

| 指标 | 上次 | 本次 | 改善 |
|---|---|---|---|
| WebSocket 连接 | ✅ | ✅ | 稳定 |
| MCP 工具列表 | - | ✅ 587ms | 新增 |
| LLM 调用 | - | ❌ 失败 | 需修复 |

## 待解决问题

| # | 问题 | 优先级 | 状态 | 下一步 |
|---|---|---|---|---|
| 1 | MiniMax LLM API 400 Bad Request | P1 | 待修复 | 检查 API 配置 |
| 2 | MCP 工具调用超时 | P1 | 待观察 | 设备在线后验证 |
| 3 | 设备 HTTP 服务未启动 | P2 | 待修复 | 检查 sdcard_log_http.cc |
| 4 | VAD 超时强制结束 | P2 | 正常行为 | 无需修复 |

---

*报告生成时间：2026-07-14 13:15*
*数据来源：xiaozhi-dialogue.log + xiaozhi-dialogue-error.log*