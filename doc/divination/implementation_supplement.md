# 摇一摇占卜 — 实现补充文档（T15/T16）

> 更新：2026-07-15  
> 关联：[feature_shake_divination.md](./feature_shake_divination.md) · [device_verification_checklist.md](./device_verification_checklist.md)

---

## 一、异常 Cleanup 序列（T15）

设备端占卜相关状态在异常、超时、TTS 结束、用户打断时必须按固定顺序收敛，避免 callback 泄漏、视图栈错乱、标志位卡死。

### 1.1 设备端 Cleanup 总序

```
1. 停止音频/UI 音效（StopUiSound / AbortSpeaking）
2. 触发或清空 MCP 延迟回调（divination_deferred_callback_ 一次性消费）
3. 停止跑马灯定时器 + 重置 FortuneDivinationState → Idle
4. 重置标志位（divination_from_shake_ / divination_from_jarvis_ / divination_waiting_for_tts_ / divination_tts_started_）
5. 清理 GIF / 图片预览层（HideImagePreview / ClearPreviewGif）
6. 视图栈归一化（ReturnToCompassIdleView 或 SwitchBackFromDivination）
7. 恢复唤醒词 / DeviceState → Idle
```

### 1.2 按场景分解

| 场景 | 入口函数 | Cleanup 路径 | 关键标志 |
|------|----------|--------------|----------|
| 链路 A TTS 正常结束 | `tts:stop` → `ReturnToCompassAfterTts` | `StopFortuneDivination` → `ReturnToCompassIdleView` | `divination_from_shake_=true` 且 `divination_tts_started_=true` |
| 链路 B TTS 正常结束 | `tts:stop` → `ReturnToCompassAfterTts` | `SwitchBackFromDivination` → `ShowJarvisWatchface` | `divination_from_jarvis_=true` 且 `divination_tts_started_=true` |
| 链路 B 跑马灯结束（无 TTS） | `FinishFortuneDivination` 2s timer | `SwitchBackFromDivination` → 触发 deferred callback | `divination_from_jarvis_=true` |
| 35s 超时（链路 A） | `OnFortuneDivinationTick` | `StopFortuneDivination` + 调试卡 + `ReturnToCompassIdleView` | `divination_waiting_for_tts_=true` |
| abort 空 tts:stop | `tts:stop`（无 tts:start） | **不**调用 `ReturnToCompassAfterTts` | `divination_tts_started_=false` |
| WebSocket 断开 | 协议层 | `StopFortuneDivination` + 罗盘归一化 | 全部标志清零 |
| `SwitchBackFromDivination` 异常 | 兜底 | `ReturnToCompassIdleView` | `view_stack_.clear()` + push(Compass) |

### 1.3 Callback 清理规则

| 回调类型 | 注册方 | 触发方 | 清理 |
|----------|--------|--------|------|
| `divination_callback_`（常驻） | `application.cc` 初始化 | 链路 A `FinishFortuneDivination` / 链路 B `SwitchBackFromDivination` | **不**置 null，永久保留 |
| `divination_deferred_callback_`（一次性） | `mcp_server.cc` 收到 `__DEFERRED_DIVINATION__` | 同上 FireDivinationCallbacks | `std::move` 消费后自动清空 |
| 服务端 `divinationResultFetched` | `ChatSession` | `get_divination_result` 或 T18 强制补调 | `maybeAppendDivinationClosing` / `onWakeWord` 时重置 |

### 1.4 视图栈（ViewStack）归一化

| 当前视图 | 目标 | 函数 |
|----------|------|------|
| Divination（链路 A） | Compass | `ReturnToCompassIdleViewUnlocked` |
| Divination（链路 B） | JarvisWatchface | `SwitchBackFromDivination` |
| 栈不一致 | Compass | `view_stack_.clear()` + `push(Compass)` |

### 1.5 服务端 Cleanup

| 场景 | 动作 |
|------|------|
| TTS 流结束（占卜链路） | `maybeAppendDivinationClosing` → mode 回 `JARVIS_MENU` |
| `abortDialogue` | 取消 Synthesizer + Player 队列 |
| 会话关闭 | `PersonaCleanup` 释放 Persona |
| Redis 角色变更 | 清理 Persona 缓存，下次唤醒重建 |

---

## 二、设备 ↔ 服务端消息协议（T16）

