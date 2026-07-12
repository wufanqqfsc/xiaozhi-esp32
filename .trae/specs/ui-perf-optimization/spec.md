# UI 性能优化 Spec

## Why

当前 AttitudeDisplay 及其子视图存在三类内存/性能问题：

1. **FortuneWatchfaceView 每次 Show() 同步创建 70+ LVGL 对象**，包括 60 个刻度、12 个轨道点、3 个弧等，全部在持有 LVGL 锁时 malloc，峰值内存和堆碎片风险高。
2. **鱼眼（WiFi/BLE Fisheye）Canvas 每帧状态切换时重新逐像素绘制**，且太极图自动旋转（60s/圈）时鱼眼区域被纳入脏区扫描，但鱼眼内容在稳态下并未变化，造成无效 CPU 消耗。
3. **ShowDebugInfo 使用单一 lv_timer_t 独占定时器**，高优先级事件（如"唤醒成功"hold 30s）会被低优先级事件（如"工具调用"hold 2.5s）的后续调用打断并提前隐藏，出现「唤醒成功卡在 2.5s 时消失」的体验 Bug。

## What Changes

### 优化项 1：FortuneWatchfaceView 对象池化 + Canvas 动画合并

- 所有静态视觉元素（刻度 60 个、弧背景、JARVIS 标签、光晕、状态栏）在 `FortuneWatchfaceView` 构造时一次性创建并缓存，**Show() 只控制可见性**，不再重复 malloc。
- 轨道点动画（12 个 `orbit_dots_`）从 12 个独立 `lv_obj_t` 合并为 **1 个 lv_canvas**，每帧仅重绘 12 个点的位置坐标，内存占用从 ~12 × (对象头 + 样式 + 坐标) 降至 ~1 个 canvas buffer。
- `scan_arc_` / `pulse_arc_` / `seconds_arc_` 保持独立 `lv_obj_t`（LVGL arc 控件无 canvas 等效替代），但构造时预创建，不在 Show() 中重复创建。
- `timer_` 改为在构造时创建，Show() 时 `lv_timer_resume`，Hide() 时 `lv_timer_pause`，**避免反复创建销毁**。
- 不改变外部接口：`Show()` / `Hide()` / `IsVisible()` 签名不变。

### 优化项 2：鱼眼 Canvas 预渲染三帧静态 Buffer

- 每个鱼眼（WiFi 上鱼眼 / BLE 下鱼眼）维护 **3 个预渲染 canvas buffer**，分别对应 `DISCONNECTED` / `CONNECTING` / `CONNECTED` 三种 `FisheyeStatus`。
- 三帧在鱼眼对象首次创建时一次性绘制到各自 buffer，后续状态切换时**仅调用 `lv_canvas_set_buffer` 切换指针**，不再逐像素重绘。
- 新增 `FisheyeStatus` 与缓存索引的映射：`DISABLED→0, ADVERTISING→1, CONNECTED→2`。
- `UpdateWifiFisheye(WifiStatus)` / `UpdateBleFisheye(BleStatus)` 增加「稳态跳过」逻辑：当前 status 与上一次绘制相同时跳过重绘。
- 不改变鱼眼视觉外观，不改变外部接口。

### 优化项 3：ShowDebugInfo 改为事件队列 + Per-Event Timer

- 维护 `std::deque<DebugInfoItem>` 事件队列，每进入一个 `ShowDebugInfo(title, detail, hold_ms)` 就 push_back 并启动独立的 per-event timer。
- **高优先级事件不被低优先级事件打断**：事件等级定义为 `enum DebugInfoPriority { LOW=0, MEDIUM=1, HIGH=2, CRITICAL=3 }`；新事件若优先级低于当前显示事件则拒绝入队。
- 优先级映射：
  - `CRITICAL`：唤醒成功（30s）
  - `HIGH`：WiFi 已连接（5s）、握手成功（5s）
  - `MEDIUM`：识别到 / 识别失败（5s）
  - `LOW`：工具调用（2.5s）、联网失败（5s）
- 队列头部事件显示；头部消失（timer 到时或被覆盖）后显示下一个。
- `HideDebugInfo()` 改为弹出队列头部，若队列非空则立即显示下一个。
- `RefreshDebugInfoTimer` 在当前显示事件仍为队列头部时重置其 timer。
- 新增 `DEBUG_INFO_MAX_QUEUE_SIZE = 5`，队列满时拒绝新 LOW 事件入队。
- 不改变调试信息卡的视觉外观，不改变外部接口签名（仅在头文件中补充注释）。

