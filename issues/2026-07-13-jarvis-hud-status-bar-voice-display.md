# JARVIS HUD 状态栏显示语音交互文本 + function_area_card_ 路由重构

> 记录时间：2026-07-13 16:00
> 关联分支：`xiaozhi-esp32 main`
> 关联提交：`36a9e10 update` (本轮修改合入) / `5c42ccd feat(display): SD card Jarvis display`

---

## 一、需求与目标

### 1.1 原始问题

用户语音唤醒设备进入 JARVIS HUD 视图后，**所有需要显示给用户的文本信息**（server 返回的消息、状态通知、系统消息等）仍然通过罗盘主界面的 `function_area_card_` 显示，造成：
1. **视图未真正切换**：`function_area_card_` 实际位于 `attitude_container_`（罗盘主屏幕），JARVIS HUD 是 `overlay_screen_`（独立屏幕），切换后 function_area_card_ 在原屏幕中不可见，但**新事件入队**后，**定时器回调**会再次触发它的显示
2. **视觉体验割裂**：语音交互过程中看不到流式到达的 server 消息
3. **UI 性能浪费**：每次都走 DebugInfoCard 的完整创建/销毁流程

### 1.2 目标

1. JARVIS HUD 视图可见时，**所有**文本信息（server 消息、状态、通知、调试信息、运势菜单卡）一律走 `status_label_` 显示
2. 状态栏文本**支持左右滚动**（`LV_LABEL_LONG_SCROLL_CIRCULAR`），长消息自动循环显示
3. 状态栏**加宽加高**支持两行显示，**字体缩小**至 14px 以容纳更多字符
4. 消息格式统一化：`#AI:内容` / `#你:内容` / `#系统:内容`

---

## 二、关键技术分析

### 2.1 FortuneWatchfaceView 状态栏原状

| 属性 | 原值 | 影响 |
|------|------|------|
| 位置 (x, y) | (54, 284) | 距离屏幕底部 40px |
| 尺寸 (w, h) | 252 × 36 px | 单行 30px 字体，约 8 字符/行 |
| 字体 | BUILTIN_TEXT_FONT (30px) | 单字符宽 ~16px |
| 内容 | `ESP32-S3 JARVIS HUD SCAN XX%` | 仅显示扫描进度 |

### 2.2 function_area_card_ 触发链路分析

**触发入口**（按调用频次）：

| 入口 | 调用位置 | 行为 |
|------|---------|------|
| `SetChatMessage(role, content)` | attitude_display.cc:370 | 路由到 ShowDebugInfo |
| `SetStatus(status)` | attitude_display.cc:342 | 路由到 ShowDebugInfo |
| `ShowNotification(msg, hold_ms)` | attitude_display.cc | 路由到 ShowDebugInfo |
| `ShowDebugInfo(title, detail, hold_ms)` | attitude_display.cc:2294 | 创建 DebugInfoCard 并显示 |
| `PresentDebugInfoCardUnlocked(title, detail)` | attitude_display.cc:2112 | 直接显示 DebugInfoCard（绕开 ShowDebugInfo） |
| `DisplayDebugInfoCard(title, detail)` | attitude_display.cc:2213 | 底层 UI 更新（被 PopAndShowNext 调用） |

**关键调用链**：
```
ShowDebugInfo (入口)
  → DisplayDebugInfoCard (立即显示)
  → EnqueueItem → lv_timer_create(OnDebugInfoTimer)
  → 定时器到期 → PopAndShowNext → DisplayDebugInfoCard (再次显示)
```

**根因**：旧实现中只有 `ShowDebugInfo` 入口检查 `fortune_watchface_visible_`，但：
1. `PresentDebugInfoCardUnlocked` 是**直接调用路径**（运势菜单功能卡），绕过 ShowDebugInfo
2. `DisplayDebugInfoCard` 是**定时器回调路径**（`OnDebugInfoTimer → PopAndShowNext → DisplayDebugInfoCard`），绕过 ShowDebugInfo
3. 这两个路径都会让 `function_area_card_` 在 JARVIS HUD 可见时**绕过 JARVIS 路由**，直接显示在罗盘主屏幕

