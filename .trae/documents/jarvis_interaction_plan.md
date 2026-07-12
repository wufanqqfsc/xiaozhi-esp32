# ESP32 Jarvis 语音交互端到端改造计划

**版本**: v3.0 (Review 完善版)  
**日期**: 2026-07-12  
**作者**: Trae Agent

---

## 一、需求分析

### 1.1 期望交互流程

```
人：贾维斯（唤醒）
ESP32：1.唤醒  2.显示Jarvis语音交互view  3.开始简短语音回复

人：显示一个gif图片
ESP32：切换到显示gif图片持续5s → 切回Jarvis语音交互view → 语音提示"图片已显示"

人：开始占卜
ESP32：从Jarvis语音交互view切换到占卜view → 开始占卜跑马灯效果 → 效果结束 → 切回Jarvis语音交互view → 语音播报占卜结果
```

### 1.2 当前实现状态 Review

| 功能 | 状态 | 当前实现 | 差距 |
|------|------|----------|------|
| 唤醒显示 JARVIS | ✅ 已实现 | `HandleWakeWordDetectedEvent` → `ShowJarvisWatchface()` | 无 |
| 交互结束隐藏 JARVIS | ✅ 已实现 | `kDeviceStateIdle` → `HideJarvisWatchface()` | 无 |
| GIF 显示（主屏幕） | ✅ 已实现 | `SetPreviewImage()` / `image_overlay_card_` | 需要支持 JARVIS 视图 |
| GIF 动画播放 | ✅ 已实现 | `LvglGif` + `SetPreviewGif()` | 需要支持 JARVIS 视图 |
| 占卜跑马灯 | ✅ 已实现 | `StartFortuneDivination()` + 定时器驱动 | 需要与 JARVIS 视图联动 |
| GIF 与 JARVIS 视图联动 | ❌ 缺失 | 无 | **需要新增** |
| 占卜与 JARVIS 视图联动 | ❌ 缺失 | 无 | **需要新增** |
| 语音播报结果 | ⚠️ 部分 | TTS 回复正常 | 需要 LLM 配合生成特定播报 |

### 1.3 功能需求清单

| 编号 | 需求描述 | 优先级 | 状态 |
|------|----------|--------|------|
| FR-001 | 用户唤醒后，设备显示 JARVIS HUD 动画视图 | 高 | ✅ |
| FR-002 | JARVIS 视图显示期间，设备接收语音输入并进行对话 | 高 | ✅ |
| FR-003 | 语音交互结束后，自动隐藏 JARVIS 视图，返回罗盘主界面 | 高 | ✅ |
| FR-004 | 用户请求显示 GIF 图片时，图片应在当前活动视图上覆盖显示 | 高 | ❌ |
| FR-005 | GIF 显示持续时间为 5 秒，超时后自动隐藏 | 高 | ❌（JARVIS视图） |
| FR-006 | GIF 隐藏后，设备语音提示"图片已显示" | 高 | ❌（LLM配合） |
| FR-007 | 用户请求占卜时，从 JARVIS 视图切换到罗盘主界面 | 高 | ❌ |
| FR-008 | 罗盘主界面显示占卜跑马灯动画 | 高 | ✅ |
| FR-009 | 跑马灯动画结束后，自动切换回 JARVIS 视图 | 高 | ❌ |
| FR-010 | 切换回 JARVIS 视图后，设备语音播报占卜结果 | 高 | ❌（LLM配合） |

### 1.4 非功能需求

| 编号 | 需求描述 | 优先级 |
|------|----------|--------|
| NFR-001 | 视图切换时间 < 200ms | 高 |
| NFR-002 | 内存占用增加 < 100KB | 中 |
| NFR-003 | 视图切换过程无白屏闪烁 | 高 |
| NFR-004 | 支持连续操作（唤醒→显示图片→占卜）无状态错乱 | 高 |

---

## 二、技术架构分析

### 2.1 现有视图层级

