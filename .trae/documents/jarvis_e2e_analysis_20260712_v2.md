# 贾维斯语音交互 E2E 流程梳理（最新一次）

> 分析时间：2026-07-12 18:50
> 数据来源：
> - 后端日志：`xiaozhi-esp32-server-java/logs/xiaozhi-dialogue.log`
> - 设备日志：`xiaozhi_2026-07-12_18-45-04_03927.log`（含交互过程）
> - 设备日志：`xiaozhi_2026-07-12_18-46-46_03927.log`（断开后重启）
> 使用技能：xiaozhi-e2e-pipeline、esp32-http-api

## 会话基本信息

| 项 | 值 |
|---|---|
| SessionId | `656f8605-38ef-22fa-db2d-bcb62302b4f7` |
| Device MAC | `a0:f2:62:e4:3a:40` |
| 设备 IP | `192.168.3.22` |
| WebSocket URI | `ws://192.168.3.32:8092/ws/xiaozhi/v1/?device-id=a0:f2:62:e4:3a:40` |
| 时间范围 | 2026-07-12 18:46:15.174 → 18:46:51.770（约 36.6 秒） |
| LLM 提供商 | MiniMax（`https://api.minimaxi.com/v1`） |
| STT 提供商 | SenseVoiceSmall（本地 Docker ws 10096） |
| TTS 提供商 | Edge TTS（`zh-CN-XiaoxiaoNeural`） |
| 角色 | 贾维斯（roleId=1） |
| 后端版本 | xiaozhi-dialogue 5.0.0（pid=82992） |
| 设备版本 | 2.2.6（sdk v5.5.4） |

---

## 第一轮：唤醒应答（成功，耗时 ~5.7s）

对应 E2E pipeline 步骤 1-16（唤醒分支）。

| # | 阶段 | 时间戳 | 耗时 | 日志位置 |
|---|---|---|---|---|
| 1-3 | ESP32 AFE 唤醒词检测 → `Wake word detected: Jarvis` | 设备 [+72.673] | — | 设备日志 |
| 4 | 设备发送 `listen detect "Jarvis"`（WS IN TEXT） | 18:46:15.206 | — | 后端 L? |
| 5 | 后端 `MessageHandler:357` 收到 Detect，状态 `IDLE → SPEAKING` | 18:46:15.207 | **6ms** | 后端 |
| 6 | `LLM_FIRST_TOKEN START` + `TTS_FIRST_CHUNK START` 标记 | 18:46:15.207/.210 | — | 后端 |
| 7 | 设备 `listen start mode=auto`，状态 `SPEAKING → LISTENING`，VAD 初始化 | 18:46:15.245-246 | — | 后端 |
| 8 | MCP initialize 握手 + tools/list 请求 | 18:46:15.466-468 | — | 后端 |
| 9 | 设备返回 tools/list（8036 字节，含 self.attitude.start_divination 等） | 18:46:15.534 | — | 后端 |
| 10 | **LLM 首字返回** `LLM_FIRST_TOKEN delta=1409ms` | 18:46:16.617 | **1409ms**（LLM TTFT） | 后端 |
| 11 | 发送 `tts start`，状态 `LISTENING → SPEAKING` | 18:46:16.707 | — | 后端 |
| 12 | `LLM_DONE ORPHAN`（唤醒应答文本生成完成） | 18:46:16.859 | **252ms**（LLM 总时长） | 后端 |
| 13 | 发送 emotion `loving` + `tts sentence_start "随时为您效劳，先生。"` | 18:46:18.154-155 | — | 后端 |
| 14 | **TTS 首包就绪** `TTS_FIRST_CHUNK delta=3032ms` | 18:46:18.242 | **3032ms**（TTS TTFA） | 后端 |
| 15 | 设备收到 `tts sentence_start`（设备 [+76.824]） | 18:46:18.9 | — | 设备日志 |
| 16 | 发送 `tts stop`（第一轮 TTS 结束） | 18:46:20.904 | — | 后端 |
| 17 | 设备收到 `tts stop`，状态 `SPEAKING → LISTENING`（设备 [+79.574]） | 18:46:20.9 | — | 设备日志 |
| 18 | 设备 `listen start mode=auto`（第二轮用户输入） | 18:46:20.954 | — | 后端 |

