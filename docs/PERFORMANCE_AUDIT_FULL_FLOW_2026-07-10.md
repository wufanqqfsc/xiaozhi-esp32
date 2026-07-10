# Xiaozhi ESP32 全功能性能审计（启动到运行全链路）

- 审计日期: 2026-07-10
- 代码仓库: `c:/workspace/xiaozhi-esp32`
- 覆盖目标: 从 `app_main` 启动到各功能稳定运行，覆盖显示/语音/网络/HTTP/SD/BLE/MCP/截图/唤醒链路

---

## 1. 覆盖范围（本次已扫描）

### 启动与主流程
- `main/main.cc` (`app_main`)
- `main/application.cc` (`Initialize` / `Run` / `HandleNetworkConnectedEvent` / `ActivationTask`)
- `main/device_state_machine.cc`

### 板级与显示
- `main/boards/waveshare/esp32-s3-touch-lcd-1.85b/esp32-s3-touch-lcd-1.85b.cc`
- `main/display/lcd_display.cc`
- `main/display/lvgl_display/lvgl_display.cc`
- `main/display/attitude_display.cc`
- `main/display/compass_taiji.cc`

### 音频与唤醒
- `main/audio/audio_service.cc`
- `main/audio/wake_words/afe_wake_word.cc`
- `main/audio/wake_words/custom_wake_word.cc`

### 网络协议与设备连接
- `main/boards/common/wifi_board.cc`
- `main/protocols/websocket_protocol.cc`
- `main/protocols/mqtt_protocol.cc`
- `main/ota.cc`
- `main/assets.cc`

### HTTP / SD / 截图 / MCP / BLE
- `main/sdcard_log_http.cc`
- `main/sdcard_log.cc`
- `main/http_api_unified.cc`
- `main/mcp_server.cc`
- `main/ble/ble_server.cc`

### 配置与全局参数
- `sdkconfig`
- `sdkconfig.defaults.esp32s3`

---

## 2. 启动到可用的关键路径（性能视角）

典型链路:

1. `app_main` → `nvs_flash_init`
2. `Application::Initialize`
3. `Board::GetInstance`（板级初始化，含 LCD/LVGL/触摸/SD 相关）
4. `display->SetupUI`（含太极资源准备与首次刷新）
5. `audio_service_.Initialize/Start`（音频任务创建）
6. WiFi 异步连接
7. `ActivationTask`（资产/协议）
8. 状态切到 `Idle`，启用唤醒检测

### 启动阶段主要耗时与压力点
- 板级 + 显示初始化（SPI 屏初始化命令序列、首帧构建）是启动早期大头。
- UI 初始化和太极资源构建有明显 CPU/PSRAM 峰值。
- 激活任务、HTTP、BLE、SD 日志在前 10 秒内容易形成叠加压力峰。
- 主循环优先级较高且 `Schedule()` 负载集中时，会挤压低优先级任务执行窗口。

---

## 3. 按功能模块的性能问题与改进建议

## 3.1 显示/LVGL

### 发现
- 分页渲染 + SPI 传输正常，但动态动画路径存在过高刷新频率。
- 占卜动画 tick 25ms（40Hz）在 30fps 刷新环境中偏激进。
- 旋转/布局更新中存在高频 `lv_obj_update_layout` 风险点。
- 截图链路在锁内做完整刷新 + 全帧抓取 + JPEG 编码，锁占用长。

### 建议
- 将占卜动画 tick 调整到 33~50ms，并仅更新状态变化的控件。
- 将 UI 声音播放移出显示锁关键区，缩短锁持有时间。
- 截图任务改为常驻 worker，避免频繁创建任务栈。
- 若画质允许，截图 JPEG 质量适度下调或支持下采样输出。

---

## 3.2 音频/唤醒链路

### 发现
- `audio_input`、`opus_codec`、唤醒检测、语音处理并发时 CPU 竞争明显。
- 编解码和网络发送链路有较多短生命周期动态分配。
- 解码队列较深时，`wait=true` 路径可能阻塞生产侧。
- 某些状态切换会触发解码/播放队列清空，可能截断播报体验。

### 建议
- 编解码热路径引入可复用 scratch buffer，减少每帧 `vector` 分配。
- 评估 `opus_codec` 优先级与队列深度，降低高负载下音频抖动。
- 唤醒与语音处理模型采用更精细启停策略（按状态启用，避免无效并发）。
- 为关键语音链路建立“时延预算”监控（唤醒->首包、首包->播放）。

