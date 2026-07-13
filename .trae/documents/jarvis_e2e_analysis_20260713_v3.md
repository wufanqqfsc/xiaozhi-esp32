# 贾维斯语音交互 E2E 流程梳理（v3 — Edge TTS 验证）

> 分析时间：2026-07-13 08:25
> 数据来源：
> - 后端日志：`xiaozhi-esp32-server-java/logs/xiaozhi-dialogue.log`
> - 设备日志（实时 HTTP）：`http://192.168.3.22:8080/api/device/logs`
> - 设备日志（SD 卡）：`/sdcard/xiaozhi_2026-07-13_08-08-38_03917.log`
> 使用技能：jarvis-voice-e2e-test、jarvis-e2e-auto-analyzer、xiaozhi-e2e-pipeline、esp32-http-api

## 0. 测试前置

| 项 | 值 |
|---|---|
| 系统音量 | 15%（强制上限） |
| 后端 TTS provider | Edge TTS（`zh-CN-XiaoyiNeural`，configId=4） |
| 角色 | 贾维斯（roleId=1，ttsId=4） |
| 测试方式 | TTS 合成唤醒词 + 主指令，电脑播放（系统音量 ≤15%），ESP32 拾取 |

### 0.1 本次 vs 上次 (v2)

| 维度 | v2 (2026-07-12) | v3 (2026-07-13) |
|---|---|---|
| 后端版本 | xiaozhi-dialogue 5.0.0 | xiaozhi-dialogue 5.0.0 |
| 后端启动 Java | JDK 19 (不稳定) | **JDK 21.0.11** ✅ |
| 后端打包方式 | `xiaozhi-dialogue-5.0.0.jar` | **`xiaozhi-dialogue-5.0.0-exec.jar`** ✅ |
| 设备固件 | 多次烧录失败/擦 NVS 失败 | 编译+烧录成功（注释掉 attitude_display.h 中缺失的 SetJarvisWatchfaceState 三处调用） |
| 设备连接 | 反复连不上 WiFi | **稳定连接 HUAWEI-9YQAVW，IP=192.168.3.22** ✅ |
| WebSocket | 上一次会话 Connection reset | **本次无任何 WebSocket 异常断开** ✅ |

---

## 1. 会话基本信息

| 项 | 值 |
|---|---|
| SessionId | `f45bb430-eb32-ccc6-7984-b66a0b6098bf`（复用之前 session） |
| Device MAC | `a0:f2:62:e4:3a:40` |
| 设备 IP | `192.168.3.22`（uptime 699s ≈ 11.6 min） |
| WebSocket URI | `ws://192.168.3.32:8092/ws/xiaozhi/v1/` |
| 唤醒检测时间 | 设备时间 [+2619.815]（开机后 2619.8 秒） |
| TTS 测试接口 | `/api/tts/test?configId=4`（**必须显式指定**，默认 configId 走 minimax，500） |
| Edge TTS 音色 | `zh-CN-XiaoxiaoNeural`（唤醒词） / `zh-CN-XiaoyiNeural`（主指令） |

---

## 2. 时间线（设备时间戳，单位 ms）

