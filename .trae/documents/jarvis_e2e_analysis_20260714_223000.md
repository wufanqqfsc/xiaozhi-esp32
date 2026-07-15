# 贾维斯语音交互 E2E 流程梳理（最新一次）

> 分析时间：2026-07-14 22:30
> 数据来源：后端日志 + 设备日志 + SenseVoice 容器
> 使用技能：xiaozhi-e2e-pipeline、esp32-http-api、xiaozhi-server-java
> 设备 IP：192.168.3.22

## 会话基本信息

| 项 | 值 |
|------|------|
| 设备 IP | 192.168.3.22 |
| SessionId | c061c8c7-dd9a-c32d-dbb0-a17d8ebce5fa |
| 时间范围 | 2026-07-14 22:00:13 ~ 22:00:37 |
| STT 提供商 | SenseVoice (xiaozhi-sensevoice, port 10096) |
| TTS 提供商 | Edge TTS（推断） |
| 设备版本 | 2.2.6 |
| 当前音量 | 20%（已调整） |

## 第一轮：摇晃占卜触发

### 设备端时间线

| 时间（+s） | 事件 | 说明 |
|-----------|------|------|
| +9.983 | State: starting -> activating | 设备启动 |
| +11.372 | WebSocket connected | 连接后端成功 |
| +11.374 | State: activating -> idle | 进入待机状态 |
| +11.855 | OnShakeDetected: triggering divination | 检测到摇晃，触发占卜 |
| +11.871 | Fortune divination started, result index 11 | 占卜开始，结果索引11 |
| +122.165 | Received TTS start | 收到TTS开始（延迟约110s，异常） |
| +124.235 | TTS sentence_start: "先生，罗盘已收到您的摇晃指令，" | TTS语音播报 |
| +127.199 | State: idle -> speaking | 唤醒词检测到 |
| +127.385 | State: speaking -> listening | 进入聆听状态 |
| +128.159 | SendStartListening mode=auto | 发送listen start |
| +132 ~ +152 | LISTEN-DBG clock=5~25, timeout=30 | 聆听中，等待用户说话 |

### 后端时间线（摇晃占卜）

| 时间 | 事件 | 说明 |
|------|------|------|
| 22:00:13 | TTS 开始合成 | （推断，基于 TTS_FIRST_CHUNK total=13171ms） |
| 22:00:19 | TTS_FIRST_CHUNK delta=2056ms total=13171ms | 首个TTS音频块 |
| 22:00:20 | TTS_FIRST_CHUNK delta=1143ms total=14315ms | 第二个句子 |
| 22:00:20 | sentence_start: "随时听候差遣。若有需要，" | 唤醒应答第一句 |
| 22:00:25 | sentence_start: "随时吩咐。" | 唤醒应答第二句 |
| 22:00:27 | TTS stop, SPEAKING -> LISTENING | TTS结束，进入聆听 |
| 22:00:27 | 收到 listen start, VAD 会话已初始化 | VAD 开始工作 |
| 22:00:37 | VAD 聆听超时 10000ms，强制结束 | VAD 超时 |
| 22:00:37 | LISTENING -> THINKING | 进入思考状态 |

## 第二轮：唤醒应答 + 聆听超时

### 16 步 Pipeline 对照表

| # | 阶段 | 设备端 | 后端 | 状态 |
|---|------|--------|------|------|
| 1 | AFE 唤醒词检测 | ✅ +127.199 Wake word detected | — | 正常 |
| 2 | 设备发送 listen detect | ✅ +128.159 SendStartListening | — | 正常 |
| 3 | WebSocket 握手 + 音频流 | ✅ WS connected | ✅ 收到 hello | 正常 |
| 4 | VAD 初始化 | — | ✅ 22:00:27 VAD会话已初始化 | 正常 |
| 5 | 音频流接收 | ✅ Opus 发送 | ✅ WS IN BIN 持续接收 | 正常 |
| 6 | VAD 语音检测 | — | ❌ 未检测到语音 | 异常 |
| 7 | VAD 语音结束 | — | ❌ 未触发（超时强制结束） | 异常 |
| 8 | STT 识别 | — | ❌ 无 STT 结果（VAD 超时） | 异常 |
| 9 | LLM 对话生成 | — | ❌ 无 LLM 输出 | 异常 |
| 10 | TTS 流式合成 | — | ❌ 无 TTS（无输入） | 异常 |
| 11 | Player → WS 发送音频 | — | ❌ 无音频发送 | 异常 |
| 12 | 设备 OnIncomingJson 分发 | — | — | 未触发 |
| 13 | 设备 OnIncomingAudio 播放 | — | — | 未触发 |

### 说明

第二轮对话因**VAD 超时**（10秒未检测到语音）而终止，未进入 STT/LLM/TTS 阶段。这可能是因为：
1. 用户在唤醒后没有说话
2. 音频质量问题导致 VAD 无法检测到语音
3. 设备端音频重采样问题影响了 VAD 检测

## 各阶段耗时汇总

| 指标 | 耗时 | 评价 |
|------|------|------|
| **摇晃占卜 → TTS 首块** | ~110s | ⚠️ 异常延迟（正常应 < 5s） |
| TTS_FIRST_CHUNK（唤醒应答） | 2056ms | ✅ 正常（< 3000ms） |
| TTS 总时长（唤醒应答） | ~8s | ✅ 正常 |
| VAD 聆听时长 | 10009ms（超时） | ⚠️ 超时结束 |
| 设备端 LISTEN 超时 | 30s（配置值） | — |

## 关键问题诊断

### ⚠️ 当前发现的问题

#### P1: 摇晃占卜 TTS 延迟异常（~110s）

