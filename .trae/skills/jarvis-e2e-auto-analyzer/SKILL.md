---
name: "jarvis-e2e-auto-analyzer"
description: "Automated E2E analyzer for Jarvis voice interactions. Generates jarvis_e2e_analysis_*.md reports and runs diagnose→fix→verify closed-loop. Invoke when user asks to analyze last voice interaction, review the most recent jarvis E2E, or run diagnose→fix→verify cycle."
---

# Jarvis E2E 自动分析与闭环修复技能

自动分析最近的贾维斯语音交互流程，生成与 `jarvis_e2e_analysis_*.md` 同格式的分析报告，并基于分析结果调用 `xiaozhi-e2e-pipeline` / `xiaozhi-server-java` / `esp32-http-api` 三个相关技能完成问题排查、代码修复和闭环验证。

## 适用场景

- 用户要求"分析上一次的语音交互"、"复盘最近一次对话"、"出具 E2E 分析报告"
- 用户要求"排查延迟问题"、"诊断 TTS/MCP/VAD/STT 卡顿"
- 用户要求"修复问题并验证"、"闭环迭代"、"从分析到修复"
- v2 文档新增问题需立即分析

## 前置条件

| 条件 | 验证命令 |
|------|---------|
| 后端服务运行 | `cd xiaozhi-esp32-server-java && ./start.sh status` |
| ESP32 设备在线 | `curl http://<DEVICE_IP>:8080/api/device/status` |
| 日志可访问 | `curl http://<DEVICE_IP>:8080/api/sdcard/logs` |

## 自动化执行流程（六阶段闭环）

```
┌─────────────────────────────────────────────────────────────────────┐
│  阶段 1: 数据采集                                                    │
│    ├─ 设备状态：GET /api/device/status                               │
│    ├─ NVS 配置：GET /api/device/ota-url                              │
│    ├─ 最新设备日志：GET /api/sdcard/logs → 下载最新 .log 文件        │
│    ├─ 后端对话日志：tail -f xiaozhi-dialogue.log（最近 1 小时）      │
│    └─ SenseVoice 容器：docker ps | grep sensevoice                  │
└─────────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────────┐
│  阶段 2: 16 步 E2E Pipeline 提取                                     │
│    解析后端 + 设备日志，按 16 步框架提取关键事件：                   │
│    1-3. AFE 唤醒词检测                                               │
│    4.    设备发送 listen detect                                      │
│    5-6.  WebSocket 握手 + 音频流                                     │
│    7-9.  VAD 初始化 + Opus 解码                                      │
│    10.   STT 识别完成                                                │
│    11-13. LLM → TTS 流式合成                                        │
│    14.   Player → WS 发送音频                                        │
│    15.   设备 OnIncomingJson 分发                                    │
│    16.   设备 OnIncomingAudio 播放                                   │
└─────────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────────┐
│  阶段 3: 性能指标计算                                                │
│    ├─ 唤醒 → LLM_FIRST_TOKEN（LLM TTFT）                             │
│    ├─ 唤醒 → TTS_FIRST_CHUNK（TTS TTFA）                             │
│    ├─ VAD START → VAD END（用户说话时长）                            │
│    ├─ VAD END → STT 返回（STT 延迟）                                 │
│    ├─ LLM TTFT + LLM 总时长                                          │
│    ├─ MCP 工具调用耗时                                               │
│    └─ WS 断开/Connection reset 事件                                  │
└─────────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────────┐
│  阶段 4: 问题诊断（参考 xiaozhi-e2e-pipeline 阶段定义）              │
│    按 LATENCY 日志 + Connection reset 标记识别：                     │
│    ├─ P0: 设备重启/WS 异常断开                                       │
│    ├─ P1: TTS TTFA > 3s                                              │
│    ├─ P1: MCP 工具调用 > 3s                                          │
│    ├─ P1: VAD 超时重复触发                                           │
│    ├─ P2: NVS 配置错误                                               │
│    ├─ P2: STT 延迟 > 6s                                              │
│    └─ 其他可观察问题                                                 │
└─────────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────────┐
│  阶段 5: 代码修复（按问题严重度）                                     │
│    P0: 立即修复并重启后端 + 烧录固件                                  │
│    P1: 修复后端（VADService/SentenceHelper/DisplayLockGuard）        │
│    P1: 修复设备端（attitude_display.cc/sdcard_log_http.cc）           │
│    P2: 清除 NVS 配置错误                                             │
│    每修复一项 → 编译 → 重启服务 → 记录修复内容到报告                  │
└─────────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────────┐
│  阶段 6: 闭环验证（不引入新 bug）                                     │
│    ├─ 重新执行 E2E 测试（xiaozhi-voice-e2e-test）                    │
│    ├─ 对比修复前/后关键指标                                          │
│    ├─ 确认无新引入问题                                               │
│    └─ 生成最终分析报告 → 保存到 .trae/documents/                    │
└─────────────────────────────────────────────────────────────────────┘
```

