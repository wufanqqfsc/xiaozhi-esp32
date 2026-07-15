# 贾维斯语音交互 E2E 流程梳理（FT-01 链路 A 摇一摇）

> 分析时间：2026-07-15 12:04
> 数据来源：设备 XZHI.LOG（3.4MB） + 后端 dialogue 日志（15MB） + 串口实时日志
> 使用技能：jarvis-e2e-auto-analyzer、esp32-http-api、xiaozhi-voice-e2e-test

## 会话基本信息

| 项 | 值 |
|----|----|
| 设备 IP | 192.168.0.152 |
| 设备 MAC | a0:f2:62:e4:3a:40 |
| WS URI | ws://192.168.0.198:8092/ws/xiaozhi/v1/ |
| OTA URL | http://192.168.0.198:8091/api/device/ota |
| 后端 WebSocket | 192.168.0.198:8092 |
| 后端 REST | 192.168.0.198:8091 |
| 设备 runtime | ~151s（启动后 2 分钟） |
| 固件版本 | P0 修复后（最新烧录） |
| 烧录时间 | 2026-07-15 12:00 左右 |
| WiFi | 已连接（SSID: Zikkoy） |

## FT-01 链路 A 摇一摇测试结论

### ⚠ **关键问题：测试尚未真正执行**

通过日志回溯，**本次烧录后没有任何摇一摇占卜事件发生**：

| 数据源 | 关键事件 | 命中 |
|--------|---------|------|
| 设备 XZHI.LOG | `OnShakeDetected` / `MarkDivinationTtsStarted` | **0 次** |
| 设备 XZHI.LOG | `StartFortuneDivination` | 0 次（AttitudeDisplay 类被实例化但未触发） |
| 后端 dialogue.log | `SHAKE_DIVINATION` / `interactionMode` | 0 次（全部 `IDLE`） |
| 后端 dialogue.log | `ToolLogger 工具调用成功` (self.attitude.*) | 0 次 |
| 设备 wake-word | `Wake word detected: Jarvis` | 0 次 |

## 实际发现的两个 P0 问题

### 🚨 P0-1：MCP 工具列表中**完全缺少** self.attitude.* 工具

**设备 XZHI.LOG 启动时实际注册的 MCP 工具**：
```
[user] self.device.get_status
[user] self.device.get_logs
[user] self.device.get_ota_url
[user] self.device.clear_nvs
[user] self.device.set_server_config
[user] self.display.show
[user] self.display.hide
```

**代码定义但未注册的工具**（字符串已编译进 ELF，但运行时不注册）：
```
self.attitude.select_fortune
self.attitude.cycle_fortune
self.attitude.start_divination
self.attitude.get_divination_result
self.attitude.stop_divination
self.attitude.search_and_display_gif
```