```
┌─────────────────────────────────┐
│  FortuneWatchfaceView (JARVIS) │ ← overlay_screen_ 独立屏幕
│  ├── scan_arc_                 │
│  ├── pulse_arc_               │
│  ├── orbit_canvas_            │
│  └── jarvis_label_            │
└─────────────────────────────────┘

┌─────────────────────────────────┐
│  AttitudeDisplay (罗盘主界面)   │ ← lv_screen_active() 返回此屏幕
│  ├── attitude_container_       │
│  │   ├── 太极 + 鱼眼           │
│  │   ├── 运势菜单环            │
│  │   └── debug_info_card_     │
│  └── image_overlay_card_      │ ← 当前 GIF 显示位置
│      ├── preview_image_        │
│      └── preview_gif_          │
└─────────────────────────────────┘
```

### 2.2 改造后视图层级

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
│  │  │   占卜跑马灯动画              │  │   │
│  │  └───────────────────────────────┘  │   │
│  └─────────────────────────────────────┘   │
└─────────────────────────────────────────────┘
```

### 2.3 视图切换状态机

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

### 2.4 MCP 工具链路

```
用户语音 → STT → LLM → MCP Tool Call → McpServer::ParseMessage → 工具回调
                                                         │
                    ┌────────────────────────────────────┼────────────────────────────────────┐
                    ▼                                    ▼                                    ▼
           self.screen.display_gif              self.attitude.start_divination        self.attitude.get_divination_result
                    │                                    │                                    │
                    ▼                                    ▼                                    ▼
           ShowImageOnActiveView()          SwitchToDivination()             GetDivinationResult()
                    │                                    │                                    │
                    ▼                                    ▼                                    ▼
        ┌─────────┴─────────┐                 HideJarvisWatchface()                 返回占卜结果
        ▼                   ▼                 → StartFortuneDivination()
   JARVIS可见?                                                                      
   YES                    NO                                                        
        │                   │
        ▼                   ▼
   FortuneWatchfaceView  SetPreviewImage()
   ::ShowImage()
```

### 2.5 关键文件清单

| 文件 | 职责 | 需修改 | 修改内容 |
|------|------|--------|----------|
| [fortune_watchface_view.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/fortune_watchface_view.h) | JARVIS 视图定义 | ✅ | 添加图片覆盖层成员变量和方法声明 |
| [fortune_watchface_view.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/fortune_watchface_view.cc) | JARVIS 视图实现 | ✅ | 实现图片覆盖层创建和显示/隐藏逻辑 |
| [attitude_display.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.h) | AttitudeDisplay 类定义 | ✅ | 添加视图切换协调方法声明 |
| [attitude_display.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc) | AttitudeDisplay 实现 | ✅ | 实现视图切换逻辑和占卜结束回调 |
| [mcp_server.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/mcp_server.cc) | MCP 工具注册 | ✅ | 更新工具实现，调用新方法 |
| [application.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/application.cc) | 应用主逻辑 | ✅ | 状态联动优化，添加占卜结束回调注册 |

---

## 三、改造方案

### 3.1 方案总览

采用**视图栈管理**模式，在 AttitudeDisplay 中维护当前活动视图，实现视图切换时的平滑过渡：

```
ViewStack: [罗盘主界面] → push(JARVIS) → [罗盘, JARVIS]
                     → push(图片/GIF) → [罗盘, JARVIS, 图片]
                     → pop() → [罗盘, JARVIS]
                     → push(占卜) → [罗盘, 占卜]
                     → pop() → [罗盘]
```

### 3.2 阶段一：JARVIS 视图添加图片显示能力

**修改文件**: [fortune_watchface_view.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/fortune_watchface_view.h)

**新增成员变量**:
```cpp
// 图片覆盖层
lv_obj_t* image_overlay_ = nullptr;
lv_obj_t* image_widget_ = nullptr;
class LvglGif* gif_controller_ = nullptr;
lv_timer_t* image_hide_timer_ = nullptr;
uint8_t* gif_raw_data_ = nullptr;
size_t gif_raw_size_ = 0;
```

**新增方法声明**:
```cpp
// 显示图片/GIF（在 JARVIS 视图之上覆盖显示）
void ShowImage(const lv_img_dsc_t* img_dsc, bool is_gif = false, uint32_t timeout_ms = 5000);

// 隐藏 JARVIS 视图上的图片
void HideImage();

// 判断当前是否正在显示图片
bool IsImageVisible() const;

