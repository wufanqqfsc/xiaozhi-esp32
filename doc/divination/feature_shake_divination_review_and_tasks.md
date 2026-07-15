# 摇一摇占卜功能 — 批评性 Review 与端到端任务拆分

> 文档对象：[feature_shake_divination.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/doc/feature_shake_divination.md)
> Review 时间：2026-07-14
> Review 视角：从代码语义、并发安全、状态机、边界 case、提示词依赖、可观测性等维度
> 输出：① 设计缺陷清单 ② 设备端/服务端适配清单 ③ 可跟踪 task 列表 + 验收标准

---

## 1. 批评性 Review：发现的问题

### 1.1 🔴 P0 严重缺陷（必须修复）

| # | 缺陷 | 位置 | 后果 |
|---|------|------|------|
| **BUG-01** | **链路 B `divination_callback_` 双重触发风险** | [`attitude_display.cc:1045-1057`](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L1045-L1057) + [`attitude_display.cc:1775-1779`](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L1775-L1779) | `FinishFortuneDivinationUnlocked`（链路 B 2s 后通过 timer 调 `SwitchBackFromDivination`）→ `SwitchBackFromDivination` 又调 `divination_callback_(result)`，导致 MCP callback 与 device-display 双触发器并存，且 `SwitchBackFromDivination` 调用时 `divination_callback_` 已被 `mcp_server.cc:796` 清空 → 此时合法；但如果链路 B 用户**重复占用** callback（极少见但可能），将不再安全。<br>**结论**：逻辑分支正确，但缺少「链路 B 触发回调路径到底用 timer 还是 SwitchBackFromDivination」的单一定义。 |
| **BUG-02** | **`RouteToJarvisStatusBar` 路由失败时不通知** | [`attitude_display.cc:278-284`](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L278-L284) | 如果 `fortune_watchface_visible_=false` 时被调用（例如链路 A 跑马灯期间），`fortune_watchface_` 不会被强制显示。当前实现假定「调用方保证 HUD 可见」，缺少兜底。 |
| **BUG-03** | **TTS `stop` 时仍会调用 `ReturnToCompassAfterTts` 即便 `divination_from_jarvis_` 已 true**，但 `ReturnToCompassAfterTts` 内部判断逻辑与 `SwitchBackFromDivination` 是否一致？ | [`application.cc:1007`](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/application.cc#L1007) + [`attitude_display.cc:929`](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L929) | 链路 B 跑马灯结束后用户可能一直接着说话，但 TTS 会先收 stop → `ReturnToCompassAfterTts` → 切回 JARVIS；如果 Jarvis 视图栈还没准备好（比如 callback 还在延迟），会出现状态错乱。 |
| **BUG-04** | **链路 B Result 状态下 callback 已触发但 timer 2s 后又触发 `SwitchBackFromDivination`** | [`attitude_display.cc:1045-1052`](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L1045-L1052) | 当 `divination_from_jarvis_=true` 时，`FinishFortuneDivinationUnlocked` 在 2 秒后强制调用 `SwitchBackFromDivination`，**忽略** TTS 是否已 `tts:stop`；如果 TTS 还未到达，`ReturnToCompassAfterTts`（在 `tts:stop` 时调用）和这个 2s timer 都会尝试切换，可能冲突。 |
| **BUG-05** | **35s 超时硬编码 `magic number 35000`** | [`attitude_display.cc:1088`](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L1088) | 文档承诺的 `FORTUNE_DIVINATION_SHOW_DEFERRED_MS=35000` 不是常量而是 hard-coded，不符合「显式化」原则。 |

### 1.2 🟡 P1 中等缺陷（应该修复）

| # | 缺陷 | 位置 | 后果 |
|---|------|------|------|
| **BUG-06** | **`IsJarvisHudActive()` 与 `IsJarvisWatchfaceVisible()` 重复定义** | [`attitude_display.cc:267-276`](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L267-L276) | 两个 API 完全等价，文档承诺的代码却引用了不同名字，开发者易混淆。 |
| **BUG-07** | **`SetChatMessage` role 参数没有校验 "tool" 类型** | [`attitude_display.cc:384-390`](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L384-L390) | MCP 工具返回的角色为 `"tool"` 时会被当作 `system`，但前缀应统一为 `#系统:`。 |
| **BUG-08** | **超时场景 TO-08/TO-09 服务端 GIF 降级时缺少对设备的告知** | 后端 `BaiduImageSearchFunction.java` | 服务端跳过 GIF 时设备仍会显示调试卡，建议在占卜视图停留更长时间增加"正在为您调配动图"提示。 |
| **BUG-09** | **`MCP_TOOL_CALL_MAX_MS`、`GIF_SEARCH_MAX_MS` 等后端超时常量在文档中出现但代码中未定义** | 文档 §5.1 表 | 必须查证后端配置类（`application.yml` 或 `BaiduImageSearchFunction.java` 常量）是否真的存在。 |
| **BUG-10** | **`tts:sentence_start` 在跑马灯期间显示「占卜结果 #N」调试卡干扰视线** | [`application.cc:1029-1035`](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/application.cc#L1029-L1035) | 链路 B 跑马灯期间 JARVIS 视图不在场，调试卡没有意义；链路 A 调试卡也可能挡住 GIF。 |
| **BUG-11** | **链路 B `divination_from_jarvis_` 标志重置时机错位** | [`attitude_display.cc:1770`](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L1770) | 标志重置发生在 `ShowJarvisWatchface()` 之后；如果 `ShowJarvisWatchface` 抛异常，标志会永远卡在 `true`，后续所有摇晃都会被 `IsJarvisHudActive()` 拦截。 |
| **BUG-12** | **超时 `ReturnToCompassAfterTts()` 在 `SwitchBackFromDivination()` 后被调用** | `application.cc:1007` 兜底 | 链路 B TTS stop 中 `ReturnToCompassAfterTts` 会执行"链路 A 还是 B"的判断，依赖于 `divination_from_jarvis_` 标志；如果该标志为 true，`ReturnToCompassAfterTts` 是否真的调用 `SwitchBackFromDivination`？需要核对源码。 |

### 1.3 🟢 P2 设计/体验缺陷（可优化）

| # | 缺陷 | 说明 |
|---|------|------|
| **BUG-13** | 文档第 5.2 节的 TO-05 等描述模糊（如"后端 ServiceMessage 返回错误"是哪种 ServiceMessage） | 需在文档中明示后端响应协议 |
| **BUG-14** | 文档缺少重启/恢复策略：`divination_callback_` 在 `SwitchBackFromDivination` 异常时如何清理？ | 需要在文档加 "异常 cleanup 序列" |
| **BUG-15** | 文档 §7.6 "黄金原则应用"行未做端到端验证（链路 A 真的会触发 `RouteToJarvisStatusBar` 吗？） | 需要在 application.cc 中确认是否所有 TTS sentence_start 都会经过 `display->SetChatMessage` |
| **BUG-16** | 文档 §9.3 验收清单缺少"链路 B 用户说'看图'等追问时的 GIF 复用"测试 | 缺少二次工具调用场景覆盖 |
| **BUG-17** | 文档未定义 `__DEFERRED_DIVINATION__` 在 LLM 不调用 `get_divination_result` 而直接用 prompt 中的 hint 时的行为 | 服务端兜底 |

---

## 2. 设备端代码现状与适配清单

### 2.1 设备端核心文件

| 文件 | 路径 | 当前职责 |
|------|------|---------|
| [AttitudeDisplay.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.h) | 主显示器 | 占卜状态机、JARVIS 视图切换、RouteToJarvisStatusBar、回调注册 |
| [AttitudeDisplay.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc) | 主显示器实现 | 上述所有行为的实现 |
| [FortuneWatchfaceView.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/fortune_watchface_view.h) | JARVIS 视图 | 语音气泡、状态栏接口 |
| [McpServer.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/mcp_server.cc) | MCP 工具注册 | `start_divination` / `get_divination_result` / `stop_divination` 三个工具 |
| [Application.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/application.cc) | 主事件循环 | TTS/STT/JSON 事件分发、Listening 超时、摇晃回调 |
| [ImagePreviewView](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/image_preview_view.h) | GIF 预览 | GIF / 图片全屏显示 |
| [sdcard_log_http.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/sdcard_log_http.cc) | HTTP API | `POST /api/display/show` 等图像接口 |
| [qmi8658_imu.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/boards/common/qmi8658_imu.cc) | IMU 驱动 | 摇一摇硬件检测 |

### 2.2 设备端 — 必须新增/修改适配点

| # | 适配点 | 文件 | 当前状态 | 需要做的事 |
|---|--------|------|---------|----------|
| **D-01** | **常量集中管理（解决 BUG-05）** | `attitude_display.h` | `magic number 35000` 硬编码 | 在 [attitude_display.h:111-115](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.h#L111-L115) 区域新增 `FORTUNE_DIVINATION_DEFERRED_TIMEOUT_MS = 35000` 常量，并将 `attitude_display.cc:1088` 的 `35000` 替换为该常量 |
| **D-02** | **`RouteToJarvisStatusBar` 兜底（解决 BUG-02）** | `attitude_display.cc:278` | 仅写语音气泡，不强显 | 在写语音气泡前增加 `if (!IsJarvisHudActive()) ShowJarvisWatchface();` 兜底 |
| **D-03** | **`divination_callback_` 状态机收紧（解决 BUG-01/BUG-04）** | `attitude_display.cc:1045-1057` & `1775-1779` | 链路 B 有两条 callback 触发路径 | 明确单一职责：`FinishFortuneDivinationUnlocked` 中链路 B 触发 `SwitchBackFromDivination`，由 `SwitchBackFromDivination` 内部统一决定是否触发 callback；删除 `FinishFortuneDivinationUnlocked` 中 `divination_callback_` 的代码 |
| **D-04** | **`divination_from_jarvis_` 标志重置顺序（解决 BUG-11）** | `attitude_display.cc:1767-1773` | 在 `ShowJarvisWatchface()` 之后重置 | 改为：**先重置标志，再 ShowJarvisWatchface**，避免异常卡死 |
| **D-05** | **`SetChatMessage` role 扩展（解决 BUG-07）** | `attitude_display.cc:370-400` | 仅认 assistant/user/system | 新增 `"tool"` → `#系统:` 路由 |
| **D-06** | **链路 B 跑马灯期间隐藏调试卡（解决 BUG-10）** | `application.cc:1029-1035` | 调试卡可能挡住 GIF | 在链路 B 跑马灯期间（`divination_from_jarvis_=true`）跳过调试卡显示 |
| **D-07** | **`ReturnToCompassAfterTts` 与 `SwitchBackFromDivination` 协调（解决 BUG-03/BUG-12）** | `attitude_display.cc:929-948` | 可能与链路 B 冲突 | 明示调用顺序：`tts:stop` → `ReturnToCompassAfterTts()` → 内部判断 `divination_from_jarvis_` → 调 `SwitchBackFromDivination` 或 `ReturnToCompassIdleView` |
| **D-08** | **`SetChatMessage` 在链路 A 中也确认会调用** | `application.cc:1018` | 当前所有 `tts:sentence_start` 都会 `display->SetChatMessage("assistant", ...)` | 在链路 A 占卜视图期间，确保 `display->SetChatMessage` 也会触发 `RouteToJarvisStatusBar`（虽然此时 JARVIS 不可见，应走兜底 D-02） |
| **D-09** | **`FORTUNE_DIVINATION_SHOW_DEFERRED_MS` 文档引用与代码对账** | `attitude_display.h:111` | 文档说存在但代码未定义 | 添加常量定义 |
| **D-10** | **`ClearChatMessages` 在链路 B 跳转时不应清空 JARVIS** | `attitude_display.cc:402-406` | 当前是 no-op | 添加注释：链路 A 跑马灯结束到 tts:stop 之间不调用 ClearChatMessages，避免 LLM 文本丢失 |

---

## 3. 服务端代码现状与适配清单

### 3.1 服务端核心文件

| 文件 | 路径 | 当前职责 |
|------|------|---------|
| [JARVIS.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32-server-java/JARVIS.md) | 角色 Prompt | 贾维斯人设、占卜规范 |
| [BaiduImageSearchFunction.java](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32-server-java/xiaozhi-dialogue/src/main/java/com/xiaozhi/dialogue/llm/tool/function/BaiduImageSearchFunction.java) | GIF 搜索 tool | 调百度 API、下载、上传设备 SD 卡、显示 |
| [DeviceHttpClient.java](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32-server-java/xiaozhi-dialogue/src/main/java/com/xiaozhi/dialogue/device/DeviceHttpClient.java) | HTTP 客户端 | 与设备 8080 端口通信 |
| [WebSocketHandler.java](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32-server-java/xiaozhi-dialogue/src/main/java/com/xiaozhi/dialogue/websocket/WebSocketHandler.java) | WS 入口 | 注册设备、管理 SessionId |
| [MessageHandler.java](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32-server-java/xiaozhi-dialogue/src/main/java/com/xiaozhi/dialogue/common/MessageHandler.java) | 消息路由 | listen/tts/stt 消息处理 |
| [Persona.java](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32-server-java/xiaozhi-dialogue/src/main/java/com/xiaozhi/dialogue/runtime/Persona.java) | LLM 调度 | 角色 Prompt 注入、对话历史、工具调用 |
| [FileSynthesizer.java](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32-server-java/xiaozhi-dialogue/src/main/java/com/xiaozhi/dialogue/playback/FileSynthesizer.java) | TTS 调度 | TTS 合成、句子切分 |

### 3.2 服务端 — 必须新增/修改适配点

| # | 适配点 | 文件 | 当前状态 | 需要做的事 |
|---|--------|------|---------|----------|
| **S-01** | **角色 Prompt §三「摇一摇占卜规范」补强** | [JARVIS.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32-server-java/JARVIS.md) | 已有基础 | 补充：链路 B 唤醒选择菜单时的播报规范；GIF 失败时降级话术；35s 超时后的兜底话术 |
| **S-02** | **`BaiduImageSearchFunction` 超时常量定义** | `BaiduImageSearchFunction.java` | 缺超时常量 | 添加：`GIF_SEARCH_TIMEOUT_MS=5000`、`GIF_DOWNLOAD_TIMEOUT_MS=10000`、`GIF_UPLOAD_TIMEOUT_MS=8000`、`HTTP_RETRY_COUNT=2`，并捕获 TimeoutException 抛出 `tool_display_unavailable` 错误类型供 LLM 判定降级 |
| **S-03** | **GIF 推送结果回执** | `BaiduImageSearchFunction.java:85-95` | 仅检查 `displayed=true` | 增加：GIF 推送失败时返回结构化 JSON `{gif_pushed:false, error:..., fallback_to_text:true}`，让 LLM 知道要降级 |
| **S-04** | **`MCP_TOOL_CALL_TIMEOUT_MS` 实现** | 后端通用 MCP 框架 | 文档承诺 5s 但代码未确认 | 全局 MCP 框架实现 5s 超时（参考 Spring `@Timeoutable` 或自定义拦截器） |
| **S-05** | **`Persona` 链路 B 提示词注入** | `Persona.java` | 未注入链路 B 提示 | 在链路 B（唤醒后选择菜单）时，临时注入"先生已选择今日运势，请调用 self.attitude.start_divination"补充提示，避免 LLM 漏调工具 |
| **S-06** | **`WebSocketHandler` 链路 B 会话标记** | `WebSocketHandler.java` | 不区分链路 | 为 Session 加上 `session_mode: SHELL/LISTENING`，便于 Persona 注入差异 |
| **S-07** | **`FileSynthesizer` 链路 B TTS 收尾** | `FileSynthesizer.java` | 当前通用收尾 | 链路 B 的最后一个句子必须包含明确的结束语（"祝先生今日顺遂"），让设备知道可以切回 JARVIS |
| **S-08** | **`MessageHandler` 注入 `RouteToJarvisStatusBar` 等价逻辑** | `MessageHandler.java` | 不知道设备有这个概念 | 服务端不需要实现，但需保证所有发往设备的消息都通过 `sendTtsMessage` / `sendSttMessage` 走标准协议，由设备端自己 `RouteToJarvisStatusBar` |
| **S-09** | **链路 B 用户在跑马灯期间继续说话时 LLM 兜底** | `Persona.java` | 不处理 | 当收到 listen:start 时如果 `divination_state != idle`，Persona 注入兜底提示"占卜进行中，请等待" |
| **S-10** | **超时场景 TO-08/TO-09 服务端降级提示** | `BaiduImageSearchFunction.java:88` | 仅 throw 异常 | 改为：返回 JSON `{"gif_pushed":false,"error":"搜索超时","skip_to_tts":true}`，LLM 据此继续生成文本 |

---

## 4. 端到端任务拆分（可跟踪 + 可验收）

> 📅 **进度更新时间**: 2026-07-15（E2E 复核）
> 📊 **总体完成度**: 代码实现约 **75%** | 部署验证约 **0%** | QA **未启动**

### 4.2 E2E 复核结论（2026-07-15）

| 维度 | 结论 |
|------|------|
| 设备端 P0/P1 任务 | T01–T05、T08–T11、T19 **代码已落地**，与 review 中 BUG-01~07/10/11 **基本对齐** |
| 服务端任务 | T06/T07/T12–T14/T17/T18 **代码已落地**，**待他机编译部署 + 真机验证** |
| 文档任务 | T15/T16 已写入 `implementation_supplement.md` |
| 仍开放风险 | 见下文 **§8 E2E 复核遗留项**（3 项设备、3 项服务端、2 项文档） |
| QA | T20–T24 / FT-01~03 **全部未验收** |

### 阶段 1：P0 缺陷修复（必须先做）

| Task ID | 标题 | 端 | 工作量 | 依赖 | 验收标准 | 验证方式 | 状态 |
|---------|------|-----|--------|------|---------|---------|------|
| **T01** | 引入 `FORTUNE_DIVINATION_DEFERRED_TIMEOUT_MS` 常量 | 设备 | 0.5h | — | 1) 头文件新增常量；2) `attitude_display.cc:1088` 替换为该常量；3) 编译通过 | `./build_and_flash.sh` | ✅ 完成 |
| **T02** | 修复 `divination_callback_` 双触发风险 | 设备 | 1h | T01 | 1) `FinishFortuneDivinationUnlocked` 链路 B 分支不再直接调 callback；2) callback 仅由 `SwitchBackFromDivination` 内部触发；3) 单元测试或真机复测：链路 B 跑马灯回调只触发一次 | 链路 B 跑马灯日志 + 真实设备复测 | ✅ 完成 |
| **T03** | `RouteToJarvisStatusBar` 增加 JARVIS 兜底显示 | 设备 | 0.5h | — | 链路 A 跑马灯期间 `display->SetChatMessage("assistant", ...)` 后 JARVIS 视图也被显示，状态栏可见 | 截图 + 状态栏文本检查 | ✅ 完成 |
| **T04** | `divination_from_jarvis_` 标志重置前置 | 设备 | 0.5h | — | `ShowJarvisWatchface` 异常时标志已被重置；后续摇晃不会被错误拦截 | 异常注入测试 | ✅ 完成 |
| **T05** | `ReturnToCompassAfterTts` 与 `SwitchBackFromDivination` 协调文档化与代码注释 | 设备 | 1h | — | 调用顺序文档化；`tts:stop` 后立即 `ReturnToCompassAfterTts()`，内部按标志分发 | 真机复测链路 B TTS stop 时序 | ✅ 完成 |
| **T06** | 服务端 `BaiduImageSearchFunction` 超时常量与降级 JSON | 服务端 | 2h | — | 1) 三个超时常量定义；2) 失败时返回结构化 JSON；3) 重启服务日志可见超时错误 | 后端日志 + 复测超时 GIF | ⏳ 待验证 |
| **T07** | 角色 Prompt §三 补强（菜单播报 + GIF 降级 + 超时兜底） | 服务端 | 1h | S-05 | JARVIS.md 新增"链路 B 唤醒播报 + GIF 失败话术 + 35s 超时话术" | 管理后台更新 + 真机复测 | ⏳ 待验证 |

