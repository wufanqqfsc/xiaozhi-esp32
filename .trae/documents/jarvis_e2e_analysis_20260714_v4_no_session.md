# 贾维斯语音交互 E2E 流程梳理（v4 — 设备重启后零会话）

> 分析时间：2026-07-14 11:16 (Asia/Shanghai)
> 数据来源：
> - 后端日志：`xiaozhi-esp32-server-java/logs/xiaozhi-dialogue.log`
> - 设备日志（SD 卡）：`/sdcard/XZHI.LOG`
> - 设备实时日志：`http://192.168.0.152:8080/api/device/status`
> 使用技能：jarvis-e2e-auto-analyzer、xiaozhi-e2e-debug、esp32-http-api、xiaozhi-server-java、xiaozhi-voice-e2e-test

---

## 0. 摘要

**结论**：本次分析窗口内（2026-07-14 11:12 后端启动 ~ 11:16 当前时刻）**没有任何语音交互事件**：
- 设备刚启动 388 秒，处于 idle + WakeWord enabled 状态，未发生任何唤醒
- 后端从 11:12:21 启动后零 WebSocket 连接、零 STT/LLM/TTS 事件
- 唯一的新发现是后端启动时 Vosk STT 本地服务初始化失败（不影响生产，因为默认角色走 FunASR API）

**本报告主要价值**：基于当前快照定位"设备/后端基本盘"，不进行代码修改，下一步等用户产生新一次真实交互后再做闭环分析。

---

## 1. 会话基本信息

| 项 | 值 |
|---|---|
| SessionId | **无**（后端启动后零连接） |
| Device MAC | `a0:f2:62:e4:3a:40` |
| 设备 IP | `192.168.0.152`（**已切换**！上次 v3 = 192.168.3.22） |
| 设备 WiFi | `Zikkoy`（**已切换**！上次 v3 = HUAWEI-9YQAVW） |
| 设备 Uptime | 388 秒（约 6.5 分钟） |
| WebSocket URI | `ws://192.168.0.198:8092/ws/xiaozhi/v1/` |
| OTA URL | `http://192.168.0.198:8091/api/device/ota` |
| 设备固件 | `app_version=2.2.6`、`idf_version=v5.5.4`、`board_type=wifi` |
| 内存 | free_heap=7491072、min_free_heap=7339964（健康） |
| 后端启动时间 | 2026-07-14 11:12:21 ~ 11:12:34 |
| 后端运行进程 | xiaozhi-server pid=4865、xiaozhi-dialogue pid=4875、xiaozhi-web pid=5212 |
| 默认角色 (sys_role) | roleId=1「贾维斯」modelId=3(MiniMax-M3) / sttId=2(FunASR) / ttsId=6(volcengine doubao) |

### 1.1 本次 vs 上次 (v3) 网络环境变化

| 维度 | v3 (2026-07-13) | v4 (2026-07-14) |
|---|---|---|
| 设备 IP | 192.168.3.22 | **192.168.0.152** ⚠️ 网段切换 |
| 设备 WiFi | HUAWEI-9YQAVW (192.168.3.x) | **Zikkoy** (192.168.0.x) ⚠️ |
| 主机 IP | 192.168.3.32/33 | **192.168.0.198**（Mac 自适应） |
| NVS OTA 配置 | 使用 build-config | 使用 build-config（与主机一致）✅ |
| OTA / WS 配置对齐 | ✅ | ✅ 无需改 NVS |

---

## 2. 时间线（设备 + 后端启动，单位 s/ms）

### 2.1 设备启动序列（uptime 0 → 388s）

| 设备 [+T] | 事件 | 备注 |
|---|---|---|
| +0 | 设备上电（推断） | uptime 388s 对应 11:09 左右 |
| +9.507 | `StateMachine: State: starting -> activating` | |
| +9.693 | `ActivationTask: Skipping OTA version check for faster startup` | |
| +10.106 | `Local server OTA: http://192.168.0.198:8091/api/device/ota` | 与本机对齐 ✅ |
| +10.162 | `StateMachine: State: activating -> idle` | 进入空闲态 |
| +10.630 | `AudioService: EnableWakeWordDetection called: enable=1` | 开启唤醒检测 |
| +10.632 | `AfeWakeWord: Model 0: wn9_jarvis_tts` | 唤醒模型：Jarvis |
| +10.687 | `AFE Pipeline: VAD(WebRTC) -> WakeNet(wn9_jarvis_tts) -> AGC(WakeNet)` | AFE 链路就绪 |
| +10.693 | `AfeWakeWord: Set WakeNet threshold to 0.4: success` | 阈值 0.4 |
| +10.732 | `[MicLevel] fed 1 chunks RMS=5820.5` | 麦克风初始高 RMS（开机瞬态） |
| +20.742 | `RMS=30.2 (max=47)` | 稳态，背景噪音低 |
| ... | 持续 WakeWordFeed + WakeNet-Fetch（wakeup_state=0） | 未触发唤醒 |
| +387.068 | `count=11761 wakeup_state=0` | 仍未唤醒，**本次无任何 WakeWord 触发事件** |

