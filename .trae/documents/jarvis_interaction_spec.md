# ESP32 Jarvis 语音交互端到端规格说明

## 1. 问题背景

当前 ESP32 设备的 UI 交互存在以下问题：

1. **GIF 图片显示与 JARVIS 视图脱节**：用户在语音交互过程中请求显示图片时，图片在主屏幕的 `image_overlay_card_` 上显示，而不是在当前活动的 JARVIS 视图上，导致视图层级混乱
2. **占卜视图与 JARVIS 视图无联动**：用户请求占卜时，JARVIS 视图不会自动切换到罗盘主界面，占卜动画与 JARVIS HUD 同时显示造成视觉冲突
3. **缺少统一的视图切换机制**：现有实现中，视图切换分散在多个地方，缺乏统一的状态管理和协调

## 2. 功能需求

### 2.1 唤醒交互

| 编号 | 需求描述 | 优先级 | 来源 |
|------|----------|--------|------|
| FR-001 | 用户唤醒后，设备显示 JARVIS HUD 动画视图 | 高 | 业务需求 |
| FR-002 | JARVIS 视图显示期间，设备接收语音输入并进行对话 | 高 | 业务需求 |
| FR-003 | 语音交互结束后，自动隐藏 JARVIS 视图，返回罗盘主界面 | 高 | 业务需求 |

### 2.2 GIF 图片显示

| 编号 | 需求描述 | 优先级 | 来源 |
|------|----------|--------|------|
| FR-004 | 用户请求显示 GIF 图片时，图片应在当前活动视图（JARVIS 或罗盘）上覆盖显示 | 高 | 业务需求 |
| FR-005 | GIF 显示持续时间为 5 秒，超时后自动隐藏 | 高 | 业务需求 |
| FR-006 | GIF 隐藏后，设备语音提示"图片已显示" | 高 | 业务需求 |
| FR-007 | GIF 显示期间，JARVIS HUD 动画暂停或减弱 | 中 | 体验需求 |

### 2.3 占卜交互

| 编号 | 需求描述 | 优先级 | 来源 |
|------|----------|--------|------|
| FR-008 | 用户请求占卜时，从 JARVIS 视图切换到罗盘主界面 | 高 | 业务需求 |
| FR-009 | 罗盘主界面显示占卜跑马灯动画 | 高 | 业务需求 |
| FR-010 | 跑马灯动画结束后，自动切换回 JARVIS 视图 | 高 | 业务需求 |
| FR-011 | 切换回 JARVIS 视图后，设备语音播报占卜结果 | 高 | 业务需求 |

## 3. 非功能需求

| 编号 | 需求描述 | 优先级 |
|------|----------|--------|
| NFR-001 | 视图切换时间 < 200ms | 高 |
| NFR-002 | 内存占用增加 < 100KB | 中 |
| NFR-003 | 视图切换过程无白屏闪烁 | 高 |
| NFR-004 | 支持连续操作（唤醒→显示图片→占卜）无状态错乱 | 高 |

## 4. 系统架构

### 4.1 视图层级架构

```
┌─────────────────────────────────────────────┐
│              View Stack Manager             │
│  ┌─────────────────────────────────────┐   │
│  │  当前活动视图: JarvisWatchfaceView   │   │
│  │  ┌───────────────────────────────┐  │   │
│  │  │   JARVIS HUD Animation        │  │   │
│  │  │   (scan_arc, pulse_arc, etc.) │  │   │
│  │  └───────────────────────────────┘  │   │
│  │  ┌───────────────────────────────┐  │   │
│  │  │   Image Overlay Layer         │  │   │
│  │  │   (GIF/PNG/JPG, 5s timeout)   │  │   │
│  │  └───────────────────────────────┘  │   │
│  └─────────────────────────────────────┘   │
│  ┌─────────────────────────────────────┐   │
│  │  后台视图: AttitudeDisplay         │   │
│  │  ┌───────────────────────────────┐  │   │
│  │  │   罗盘主界面 (太极+鱼眼+菜单)  │  │   │
│  │  └───────────────────────────────┘  │   │
│  └─────────────────────────────────────┘   │
└─────────────────────────────────────────────┘
```