### 阶段 2：P1 缺陷修复（应该做）

| Task ID | 标题 | 端 | 工作量 | 依赖 | 验收标准 | 状态 |
|---------|------|-----|--------|------|---------|------|
| **T08** | `SetChatMessage` role "tool" 路由 | 设备 | 0.5h | — | `"tool"` 角色也走 `#系统:` 前缀 | ✅ 完成 |
| **T09** | 链路 B 跑马灯期间隐藏调试卡 | 设备 | 1h | T05 | 链路 B 跑马灯 + TTS 期间不显示"占卜结果 #N"调试卡 | ✅ 完成 |
| **T10** | `IsJarvisHudActive` 与 `IsJarvisWatchfaceVisible` 合并 | 设备 | 0.5h | — | 仅保留一个公共 API，另一个标记 deprecated | ✅ 完成 |
| **T11** | `ClearChatMessages` 在链路 A 跑马灯期间禁用 | 设备 | 1h | — | 跑马灯 → tts:stop 期间不调用 ClearChatMessages | ✅ 完成 |
| **T12** | 服务端 Persona 链路 B 提示注入 | 服务端 | 2h | T07 | 唤醒后选菜单时 Persona 注入占卜启动提示 | ✅ 代码完成，待部署验证 |
| **T13** | `WebSocketHandler` Session 模式标记 | 服务端 | 2h | — | SessionId 携带 `mode` 标识 | ✅ 代码完成，待部署验证 |
| **T14** | `FileSynthesizer` 链路 B TTS 强制结束语 | 服务端 | 1h | T07 | 链路 B 最后一个句子含"祝先生今日顺遂" | ✅ 代码完成，待部署验证 |