### 2.2 后端启动序列（11:12:21 → 11:12:34）

| 时间 | 事件 |
|---|---|
| 11:12:21.161 | Spring Boot 启动 |
| 11:12:30.709 | Silero VAD 初始化成功（windowSize=512） |
| 11:12:31.002 | **SttServiceFactory 开始初始化 Vosk（默认 provider）** |
| 11:12:31.008 | `Vosk library loaded for macOS M-series chip` |
| 11:12:31.560 | ⚠️ **Vosk STT 初始化失败**：`dlsym(0x6e0c84f0, vosk_recognizer_set_grm): symbol not found` |
| 11:12:31.560 | `将在需要时尝试使用备选服务` |
| 11:12:31.818 | Redisson RTopic 订阅初始化（6 个频道） |
| 11:12:31.830 | KeepaliveService 启动（interval=30000ms） |
| 11:12:31.943 | `WebSocket 服务地址: ws://192.168.0.198:8092/ws/xiaozhi/v1/` |
| 11:12:34.057 | `Started DialogueApplication in 13.605 seconds` |
| 11:12:34.374 | `已发布系统全局工具元数据到 Redis，数量: 3` |
| 11:12:34 ~ 11:16 | **零 WebSocket 连接、零 STT/LLM/TTS/LATENCY 事件** |

---

## 3. 关键指标

### 3.1 LATENCY 指标（基于本次会话）

| 指标 | 数值 | 状态 |
|---|---|---|
| 唤醒 → LLM_FIRST_TOKEN | **N/A** | 零会话 |
| 唤醒 → TTS_FIRST_CHUNK | **N/A** | 零会话 |
| VAD END → STT 返回 | **N/A** | 零会话 |
| MCP 工具调用耗时 | **N/A** | 零会话 |
| WebSocket 异常断开 | 0 | ✅ |
| 后端启动耗时 | 13.605s | ✅ 正常 |
| 设备启动到 idle | 10.162s | ✅ 正常 |
| 设备 WakeWord 阈值 | 0.4 | 默认值 |

### 3.2 资源指标

| 资源 | 当前值 | 评价 |
|---|---|---|
| 设备 free_heap | 7,491,072 bytes (≈7.1MB) | ✅ 充足 |
| 设备 min_free_heap | 7,339,964 bytes (≈7.0MB) | ✅ 无泄漏迹象 |
| 设备 uptime | 388s | 短 |
| 麦克风 RMS（稳态） | 23-35 / max=87 | ✅ 安静环境 |
| WakeNet 唤醒状态 | wakeup_state=0 | ✅ 待命中 |

---

## 4. 关键问题诊断

### 4.1 [P2] Vosk STT 本地服务初始化失败（macOS arm64 符号缺失）

**现象**：
```
11:12:31.560 WARN  c.x.ai.stt.SttServiceFactory:82 - Vosk STT服务初始化失败: 
  Error looking up function 'vosk_recognizer_set_grm': 
  dlsym(0x6e0c84f0, vosk_recognizer_set_grm): symbol not found
```

**根因分析**：
- `VoskSttService.initialize()` 调用 `dlsym` 加载原生库中的 `vosk_recognizer_set_grm` 函数
- macOS arm64 的 native lib（推测是 dylib 或 jnilib）不包含该符号
- 可能原因：
  1. Vosk Java 库版本与 native lib 不匹配
  2. native lib 编译时未包含 grammar 相关导出
  3. macOS arm64 平台未提供该 Vosk 版本

**影响评估**：
- **不影响生产**：因为 `sys_role.roleId=1.sttId=2` 走的是 `provider=funasr`（API），而非 Vosk 本地服务
- 但是日志噪音：每次启动都打 WARN，且 `VoskSttService` 是 `@PostConstruct` 默认初始化路径

**修复建议**（低优先级）：
```java
// SttServiceFactory.java initializeDefaultSttService()
// 在 catch 块判断 e.getMessage() 是否包含 "symbol not found"
// 是的话降级为 log.debug 而非 log.warn，避免每次启动噪音
if (e.getMessage() != null && e.getMessage().contains("symbol not found")) {
    log.debug("Vosk 本地 native lib 在当前平台不支持（macOS arm64），跳过");
} else {
    log.warn("Vosk STT 服务初始化失败: {}", e.getMessage());
}
```

### 4.2 [P2] 后端对话日志完全无 LATENCY/STT/TTS 事件