**第一轮总耗时**：唤醒（18:46:15.206）→ TTS stop（18:46:20.904）= **5.698s**
**首字响应时间**：唤醒 → LLM 首字 = **1.411s**
**首音到达设备**：唤醒 → TTS 首包 = **3.036s**（MiniMax + Edge TTS）

---

## 第二轮：用户占卜请求（STT 正常 + MCP 工具调用 + WebSocket 异常断开）

### 阶段 A：STT 识别（成功，SenseVoice 表现优秀）

| # | 阶段 | 时间戳 | 耗时 | 说明 |
|---|---|---|---|---|
| 1 | 设备 `[LISTEN-DBG] state=listening` 等待用户说话 | 18:46:24.0（设备 [+84.0]） | — | 距上一轮 TTS stop 3s |
| 2 | 后端 `[DEBUG] 开始调用FunASR STT`（startStt） | 18:46:23.275 | — | VAD START |
| 3 | 状态 `LISTENING → THINKING`（VAD END，Keepalive stopped） | 18:46:24.971 | **1.696s**（VAD START→END） | 用户说话时长 |
| 4 | **SenseVoice STT 返回** `result: 开启占卜。` | 18:46:31.108 | **6.137s**（VAD END→STT 返回） | 后端 L? |
| 5 | 发送 `stt text="开启占卜。"` | 18:46:31.109 | — | 后端 |
| 6 | 设备收到 `stt`，显示 `识别到 \| 开启占卜。`（设备 [+89.833]） | 18:46:31.1 | — | 设备日志 |

**STT 总耗时**：startStt（18:46:23.275）→ STT 返回（18:46:31.108）= **7.833s**
**VAD END → STT 返回**：**6.137s**（含 SenseVoice 推理 + WebSocket 通信）
**STT 结果质量**：`开启占卜。` ✅ **识别准确**（vs 上次 Paraformer 的 `算卦算 蒜瓜，蒜 占卜`）

> 🎉 **对比改进**：上次 Paraformer-large VAD END → STT 返回 = 29.337s，本次 SenseVoice = 6.137s，**提升 4.8 倍**。
> 注：6.137s 仍偏长，推测包含 SenseVoice 容器 CPU 推理（~3-4s）+ P0 主动关闭逻辑的 1.5s 延迟 + 通信开销。

### 阶段 B：LLM + MCP 工具调用（成功，但触发第二轮 LLM）

| # | 阶段 | 时间戳 | 耗时 | 说明 |
|---|---|---|---|---|
| 1 | `LLM_FIRST_TOKEN START` + `TTS_FIRST_CHUNK START` | 18:46:31.119/.122 | — | 后端 |
| 2 | **LLM 首字返回** `delta=1940ms` | 18:46:33.059 | **1940ms**（LLM TTFT） | 后端 |
| 3 | 发送 `tts start`，状态 `THINKING → SPEAKING` | 18:46:33.868 | — | 后端 |
| 4 | 后端发起 MCP 工具调用 `self.attitude.start_divination`（id=10002） | 18:46:34.023 | — | 后端 |
| 5 | 设备收到 MCP `tools/call`（设备 [+92.687]） | 18:46:34.0 | — | 设备日志 |
| 6 | 设备 `HideJarvisWatchface` + `SwitchToDivination`（设备 [+92.840]） | 18:46:34.1 | — | 设备日志 |
| 7 | 设备 `Fortune divination started, result will be index 6` | 18:46:34.1 | — | 设备日志 |
| 8 | 发送 emotion `kissy` + `tts sentence_start "好的，先生。罗盘正在为您启动占卜，"` | 18:46:38.178 | — | 后端 |
| 9 | **TTS 首包就绪** `delta=7168ms` | 18:46:38.290 | **7168ms**（TTS TTFA） | 后端 |
| 10 | 设备收到 `tts sentence_start`（设备 [+96.842]） | 18:46:38.9 | — | 设备日志 |
| 11 | 设备显示 `<< 好的，先生。罗盘正在为您启动占卜，` | 18:46:38.9 | — | 设备日志 |
| 12 | **MCP 工具返回** `占卜已开始，跑马灯动画进行中...`（id=10002） | 18:46:42.058 | **8067ms**（工具调用耗时） | 后端 |
| 13 | `工具调用成功 - self_attitude_start_divination, 耗时: 8067ms` | 18:46:42.066 | — | 后端 |
| 14 | 发送 `tts stop`，状态 `SPEAKING → LISTENING` | 18:46:42.905 | — | 后端 |

