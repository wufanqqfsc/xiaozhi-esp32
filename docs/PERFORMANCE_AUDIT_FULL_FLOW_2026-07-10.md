# Xiaozhi ESP32 全链路性能审计报告（重整版）

- 报告日期: 2026-07-10
- 仓库路径: `c:/workspace/xiaozhi-esp32`
- 审计范围: 启动 -> 激活 -> Idle -> 唤醒/对话 -> HTTP/SD -> 显示/音频/BLE/MCP
- 目标: 给出“可执行、可验收、可排期”的性能优化清单

---

## 1. 结论先行（Executive Summary）

当前性能瓶颈并非单点，而是四条链路叠加：

1. **显示链路高频刷新过密**（占卜动画 40Hz + 旋转变换 + LVGL 布局更新）
2. **音频链路并发负载高**（录音/唤醒/编解码/网络发送叠加）
3. **HTTP/SD I/O 同线程重活**（目录扫描、JSON 构建、文件读写在 `httpd` 线程串行）
4. **主循环承担过多重任务**（高优先级下执行 `Schedule` 负载，影响低优先级实时任务）

**建议优先级**:
- P0: 先做 5 项，预计 1~2 周可落地并见到明显收益
- P1: 结构性优化，2~4 周
- P2: 长线治理与配置收敛

---

## 2. 覆盖范围与核心路径

### 已覆盖模块
- 启动主流程: `main/main.cc`, `main/application.cc`
- 板级/显示: `main/boards/...1.85b.cc`, `main/display/*`
- 音频/唤醒: `main/audio/*`, `main/audio/wake_words/*`
- 网络协议: `main/protocols/websocket_protocol.cc`, `main/protocols/mqtt_protocol.cc`
- OTA/资产: `main/ota.cc`, `main/assets.cc`
- HTTP/SD/截图: `main/sdcard_log_http.cc`, `main/sdcard_log.cc`, `main/http_api_unified.cc`
- MCP/BLE: `main/mcp_server.cc`, `main/ble/ble_server.cc`
- 配置: `sdkconfig`, `sdkconfig.defaults.esp32s3`

### 启动关键路径（性能关键点）
1. `app_main` -> `nvs_flash_init`
2. `Application::Initialize`
3. `Board::GetInstance`（LCD/LVGL/触摸/SD）
4. `display->SetupUI`（太极资源、首帧刷新）
5. `audio_service_.Initialize/Start`
6. WiFi 异步连接 + 激活任务
7. 进入 `Idle`，启用唤醒检测

---

## 3. Top 10 性能问题（按优先级）

| 优先级 | 问题 | 影响 | 位置 |
|---|---|---|---|
| P0 | 占卜动画 tick 25ms（40Hz） | UI 抖动、CPU 占用偏高 | `main/display/attitude_display.h`, `attitude_display.cc` |
| P0 | `httpd` 线程执行重 I/O 与目录遍历 | 接口互相阻塞、尾延迟高 | `main/sdcard_log_http.cc` |
| P0 | SD 日志重定向同步写 SD + UART | I/O 争用，影响实时链路 | `main/sdcard_log.cc` |
| P0 | 主循环优先级高且承载重 `Schedule` 工作 | 低优任务饥饿风险 | `main/application.cc` |
| P0 | WS 重连策略弱于 MQTT | 弱网下语音链路恢复慢 | `main/protocols/websocket_protocol.cc` |
| P1 | 音频热路径频繁动态分配 | 抖动、碎片化风险 | `main/audio/audio_service.cc` |
| P1 | 目录扫描无缓存（logs/shots/files） | 重复 I/O 与 JSON 开销 | `main/sdcard_log_http.cc`, `main/http_api_unified.cc` |
| P1 | 截图链路锁内全量流程 | 长锁占用，影响显示实时性 | `main/sdcard_log_http.cc`, `lvgl_display.cc` |
| P1 | 多任务栈偏保守（activation/httpd/opus） | RAM/PSRAM 余量不足 | `application.cc`, `audio_service.cc`, `sdcard_log_http.cc` |
| P2 | 编译配置偏体积优化（`-Os`） | 热路径 CPU 性能损失 | `sdkconfig` |

---

## 4. 分模块审计与改进建议

## 4.1 显示/LVGL

### 主要问题
- 动画刷新频率高于实际必要值（40Hz）。
- 局部布局更新频繁触发布局计算。
- 截图路径在显示锁内执行较长流程。

### 改进建议
1. 占卜动画 tick 调整到 33~50ms，且仅更新“状态变化”的节点。
2. 避免无变化时调用 `lv_obj_update_layout`。
3. 截图改常驻 worker，减少任务创建与锁占用时间。
4. UI 音效播放从显示锁临界区移出。

---

## 4.2 音频/唤醒

### 主要问题
- 录音、唤醒、编解码、网络发送并发时有 CPU 抢占。
- 编解码与消息发送路径存在短生命周期分配。
- 深队列 + 阻塞写策略在高压场景可能放大时延。

