# 摇一摇占卜 — 真机验证清单

> 创建时间：2026-07-15  
> 固件版本：P0 修复后（`divination_tts_started_` / 双 callback / 35s 超时四件套）  
> 设备：waveshare esp32-s3-touch-lcd-1.85b，MAC `a0:f2:62:e4:3a:40`  
> 状态图例：⏳ 待验证 · 🔄 进行中 · ✅ 通过 · ❌ 失败

---

## 1. 验证前置条件

| # | 检查项 | 预期 | 状态 |
|---|--------|------|------|
| PRE-01 | 设备已烧录最新固件 | `build_and_flash.ps1` 成功，COM9 烧录完成 | ✅ |
| PRE-02 | 设备 WiFi 已连接 | 串口出现 `Network connected`，鱼眼正常 | ⏳ |
| PRE-03 | 后端服务运行 | `xiaozhi-server` 8091 + `xiaozhi-dialogue` 8092 | ⏳ |
| PRE-04 | 设备已绑定角色 | 管理后台可见设备，角色为 JARVIS | ⏳ |
| PRE-05 | 设备 IP 可访问 | `http://<IP>:8080/api/device/status` 5s 内返回 JSON | ⏳ |
| PRE-06 | Redis 角色缓存已清 | `xiaozhi:role:*` 已 flush（Prompt 更新后） | ⏳ |

---

## 2. 功能验证（FT）

### FT-01 链路 A：摇一摇完整流程

| 步骤 | 操作 | 预期结果 | 状态 |
|------|------|----------|------|
| 1 | 在**罗盘主界面**（非 JARVIS）用力摇晃设备 | 播放 `OGG_POPUP`，跑马灯启动（5 图标随机高亮） | ⏳ |
| 2 | 观察 0–3s 内 | **不应**因 abort 的空 `tts:stop` 提前结束占卜；跑马灯持续转动 | ⏳ |
| 3 | 等待后端响应（≤30s） | 串口出现 `MarkDivinationTtsStarted`；收到 `tts:start` | ⏳ |
| 4 | TTS 播报期间 | 状态栏出现 `#AI:` 前缀文本；可选 GIF 全屏显示 | ⏳ |
| 5 | TTS 结束后 | 回到**罗盘主界面**；跑马灯/GIF 已清理 | ⏳ |
| 6 | 多轮对话 | 无需重新操作，设备处于 Idle，可再次摇晃或唤醒 | ⏳ |

**关键日志关键字**：
```
OnShakeDetected: triggering divination
MarkDivinationTtsStarted: divination TTS session active
Deferred divination result ready / get_divination_result
ReturnToCompassAfterTts: routed ... (链路 A)
```

---

### FT-02 链路 B：唤醒 → 选 1 → 占卜

| 步骤 | 操作 | 预期结果 | 状态 |
|------|------|----------|------|
| 1 | 说唤醒词「贾维斯」 | JARVIS 视图显示，菜单 TTS 播报 | ⏳ |
| 2 | 说「1」或「一」 | STT 识别，状态栏 `#你: 1` | ⏳ |
| 3 | LLM 确认后 | 调用 `start_divination`，切换到占卜视图，跑马灯启动 | ⏳ |
| 4 | 跑马灯期间 | LLM 调用 `get_divination_result` 返回 `__DEFERRED_DIVINATION__` | ⏳ |
| 5 | 跑马灯结束（约 30s 或 TTS 到达） | MCP 延迟回调触发一次；`SwitchBackFromDivination` 或 TTS 期间保持 JARVIS | ⏳ |
| 6 | TTS 完整播报 | GIF 可选显示；`#AI:` 文本在 JARVIS 气泡滚动 | ⏳ |
| 7 | TTS 结束 | **回到 JARVIS 视图**（非罗盘） | ⏳ |

**关键日志关键字**：
```
SwitchToDivination: JARVIS hidden
Divination result deferred, waiting for animation
SwitchBackFromDivination: callbacks fired
ReturnToCompassAfterTts: routed to SwitchBackFromDivination (JARVIS)
```

---

### FT-03 链路 B 多轮对话延续