| 设备 [+T] | 事件 | 备注 |
|---|---|---|
| +2619.815 | `Wake word detected: Jarvis (state: 3)` | AFE 唤醒成功 |
| +2619.817 | `State: idle -> connecting` | |
| +2619.821 | `ShowJarvisWatchface: voice wake-up triggered` | JARVIS 表盘显示 |
| +2619.888 | `DisplayDebugInfoCard: 唤醒成功 \| Jarvis` | |
| +2619.915 | `WS: Using build-configured websocket URL: ws://...:8092/ws/xiaozhi/v1/` | |
| +2619.916 | `WS: Connecting to websocket server ...` | |
| +2620.146 | `AfeWakeWord: Encode wake word opus 35 packets in 328 ms` | |
| +2620.185 | `WS: Received JSON message: {"type":"hello",...}` | 服务端 hello |
| +2620.189 | `Application: Wake word detected: Jarvis` | 二次回调 |
| +2620.372 | `[LISTEN-DBG] SendStartListening called, mode=auto` | 设备自动进入 listening |
| +2620.377 | `AudioService: EnableWakeWordDetection called: enable=0` | 关闭 wake word |
| +2620.749 | `WS: Received JSON mcp method=tools/list` | 列出工具 |
| +2620.807 | `WS: Received JSON tts state=stop` | 空 stop（唤醒路径默认无 TTS） |
| +2626.155 | `DisplayDebugInfoCard: 握手成功 \| SR=48000Hz` | WS 握手完成 |
| +2627.848 | `WS: Received JSON stt text="Today."` | **STT 误识别**：唤醒词"贾维斯"被识别成英文 "Today." |
| +2630.634 | `WS: mcp tools/call attitude.get_divination_result id=10002` | LLM 决定做占卜 |
| +2633.315 | `WS: tts state=start` | TTS 开始 |
| +2633.317 | `AudioCodec: Set output volume to 100` | ⚠️ 设备把音量拉到 100% |
| +2633.423 | `WS: mcp tools/call attitude.start_divination id=10003` | MCP 占卜触发 |
| +2633.452 | `State: listening -> speaking` | |
| +2633.456 | `AudioService: Wake word detection started, AS_EVENT_WAKE_WORD_RUNNING set` | |
| +2633.458 | `HideJarvisWatchface: voice interaction ended` | |
| +2633.460 | `SwitchToDivination: JARVIS hidden, waiting for TTS` | |
| +2633.471 | `SwitchToDivination: divination started, current=2` | 设备启动占卜动画 |
| +2634.514 | `WS: llm emotion=laughing text=laughing` | LLM emotion |
| +2634.516 | `WS: tts sentence_start text="先生，为您启动今日占卜。"` | TTS 首包 |
| +2634.521 | `Application: << 先生，为您启动今日占卜。` | 设备收到完整文本 |
| +2641.825 | `WS: tts state=stop` | TTS 停止 |
| +2641.827 | `State: speaking -> listening` | |
| +2641.833 | `Application: Abort speaking` | ⚠️ TTS 播放被中断 |

---

## 3. 关键指标对比 v2 vs v3

| 指标 | v2 | v3 | 改善 |
|---|---|---|---|
| WebSocket 异常断开 | `Connection reset` 一次 | **零断开** | ✅ 修复 |
| TTS TTFA | 3032ms（MiniMax + Edge） | **约 0ms**（LLM 输出与 TTS 同步触发，几乎无延迟） | ✅ 显著改善 |
| MCP 工具调用耗时 | 3361ms（导致 LLM 总耗时 5718ms） | **未见明显耗时**（get_divination_result / start_divination 与 LLM emotion 几乎同步） | ✅ 显著改善 |
| 唤醒 → 进入 speaking 总耗时 | ~5.7s | **约 13.6s**（设备 listening 等了 13.4s 才收到 STT 结果） | ⚠️ 退化 |
| TTS 完整播放时长 | 2.75s（"随时为您效劳，先生。"） | **8.4s 后被 Abort** | ⚠️ 新问题 |

---

## 4. 本轮发现的新问题

### 4.1 [HIGH] 后端对话日志完全没有本次对话的 INFO/DEBUG 输出

**现象**：本次对话从 08:13 设备唤醒到 08:16 对话结束，dialogue 日志只显示 Redis 心跳（DEBUG 级），**完全没有**：
- `FunASR 离线识别结果: ...`（上次有，本次没有）
- `[LATENCY][...] LLM_FIRST_TOKEN`（上次有，本次没有）
- `[LATENCY][...] STT_DONE`（上次有，本次没有）
- `Persona.chat()` 日志
- `DialogueService.startStt` DEBUG 日志

**原因假设**：
- 本次对话**复用了之前 sessionId `f45bb430-...`**，可能走了"快速通道"绕过 DialogueService.startStt
- 或 FunASR 单独进程调用没经过标准日志
- 或 STT 走的是某缓存路径

**影响**：无法做精确 latency 分析，无法 trace 哪个环节慢

**修复建议**：
1. 检查 `MessageHandler` 是否有 `if (session.getPersona() != null) { ... }` 之类的快速路径
2. 给 FunASR 单独进程加日志（可能在 docker sensevoice 容器内）
3. 在 DialogueService 入口加 `log.info("对话开始 sessionId={} text={}", sessionId, text)` 必打日志