### 改进建议
1. 编解码热路径使用复用 buffer（对象池或成员缓存）。
2. 复核 `opus_codec` 优先级与队列上限，平衡时延与抗抖。
3. 唤醒/语音处理模型采用状态化启停，避免无效并发推理。
4. 建立“唤醒到首包到出声”链路延迟指标并固化阈值。

---

## 4.3 HTTP / SD / 文件 API

### 主要问题
- `httpd` 线程承担目录扫描、stat、JSON 构建、文件 I/O。
- 高并发下接口相互影响明显。
- SD 日志高频写入会与 HTTP 文件访问争用存储带宽。

### 改进建议
1. logs/shots/files 接口引入短 TTL 目录缓存（含 mtime 失效）。
2. 重 I/O 路径改“快速返回 + worker 异步执行”。
3. 生产场景收敛日志级别；必要时关闭 SD+UART 双写。
4. 为关键 API 增加耗时统计（avg/p95/p99）。

---

## 4.4 协议层与主循环调度

### 主要问题
- 主循环承担了较多非必要重任务。
- WebSocket 重连与恢复策略不完整。
- 高频序列化/字符串构建带来分配开销。

### 改进建议
1. WebSocket 对齐 MQTT 的重连退避策略。
2. 主循环只保留状态提交与轻任务；重任务下沉 worker。
3. 高频消息路径改复用缓冲，降低分配/拷贝。

---

## 4.5 MCP / BLE

### 主要问题
- MCP 重工具运行于主线程路径时会拉高主循环占用。
- BLE 启动时机与激活/HTTP/WiFi 重叠时存在峰值竞争。

### 改进建议
1. MCP 重工具（抓图/下载/大 JSON）worker 化。
2. BLE 启动策略后移或按需启用，减少启动峰值叠加。

---

## 5. 优化 Backlog（带实施成本）

## P0（立即执行，1~2 周）

| 项 | 动作 | 预期收益 | 成本 |
|---|---|---|---|
| P0-1 | 占卜动画降频 + 增量更新 | UI 更稳，CPU 降 | 低 |
| P0-2 | WS 重连补齐 | 弱网恢复能力提升 | 中 |
| P0-3 | 主循环重任务下沉 | 降低实时链路抖动 | 中 |
| P0-4 | SD 日志写入降压 | I/O 争用下降 | 低 |
| P0-5 | 热路径分配削峰 | 减少抖动与碎片 | 中 |

## P1（中期，2~4 周）

| 项 | 动作 | 预期收益 | 成本 |
|---|---|---|---|
| P1-1 | 目录/元数据缓存 | API 延迟明显下降 | 中 |
| P1-2 | 栈高水位复测后收敛 | 释放 RAM 余量 | 低 |
| P1-3 | 截图 worker 常驻化 | 降低锁占用/任务开销 | 中 |
| P1-4 | 启动阶段并发编排优化 | 启动更平稳可预测 | 中-高 |

## P2（持续治理）

| 项 | 动作 | 预期收益 | 成本 |
|---|---|---|---|
| P2-1 | `-Os` vs 性能 profile A/B | 热路径性能提升 | 中 |
| P2-2 | 日志分级与发布态策略 | 长期稳定性提升 | 低 |
| P2-3 | LVGL 脏区/动画策略深度优化 | UI 上限提升 | 中 |

---

## 6. 量化指标与验收阈值（建议）

### 启动
- T1: 上电 -> `Initialize` 完成
- T2: 上电 -> WiFi 获取 IP
- T3: 上电 -> `Idle`
- T4: 上电 -> 唤醒可用

### 语音
- V1: 唤醒命中 -> `OpenAudioChannel` 完成
- V2: `OpenAudioChannel` -> 首个 TTS 包
- V3: 首个 TTS 包 -> 扬声器出声

### 资源
- M1: internal heap / PSRAM free & min-free
- M2: 关键任务 stack high-water mark
- C1: main/audio/lvgl/httpd 任务 CPU 占比

### 接口
- I1: `/api/sdcard/logs` avg/p95/p99
- I2: `/api/sdcard/files` avg/p95/p99
- I3: 截图接口平均耗时与失败率

---

## 7. 执行计划（先测后改）

1. **基线测量**: 增加埋点并固定测试场景（启动 + 唤醒对话 + HTTP 压测）
2. **落地 P0**: 先做低风险项（动画降频、日志降压、WS 重连）
3. **回归对比**: 同场景复测，输出前后指标对照
4. **推进 P1**: 缓存、栈收敛、worker 化
5. **固化阈值**: 将指标门槛加入后续迭代验收

---

## 8. 最终建议

建议把本报告作为后续性能治理主文档，按 P0 -> P1 的顺序推进。  
下一步可直接拆成“文件级改造任务单”，每项包含：
- 目标函数/文件
- 改动点
- 风险
- 验收指标

这样可以做到每次迭代都可量化、可回归、可持续优化。