| 步骤 | 操作 | 预期结果 | 状态 |
|------|------|----------|------|
| 1 | FT-02 完成后 | JARVIS 视图保持显示 | ⏳ |
| 2 | 继续说「今天还有什么要注意的」 | 无需重新唤醒，STT + TTS 正常 | ⏳ |
| 3 | 状态栏 | 持续更新 `#AI:` / `#你:` 文本 | ⏳ |

---

### FT-04 长按太极圆心占卜

| 步骤 | 操作 | 预期结果 | 状态 |
|------|------|----------|------|
| 1 | 长按太极圆心 3s | 跑马灯启动，显示占卜卡片 | ⏳ |
| 2 | 动画期间按住不松 | 跑马灯延长约 5s 后停止 | ⏳ |

---

## 3. P0 修复专项验证

| ID | 验证项 | 操作 | 预期 | 状态 |
|----|--------|------|------|------|
| **P0-V01** | abort 空 tts:stop 不提前结束 | 摇晃后 0–1s 内观察 UI | 跑马灯**不**停止；串口**无** `ReturnToCompassAfterTts`（在 `tts:start` 之前） | ⏳ |
| **P0-V02** | MCP callback 不覆盖常驻回调 | 链路 B 完整跑通后，再次摇晃触发链路 A | 第二次占卜 MCP 延迟回调仍正常（`Deferred divination result ready`） | ⏳ |
| **P0-V03** | 35s 超时四件套 | 关闭后端 → 摇晃 | 约 35s 后：提示音 + 红色调试卡 + `#系统: 占卜超时` + 回罗盘 | ⏳ |
| **P0-V04** | `SwitchBackFromDivination` result 正确 | 链路 B 跑通 | 日志 `callbacks fired, result=N`（N 为 0–11，非 -1） | ⏳ |
| **P0-V05** | T18 强制补调 get_divination_result | 部署含 T18 的服务端；观察 LLM 跳过工具直接播报运势 | 后端日志 `[T18] LLM 未调用 get_divination_result，强制补调`；设备仍收到 GIF/结果 | ⏳ |
| **P0-V06** | T14 结束语补播 | 链路 A/B 完整跑通 | TTS 末尾播报「祝先生今日顺遂。」（LLM 未含时由服务端补播） | ⏳ |

---

## 4. 异常验证（ET）

| ID | 场景 | 操作 | 预期 | 状态 |
|----|------|------|------|------|
| ET-01 | 后端关闭后摇一摇 | 停后端 → 摇晃 | 35s 超时兜底（见 P0-V03） | ⏳ |
| ET-02 | 跑马灯期间再摇晃 | 摇晃启动后再摇 | 守卫拦截，日志 `ignored, divination busy` | ⏳ |
| ET-03 | JARVIS 在场时摇晃 | 唤醒后立刻摇晃 | 守卫拦截，日志 `ignored, Jarvis visible` | ⏳ |
| ET-04 | GIF 搜索失败 | 断百度 API / 返回 `[GIF_FALLBACK]` | TTS 仍完整播报四段运势 | ⏳ |
| ET-05 | Listening 30s 超时 | 唤醒后不说话 | 30s 后回 Idle，状态栏超时提示 | ⏳ |
| ET-06 | 网络断开重连 | 断 WiFi 再恢复 | 鱼眼变红 → 重连成功回 Idle | ⏳ |

---

## 5. 黄金原则验证

| ID | 前缀 | 触发方式 | 预期显示位置 | 状态 |
|----|------|----------|--------------|------|
| GP-01 | `#AI:` | TTS sentence_start | JARVIS 语音气泡 + 状态栏 | ⏳ |
| GP-02 | `#你:` | STT 识别结果 | JARVIS 状态栏 | ⏳ |
| GP-03 | `#系统:` | 系统通知 / 超时 | JARVIS 状态栏（toast 5s） | ⏳ |
| GP-04 | 链路 A 兜底 | 摇一摇期间 TTS 到达 | `RouteToJarvisStatusBar` 强制显示 JARVIS | ⏳ |

---

## 6. 验证记录模板

每次真机测试后填写：

