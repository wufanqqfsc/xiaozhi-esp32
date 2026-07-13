# 贾维斯语音交互 E2E 流程梳理(最新一次)

> 分析时间:2026-07-12
> 数据来源:`xiaozhi-esp32-server-java/logs/xiaozhi-dialogue.log`
> 使用技能:xiaozhi-e2e-pipeline、esp32-http-api

## 会话基本信息

| 项 | 值 |
|---|---|
| SessionId | `746a360c-6fee-004f-779e-685651dee896` |
| Device MAC | `a0:f2:62:e4:3a:40` |
| WebSocket URI | `ws://192.168.3.32/ws/xiaozhi/v1/?device-id=a0:f2:62:e4:3a:40` |
| 时间范围 | 2026-07-12 16:41:41.256 → 16:46:50.850(约 5 分 9 秒) |
| LLM 提供商 | MiniMax(`https://api.minimaxi.com/v1/chat/completions`) |
| STT 提供商 | FunASR(本地 ws 10096) |
| 后端日志 | `xiaozhi-esp32-server-java/logs/xiaozhi-dialogue.log` |
| 设备状态 | 当前 `192.168.3.39:8080` 不可达(无法取设备端日志) |

---

## 第一轮:唤醒应答(成功,耗时 ~6.5s)

对应 E2E pipeline 步骤 1-16(唤醒分支)。

| # | 阶段 | 时间戳 | 耗时 | 日志位置 |
|---|---|---|---|---|
| 1-3 | ESP32 AFE 唤醒词检测 → `MAIN_EVENT_WAKE_WORD_DETECTED` | 设备端(无日志) | 5-30ms(典型) | — |
| 4 | 设备发送 `listen detect "Jarvis"`(WS IN TEXT) | 16:41:41.256 | — | L9472 |
| 5 | 后端 `DialogueService:222` 检测到唤醒词,状态 `IDLE → SPEAKING` | 16:41:41.262-263 | **6ms** | L9476-9478 |
| 6 | `LLM_FIRST_TOKEN START` + `TTS_FIRST_CHUNK START` 标记 | 16:41:41.263 / .312 | — | L9480-9482 |
| 7 | HTTP POST 到 MiniMax API | 16:41:41.390 | **127ms**(唤醒→请求) | L9486 |
| 8 | 设备进入监听 `listen start mode=auto`,状态 `SPEAKING → LISTENING`,VAD 初始化 | 16:41:41.419-421 | — | L9490-9500 |
| 9 | **LLM 首字返回** `LLM_FIRST_TOKEN delta=1424ms` | 16:41:42.687 | **1424ms**(LLM TTFT) | L9864 |
| 10 | `LLM_DONE`(唤醒应答文本生成完成) | 16:41:43.305 | **622ms**(LLM 总时长) | L9964 |
| 11 | 发送 `tts start` | 16:41:43.183 | — | L9950-9952 |
| 12 | 发送 emotion `funny` + `tts sentence_start "随时为您效劳，先生。"` | 16:41:45.076-077 | — | L10032-10038 |
| 13 | **TTS 首包就绪** `TTS_FIRST_CHUNK delta=4070ms` | 16:41:45.383 | **4070ms**(TTS TTFA) | L10056 |
| 14 | 设备播放 TTS(无后端日志) | 16:41:45-47 | ~2.4s | — |
| 15 | 发送 `tts stop`(第一轮 TTS 结束) | 16:41:47.780 | — | L10208-10210 |
| 16 | 设备再次 `listen start mode=auto`,VAD 重新初始化(进入第二轮用户输入) | 16:41:47.945-946 | — | L10216-10222 |

**第一轮总耗时**:唤醒(16:41:41.256)→ TTS stop(16:41:47.780)= **6.524s**
**首字响应时间**:唤醒 → LLM 首字 = **1.431s**
**首音到达设备**:唤醒 → TTS 首包 = **4.127s**(MiniMax + Edge TTS 链路偏慢)

---

## 第二轮:用户占卜请求(STT 超时 + WebSocket 异常断开)