// 设置 GIF 原始数据
void SetGifRawData(uint8_t* data, size_t size);
```

**修改文件**: [fortune_watchface_view.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/fortune_watchface_view.cc)

**实现细节**:

1. **在 `CreateUI()` 中创建图片覆盖层**:
   - 创建 `image_overlay_`：300x300 圆形，半透明黑色背景，金色边框
   - 创建 `image_widget_`：居中放置，初始隐藏

2. **实现 `ShowImage()`**:
   - 获取 LVGL 锁
   - 停止图片隐藏定时器（如果存在）
   - 设置 `image_widget_` 的图片源
   - 如果是 GIF，启动 GIF 控制器
   - 暂停 JARVIS HUD 动画定时器
   - 显示 `image_overlay_`（淡入动画）
   - 设置图片隐藏定时器（timeout_ms）
   - 释放 LVGL 锁

3. **实现 `HideImage()`**:
   - 获取 LVGL 锁
   - 停止图片隐藏定时器
   - 停止 GIF 控制器
   - 隐藏 `image_overlay_`（淡出动画）
   - 恢复 JARVIS HUD 动画定时器
   - 释放 LVGL 锁

4. **实现定时器回调**:
   - 定时器触发时调用 `HideImage()`

### 3.3 阶段二：AttitudeDisplay 添加视图切换协调方法

**修改文件**: [attitude_display.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.h)

**新增公共方法声明**:
```cpp
// 在当前活动视图（罗盘或 JARVIS）上显示图片/GIF
void ShowImageOnActiveView(std::unique_ptr<LvglImage> image, uint32_t timeout_ms = 5000);

// 从 JARVIS 视图切换到占卜视图（隐藏 JARVIS，显示罗盘并开始占卜）
void SwitchToDivination();

// 占卜结束后切换回 JARVIS 视图
void SwitchBackFromDivination();

// 获取当前是否显示 JARVIS 视图
bool IsJarvisWatchfaceVisible() const { return fortune_watchface_visible_; }

// 设置占卜结束回调
void SetDivinationCallback(std::function<void(int)> callback);
```

**新增成员变量**:
```cpp
std::function<void(int)> divination_callback_ = nullptr;
bool divination_from_jarvis_ = false;  // 记录是否从 JARVIS 进入占卜
```

**修改文件**: [attitude_display.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc)

**实现细节**:

1. **实现 `ShowImageOnActiveView()`**:
   ```
   1. 获取 LVGL 锁 (DisplayLockGuard)
   2. 判断 fortune_watchface_visible_
   3. 如果 JARVIS 可见:
      - 获取 img_dsc 和 is_gif
      - 保存 GIF 原始数据（如果是 GIF）
      - FortuneWatchfaceView::ShowImage(img_dsc, is_gif, timeout_ms)
   4. 否则:
      - SetPreviewImage(image, timeout_ms)
   5. 释放 LVGL 锁
   ```

2. **实现 `SwitchToDivination()`**:
   ```
   1. 获取 LVGL 锁
   2. 如果 JARVIS 可见:
      - 记录 divination_from_jarvis_ = true
      - HideJarvisWatchface()  // 隐藏 JARVIS
   3. StartFortuneDivination()  // 开始跑马灯动画
   4. 释放 LVGL 锁
   ```

3. **实现 `SwitchBackFromDivination()`**:
   ```
   1. 获取 LVGL 锁
   2. StopFortuneDivination()  // 停止占卜动画
   3. 如果 divination_from_jarvis_ == true:
      - ShowJarvisWatchface()  // 重新显示 JARVIS
      - divination_from_jarvis_ = false
   4. 释放 LVGL 锁
   5. 如果 divination_callback_ 不为空:
      - 获取占卜结果
      - 调用 divination_callback_(result)
   ```

4. **修改 `FinishFortuneDivinationUnlocked()`**:
   - 在方法末尾添加延迟调用 `SwitchBackFromDivination()`
   - 延迟时间：2 秒（让用户看到占卜结果）

### 3.4 阶段三：更新 MCP 工具实现

**修改文件**: [mcp_server.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/mcp_server.cc)

**更新 `self.screen.display_gif` 工具**:
```
修改回调逻辑：
1. HTTP 下载图片（保持不变）
2. 创建 LvglImage 对象（保持不变）
3. 获取 attitude_display（使用 dynamic_cast）
4. 如果 attitude_display 不为空:
   - 调用 attitude_display->ShowImageOnActiveView(image, 5000)