## 报告输出格式

报告文件命名：`jarvis_e2e_analysis_YYYYMMDD_HHMMSS.md`

存放路径：`xiaozhi-esp32/.trae/documents/`

### 报告章节模板

```markdown
# 贾维斯语音交互 E2E 流程梳理（最新一次）

> 分析时间：YYYY-MM-DD HH:MM
> 数据来源：后端日志 + 设备日志 + SenseVoice 容器
> 使用技能：xiaozhi-e2e-pipeline、esp32-http-api、xiaozhi-server-java

## 会话基本信息
| 项 | 值 |
| 设备 IP / WS URI / SessionId / 时间范围 / 提供商 / 版本 | ... |

## 第一轮：唤醒应答
| # | 阶段 | 时间戳 | 耗时 | 日志位置 |
（16 步 pipeline 表）

## 第二轮：用户输入
| 阶段 A: STT / 阶段 B: LLM+MCP / 阶段 C: TTS 持续 |
（详细时序表）

## 各阶段耗时汇总
| 第一轮/第二轮 | 指标 | 耗时 | 评价 |

## 关键问题诊断
### ✅ 已验证修复项（vs 上次）
### ⚠ 当前发现的新问题
- P0/P1/P2 分类
- 根因分析
- 修复状态

## 优化建议
| 高优先级/中优先级 | 建议 |

## E2E Pipeline 对照（16 步骤）
（16 步对照图）

## 修复效果对比
| 指标 | 上次 | 本次 | 改善 |

## 待解决问题
| # | 问题 | 优先级 | 状态 | 下一步 |
```

## 执行命令清单

### 阶段 1: 数据采集

```bash
# 1.1 设备状态
curl -s http://192.168.3.22:8080/api/device/status | python3 -m json.tool

# 1.2 NVS OTA URL 配置
curl -s http://192.168.3.22:8080/api/device/ota-url | python3 -m json.tool

# 1.3 SD 卡日志列表
curl -s http://192.168.3.22:8080/api/sdcard/logs | python3 -c "import sys,json; d=json.load(sys.stdin); [print(f\"{x['name']:<55} {x['size_bytes']:>10}\") for x in d[-10:]]"

# 1.4 下载最新设备日志（取最新一个 .log 文件）
LATEST=$(curl -s http://192.168.3.22:8080/api/sdcard/logs | python3 -c "import sys,json; d=json.load(sys.stdin); print(d[-1]['name'])")
curl -s "http://192.168.3.22:8080/api/sdcard/logs/$LATEST" -o /tmp/device_latest.log

# 1.5 后端对话日志（最新 1 小时）
tail -n +$(wc -l < xiaozhi-esp32-server-java/logs/xiaozhi-dialogue.log | awk '{print $1-5000}') \
  xiaozhi-esp32-server-java/logs/xiaozhi-dialogue.log > /tmp/dialogue_recent.log

# 1.6 SenseVoice 容器状态
docker ps -a | grep -i sensevoice

# 1.7 FunASR 端口检查
curl -s -o /dev/null -w "FunASR 10096: %{http_code}\n" http://localhost:10096/
```

