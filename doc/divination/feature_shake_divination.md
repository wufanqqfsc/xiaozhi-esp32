# 摇一摇占卜功能交互与技术实现方案

## 1. 功能概述

本项目在原有罗盘占卜功能的基础上，新增了「摇一摇」硬件交互触发方式，并深度融合了后端的 J.A.R.V.I.S. AI 助手。当前设备提供**两条相互独立的占卜触发链路**，共享同一套后端占卜流水线：

| 触发链路 | 入口 | 设备状态前置条件 |
|---------|------|-----------------|
| **链路 A：摇一摇触发** | IMU 检测到摇晃 | `kDeviceStateIdle` + JARVIS 视图不可见 + 无占卜动画 |
| **链路 B：语音唤醒 → JARVIS 菜单 → 占卜** | 唤醒词 → 选择数字 1（今日运势） | 任何状态，唤醒后自动切 JARVIS 视图 |

两条链路最终都执行相同的「跑马灯动画 → MCP 工具调用 → TTS 语音播报」流程，**唯一差异**在于：
- 链路 A：跑马灯结束后直接 `ReturnToCompassAfterTts()` 回到罗盘主界面
- 链路 B：跑马灯结束后 `SwitchBackFromDivination()` 回到 **JARVIS 视图**继续进行后续 TTS 播报

> 💡 链路 B 解决了「用户唤醒 → 选占卜 → 跑马灯期间丢失语音显示」的体验断裂问题，让 TTS 解读继续在 JARVIS 视图的语音气泡中滚动显示。

---

## 2. ⚡ 核心设计原则

### 2.1 黄金原则：服务端 AI 内容强制路由 JARVIS 视图

> **凡事服务端 AI（贾维斯）返回的内容信息，必须路由到 JARVIS 页面的语音气泡（Voice Message）+ 状态栏（Status Bar）进行显示和语音播报。**

**实现位置**：[`attitude_display.cc:386-401`](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L386-L401) `RouteToJarvisStatusBar()`

**实现原理**：

```cpp
void AttitudeDisplay::RouteToJarvisStatusBar(const std::string& text) {
    DisplayLockGuard lock(this);
    if (!fortune_watchface_visible_) {
        // ⚠️ 关键：当前非 JARVIS 视图时，先确保 JARVIS 视图在场
        ShowJarvisWatchface();
    }
    fortune_watchface_->SetChatMessage(text);   // 写入语音气泡
    fortune_watchface_->SetStatusText(text);    // 写入状态栏
}
```

**三套前缀协议**（`attitude_display.cc:402-419`）：

| 前缀 | 含义 | 渲染样式 | 来源 |
|------|------|---------|------|
| `#AI:` | 贾维斯发言 | 蓝色渐变文字 + 全屏播报动画 + TTS 同步播放 | TTS 音频流解析 / MCP 工具返回 |
| `#你:` | 用户发言 | 金色描边 + 静态显示，无 TTS | STT 识别结果回显 |
| `#系统:` | 系统通知 | 灰色 + 一次性 toast 5s 后消失 | 状态变更 / 错误提示 |

**强制路由清单**（任何 AI 返回内容都必须经过 `RouteToJarvisStatusBar`）：

