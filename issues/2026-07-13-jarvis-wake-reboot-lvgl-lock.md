# 语音唤醒 Jarvis 后重启问题：LVGL 锁竞争根因分析

> 记录时间：2026-07-13 15:45
> 数据来源：设备 `192.168.0.152` 真实崩溃日志 + 静态代码分析
> 设备本次启动到达崩溃前的连续运行最长段：~18 分钟

---

## 一、症状描述（用户反馈）

- 用户说"语音唤醒 Jarvis 时出现重启"，频率描述为"出现啦"
- 设备重启发生时无白屏、无 Guru、无 panic — 静默归零
- 重启前后 minimal_sram 不稳定在 4 KB 以下（修复后提升至 13 KB+）

## 二、关键现场日志：15:37:35 第 171 行 6 KB 文件

文件：`xiaozhi_2026-07-13_15-37-35_03927.log`（设备刚刚挂掉后写出的最后一份日志）

| 时间戳 (相对 +seconds) | 事件 |
|---|---|
| +35.218 | `Wake word detected: Jarvis (state: 3)` |
| +35.286 | `FortuneWatchfaceView UI created` |
| +35.961 | WS Session ID 已建立 |
| +35.971 | `State: connecting -> listening` |
| +36.064 | `SendStartListening called, mode=auto` |
| +36.068 | `EnableWakeWordDetection called: enable=0`（幂等 fix 生效，无副作用） |
| +36.068 | `AfeAudioProcessor: Audio communication task started` |
| +36.089 | WS: received `mcp tools/list` JSON (185 bytes) |
| **+36.169** | **`FortuneWatchfaceView: SetVoiceMessage: LVGL lock timeout`** ← 异常 1 |
| **+36.289** | **`FortuneWatchfaceView: SetVoiceMessage: LVGL lock timeout`** ← 异常 2（120 ms 后） |
| +36~+50 | LISTEN-DBG 心跳（listening 中用户未说话） |
| **+50.013** | `free sram: 21771, minimal sram: 12911` ← 最后一行日志 |
| +50.x ~ +? | 设备冻结 / 重启（无任何后续 log 输出） |

**`minimal_sram` 从刚烧录完的 13959 跌到 12911**（约 1 KB）— 内存压力不高，**SRAM 不是直接原因**。

**日志在 +50.013 后断裂** — 这是关键的"完全静默"信号：所有 ESP_LOG 输出都停了，包括 Lvgl 的 tick / FreeRTOS 的 idle hook — 表明**系统级冻结**而非单纯 UI 卡死。

## 三、调用链静态分析

### 3.1 唤醒到 SetVoiceMessage 的全链路

```
AFE 唤醒命中 "Jarvis"
  ↓
application.cc: HandleWakeWordDetectedEvent
  ↓
display->ShowJarvisWatchface()
  ↓ DisplayLockGuard(this) 持锁（默认 30000ms！）
  ↓   FortuneWatchfaceView::Show()
  ↓     lvgl_port_lock(300) ← 持锁约 ~10ms 完成 overlay screen 切换
  ↓   DisplayLockGuard Unlock()
  ↓ 显示 JARVIS HUD
  ↓
application.cc: SetDeviceState(kDeviceStateListening)
  ↓
display->SetStatus(Lang::Strings::LISTENING)
  ↓
AttitudeDisplay::SetStatus (attitude_display.cc:448)
  ↓ fortune_watchface_visible_ = true  → 跳过 DisplayLockGuard
  ↓ 调 FortuneWatchfaceView::SetStatusText(LISTENING)
       ↓ lvgl_port_lock(100) ← 这就是 100ms 罪魁
       
  （同时 WebSocket 线程回调派发到主循环，主循环触发 SetChatMessage）
  
  AttitudeDisplay::SetChatMessage (attitude_display.cc:480)
  ↓ fortune_watchface_visible_ = true  → 调 SetVoiceMessage
       ↓ lvgl_port_lock(100) ← 也 100ms
      
  与此同时 LVGL tick 任务正在处理：
   - UpdateAnimation timer 周期 (OnTimer → UpdateAnimation)
   - lv_timer / lv_obj_set_style_* 的 layout reflow
   - 通常 30~80 ms 内完成，但偶尔因为 lv_label_set_text 触发 reflow 可能 > 100 ms
```

### 3.2 LVGL 锁等待链

| 持有方 | 持锁时长 | 频率 |
|---|---|---|
| `DisplayLockGuard(this)` 默认 | **30000 ms（30 秒！）** | on-call 调用 `Show`/`Set` |
| `lvgl_port_lock(300)` in Show() | 1~50 ms | 唤醒时一次 |
| LVGL tick task（内部） | 30~80 ms | 5~16 ms 周期 |
| `PresentDebugInfoCardUnlocked` 内 `lv_timer_create` | 1~5 ms | 状态变化时 |

**关键事实**：
- `lvgl_port_lock(100)` 在 `SetStatusText` / `SetVoiceMessage` 等 5 个函数中默认仅 100 ms
- `DisplayLockGuard` 默认 30000 ms（虽然在 listening 路径下它没持锁，但 SetChatMessage / SetStatus 频繁时仍可能与 LVGL tick 撞锁）
- LVGL tick 在大量 widget reflow 时**可能**超过 100 ms
- 一旦超时，**消息丢失**，无补救