### 2.3 滚动显示原理

`LV_LABEL_LONG_SCROLL_CIRCULAR` 是 LVGL 内置的循环滚动模式：
- **触发条件**：`lv_obj_set_width(label, W)` 限制宽度，且文本实际宽度 > W
- **滚动行为**：从右向左持续滚动，超出左边界后从右边界循环出现
- **性能**：由 LVGL tick 任务统一驱动，无需应用层介入

字体宽度估算（`font_puhui_14_1`，14px）：
- 中文：~14 px/字符
- ASCII：~7 px/字符
- 单行 270px 可容纳：约 19 个中文字符 或 38 个 ASCII 字符
- 两行：约 38 个中文字符 或 76 个 ASCII 字符

---

## 三、修复方案

### 3.1 状态栏 UI 升级

**文件**：`main/display/fortune_watchface_view.cc`

```cpp
// 状态栏（向上移动 20px，加宽加高，支持两行显示和滚动）
// 位置 y=264（原 284），宽度 290（原 252），高度 56（原 36），容纳两行 14px 文本
lv_obj_t* status_bar = AddBox(screen, 35, 264, 290, 56, 0x07182b, LV_RADIUS_CIRCLE);
// ... 边框/阴影/内边距/行间距 ...

status_label_ = lv_label_create(status_bar);
lv_label_set_text(status_label_, "ESP32-S3  PSRAM 8M  BAT 96%");
// 使用 SCROLL_CIRCULAR 模式：单行超出自动左右循环滚动
lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
// 字体缩小到 14px 以容纳更多字符
lv_obj_set_style_text_font(status_label_, &font_puhui_14_1, 0);
lv_obj_set_style_text_color(status_label_, lv_color_hex(0xc8f7ff), 0);
// 设置高度支持两行（14px 行高 * 2 + 行间距 = ~30px）
lv_obj_set_height(status_label_, 44);
lv_obj_set_width(status_label_, 270);
lv_obj_center(status_label_);
```

### 3.2 状态栏位置对比

| 属性 | 原值 | 新值 | 收益 |
|------|------|------|------|
| y 坐标 | 284 | **264** | 向上 20px |
| 宽度 | 252 | **290** | +38px，靠近屏幕边 |
| 高度 | 36 | **56** | +20px，容纳两行 |
| 字体 | BUILTIN_TEXT_FONT (30px) | **font_puhui_14_1 (14px)** | -16px/字符 |
| 单行字符数 | ~8 中文字 | **~19 中文字** | +137% |
| 双行字符数 | - | **~38 中文字** | 显著提升 |

### 3.3 JARVIS 视图外环（与罗盘主界面保持一致）

**文件**：`main/display/fortune_watchface_view.cc`、`main/display/attitude_display.cc`

| 颜色 | 触发条件 | RGB |
|------|---------|-----|
| 金色 `COLOR_TEXT_MAIN` | 默认 | `0xD4AF37` |
| 青色 `COLOR_WIFI_GREEN` | WiFi 已连接 | `0x00FFFF` |
| 蓝色 `COLOR_BT_BLUE` | BLE 已连接 | `0x2196F3` |

**AttitudeDisplay 同步**：
```cpp
void AttitudeDisplay::UpdateOuterRingColor() {
    lv_color_t color = COLOR_TEXT_MAIN;
    if (wifi_status_ == WifiStatus::CONNECTED) color = COLOR_WIFI_GREEN;
    else if (ble_status_ == BleStatus::CONNECTED) color = COLOR_BT_BLUE;

    if (layer4_outer_ring_ != nullptr) {
        lv_obj_set_style_arc_color(layer4_outer_ring_, color, LV_PART_INDICATOR);
    }
    // 同步更新 JARVIS HUD 视图的外环颜色（如果可见）
    if (fortune_watchface_visible_) {
        FortuneWatchfaceView::GetInstance().UpdateOuterRingColor(color);
    }
    if (!taiji_rotation_paused_by_press_) {
        UpdateTaijiGoldRingColor(color);
    }
}
```