### 阶段 2-3: 提取 16 步 Pipeline + 计算性能指标

```bash
# 2.1 提取所有 LATENCY 事件
grep -E "\[LATENCY\]" /tmp/dialogue_recent.log | tail -100

# 2.2 提取 WS 连接事件
grep -E "WebSocketHandler.*connect|WebSocketHandler.*close|Transport error|Connection reset" \
  /tmp/dialogue_recent.log | tail -30

# 2.3 提取 TTS 句子开始事件
grep -E "sentence_start.*text" /tmp/dialogue_recent.log | tail -20

# 2.4 提取 STT 识别结果
grep -E "sendSttMessage.*text" /tmp/dialogue_recent.log | tail -10

# 2.5 提取 MCP 工具调用结果
grep -E "ToolLogger.*工具调用成功" /tmp/dialogue_recent.log | tail -10

# 2.6 设备端关键事件
grep -nE "Wake word|Listen|State:|SwitchTo|Taiji|fortune_divination|VAD" /tmp/device_latest.log
```

### 阶段 4: 性能阈值检查

| 指标 | 正常阈值 | 警告 | 严重 |
|------|---------|------|------|
| LLM TTFT | <3000ms | 3000-5000ms | >5000ms |
| TTS TTFA（首句） | <2000ms | 2000-5000ms | >5000ms |
| TTS TTFA（长句） | <3000ms | 3000-5000ms | >5000ms |
| VAD END → STT 返回 | <5000ms | 5000-8000ms | >8000ms |
| MCP 工具调用 | <3000ms | 3000-5000ms | >5000ms |
| WS 连接持续时间 | — | — | 突然 reset |

### 阶段 5: 代码修复模板

**修复 VAD 超时重复触发** → `VadService.java:302-308`:
```java
state.setSpeaking(false);
log.info("聆听时长超过 {}ms...");
state.resetSilenceFrameCount();
state.sessionStartMs = System.currentTimeMillis();  // 新增：重置防止重复触发
return new VadResult(VadStatus.SPEECH_END, pcmData);
```

**修复 TTS 整段合成阻塞** → `SentenceHelper.java`:
```java
private static final int FORCE_FLUSH_LENGTH = 32;
// 当 currentSentence.length() >= FORCE_FLUSH_LENGTH 时强制送出
```

**修复 MCP DisplayLockGuard 阻塞** → `display.h:69-93`:
```cpp
explicit DisplayLockGuard(Display *display, int timeout_ms = 2000)
    : display_(display), locked_(false) { ... }
```

**修复 NVS OTA URL** → 通过 `POST /api/device/clear-nvs?key=ota_url`

### 阶段 6: 闭环验证

```bash
# 6.1 重启后端
cd xiaozhi-esp32-server-java && ./start.sh restart

# 6.2 验证 jar 包含修复
unzip -p xiaozhi-ai/target/xiaozhi-ai-5.0.0.jar com/xiaozhi/ai/tts/SentenceHelper.class | strings | grep FORCE_FLUSH

# 6.3 执行 E2E 测试（通过 xiaozhi-voice-e2e-test skill）
# - 合成唤醒语音 "Jarvis"
# - afplay 播放
# - 观察后端日志 TTS_FIRST_CHUNK 是否下降

# 6.4 对比修复前/后
# 在报告中追加 "修复效果对比" 表格
```

## 与其他技能的协同

| 技能 | 协同方式 |
|------|---------|
| **xiaozhi-e2e-pipeline** | 提供 16 步 pipeline 阶段定义，LATENCY 关键字段 |
| **xiaozhi-server-java** | 提供后端代码位置、配置说明、构建命令 |
| **esp32-http-api** | 提供设备侧 API 列表（NVS 清除、状态查询、日志下载） |
| **xiaozhi-voice-e2e-test** | 阶段 6 验证时调用执行 TTS + afplay 测试 |