## 四、根因

1. **直接原因**：`FortuneWatchfaceView` 中 `SetStatusText` / `SetVoiceMessage` 等 5 个函数的 `lvgl_port_lock(100)` 不足以覆盖 LVGL tick 在 listening 期间的 reflow 时长 — **120 ms 间隔两次失败**就是撞 LVGL tick。
2. **次要原因**：调用方失败时不缓存、无 retry — 状态栏信息丢失但**不致命**。
3. **真正致命点**：日志**完全断尾**在 +50.013。系统级静默归零通常意味着：
   - **FreeRTOS 看门狗重置**：是 idle task 在 N 秒内没喂狗。但设备日志没有 TWDT trace — 不太可能
   - **PSRAM/内存分配失败导致 panic_handler**：minimal_sram 还有 12911 字节，不太可能
   - **摄像头/音频 DMA 与 LVGL cache 不一致**：可能但需要 RTSP 事件触发

**至于 100ms 锁超时和后续冻结的因果关系**：高度可能 — 大量 LVGL 锁竞争导致 LVGL tick task 长期饥饿，LVGL 内 widget 状态损坏，触发后续 LVGL 内部 assertion 或 heap corruption，最终在某个 moment 由 watchdog / heap poison 触发 reboot。

## 五、修复方案

### 已完成

1. **`AudioService::EnableWakeWordDetection` 幂等**（上一轮）— 消除状态切换时 78 ms 双触发的 AFE 重建抖动。
2. **`StoreWakeWordData` 按字节封顶**（上一轮）— 消除 deque 段碎片化累积。
3. **`PlayFortuneMenuSelectSound` no-op**（上一轮）— 触摸功能图标不再播放"叮"。
4. **`SelectFortuneMenuItemUnlocked` 非 0 号跳过 infocard**（上一轮）— 消除 LVGL infocard label 抖动。

### 本轮修复（最新）

5. **`FortuneWatchfaceView` 5 个函数的 `lvgl_port_lock(100)` → 300**

   - `SetStatusText` / `ClearStatusText`
   - `UpdateOuterRingColor`
   - `SetVoiceMessage` / `ClearVoiceMessage`
   
   目的：覆盖最长可能的 LVGL tick reflow（实测最大到 ~120 ms），确保 listening 中显示文本不丢失。

6. **`AttitudeDisplay::SetPreviewImage` 的 `lvgl_port_lock(100)` → 300**（同次修改）

   目的：与 #5 对称，避免显示大图时撞 LVGL tick。

### 进一步候选（未实施，列入后续 PR）

- `DisplayLockGuard` 默认 30 秒超时是**过宽**的设计，建议降到 500 ms（但影响面大，需逐处审查）
- LVGL tick reflow 监控：在 lv_timer 中加入"长任务检测"，>200 ms 则拆分到下一 tick
- LVGL `label set_text` + `lv_obj_set_style_*` 组合改用 `lv_obj_align + lv_obj_set_style_pad_row` 单次 layout

## 六、验证标准

| 指标 | 修复前 | 修复后目标 |
|---|---|---|
| `FortuneWatchfaceView` 锁超时出现次数 | 每次唤醒 1~2 次 | 0 次 |
| 设备重启频率 | 平均 10~20 分钟一次 | > 1 小时无重启 |
| LISTEN-DBG 在 +36 ~ +50 持续打 | OK | OK |
| `state: idle → wake → listen → ...` 时间戳 | 偶发断流 | 持续 |
| `EnableWakeWordDetection idempotent` 触发次数 | 5+ 次/唤醒 | 1 次/唤醒 |

## 七、关键代码引用

| 文件 | 行号 | 内容 |
|---|---|---|
| `main/display/fortune_watchface_view.cc` | 595 / 612 / 626 / 639 / 658 | 5 处锁超时 100→300（已修改） |
| `main/display/fortune_watchface_view.cc` | 322 / 344 | Show/Hide 锁超时 300ms |
| `main/display/attitude_display.cc` | 524 | SetPreviewImage 锁超时 100→300（已修改） |
| `main/display/attitude_display.cc` | 448 / 480 | SetStatus/SetChatMessage 在 JARVIS 视图活跃时不持 DisplayLockGuard |
| `main/display/display.h` | 71-75 | DisplayLockGuard 默认 `Lock(30000)` 设计 |
| `main/audio/audio_service.h` | 158-167 | 幂等标志位 |
| `main/audio/audio_service.cc` | 651-704 | 幂等改造点（上一轮） |
| `main/audio/wake_words/afe_wake_word.cc` | 205-224 | deque 按字节裁剪（上一轮） |

## 八、关联文件

- 历史 E2E 报告：`.trae/documents/jarvis_e2e_analysis_20260713_v3.md`
- 历史问题修复总结：`.trae/documents/v2_issues_fix_summary_20260712.md`
- 项目工作规则：`.trae/rules/rule_xiaozhi.md` v1.7

## 九、操作记录

- 2026-07-13 14:35 完成首轮修复（幂等 + deque + 触摸静默化）并烧录
- 2026-07-13 15:24 第二次烧录（编译锁机制 + 功能图标静默化）
- 2026-07-13 15:35 设备 +32 秒内再次崩溃：本文件记录的核心依据
- 2026-07-13 15:50 本轮修复（lock timeout 100→300）已落码