### 3.4 消息路由重构（核心修复）

**所有 function_area_card_ 显示路径在 JARVIS HUD 可见时统一路由到 status_label_**：

```cpp
// AttitudeDisplay::SetStatus (attitude_display.cc:342)
void AttitudeDisplay::SetStatus(const char* status) {
    if (fortune_watchface_visible_) {
        FortuneWatchfaceView::GetInstance().SetStatusText(status);
        return;
    }
    DisplayLockGuard lock(this);
    ShowDebugInfo("状态", std::string(status), 5000);
}

// AttitudeDisplay::SetChatMessage (attitude_display.cc:370)
void AttitudeDisplay::SetChatMessage(const char* role, const char* content) {
    if (fortune_watchface_visible_) {
        // 根据 role 添加前缀，便于辨识消息来源
        std::string prefixed;
        if (strcmp(role, "assistant") == 0) {
            prefixed = std::string("#AI:") + content;
        } else if (strcmp(role, "user") == 0) {
            prefixed = std::string("#你:") + content;
        } else {
            prefixed = std::string("#系统:") + content;
        }
        FortuneWatchfaceView::GetInstance().SetVoiceMessage(prefixed.c_str());
        return;
    }
    // ...
}

// AttitudeDisplay::ShowDebugInfo (attitude_display.cc:2294)
void AttitudeDisplay::ShowDebugInfo(const std::string& title, const std::string& detail, uint32_t hold_ms) {
    // JARVIS HUD 可见时：所有调试信息直接走 status_label_
    if (fortune_watchface_visible_) {
        std::string combined;
        if (!title.empty()) combined = title + ":" + detail;
        else combined = detail;
        FortuneWatchfaceView::GetInstance().SetVoiceMessage(combined.c_str());
        return;
    }
    // ... 原队列调度逻辑 ...
}

// AttitudeDisplay::PresentDebugInfoCardUnlocked (attitude_display.cc:2112)
void AttitudeDisplay::PresentDebugInfoCardUnlocked(const std::string& title, ...) {
    // JARVIS HUD 可见时：路由到 status_label_
    if (fortune_watchface_visible_) {
        std::string combined = title + "\n" + detail;
        FortuneWatchfaceView::GetInstance().SetVoiceMessage(combined.c_str());
        return;
    }
    // ... 原显示逻辑 ...
}

// AttitudeDisplay::DisplayDebugInfoCard (attitude_display.cc:2213)
void AttitudeDisplay::DisplayDebugInfoCard(const std::string& title, const std::string& detail) {
    // JARVIS HUD 可见时：直接走 status_label_，避免 function_area_card_ 显示
    if (fortune_watchface_visible_) {
        std::string combined;
        if (!title.empty()) combined = title + ":" + detail;
        else combined = detail;
        FortuneWatchfaceView::GetInstance().SetVoiceMessage(combined.c_str());
        return;
    }
    // ... 原 UI 更新逻辑 ...
}
```

**关键修复**：`DisplayDebugInfoCard` 是**定时器回调路径**的入口，必须也添加 JARVIS HUD 检查，否则旧入队事件仍会弹出显示。

### 3.5 FortuneWatchfaceView 新接口

**文件**：`main/display/fortune_watchface_view.h`、`main/display/fortune_watchface_view.cc`

```cpp
// 状态栏显示模式
enum StatusMode {
    kModeDefault,      // 默认模式：显示扫描进度
    kModeVoiceActive   // 语音交互模式：显示交互文本
};

// 通用接口
void SetStatusText(const char* text);   // 兼容 SetStatus 路径
void ClearStatusText();                  // 恢复默认模式
void SetVoiceMessage(const char* text);  // 专用消息接口（带滚动）
void ClearVoiceMessage();                // 清除消息文本
void UpdateOuterRingColor(lv_color_t color);  // 外环颜色同步
```