---

## 3.3 HTTP / SD / 文件与截图 API

### 发现
- 多个接口在 `httpd` 线程执行目录扫描/文件 stat/JSON 构建，容易互相阻塞。
- 日志、截图、文件列表 API 在高频调用下重复扫描 SD 目录。
- SD 卡日志重定向会把大量日志写入 SD + 串口，I/O 压力高。

### 建议
- 热接口增加目录缓存（短 TTL + mtime 失效）避免重复全量扫描。
- 大 I/O 路径逐步迁移为“handler 快速返回 + worker 异步执行”模式。
- 生产场景降低日志级别；必要时关闭 SD+UART 双写镜像。
- 批量日志删除、文件列举等路径输出增加统计，便于容量和耗时治理。

---

## 3.4 协议层（WebSocket/MQTT）与主循环调度

### 发现
- 主循环优先级较高，且承接了大量 `Schedule` 回调执行。
- WebSocket 断线恢复策略与 MQTT 不对称，弱网时会影响音频业务连续性。
- 音频发送数据序列化存在频繁字符串构建开销。

### 建议
- 为 WebSocket 增加明确重连策略（含退避与状态回调）。
- 对高频消息路径使用复用缓冲或对象池，减少反复分配。
- 将重任务从主循环迁移到专用 worker，再回主线程做 UI/状态提交。

---

## 3.5 MCP / BLE / 其他并行功能

### 发现
- MCP 某些工具调用路径较重，若跑在主循环会拉高主线程占用。
- BLE 初始化与 WiFi/HTTP/激活时序重叠时，存在资源竞争风险。

### 建议
- MCP 重工具（抓图、下载、复杂 JSON）改为 worker 执行。
- BLE 启动时机可后移到更稳定状态，减少启动期并发峰值。

---

## 4. 跨模块优先级待办（性能优化 Backlog）

## P0（优先立即处理）
1. 占卜动画刷新频率与布局更新降载（显示主热点）。
2. WebSocket 重连策略补齐（弱网可恢复能力）。
3. SD 日志写入策略降压（降低 I/O 抢占）。
4. 主循环重任务下沉 worker（避免高优线程拥塞）。
5. 高频路径动态分配削峰（音频/网络/HTTP）。

## P1（中期）
1. 目录与文件元数据缓存（logs/shots/files/audio-list）。
2. 任务栈高水位复测后收敛（activation/httpd/opus 等）。
3. 截图链路常驻 worker + 参数化质量策略。
4. 启动阶段并发编排优化（减少前 10 秒峰值叠加）。

## P2（持续优化）
1. 构建 profile A/B（`-Os` vs 性能优化）量化对比。
2. 日志体系分级治理（开发/测试/生产）。
3. LVGL 更精细脏区与动画策略优化。

---

## 5. 建议的量化指标（必须先测后改）

### 启动体验
- T1: 上电到 `Initialize` 完成
- T2: 上电到 WiFi 获取 IP
- T3: 上电到 `Idle`
- T4: 上电到唤醒检测可用

### 语音链路
- V1: 唤醒词命中到 `OpenAudioChannel` 完成
- V2: `OpenAudioChannel` 到首个 TTS 包接收
- V3: 首包接收到扬声器出声

### 资源占用
- M1: internal heap / PSRAM free 与 min-free
- M2: 关键任务 stack high-water mark
- C1: 主循环、audio_input、opus、LVGL 任务 CPU 占比

### I/O
- I1: SD 日志每秒写入字节
- I2: `/api/sdcard/logs`、`/api/sdcard/files` 平均/95分位耗时
- I3: 截图接口平均耗时与并发失败率

---

## 6. 推荐执行顺序（落地路线）

1. 先加埋点，跑基线（启动 + 语音 + HTTP 压测）
2. 实施 P0（显示降频、WS 重连、主循环减负、日志降压）
3. 回归与复测（同一场景同一指标）
4. 推进 P1（缓存、栈收敛、worker 化）
5. 再次复测并固化阈值（作为后续迭代门槛）

---

## 7. 结论

当前工程结构清晰、功能完整，但性能瓶颈集中在“**高频 UI 动画 + 音频并发 + SD/HTTP I/O + 主循环负载集中**”四条线上。  
优先处理 P0 后，预计可显著改善：

- 启动稳定性与可预测性
- 语音交互时延与流畅性
- 页面/动画流畅度
- HTTP 接口在并发场景下的响应稳定性