5. 否则:
   - 调用 display->SetPreviewImage(image)
```

**更新 `self.attitude.start_divination` 工具**:
```
修改回调逻辑：
1. 调用 attitude->SwitchToDivination() 替代 attitude->StartFortuneDivination()
2. 返回提示信息："占卜已开始，请等待跑马灯结束后获取结果。"
```

**保持 `self.attitude.get_divination_result` 工具**:
- 保持现有实现不变

### 3.5 阶段四：应用层状态联动

**修改文件**: [application.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/application.cc)

**添加占卜结束回调注册**:
```cpp
// 在 Initialize 或 SetupUI 代码中
auto* attitude = GetAttitudeDisplay();
if (attitude != nullptr) {
    attitude->SetDivinationCallback([this](int result) {
        // 占卜结束后，通过 MCP 广播通知后端获取结果
        if (mcp_broadcast_callback_) {
            cJSON* json = cJSON_CreateObject();
            cJSON_AddStringToObject(json, "type", "divination_result");
            cJSON_AddNumberToObject(json, "result", result);
            char* json_str = cJSON_PrintUnformatted(json);
            mcp_broadcast_callback_(json_str);
            cJSON_free(json_str);
            cJSON_Delete(json);
        }
    });
}
```

---

## 四、端到端流程改造

### 4.1 场景一：唤醒显示 JARVIS

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
        ├─ lv_obj_add_flag(attitude_container_, LV_OBJ_FLAG_HIDDEN)
        ├─ lv_screen_load(overlay_screen_)
        ├─ fortune_watchface_visible_ = true
        └─ lv_timer_resume(timer_) → 启动 JARVIS HUD 动画
    │
    ▼
[Java] WebSocket 连接 → STT → LLM → TTS
    │
    ▼
[ESP32] 播放 TTS 回复
```

### 4.2 场景二：显示 GIF 图片

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
            │     FortuneWatchfaceView::ShowImage(img_dsc, is_gif)
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

### 4.3 场景三：开始占卜

