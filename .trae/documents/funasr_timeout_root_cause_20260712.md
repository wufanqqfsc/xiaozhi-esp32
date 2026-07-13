# 第二轮交互 FunASR 识别超时原因深度分析

> 分析时间: 2026-07-12
> 会话: `746a360c-6fee-004f-779e-685651dee896`
> 数据来源: 后端 dialogue 日志 + FunASR 服务端源码 + Docker 容器状态

---

## 一、超时现象

| 指标 | 值 |
|---|---|
| STT 开始时间 | 16:41:52.796 (`STT_RECV START`) |
| VAD 检测语音结束 | 16:41:55.486 (静音 1243ms) |
| FunASR 超时日志 | 16:43:22.800 (`FunASR识别超时`) |
| STT 返回结果 | 16:43:24.823 (回退 offlineResult) |
| STT 总耗时 | **91.984 秒** (约 92 秒) |
| 超时时间设置 | `RECOGNITION_TIMEOUT_MS = 90000` (90 秒) |
| 识别结果 | `算卦算 蒜瓜，蒜 占卜` (3 段拼接, 质量差) |

---

## 二、根本原因分析（五层）

### 第一层：直接原因 — 双方都不关闭 WebSocket 连接

**客户端（Java FunASRSttService）行为**：
```
1. 音频发送完毕 → 发送 SPEAKING_END {"is_speaking": false}
2. Thread.sleep(3000)  // 等待 3 秒
3. 发送线程结束
4. recognitionLatch.await(90000ms)  // 等待服务端关闭连接
5. ↓ 等待 90 秒 ↓
6. 超时返回 false
7. 回退读取 offlineResult
```
代码位置: [FunASRSttService.java:102-113](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32-server-java/xiaozhi-ai/src/main/java/com/xiaozhi/ai/stt/providers/FunASRSttService.java#L102-L113), [L178-190](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32-server-java/xiaozhi-ai/src/main/java/com/xiaozhi/ai/stt/providers/FunASRSttService.java#L178-L190)

**服务端（FunASR funasr_wss_server.py）行为**：
```python
async for message in websocket:           # 无限循环等待消息
    ...
    if speech_end_i != -1 or not websocket.is_speaking:
        await async_asr(websocket, ...)   # 执行离线识别
        # 识别完成后继续循环，不关闭连接
    ...
# 只有 ConnectionClosed 异常才会退出
```
代码位置: 服务端 `/workspace/FunASR/runtime/python/websocket/funasr_wss_server.py` 第 156-242 行

**结论**：客户端等服务端关，服务端等客户端关 → 死等 90 秒超时。

---

### 第二层：架构原因 — 服务端 VAD 产生多段识别，延长等待

FunASR 服务端内部有**独立的 VAD**（`iic/speech_fsmn_vad_zh-cn-16k-common-pytorch`），与后端 Java 的 Silero VAD 是两套独立系统。

**时间线 — 服务端 3 段识别结果**：

| 序号 | 时间 | 模式 | 文本 | 距 VAD END |
|---|---|---|---|---|
| 1 | 16:42:16.904 | `2pass-online` | 算卦算 | +21.4s |
| 2 | 16:42:23.430 | `2pass-offline` | 蒜瓜，蒜 | +27.9s |
| 3 | 16:42:29.812 | `2pass-offline` | 占卜 | +34.3s |
| 4 | 16:42:30.737 | `2pass-offline` | （空文本） | +35.2s |

**为什么有 3 段？**

服务端 VAD 将用户语音切成了 **3 个独立片段**，每段结束都会触发一次完整的 `async_asr` 离线识别（Paraformer-large + 标点模型）。这导致：
- 识别不是一次性完成，而是分 3 次
- 客户端无法知道"这是最后一段"，只能继续等
- 等完 3 段后，没有更多结果了，但连接也没关

---

### 第三层：性能原因 — Paraformer-large CPU 推理极慢

**服务端配置**：
- 模型: `speech_paraformer-large_asr_nat-zh-cn-16k-common-vocab8404-pytorch`（Paraformer-large）
- VAD: `speech_fsmn_vad_zh-cn-16k-common-pytorch`
- 标点: `punc_ct-transformer_zh-cn-common-vad_realtime-vocab272727`
- 硬件: CPU 4 核（`--ncpu 4`），无 GPU
- 容器: `xiaozhi-sensevoice`（注意：名称是 sensevoice，但实际跑的是 Paraformer-large）

**性能问题**：
- 用户说话约 1.5~2.7 秒
- 服务端处理了 35 秒才出完所有结果
- **实时率约 15~20x**（处理 1 秒音频需要 15-20 秒）
- 这是 Paraformer-large 在低端 CPU 上的典型表现

**为什么这么慢？**
1. Paraformer-large 参数量大（约 200M+）
2. CPU 推理，无 GPU 加速
3. 同时跑 3 个模型（在线 ASR + VAD + 离线 ASR + 标点），资源竞争
4. 服务端声明 `only support one client at the same time now!!!!`，但实际可能有并发请求排队

---

### 第四层：设计原因 — 客户端缺少"识别完成"判定逻辑

当前客户端 `onMessage` 只做了一件事：**累积识别文本到 `offlineResult`**。

**缺失的逻辑**：
- ❌ 没有判断"所有结果都返回了，可以关闭了"
- ❌ 没有超时早停（比如连续 N 秒没新结果就提前结束）
- ❌ 没有利用 `is_final` 字段判断结束
- ❌ 没有区分 `2pass-online` 和 `2pass-offline` 的语义

**`is_final` 字段的含义**（服务端代码第 287-289 行）：
```python
"is_final": websocket.is_speaking,
```
- `is_speaking=True` 时 → `is_final=True`（表示"语音还在继续，这只是中间结果"）
- `is_speaking=False` 时 → `is_final=False`（表示"语音已结束"）

⚠ 这个语义是**反直觉**的。`is_final=True` 反而不是最终结果。

客户端代码没有利用这个字段，也没有基于"空文本 + is_final=false"来判断结束。

---

### 第五层：环境原因 — 模型与容器不匹配

**现状**：
- 容器名: `xiaozhi-sensevoice`
- 实际运行: `funasr_wss_server.py` + Paraformer-large 模型
- 项目根目录下有 `sensevoice_server.py`（SenseVoiceSmall 轻量模型），但未使用

**对比**：

| 方案 | 模型 | CPU 实时率 | 准确率 |
|---|---|---|---|
| 当前 | Paraformer-large（funasr_wss_server） | 15~20x（慢） | 高 |
| 备选 | SenseVoiceSmall（sensevoice_server.py） | ~1-2x（快） | 中 |

如果使用 SenseVoiceSmall，处理 2 秒音频只需 2-4 秒，不会出现 35 秒延迟。

---

## 三、证据链

### 后端日志证据（xiaozhi-dialogue.log）

```
16:41:52.789  VAD 检测到语音开始（概率 0.9975）
16:41:52.793  startStt - audioSinks 创建 OK
16:41:52.796  STT_RECV START
16:41:52.852  FunASR WebSocket连接已打开
16:41:55.486  VAD 语音结束（静音 1243ms）

16:42:16.904  FunASR 收到: is_final=true, mode=2pass-online, text=算卦算
16:42:23.430  FunASR 收到: is_final=true, mode=2pass-offline, text=蒜瓜，蒜
16:42:29.812  FunASR 收到: is_final=true, mode=2pass-offline, text=占卜
16:42:30.737  FunASR 收到: is_final=false, mode=2pass-offline, text=（空）

16:43:22.800  WARN FunASR识别超时，等待音频发送线程结束...
16:43:24.822  INFO FunASR超时但已有识别结果: 算卦算 蒜瓜，蒜 占卜
16:43:24.823  [DEBUG] FunASR STT返回 - result: 算卦算 蒜瓜，蒜 占卜
16:43:24.834  INFO FunASR WS关闭，原因：（空）
```

### 服务端代码证据（funasr_wss_server.py）

```python
# 第 224 行：离线识别触发条件 — VAD 段结束 或 is_speaking=False
if speech_end_i != -1 or not websocket.is_speaking:
    if websocket.mode == "2pass" or websocket.mode == "offline":
        await async_asr(websocket, audio_in)  # 触发离线识别
    # 识别完成后继续循环，不关闭连接
    # ...

# 第 243 行：只有连接断开才退出
except websockets.ConnectionClosed:
    await ws_reset(websocket)

# 第 287 行：is_final 字段的真实含义
"is_final": websocket.is_speaking,  # True=还在说, False=说完了
```

### 容器状态证据

```
容器: xiaozhi-sensevoice  (Up 27 hours)
进程: python funasr_wss_server.py 
参数: --asr_model speech_paraformer-large --vad_model speech_fsmn_vad --punc_model punc_ct-transformer
      --ngpu 0 --device cpu --ncpu 4
端口: 10096→10095
```

---

## 四、影响评估

| 影响项 | 严重程度 | 说明 |
|---|---|---|
| 用户体验 | ⚠️ 严重 | 说话后等 30+ 秒才有反应，用户以为卡死了 |
| 设备端行为 | ⚠️ 严重 | 设备在 8 秒后主动断开 WS（16:43:37 Connection reset） |
| 后端僵尸流程 | ⚠️ 严重 | WS 断开后 LLM/MCP 继续跑 3 分钟，产生死循环 |
| 识别准确率 | ⚠️ 中等 | 3 段结果拼接混乱（"算卦算 蒜瓜，蒜 占卜"），影响 LLM 理解 |
| 资源占用 | 🟡 中等 | 单次 STT 占用 FunASR 90 秒，服务端单连接限制可能阻塞其他请求 |

---

## 五、修复建议（按优先级）

### P0：客户端增加主动关闭逻辑（修复 90s 超时）

**方案**：在 `FunASRSttService` 的 `onMessage` 中增加"识别完成"判定：

判定条件（满足任意一个即可主动关闭）：
1. 收到 `is_final=false` 且 `text` 为空的消息（服务端在 is_speaking=False 后的最终信号）
2. 连续 5 秒没有收到新的识别结果
3. 音频已发送完毕 + 收到至少一段 offline 结果 + 2 秒无新结果

**预期收益**：STT 耗时从 90s → 35s 左右（等所有结果返回就关）。

### P1：更换为 SenseVoiceSmall 轻量模型（修复 35s 延迟）

**方案**：改用 `sensevoice_server.py`（SenseVoiceSmall int8 量化），或把 funasr_wss_server 的模型换成小模型（如 paraformer-small）。

**预期收益**：STT 耗时从 35s → 3-5s，整体 90s → 5s 以内。

### P2：修复服务端 VAD 分段问题（修复多段结果）

**方案**：
- 方案 A: 关闭服务端 VAD，只做整段离线识别（is_speaking=False 时一次性识别所有音频）
- 方案 B: 客户端合并多段结果时做去重和语义修正
- 方案 C: 完全依赖后端 Java 的 Silero VAD，FunASR 只做离线整段识别

**预期收益**：识别结果更准确，不会出现"算卦算 蒜瓜，蒜"这种混乱拼接。

### P3：WebSocket 断开后清理会话（修复僵尸流程）

**方案**：在 `WebSocketHandler.afterConnectionClosed` 中：
- 取消正在进行的 LLM 流
- 中断所有 MCP 工具调用
- 停止 TTS 合成
- 标记 session 为已关闭

**预期收益**：避免 WS 断开后后端继续运行 3 分钟。

---

## 六、当前临时兜底机制

当前代码已有部分兜底（`FunASRSttService.java:199-210`）：

```java
String result = finalResult.get();
if (result.isEmpty()) {
    // 超时后 onClose 可能尚未触发，finalResult 为空
    // 回退检查 offlineResult 是否已有识别文本
    synchronized (offlineResult) {
        result = offlineResult.toString();
    }
    if (!result.isEmpty()) {
        log.info("FunASR超时但已有识别结果: {}", result);
    }
}
```

这个兜底保证了**即使超时，也能返回已识别的文本**，而不是返回空。这也是为什么第二轮 STT 虽然超时了 90 秒，但最终还是有结果返回给 LLM 的原因。

但兜底解决不了"慢"的问题，也解决不了"结果混乱"的问题。

---

## 七、总结

```
用户说话 2s
    ↓
服务端 VAD 切成 3 段  ←── 问题三：分段混乱
    ↓
Paraformer-large CPU 推理  ←── 问题二：性能差, 35s
    ↓
3 段结果陆续返回
    ↓
连接保持, 双方都不关  ←── 问题一：协议死锁
    ↓
等 90s 超时  ←── 问题四：缺少完成判定
    ↓
回退 offlineResult 返回
```

**核心结论**：超时不是单一原因，而是**四层问题叠加**的结果：
1. 协议层死锁（双方都不关连接）→ 90s 超时
2. 性能瓶颈（大模型 CPU 推理）→ 35s 实际处理时间
3. 分段问题（双 VAD 不一致）→ 结果拼接混乱
4. 设计缺失（无完成判定）→ 不能提前终止

最有效的修复路径是 **P1（换轻量模型）+ P0（增加主动关闭）**，可将 STT 耗时从 90s 降到 3-5s。