### 阶段 C：第二轮 LLM 回复（TTS 播报中 WebSocket 断开）

| # | 阶段 | 时间戳 | 耗时 | 说明 |
|---|---|---|---|---|
| 1 | `LLM_DONE ORPHAN`（第二轮 LLM 完成） | 18:46:44.133 | — | 后端 |
| 2 | 数据库写入 TOOL_CALL + TOOL_RESPONSE + assistant 消息 | 18:46:44.158-168 | — | 后端 |
| 3 | 发送 `tts start`（第二轮 TTS），状态 `LISTENING → SPEAKING` | 18:46:43.948 | — | 后端 |
| 4 | 发送 emotion `confused` + `tts sentence_start "请稍候。先生，占卜已启动，"` | 18:46:45.466-473 | — | 后端 |
| 5 | `TTS_FIRST_CHUNK delta=7262ms total=14430ms` | 18:46:45.553 | — | 后端 |
| 6 | `TTS_FIRST_CHUNK delta=1168ms total=15598ms` | 18:46:46.721 | — | 后端 |
| 7 | 发送 emotion `angry` + `tts sentence_start "太极罗盘正在旋转。"` | 18:46:49.079 | — | 后端 |
| 8 | `TTS_FIRST_CHUNK delta=1291ms total=18250ms` | 18:46:49.373 | — | 后端 |
| 9 | `TTS_FIRST_CHUNK delta=1406ms total=19657ms` | 18:46:50.780 | — | 后端 |
| 10 | ⚠ **WS Transport error** | 18:46:51.694 | — | 后端 |
| 11 | ⚠ **WebSocket closed** `CloseStatus[code=1006, reason=Connection reset]` | 18:46:51.701 | — | 后端 |
| 12 | 会话资源清理：停止播放器 + 清理对话历史 | 18:46:51.767 | — | 后端 |
| 13 | 状态 `SPEAKING → IDLE` | 18:46:51.770 | — | 后端 |
| 14 | 设备重启（新日志文件 `xiaozhi_2026-07-12_18-46-46_03927.log`） | 18:46:46.0 | — | 设备日志 |

**第二轮总耗时**：listen start（18:46:20.954）→ WS 断开（18:46:51.701）= **30.747s**
**WebSocket 断开时点**：第二轮 TTS 播报进行中（total TTS_FIRST_CHUNK 已达 19657ms）

---

## 各阶段耗时汇总

### 第一轮（正常）
| 阶段 | 耗时 | 评价 |
|---|---|---|
| 唤醒词检测 → 后端接收 | <6ms | 正常（LAN） |
| LLM TTFT（首字） | **1409ms** | 正常（MiniMax 云） |
| LLM 总时长 | 252ms | 短文本，正常 |
| TTS TTFA（首包） | **3032ms** | 偏慢（Edge TTS） |
| 唤醒 → TTS stop | **5.698s** | 用户可感知延迟略高 |

### 第二轮 STT（改进显著）
| 阶段 | 耗时 | 评价 |
|---|---|---|
| VAD START → VAD END | **1.696s** | 用户说话时长 |
| **VAD END → STT 返回** | **6.137s** | ⚠ 偏长但可接受（SenseVoice CPU 推理） |
| STT 结果质量 | `开启占卜。` | ✅ **准确识别** |

> **对比上次（Paraformer-large）**：
> - VAD END → STT 返回：29.337s → 6.137s（**提升 4.8 倍**）
> - STT 结果：`算卦算 蒜瓜，蒜 占卜` → `开启占卜。`（**质量大幅提升**）

### 第二轮 LLM + MCP + TTS（问题所在）
| 阶段 | 耗时 | 评价 |
|---|---|---|
| LLM TTFT | **1940ms** | 正常 |
| TTS TTFA | **7168ms** | ⚠ 严重偏慢 |
| MCP 工具调用 | **8067ms** | ⚠ 偏长（设备端跑马灯动画启动） |
| 第二轮 TTS 持续时间 | ~7s（18:46:43.948 → 18:46:51.701） | — |
| WebSocket 断开 | TTS 播报中 | ⚠ 设备异常重启 |

---

## 关键问题诊断