| 内容来源 | 文本前缀 | 路由点 | 备注 |
|---------|---------|--------|------|
| TTS `sentence_start.text` | `#AI: <text>` | JARVIS 语音气泡 | [`application.cc:888`](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/application.cc#L888) |
| STT 识别结果回显 | `#你: <text>` | JARVIS 状态栏 | `application.cc:1174` |
| MCP `get_divination_result` 返回 | `#AI: <text>` | JARVIS 语音气泡 | `mcp_server.cc:790` |
| MCP `start_divination` 调用 | `#系统: 正在为您占卜...` | JARVIS 状态栏 | `mcp_server.cc:130` |
| 唤醒后菜单播报 | `#AI: <text>` | JARVIS 语音气泡 | `application.cc:1414` |
| 长时间无响应兜底 | `#系统: <text>` | JARVIS 状态栏（toast） | `application.cc:524` |

### 2.2 黄金原则：超时显式化、可量化、可恢复

所有跨设备 / 跨服务调用必须配置**显式超时**，且每个超时必须有：
- ✅ 明确超时时间（毫秒）
- ✅ 触发后的兜底 UI 反馈
- ✅ 用户可见的提示音 / 状态栏 / 调试卡
- ✅ 状态机强制收敛路径（不允许「永远等待」）

### 2.3 黄金原则：UI 状态机与状态栏解耦

跑马灯动画、占卜视图、JARVIS 视图三者之间的切换由 `ViewStack` 严格管理；状态栏内容（`SetStatusText` / `SetChatMessage`）由独立的 `RouteToJarvisStatusBar` 控制，**不**随视图切换而清空，保证 AI 输出始终可见。

---

## 3. 关键 UI 组件全景

定义于 [fortune_watchface_view.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/fortune_watchface_view.h)：

| 组件 | 类型 | LVGL 对象 | 作用 |
|------|------|-----------|------|
| **outer_ring_canvas_** | 圆环 | `lv_canvas` | 外环（WiFi 时青色、其他金色） |
| **taiji_gold_ring_canvas_** | 圆环 | `lv_canvas` | 太极金色环 |
| **status_bar_label_** | 标签 | `lv_label` | 状态栏文字（`SetStatusText`） |
| **voice_message_label_** | 标签（多行） | `lv_label` | 语音气泡（`SetChatMessage`） |
| **tts_waveform_canvas_** | 圆弧波纹 | `lv_canvas` | TTS 播放时的圆弧动态指示器 |
| **debug_title_label_** | 标签 | `lv_label` | 调试卡标题 |
| **debug_message_label_** | 标签（多行） | `lv_label` | 调试卡内容 |
| **fortune_marquee_objects_[8]** | 8 个图标 | `lv_obj` | 跑马灯轨道图标（外圈 + 8 个卦象） |
| **fortune_marquee_highlight_canvas_[8]** | 高亮覆盖层 | `lv_canvas` | 跑马灯高亮遮罩 |
| **fortune_marquee_color_[8]** | 颜色 | RGB | 跑马灯随机高亮颜色 |
| **fortune_feature_category_canvas_** | 卡片 | `lv_canvas` | 占卜结果卡片（财运 / 事业等） |
| **preview_image_canvas_** | 全屏画布 | `lv_canvas` | GIF / 图片预览层（`SetPreviewGif`） |
| **compass_canvas_** | 罗盘 | `lv_canvas` | 罗盘主界面盘面 |
| **fisheye_canvas_** | 鱼眼 | `lv_canvas` | 鱼眼 WiFi / BLE 状态指示 |

### 3.1 路由前缀到组件映射表

| 前缀 | 写入组件 | 触发组件 | 视觉反馈 |
|------|---------|---------|---------|
| `#AI: <text>` | `SetChatMessage(text)` + `SetStatusText(text)` | `tts_waveform_canvas_`（TTS 期间） | 蓝渐变文字 + 圆弧波纹 + TTS 播放 |
| `#你: <text>` | `SetStatusText(text)` | 无 | 金色描边，无波纹 |
| `#系统: <text>` | `SetStatusText(text)` | `ShowDebugInfo`（HIGH/CRITICAL 等级） | 灰色 + 5s 后自动清空 |
| 其他（兼容） | `SetChatMessage(text)` | — | 默认样式 |

### 3.2 UI 组件生命周期与视图切换

| 视图 | 显示组件 | 隐藏组件 | 渲染优先级 |
|------|---------|---------|----------|
| **罗盘主界面** | `compass_canvas_`, `outer_ring_canvas_`, `fisheye_canvas_`, `status_bar_label_` | `fortune_watchface_*`, `fortune_marquee_*`, `preview_image_canvas_` | 0 |
| **JARVIS 视图** | `outer_ring_canvas_`, `voice_message_label_`, `status_bar_label_`, `tts_waveform_canvas_`, `fisheye_canvas_` | `compass_canvas_`, `fortune_marquee_*`, `preview_image_canvas_`, `debug_*` | 1 |
| **占卜视图** | `fortune_marquee_objects_[8]`, `fortune_marquee_highlight_canvas_[8]`, `fortune_feature_category_canvas_` | `compass_canvas_`, `voice_message_label_`（暂时） | 2 |
| **预览图层** | `preview_image_canvas_`（GIF / 图片） | 始终叠加在最顶层 | 3 |

> ⚠️ **关键约束**：状态栏（`status_bar_label_`）和鱼眼（`fisheye_canvas_`）**永远显示**，不参与视图切换，确保 WiFi / 音量 / 系统提示永远可见。

---

## 4. 交互流程（含完整 UI 组件 + 超时处理）

### 4.1 链路 A：摇一摇触发

| 步骤 | 用户动作 | 服务端事件 | 设备端 UI 状态 | 设备端状态栏 | 设备端音效 | 超时配置 |
|------|---------|-----------|---------------|------------|-----------|---------|
| **A1** | 在罗盘界面用力摇晃 | — | 罗盘主界面 + IMU 数值刷新 | `#系统: 检测到摇晃`（如启用） | `OGG_POPUP` 提示音 | 冷却 `kShakeCooldownMs=2000` |
| **A2** | — | — | 切到占卜视图：跑马灯启动（5 图标随机高亮） | `SetStatusText("正在占卜...")` | 第 1 声节拍音 + 节拍音每 8745ms | 跑马灯最长 30s（`FORTUNE_DIVINATION_DURATION_MS`） |
| **A3** | — | `OpenAudioChannel` + `SendStartListening(ManualStop)` | `DeviceState: Idle → Listening` | `#系统: 聆听中...` | — | — |
| **A4** | — | 发送 `SendUserPrompt("[设备摇一摇事件]...")` | 同 A3 | 同 A3 | — | — |
| **A5** | — | LLM 收到指令，调用 `get_divination_result` 工具 | 跑马灯继续转动 | `#系统: 等待占卜结果...` | 节拍音持续 | LLM TTFT < 3000ms（警告） |
| **A6** | — | MCP 返回结果 `result=3`（事业运势） | 同 A5 | `#AI: 罗盘已收到您的摇晃指令，正在为您占卜` | — | — |
| **A7** | — | LLM 调用 `search_and_display_gif("事业")` | 同 A5 | `#系统: 搜索动图中...` | — | GIF 搜索 5s 兜底 |
| **A8** | — | BaiduImageSearch 推送 GIF 到设备 | `preview_image_canvas_` 全屏显示 GIF | `#系统: 显示 GIF 动图` | — | GIF 加载 10s 兜底 |
| **A9** | — | TTS `start` 事件 | `FinishFortuneDivination(result=3)` + 播放 SUCCESS 音 + 显示功能卡片 | `#AI: <TTS sentence 1>` | `OGG_SUCCESS` | TTS TTFA < 2000ms（首句） |
| **A10** | — | TTS 句子流 | `tts_waveform_canvas_` 圆弧波动 + Opus 解码 + I2S 播放 | `#AI: <TTS sentence N>`（持续累加） | TTS 音频流 | TTS 总时长 < 10s（警告） |
| **A11** | — | TTS `stop` 事件 | `ReturnToCompassAfterTts()` → 隐藏占卜视图 + 清理 GIF + 显示罗盘 | 状态栏保留最后一句 TTS 文本 | — | — |

### 4.2 链路 B：语音唤醒 → JARVIS 菜单 → 占卜

| 步骤 | 用户动作 | 服务端事件 | 设备端 UI 状态 | 设备端状态栏 | 设备端音效 | 超时配置 |
|------|---------|-----------|---------------|------------|-----------|---------|
| **B1** | 说唤醒词「贾维斯」 | — | AFE 检测 | `#系统: 检测到唤醒词` | `OGG_POPUP` 提示音 | — |
| **B2** | — | — | `HandleWakeWordDetectedEvent` → `ShowJarvisWatchface()` 隐藏罗盘 + 显示 JARVIS 视图 | `#系统: 唤醒成功` | — | — |
| **B3** | — | LLM 播报菜单：发送 `tts:start` | `fortune_watchface_->Show()` 加载语音气泡 + 外环 + 鱼眼 | `#AI: 先生，贾维斯随时为您效劳。请选择您需要的服务：1.今日运势 ...` | TTS 音频流 | TTS TTFA < 2000ms |
| **B4** | — | LLM 输出菜单文本 | 语音气泡开始滚动显示 | `#AI: ...1.今日运势 2.爱情...` | 同上 | — |
| **B5** | 用户说"1" | STT 识别为"一"或"1" | JARVIS 视图保持，`DeviceState: Idle → Listening` | `#你: 1` | — | Listening 30s（`LISTENING_TIMEOUT_SEC=30`） |
| **B6** | — | LLM 确认选择 + 调用 `start_divination` MCP 工具 | `attitude->SwitchToDivination()`：隐藏 JARVIS 视图 + 显示占卜视图 | `#AI: 好的，先生，为您测算今日运势` + `#系统: 启动占卜流程` | TTS 短确认 | MCP 调用 5s 兜底 |
| **B7** | — | — | `StartFortuneDivination()`：state = Animating + 5 图标高亮 | `#系统: 跑马灯转动中` | 第 1 声节拍音 | 跑马灯 30s（`FORTUNE_DIVINATION_DURATION_MS`） |
| **B8** | — | LLM 调用 `get_divination_result` | 跑马灯继续 | `#系统: 等待占卜结果...` | 节拍音持续 | LLM TTFT < 3000ms |
| **B9** | — | MCP 返回 `__DEFERRED_DIVINATION__`（跑马灯未结束） | 同 B8 | 同 B8 | 同 B8 | `__DEFERRED_DIVINATION__` 35s 兜底（与跑马灯一致） |
| **B10** | — | 注册 `divination_callback_` 等待 | 同 B8 | 同 B8 | 同 B8 | 同上 |
| **B11** | — | `FinishFortuneDivinationUnlocked(result=3)` 触发回调 | 跑马灯停止 + 卡片显示 + 触发回调发送 MCP 响应 | `#系统: 占卜完成，播放解读` | `OGG_SUCCESS` | — |
| **B12** | — | LLM 收到结果 → 调用 `search_and_display_gif` + 生成运势文案 | `preview_image_canvas_` 显示 GIF | `#AI: 先生，本次罗盘占卜完整结果如下` | — | GIF 5s + TTS TTFA < 2s |
| **B13** | — | TTS `start` 事件 | `StopMarqueeForTts()`（幂等，state 已为 Result） | `#AI: <完整运势第一句>` | TTS 音频流 | 同 A9 |
| **B14** | — | TTS 句子流 | `tts_waveform_canvas_` 圆弧波动 + Opus 解码 | `#AI: ...综合→财运→桃花→爱情` | 同上 | 同 A10 |
| **B15** | — | TTS `stop` 事件 | `ReturnToCompassAfterTts()` → 检测 `divination_from_jarvis_==true` → `SwitchBackFromDivination()` → **重新显示 JARVIS 视图** | 状态栏保留最后 TTS 文本 | — | — |
| **B16** | 用户继续追问 | 下一轮对话 | JARVIS 视图保持显示，可继续对话 | `#AI: ...` | — | Listening 30s |

### 4.3 关键 UI 状态码变更点

| 触发事件 | UI 状态变更 | 关键函数 | 代码位置 |
|---------|-----------|---------|---------|
| 唤醒词检测 | `kDeviceStateIdle → kDeviceStateConnecting → Speaking` | `SetDeviceState` | `application.cc:1421` |
| 显示 JARVIS 视图 | 罗盘隐藏 + JARVIS 显示 | `ShowJarvisWatchface` | [`attitude_display.cc:1627`](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L1627) |
| 收到 listen start | `kDeviceStateSpeaking → kDeviceStateListening` | `SetDeviceState` | `application.cc:910` |
| TTS start 事件 | `kDeviceStateListening → kDeviceStateSpeaking` | `SetDeviceState` | `application.cc:932` |
| TTS stop 事件 | `kDeviceStateSpeaking → kDeviceStateListening` | `SetDeviceState` | `application.cc:947` |
| 跑马灯启动 | 占卜视图入场 | `StartFortuneDivination` | [`attitude_display.cc:1000`](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L1000) |
| TTS start 拦截 | `state: Animating → Result`（不变更视图） | `StopMarqueeForTts` | [`attitude_display.cc:925`](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L925) |
| TTS stop 触发链路 A | 罗盘重新显示 | `ReturnToCompassAfterTts` | [`attitude_display.cc:929`](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L929) |
| TTS stop 触发链路 B | JARVIS 视图重新显示 | `SwitchBackFromDivination` | [`attitude_display.cc:1761`](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L1761) |
| Listening 30s 超时 | `kDeviceStateListening → kDeviceStateIdle` | `SetDeviceState(kDeviceStateIdle)` | `application.cc:523` |

---

## 5. 全量超时场景矩阵

### 5.1 超时常量速查

| 常量 | 值 | 位置 | 作用 |
|------|------|------|------|
| `LISTENING_TIMEOUT_SEC` | 30s | `application.cc:510` | Listening 状态超时 |
| `FORTUNE_DIVINATION_DURATION_MS` | 30000ms | `attitude_display.h:112` | 跑马灯最大持续 |
| `FORTUNE_DIVINATION_RELEASE_FINISH_MS` | 5000ms | `attitude_display.h:113` | 长按松开后延时 |
| `FORTUNE_DIVINATION_TICK_MS` | 25ms | `attitude_display.h:114` | 跑马灯刷新频率 |
| `FORTUNE_DIVINATION_SOUND_INTERVAL_MS` | 8745ms | `attitude_display.h:115` | 节拍音效间隔 |
| `kShakeCooldownMs` | 2000ms | `esp32-s3-touch-lcd-1.85b.cc:612` | 摇晃冷却时间 |
| `kShakeDeltaThreshold` | 15000 | `esp32-s3-touch-lcd-1.85b.cc:617` | 摇晃加速度差分阈值 |
| `FORTUNE_DIVINATION_DEFERRED_TIMEOUT_MS` | 35000ms | `attitude_display.h:121` | 摇一摇等待 TTS 超时兜底（原稿称 `FORTUNE_DIVINATION_SHOW_DEFERRED_MS`） |
| `TTS_FIRST_CHUNK_MAX_MS` | 3000ms | 后端配置 | TTS 首块超时 |
| `MCP_TOOL_CALL_MAX_MS` | 5000ms | 后端配置 | MCP 工具调用超时 |
| `GIF_SEARCH_MAX_MS` | 5000ms | 后端配置 | GIF 搜索超时 |
| `GIF_DOWNLOAD_MAX_MS` | 10000ms | 后端配置 | GIF 下载超时 |
| `WS_RECONNECT_INTERVAL_MS` | 3000ms | `application.cc` | WS 重连间隔 |
| `WS_MAX_RECONNECT_FAILURES` | 5 | `application.cc` | WS 最大重连失败次数 |

### 5.2 超时与异常场景完整清单

| # | 场景 | 触发条件 | 兜底行为 | UI 反馈 | 状态栏反馈 | 状态收敛 |
|---|------|---------|---------|---------|----------|---------|
| **TO-01** | **链路 A 摇晃检测后 LLM 35 秒未响应** | 摇一摇触发后无 `tts:start` | `StopFortuneDivinationUnlocked` + `ShowDebugInfo(CRITICAL)` | 跑马灯强制停止 + 红色调试卡 5s | `#系统: 占卜超时，后端未响应` | 回到罗盘 + Listening → Idle |
| **TO-02** | **链路 B LLM 35 秒未调用 `start_divination`** | 唤醒后用户选了数字但 LLM 没触发 MCP | JARVIS 视图保持 + Listening 超时 | JARVIS 视图保持 + 30s 后回 Idle | `#系统: 后端无响应，请重试` | JARVIS 隐藏 → 罗盘 |
| **TO-03** | **Listening 状态 30 秒无 TTS 响应** | 用户唤醒或摇晃后无响应 | `SetDeviceState(kDeviceStateIdle)` + 唤醒词重启用 | 罗盘主界面 | `#系统: 聆听超时，自动返回` | Idle + 唤醒词重新启用 |
| **TO-04** | **`__DEFERRED_DIVINATION__` 35 秒未收到回调** | 链路 B 跑马灯超时 | 同 TO-01 + 清理 callback | 同 TO-01 | 同 TO-01 | 罗盘 |
| **TO-05** | **MCP 工具调用 5 秒未返回** | 服务端 LLM 卡死 | 后端 ServiceMessage 返回错误 | 设备端保持当前状态 | `#系统: 工具调用超时` | 状态不变 |
| **TO-06** | **TTS `tts:start` 3 秒未到达** | Edge TTS 限流 | 继续等待，超 10s 转 TO-07 | 跑马灯继续 | `#系统: TTS 等待中...` | Animating |
| **TO-07** | **TTS 完全无响应 10 秒** | 后端 STT/LLM/TTS 全链路故障 | Listening 30s 兜底触发 → 回 Idle | 跑马灯停止 + 罗盘恢复 | `#系统: 服务无响应` | 罗盘 + Idle |
| **TO-08** | **GIF 搜索 5 秒超时** | 百度图片 API 故障 | 后端降级：跳过 GIF 推送，直接 TTS | 占卜视图正常 | `#系统: 动图搜索超时，仅播报语音` | TTS 继续 |
| **TO-09** | **GIF 下载 10 秒超时** | 网络异常 | 后端放弃推送 | 占卜视图正常 | `#系统: 动图下载失败` | TTS 继续 |
| **TO-10** | **GIF 推送后设备解码失败** | GIF 文件损坏 | 设备自动隐藏 `preview_image_canvas_` | 不显示 GIF | `#系统: 动图解码失败` | TTS 继续 |
| **TO-11** | **跑马灯音效冲突** | 后台 OGG 未结束 | `StopUiSound()` 强制停止旧音 | 无 UI 变化 | 无 | 状态不变 |
| **TO-12** | **摇晃时正在聆听/说话** | IMU 误触发 | 守卫拦截：ignored, state is X | 罗盘/JARVIS 保持 | 不更新 | Listening → Speaking 流程不变 |
| **TO-13** | **JARVIS 视图在场时摇晃** | 用户重复触发 | 守卫拦截：ignored, Jarvis visible | JARVIS 保持 | 不更新 | 不进入链路 A |
| **TO-14** | **跑马灯期间再次摇晃** | IMU 重复触发 | 守卫拦截：ignored, divination busy | 占卜视图保持 | 不更新 | Animating 继续 |
| **TO-15** | **长按太极圆心已存在 Result** | 用户重复长按 | `StopFortuneDivinationUnlocked()` + 重新显示按住遮罩 | 长按遮罩覆盖 | `#系统: 重新占卜中` | Animating 重新启动 |
| **TO-16** | **WebSocket 断开** | 网络抖动 | 3 秒后自动重连（最多 5 次） | 罗盘保持 | `#系统: 网络连接中断，正在重连...` | 重连成功 → Idle |
| **TO-17** | **WebSocket 重连 5 次失败** | 网络长时间不可用 | 永久回 Idle + 鱼眼变红 | 罗盘保持 | `#系统: 网络不可用，请检查 WiFi` | Idle |
| **TO-18** | **TTS 解码失败** | Opus 数据损坏 | 跳过当前帧，继续解码 | 状态栏不更新 | `#系统: 音频解码失败` | TTS 继续 |
| **TO-19** | **设备音频通道未打开** | 摇晃后 WS 未连接 | `OpenAudioChannel` 重试 + Listening 30s 兜底 | 跑马灯 30s 后停止 | `#系统: 音频通道未就绪` | 罗盘 |
| **TO-20** | **MCP callback 重复触发** | LLM 二次调用 `get_divination_result` | 第二次直接返回结果字符串，callback 已清空 | 跑马灯保持 Result | 无 | 正常 |
| **TO-21** | **链路 B 用户在跑马灯中说话** | 唤醒后跑马灯期间用户继续说 | 进入 listening，30s 超时后回 idle | 跑马灯可能继续 | `#系统: 聆听中...` | Listening → Idle |
| **TO-22** | **链路 B 用户在跑马灯中摇晃** | IMU 重复触发 | 守卫拦截：ignored, divination busy | 占卜视图保持 | 不更新 | Animating 继续 |
| **TO-23** | **`SwitchBackFromDivination` 异常** | 视图栈不一致 | `ReturnToCompassIdleView()` 兜底 | 完全释放资源 | `#系统: 已返回主界面` | 罗盘 |
| **TO-24** | **视图栈不一致** | 多次切换状态错乱 | `view_stack_.clear() + push(Compass)` 归一化 | 罗盘显示 | 无 | 罗盘 |
| **TO-25** | **服务返回 STT 空结果** | FunASR 超时 / 音频太短 | 后端转 TO-05 处理 | 设备无变化 | `#系统: 语音识别失败，请重试` | Listening 30s 后 Idle |
| **TO-26** | **服务返回 LLM 空响应** | 后端 LLM 异常 | 同上 TO-05 | 设备无变化 | `#系统: 对话服务异常` | Listening 30s 后 Idle |
| **TO-27** | **PSRAM 不足** | 大量 GIF 缓存 | 自动清理预览层 | GIF 不显示 | `#系统: 资源不足` | 继续 |
| **TO-28** | **设备 OTA URL NVS 错误** | NVS 存了错误的 OTA 地址 | 用户通过 `POST /api/device/clear-nvs?key=ota_url` 清除 | 罗盘保持 | `#系统: 配置错误` | 重启后恢复 |

### 5.3 超时处理的统一响应模板

每个超时/异常都必须产生**四件套**反馈：

```
1. 音效（如果适用）        ：短促"嘟"音 / SUCCESS / POPUP / 无
2. 调试卡（按严重度）      ：HIGH 5s 黄色 / CRITICAL 5s 红色
3. 状态栏文字（用户可见）  ：#系统: <中文短语，控制在 12 字以内>
4. 状态机收敛路径          ：明确下一步状态（罗盘 / JARVIS / Idle）
```

实现示例（TO-01 摇一摇超时）：

```cpp
if (now - start > 35000 && divination_waiting_for_tts_) {
    ESP_LOGW(TAG, "Shake divination timeout (no TTS in 35s)");
    StopFortuneDivinationUnlocked();                          // 1. 状态收敛
    ShowDebugInfo("占卜超时", "后端未响应", DebugLevel::CRITICAL, 5000);  // 2. 调试卡
    fortune_watchface_->SetStatusText("#系统: 占卜超时");        // 3. 状态栏
    RouteToJarvisStatusBar("#系统: 占卜超时，请稍后再试");        // 4. JARVIS 路由（链路 B）
    if (divination_from_shake_) {
        ReturnToCompassIdleView();                            // 链路 A → 罗盘
    } else if (divination_from_jarvis_) {
        SwitchBackFromDivination();                           // 链路 B → JARVIS
    }
}
```

---

## 6. UI 交互状态机详解

### 6.1 触发入口与守卫

设备仅在以下**全部条件满足**时才会响应摇晃：

| 条件 | 来源 | 失败行为 |
|------|------|----------|
| 设备状态 = `kDeviceStateIdle` | [`application.cc:1368`](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/application.cc#L1368) | 打印 `ignored, state is X`，直接 return |
| JARVIS Watchface 视图不可见 | `attitude->IsJarvisWatchfaceVisible()` | 打印 `ignored, Jarvis visible` |
| 当前无占卜动画运行 | `attitude->IsFortuneDivinationBusy()` | 打印 `ignored, divination busy` |

满足条件后，触发链 `Application::OnShakeDetected()` → `attitude->StartFortuneDivination()` 同步启动 UI 跑马灯，同时 `protocol_->OpenAudioChannel()` + `protocol_->SendStartListening(kListeningModeManualStop)` 打开音频通道。

### 6.2 占卜状态机

定义于 [`attitude_display.h:153`](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.h#L153-L157)：

```cpp
enum class FortuneDivinationState {
    Idle = 0,        // 待机：罗盘主界面正常显示
    Animating = 1,   // 跑马灯转动中（5图标随机高亮）
    Result = 2,      // 跑马灯已停在最终结果上，等待后续流程
};
```

**状态转移图**：

```
┌─────────┐  StartFortuneDivination()   ┌─────────────┐
│  Idle   │ ─────────────────────────► │  Animating  │
└─────────┘                             └──────┬──────┘
     ▲                                        │
     │ ReturnToCompassAfterTts()              │ deadline 到达
     │ 或 StopFortuneDivination()             │ / TTS start 触发
     │ 或 35s 超时                            │
     │                                        ▼
     │                                  ┌──────────┐
     └────────────────────────────────  │  Result  │
                                        └────┬─────┘
                                             │ 2 秒后自动隐藏
                                             ▼
                                        (无新交互 → Idle)
```

### 6.3 跑马灯核心逻辑（`OnFortuneDivinationTick`）

每 25ms 触发一次，由 [`attitude_display.cc:1060-1110`](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L1060-L1110) 实现：

```cpp
1. DisplayLockGuard 加锁（防 LVGL 并发）
2. if (state != Animating) return                // 状态守卫
3. 当前时间是否到达 sound_next_play_ms?          // 节拍音效
   └─ 是 → PlayFortuneDivinationMarqueeSound()
4. taiji_pressed_during_anim_?                   // 长按延期逻辑
   └─ 是 → finish_deadline = max(now+5000, start+30000)
5. divination_waiting_for_tts_?                  // 摇一摇模式分支
   ├─ 是 (链路 A 触发)：
   │   - 超时检查：now - start > 35000？          ← TO-01 触发点
   │   │   └─ 是 → StopFortuneDivination() + ShowDebugInfo(CRITICAL)
   │   - deadline 到达？→ FinishFortuneDivination(result)
   ├─ 否 (链路 B 触发)：
   │   - deadline 到达？→ FinishFortuneDivination(result)
6. RandomizeFortuneDivinationMarqueeUnlocked()  // 随机化 5 个高亮位置+颜色
7. UpdateFortuneDivinationMarqueeVisual(-1)     // -1 表示重新随机显示
```

### 6.4 摇一摇完整时序图（含 UI 组件标注）

```
设备端 UI                                          服务端
─────────                                          ──────
QMI8658 IMU 检测 (50ms 轮询)
  │ dx+dy+dz > 15000
  │ cooldown 2s 已过
  ▼
imu_event_task 调用 Application::OnShakeDetected()
  │  状态守卫检查（Idle + JARVIS 不可见 + 无占卜）
  ▼
Application::Schedule() 投递到主线程
  │
  ├──► audio_service_.PlaySound(OGG_POPUP)          ← UI: 音效
  ├──► attitude->StartFortuneDivination()           ← UI: 切换到占卜视图
  │       └─► StartFortuneDivinationUnlocked()
  │             ├─ state = Animating
  │             ├─ result = esp_random() % 12       ← 预生成结果
  │             ├─ lv_timer_create (25ms 周期)
  │             ├─ RandomizeFortuneDivinationMarqueeUnlocked()  ← UI: 5图标随机高亮+随机颜色
  │             └─ PlayFortuneDivinationMarqueeSound()          ← UI: 节拍音效
  │
  ├──► protocol_->OpenAudioChannel()                ← UI: 状态栏"#系统: 开启音频通道"
  ├──► protocol_->SendStartListening(ManualStop)    ← DeviceState: Idle → Listening
  ├──► protocol_->SendUserPrompt("[设备摇一摇事件]...")  ← 发送隐藏指令
  └──► attitude->SetDivinationWaitingForTts(true)   ← 标记等待 TTS
         attitude->SetDivinationFromShake(true)
                                                                 ┌──────────────────┐
                                                                 │  收到 UserPrompt   │
                                                                 │  LLM 第一步:      │
                                                                 │  【调用工具】       │
                                                                 │  get_divination_   │
                                                                 │  result            │
                                                                 └──────┬───────────┘
                                                                        │ 返回 result=3
                                                                        │ (事业运势)
                                                                        ▼
                                                                 LLM 第二步:
                                                                 【调用工具】
                                                                 search_and_display_gif
                                                                 {"query":"事业",...}
                                                                        │
                                                                        ▼
                                                                 BaiduImageSearch → 下发 GIF
                                                                                                                ┌──────────────────┐
                                                                                                                │ POST /api/display│
                                                                                                                │ /show (GIF 文件)  │
                                                                                                                └────────┬─────────┘
                                                                                                                         ▼
设备端收到 MCP 工具响应                                                                  SetPreviewGif(gif_url)
  └─► 继续等待 TTS 音频流                                                                 ├─► preview_image_canvas_ 全屏 GIF 显示
                                                                        │                          └─► 状态栏"#系统: 动图已推送"
                                                                        ▼
LLM 第三步: 输出完整运势文本 (TTS_FIRST_CHUNK)
  │
  ▼
设备端收到 tts:start ─────► AttitudeDisplay::StopMarqueeForTts()
                              └─► FinishFortuneDivinationUnlocked(result=3)
                                    ├─ state = Result                ← UI: 跑马灯停止
                                    ├─ StopUiSound() + PlaySound(OGG_SUCCESS)  ← UI: SUCCESS 音效
                                    ├─ UpdateFortuneDivinationMarqueeVisual(3)  ← UI: 定格在 result=3
                                    ├─ ShowFortuneFeatureCategoryUnlocked(3)    ← UI: 显示"事业运势"卡片
                                    └─ RouteToJarvisStatusBar("#AI: ...")        ← ★ 黄金原则: 路由到 JARVIS

  ▼ 设备端 OnIncomingAudio 解码 Opus + I2S 播放语音
  ▼ OnIncomingJson tts:sentence_start.text → RouteToJarvisStatusBar("#AI: ...")  ← ★ 持续路由
  ▼
TTS 句子播报中（综合运势→财运→桃花运→爱情感情）
  ▼ 状态栏持续更新（按 #AI: 前缀解析）
  ▼ tts_waveform_canvas_ 圆弧波动

  ▼ 设备端收到 tts:stop ─────► AttitudeDisplay::ReturnToCompassAfterTts()
                                  ├─ 检测 divination_from_shake_ == true
                                  ├─ StopFortuneDivinationUnlocked()
                                  │    ├─ 清空跑马灯高亮+颜色
                                  │    ├─ 重置所有状态标志位
                                  │    └─ 删除 lv_timer
                                  ├─ HideImagePreview() / ClearPreviewGif()    ← UI: 清理 GIF
                                  ├─ ReturnToCompassIdleView()                 ← UI: 罗盘恢复
                                  └─ 释放 fortune_watchface_visible_ = false
```

---

## 7. 链路 B：JARVIS 菜单触发占卜的状态机详解

链路 A（摇一摇）使用 IMU 中断直达设备，链路 B（语音唤醒 → JARVIS 菜单）则通过服务端 MCP 工具链回环触发设备，状态切换更复杂，需要明确视图栈与回调链路。

### 7.1 关键 API 入口

| API | 位置 | 作用 |
|-----|------|------|
| `HandleWakeWordDetectedEvent()` | [`application.cc:1414`](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/application.cc#L1414) | 唤醒词入口，触发 `ShowJarvisWatchface()` |
| `ShowJarvisWatchface()` | [`attitude_display.cc:1627`](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L1627) | 显示 JARVIS 视图（含语音气泡 + 外环） |
| `RouteToJarvisStatusBar()` | [`attitude_display.cc:386`](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L386) | ★ 黄金原则入口 |
| `MCP self.attitude.start_divination` | [`mcp_server.cc:124`](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/mcp_server.cc#L124) | LLM 工具调用入口 |
| `SwitchToDivination()` | [`attitude_display.cc:1741`](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L1741) | 隐藏 JARVIS → 启动跑马灯 |
| `MCP self.attitude.get_divination_result` | [`mcp_server.cc:132`](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/mcp_server.cc#L132) | LLM 取结果（支持延迟回调） |
| `SetDivinationCallback()` | [`attitude_display.cc:1782`](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L1782) | 注册跑马灯结束回调 |
| `__DEFERRED_DIVINATION__` 标记 | [`mcp_server.cc:760`](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/mcp_server.cc#L760) | 通知 LLM 工具结果需要等回调 |
| `SwitchBackFromDivination()` | [`attitude_display.cc:1761`](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L1761) | TTS stop 后从占卜视图切回 JARVIS |
| `ReturnToCompassAfterTts()` | [`attitude_display.cc:929`](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L929) | TTS stop 兜底：根据 `divination_from_shake_` / `divination_from_jarvis_` 决定去向 |

### 7.2 视图栈（ViewStack）转移

链路 B 涉及四个视图状态切换，由 `ViewStack` 维护：

```
[初始] Compass (罗盘主界面)
   │ 用户唤醒
   ▼
JarvisWatchface (JARVIS 视图)
   │ LLM 调用 start_divination → SwitchToDivination
   ▼
Divination (占卜视图，跑马灯中)
   │ 跑马灯结束 + TTS 播报完毕 → SwitchBackFromDivination
   ▼
JarvisWatchface (回到 JARVIS，继续对话)
   │ 用户说"谢谢" / 静默超时
   ▼
[结束] 回到 Compass 或保持 JARVIS
```

### 7.3 SwitchToDivination 决策逻辑

`SwitchToDivination()` 实现位于 [`attitude_display.cc:1741-1759`](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L1741-L1759)：

```cpp
void AttitudeDisplay::SwitchToDivination() {
    DisplayLockGuard lock(this);

    if (fortune_watchface_visible_) {
        // 链路 B：从 JARVIS 视图进入占卜
        divination_from_jarvis_ = true;       // ★ 关键标志，决定 TTS stop 后切回哪里
        HideJarvisWatchface();                // 隐藏 JARVIS 视图
        view_stack_.push(ActiveView::Divination);
    } else {
        // 链路 A：从罗盘直接进入占卜（与摇一摇流程一致）
        divination_from_jarvis_ = false;
        view_stack_.clear();
        view_stack_.push(ActiveView::Compass);
        view_stack_.push(ActiveView::Divination);
    }

    StartFortuneDivination();                 // 启动跑马灯
}
```

### 7.4 延迟回调机制（__DEFERRED_DIVINATION__）

链路 B 解决了一个关键问题：**LLM 调用工具时跑马灯还在转动**。

LLM 通过 MCP 调用 `get_divination_result` 时，设备可能仍在 Animating 状态。如果直接返回「还在转」会让 LLM 反复重试，浪费 token。

解决方案（[`mcp_server.cc:757-799`](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/mcp_server.cc#L757-L799)）：

```cpp
// 1. 工具调用入口
if (call_result.find("__DEFERRED_DIVINATION__") != std::string::npos) {
    // 2. 注册回调（不等当前 call_result 返回）
    attitude_display->SetDivinationCallback([this, id, attitude_display](int result) {
        // 3. 跑马灯结束后由 FinishFortuneDivinationUnlocked 触发此回调
        std::string result_text = "占卜结果：%d - %s";
        ReplyResultDeferred(id, result_str);  // 4. 延迟发送 MCP 响应
        attitude_display->SetDivinationCallback(nullptr);  // 清空避免重复触发
    });
    return;  // 5. 当前不调用 ReplyResult，等回调
}
```

回调触发链：

```
OnFortuneDivinationTick (25ms)
  └─► FinishFortuneDivinationUnlocked(result_index)  [state=Animating → Result]
        ├─► if (divination_from_jarvis_)
        │     └─► lv_timer_create 2 秒后 SwitchBackFromDivination()
        └─► if (divination_callback_ != nullptr)
              └─► divination_callback_(result_index)  ─┐
                                                      │
                                                      ▼
                                          mcp_server 延迟回调
                                          ├─► 构建 MCP 响应 JSON
                                          └─► ReplyResultDeferred(id, json_str)
                                                      │
                                                      ▼
                                              LLM 收到占卜结果
                                              ├─► 调用 search_and_display_gif
                                              └─► 输出完整运势文本 (TTS)
```

### 7.5 链路 B 完整时序图（含 UI 组件标注）

```
设备端 UI 组件                                       服务端
─────────────────                                   ──────
AFE 检测到唤醒词 "Jarvis"
  │
  ▼
HandleWakeWordDetectedEvent()
  ├─► ShowJarvisWatchface()                 ─► 罗盘隐藏，JARVIS 视图显示
  │       ├─ voice_message_label_ 创建
  │       ├─ status_bar_label_ 创建
  │       └─ tts_waveform_canvas_ 创建
  ├─► ShowDebugInfo("唤醒成功", "Jarvis")
  ├─► audio_service_.PlaySound(OGG_POPUP)   ← UI: 唤醒提示音
  └─► ContinueWakeWordInvoke("Jarvis")
       └─► OpenAudioChannel + SendStartListening
                                                                 ┌─────────────────┐
                                                                 │ 收到 hello/listen│
                                                                 │ 贾维斯播报菜单:   │
                                                                 │ "先生，贾维斯随时 │
                                                                 │  为您效劳..."     │
                                                                 └────────┬────────┘
                                                                          │ 用户说"1"
                                                                          ▼
                                                                  STT: "一"
                                                                          │
                                                                          ▼
                                                                  LLM: "好的，先生，为您测算今日运势"
                                                                          │
                                                                          ▼
                                                                  MCP: self.attitude.start_divination
                                                                          │
                                                                          ▼
设备端收到 MCP 工具调用                                  ─────────►  attitude_display->SwitchToDivination()
                                                                          │
  ├─► fortune_watchface_visible_ = true                   ─►       divination_from_jarvis_ = true
  ├─► HideJarvisWatchface()                                ─►       JARVIS 视图隐藏（voice_message_label_ 隐藏）
  └─► StartFortuneDivination()                             ─►       state = Animating
       ├─► state = Animating
       ├─► result = esp_random() % 12
       ├─► 5 个 fortune_marquee_objects_ 随机高亮           ← UI: 跑马灯启动
       └─► lv_timer_create (25ms)                          ← UI: 刷新调度
                                                                          │
                                                                          ▼
                                                                  LLM 第二步：调用 get_divination_result
                                                                              │
                                                                              ▼
设备端收到 MCP 工具调用                                  ◄─────────  attitude_display->GetFortuneDivinationState()
                                                                          │
                                                                          ├─ state == 1 (Animating)
                                                                          │   └─► 返回 "__DEFERRED_DIVINATION__"
                                                                          │
                                                                          ▼
                                                                  mcp_server 检测到 __DEFERRED_DIVINATION__
                                                                          │
                                                                          └─► SetDivinationCallback(lambda)
                                                                              │
                                                                              │ (等待跑马灯结束...)
                                                                              │
                                                                              ▼
设备端 OnFortuneDivinationTick                            ─►       FinishFortuneDivinationUnlocked(result=3)
                                                                          ├─ state = Result
                                                                          ├─ 播放 OGG_SUCCESS 音效
                                                                          ├─ 5 个图标停止在 result=3 上
                                                                          ├─ ShowFortuneFeatureCategoryUnlocked(3) ← UI: 显示"事业运势"卡片
                                                                          └─ divination_callback_(3) 触发
                                                                                       │
                                                                                       ▼
                                                                          mcp_server 回调 ReplyResultDeferred
                                                                                       │
                                                                                       ▼
                                                                  LLM 收到 result=3 ("事业运势")
                                                                              │
                                                                              ▼
                                                                  LLM 第三步：
                                                                  ├─► 调用 search_and_display_gif {"query":"事业"}
                                                                  └─► 输出 TTS 文本
                                                                              │
                                                                              ▼
                                                                  TTS_FIRST_CHUNK 推送音频流
                                                                              │
                                                                              ▼
设备端收到 tts:start                                       ◄───────  OnIncomingJson
  └─► StopMarqueeForTts()                                 ─►       FinishFortuneDivinationUnlocked (幂等)
       └─► state = Result (已为 Result，跳过)
       └─► tts_waveform_canvas_ 开始圆弧波动                  ← UI: TTS 波形指示

设备端 OnIncomingAudio 解码 Opus + I2S 播放语音
设备端 tts:sentence_start.text → RouteToJarvisStatusBar("#AI: <text>")  ← ★ 黄金原则路由
       ├─► ShowJarvisWatchface() 重新显示（保险）
       ├─► voice_message_label_ 滚动显示
       └─► status_bar_label_ 更新

                                                                              │
                                                                              ▼
                                                                  TTS 句子完整播报完毕 (综合→财运→桃花→爱情)
                                                                              │
                                                                              ▼
设备端收到 tts:stop                                        ◄───────  OnIncomingJson
  └─► ReturnToCompassAfterTts()
       ├─► if (divination_from_shake_) → StopFortuneDivinationUnlocked() (链路 A)
       │      └─► 释放状态 + 罗盘主界面显示
       └─► if (divination_from_jarvis_) → SwitchBackFromDivination() (链路 B ★)
              ├─► StopFortuneDivination()
              ├─► divination_from_jarvis_ = false
              ├─► view_stack_.pop_if_top(Divination)
              └─► ShowJarvisWatchface()  ─► JARVIS 视图重新显示
                                                  │
                                                  ▼
                                          用户可继续对话：
                                          "今天还有什么要注意的？"
                                                  │
                                                  ▼
                                          多轮对话上下文保持
```

### 7.6 链路 A vs 链路 B 关键差异

| 维度 | 链路 A：摇一摇 | 链路 B：JARVIS 菜单 |
|------|---------------|-------------------|
| **触发源** | IMU 硬件中断 | 服务端 MCP 工具调用 |
| **触发前视图** | 罗盘主界面 | JARVIS 视图 |
| **是否打开音频通道** | 是（`OpenAudioChannel`） | 否（唤醒时已打开） |
| **占卜结果获取** | LLM 通过 `get_divination_result` 工具查询 | 同链路 A，但首次必为 `__DEFERRED_DIVINATION__` |
| **TTS 播放期间视图** | 罗盘主界面 + GIF 叠加 | JARVIS 视图 + GIF 叠加 |
| **TTS stop 后行为** | `ReturnToCompassAfterTts()` → 罗盘 | `SwitchBackFromDivination()` → JARVIS |
| **状态标志位** | `divination_from_shake_ = true` | `divination_from_jarvis_ = true` |
| **用户可继续对话** | 否（需重新唤醒） | 是（无需重新唤醒） |
| **超时兜底** | 35s 后停止跑马灯 | 同链路 A |
| **MCP 工具入口** | 无直接调用 | `start_divination` + `get_divination_result` |
| **黄金原则应用** | `RouteToJarvisStatusBar` 在链路 A 也会触发（保险起见显示 JARVIS） | `RouteToJarvisStatusBar` 是主路径 |

### 7.7 链路 B 异常兜底

| 异常场景 | 兜底行为 | 代码位置 |
|---------|---------|----------|
| 跑马灯 35 秒未结束 | 强制停止 + ShowDebugInfo("占卜超时") | `attitude_display.cc:1086-1094` |
| 用户在跑马灯中说出新指令 | 状态机进入 listening，30 秒超时后回 idle | `application.cc:436-451` |
| LLM 未调用 `start_divination` | 用户选择功能后 LLM 直接生成文案，不进入占卜流程 | 服务端逻辑 |
| `get_divination_result` 重复调用 | 第二次调用时 callback 已清空，工具直接返回结果字符串 | `mcp_server.cc:796` |
| TTS 期间用户摇晃设备 | 守卫拦截：ignored, Jarvis visible | `application.cc:1379` |
| GIF 推送失败 | TTS 仍然播报，占卜视图正常返回 | `search_and_display_gif` 内部 try/catch |
| `SwitchBackFromDivination` 异常 | `ReturnToCompassIdleView()` 兜底完全释放资源 | `attitude_display.cc:1676` |
| 视图栈不一致 | `view_stack_.clear()` + `push(Compass)` 归一化 | `attitude_display.cc:1724-1726` |

### 7.8 链路 B 调试要点

**日志关键字**：

```
设备端：
  "ShowJarvisWatchface: voice wake-up triggered"
  "SwitchToDivination: JARVIS hidden"
  "SwitchToDivination: divination started, current=2"  // ActiveView::Divination
  "Fortune divination finished -> 3 (事业运势)"
  "SwitchBackFromDivination: JARVIS shown, current=1"  // ActiveView::JarvisWatchface
  "RouteToJarvisStatusBar: text=#AI: <TTS sentence>"

服务端：
  "Divination result deferred, waiting for animation to complete (id=X)"
  "Deferred divination result ready: 占卜结果：3 - 事业运势 (id=X)"
```

**常见问题排查**：

| 问题 | 排查点 |
|------|--------|
| 跑马灯结束后没有回到 JARVIS | 检查 `divination_from_jarvis_` 是否在 `SwitchToDivination` 中被设置 |
| LLM 一直重试 `get_divination_result` | 检查 `__DEFERRED_DIVINATION__` 返回逻辑是否正常触发 callback |
| TTS 播放期间 JARVIS 视图消失 | 检查 `tts:start` 路径是否调用 `StopMarqueeForTts` 而非 `StopFortuneDivination` |
| 用户唤醒后摇晃无响应 | 检查 `OnShakeDetected` 中的 `IsJarvisWatchfaceVisible` 守卫（正确拒绝） |
| TTS 文字未显示在 JARVIS | 检查 `RouteToJarvisStatusBar` 是否被调用，前缀是否正确（`#AI:`） |
| TTS 文字显示但语音不播放 | 检查 `tts_waveform_canvas_` 是否启动，Opus 解码是否正常 |

---

## 8. 详细技术实现

### 8.1 设备端 (ESP32, C++)

#### 8.1.1 IMU 驱动与摇一摇检测
- **驱动层**：新增 `Qmi8658Imu` 驱动类（`boards/common/qmi8658_imu.cc`），通过 I2C 总线与 QMI8658 陀螺仪通信，配置加速度计（±4g）和陀螺仪（±512dps），输出数据速率为 125Hz。
- **检测逻辑**：在 `esp32-s3-touch-lcd-1.85b.cc` 中创建独立的 FreeRTOS 任务 `imu_event_task`，以 50ms 间隔轮询加速度数据。
- **算法**：采用三轴加速度差分绝对值求和算法（`dx + dy + dz`）。当差值总和超过设定的阈值（`kShakeDeltaThreshold = 15000`）且距离上次触发超过冷却时间（`kShakeCooldownMs = 2000`）时，判定为摇一摇触发。

#### 8.1.2 跑马灯 UI 升级
- **多图标高亮**：修改 `AttitudeDisplay` 类，将原本的单图标高亮改为同时高亮 5 个随机图标（`FORTUNE_DIVINATION_HIGHLIGHT_COUNT = 5`）。
- **随机颜色**：新增 `RandomizeFortuneDivinationMarqueeUnlocked()` 方法，在 HSV 色彩空间内生成高饱和度、高明度的随机颜色，并映射到选中的 5 个图标上，提升视觉动感。
- **PSRAM 优化**：重构了 `FortuneWatchfaceView` 中的轨道点动画，废弃了占用大量 PSRAM 且容易导致 LVGL 卡顿的全屏 Canvas 方案，改为使用原生的 `lv_obj` 圆点对象进行坐标计算和透明度控制，大幅提升了渲染性能和系统稳定性。

#### 8.1.3 状态机与 TTS 同步
- **触发入口**：`Application::OnShakeDetected()` 作为摇一摇回调，内部包含严格的状态守卫（确保设备处于 Idle 状态且未在占卜中）。触发后调用 `StartFortuneDivination()`，打开音频通道，并通过 `SendUserPrompt()` 发送隐藏指令。
- **异步等待**：引入 `divination_waiting_for_tts_` 标志位。摇一摇触发的占卜不会在固定时间后自动结束，而是持续转动，直到收到服务端的 TTS 响应。
- **TTS 事件联动**：
  - 拦截 `tts:start` 事件：调用 `AttitudeDisplay::StopMarqueeForTts()`，强制跑马灯停在预先计算好的结果上。
  - 拦截 `tts:stop` 事件：调用 `AttitudeDisplay::ReturnToCompassAfterTts()`，清理占卜状态、隐藏 UI 并返回罗盘主界面。
- **超时兜底**：在 `OnFortuneDivinationTick` 中增加 35 秒超时检测。如果后端网络异常或 LLM 未响应，跑马灯将在 35 秒后自动停止，并弹出"占卜超时"的提示卡片，防止 UI 永久卡死。
- **黄金原则实现**：`RouteToJarvisStatusBar()` 是所有 AI 输出内容的统一入口。无论链路 A 还是 B，都通过该函数将文本写入 JARVIS 视图的语音气泡和状态栏。

### 8.2 服务端 (Java & LLM)

#### 8.2.1 Prompt 规则更新（贾维斯角色 Prompt）

在贾维斯角色 Prompt 中新增了专门的**「三、摇一摇占卜规范」**章节，强制 LLM 按贾维斯人设风格输出占卜结果。核心约束如下：

| # | 强制规则 |
|---|---------|
| 1 | 全程统一称呼用户为「先生」 |
| 2 | 收到「用户摇了摇设备」指令时，**必须**立即识别为占卜请求，跳过菜单直接执行完整占卜 |
| 3 | 摇一摇占卜时，**必须**先调用 `self.attitude.get_divination_result` 工具获取跑马灯结果，再生成运势解读 |
| 4 | 摇一摇占卜结果播报前，**必须**调用 `search_and_display_gif` 工具搜索相关 GIF 动图并推送到设备屏幕 |
| 5 | 严格按「综合运势 → 财运 → 桃花运 → 爱情感情」四大板块依次解读 |
| 6 | 行文自带生活化八卦口吻，吉利调侃与少量负面吐槽穿插 |
| 7 | 结尾必须使用「祝先生今日顺遂」结束语 |
| 8 | 绝不主动暴露 AI 身份，不跳出贾维斯罗盘人设 |
| 9 | **所有语音播报文本必须简洁清晰**，避免冗长，便于设备状态栏滚动显示 |

**开场应答（固定话术）**：

> 好的，先生，罗盘已收到您的摇晃指令，正在为您占卜。

**占卜结果播报示例**：

> 先生，本次罗盘占卜完整结果如下：
>
> **一、综合运势**：今日整体运势中等偏上，上午办事顺畅，午后容易分心走神……
>
> **二、财运**：财位东南，上午 9-11 点求财气场最佳……
>
> **三、桃花运**：人际桃花活跃度拉满，逛街、通勤极易结识聊得来的人……
>
> **四、爱情感情**：单身者大胆主动聊天更容易脱单……
>
> 祝先生今日顺遂。

完整规则定义见 [JARVIS.md §三 摇一摇占卜规范](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/doc/JARVIS.md) 与 [JARVIS_PROMPT.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/doc/JARVIS_PROMPT.md)。

**提示词生效流程**：

1. 编辑 `xiaozhi-esp32-server-java/JARVIS.md` 文件，更新角色 prompt
2. 通过管理后台（`http://localhost:8084`）更新系统提示词配置
3. **清除 Redis 角色配置缓存**：`redis-cli --scan --pattern 'xiaozhi:role:*' | xargs -r redis-cli del`
4. **重启后端服务**：`cd xiaozhi-esp32-server-java && ./start.sh restart`
5. **重启对话服务**以清空 Persona/Conversation 缓存

#### 8.2.2 MCP 工具链
- **`self.attitude.start_divination`**：LLM 调用入口，触发链路 B 的视图切换（隐藏 JARVIS → 显示占卜视图）。
- **`self.attitude.get_divination_result`**：设备端注册的 MCP 工具，供 LLM 查询当前跑马灯的停留位置。支持 `__DEFERRED_DIVINATION__` 延迟回调机制。
- **`search_and_display_gif`**：服务端 Java 实现的全局 Function Tool（`BaiduImageSearchFunction.java`）。负责通过百度图片 API 搜索 GIF，下载后通过 HTTP API (`/api/display/show`) 上传到设备的 SD 卡并触发全屏显示。

---

## 9. 测试与验收

### 9.1 功能测试用例

| # | 测试场景 | 操作步骤 | 预期结果 | 通过标准 |
|---|---------|---------|---------|---------|
| FT-01 | 链路 A 正常摇一摇 | 在罗盘界面摇晃设备 | 跑马灯启动 + 30s 内 TTS 播报 + GIF 显示 | TTS 文字出现在状态栏 |
| FT-02 | 链路 B 菜单触发 | 唤醒 → 说"1" | 跑马灯启动 + TTS 播报 + TTS 结束后回到 JARVIS | JARVIS 视图保持 |
| FT-03 | 多轮对话延续 | FT-02 后继续说话 | JARVIS 视图继续显示多轮对话 | 状态栏持续更新 |
| FT-04 | 长按太极圆心占卜 | 长按 3 秒 | 触发普通占卜流程 | 显示占卜卡片 |
| FT-05 | 长按松开延期 | FT-04 中松开 | 跑马灯额外转 5 秒后停下 | 跑马灯延期 |

### 9.2 异常测试用例

| # | 测试场景 | 操作步骤 | 预期结果 |
|---|---------|---------|---------|
| ET-01 | 后端关闭后摇一摇 | 关闭后端 → 摇晃 | 跑马灯 35s 后超时，弹出 CRITICAL 调试卡 |
| ET-02 | 网络断开后唤醒 | 断网 → 唤醒词 | 3 次重连失败后鱼眼变红，状态栏提示 |
| ET-03 | GIF 搜索失败 | 后端关闭百度 API → 摇一摇 | TTS 正常播报，无 GIF |
| ET-04 | TTS 全链路故障 | Mock 后端不返回 TTS | 跑马灯 35s 后超时 |
| ET-05 | 跑马灯期间摇晃 | 摇晃启动后再摇晃 | 守卫拦截，无响应 |
| ET-06 | 唤醒后立刻摇晃 | 唤醒 → 立即摇晃 | 守卫拦截：ignored, Jarvis visible |

### 9.3 验收清单

> 📅 **进度更新时间**: 2026-07-15

#### 代码修复完成状态

| 缺陷 ID | 标题 | 状态 | 备注 |
|---------|------|------|------|
| BUG-01 | `divination_callback_` 双触发风险 | ✅ 已修复 | T02: callback 仅由 `SwitchBackFromDivination` 统一触发 |
| BUG-02 | `RouteToJarvisStatusBar` 路由失败时不通知 | ✅ 已修复 | T03: 增加 JARVIS 兜底显示 |
| BUG-03 | TTS stop 时状态分发 | ✅ 已修复 | T05: 按 `divination_from_jarvis_` 标志分发 |
| BUG-04 | 链路 B Result 状态 timer 冲突 | ✅ 已修复 | T02: 单一职责明确化 |
| BUG-05 | 35s 超时硬编码 | ✅ 已修复 | T01: 引入 `FORTUNE_DIVINATION_DEFERRED_TIMEOUT_MS` 常量 |
| BUG-06 | `IsJarvisHudActive` 与 `IsJarvisWatchfaceVisible` 重复定义 | ✅ 已修复 | T10: 两个 API 共享底层标志 |
| BUG-07 | `SetChatMessage` role 参数没有校验 "tool" 类型 | ✅ 已修复 | T06/T08: 支持 `"tool"` 角色 |
| BUG-10 | 链路 B 跑马灯期间调试卡干扰 | ✅ 已修复 | T09: 跑马灯期间隐藏调试卡 |
| BUG-11 | `divination_from_jarvis_` 标志重置时机错位 | ✅ 已修复 | T04: 先重置标志再 `ShowJarvisWatchface()` |

#### 功能验收清单

- [ ] 链路 A 完整跑通：摇晃 → 跑马灯 → GIF → TTS → 罗盘（🔄 真机测试中）
- [ ] 链路 B 完整跑通：唤醒 → 选 1 → 跑马灯 → GIF → TTS → 回到 JARVIS
- [ ] 链路 B 多轮对话可延续
- [ ] 35s 超时兜底触发红色调试卡
- [ ] Listening 30s 超时回 Idle
- [ ] 网络断开/重连正常
- [x] 黄金原则：`#AI:` 前缀文本出现在 JARVIS 语音气泡（代码已实现）
- [x] 黄金原则：`#你:` 前缀文本出现在 JARVIS 状态栏（代码已实现）
- [x] 黄金原则：`#系统:` 前缀文本出现在 JARVIS 状态栏（toast 5s）（代码已实现）
- [ ] 所有超时场景都有四件套反馈（音效 + 调试卡 + 状态栏 + 状态收敛）

#### 待验证项

- T06/T07: 服务端超时常量与 Prompt 补强（需后端确认）
- T12/T13/T14: 服务端链路 B 增强（需后端开发）
- FT-01/FT-02/FT-03: 真机端到端测试（待设备固件验证）

---

## 10. 总结

该方案巧妙地结合了端侧的硬件传感器（IMU）、UI 渲染（LVGL）与云端的大模型编排能力（MCP 工具调用、API 联动）。通过"摇一摇"与"语音唤醒 → JARVIS 菜单"两条独立的触发链路，串联起了"动画反馈 → 结果查询 → 动图搜索 → 语音播报"的完整多模态体验。

**三大黄金原则**贯穿全文：

1. **服务端 AI 内容强制路由 JARVIS 视图** —— 任何 AI 返回内容（包括 TTS 文本、MCP 结果、菜单播报）必须通过 `RouteToJarvisStatusBar` 写入 JARVIS 视图的语音气泡和状态栏，确保用户始终能在统一界面看到 AI 输出。
2. **超时显式化、可量化、可恢复** —— 28+ 种超时场景都有明确的触发条件、兜底行为、UI 反馈、状态栏文字和状态收敛路径，杜绝「永远等待」。
3. **UI 状态机与状态栏解耦** —— 视图切换（罗盘 ↔ JARVIS ↔ 占卜）由 `ViewStack` 管理，状态栏内容由独立的 `RouteToJarvisStatusBar` 控制，二者解耦保证 AI 输出始终可见。

系统具备完善的异常兜底机制（28 种超时场景）和严格的 UI 组件生命周期管理，保证了交互的流畅性和系统的稳定性。