**根因分析**：
- [mcp_server.cc:103](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/mcp_server.cc#L103) `auto attitude_display = dynamic_cast<AttitudeDisplay*>(display);` **运行时返回 nullptr**
- 但同一 `Board::GetInstance().GetDisplay()` 在 [application.cc:36](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/application.cc#L36) `GetAttitudeDisplay()` 中 cast **成功**（有 `AttitudeDisplay: ReturnToCompassIdleView` 日志）
- mcp_server.cc 位于 `#ifdef HAVE_LVGL` 段中（line 82-240），推测 **RTTI 链接/内联边界问题**导致 dynamic_cast 失败

**影响链**：
```
设备摇晃 → OnShakeDetected 触发
  → SendUserPrompt 包含「self.attitude.get_divination_result」标记
    → LLM 解析标记，尝试调用工具
      → 设备 MCP 列表无此工具 ❌
        → LLM 收到 "tool not found" 错误
          → 占卜流程中断或超时
```

**验证证据**：
```bash
# xiaozhi.elf 中确实包含字符串
$ strings build/xiaozhi.elf | grep self.attitude
self.attitude.select_fortune
self.attitude.cycle_fortune
self.attitude.start_divination
self.attitude.get_divination_result
self.attitude.stop_divination
self.attitude.search_and_display_gif
```

### 🚨 P0-2：HTTP 服务线程在请求后立即 RST

| 测试 | 结果 |
|------|------|
| TCP `nc -z 8080` | ✅ 端口开放 |
| `POST /api/device/reboot` | ✅ 返回 `{"ok":true,"message":"Rebooting in 3 seconds..."}` |
| `GET /api/sdcard/info` | ✅ 200 OK |
| `GET /api/sdcard/logs` | ✅ 200 OK |
| `GET /` | ✅ 200 OK（Web UI） |
| `GET /api/device/status` | ❌ `Connection reset by peer` / 超时 |
| `GET /api/device/ota-url` | ❌ 超时 |
| `GET /api/device/logs` | ❌ 超时 |
| 大文件下载（xiaozhi_boot_3.log 10MB） | ❌ 超时 |

**根因分析**：
- 走 `sdcard_log_http.cc` 简单 GET 的端点正常
- 走 `application.cc` 状态查询的端点全部失败
- 推测：HTTP 处理线程在 `device/status` 路径上获取了某个锁（很可能是 `DisplayLockGuard` 类的锁）后**被永久阻塞**
- 印证 [project_memory.md](file:///Users/sfan/.trae-cn/memory/projects/-Users-sfan-Desktop-cv-github-OpenMAIC/project_memory.md) 中的记载："修复 MCP DisplayLockGuard 阻塞 → display.h:69-93"

## E2E Pipeline 16 步骤对照（FT-01 链路 A）

| # | 阶段 | 设备事件 | 后端事件 | 状态 |
|---|------|---------|---------|------|
| 1 | 用户摇晃设备 | `OnShakeDetected` | — | ❌ 未触发 |
| 2 | 播放 OGG_POPUP | 音频 OGG 播放 | — | — |
| 3 | 跑马灯启动 | `StartFortuneDivination` | — | — |
| 4 | WS 已建立 | `WebSocket connected` | `WebSocket连接建立成功` | ✅ IDLE 模式连接成功 |
| 5 | `SendStartListening` | listening 模式 | state=listening | ❌ 未触发 |
| 6 | `SendUserPrompt` | 摇一摇 prompt | receive user_prompt | ❌ 未触发 |
| 7 | LLM 处理 | — | `LLM_FIRST_TOKEN` | ❌ 未触发 |
| 8 | LLM 输出工具标记 | — | `get_divination_result` 调用 | ❌ 工具不存在 |
| 9 | 设备 MCP 返回结果 | `get_divination_result` 返回类别 | — | ❌ 工具未注册 |
| 10 | LLM 调用 GIF 工具 | — | `search_and_display_gif` | ❌ 工具不存在 |
| 11 | 设备播放 GIF | GIF 全屏 | — | — |
| 12 | LLM 输出完整解读 | — | TTS 流 | — |
| 13 | TTS 同步 | `tts:start` 收到 | `TTS_FIRST_CHUNK` | — |
| 14 | 设备播放 TTS | `TTSDecodeStart` | — | — |
| 15 | TTS 结束 | `tts:stop` | `state=stop` | — |
| 16 | 返回罗盘 | `ReturnToCompassAfterTts` | `LLM_DONE` | — |

## 关键问题诊断

### ✅ 已验证修复项（vs 上次报告）

- ✅ 固件 P0 修复已包含（`divination_tts_started_` / 双 callback / 35s 超时）
- ✅ 设备 WiFi 连接成功（SSID: Zikkoy）
- ✅ 后端服务可启动（8091 + 8092 + 8084）
- ✅ WebSocket 可建立（MCP initialize 成功）
- ✅ HTTP server 启动（端口 8080）
- ✅ OTA URL 配置正确（指向 192.168.0.198:8091）
- ✅ MCP 工具注册 7 个（self.device.* + self.display.*）

### ⚠ 当前发现的 P0 问题

| # | 问题 | 严重度 | 根因 | 修复状态 |
|---|------|--------|------|----------|
| P0-1 | self.attitude.* 工具**未注册** | 致命 | dynamic_cast 运行时返回 nullptr | ❌ 待修复 |
| P0-2 | HTTP service status 端点 RST | 高 | DisplayLockGuard 锁阻塞 | ❌ 待修复 |
| P0-3 | 摇一摇事件从未触发 | 高 | 用户未实际摇晃设备（？） | ❌ 待验证 |

## 优化建议

### 🔴 高优先级（必须先修复）

1. **修复 P0-1 attitude MCP 工具注册问题**
   - 方案 A：检查 RTTI 链接——确保 mcp_server.cc 与 attitude_display.h 编译时的 RTTI 边界一致
   - 方案 B：改用 `static_cast`（前提：确认 board 必然使用 AttitudeDisplay）
   - 方案 C：添加 `attitude_display = static_cast<AttitudeDisplay*>(display);` + `if (attitude_display)` 双保险
   - 修复位置：[mcp_server.cc:103](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/mcp_server.cc#L103)

2. **修复 P0-2 HTTP service 锁阻塞**
   - 方案 A：检查 `device/status` handler 路径上是否使用 DisplayLockGuard
   - 方案 B：将 `DisplayLockGuard` timeout 缩短至 200ms（参考 [project_memory.md](file:///Users/sfan/.trae-cn/memory/projects/-Users-sfan-Desktop-cv-github-OpenMAIC/project_memory.md) 中的 "DisplayLockGuard 30s → 2s" 修复）
   - 修复位置：[display.h:69-93](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/display.h#L69-L93)

3. **重新执行 FT-01 链路 A 实测**
   - 修复 P0-1 后重新烧录
   - 在罗盘主界面用力摇晃设备
   - 验证 `OnShakeDetected` → 工具调用 → TTS 播报 → 返回罗盘 完整链路

### 🟡 中优先级

1. **HTTP API 限流保护**：为 `/api/sdcard/logs/<large_file>` 添加 Range 支持（部分已实现但未生效）
2. **MCP 工具注册日志增强**：在 `AddCommonTools` 失败时打印具体哪个 cast 失败
3. **启动时 RTTI 自检**：在 Application::Start() 早期添加 self-attitude-display 探测 log

## 修复效果对比

| 指标 | 上次（v2 文档） | 本次（本次烧录） | 改善 |
|------|----------------|----------------|------|
| 设备 HTTP 8080 | ✅ 正常 | ⚠ RST (仅 status 路径) | 退步 |
| self.attitude.* 工具 | ❌ 缺失 | ❌ 缺失 | 无改善 |
| WiFi 自动重连 | ✅ NVS 持久化 | ✅ 正常 | 一致 |
| WebSocket 自动重连 | ✅ | ✅ | 一致 |
| 后端 MCP 握手 | ✅ | ✅ | 一致 |
| 摇一摇事件触发 | ⚠ 未记录 | ❌ 未记录 | — |

## 待解决问题

| # | 问题 | 优先级 | 状态 | 下一步 |
|---|------|--------|------|--------|
| 1 | self.attitude.* MCP 工具未注册 | P0 | 待修复 | 修 dynamic_cast + 重新烧录 |
| 2 | HTTP service `/api/device/*` 全部 RST | P0 | 待修复 | 修 DisplayLockGuard 超时 |
| 3 | 摇一摇事件无实际触发记录 | P0 | 待验证 | 修复后用户亲自摇晃测试 |
| 4 | `MarkDivinationTtsStarted` 无事件 | P1 | 待验证 | 取决于 #1 修复 |
| 5 | 多轮对话 shake 后空闲 | P1 | 待验证 | 取决于 #1 修复 |

## 关键文件索引（本次分析涉及）

### 设备端
| 文件 | 关键行 | 说明 |
|------|--------|------|
| [main/mcp_server.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/mcp_server.cc) | L82-240 | `#ifdef HAVE_LVGL` 段，attitude 工具注册 |
| [main/mcp_server.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/mcp_server.cc) | L103 | `dynamic_cast<AttitudeDisplay*>(display)` 关键 cast |
| [main/application.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/application.cc) | L36 | `GetAttitudeDisplay()` 对比 cast 成功 |
| [main/application.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/application.cc) | L1370-1415 | `OnShakeDetected` 完整流程 |
| [main/boards/waveshare/esp32-s3-touch-lcd-1.85b/esp32-s3-touch-lcd-1.85b.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/boards/waveshare/esp32-s3-touch-lcd-1.85b/esp32-s3-touch-lcd-1.85b.cc) | L353 | `display_ = new AttitudeDisplay(...)` 实际创建 |
| [main/display/attitude_display.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.h) | L184 | `class AttitudeDisplay : public SpiLcdDisplay` |
| [main/display/display.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/display.h) | L69-93 | `DisplayLockGuard` 定义（HTTP RST 嫌疑） |

### 文档
| 文档 | 说明 |
|------|------|
| [doc/divination/device_verification_checklist.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/doc/divination/device_verification_checklist.md) | FT-01 ~ FT-04 验证清单 |
| [.trae/skills/jarvis-e2e-auto-analyzer/SKILL.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/.trae/skills/jarvis-e2e-auto-analyzer/SKILL.md) | 本次分析所用技能 |
| [project_memory.md](file:///Users/sfan/.trae-cn/memory/projects/-Users-sfan-Desktop-cv-github-OpenMAIC/project_memory.md) | 历史经验记录 |

## 分析结论

**FT-01 链路 A 摇一摇测试当前**：
- ❌ **不可用** —— 因 self.attitude.* MCP 工具未注册，即使摇晃设备也无法完成完整链路
- 🔧 **需要先修复 P0-1**（attitude 工具注册）才能开始真机测试
- ⚠ **HTTP service 状态查询阻塞**（P0-2）会影响后续 debug 工具链

**建议执行顺序**：
1. 修复 P0-1（attitude MCP 工具注册）
2. 修复 P0-2（HTTP service 锁阻塞）
3. 重新编译烧录（`./build_and_flash.sh all`）
4. 用户亲自执行 FT-01 链路 A 真机测试
5. 验证完整链路后**才能**更新 [device_verification_checklist.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/doc/divination/device_verification_checklist.md) 中 FT-01 步骤 1-6 的状态