### 4.2 视图切换状态机

```
        ┌──────────────────┐
        │   CompassView   │ ← 待机状态
        │   (罗盘主界面)   │
        └────────┬────────┘
                 │
           [唤醒词触发]
                 │
                 ▼
        ┌──────────────────┐
        │  JarvisWatchface │ ← 语音交互状态
        │   (JARVIS HUD)   │
        └────────┬────────┘
                 │
    ┌────────────┼────────────┐
    │            │            │
[显示图片]  [开始占卜]  [交互结束]
    │            │            │
    ▼            ▼            ▼
┌──────────┐ ┌──────────┐ ┌──────────┐
│ Image    │ │ Divination│ │ Compass  │
│ Overlay  │ │  View    │ │  View    │
│ (5s后返回│ │ (结束后返│ │          │
│  JARVIS) │ │ 回JARVIS)│ │          │
└──────────┘ └──────────┘ └──────────┘
```

## 5. API 接口规范

### 5.1 AttitudeDisplay 新增方法

| 方法名 | 签名 | 功能描述 |
|--------|------|----------|
| `ShowImageOnActiveView` | `void ShowImageOnActiveView(std::unique_ptr<LvglImage> image, uint32_t timeout_ms = 5000)` | 在当前活动视图上显示图片/GIF |
| `SwitchToDivination` | `void SwitchToDivination()` | 从 JARVIS 视图切换到占卜视图 |
| `SwitchBackFromDivination` | `void SwitchBackFromDivination()` | 占卜结束后切换回 JARVIS 视图 |
| `IsJarvisWatchfaceVisible` | `bool IsJarvisWatchfaceVisible() const` | 获取 JARVIS 视图是否可见 |

### 5.2 FortuneWatchfaceView 新增方法

| 方法名 | 签名 | 功能描述 |
|--------|------|----------|
| `ShowImage` | `void ShowImage(const lv_img_dsc_t* img_dsc, bool is_gif = false)` | 在 JARVIS 视图上显示图片/GIF |
| `HideImage` | `void HideImage()` | 隐藏 JARVIS 视图上的图片 |

### 5.3 MCP 工具接口

| 工具名 | 输入参数 | 输出 | 功能描述 |
|--------|----------|------|----------|
| `self.screen.display_gif` | `url` (string, required) | 文本结果 | 下载并显示图片/GIF |
| `self.attitude.start_divination` | 无 | 文本结果 | 开始占卜跑马灯 |
| `self.attitude.get_divination_result` | 无 | 文本结果 | 获取占卜结果 |

## 6. 数据结构

### 6.1 视图状态枚举

```cpp
enum class ActiveView {
    Compass,       // 罗盘主界面
    JarvisWatchface, // JARVIS HUD 视图
    Divination,    // 占卜视图
};
```

### 6.2 视图栈结构

```cpp
struct ViewStack {
    std::vector<ActiveView> stack;
    ActiveView current() const { return stack.empty() ? ActiveView::Compass : stack.back(); }
    void push(ActiveView view);
    void pop();
    void clear();
};
```

## 7. UI/UX 规范

### 7.1 JARVIS 视图图片覆盖层

| 属性 | 值 |
|------|-----|
| 尺寸 | 300x300 圆形 |
| 位置 | 屏幕居中 |
| 背景 | 半透明黑色 (0x0A1414, 90% 不透明度) |
| 边框 | 金色 (0xD4AF37), 2px |
| 圆角 | 150px (圆形) |
| 动画 | 淡入淡出 (300ms) |

### 7.2 视图切换过渡