### 1. ✅ SenseVoice STT 切换成功（已修复）
- **上次问题**：Paraformer-large CPU 推理 35-90 秒，结果质量差
- **本次表现**：SenseVoiceSmall 6.137s 返回，结果准确（`开启占卜。`）
- **P0 主动关闭逻辑生效**：未出现 90 秒超时，连接正常关闭
- 对应代码：`xiaozhi-ai/src/main/java/com/xiaozhi/ai/stt/providers/FunASRSttService.java:191`（处理 `offline+is_final=true` 信号）

### 2. ⚠ WebSocket 异常断开（新问题）
- **现象**：18:46:51.701 后端检测到 `CloseStatus[code=1006, Connection reset]`，设备同时重启
- **时序**：第二轮 TTS 播报进行中（已播报"请稍候。先生，占卜已启动，"和"太极罗盘正在旋转。"），设备突然断开
- **可能原因**：
  1. **设备看门狗超时**：TTS TTFA 7168ms + 持续播报，可能触发设备侧超时重启
  2. **内存不足**：设备日志显示 `free sram: 38695 minimal sram: 8159`（仅 38KB SRAM），跑马灯动画 + TTS 解码可能耗尽内存
  3. **跑马灯动画与 TTS 冲突**：`SwitchToDivination` 启动跑马灯后，TTS 音频播放可能资源竞争
  4. **OTA URL 配置错误**：NVS 中 `ota_url=http://192.168.3.39:8091`（应为 192.168.3.32），可能导致 OTA 检查失败触发重启
- **待验证**：需查看设备 `xiaozhi_2026-07-12_18-46-46_03927.log` 是否有 panic/watchdog 记录

### 3. ⚠ TTS TTFA 严重偏慢
- **第一轮**：3032ms（可接受）
- **第二轮**：7168ms（⚠ 严重偏慢）
- **可能原因**：Edge TTS 网络抖动 + MiniMax LLM 流式输出与 TTS 串行依赖
- 对应代码：`xiaozhi-dialogue/.../playback/Synthesizer.java`

### 4. ⚠ MCP 工具调用耗时偏长
- `self.attitude.start_divination` 耗时 8067ms
- 设备端处理：HideJarvis + SwitchToDivination + PlayUiSound + 跑马灯动画启动
- 对应代码：`xiaozhi-esp32/main/display/attitude_display.cc`（SwitchToDivination）

### 5. ✅ 会话资源清理已生效（P3 修复验证）
- WS 断开后 66ms 内完成资源清理：
  - 18:46:51.767 停止播放器
  - 18:46:51.767 清理对话历史
  - 18:46:51.769 会话已关闭
  - 18:46:51.770 状态 `SPEAKING → IDLE`
- **对比上次**：WS 断开后僵尸流程持续 3 分 13 秒，本次 <1 秒清理完成 ✅
- 对应代码：`SessionManager.java:217 cleanupPersonaResources`

---

## 优化建议

### 高优先级

1. **排查设备重启原因**（P0）
   - 获取设备 `xiaozhi_2026-07-12_18-46-46_03927.log` 完整日志，查找 panic/watchdog/reset cause
   - 检查跑马灯动画期间的内存使用（SRAM 仅 38KB）
   - 修正 NVS 中 OTA URL（192.168.3.39 → 192.168.3.32）

2. **优化 TTS TTFA**（P1）
   - 第二轮 TTS TTFA 7168ms 严重影响用户体验
   - 考虑预合成常用应答（如"好的，先生"）或切换更快的 TTS 引擎

3. **MCP 工具调用超时熔断**（P1）
   - `self.attitude.start_divination` 8067ms 偏长，建议设 5s 超时
   - 设备端跑马灯动画启动应异步，不阻塞 MCP 响应

### 中优先级

4. **STT 延迟进一步优化**（P2）
   - 当前 6.137s 仍偏长，SenseVoice 容器 CPU 推理 ~3-4s
   - 可尝试减少 P0 主动关闭延迟（`FINAL_CLOSE_DELAY_MS = 1500` → 500ms）

5. **第二轮 LLM 触发逻辑审查**（P2）
   - MCP 工具调用后 LLM 自动触发了第二轮回复（"请稍候。先生，占卜已启动"）
   - 这是否是预期行为？可能导致不必要的 TTS 播报

---

## E2E Pipeline 对照（16 步骤）

