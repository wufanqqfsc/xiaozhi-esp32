---
name: "xiaozhi-voice-e2e-test"
description: "通过 TTS 播放语音模拟用户交互，测试 ESP32 设备端到端语音交互全流程。Invoke when you want to test wake word detection, STT, LLM, TTS, UI transitions, or any voice interaction features without physical speaking."
---

# 小智语音交互 E2E 测试

通过 TTS 合成语音并播放，模拟用户说话，验证 ESP32 设备与后端的完整语音交互链路。

## 适用场景

- 验证唤醒词检测功能
- 测试完整语音对话流程（唤醒 → STT → LLM → TTS → 播放）
- 验证 UI 状态切换（JARVIS 表盘、debuginfo 卡片等）
- 回归测试功能变更
- 占卜流程等复杂交互验证

## 前置条件

| 条件 | 验证命令 |
|------|---------|
| 后端服务运行中 | `cd xiaozhi-esp32-server-java && ./start.sh status` |
| ESP32 设备在线 | `curl http://<DEVICE_IP>:8080/api/device/status` |
| 设备 WiFi 已连接 | status 中 `wifi_connected: true` |
| TTS 接口可用 | `curl -X POST /api/tts/test` 验证 |
| 电脑有音频输出 | macOS 用 `afplay`，Linux 用 `aplay` |

## 测试流程总览

```
电脑 TTS 合成语音 → 播放 → ESP32 麦克风拾取
     │
     ▼
唤醒词检测 → WebSocket 连接 → 音频流传输
     │
     ▼
后端 STT 识别 → LLM 对话 → TTS 合成
     │
     ▼
ESP32 播放回复 → UI 状态切换 → 结束
```

## 快速开始

### 步骤 1: 登录获取 Token

```bash
TOKEN=$(curl -s -X POST http://localhost:8091/api/user/login \
  -d '{"username":"admin","password":"123456"}' \
  -H 'Content-Type: application/json' \
  | python3 -c "import sys,json; print(json.load(sys.stdin).get('data',{}).get('token',''))")
echo "Token: ${TOKEN:0:30}..."
```

### 步骤 2: 合成并播放唤醒语音

```bash
DEVICE_IP="192.168.3.22"

# 合成唤醒词语音（英文音色唤醒效果更好）
curl -s -X POST http://localhost:8091/api/tts/test \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/x-www-form-urlencoded" \
  -d "text=Hey Jarvis, what's the weather today?&voiceName=en-US-AriaNeural&speed=0.9&pitch=1.0" \
  -o /tmp/wake_test.mp3

# 播放语音
afplay /tmp/wake_test.mp3
```

### 步骤 3: 验证唤醒结果

```bash
# 等待 5-10 秒后检查设备日志
sleep 8

# 检查设备端唤醒日志
LATEST_LOG=$(curl -s http://$DEVICE_IP:8080/api/sdcard/files \
  | python3 -c "import sys,json; files=json.load(sys.stdin); logs=[f for f in files if f['name'].endswith('.log')]; logs.sort(key=lambda f: f['mtime'], reverse=True); print(logs[0]['name'])")

curl -s "http://$DEVICE_IP:8080/api/sdcard/files/$LATEST_LOG" \
  | grep -iE "Wake word|ShowJarvis|HideJarvis|State:|tts" \
  | tail -20
```

## 完整测试用例

### 用例 1: 基础唤醒对话

**测试目标**：验证唤醒 → 对话 → 回复全链路