| 属性 | 值 |
|------|-----|
| 切换动画 | 淡入淡出 |
| 动画时长 | 200ms |
| 过渡顺序 | 先显示新视图，再隐藏旧视图 |

## 8. 端到端流程

### 8.1 唤醒显示 JARVIS

```
用户: 贾维斯
    │
    ▼
[ESP32] HandleWakeWordDetectedEvent
    │
    ├─ ShowDebugInfo("唤醒成功", "Jarvis")
    ├─ PlaySound(OGG_POPUP)
    └─ ShowJarvisWatchface()
        │
        ├─ lv_screen_load(overlay_screen_)
        ├─ fortune_watchface_visible_ = true
        └─ 启动 JARVIS HUD 动画
    │
    ▼
[Java] WebSocket 连接 → STT → LLM → TTS
    │
    ▼
[ESP32] 播放 TTS 回复
```

### 8.2 显示 GIF 图片

```
用户: 显示一个 gif 图片
    │
    ▼
[Java] STT → LLM → MCP Tool Call
    │
    │  {
    │    "type": "mcp",
    │    "payload": {
    │      "id": 1,
    │      "method": "tool_call",
    │      "params": {
    │        "tool_name": "self.screen.display_gif",
    │        "arguments": {
    │          "url": "https://example.com/image.gif"
    │        }
    │      }
    │    }
    │  }
    │
    ▼
[ESP32] McpServer::ParseMessage
    │
    └─ self.screen.display_gif 回调
        │
        ├─ HTTP 下载图片
        ├─ 创建 LvglImage 对象
        └─ attitude->ShowImageOnActiveView(image, 5000)
            │
            ├─ if fortune_watchface_visible_:
            │     FortuneWatchfaceView::ShowImage(img_dsc)
            │  else:
            │     SetPreviewImage(image)
            │
            └─ 设置 5 秒定时器
    │
    ▼
5秒后...
    │
    ├─ HideImage() / OnPreviewImageHideTimer()
    │
    ▼
[Java] LLM → "图片已显示"
    │
    ▼
[ESP32] TTS 播放
```

### 8.3 开始占卜

```
用户: 开始占卜
    │
    ▼
[Java] STT → LLM → MCP Tool Call: self.attitude.start_divination()
    │
    ▼
[ESP32] attitude->SwitchToDivination()
    │
    ├─ HideJarvisWatchface()
    │     ├─ lv_timer_pause(timer_)
    │     ├─ lv_obj_add_flag(overlay_screen_, LV_OBJ_FLAG_HIDDEN)
    │     └─ lv_screen_load(prev_screen_)
    │
    ├─ fortune_watchface_visible_ = false
    │
    └─ StartFortuneDivination()
        ├─ fortune_divination_state_ = Animating
        ├─ 创建跑马灯定时器
        └─ 开始动画
    │
    ▼
跑马灯动画进行中 (30秒)
    │
    ▼
占卜结束 → FinishFortuneDivinationUnlocked(result)
    │
    ├─ fortune_divination_state_ = Result
    ├─ 显示占卜结果提示
    │
    ▼
[延迟 2 秒后...]
    │
    ▼
attitude->SwitchBackFromDivination()
    │
    ├─ StopFortuneDivination()
    │
    └─ ShowJarvisWatchface()
        ├─ lv_screen_load(overlay_screen_)
        ├─ fortune_watchface_visible_ = true
        └─ lv_timer_resume(timer_)
    │
    ▼
[Java] MCP Tool Call: self.attitude.get_divination_result()
    │
    ├─ 返回结果: "占卜结果：今日运势 - 大吉"
    │
    ▼
[Java] LLM → "今日占卜结果：大吉，诸事顺遂"
    │
    ▼
[ESP32] TTS 播放占卜结果
```

## 9. 关键实现细节

### 9.1 FortuneWatchfaceView::ShowImage()