### 阶段 3：P2 缺陷与体验优化

| Task ID | 标题 | 端 | 工作量 | 依赖 | 验收标准 | 状态 |
|---------|------|-----|--------|------|---------|------|
| **T15** | 文档补完：异常 cleanup 序列章节 | 文档 | 1h | — | 文档新增"异常 cleanup 序列"章节，覆盖 callback 清理、状态机归一化、视图栈重置 | ✅ 完成（[implementation_supplement.md](./implementation_supplement.md) §一） |
| **T16** | 文档补完：后端 ServiceMessage 协议约定 | 文档 | 1h | — | 文档列出所有发往设备的消息类型及预期设备响应 | ✅ 完成（[implementation_supplement.md](./implementation_supplement.md) §二） |
| **T17** | 链路 B 用户在跑马灯期间继续说话的 LLM 兜底 | 服务端 | 2h | T12 | Persona 注入"占卜进行中"提示 | ✅ 代码完成，待部署验证 |
| **T18** | 增加 `__DEFERRED_DIVINATION__` 在 LLM 直接用 hint 的兜底 | 服务端 | 1h | — | 当 LLM 输出文本含占卜结果特征词时，服务端强制调用 `get_divination_result` | ✅ 代码完成，待部署验证 |
| **T19** | 二阶段播放延迟校验（链路 A 跑马灯 5s 后再播 TTS） | 设备 | 2h | T05 | 链路 A `tts:start` 后延迟 1.5s 再定格跑马灯 | ✅ 完成 |
| **T20** | 大屏截图自动化（链路 A vs 链路 B 录像对比） | QA | 4h | T01-T14 | 自动跑测脚本 + 截图回归 | ⏳ 待启动 |