```bash
DEVICE_IP="192.168.3.22"
TOKEN=...

# 1. 截图：播放前
curl -s -X POST http://$DEVICE_IP:8080/api/sdcard/shots
sleep 2

# 2. 合成并播放唤醒+对话语音
curl -s -X POST http://localhost:8091/api/tts/test \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/x-www-form-urlencoded" \
  -d "text=贾维斯，今天天气怎么样？&voiceName=zh-CN-XiaoxiaoNeural&speed=0.9&pitch=1.0" \
  -o /tmp/test_weather.mp3

afplay /tmp/test_weather.mp3

# 3. 等待对话完成
sleep 20

# 4. 截图：对话结束后
curl -s -X POST http://$DEVICE_IP:8080/api/sdcard/shots
sleep 2

# 5. 验证结果
echo "=== 设备日志 ==="
LATEST_LOG=$(curl -s http://$DEVICE_IP:8080/api/sdcard/files \
  | python3 -c "import sys,json; files=json.load(sys.stdin); logs=[f for f in files if f['name'].endswith('.log')]; logs.sort(key=lambda f: f['mtime'], reverse=True); print(logs[0]['name'])")

curl -s "http://$DEVICE_IP:8080/api/sdcard/files/$LATEST_LOG" | grep -iE "Wake word|ShowJarvis|HideJarvis|State:|tts|stt" | tail -30

echo ""
echo "=== 后端对话日志 ==="
grep -E "STT|LLM|TTS|LATENCY" xiaozhi-esp32-server-java/logs/xiaozhi-dialogue.log | tail -20
```

**预期结果**：
- ✅ 设备检测到唤醒词 "Jarvis"
- ✅ 显示 JARVIS 表盘
- ✅ WebSocket 连接建立
- ✅ STT 识别出文本
- ✅ LLM 返回回复
- ✅ TTS 合成并播放
- ✅ 对话结束后隐藏 JARVIS 表盘，返回 idle

### 用例 2: 占卜交互测试

**测试目标**：验证占卜相关 UI 切换和功能

```bash
# 合成占卜问题语音
curl -s -X POST http://localhost:8091/api/tts/test \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/x-www-form-urlencoded" \
  -d "text=贾维斯，帮我占卜一下今天的运势&voiceName=zh-CN-XiaoxiaoNeural&speed=0.9&pitch=1.0" \
  -o /tmp/test_divination.mp3

afplay /tmp/test_divination.mp3

# 等待占卜结果
sleep 25

# 检查日志中的占卜相关信息
curl -s "http://$DEVICE_IP:8080/api/sdcard/files/$LATEST_LOG" \
  | grep -iE "divination|fortune|占卜|罗盘|compass|debuginfo|SetChatMessage" \
  | tail -20
```

### 用例 3: 多轮对话测试

**测试目标**：验证上下文记忆和多轮交互

```bash
# 第一轮：自我介绍
curl -s -X POST http://localhost:8091/api/tts/test \
  -H "Authorization: Bearer $TOKEN" \
  -d "text=贾维斯，我叫小明&voiceName=zh-CN-XiaoxiaoNeural&speed=0.9&pitch=1.0" \
  -o /tmp/test_multi1.mp3
afplay /tmp/test_multi1.mp3
sleep 15

# 第二轮：询问是否记得
curl -s -X POST http://localhost:8091/api/tts/test \
  -H "Authorization: Bearer $TOKEN" \
  -d "text=我叫什么名字？&voiceName=zh-CN-XiaoxiaoNeural&speed=0.9&pitch=1.0" \
  -o /tmp/test_multi2.mp3
afplay /tmp/test_multi2.mp3
sleep 15
```

## 验证检查清单

### 设备端检查项

| 检查项 | 日志关键词 | 状态 |
|--------|-----------|------|
| 唤醒词检测 | `Wake word detected: Jarvis` | ⬜ |
| JARVIS 表盘显示 | `ShowJarvisWatchface` | ⬜ |
| Debug 卡片显示 | `DisplayDebugInfoCard` | ⬜ |
| WebSocket 连接 | `WebSocket connected` | ⬜ |
| Listening 模式 | `State: speaking -> listening` | ⬜ |
| TTS 开始 | `State: listening -> speaking` | ⬜ |
| TTS 结束 | `State: speaking -> idle` | ⬜ |
| JARVIS 表盘隐藏 | `HideJarvisWatchface` | ⬜ |

