# Tasks

## 优化项 1：FortuneWatchfaceView 对象池化 + Canvas 动画合并

- [ ] Task 1.1: 分析当前 CreateDynamicWatchface() 创建的所有 lv_obj_t，统计对象数量和每类对象的内存占用
  - [ ] 读取 fortune_watchface_view.cc 中 CreateTickMarks / CreateDynamicWatchface / AddArc / AddBox 等函数
  - [ ] 统计：刻度 60 个、轨道点 12 个、频谱条 5 个、弧 3 个、标签 3 个、其他容器/背景若干

- [ ] Task 1.2: 将所有静态 UI 元素（刻度、弧背景、JARVIS 标签、光晕、状态栏）的创建从 CreateDynamicWatchface() 移至 FortuneWatchfaceView 构造函数
  - [ ] 新增私有成员指针所有待缓存的 lv_obj_t*
  - [ ] 在构造函数中调用 BuildStaticUI() 创建全部静态元素
  - [ ] 移除 CreateDynamicWatchface() 中对应的动态创建逻辑
  - [ ] 保留 scan_arc_ / pulse_arc_ / seconds_arc_ / orbit_dots_ / jarvis_bars_ 的预创建

- [ ] Task 1.3: 将轨道点动画从 12 个独立 lv_obj_t 合并为 1 个 lv_canvas
  - [ ] 将 orbit_dots_[12] 替换为 orbit_canvas_ (lv_obj_t*)
  - [ ] 在 orbit_canvas_ 上每帧重绘 12 个点的位置，而非操作 12 个独立对象
  - [ ] UpdateAnimation() 中同步更新轨道点动画逻辑
  - [ ] 验证：动画视觉效果与优化前一致

- [ ] Task 1.4: 将 timer_ 生命周期从 Show()/Hide() 移至构造/析构
  - [ ] 在构造函数中创建 timer_ (lv_timer_create)
  - [ ] Show() 中使用 lv_timer_resume，Hide() 中使用 lv_timer_pause
  - [ ] 确认 CreateUI() 不再被重复调用（添加 if 保护）

- [ ] Task 1.5: 验证优化后对象数量和内存峰值
  - [ ] 使用 esp_get_free_heap_size() 对比优化前后内存峰值
  - [ ] 多次 Show/Hide 循环后验证无对象泄漏（lv_obj_tree 校验）

## 优化项 2：鱼眼 Canvas 预渲染三帧静态 Buffer

- [ ] Task 2.1: 分析 RedrawWifiFisheyeCanvas() / RedrawBleFisheyeCanvas() 的绘制逻辑
  - [ ] 读取 attitude_display.cc 中 CreateWifiFisheye / RedrawWifiFisheyeCanvas 等函数
  - [ ] 确认每帧绘制操作：清 buffer → 画圆形底 → 画描边圆 → 画图标

- [ ] Task 2.2: 为每个鱼眼新增 3 个预渲染 canvas buffer
  - [ ] 在 AttitudeDisplay 私有成员中新增：
    - `uint8_t* wifi_fisheye_buffer_[3]` (DISCONNECTED/CONNECTING/CONNECTED)
    - `uint8_t* ble_fisheye_buffer_[3]`
  - [ ] 在 CreateWifiFisheye() / CreateBleFisheye() 时一次性绘制 3 帧到各 buffer
  - [ ] 使用与当前 canvas 相同尺寸 (FISHEYE_ICON_SIZE × FISHEYE_ICON_SIZE)

- [ ] Task 2.3: 实现状态切换时直接切换 buffer 而非重绘
  - [ ] 新增 `UpdateWifiFisheyeFast(WifiStatus)` / `UpdateBleFisheyeFast(BleStatus)` 内部方法
  - [ ] 调用 `lv_canvas_set_buffer` 切换到对应预渲染 buffer
  - [ ] 保留原有 Redraw 函数用于首次初始化

- [ ] Task 2.4: 实现稳态跳过逻辑
  - [ ] 在 UpdateWifiFisheye() / UpdateBleFisheye() 入口增加：
    - 如果当前 status == 上次绘制时的 status，直接 return
  - [ ] 用成员变量记录上次绘制的 status

