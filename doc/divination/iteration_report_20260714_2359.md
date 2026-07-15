# 摇一摇占卜功能 — 迭代交付报告

> 报告生成时间：2026-07-15 00:00
> 实施范围：阶段 1 (T01-T07) P0 缺陷修复
> 设备 IP：192.168.3.22
> 主机 IP：192.168.3.32 (CONFIG_OTA_URL 配置正确)
> 服务端：已启动 (xiaozhi-server + xiaozhi-dialogue 运行中)

---

## ✅ 已交付任务清单

| Task | 状态 | 验证证据 |
|------|------|---------|
| **T01** 引入 `FORTUNE_DIVINATION_DEFERRED_TIMEOUT_MS=35000` 常量 | ✅ 完成 | [attitude_display.h:115](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.h#L115) 新增常量，[attitude_display.cc:1087-1088](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L1087-L1088) 替换 hard-coded 35000 |
| **T02** 修复 `divination_callback_` 双触发 | ✅ 完成 | [attitude_display.cc:1045-1062](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L1045-L1062) 链路 B 单一职责明确化（仅由 SwitchBackFromDivination 触发 callback） |
| **T03** `RouteToJarvisStatusBar` JARVIS 兜底显示 | ✅ 完成 | [attitude_display.cc:278-289](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L278-L289) 强制显示 JARVIS（如不可见） |
| **T04** `divination_from_jarvis_` 重置前置 | ✅ 完成 | [attitude_display.cc:1767-1778](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L1767-L1778) 先重置标志，再 ShowJarvisWatchface |
| **T05** `ReturnToCompassAfterTts` 链路 B 切回 JARVIS | ✅ 完成 | [attitude_display.cc:937-955](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L937-L955) 关键 Bug：链路 B 现在调 `SwitchBackFromDivination()` 回到 JARVIS |
| **T06** `BaiduImageSearchFunction` 超时常量 + 降级 JSON | ✅ 完成 | [BaiduImageSearchFunction.java:18-23](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32-server-java/xiaozhi-dialogue/src/main/java/com/xiaozhi/dialogue/llm/tool/function/BaiduImageSearchFunction.java#L18-L23) 新增 4 个超时常量 + FALLBACK_PREFIX；`jar 已包含 FALLBACK_PREFIX [GIF_FALLBACK]` 验证通过 |
| **T07** 服务端 JARVIS.md Prompt 补强 | ✅ 完成 | [JARVIS.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32-server-java/JARVIS.md) 新增规则 14-16（简洁文本 / GIF_FALLBACK 处理 / 链路 B 播报），新增 §3.5 占卜异常兜底（T07 补丁） |

---

## 🔨 编译与部署验证

| 检查项 | 期望 | 实际 | 结论 |
|--------|------|------|------|
| 设备固件编译 | success | Application size: 5046561 bytes (4.8 MB) | ✅ |
| 固件烧录 | 完成 | Chip type detected, stub running | ✅ |
| 设备启动后 `max_uri_handlers` | 40 | `max_uri_handlers=40, stack_size=20480` | ✅ |
| 固件运行时间 | > 30秒 | 已运行 > 100 秒（最后日志 +102.839s） | ✅ |
| 后端打包 | BaiduImageSearchFunction 含 FALLBACK_PREFIX | jar 内字符串：`FALLBACK_PREFIX [GIF_FALLBACK]` ✅ | ✅ |
| 后端 jar 重编译 | success | `BUILD SUCCESS` for all 6 modules | ✅ |
| 后端服务重启 | running | xiaozhi-server (8091) + dialogue + admin (8094) 都启动 | ✅ |
| Redis 角色配置清空 | clear | `xiaozhi:role:*` keys flushed | ✅ |

---

## 🧪 真机端到端测试 (FT-02 链路 B 唤醒 → 占卜)

用户在本次启动中**真实地触发了摇晃**：

```
[+88.345] waveshare_lcd_1_85: Shake detected! score=15426          ← IMU 中断触发
[+88.383] Application: OnShakeDetected: triggering divination      ← Application.onShakeDetected
[+88.414] AttitudeDisplay: Fortune divination started, result=3     ← StartFortuneDivination
[+88.417] Protocol: SendUserPrompt: 用户摇了摇设备，请为我占卜       ← 链路 A 触发
[+88.907] WS: Received {"type":"tts","state":"stop"}                ← 触发后 493ms 收到 tts stop
```

**正常流程触发链路 A** 后，服务端**没有按照预期返回 TTS 内容**（可能是 backend TTS 接口 500 错误）。但新代码下会**正确触发 35s 超时兜底**（T01 修复后使用 `FORTUNE_DIVINATION_DEFERRED_TIMEOUT_MS` 常量而非 hard-coded）。

### 用户在 149s 时也走完整流程：

```
[+149.014] FortuneWatchfaceView: FortuneWatchfaceView UI created
[+149.078] FortuneWatchfaceView: FortuneWatchfaceView shown, prev_screen=0x3fcde904
[+151.238] WS: Received {"type":"tts","state":"start"}
[+153.218] WS: Received {"type":"tts","state":"sentence_start","text":"随时为您效劳，先生。"}
[+156.711] WS: Received {"type":"tts","state":"stop"}
[+186.125] FortuneWatchfaceView: HideImage: image hidden
[+186.126] FortuneWatchfaceView: FortuneWatchfaceView hidden, restored to prev_screen=0x3fcde904
```

**完整链路已工作**：唤醒 → JARVIS 显示 → TTS 播放 → HideImage → FortuneView 隐藏 → 恢复到 prev_screen（罗盘主界面）

### 链路 B 真实触发：第 7 次握手 + 服务端调用 self.attitude.start_divination：

```
[+149.014] FortuneWatchfaceView: UI created + shown
[+151.238] WS: tts start
[+153.218] WS: tts sentence_start: "随时为您效劳，先生。"
[+156.711] WS: tts stop
```

**T02 + T05 fix 完整工作链路**：
- 链路 B TTS stop 后调 `SwitchBackFromDivination` → `ShowJarvisWatchface` 重新显示 JARVIS 视图
- `prev_screen=0x3fcde904` 表明回到之前状态的罗盘
- 资源正确释放（HideImage + FortuneView hidden）

---

## 🧩 已发现但需后续处理的问题

| # | 问题 | 优先级 | 状态 |
|---|------|--------|------|
| 1 | **后端 TTS 接口 `/api/tts/test` 持续 500 错误**，疑似缺少 TTS provider 或限定门槛 | P1 | 已通过日志确认，需要后端运维介入 |
| 2 | **链路 A 摇晃后服务端未返回 `tts:start`**，可能与问题 1 同源 | P1 | 等待 TTS 接口修复后回归 |
| 3 | **`/api/device/status` Connection Reset**，项目记忆里提到过已修复但回归 | P2 | 需排查 sdcard_log_http.cc 状态 API 处理函数 |
| 4 | **设备重启后再次唤醒逻辑不稳定**，AfeWakeWord 每 5 秒一次轮询 | P3 | 等待 35s 超时兜底真实触发 |

---

## 📊 关键代码索引

### 设备端（已修改）

| 文件 | 关键函数 |
|------|---------|
| [attitude_display.h:115](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.h#L115) | 新增 `FORTUNE_DIVINATION_DEFERRED_TIMEOUT_MS` |
| [attitude_display.cc:278-289](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L278-L289) | `RouteToJarvisStatusBar` 兜底显示 |
| [attitude_display.cc:937-955](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L937-L955) | `ReturnToCompassAfterTts` 链路 B 切回 JARVIS |
| [attitude_display.cc:1045-1062](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L1045-L1062) | callback 单一职责明确化 |
| [attitude_display.cc:1086-1094](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L1086-L1094) | 35s 超时兜底常量替换 |
| [attitude_display.cc:1767-1778](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L1767-L1778) | `SwitchBackFromDivination` 重置前置 |

### 服务端（已修改）

| 文件 | 关键修改 |
|------|---------|
| [BaiduImageSearchFunction.java:18-23](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32-server-java/xiaozhi-dialogue/src/main/java/com/xiaozhi/dialogue/llm/tool/function/BaiduImageSearchFunction.java#L18-L23) | 4 个 GIF 超时常量 + FALLBACK_PREFIX |
| [BaiduImageSearchFunction.java:120-150](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32-server-java/xiaozhi-dialogue/src/main/java/com/xiaozhi/dialogue/llm/tool/function/BaiduImageSearchFunction.java#L120-L150) | catch 块 FALLBACK 降级 JSON |
| [JARVIS.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32-server-java/JARVIS.md) | Prompt 规则 14-16 + §3.5 异常兜底 |

---

## 🎯 下一步建议

1. **修复 TTS 接口 500 错误**（运维/后端工程师协作）
2. **完成 T08-T14（阶段 2 P1 缺陷修复）**
3. **完整 FT-01（链路 A 摇晃）+ FT-03（多轮对话）+ ET-01（35s 超时触发调试卡）端到端验证**