```
1. 创建 image_overlay_ (如果不存在)
2. 创建 image_widget_ (如果不存在)
3. 设置 image_widget_ 的图片源
4. 暂停 JARVIS HUD 动画定时器
5. 显示 image_overlay_ (淡入动画)
6. 设置图片隐藏定时器
```

### 9.2 AttitudeDisplay::ShowImageOnActiveView()

```
1. 获取 LVGL 锁
2. 判断 fortune_watchface_visible_
3. 如果 JARVIS 可见:
   - FortuneWatchfaceView::ShowImage(img_dsc)
4. 否则:
   - SetPreviewImage(image, timeout_ms)
5. 释放 LVGL 锁
```

### 9.3 AttitudeDisplay::SwitchToDivination()

```
1. 获取 LVGL 锁
2. 如果 JARVIS 可见:
   - HideJarvisWatchface()
3. StartFortuneDivination()
4. 释放 LVGL 锁
```

### 9.4 AttitudeDisplay::SwitchBackFromDivination()

```
1. 获取 LVGL 锁
2. StopFortuneDivination()
3. ShowJarvisWatchface()
4. 释放 LVGL 锁
5. 通过 MCP 广播回调通知后端获取占卜结果
```

## 10. 测试用例

### 10.1 功能测试

| 编号 | 测试场景 | 步骤 | 预期结果 |
|------|----------|------|----------|
| TC-001 | 唤醒显示 JARVIS | 1. 说"贾维斯" | JARVIS HUD 显示 |
| TC-002 | JARVIS 显示期间显示 GIF | 1. 唤醒 2. 说"显示图片" | GIF 在 JARVIS 视图上显示 |
| TC-003 | GIF 5秒后自动隐藏 | 1. 显示 GIF 2. 等待 5 秒 | GIF 自动隐藏 |
| TC-004 | GIF 隐藏后语音提示 | 1. 显示 GIF 2. 等待 5 秒 | 语音提示"图片已显示" |
| TC-005 | JARVIS 期间开始占卜 | 1. 唤醒 2. 说"开始占卜" | 切换到罗盘，跑马灯动画 |
| TC-006 | 占卜结束返回 JARVIS | 1. 开始占卜 2. 等待结束 | 切换回 JARVIS 视图 |
| TC-007 | 占卜结果语音播报 | 1. 占卜结束 | 语音播报占卜结果 |

### 10.2 边界测试

| 编号 | 测试场景 | 步骤 | 预期结果 |
|------|----------|------|----------|
| TC-008 | 连续唤醒 | 1. 唤醒 2. 交互结束 3. 再次唤醒 | 每次正常显示/隐藏 |
| TC-009 | 连续占卜 | 1. 唤醒 2. 占卜 3. 返回 4. 再次占卜 | 每次正常切换 |
| TC-010 | 网络中断恢复 | 1. 唤醒 2. 断网 3. 恢复 | 恢复后正常工作 |

## 11. 风险与缓解

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| GIF 解码内存不足 | 应用崩溃 | 使用 PSRAM，限制图片大小 |
| 视图切换白屏 | 用户体验差 | 先显示新视图再隐藏旧视图 |
| 定时器冲突 | 状态错乱 | 使用互斥锁保护定时器操作 |
| 网络延迟导致 TTS 超时 | 无语音回复 | 添加超时重试机制 |

## 12. 实施计划

| 阶段 | 内容 | 涉及文件 | 状态 |
|------|------|----------|------|
| 阶段一 | FortuneWatchfaceView 添加图片覆盖层 | fortune_watchface_view.h/.cc | 待实施 |
| 阶段二 | AttitudeDisplay 添加视图切换协调方法 | attitude_display.h/.cc | 待实施 |
| 阶段三 | 更新 MCP 工具实现 | mcp_server.cc | 待实施 |
| 阶段四 | 编译验证 | - | 待实施 |
| 阶段五 | 烧录测试 | - | 待实施 |
| 阶段六 | 真机功能验证 | - | 待实施 |