| # | 阶段 | 时间戳 | 耗时 | 日志位置 |
|---|---|---|---|---|
| 1 | VAD 检测到语音,`startStt` audioSinks 就绪 | 16:41:52.793 | 唤醒后 **11.5s** 才进入 STT(用户开口晚) | L10704 |
| 2 | VAD 检测语音结束,静音 1243ms | 16:41:55.486 | 说话时长 ~2.7s | L10984 |
| 3 | **FunASR 识别超时** | 16:43:22.800 | **⚠ 27.314s**(VAD END → 超时) | L11631 |
| 4 | FunASR 超时但已有识别结果:`算卦算 蒜瓜，蒜 占卜` | 16:43:24.822 | +2.022s(回退读取 offlineResult) | L11633 |
| 5 | 后端 `DialogueService:191` STT 返回 + 发送 `stt` 消息 | 16:43:24.823-824 | — | L11635-11639 |
| 6 | `LLM_FIRST_TOKEN START` + `TTS_FIRST_CHUNK START` | 16:43:24.834 / .842 | — | L11643-11645 |
| 7 | **LLM 首字返回** `delta=2017ms` | 16:43:26.852 | **2017ms**(LLM TTFT) | L11683 |
| 8 | 设备上报 MCP `divination_result result=2` | 16:43:27.704 | — | L11721 |
| 9 | 发送 `tts start` | 16:43:28.191 | — | L11885 |
| 10 | 后端发起 MCP 工具调用 `self.attitude.start_divination`(id=10002) | 16:43:28.222 | — | L11897 |
| 11 | 发送 `tts sentence_start "先生，稍候，我这就为您起卦。"` | 16:43:29.507 | — | L11915 |
| 12 | **TTS 首包就绪** `delta=4930ms` | 16:43:29.773 | **4930ms**(TTS TTFA) | L11937 |
| 13 | ⚠ **WebSocket 异常断开** `CloseStatus[code=1006, Connection reset]` | 16:43:37.978 | 设备主动断开 | L12191 |
| 14 | 工具调用"成功" `self_attitude_start_divination` 耗时 30030ms | 16:43:58.244 | **30030ms**(实际是 30s 超时) | L12325 |

**第二轮 STT 总耗时**:VAD END(16:41:55.486)→ STT 返回(16:43:24.823)= **29.337s**(严重异常,FunASR 未在 VAD END 时关闭流)
**STT 结果质量差**:`算卦算 蒜瓜，蒜 占卜`(应是"算卦"或"占卜")
**设备在 TTS 播报"先生稍候"后 ~8s 断开 WS**:可能用户觉得卡死或设备侧超时

---

## 第三轮及之后:后端僵尸流程(MCP 工具调用死循环)

WebSocket 已断开,但后端 `Persona.chatStream` 未感知到连接断开,继续因 LLM 反复触发 `self.attitude.start_divination` 工具调用,每次都 30s 超时,且所有 WS OUT 消息发送失败。

| 时间 | 事件 | 耗时 | 日志 |
|---|---|---|---|
| 16:44:00.434 | MCP 工具调用 id=10003 → **发送失败** | — | L12385-12387 |
| 16:44:02.023 | `TTS_FIRST_CHUNK delta=32251ms total=37181ms` | — | L12525 |
| 16:44:30.448 | 工具调用"成功" 耗时 30008ms | 30s | L12638 |
| 16:45:04.576 | MCP id=10005 → **发送失败** | — | L12891-12893 |
| 16:45:34.595 | 工具调用"成功" 耗时 30020ms | 30s | L13045 |
| 16:45:36.593 | MCP id=10006 → **发送失败** | — | L13091-13093 |
| 16:46:06.616 | 工具调用"成功" 耗时 30025ms | 30s | L13247 |
| 16:46:08.864 | LLM 返回 `Jarvis view` 相关内容 | — | L13265 |
| 16:46:10.151 | MCP id=10007 → **发送失败** | — | L13305-13307 |
| 16:46:43.361 | `LLM_DONE ORPHAN` | — | L13537 |
| 16:46:46.057 | `TTS_FIRST_CHUNK delta=164038ms total=201220ms` | **201s** | L14373 |
| 16:46:50.850 | 最后一条 `TTS_FIRST_CHUNK delta=1258ms total=206013ms` | — | L14697 |

**僵尸流程总耗时**:WebSocket 断开(16:43:37)→ 最后一条 TTS(16:46:50)= **3 分 13 秒**

---

## 各阶段耗时汇总

### 第一轮(正常)
| 阶段 | 耗时 | 评价 |
|---|---|---|
| 唤醒词检测 → 后端接收 | <6ms | 正常(LAN) |
| LLM TTFT(首字) | **1424ms** | 正常(MiniMax 云) |
| LLM 总时长 | 622ms | 短文本,正常 |
| TTS TTFA(首包) | **4070ms** | 偏慢(Edge TTS) |
| 唤醒 → TTS stop | **6.524s** | 用户可感知延迟略高 |

### 第二轮(异常)
| 阶段 | 耗时 | 评价 |
|---|---|---|
| VAD START → VAD END | ~2.7s | 用户说话时长 |
| **VAD END → STT 返回** | **29.337s** | ⚠ 严重异常,FunASR 流未及时关闭 |
| STT 结果回退读取 | 2.022s | 兜底逻辑生效(offlineResult) |
| LLM TTFT | 2017ms | 正常 |
| TTS TTFA | 4930ms | 偏慢 |
| 设备 WS 断开时间点 | TTS 播报后 8.5s | 用户/设备超时 |