- [ ] Task 2.5: 验证鱼眼视觉外观不变
  - [ ] 对比优化前后截图，确认 DISCONNECTED / CONNECTING / CONNECTED 三帧视觉效果完全一致

## 优化项 3：ShowDebugInfo 事件队列 + Per-Event Timer

- [ ] Task 3.1: 设计 DebugInfoItem 结构体和事件队列
  - [ ] 定义 `enum DebugInfoPriority { LOW, MEDIUM, HIGH, CRITICAL }`
  - [ ] 定义 `struct DebugInfoItem { string title, detail; uint32_t hold_ms; DebugInfoPriority priority; lv_timer_t* timer; }`
  - [ ] 新增 `std::deque<DebugInfoItem> debug_info_queue_` 成员
  - [ ] 新增 `DebugInfoItem* current_item_` 指向队列头部

- [ ] Task 3.2: 重写 ShowDebugInfo 实现队列逻辑
  - [ ] 新事件优先级 < 当前显示事件优先级 → 拒绝入队（ESP_LOGD 记录）
  - [ ] 新事件优先级 >= 当前显示事件优先级 → 弹出当前事件（timer pause），新事件入队并立即显示
  - [ ] 队列满（>= 5）时拒绝 LOW 事件入队
  - [ ] 显示队列头部事件的 UI（复用现有 DrawDebugInfoCard）
  - [ ] 为新事件创建独立 timer

- [ ] Task 3.3: 重写 HideDebugInfo 实现队列弹出逻辑
  - [ ] 弹出队列头部事件，删除其 timer
  - [ ] 若队列非空，立即显示下一个（重新创建 timer）
  - [ ] 若队列为空，清空 DebugInfo 卡 UI

- [ ] Task 3.4: 实现 RefreshDebugInfoTimer
  - [ ] 仅在当前显示事件仍为队列头部时重置其 timer
  - [ ] 使用传入的 hold_ms 或默认 DEBUG_INFO_SHOW_MS

- [ ] Task 3.5: 验证事件优先级打断逻辑
  - [ ] 模拟：唤醒成功（CRITICAL 30s）后立即调用工具调用（LOW 2.5s），确认唤醒成功不被覆盖
  - [ ] 模拟：工具调用（LOW 2.5s）后收到识别到（HIGH 5s），确认工具调用被识别到覆盖
  - [ ] 模拟：队列满（5个）时收到 LOW 事件，确认被拒绝入队

- [ ] Task 3.6: 清理旧的 dedup 机制和独占定时器逻辑
  - [ ] 移除 debug_info_hide_timer_ 单一定时器
  - [ ] 移除 debug_info_dedup_map_ dedup 去重逻辑（队列本身已提供去重语义）
  - [ ] 确认无悬空 timer 引用

## Task Dependencies

- Task 1.2 依赖 Task 1.1（需先确认要移动哪些对象）
- Task 1.3 依赖 Task 1.2（轨道点合并需在静态元素迁移后进行）
- Task 1.4 依赖 Task 1.2（timer 生命周期调整需与对象创建同步）
- Task 2.2 依赖 Task 2.1（预渲染 buffer 尺寸基于现有绘制逻辑）
- Task 2.3 依赖 Task 2.2（buffer 切换逻辑基于已创建的 buffer）
- Task 2.4 依赖 Task 2.3（稳态跳过需 buffer 切换已就绪）
- Task 3.2 依赖 Task 3.1（队列结构就绪后才能实现入队/出队逻辑）
- Task 3.3 依赖 Task 3.1
- Task 3.4 依赖 Task 3.2 和 Task 3.3
- Task 3.5 依赖 Task 3.2、3.3、3.4（全部就绪后才做端到端验证）
- Task 3.6 依赖 Task 3.5（验证通过后清理旧代码）

**可并行执行**：
- Task 1.x 系列与 Task 2.x 系列互相独立，可并行
- Task 3.x 系列与 Task 1.x/2.x 互相独立，可并行

**验证任务**（需所有优化项完成后）：
- [ ] 综合验证：编译通过 + 设备启动 + 反复 Show/Hide FortuneWatchfaceView 无泄漏
- [ ] 综合验证：WiFi 连接/断开时鱼眼状态切换正常 + 太极自动旋转时鱼眼无闪烁
- [ ] 综合验证：唤醒成功后立即工具调用，唤醒成功不被工具调用打断