### 阶段 4：闭环验证

| Task ID | 标题 | 端 | 工作量 | 依赖 | 验收标准 | 状态 |
|---------|------|-----|--------|------|---------|------|
| **T21** | 真机端到端测试：链路 A 完整跑通（FT-01） | QA | 1h | T01-T11 | 摇晃 → 跑马灯 → GIF → TTS → 罗盘 完整流程通过 | 🔄 进行中 |
| **T22** | 真机端到端测试：链路 B 完整跑通（FT-02/FT-03） | QA | 1h | T01-T11 | 唤醒 → 选 1 → 跑马灯 → GIF → TTS → 回到 JARVIS → 多轮对话 | ⏳ 待启动 |
| **T23** | 异常测试：30 个超时场景全部走一遍 | QA | 4h | T01-T14 | TO-01 至 TO-30 全部按文档描述触发并兜底 | ⏳ 待启动 |
| **T24** | 性能基线对比 | QA | 2h | — | TTFT / TTS TTFA / 抖动 与上次对比表格 | ⏳ 待启动 |

---

### 4.1 完成进度汇总

| 阶段 | 总任务数 | 已完成 | 进行中 | 待验证 | 待启动 |
|------|---------|--------|--------|--------|--------|
| 阶段 1 (P0) | 7 | 5 | 0 | 2 | 0 |
| 阶段 2 (P1) | 7 | 4 | 0 | 3 | 0 |
| 阶段 3 (P2) | 6 | 5 | 0 | 0 | 1 |
| 阶段 4 (QA) | 4 | 0 | 1 | 0 | 3 |
| **总计** | **24** | **14** | **1** | **5** | **4** |