### 第三轮+(僵尸)
| 阶段 | 耗时 | 评价 |
|---|---|---|
| 单次 MCP `start_divination` 超时 | **30s** ×6 次 | ⚠ 工具调用无超时熔断 |
| 后端僵尸流程总时长 | **3 分 13 秒** | ⚠ WS 断开后未中止 LLM/TTS |
| 累计 TTS_FIRST_CHUNK total | **206s** | 完全失控 |

---

## 关键问题诊断

1. **FunASR STT 流关闭逻辑缺陷**(已部分修复但仍异常)
   - VAD END(16:41:55)→ FunASR 超时(16:43:22)= 27.3s,说明 `onClose` 未在 VAD END 时被触发,依赖 30s 超时兜底
   - 结果质量差("算卦算 蒜瓜"),可能与音频切分异常或中英混合识别有关
   - 对应代码:`xiaozhi-ai/src/main/java/com/xiaozhi/ai/stt/providers/FunASRSttService.java:183`

2. **MCP 工具调用无超时熔断**
   - `self.attitude.start_divination` 连续 6 次 30s 超时,后端未在第一次超时后中止 LLM 循环
   - LLM 反复决定调用同一工具,陷入死循环
   - 对应代码:`xiaozhi-dialogue/src/main/java/com/xiaozhi/dialogue/runtime/Persona.java:193`(`conversation.addToolCallChain`)

3. **WebSocket 断开后后端未清理会话**
   - 16:43:37.978 WS 关闭(Connection reset),但 `Persona.chatStream` 继续运行 3 分钟,所有 WS OUT 失败
   - 应在 WS onClose 时取消正在进行的 LLM 流并中断工具调用
   - 对应代码:`xiaozhi-dialogue/src/main/java/com/xiaozhi/communication/server/websocket/WebSocketHandler.java`(`afterConnectionClosed`)

4. **设备端原因待查**
   - 设备 HTTP API(192.168.3.39:8080)当前不可达,无法获取设备日志确认断开原因
   - 推测:用户在 TTS 播报"先生稍候"后等待 8s 仍未有结果,触发设备侧超时或用户主动退出

---

## 优化建议

- 修复 FunASR STT 在 VAD END 时主动关闭 WebSocket 流,避免依赖 30s 超时
- 为 MCP 工具调用增加全局超时熔断(如同一工具连续 2 次超时则中止本轮 LLM)
- 在 `WebSocketHandler.afterConnectionClosed` 中取消该 session 的所有正在进行的 LLM/TTS/MCP 任务
- 设备恢复在线后,抓取 `xiaozhi_boot_*.log` 确认 16:43:37 断开原因(看门狗/用户操作/网络抖动)

---

## E2E Pipeline 对照(16 步骤)

```
User → "Jarvis"
 │
 │ 1.  AFE wake-word detect                ─ 设备端无日志
 │ 2.  MAIN_EVENT_WAKE_WORD_DETECTED       ─ 设备端无日志
 │ 3.  HandleWakeWordDetectedEvent         ─ 设备端无日志
 │ 4.  ContinueWakeWordInvoke              ─ 16:41:41.256 WS IN "detect" (L9472)
 │ 5.  Listening mode transition           ─ 16:41:41.419 WS IN "start" (L9490)
 │ 6.  AudioService PCM→Opus encode        ─ 设备端无日志
 │ 7.  MAIN_EVENT_SEND_AUDIO               ─ 16:41:52.793 startStt (L10704)
 │
 ▼  WS binary frame
[JAVA]
 │ 8.  WebSocketHandler → handleBinaryMessage   ─ 16:41:41.256 (L9470)
 │ 9.  VadService.processAudio                  ─ 16:41:41.421 VAD 初始化 (L9500)
 │ 10. STT stream → first partial               ─ 16:43:24.823 ⚠ 超时 27.3s (L11635)
 │ 11. DialogueService.handleText → Persona.chat  ─ 16:43:24.834 (L11643)
 │ 12. LLM stream → first token                 ─ 16:43:26.852 delta=2017ms (L11683)
 │ 13. Synthesizer.synthesize → TTS             ─ 16:43:29.773 delta=4930ms (L11937)
 │ 14. Player.play → sendTtsMessage             ─ 16:43:28.191 tts start (L11885)
 │
 ▼  WS JSON + binary
[ESP32]
 │ 15. OnIncomingJson "tts" dispatch            ─ 设备端无日志
 │ 16. OnIncomingAudio → playback               ─ 设备端无日志
 │
 ▼  ⚠ 16:43:37.978 WS Connection reset (L12191)
```