**现象**：设备在 +11.855s 检测到摇晃并触发占卜，但 TTS 语音响应直到 +122.165s 才到达，延迟约 110 秒。

**可能原因**：
1. MCP 工具调用（`get_divination_result` / `search_and_display_gif`）耗时过长
2. LLM 响应慢或调用 MCP 工具时阻塞
3. 后端对话处理队列阻塞

**日志证据**：
- 设备端：+11.855s 触发占卜，+122.165s 才收到 TTS start
- 后端日志中未找到该轮对话的 LATENCY 记录（可能在更早的日志中）

**建议排查**：
- 检查完整后端日志中该时间段的 LLM/MCP 调用耗时
- 确认 `get_divination_result` 和 `search_and_display_gif` 工具执行时间

#### P2: VAD 聆听超时（未检测到语音）

**现象**：唤醒后用户未说话（或 VAD 未检测到语音），10 秒后 VAD 强制结束聆听。

**后端日志**：
```
VadService:305 - 聆听时长超过 10000ms (10009ms)，强制结束聆听
```

**可能原因**：
1. 用户确实没有说话（正常场景）
2. 设备端音频重采样问题导致音频质量下降
3. VAD 阈值设置过高

**设备端观察**：
- 大量 `AudioService: Input resample output_samples (288) < expected_max (1184), correcting` 警告
- 这些警告持续出现，可能影响音频质量和 VAD 检测

#### P2: 设备状态 API Connection Reset

**现象**：`GET /api/device/status` 返回 `Connection reset by peer`，但其他 API（SD卡日志、音量设置）正常。

**可能原因**：
1. 状态 API 处理函数中存在内存访问问题或死锁
2. HTTP 服务器资源不足
3. 设备端状态收集时触发了异常

**验证**：
- `POST /api/device/volume` ✅ 正常
- `GET /api/sdcard/logs` ✅ 正常
- `GET /api/device/status` ❌ Connection reset

#### P2: 音频重采样警告过多

**现象**：设备日志中持续出现音频重采样警告，频率约 10 次/秒。

```
AudioService: Input resample output_samples (288) < expected_max (1184), correcting
AudioService: Resample target_size (608) < expected_max (1504), correcting
```

**影响**：
- 可能影响 VAD 语音检测准确率
- 可能增加 CPU 负载
- 日志量过大可能影响性能

### ✅ 已验证正常项

| 项 | 状态 | 说明 |
|------|------|------|
| 设备音量控制 | ✅ 正常 | GET/POST /api/device/volume 均正常 |
| 后端服务 | ✅ 正常 | xiaozhi-server/dialogue/web 均运行 |
| Docker 容器 | ✅ 正常 | SenseVoice/Redis/MySQL 均运行 |
| TTS 合成速度 | ✅ 正常 | TTS_FIRST_CHUNK ~2s，在正常范围 |
| WebSocket 连接 | ✅ 正常 | 连接稳定，音频流持续传输 |

## 优化建议

| 优先级 | 建议 | 说明 |
|--------|------|------|
| **高** | 排查摇晃占卜 TTS 延迟 110s 问题 | 检查 MCP 工具调用耗时、LLM 响应时间 |
| **中** | 调查音频重采样警告原因 | 检查 AudioService 重采样配置，优化音频质量 |
| **中** | 修复 /api/device/status Connection reset | 检查状态 API 处理函数，定位 crash 原因 |
| **低** | 调整 VAD 超时阈值 | 设备端 30s vs 后端 10s 不一致，建议统一 |

## E2E Pipeline 对照（16 步骤）

```
┌─────────────────────────────────────────────────────────────────┐
│  设备端                          │  后端                        │
├─────────────────────────────────┼───────────────────────────────┤
│  1. 唤醒词检测 ✅                 │  —                           │
│  2. 发送 listen start ✅          │  —                           │
│  3. WebSocket 连接 ✅             │  3. WS 握手 ✅                │
│  4. 音频采集 + Opus 编码 ✅       │  4. VAD 初始化 ✅             │
│  5. 发送音频流 ✅                 │  5. 接收音频流 ✅             │
│  —                               │  6. VAD 语音检测 ❌（超时）   │
│  —                               │  7. STT 识别 ❌（未触发）     │
│  —                               │  8. LLM 生成 ❌（未触发）     │
│  —                               │  9. TTS 合成 ❌（未触发）     │
│  —                               │  10. 发送音频 ❌（未触发）    │
│  11. OnIncomingJson ❌（未触发）  │  —                           │
│  12. OnIncomingAudio ❌（未触发） │  —                           │
└─────────────────────────────────┴───────────────────────────────┘
```

## 修复效果对比

| 指标 | 上次 | 本次 | 变化 |
|------|------|------|------|
| 设备音量控制 | — | ✅ 正常 | 新功能验证 |
| TTS_FIRST_CHUNK | — | 2056ms | 正常范围 |
| VAD 超时行为 | — | 10s 强制结束 | 符合预期 |

## 待解决问题

| # | 问题 | 优先级 | 状态 | 下一步 |
|---|------|--------|------|--------|
| 1 | 摇晃占卜 TTS 延迟 ~110s | P1 | 待排查 | 检查完整后端日志，定位 MCP/LLM 耗时 |
| 2 | /api/device/status Connection reset | P2 | 待排查 | 检查设备端 crash 日志，定位状态 API 问题 |
| 3 | 音频重采样警告过多 | P2 | 待优化 | 检查 AudioService 重采样配置 |
| 4 | VAD 设备端/后端超时不一致 | P3 | 待确认 | 统一超时阈值（设备 30s vs 后端 10s） |