**现象**：从 11:12:34 启动到 11:16:30 当前时刻，约 4 分钟内**后端 dialogue 日志**：
- 0 条 `[LATENCY]` 事件
- 0 条 `STT_DONE` / `FunASR.*离线修正`
- 0 条 `TTS_FIRST_CHUNK` / `sendTtsMessage.*state=sentence_start`
- 0 条 `WebSocketHandler.*connect`
- 0 条 `MCP.*工具调用成功`

**根因**：
- 设备刚启动 6.5 分钟，处于 idle 等待唤醒状态
- 没有任何用户对 ESP32 说过话，所以零交互
- 设备从未主动发起 WebSocket 连接（**正常**：唤醒词检测在 ESP32 本地 AFE 完成，不需连 WS）

**结论**：**这是预期行为，不是 bug**。本次无法做精确 latency 分析。

### 4.3 [INFO] 设备 WiFi / IP 切换

**现象**：设备从 v3 的 `HUAWEI-9YQAVW / 192.168.3.22` 切到 v4 的 `Zikkoy / 192.168.0.152`

**影响**：
- ✅ NVS OTA 配置未改用 build-config（`nvs_ota_overridden: false`），自动跟随 build-config 切换到新主机 IP `192.168.0.198`
- ✅ 主机 Mac IP 也自动切换到 `192.168.0.198`
- ✅ 两者对齐，OTA/WS 配置无需手动修改
- 这正是上版本 v2 修复「NVS OTA URL 清除 API」带来的好处

**建议**：保持现状，规则文档「5. 服务器地址同步规则」已经过时（现在 NVS 不强制覆写，自动对齐）。

### 4.4 [INFO] 设备已自动跳过 OTA version check

**现象**：
```
+9.693 Application: ActivationTask: Skipping OTA version check for faster startup
```

**评价**：✅ 上次 v3 优化的成果，启动更快（避免每次都 HTTP GET OTA）

---

## 5. 已验证的修复项（vs v3）

| 修复项 | v3 状态 | v4 验证 |
|---|---|---|
| ✅ WebSocket 异常断开 | 0 次 | 0 次（设备未连接，所以 0） |
| ✅ NVS OTA URL 清除 API | 已实现 | 设备 NVS 未被错误覆写 ✅ |
| ✅ 后端跳过 OTA version check | 已实现 | 设备日志确认 Skipping ✅ |
| ✅ TTS TTFA（SentenceHelper.FORCE_FLUSH_LENGTH=32） | ~0ms | 无新会话验证 |
| ✅ MCP 工具调用耗时 | <4s | 无新会话验证 |
| ✅ 设备音量从 15% 拉到 100% | v3 发现问题 | 无新会话验证 |
| ✅ DisplayLockGuard 超时 30s → 2s | v3 已修 | 无新会话验证 |

---

## 6. 当前已配置但未运行的能力

| 模块 | 配置位置 | 状态 |
|---|---|---|
| WebSocket 服务 | `ws://192.168.0.198:8092/ws/xiaozhi/v1/` | ✅ 启动就绪 |
| Silero VAD | 内存模型，windowSize=512 | ✅ 初始化成功 |
| FunASR（API） | sys_config id=2, provider=funasr | ✅ 配置存在（待实际 STT 调用触发） |
| FunASR 容器 | xiaozhi-sensevoice port 10096 | ✅ 容器运行中 |
| Edge TTS | sys_config id=4, provider=edge | ✅ 默认（待实际 TTS 调用触发） |
| MiniMax LLM | sys_config id=3, provider=MiniMax | ✅ 配置存在 |
| volcengine doubao TTS | sys_config id=6, provider=volcengine | ✅ 配置存在（isDefault=0） |
| 3 个 MCP 工具 | 已发布到 Redis | ✅ 已注册 |

---

## 7. E2E Pipeline 对照（16 步骤）

> 本次 0 会话，所以全部 N/A。下次有新交互后会按此框架逐项提取。

```
┌─ 设备端 (ESP32 @ 192.168.0.152) ──────────────────────┐
│ [1] AFE 唤醒检测    : ⬜ 未触发                          │
│ [2] Wake word 编码  : ⬜                                │
│ [3] WS hello 上行   : ⬜                                │
│ [4] SendStartListen : ⬜                                │
│ [5-6] WS 握手/音频  : ⬜                                │
│ [15] OnIncomingJson : ⬜                                │
│ [16] OnIncomingAudio: ⬜                                │
└───────────────────────────────────────────────────────┘
                          │
                          ▼
┌─ 后端 (Java @ 192.168.0.198:8092) ─────────────────────┐
│ [7-9]  VAD init + Opus  : ⬜                            │
│ [10]   STT (FunASR API): ⬜                            │
│ [11-13]LLM→TTS 流式   : ⬜                            │
│ [14]   Player → WS    : ⬜                            │
└───────────────────────────────────────────────────────┘
```

---

## 8. 下一步建议