```
用户: 开始占卜
    │
    ▼
[Java] STT → LLM → MCP Tool Call: self.attitude.start_divination()
    │
    ▼
[ESP32] attitude->SwitchToDivination()
    │
    ├─ divination_from_jarvis_ = true
    │
    ├─ HideJarvisWatchface()
    │     ├─ lv_timer_pause(timer_)
    │     ├─ lv_obj_add_flag(overlay_screen_, LV_OBJ_FLAG_HIDDEN)
    │     ├─ lv_screen_load(prev_screen_)
    │     └─ lv_obj_remove_flag(attitude_container_, LV_OBJ_FLAG_HIDDEN)
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
    ├─ if divination_from_jarvis_:
    │     ShowJarvisWatchface()
    │     ├─ lv_obj_add_flag(attitude_container_, LV_OBJ_FLAG_HIDDEN)
    │     ├─ lv_screen_load(overlay_screen_)
    │     ├─ fortune_watchface_visible_ = true
    │     └─ lv_timer_resume(timer_)
    │
    ├─ divination_from_jarvis_ = false
    │
    └─ divination_callback_(result) → MCP 广播通知后端
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

---

## 五、关键实现细节

### 5.1 FortuneWatchfaceView::ShowImage()

```cpp
void FortuneWatchfaceView::ShowImage(const lv_img_dsc_t* img_dsc, bool is_gif, uint32_t timeout_ms) {
    if (!lvgl_port_lock(300)) {
        ESP_LOGW(TAG, "ShowImage: LVGL lock timeout");
        return;
    }

    // 停止图片隐藏定时器
    if (image_hide_timer_ != nullptr) {
        lv_timer_del(image_hide_timer_);
        image_hide_timer_ = nullptr;
    }

    // 停止之前的 GIF
    if (gif_controller_ != nullptr) {
        delete gif_controller_;
        gif_controller_ = nullptr;
    }

    // 设置图片源
    if (image_widget_ != nullptr && img_dsc != nullptr) {
        lv_image_set_src(image_widget_, img_dsc);
    }

    // 如果是 GIF，启动 GIF 控制器
    if (is_gif && gif_raw_data_ != nullptr && gif_raw_size_ > 0) {
        gif_controller_ = new LvglGif(image_widget_, gif_raw_data_, gif_raw_size_);
        gif_controller_->Start(true);
    }

    // 暂停 JARVIS HUD 动画
    if (timer_ != nullptr) {
        lv_timer_pause(timer_);
    }

    // 显示图片覆盖层（淡入动画）
    if (image_overlay_ != nullptr) {
        lv_obj_remove_flag(image_overlay_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_opa(image_overlay_, 0, 0);
        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, image_overlay_);
        lv_anim_set_values(&anim, 0, LV_OPA_COVER);
        lv_anim_set_time(&anim, 300);
        lv_anim_set_exec_cb(&anim, [](void* var, int32_t value) {
            lv_obj_set_style_opa((lv_obj_t*)var, value, 0);
        });
        lv_anim_start(&anim);
    }

    // 设置图片隐藏定时器
    image_hide_timer_ = lv_timer_create([](lv_timer_t* timer) {
        auto* self = static_cast<FortuneWatchfaceView*>(lv_timer_get_user_data(timer));
        if (self != nullptr) {
            self->HideImage();
        }
    }, timeout_ms, this);

    ESP_LOGI(TAG, "ShowImage: displayed image, timeout=%dms", timeout_ms);

    lvgl_port_unlock();
}
```

### 5.2 FortuneWatchfaceView::HideImage()

```cpp
void FortuneWatchfaceView::HideImage() {
    if (!lvgl_port_lock(300)) {
        ESP_LOGW(TAG, "HideImage: LVGL lock timeout");
        return;
    }

    // 停止图片隐藏定时器
    if (image_hide_timer_ != nullptr) {
        lv_timer_del(image_hide_timer_);
        image_hide_timer_ = nullptr;
    }

    // 停止 GIF 控制器
    if (gif_controller_ != nullptr) {
        gif_controller_->Stop();
        delete gif_controller_;
        gif_controller_ = nullptr;
    }

    // 隐藏图片覆盖层（淡出动画）
    if (image_overlay_ != nullptr) {
        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, image_overlay_);
        lv_anim_set_values(&anim, LV_OPA_COVER, 0);
        lv_anim_set_time(&anim, 300);
        lv_anim_set_exec_cb(&anim, [](void* var, int32_t value) {
            lv_obj_set_style_opa((lv_obj_t*)var, value, 0);
        });
        lv_anim_set_ready_cb(&anim, [](lv_anim_t* anim) {
            lv_obj_add_flag((lv_obj_t*)lv_anim_get_var(anim), LV_OBJ_FLAG_HIDDEN);
        });
        lv_anim_start(&anim);
    }

    // 恢复 JARVIS HUD 动画
    if (timer_ != nullptr && visible_) {
        lv_timer_resume(timer_);
    }

    ESP_LOGI(TAG, "HideImage: image hidden");

    lvgl_port_unlock();
}
```

### 5.3 AttitudeDisplay::ShowImageOnActiveView()

```cpp
void AttitudeDisplay::ShowImageOnActiveView(std::unique_ptr<LvglImage> image, uint32_t timeout_ms) {
    DisplayLockGuard lock(this);

    // 清理之前的图片
    if (preview_image_hide_timer_ != nullptr) {
        lv_timer_del(preview_image_hide_timer_);
        preview_image_hide_timer_ = nullptr;
    }

    if (image == nullptr) {
        ESP_LOGW(TAG, "ShowImageOnActiveView: null image");
        return;
    }

    if (fortune_watchface_visible_) {
        // JARVIS 视图可见，在 JARVIS 上显示图片
        auto img_dsc = image->image_dsc();
        bool is_gif = image->IsGif();
        
        // 保存 GIF 原始数据供 JARVIS 使用
        if (is_gif) {
            FortuneWatchfaceView::GetInstance().SetGifRawData(
                const_cast<uint8_t*>(image->data()), image->size());
        }
        
        FortuneWatchfaceView::GetInstance().ShowImage(img_dsc, is_gif, timeout_ms);
        ESP_LOGI(TAG, "ShowImageOnActiveView: displayed on JARVIS view");
    } else {
        // JARVIS 视图不可见，在主屏幕显示
        SetPreviewImageUnlocked(std::move(image), timeout_ms);
        ESP_LOGI(TAG, "ShowImageOnActiveView: displayed on main screen");
    }
}
```

### 5.4 AttitudeDisplay::SwitchToDivination()

```cpp
void AttitudeDisplay::SwitchToDivination() {
    DisplayLockGuard lock(this);

    // 如果 JARVIS 可见，记录并隐藏
    if (fortune_watchface_visible_) {
        divination_from_jarvis_ = true;
        HideJarvisWatchface();
        ESP_LOGI(TAG, "SwitchToDivination: JARVIS hidden");
    } else {
        divination_from_jarvis_ = false;
    }

    // 开始占卜
    StartFortuneDivination();
    ESP_LOGI(TAG, "SwitchToDivination: divination started");
}
```

### 5.5 AttitudeDisplay::SwitchBackFromDivination()

```cpp
void AttitudeDisplay::SwitchBackFromDivination() {
    DisplayLockGuard lock(this);

    // 停止占卜
    StopFortuneDivination();
    ESP_LOGI(TAG, "SwitchBackFromDivination: divination stopped");

    // 如果是从 JARVIS 进入的占卜，重新显示 JARVIS
    if (divination_from_jarvis_) {
        ShowJarvisWatchface();
        divination_from_jarvis_ = false;
        ESP_LOGI(TAG, "SwitchBackFromDivination: JARVIS shown");
    }

    // 触发占卜结果回调
    int result = GetFortuneDivinationResult();
    if (divination_callback_ != nullptr) {
        divination_callback_(result);
        ESP_LOGI(TAG, "SwitchBackFromDivination: callback triggered, result=%d", result);
    }
}
```

### 5.6 修改 FinishFortuneDivinationUnlocked()

```cpp
void AttitudeDisplay::FinishFortuneDivinationUnlocked(int result_index) {
    // ... 原有代码 ...

    // 延迟 2 秒后切换回 JARVIS 视图（如果是从 JARVIS 进入的）
    if (divination_from_jarvis_) {
        lv_timer_create([](lv_timer_t* timer) {
            auto* self = static_cast<AttitudeDisplay*>(lv_timer_get_user_data(timer));
            if (self != nullptr) {
                self->SwitchBackFromDivination();
            }
            lv_timer_del(timer);
        }, 2000, this);
    } else {
        // 非 JARVIS 场景，直接触发回调
        if (divination_callback_ != nullptr) {
            divination_callback_(result_index);
        }
    }
}
```

---

## 六、风险与注意事项

### 6.1 内存管理

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| GIF 解码占用内存 | 应用崩溃 | 使用 PSRAM 分配 buffer，限制图片大小 < 100KB |
| 多视图叠加导致内存不足 | 应用崩溃 | 切换视图时释放前一视图资源 |
| 图片缓存未及时释放 | 内存泄漏 | 在 HideImage() 中清理 image_widget_ 和 GIF 资源 |
| GIF 原始数据重复拷贝 | 内存浪费 | 使用指针共享，避免拷贝 |

### 6.2 LVGL 线程安全

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| 视图切换时的 LVGL 锁竞争 | 死锁或卡顿 | 所有 LVGL 操作使用 `DisplayLockGuard` 或 `lvgl_port_lock()` |
| 定时器回调与切换冲突 | 状态错乱 | 在关键路径暂停定时器，使用互斥锁保护 |
| 动画回调与视图切换冲突 | 白屏或闪烁 | 动画结束后再执行视图切换 |
| 占卜结束延迟定时器与其他操作冲突 | 状态错乱 | 使用一次性定时器，执行后立即删除 |

### 6.3 状态一致性

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| 视图状态与 fortune_watchface_visible_ 不一致 | 显示错误 | 所有视图切换通过 AttitudeDisplay 方法进行，统一更新标志 |
| divination_from_jarvis_ 状态未正确重置 | 状态错乱 | 在 SwitchBackFromDivination() 中重置为 false |
| 网络中断导致 LLM 回复丢失 | 无语音回复 | 添加超时重试机制，显示错误提示 |
| 占卜结束回调未触发 | 无结果播报 | 在 FinishFortuneDivinationUnlocked 中强制调用 |

### 6.4 兼容性

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| 非语音唤醒场景（触摸占卜） | 状态错乱 | 保留原有的触摸触发路径，判断是否来自语音交互 |
| 已有 MCP 工具调用 | 功能失效 | 更新工具但保持接口兼容，返回相同格式的结果 |
| 旧固件升级 | 数据不兼容 | 确保 NVS 数据格式兼容 |

---

## 七、实施步骤

| 步骤 | 内容 | 涉及文件 | 验证方式 |
|------|------|----------|----------|
| 1 | JARVIS 视图添加图片覆盖层 UI 创建 | fortune_watchface_view.h/.cc | 编译通过 |
| 2 | JARVIS 视图实现 ShowImage/HideImage | fortune_watchface_view.cc | 编译通过 |
| 3 | AttitudeDisplay 添加视图切换协调方法 | attitude_display.h/.cc | 编译通过 |
| 4 | 更新 MCP 工具实现 | mcp_server.cc | 编译通过 |
| 5 | 应用层添加占卜回调注册 | application.cc | 编译通过 |
| 6 | 编译验证 | - | 固件生成成功 |
| 7 | 烧录测试 | - | 设备启动正常 |
| 8 | 真机功能验证（三个场景） | - | 功能测试通过 |

---

## 八、验证标准

### 8.1 功能验证

| 场景 | 步骤 | 预期结果 |
|------|------|----------|
| 唤醒显示 JARVIS | 1. 说"贾维斯" | JARVIS HUD 显示，无白屏 |
| JARVIS 期间显示 GIF | 1. 唤醒 2. 说"显示图片" | GIF 在 JARVIS 视图上显示 |
| GIF 5秒后自动隐藏 | 1. 显示 GIF 2. 等待 5 秒 | GIF 自动隐藏，JARVIS 动画恢复 |
| GIF 隐藏后语音提示 | 1. 显示 GIF 2. 等待 5 秒 | 语音提示"图片已显示" |
| JARVIS 期间开始占卜 | 1. 唤醒 2. 说"开始占卜" | 切换到罗盘，跑马灯动画 |
| 占卜结束返回 JARVIS | 1. 开始占卜 2. 等待结束 | 切换回 JARVIS 视图 |
| 占卜结果语音播报 | 1. 占卜结束 | 语音播报占卜结果 |

### 8.2 边界测试

| 场景 | 步骤 | 预期结果 |
|------|------|----------|
| 连续唤醒 | 1. 唤醒 2. 交互结束 3. 再次唤醒 | 每次正常显示/隐藏 |
| 连续占卜 | 1. 唤醒 2. 占卜 3. 返回 4. 再次占卜 | 每次正常切换 |
| 网络中断恢复 | 1. 唤醒 2. 断网 3. 恢复 | 恢复后正常工作 |
| 非语音触发占卜 | 1. 长按太极 3 秒 | 正常占卜，不影响 JARVIS 状态 |

### 8.3 性能验证

| 测试项 | 标准 |
|--------|------|
| 视图切换时间 | < 200ms |
| 内存占用增加 | < 100KB |
| 视图切换无白屏 | 无闪烁 |

---

## 九、依赖与前置条件

- ESP32 设备已烧录最新固件
- 后端服务（8091 + 8092）正常运行
- STT/TTS/LLM 配置正确
- FunASR 容器正常运行（如使用）
- LVGL 版本 >= 9.x（当前项目使用）
- LvglGif 类已实现 GIF 播放功能

---

## 十、待确认事项

| 编号 | 事项 | 说明 |
|------|------|------|
| Q-001 | GIF 图片来源 | 是否支持本地文件和远程 URL 两种方式？当前 MCP 工具仅支持 URL |
| Q-002 | 占卜结束延迟 | 跑马灯结束后是否需要延迟 2 秒再切换回 JARVIS？（当前计划为 2 秒） |
| Q-003 | 语音播报触发 | "图片已显示"和"占卜结果"的语音播报是由 LLM 生成还是本地 TTS？ |
| Q-004 | 错误处理 | GIF 下载失败或图片格式不支持时如何处理？ |
| Q-005 | 图片显示尺寸 | JARVIS 视图上的图片覆盖层尺寸和位置如何确定？ |

---

## 十一、代码审查检查清单

### 11.1 FortuneWatchfaceView 修改检查

- [ ] `image_overlay_` 在 `CreateUI()` 中创建，初始隐藏
- [ ] `image_widget_` 在 `CreateUI()` 中创建，初始隐藏
- [ ] `ShowImage()` 使用 LVGL 锁保护
- [ ] `ShowImage()` 暂停 JARVIS HUD 动画
- [ ] `ShowImage()` 支持 GIF 和静态图片
- [ ] `HideImage()` 使用 LVGL 锁保护
- [ ] `HideImage()` 恢复 JARVIS HUD 动画
- [ ] `HideImage()` 清理 GIF 资源
- [ ] 图片隐藏定时器正确释放
- [ ] 析构函数中清理所有新增资源

### 11.2 AttitudeDisplay 修改检查

- [ ] `ShowImageOnActiveView()` 根据 JARVIS 可见状态选择显示位置
- [ ] `SwitchToDivination()` 正确记录 `divination_from_jarvis_`
- [ ] `SwitchBackFromDivination()` 正确重置 `divination_from_jarvis_`
- [ ] `FinishFortuneDivinationUnlocked()` 延迟调用 `SwitchBackFromDivination()`
- [ ] 占卜回调在正确时机触发
- [ ] 所有 LVGL 操作使用 `DisplayLockGuard`

### 11.3 MCP Server 修改检查

- [ ] `self.screen.display_gif` 调用 `ShowImageOnActiveView()`
- [ ] `self.attitude.start_divination` 调用 `SwitchToDivination()`
- [ ] 工具返回值格式保持兼容
- [ ] 错误处理完善

### 11.4 Application 修改检查

- [ ] 占卜回调注册正确
- [ ] MCP 广播通知正确
- [ ] 状态联动逻辑正确

---

## 12. 实现偏差修复记录 (v3.0 → v3.1)

为消除 plan 与实际实现之间的偏差，于 2026-07-12 完成以下修复：

| 编号 | 偏差内容 | 修复方式 | 文件 |
|------|---------|---------|------|
| D-001 | Plan 提到的 `SetGifRawData()` 路径不可行 | 不再使用，标记废弃 | - |
| D-002 | `LvglGif` 构造函数签名与 plan 不一致 | 改用 `LvglGif(const lv_img_dsc_t* img_dsc)` | `fortune_watchface_view.cc` |
| D-003 | `LvglGif::Start(true)` vs `Start()` | 改用 `Start()` | `fortune_watchface_view.cc` |
| D-004 | `lv_anim_get_var` vs `lv_anim_get_user_data` | 改用 `lv_anim_get_user_data` / `lv_anim_set_user_data` | `fortune_watchface_view.cc` |
| D-005 | `DestroyUI()` 未清理 `gif_controller_` 和 `image_hide_timer_` | 已在 `DestroyUI()` 中添加 `lv_timer_del` + `Stop()` + `delete` | `fortune_watchface_view.cc` |
| D-006 | Application 回调未实现 MCP 广播通知 | 已实现 cJSON + SendMcpMessage 通知后端 `divination_result` | `application.cc` |
| D-007 | 图片覆盖层背景色 `0x0A0A0A` 与 spec `0x0A1414` 不一致 | 改为 `0x0A1414` | `fortune_watchface_view.cc` |
| D-008 | 视图切换无 200ms 淡入淡出过渡 | 实现 `FadeViewTransitionUnlocked(from, to, 200)` | `attitude_display.cc` / `.h` |
| D-009 | 未实现 `ActiveView` 枚举和 `ViewStack` 结构 | 已添加 enum / struct，并在 `SwitchTo/BackFromDivination` 维护栈 | `attitude_display.h` / `.cc` |

**API 实际签名** (供后续维护参考):
- `LvglGif(const lv_img_dsc_t* img_dsc)` - 仅接受 image descriptor
- `LvglGif::Start()` - 无参数
- `lv_anim_set_user_data(anim, ptr)` / `lv_anim_get_user_data(anim)` - LVGL 9.x API
- `LvglImage::image_dsc()` / `LvglImage::IsGif()` - 替代 `data()` / `size()`

---

**文档版本**: v3.1  
**最后更新**: 2026-07-12  
**状态**: 待审核