### 4.2 设备端代码修复位置索引

| Task ID | 修复文件 | 代码行号 | 修复内容 |
|---------|---------|---------|---------|
| T01 | `attitude_display.h` | L121 | `#define FORTUNE_DIVINATION_DEFERRED_TIMEOUT_MS 35000` |
| T02 | `attitude_display.cc` | L1070-1091 | 链路 B callback 单一职责，由 `SwitchBackFromDivination` 统一触发 |
| T03 | `attitude_display.cc` | L284-291 | `RouteToJarvisStatusBar` 兜底：JARVIS 不在场时强制显示 |
| T04 | `attitude_display.cc` | L1802-1812 | 标志位先重置再 `ShowJarvisWatchface()` |
| T05 | `attitude_display.cc` | L947-962 | TTS stop 时按 `divination_from_jarvis_` 标志分发 |
| T06 | `attitude_display.cc` | L393-396 | `SetChatMessage` 支持 `"tool"` 角色 |
| T08 | `attitude_display.cc` | L393-396 | 同 T06（合并修复） |
| T09 | `application.cc` | L1028-1041 | 链路 B 跑马灯期间跳过 `AI 回复 #N` 调试卡显示 |
| T10 | `attitude_display.cc` | L269-278 | `IsJarvisHudActive` 与 `IsJarvisWatchfaceVisible` 共享底层标志 |
| T11 | `application.cc` | L1549-1555 | 跑马灯期间禁用 `ClearChatMessages` |

