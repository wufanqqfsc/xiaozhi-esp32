# 贾维斯唤醒第二轮 STT/TTS 分析报告

**会话ID**: 6843515b-94c7-bf01-533d-52cc130db56d  
**分析时间**: 2026-07-10  
**设备IP**: 192.168.0.152

## 一、完整交互时序

| 时间 | 设备端 | 服务端 | 状态 |
|------|--------|--------|------|
| 14:35:50.651 | 发送 `listen Detect` | 收到 Detect 唤醒词 | IDLE → SPEAKING |
| 14:35:50.959 | 发送 `listen Start (Auto)` | 收到 Start | SPEAKING → LISTENING |
| 14:35:54.228 | — | 发送 `tts start` | LISTENING → SPEAKING |
| 14:35:55.162 | — | 发送 `tts sentence_start` "随时为您效劳，先生。" | — |
| 14:35:55.207 | — | ⚠️ **TTS_FIRST_CHUNK ORPHAN (no start)** | — |
| 14:35:57.107 | — | 发送 `tts stop` | SPEAKING → LISTENING |
| 14:35:57.293 | 发送 `listen Start (Auto)` | 收到 Start | — |
| 14:36:02.000 | 5秒无语音 | 状态超时 | LISTENING → THINKING |

## 二、关键问题

### 问题1: TTS 消息未到达设备端
- 服务端日志显示 `tts start` / `tts sentence_start` / `tts stop` 都已发送
- 设备端日志**完全没有**收到 `tts` 相关消息
- WebSocket 连接正常（Session ID 已建立）
- `TTS_FIRST_CHUNK ORPHAN (no start)` 错误表明服务端 TTS 合成器未收到 start 事件

### 问题2: 第二轮 STT 未启动
- 14:35:57.293 设备发送 `listen Start` 进入聆听状态
- 14:36:02.000 5秒后无语音输入，服务端超时进入 THINKING
- 设备端麦克风未采集到有效音频或 VAD 未检测到语音

### 问题3: 设备端 `on_incoming_json_` 回调
- `application.cc:884` 处的 `OnIncomingJson` 回调未触发
- 可能原因：
  1. WebSocket 消息在传输层丢失
  2. JSON 解析失败
  3. 消息类型过滤错误

## 三、待排查方向

1. 在 `websocket_protocol.cc:148` 处添加 ESP_LOGI 打印所有接收的 JSON 消息
2. 检查 `version_` 配置（当前为1），确认与服务端协议版本一致
3. 验证服务端 WebSocketSession 的 `sendTextMessage` 是否真正发送到 TCP 层
4. 检查设备端 FreeRTOS 任务栈是否溢出导致回调丢失

