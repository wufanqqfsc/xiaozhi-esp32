# Checklist

## FortuneWatchfaceView 对象池化

- [ ] FortuneWatchfaceView 构造函数中创建所有静态 UI 元素（刻度 60 个、弧背景、JARVIS 标签等）
- [ ] CreateDynamicWatchface() 不再在 Show() 路径中被调用（仅构造时调用一次）
- [ ] 轨道点 12 个独立 lv_obj_t 已合并为 1 个 orbit_canvas_ lv_obj_t
- [ ] orbit_canvas_ 上每帧正确绘制 12 个轨道点的位置和透明度动画
- [ ] timer_ 在构造时创建，Show() 调用 lv_timer_resume，Hide() 调用 lv_timer_pause
- [ ] 多次 Show/Hide 循环后 lv_obj_tree 中对象数量不增长
- [ ] FortuneWatchfaceView 的 Show() / Hide() / IsVisible() 外部接口签名不变

## 鱼眼预渲染缓存

- [ ] WiFi 鱼眼和 BLE 鱼眼各创建 3 个预渲染 canvas buffer (DISCONNECTED / CONNECTING / CONNECTED)
- [ ] 预渲染 buffer 尺寸与当前 FISHEYE_ICON_SIZE 一致
- [ ] UpdateWifiFisheye / UpdateBleFisheye 调用 lv_canvas_set_buffer 切换到预渲染 buffer，而非逐像素重绘
- [ ] 当新 status 与当前 status 相同时（稳态），UpdateWifiFisheye / UpdateBleFisheye 直接返回，不触发任何绘制
- [ ] DISCONNECTED / CONNECTING / CONNECTED 三帧视觉效果与优化前完全一致
- [ ] 太极图自动旋转（60s/圈）时，鱼眼区域在稳态下不触发无效重绘

## ShowDebugInfo 事件队列

- [ ] DebugInfoItem 结构体包含 title / detail / hold_ms / priority / timer 字段
- [ ] debug_info_queue_ 为 std::deque 类型，最大队列长度 5
- [ ] 新事件优先级 < 当前显示事件优先级时被拒绝入队（ESP_LOGD 记录）
- [ ] 新事件优先级 >= 当前显示事件优先级时弹出当前事件，新事件立即显示
- [ ] 队列满（>= 5）时拒绝 LOW 优先级事件入队
- [ ] 队列头部事件拥有独立 lv_timer_t，timer 到时后自动弹出并显示下一个
- [ ] HideDebugInfo() 正确弹出队列头部，若队列非空则立即显示下一个
- [ ] RefreshDebugInfoTimer() 仅在当前事件仍为队列头部时重置其 timer
- [ ] 旧的 debug_info_hide_timer_ 单一定时器和 dedup 机制已移除
- [ ] 模拟场景 1：唤醒成功（CRITICAL 30s）后紧接工具调用（LOW 2.5s），唤醒成功不被覆盖
- [ ] 模拟场景 2：工具调用（LOW 2.5s）后收到识别到（HIGH 5s），工具调用被识别到覆盖
- [ ] 模拟场景 3：队列已有 5 个事件时收到新的 LOW 事件，确认被拒绝入队

## 编译与运行

- [ ] 代码编译通过，无报错
- [ ] 设备正常启动，AttitudeDisplay 界面正常显示
- [ ] FortuneWatchfaceView Show/Hide 循环 10 次后无内存泄漏
- [ ] WiFi 连接/断开时鱼眼状态切换视觉正常
- [ ] ShowDebugInfo 连续调用（唤醒→工具调用→识别到）时事件按优先级正确排队/覆盖