### 后端检查项

| 检查项 | 日志关键词 | 状态 |
|--------|-----------|------|
| WebSocket 连接 | `WebSocket连接建立成功` | ⬜ |
| STT 识别 | `STT_DONE` | ⬜ |
| LLM 首 token | `LLM_FIRST_TOKEN` | ⬜ |
| LLM 完成 | `LLM_DONE` | ⬜ |
| TTS 首帧 | `TTS_FIRST_CHUNK` | ⬜ |
| TTS 停止 | `state=stop` | ⬜ |

### UI 检查项（截图对比）

| 检查项 | 验证方式 | 状态 |
|--------|---------|------|
| 唤醒前：主罗盘界面 | 截图对比 | ⬜ |
| 唤醒后：JARVIS 表盘 | 截图对比 | ⬜ |
| 对话中：debuginfo 卡片 | 截图对比 | ⬜ |
| 结束后：返回主罗盘 | 截图对比 | ⬜ |

## 常用调试技巧

### 1. 实时监控设备日志

```bash
while true; do
  curl -s http://$DEVICE_IP:8080/api/device/logs 2>/dev/null | tail -5
  sleep 2
  clear
done
```

### 2. 实时监控后端对话日志

```bash
tail -f xiaozhi-esp32-server-java/logs/xiaozhi-dialogue.log \
  | grep -E "LATENCY|STT|LLM|TTS|检测到|FunASR"
```

### 3. 截图保存对比

```bash
# 播放前截图
curl -s -X POST http://$DEVICE_IP:8080/api/sdcard/shots
sleep 2
BEFORE=$(curl -s http://$DEVICE_IP:8080/api/sdcard/shots | python3 -c "import sys,json; shots=json.load(sys.stdin); print([s['name'] for s in shots if s.get('is_last')][0])")
curl -s -o /tmp/before.jpg "http://$DEVICE_IP:8080/api/sdcard/shots/$BEFORE"

# ... 执行测试 ...

# 播放后截图
curl -s -X POST http://$DEVICE_IP:8080/api/sdcard/shots
sleep 2
AFTER=$(curl -s http://$DEVICE_IP:8080/api/sdcard/shots | python3 -c "import sys,json; shots=json.load(sys.stdin); print([s['name'] for s in shots if s.get('is_last')][0])")
curl -s -o /tmp/after.jpg "http://$DEVICE_IP:8080/api/sdcard/shots/$AFTER"
```

## 注意事项

1. **唤醒词音色**：英文唤醒词 "Jarvis" 用英文音色（en-US-AriaNeural）效果更好
2. **音量控制**：播放音量要适中，太小听不到，太大可能有杂音
3. **环境噪音**：测试环境尽量安静，避免背景噪音干扰
4. **等待时间**：完整对话通常需要 10-30 秒，根据网络和 LLM 速度调整
5. **Token 过期**：后端 Token 可能快速过期，每次测试前重新获取
6. **设备 IP 变化**：设备重启后 IP 可能变化，通过路由器或扫描确认
7. **STT 语言匹配**：TTS 合成的语言应与 STT 模型语言一致

## 相关文件索引

| 功能 | 文件路径 |
|------|---------|
| TTS 测试接口 | `xiaozhi-server/.../config/TtsTestController.java` |
| 设备 HTTP API | `xiaozhi-esp32/.trae/skills/esp32-http-api/SKILL.md` |
| E2E 调试手册 | `xiaozhi-esp32/.trae/skills/xiaozhi-e2e-debug/SKILL.md` |
| JARVIS 设备调试 | `xiaozhi-esp32/.trae/skills/jarvis-device-debug/SKILL.md` |
| 交互计划文档 | `xiaozhi-esp32/.trae/documents/jarvis_interaction_plan.md` |