## Impact

- Affected specs：
  - `XIAOZHI_UI_INTERACTION.md` §2.5（ShowDebugInfo 行为）
  - `XIAOZHI_UI_INTERACTION.md` §3（FortuneWatchfaceView 生命周期）
  - `XIAOZHI_UI_INTERACTION.md` §2.7（鱼眼更新）
- Affected code：
  - `main/display/fortune_watchface_view.{h,cc}`
  - `main/display/attitude_display.{h,cc}`（鱼眼 + DebugInfo）
  - `main/display/compass_taiji.{h,cc}`（参考 canvas buffer 模式）

## ADDED Requirements

### Requirement: FortuneWatchfaceView 对象池化

FortuneWatchfaceView 构造时创建所有 UI 元素，后续 Show/Hide 仅切换可见性，不创建/销毁任何 lv_obj_t。

#### Scenario: 反复选中和取消选中今日运势

- **WHEN** 用户短按 BOOT 选中"今日运势"，然后按电源键返回，再次选中
- **THEN** FortuneWatchfaceView 的 UI 元素数量保持在初始构造时的数量，不因反复 Show/Hide 而累加
- **AND** 第二次 Show() 的响应时间 < 50ms（仅 lvgl_port_lock + 可见性切换）

#### Scenario: HUD 轨道点动画正常运行

- **WHEN** FortuneWatchfaceView 处于显示状态
- **THEN** 12 个轨道点的位置变化在单一 canvas 上实时渲染，视觉效果与优化前一致
- **AND** 每帧动画耗时 < 5ms

### Requirement: 鱼眼预渲染缓存

WiFi 鱼眼和 BLE 鱼眼各维护 3 个预渲染 canvas buffer，状态切换时直接切换 buffer 而非重绘。

#### Scenario: WiFi 从 DISCONNECTED 变为 CONNECTED

- **WHEN** 调用 `UpdateWifiFisheye(WifiStatus::CONNECTED)`
- **THEN** 鱼眼 canvas buffer 指针切换到预渲染的 CONNECTED 帧，不再逐像素绘制
- **AND** 切换耗时 < 1ms

#### Scenario: 鱼眼处于稳态（持续 CONNECTED）

- **WHEN** `UpdateWifiFisheye(WifiStatus::CONNECTED)` 被调用但当前状态已是 CONNECTED
- **THEN** 跳过全部重绘操作，函数直接返回

### Requirement: DebugInfo 事件队列

ShowDebugInfo 使用事件队列管理，队列头部事件不被低优先级事件打断。

#### Scenario: 唤醒成功（CRITICAL）后紧接工具调用（LOW）

- **WHEN** 当前显示"唤醒成功"（CRITICAL, hold 30s）后，收到"工具调用"（LOW, hold 2.5s）
- **THEN** "工具调用"进入队列但不覆盖"唤醒成功"；"唤醒成功"保持显示直到 30s 到时
- **AND** 30s 后队列中的"工具调用"才被显示

#### Scenario: 队列满时收到新的 LOW 事件

- **WHEN** 队列已有 5 个事件，又收到一个 LOW 优先级的新事件
- **THEN** 该 LOW 事件被拒绝入队（ESP_LOGD 记录丢弃原因）

#### Scenario: 刷新高优先级事件的计时器

- **WHEN** 当前显示"唤醒成功"（CRITICAL），调用 `RefreshDebugInfoTimer()`
- **THEN** 该事件的 timer 被重置为 hold_ms，30s 倒计时重新开始

## MODIFIED Requirements

### Requirement: ShowDebugInfo 行为变更

**MODIFIED FROM**: `ShowDebugInfo(title, detail, hold_ms)` 使用单一共享定时器，后调覆盖先调。

**MODIFIED TO**: `ShowDebugInfo` 使用优先级队列管理，同一时间只显示队列头部事件；高优先级事件不被低优先级事件打断；每个事件有独立定时器。

## REMOVED Requirements

### Requirement: FortuneWatchfaceView 旧生命周期

**Reason**: 原 Show() 中同步创建 70+ 对象的设计已被对象池化替代，CreateUI 逻辑移至构造时，不再需要"懒创建"。

**Migration**: 现有调用方无需改动，Show() / Hide() 外部接口不变。
