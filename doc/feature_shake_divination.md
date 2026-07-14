# 摇一摇占卜功能交互与技术实现方案

## 1. 功能概述
本项目在原有罗盘占卜功能的基础上，新增了“摇一摇”硬件交互触发方式，并深度融合了后端的 J.A.R.V.I.S. AI 助手。用户只需摇晃设备，即可触发占卜跑马灯（全新 5 图标随机高亮特效），随后贾维斯会根据占卜结果生成专属运势解读，并在屏幕上自动配以相关的 GIF 动图，最后通过 TTS 语音播报解读内容。

## 2. 交互流程
1. **触发阶段**：用户在罗盘主界面（待机状态）用力摇晃设备。
2. **本地响应**：设备检测到摇晃，立即播放提示音，并在屏幕上启动占卜跑马灯动画（5个随机图标以随机颜色闪烁）。
3. **云端请求**：设备自动打开音频通道，并在后台向服务端发送文本指令：`"用户摇了摇设备，请为我占卜今日运势"`。
4. **云端处理**：
   - 贾维斯（LLM）收到指令后，通过 MCP 协议调用设备端工具 `self.attitude.get_divination_result` 获取跑马灯最终停下的结果（如“财运”、“事业运势”）。
   - 贾维斯根据结果生成一段幽默、符合人设的运势解读文案。
   - 贾维斯调用后端的 `search_and_display_gif` 工具，搜索与运势相关的 GIF 动图（如“发财 GIF”），并推送到设备屏幕显示。
5. **端云同步与展示**：
   - 服务端开始下发 TTS 语音流，设备收到 `tts:start` 事件。
   - 设备跑马灯停止转动，定格在最终的占卜结果上。
   - 屏幕顶层叠加显示获取到的 GIF 动图，并开始播放贾维斯的语音解读。
6. **结束恢复**：语音播报完毕，设备收到 `tts:stop` 事件，自动清理 GIF 图片，隐藏占卜界面，平滑退回到罗盘主界面。

---

## 3. 详细技术实现

### 3.1 设备端 (ESP32, C++)

#### 3.1.1 IMU 驱动与摇一摇检测
- **驱动层**：新增 `Qmi8658Imu` 驱动类（`boards/common/qmi8658_imu.cc`），通过 I2C 总线与 QMI8658 陀螺仪通信，配置加速度计（±4g）和陀螺仪（±512dps），输出数据速率为 125Hz。
- **检测逻辑**：在 `esp32-s3-touch-lcd-1.85b.cc` 中创建独立的 FreeRTOS 任务 `imu_event_task`，以 50ms 间隔轮询加速度数据。
- **算法**：采用三轴加速度差分绝对值求和算法（`dx + dy + dz`）。当差值总和超过设定的阈值（`kShakeDeltaThreshold = 15000`）且距离上次触发超过冷却时间（`kShakeCooldownMs = 2000`）时，判定为摇一摇触发。

#### 3.1.2 跑马灯 UI 升级
- **多图标高亮**：修改 `AttitudeDisplay` 类，将原本的单图标高亮改为同时高亮 5 个随机图标（`FORTUNE_DIVINATION_HIGHLIGHT_COUNT = 5`）。
- **随机颜色**：新增 `RandomizeFortuneDivinationMarqueeUnlocked()` 方法，在 HSV 色彩空间内生成高饱和度、高明度的随机颜色，并映射到选中的 5 个图标上，提升视觉动感。
- **PSRAM 优化**：重构了 `FortuneWatchfaceView` 中的轨道点动画，废弃了占用大量 PSRAM 且容易导致 LVGL 卡顿的全屏 Canvas 方案，改为使用原生的 `lv_obj` 圆点对象进行坐标计算和透明度控制，大幅提升了渲染性能和系统稳定性。

#### 3.1.3 状态机与 TTS 同步
- **触发入口**：`Application::OnShakeDetected()` 作为摇一摇回调，内部包含严格的状态守卫（确保设备处于 Idle 状态且未在占卜中）。触发后调用 `StartFortuneDivination()`，打开音频通道，并通过 `SendUserPrompt()` 发送隐藏指令。
- **异步等待**：引入 `divination_waiting_for_tts_` 标志位。摇一摇触发的占卜不会在固定时间后自动结束，而是持续转动，直到收到服务端的 TTS 响应。
- **TTS 事件联动**：
  - 拦截 `tts:start` 事件：调用 `AttitudeDisplay::StopMarqueeForTts()`，强制跑马灯停在预先计算好的结果上。
  - 拦截 `tts:stop` 事件：调用 `AttitudeDisplay::ReturnToCompassAfterTts()`，清理占卜状态、隐藏 UI 并返回罗盘主界面。
- **超时兜底**：在 `OnFortuneDivinationTick` 中增加 35 秒超时检测。如果后端网络异常或 LLM 未响应，跑马灯将在 35 秒后自动停止，并弹出“占卜超时”的提示卡片，防止 UI 永久卡死。

### 3.2 服务端 (Java & LLM)

#### 3.2.1 Prompt 规则更新 (`JARVIS.md`)
在系统提示词中新增了专门的**占卜与摇一摇能力**章节，明确指示 LLM：
1. 收到“用户摇了摇设备”提示时，识别为占卜请求。
2. 必须调用 `self.attitude.get_divination_result` 工具获取本地已经生成的占卜结果。
3. 根据结果生成运势解读。
4. 必须调用 `search_and_display_gif` 工具搜索相关动图并推送到设备。
5. 最后输出语音文本。

#### 3.2.2 MCP 工具链
- **`self.attitude.get_divination_result`**：设备端注册的 MCP 工具，供 LLM 查询当前跑马灯的停留位置。
- **`search_and_display_gif`**：服务端 Java 实现的全局 Function Tool（`BaiduImageSearchFunction.java`）。负责通过百度图片 API 搜索 GIF，下载后通过 HTTP API (`/api/display/show`) 上传到设备的 SD 卡并触发全屏显示。

---

## 4. 总结
该方案巧妙地结合了端侧的硬件传感器（IMU）、UI 渲染（LVGL）与云端的大模型编排能力（MCP 工具调用、API 联动）。通过“摇一摇”这一自然交互方式，串联起了“动画反馈 -> 结果查询 -> 动图搜索 -> 语音播报”的完整多模态体验，且具备完善的异常兜底机制，保证了交互的流畅性和系统的稳定性。