```
User → "Jarvis"
 │
 │ 1.  AFE wake-word detect                ─ 设备 [+72.673] Wake word detected: Jarvis
 │ 2.  MAIN_EVENT_WAKE_WORD_DETECTED       ─ 设备 [+72.674] State: idle -> connecting
 │ 3.  HandleWakeWordDetectedEvent         ─ 设备 [+73.017] ShowJarvisWatchface
 │ 4.  ContinueWakeWordInvoke              ─ 18:46:15.206 WS IN "detect" (后端)
 │ 5.  Listening mode transition           ─ 18:46:15.245 WS IN "start" (后端)
 │ 6.  AudioService PCM→Opus encode        ─ 设备端无详细日志
 │ 7.  MAIN_EVENT_SEND_AUDIO               ─ 18:46:15.188 WS IN BIN (后端)
 │
 ▼  WS binary frame
[JAVA]
 │ 8.  WebSocketHandler → handleBinaryMessage   ─ 18:46:15.188 (后端)
 │ 9.  VadService.processAudio                  ─ 18:46:15.246 VAD 初始化 (后端)
 │ 10. STT stream → first partial               ─ 18:46:31.108 ✅ 6.137s (后端, SenseVoice)
 │ 11. DialogueService.handleText → Persona.chat  ─ 18:46:31.119 (后端)
 │ 12. LLM stream → first token                 ─ 18:46:33.059 delta=1940ms (后端)
 │ 13. Synthesizer.synthesize → TTS             ─ 18:46:38.290 delta=7168ms (后端)
 │ 14. Player.play → sendTtsMessage             ─ 18:46:33.868 tts start (后端)
 │
 ▼  WS JSON + binary
[ESP32]
 │ 15. OnIncomingJson "tts" dispatch            ─ 设备 [+75.670] tts start
 │      ├─ sentence_start "随时为您效劳，先生。"  ─ 设备 [+76.824]
 │      ├─ tts stop                              ─ 设备 [+79.574] State: speaking -> listening
 │      ├─ stt "开启占卜。"                      ─ 设备 [+89.771]
 │      ├─ tts start (第二轮)                    ─ 设备 [+92.532]
 │      ├─ mcp tools/call start_divination       ─ 设备 [+92.687]
 │      ├─ HideJarvisWatchface + SwitchToDivination ─ 设备 [+92.840]
 │      ├─ sentence_start "好的，先生。罗盘正在为您启动占卜，" ─ 设备 [+96.842]
 │      └─ sentence_start "太极罗盘正在旋转。"    ─ 设备 [+99.079] (推算)
 │ 16. OnIncomingAudio → playback               ─ 设备端无详细日志
 │
 ▼  ⚠ 18:46:51.694 WS Transport error
 ▼  ⚠ 18:46:51.701 WS Connection reset (后端)
 ▼  设备重启，新日志 xiaozhi_2026-07-12_18-46-46_03927.log
```

---

## 修复效果对比

| 指标 | 上次（Paraformer-large） | 本次（SenseVoiceSmall） | 改善 |
|---|---|---|---|
| STT VAD END → 返回 | 29.337s | 6.137s | **提升 4.8 倍** ✅ |
| STT 结果质量 | `算卦算 蒜瓜，蒜 占卜` | `开启占卜。` | **准确识别** ✅ |
| WS 断开后僵尸流程 | 3 分 13 秒 | <1 秒 | **已修复** ✅ |
| 90 秒协议死锁 | 是 | 否 | **已修复** ✅ |
| 会话资源清理 | 未清理 | 66ms 完成 | **已修复** ✅ |
| 角色一致性 | 错误（"小何"） | 正确（"先生"） | **已修复** ✅ |

---

## 待解决问题

| # | 问题 | 优先级 | 状态 | 下一步 |
|---|---|---|---|---|
| 1 | WebSocket 异常断开（设备重启） | P0 | 🔴 待排查 | 获取设备重启日志，检查内存/看门狗 |
| 2 | TTS TTFA 7168ms 严重偏慢 | P1 | 🟡 已识别 | 优化 TTS 引擎或预合成 |
| 3 | MCP 工具调用 8067ms 偏长 | P1 | 🟡 已识别 | 设备端异步处理 |
| 4 | NVS OTA URL 配置错误（.39 应为 .32） | P2 | 🟡 已识别 | 清除 NVS 或修正配置 |
| 5 | STT 6.137s 仍有优化空间 | P2 | 🟢 可接受 | 减少 P0 关闭延迟 |