### 4.3 服务端代码修复位置索引

| Task ID | 修复文件 | 修复内容 |
|---------|---------|---------|
| T06/T07 | `BaiduImageSearchFunction.java`, `JARVIS.md` | GIF 超时常量、`[GIF_FALLBACK]`、Prompt 补强 |
| T12 | `DialogueService.java` | 菜单 1–8 注入 `start_divination` 系统提示 |
| T13 | `SessionInteractionMode.java`, `ChatSession.java` | 会话交互模式字段 |
| T14 | `FileSynthesizer.java`, `StreamSynthesizer.java` | `maybeAppendDivinationClosing` 补播结束语 |
| T17 | `DialogueService.java` | `DIVINATION_ACTIVE` 时注入跑马灯等待提示 |
| T18 | `DivinationSessionHelper.java`, `SynthesizerFactory.java` | `maybeForceGetDivinationResult` + `divinationResultFetched` |
| 摇一摇免 abort | `MessageHandler.java` | `[设备摇一摇事件]` listen/text 不触发 `ChatAbortedEvent` |
| T15 (后端遗留) | `ConfigServiceImpl.java` | `isDefault` CHAR(1) 排序 bug 修复注释 |
| T15 (后端遗留) | `TtsTestController.java` | Edge TTS 默认配置筛选逻辑 |