### 3.6 LVGL 锁超时调整（与 `2026-07-13-jarvis-wake-reboot-lvgl-lock.md` 联动）

```cpp
void FortuneWatchfaceView::SetStatusText(const char* text) {
    if (!lvgl_port_lock(300)) {  // 由 100 提升至 300
        ESP_LOGW(TAG, "SetStatusText: LVGL lock timeout");
        return;
    }
    // ...
}
```

理由：listening 期间 WS 回调与 AttitudeDisplay::SetStatus 同时调用 LVGL tick，100ms 短锁直接放弃并丢失消息；提升到 300ms 大概率能取得锁。

---

## 四、修改文件清单

| 文件 | 修改内容 |
|------|---------|
| `main/display/fortune_watchface_view.h` | 新增 `SetVoiceMessage/ClearVoiceMessage/UpdateOuterRingColor` 接口；新增 `StatusMode` 枚举和成员变量 |
| `main/display/fortune_watchface_view.cc` | 状态栏布局升级（y/宽/高/字体）；新增外环；实现 `SetVoiceMessage/ClearVoiceMessage/UpdateOuterRingColor`；锁超时 100→300 |
| `main/display/attitude_display.cc` | `SetStatus/SetChatMessage/ShowDebugInfo/PresentDebugInfoCardUnlocked/DisplayDebugInfoCard` 在 JARVIS 可见时路由到 status_label_；`UpdateOuterRingColor` 同步 JARVIS 外环；`ShowJarvisWatchface` 初始化外环颜色 |
| `main/application.cc` | 同步远程：跳过 OTA 检查以加快启动 |

---

## 五、消息格式约定

JARVIS HUD 可见时，状态栏显示格式：

| 来源 | 格式 | 示例 |
|------|------|------|
| server 返回 assistant | `#AI:内容` | `#AI:今天天气晴朗，最高温度 28 度` |
| server 返回 user (识别) | `#你:内容` | `#你:今天天气怎么样` |
| system 消息 | `#系统:内容` | `#系统:设备已激活` |
| SetStatus | `内容` | `聆听中...` |
| ShowNotification | `通知:内容` | `通知:HTTP 服务已启动` |
| 运势菜单功能卡 | `title\ndetail` | `财运\n1. 股票预测\n2. 理财延伸` |

---

## 六、性能优化收益

| 指标 | 修复前 | 修复后 |
|------|--------|--------|
| 语音交互中 UI 切换 | 罗盘 ↔ JARVIS 多次 | 无切换 |
| 每次消息 LVGL 操作 | 创建+销毁 3 个 label + 1 card | 仅 `lv_label_set_text` 1 次 |
| 长消息显示 | 截断/不显示 | 自动左右滚动 |
| 消息丢失风险 | 高（100ms 锁超时） | 低（300ms 锁超时） |

---

## 七、关联文档

- 历史问题：[2026-07-13-jarvis-wake-reboot-lvgl-lock.md](2026-07-13-jarvis-wake-reboot-lvgl-lock.md) — LVGL 锁竞争
- 项目规则：`xiaozhi-esp32/.trae/rules/rule_xiaozhi.md`
- 计划文档：`OpenMAIC/.trae/documents/plan_jarvis_status_bar_voice_interaction.md`

---

## 八、待验证项

- [x] 编译通过（`./build_and_flash.sh`）
- [x] 烧录成功（`Wrote 2851804 bytes`）
- [ ] 真实设备测试：
  - [ ] 唤醒后 status_label_ 显示"正在聆听..."（SetStatus 路径）
  - [ ] 用户说话后显示 `#你:识别内容`（SetChatMessage user 路径）
  - [ ] AI 回复时显示 `#AI:回复内容`（SetChatMessage assistant 路径）
  - [ ] 长消息自动滚动
  - [ ] 语音交互结束 status_label_ 恢复默认扫描进度
  - [ ] function_area_card_ 不再在 JARVIS 视图期间弹出
  - [ ] 外环颜色与罗盘主界面同步（WiFi 青色/BLE 蓝色/默认金色）