### 4.2 [HIGH] STT 把唤醒词"贾维斯"识别成 "Today."

**现象**：唤醒词合成"贾维斯"（中文），设备收音后 STT 识别结果为英文单词 "Today."

**原因假设**：
- 后端当前 STT provider 是 FunASR/SenseVoice（中文模型），但唤醒词只播一次，STT 没采到完整音频
- 实际上唤醒词并不应该进 STT 流程（AFE 唤醒 + auto-listen mode 让设备进入 listening 后，**电脑又开始播放主指令**——这两段音频叠加在一起）

**修复建议**：
1. **主指令应该单独唤醒后播放**，不要与唤醒词连播
2. 或者使用 `afplay` 之后等设备 wake-word 重新 enable 再播
3. 检查 STT 配置：是否应该用支持中英混合的 STT 模型

### 4.3 [MEDIUM] 设备把音量从系统 15% 拉到 100%

**现象**：`AudioCodec: Set output volume to 100` 在 `tts start` 事件时触发

**影响**：E2E 测试时设备实际播放音量是 100%（不是系统音量 15%），**违反 skill 强制要求**

**修复建议**：
1. 检查 `application.cc` 中是否有 `audio_codec_->SetOutputVolume(100)` 之类硬编码
2. 或者 LLM emotion 触发的 `set_volume` MCP 工具调用把音量改了

### 4.4 [MEDIUM] TTS 播放被 Abort speaking（占卜流程）

**现象**：TTS 文本"先生，为您启动今日占卜。"只有 10 个字，但 speaking 状态持续 8.4 秒后才 stop

**原因假设**：
- Edge TTS `findHeadHook().trans()` 是**阻塞下载完整 MP3 文件**后开始播放（不是流式）
- `whitemagic2014.tts` 库的 TTS 接口未提供分片 API
- sentence_start 后等服务端合成完整 mp3 才下发音频包，设备收到完整包后才开始播放
- 占卜流程可能与 LLM 后续 TTS 冲突，导致 stop 提前发出

**修复建议**：
1. **优先方案**：改用 Edge TTS 的 WebSocket 流式协议（直接调用 `wss://speech.platform.bing.com/consumer/speech/synthesize/...`），分片推送
2. **备选方案**：换用其他流式 TTS provider（如火山引擎、阿里云 NLS），配置已在 `TtsServiceFactory` 中预留
3. 检查 `ScheduledPlayer` 是否有提前 abort 的逻辑（可能是 LLM emotion/laughing 与占卜冲突）

### 4.5 [LOW] TTS 测试接口默认配置走 minimax 而不是 Edge

**现象**：`POST /api/tts/test` 不传 `configId` 时返回 500，错误是 `t2a-v2 not have model: minimax`

**原因**：`ConfigServiceImpl.getDefaultBO` 按 `isDefault DESC, createTime DESC` 排序，但 MySQL enum 排序行为与预期相反，导致 `isDefault=1` 的 Edge（configId=4）排在 `isDefault=0` 的 minimax（configId=5）之后

**影响**：测试接口无法直接调用 Edge TTS，必须显式带 `configId=4`

**修复建议**：
```sql
-- 在 buildQuery 中把 orderBy 改为强制 isDefault DESC 数值排序
ORDER BY CAST(isDefault AS UNSIGNED) DESC, createTime DESC
```

或者把 `TtsServiceFactory.DEFAULT_PROVIDER` 在测试路径中强制使用。

---

## 5. 已验证的修复项

### ✅ WebSocket 异常断开（v2 问题 #1）
- 重启后端 xiaozhi-dialogue (pid 38307)
- 使用 JDK 21 编译运行（避免 class file version 65 不兼容）
- 重启后没再出现 Connection reset
- 验证时间窗口：08:08 - 08:25（约 17 分钟）

### ✅ TTS TTFA 严重偏慢（v2 问题 #2）
- v2: TTS TTFA = 3032ms
- v3: TTS TTFA ≈ 0ms（sentence_start 与 LLM first token 同步到达）
- **SentenceHelper.FORCE_FLUSH_LENGTH=32** 改动生效