| 优先级 | 行动 | 等待条件 |
|---|---|---|
| **P0** | 用户主动唤醒一次 ESP32（说"贾维斯"+指令），生成新一次交互数据 | 用户操作 |
| P0 | 在新会话产生后立即重跑 jarvis-e2e-auto-analyzer | 触发条件 |
| P1 | 修复 Vosk 初始化失败噪音日志（仅当 macOS 开发环境使用 Vosk 时） | 没人用 Vosk 时可暂缓 |
| P2 | 更新 `.trae/rules/rule_xiaozhi.md` 「5. 服务器地址同步规则」：说明 NVS 自动对齐 build-config，无需手动同步 | 文档维护 |

---

## 9. 验证检查清单

### 设备端（运行时验证 ✅）

| 检查项 | 验证方式 | 结果 |
|---|---|---|
| 设备上电 | uptime > 0 | ✅ 388s |
| WiFi 连接 | status.wifi_connected | ✅ true |
| SD 卡挂载 | status.sdcard_mounted | ✅ true |
| HTTP 服务 | status.http_running | ✅ true |
| WakeWord 模型加载 | 日志 `wn9_jarvis_tts` | ✅ |
| WakeWord 阈值设置 | 日志 `Set WakeNet threshold to 0.4` | ✅ |
| WakeWord 监听循环 | 日志 `WakeWordFeed fed N chunks` | ✅ 持续运行 |
| OTA 配置对齐 | status.server + ota-url API | ✅ 与 build-config 一致 |
| 麦克风 RMS 健康 | 日志稳态 < 100 | ✅ RMS=23-35 |

### 后端（运行时验证 ✅）

| 检查项 | 验证方式 | 结果 |
|---|---|---|
| xiaozhi-server (8091) | start.sh status | ✅ pid=4865 |
| xiaozhi-dialogue (8092) | start.sh status | ✅ pid=4875 |
| xiaozhi-web (8084) | start.sh status | ✅ pid=5212 |
| FunASR 容器 | docker ps | ✅ xiaozhi-sensevoice 10096 |
| MySQL | start.sh status | ✅ xiaozhi-mysql 3306 |
| Redis | start.sh status | ✅ xiaozhi-redis 6379 |
| Silero VAD 初始化 | dialogue.log | ✅ |
| WebSocket endpoint | dialogue.log WebSocketConfig | ✅ |

### 本次 E2E 测试（不可执行 ⏸）

| 检查项 | 状态 | 原因 |
|---|---|---|
| 唤醒词检测 | ⏸ 未测 | 等待用户操作 |
| WebSocket 握手 | ⏸ 未测 | 无设备连接 |
| STT 识别 | ⏸ 未测 | 无音频 |
| LLM 首 token | ⏸ 未测 | 无对话 |
| TTS 首帧 | ⏸ 未测 | 无回复 |

---

## 10. 文件索引

| 功能 | 路径 |
|---|---|
| 后端 dialogue 日志 | `xiaozhi-esp32-server-java/logs/xiaozhi-dialogue.log` |
| 后端 server 日志 | `xiaozhi-esp32-server-java/logs/xiaozhi-server.log` |
| 设备 SD 卡日志（最新） | `xiaozhi-esp32/.trae/documents/snapshots/XZHI.LOG`（需手动从 SD 卡导出） |
| 设备实时日志 API | `http://192.168.0.152:8080/api/device/logs` |
| Vosk 初始化代码 | `xiaozhi-esp32-server-java/xiaozhi-ai/src/main/java/com/xiaozhi/ai/stt/SttServiceFactory.java:42-80` |
| 默认角色配置（DB） | `sys_role` 表 `roleId=1` |
| 默认 STT/LLM/TTS 配置（DB） | `sys_config` 表 id=2/3/4/5/6 |
| 上次报告 | `xiaozhi-esp32/.trae/documents/jarvis_e2e_analysis_20260713_v3.md` |
| 本次报告 | `xiaozhi-esp32/.trae/documents/jarvis_e2e_analysis_20260714_v4_no_session.md` |

---

## 11. 关键结论

1. **当前环境就绪**：后端 5 个进程全部 healthy，设备 1 台在线并处于 idle 等待唤醒
2. **网络切换透明**：设备从 `HUAWEI-9YQAVW` 切到 `Zikkoy` 后，NVS 配置 + Mac IP 自动对齐，**无需手动操作**
3. **零交互数据**：本次报告无法做 16 步 pipeline 详细分析，需等待下一次用户唤醒
4. **唯一发现 bug**：Vosk macOS arm64 native lib 符号缺失，仅产生启动 WARN 日志，不影响生产（生产用 FunASR API）
5. **建议下一步**：用户在 ESP32 前说一次"贾维斯"+指令后，立即重跑本流程，会得到完整的 v5 分析报告