### 2.1 设备 → 服务端（WebSocket Text）

| type | state/字段 | 触发时机 | 服务端处理 |
|------|-----------|----------|------------|
| `hello` | version, features | 连接握手 | `handleHelloMessage` |
| `listen` | `start` | 开始录音 | `DeviceState.LISTENING` + VAD 初始化 |
| `listen` | `stop` | 停止录音 | 关闭音频流 → `IDLE` |
| `listen` | `text` | 文本输入（含摇一摇 hidden prompt） | `handleText`；摇一摇 prompt **不** abort |
| `listen` | `detect` | 唤醒词 | `handleWakeWord` → mode=`JARVIS_MENU` |
| `abort` | reason | 用户/设备打断 | `abortDialogue` |
| `mcp` | payload | 设备 MCP 响应 | `handleDeviceMcpMessage` |
| `iot` | descriptors/states | IoT 描述/状态 | `handleIotMessage` |
| `goodbye` | — | 会话结束 | 关闭会话 |

### 2.2 设备 → 服务端（WebSocket Binary）

| 内容 | 时机 | 服务端处理 |
|------|------|------------|
| Opus 音频帧 | `listen/start` 后 | VAD → STT → `handleText` |

### 2.3 服务端 → 设备（WebSocket Text）

| type | state | 字段 | 设备处理 |
|------|-------|------|----------|
| `tts` | `start` | — | `MarkDivinationTtsStarted` + `StopMarqueeForTts` + `Speaking` |
| `tts` | `sentence_start` | `text` | `SetChatMessage("assistant")` → `RouteToJarvisStatusBar` |
| `tts` | `stop` | — | 若 `ShouldFinalizeDivinationOnTtsStop` → `ReturnToCompassAfterTts` |
| `stt` | — | `text` | `SetChatMessage("user")` |
| `llm` | — | `emotion` | `SetEmotion`（AttitudeDisplay no-op） |
| `mcp` | — | `payload` | `McpServer::ParseMessage`（含 tools/call） |
| `keepalive` | — | — | 刷新 `last_incoming_time_` |
| `iot` | — | `commands` | IoT 命令执行 |

### 2.4 服务端 → 设备（WebSocket Binary）

| 内容 | 时机 | 设备处理 |
|------|------|----------|
| Opus TTS 帧 | TTS 播放中 | 解码 → I2S 播放 |

### 2.5 设备 MCP 工具（占卜相关）

| 工具名 | 方向 | 说明 |
|--------|------|------|
| `self.attitude.start_divination` | 服务端→设备 | 链路 B：隐藏 JARVIS，启动跑马灯 |
| `self.attitude.get_divination_result` | 服务端→设备 | 查询结果；Animating 时返回 `__DEFERRED_DIVINATION__` |
| `self.attitude.stop_divination` | 服务端→设备 | 强制停止跑马灯 |
| `search_and_display_gif` | 服务端全局工具 | 百度搜图 → HTTP 推送设备 SD 卡 |

### 2.6 服务端 SessionInteractionMode（T13）

| 模式 | 设置时机 | 作用 |
|------|----------|------|
| `IDLE` | 默认 / cleanup | 普通对话 |
| `JARVIS_MENU` | 唤醒词后 | 等待用户选 1-8 |
| `DIVINATION_PENDING` | 用户选菜单数字 | 等待 `start_divination` |
| `DIVINATION_ACTIVE` | `start_divination` 成功 | 跑马灯中；T17 注入等待提示 |
| `SHAKE_DIVINATION` | 摇一摇 listen/text | T14 结束语 + T18 强制 get_result |

### 2.7 HTTP 设备 API（GIF 推送）

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/display/show` | 服务端推送 GIF/图片到设备 SD 卡并显示 |

---

## 三、T18 强制补调说明

当 `SessionInteractionMode` 为占卜相关且 `divinationResultFetched=false`，若 LLM 输出句含「占卜结果 / 综合运势」等特征文本而未调用 `get_divination_result`，服务端在 TTS 分句前强制调用设备 MCP 工具 `self.attitude.get_divination_result`。

日志关键字：`[T18] LLM 未调用 get_divination_result，强制补调`

---

*本文档随实现迭代更新；真机验收项见 [device_verification_checklist.md](./device_verification_checklist.md)。*