```markdown
### 验证记录 YYYY-MM-DD HH:mm

- 测试人：
- 固件 SHA / 编译时间：
- 设备 IP：
- 后端版本 / 是否重启：

| 用例 ID | 结果 | 备注 |
|---------|------|------|
| FT-01   | ✅/❌ |      |
| FT-02   | ✅/❌ |      |
| ...     |       |      |

问题摘要：
-
```

---

## 7. 当前阻塞项（验证前需确认）

| # | 阻塞 | 影响用例 | 处理建议 |
|---|------|----------|----------|
| 1 | 设备 IP `192.168.3.22` HTTP 超时 | PRE-05、自动化截图 | 串口或路由器查新 IP |
| 2 | 后端 TTS 历史 500（`/api/tts/test` 需鉴权） | FT-01 若 dialogue TTS 仍异常 | 查 `xiaozhi-dialogue` 日志 `TTS\|ERROR` |
| 3 | FT-01/02 尚未人工确认 | 全部 FT | 本清单逐项打勾 |
| 4 | 服务端未部署 T12–T18 | FT-02、P0-V05/V06 | 他机 `mvn package` + `./start.sh all` + 清 Redis |

**本轮服务端改动状态**（部署后生效）：

| 任务 | 文件 | 状态 |
|------|------|------|
| T12 | `DialogueService.java` | ✅ 代码完成 |
| T13 | `ChatSession.java`, `SessionInteractionMode.java` | ✅ 代码完成 |
| T14 | `FileSynthesizer.java`, `StreamSynthesizer.java` | ✅ 代码完成 |
| T17 | `DialogueService.java` | ✅ 代码完成 |
| T18 | `DivinationSessionHelper.java`, `SynthesizerFactory.java`, `PersonaFactory.java` | ✅ 代码完成 |
| T19 | `attitude_display.cc`（设备） | ✅ 已烧录 |
| 摇一摇免 abort | `MessageHandler.java` | ✅ 代码完成 |

---

## 8. 与任务文档对应关系

| 本清单 ID | 对应任务 / 文档 |
|-----------|----------------|
| FT-01 | T21，`feature_shake_divination.md` §9.1 |
| FT-02/03 | T22，`feature_shake_divination.md` §4.2 |
| P0-V01~V06 | T01–T05、T14、T18 修复验收 |
| ET-01~06 | `feature_shake_divination.md` §9.2 |
| GP-01~04 | 黄金原则 §2.1 |

---

## 9. 服务端部署清单（他机部署时执行）

部署 `xiaozhi-esp32-server-java` 后需包含以下本轮改动：

| 模块 | 新增/修改文件 | 说明 |
|------|--------------|------|
| `xiaozhi-common` | `SessionInteractionMode.java` | T13 会话模式枚举 |
| `xiaozhi-dialogue` | `ChatSession.java` | `interactionMode` + `divinationResultFetched`（T13/T18） |
| `xiaozhi-dialogue` | `DivinationSessionHelper.java` | T14/T17/T18 共用辅助 |
| `xiaozhi-dialogue` | `SynthesizerFactory.java`, `PersonaFactory.java` | T18 注入 `DeviceMcpService` |
| `xiaozhi-dialogue` | `DialogueService.java` | T12/T17 提示注入 |
| `xiaozhi-dialogue` | `MessageHandler.java` | 摇一摇跳过 abort |
| `xiaozhi-dialogue` | `FileSynthesizer.java` / `StreamSynthesizer.java` | T14 结束语 |
| `xiaozhi-dialogue` | `WebSocketHandler.java` | debug 日志输出 mode |

```bash
cd xiaozhi-esp32-server-java
./start.sh all          # 或 mvn package + 重启 dialogue
redis-cli --scan --pattern 'xiaozhi:role:*' | xargs redis-cli del
```

**部署后日志关键字**：
- `interactionMode=SHAKE_DIVINATION` — 摇一摇 prompt 已识别
- `interactionMode=DIVINATION_ACTIVE` — start_divination 已调用
- TTS 流结束后应额外合成 `祝先生今日顺遂。`
- `[T18] LLM 未调用 get_divination_result，强制补调` — T18 兜底触发

---