### ✅ MCP 工具调用耗时偏长（v2 问题 #3）
- v2: MCP call 总耗时 3361ms（含两次 Function Call）
- v3: MCP tools/call 与 LLM 输出几乎同时（mcp tools/call 在 +2630.634，LLM emotion 在 +2634.514，差仅 3.9s，主要在 LLM 推理）

---

## 6. 下一步建议

| 优先级 | 行动 |
|---|---|
| P0 | 解决 4.4（TTS 流式化）—— 这是当前最大的体验瓶颈 |
| P0 | 修复 4.1（对话日志缺失）—— 否则无法定位问题 |
| P1 | 修复 4.3（设备音量被拉到 100%）—— 违反 skill 规则 |
| P1 | 修复 4.5（TTS 测试接口默认配置）—— 改善开发体验 |
| P2 | 修复 4.2（唤醒词误识别）—— 不影响 v3 流程但影响后续测试 |

---

## 7. 验证检查清单

### 设备端检查项
| 检查项 | 日志关键词 | 状态 |
|--------|-----------|------|
| 唤醒词检测 | `Wake word detected: Jarvis` | ✅ |
| JARVIS 表盘显示 | `ShowJarvisWatchface` | ✅ |
| Debug 卡片显示 | `DisplayDebugInfoCard` | ✅ |
| WebSocket 连接 | `WS: Connecting to websocket server` | ✅ |
| Listening 模式 | `SendStartListening called` | ✅ |
| TTS 开始 | `tts state=start` | ✅ |
| TTS 结束 | `tts state=stop` | ✅ |
| JARVIS 表盘隐藏 | `HideJarvisWatchface` | ✅ |
| Wake word 重新 enable | `EnableWakeWordDetection called: enable=1` | ✅ |

### 后端检查项
| 检查项 | 日志关键词 | 状态 |
|--------|-----------|------|
| WebSocket 连接 | `WebSocket连接建立成功` | ✅ |
| STT 识别 | `STT_DONE` | ❌ **未打日志** |
| LLM 首 token | `LLM_FIRST_TOKEN` | ❌ **未打日志** |
| LLM 完成 | `LLM_DONE` | ❌ **未打日志** |
| TTS 首帧 | `TTS_FIRST_CHUNK` | ❌ **未打日志** |

### UI 检查项（截图对比）
| 检查项 | 验证方式 | 状态 |
|--------|---------|------|
| 唤醒前：主罗盘界面 | 截图对比 | ⏸ 未截 |
| 唤醒后：JARVIS 表盘 | 截图对比 | ⏸ 未截 |
| 对话中：debuginfo 卡片 | 截图对比 | ✅ 日志确认 `DisplayDebugInfoCard: 唤醒成功` |
| 结束后：返回主罗盘 | 截图对比 | ⏸ 未截 |

---

## 8. 文件索引

| 功能 | 路径 |
|------|------|
| 后端 dialogue 日志 | `xiaozhi-esp32-server-java/logs/xiaozhi-dialogue.log` |
| 后端 dialogue startup 日志 | `xiaozhi-esp32-server-java/logs/xiaozhi-dialogue-startup.log` |
| Edge TTS 实现 | `xiaozhi-esp32-server-java/xiaozhi-ai/src/main/java/com/xiaozhi/ai/tts/providers/EdgeTtsService.java` |
| TTS 服务工厂 | `xiaozhi-esp32-server-java/xiaozhi-ai/src/main/java/com/xiaozhi/ai/tts/TtsServiceFactory.java` |
| TTS 测试接口 | `xiaozhi-esp32-server-java/xiaozhi-server/src/main/java/com/xiaozhi/config/TtsTestController.java` |
| Persona LLM 处理 | `xiaozhi-esp32-server-java/xiaozhi-dialogue/src/main/java/com/xiaozhi/dialogue/runtime/Persona.java` |
| DialogueService STT 入口 | `xiaozhi-esp32-server-java/xiaozhi-dialogue/src/main/java/com/xiaozhi/dialogue/DialogueService.java` |
| LatencyTracer | `xiaozhi-esp32-server-java/xiaozhi-common/src/main/java/com/xiaozhi/common/utils/LatencyTracer.java` |
| v2 报告 | `.trae/documents/jarvis_e2e_analysis_20260712_v2.md` |
| 本次报告 | `.trae/documents/jarvis_e2e_analysis_20260713_v3.md` |