## 已知问题与对应修复（参考库）

> 每次闭环迭代后将新问题追加到本节，形成知识库。

### 已修复（v1 → v2）
- ✅ SenseVoice STT 切换：6.137s（vs Paraformer 29.337s）
- ✅ 会话资源清理：66ms（vs 上次 3 分 13 秒僵尸流程）
- ✅ 角色一致性：先生（vs 上次小何）

### 已修复（v2 → v2后）
- ✅ VAD 超时重复触发 bug：`sessionStartMs` 重置
- ✅ DialogueService 非 LISTENING 状态跳过 VAD
- ✅ SentenceHelper 强制分句（FORCE_FLUSH_LENGTH=32）
- ✅ DisplayLockGuard 超时 30s → 2s
- ✅ NVS OTA URL 清除 API（POST /api/device/clear-nvs）

### 待持续观察
- ⚠️ 设备重启根因（无 panic 痕迹，与 OTA URL/内存/资源竞争相关）
- ⚠️ TTS TTFA 偶发 >20s（疑似 Edge TTS 限流）
- ⚠️ MCP 工具调用 8s（设备端长按占卜动画逻辑）

## 关键文件索引

### 设备端
| 文件 | 说明 |
|------|------|
| `main/application.cc` | 主事件循环、唤醒处理、状态机 |
| `main/display/attitude_display.cc` | JARVIS 视图 + 占卜动画 |
| `main/display/display.h` | DisplayLockGuard 定义 |
| `main/sdcard_log_http.cc` | HTTP API + NVS 操作 |
| `main/mcp_server.cc` | self.attitude.* MCP 工具 |

### 后端
| 文件 | 说明 |
|------|------|
| `xiaozhi-dialogue/.../audio/VadService.java` | VAD 状态管理 |
| `xiaozhi-dialogue/.../DialogueService.java` | 音频处理入口 |
| `xiaozhi-dialogue/.../playback/FileSynthesizer.java` | TTS 合成调度 |
| `xiaozhi-ai/.../tts/SentenceHelper.java` | LLM → TTS 分句 |
| `xiaozhi-ai/.../tts/providers/edge/EdgeTtsService.java` | Edge TTS |
| `xiaozhi-ai/.../tts/providers/MiniMaxTtsService.java` | MiniMax TTS |
| `xiaozhi-ai/.../stt/providers/funasr/FunASRSttService.java` | FunASR STT |

### 文档
| 文档 | 说明 |
|------|------|
| `.trae/documents/jarvis_e2e_analysis_*.md` | 历史 E2E 分析报告 |
| `.trae/documents/jarvis_interaction_plan.md` | 计划文档 |
| `.trae/documents/funasr_timeout_root_cause_*.md` | FunASR 超时分析 |
| `.trae/documents/v2_issues_fix_summary_*.md` | v2 修复总结 |

## 执行原则

1. **先分析后修复**：不要在没看到 LATENCY 日志前贸然改代码
2. **数据驱动**：每个修复必须有日志证据支持
3. **增量验证**：每修一个 P0/P1，重启服务验证一次
4. **记录一切**：每次修复都追加到报告的"修复效果对比"表
5. **闭环完成**：所有 P0/P1 修复后必须重测 E2E 确认无新引入问题

## 快速启动命令

```bash
# 一键执行完整分析闭环
cd /Users/sfan/Desktop/cv/github/OpenMAIC
curl -s http://192.168.3.22:8080/api/device/status | python3 -m json.tool
curl -s http://192.168.3.22:8080/api/device/ota-url | python3 -m json.tool
tail -3000 xiaozhi-esp32-server-java/logs/xiaozhi-dialogue.log | grep -E "\[LATENCY\]|WebSocket|Transport|Connection reset|sentence_start|工具调用成功"
echo "---"
echo "完整分析报告生成中 → .trae/documents/jarvis_e2e_analysis_$(date +%Y%m%d_%H%M%S).md"
```