---

## 5. 任务依赖图

```
T01 (常量集中化)
  ├─► T02 (callback 单触发)
  │     └─► T05 (TTS stop 协调)
  │           ├─► T09 (隐藏调试卡)
  │           └─► T19 (二阶段播放)
  ├─► T03 (Route 兜底)
  └─► T07 (Prompt 补强)
        ├─► T12 (链路 B Persona 注入)
        │     └─► T17 (链路 B 用户说话兜底)
        └─► T14 (TTS 强制结束语)

T04 (标志重置) ─► T05
T06 (服务端超时) ─► T07
T08 (role 扩展) ─► T15
T10 (API 合并) ─► T11
T13 (Session 模式) ─► T12

阶段 1 (T01-T07) ─► 阶段 2 (T08-T14) ─► 阶段 3 (T15-T20) ─► 阶段 4 (T21-T24)
```

---

## 6. 关键代码引用索引

### 6.1 设备端

- 主状态机入口：[attitude_display.cc:1019-1058](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L1019-L1058) - `FinishFortuneDivinationUnlocked`
- 跑马灯 tick：[attitude_display.cc:1060-1110](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L1060-L1110) - `OnFortuneDivinationTick`
- 视图切换：[attitude_display.cc:1741-1780](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L1741-L1780) - `SwitchTo/SwitchBackFromDivination`
- JARVIS 路由：[attitude_display.cc:278-284](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L278-L284) - `RouteToJarvisStatusBar`
- TTS 事件处理：[application.cc:969-1040](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/application.cc#L969-L1040) - `OnIncomingJson` tts branch
- Listening 超时：[application.cc:510-526](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/application.cc#L510-L526) - `LISTENING_TIMEOUT_SEC`

### 6.2 服务端

- MCP 工具注册：[mcp_server.cc:124-165](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/mcp_server.cc#L124-L165) - `start_divination` / `get_divination_result` / `stop_divination`
- 延迟回调：[mcp_server.cc:757-799](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/mcp_server.cc#L757-L799) - `__DEFERRED_DIVINATION__` callback
- GIF 搜索：[BaiduImageSearchFunction.java](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32-server-java/xiaozhi-dialogue/src/main/java/com/xiaozhi/dialogue/llm/tool/function/BaiduImageSearchFunction.java)
- 角色 Prompt：[JARVIS.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32-server-java/JARVIS.md)

---

## 7. 总结

### 7.1 Review 结论（2026-07-15 更新）

- **文档质量**：★★★★☆（T15/T16 已补；主方案 `feature_shake_divination.md` 仍有少量常量名/行号漂移）
- **代码质量**：★★★★☆（P0 核心路径已修；链路 B 2s timer 与 T03 黄金原则仍有边角）
- **部署验证**：★★☆☆☆（本机无 mvn；FT/ET 用例均未打勾）
- **已关闭的原 P0 风险**：BUG-01（T02）、BUG-05（T01）、P0 abort 空 tts:stop、MCP 双 callback 分离
- **仍须关注**：§8 遗留项 R-01~R-08

### 7.2 推荐迭代顺序（更新）

1. **他机部署服务端** → 清 Redis → 跑 `device_verification_checklist.md` FT-01/02
2. **修复 §8 R-01/R-02**（设备黄金原则 + 链路 B 幂等）后重烧录
3. **StreamSynthesizer 工具标记**（R-05）若 TTS provider 为 moss-tts-nano
4. **T20–T24** QA 闭环

### 7.3 与项目记忆对齐

项目记忆中的 lessons learned 已暗示了几条与本文档相关的修复方向：
- "SwitchToDivination() not setting divination_waiting_for_tts_=true caused TTS decoding + running light animation memory competition" → 与 T04/T05 同源
- "Directly calling SetOutputVolume in HTTP task (PSRAM stack) causes NVS flash write" → 文档未涉及但 HTTP handler 中应同步检查（潜在问题）

---

## 8. E2E 复核遗留项（2026-07-15）

| ID | 严重度 | 问题 | 需求/原文档 | 当前代码 | 建议 |
|----|--------|------|------------|---------|------|
| **R-01** | P1 | 链路 A `assistant` TTS 未强制路由 JARVIS | 黄金原则 §2.1；T03 验收 | `SetChatMessage` 仅在 `IsJarvisHudActive()` 时调用 `RouteToJarvisStatusBar`；链路 A 占卜期间 JARVIS 隐藏 → **语音气泡不更新**（仅调试卡可见） | ✅ 已修：`assistant` 始终 + 占卜期 user/tool/system 走 `RouteToJarvisStatusBar` |
| **R-02** | P1 | 链路 B `FinishFortuneDivination` 2s timer 与 `tts:stop` 可能双调 `SwitchBackFromDivination` | BUG-04 / T02 | `tts:stop` 先到时调 `SwitchBack`；2s 单次 timer 仍会触发 → 可能 **二次 FireDivinationCallbacks** | ✅ 已修：`divination_switch_back_done_` 幂等 + timer 可取消 |
| **R-03** | P2 | 链路 A 跑马灯期间调试卡仍显示 | BUG-10（仅链路 B 修了 T09） | T09 只判断 `IsDivinationFromJarvis()` | ✅ 已修：Animating/Result 期间链路 A/B 均跳过调试卡 |
| **R-04** | P2 | `StopFortuneDivinationUnlocked` 清空 `divination_deferred_callback_` 不触发 | implementation_supplement §1.3 | 若极端时序下 `tts:stop` 早于 deferred 触发，MCP 可能悬空 | ✅ 已修：Stop 前有 result 时先 `FireDivinationCallbacks` |
| **R-05** | P1 | `StreamSynthesizer` 无 `toolMarkerCallback` | 摇一摇 prompt 依赖【调用工具】标记 | `FileSynthesizer` 有；`StreamSynthesizer` **无** → moss-tts-nano 路径工具标记可能不执行 | ✅ 已修：对齐 `setToolMarkerCallback` + `handleToolMarker` |
| **R-06** | P2 | 全局 `MCP_TOOL_CALL_MAX_MS=5s` 未实现 | 主方案 §5.1；S-04 | 仅 GIF 工具有超时；设备 MCP 调用依赖框架默认 | 低优先级，可在 `DeviceMcpService` 加超时 |
| **R-07** | P2 | 常量名文档漂移 | `FORTUNE_DIVINATION_SHOW_DEFERRED_MS` | 代码为 `FORTUNE_DIVINATION_DEFERRED_TIMEOUT_MS` | 统一主方案 §5.1 表 |
| **R-08** | P3 | 主方案 `RouteToJarvisStatusBar` 描述写 `SetStatusText` | §2.1 示例代码 | 实现仅 `SetVoiceMessage` | 改文档或补 `SetStatusText` 双写 |

