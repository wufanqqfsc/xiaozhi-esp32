# JARVIS Watchface 唤醒界面整合计划

## 需求概述

将 JARVIS Watchface 整合到 xiaozhi-esp32 项目中，作为贾维斯语音唤醒时的独立显示界面。

---

## 实现进度

### ✅ 已完成

1. **JarvisWatchface 类实现**
   - [jarvis_watchface.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/jarvis_watchface.h) - 头文件定义
   - [jarvis_watchface.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/jarvis_watchface.cc) - 完整实现

2. **核心功能**
   - 360×360 圆形屏幕适配
   - 3 层能量环（外/中/内）带独立旋转
   - 45 个动态粒子系统
   - 呼吸脉动核心（青色发光）
   - 5 种状态机（Sleep/Starting/Active/Listening/Speaking）
   - Canvas 直接像素绘制

### ⏳ 待完成

1. **编译集成**
   - 在 `main/CMakeLists.txt` 中添加 `display/jarvis_watchface.cc`

2. **AttitudeDisplay 集成**
   - 在 `attitude_display.h` 中添加 `#include "jarvis_watchface.h"`
   - 添加成员变量 `JarvisWatchface& jarvis_watchface_`
   - 实现唤醒回调时调用 `jarvis_watchface_.Show()`
   - 实现聆听状态时调用 `jarvis_watchface_.SetState(Listening)`
   - 实现说话状态时调用 `jarvis_watchface_.SetState(Speaking)`
   - 返回空闲时调用 `jarvis_watchface_.Hide()`

3. **编译测试**
   - 编译验证无错误
   - 烧录到设备

---

## 架构研究结论

### 现有架构
- **AttitudeDisplay** 是主显示类，管理八卦/太极 UI
- **设备状态** 通过 `DeviceState` 枚举管理：`kDeviceStateIdle`、`kDeviceStateWakeup`、`kDeviceStateListening`、`kDeviceStateSpeaking`
- **唤醒动画** 目前使用 `ark-reactor-normal.gif`，显示在 300x300 圆形浮层上

### JARVIS Watchface 特性
- 360×360 圆形屏幕完整覆盖
- LVGL Canvas 实现，多层发光弧动画
- 33ms 刷新周期的流畅动画
- 青色霓虹风格 (#20eaff, #ffd447)

---

## 下一步实现步骤

### Step 1: 添加到 CMakeLists.txt

在 `main/CMakeLists.txt` 第 22 行后添加：

```cmake
"display/jarvis_watchface.cc"
```

### Step 2: 集成到 AttitudeDisplay

在 `main/display/attitude_display.h` 中：

```cpp
#include "jarvis_watchface.h"

// 在 private 成员中添加：
JarvisWatchface& jarvis_watchface_;

// 添加公开方法：
void ShowJarvisWatchface();
void HideJarvisWatchface();
```

在 `main/display/attitude_display.cc` 中：

```cpp
// 构造函数中初始化：
jarvis_watchface_(JarvisWatchface::GetInstance())

// 覆盖或扩展状态处理：
void AttitudeDisplay::ShowJarvisWatchface() {
    jarvis_watchface_.Show();
}

void AttitudeDisplay::HideJarvisWatchface() {
    jarvis_watchface_.Hide();
}
```

### Step 3: 连接到 Application 状态机

在 `main/application.cc` 中，根据设备状态调用相应的 JARVIS Watchface 方法。

---

## 文件清单

### 新增文件
- `main/display/jarvis_watchface.h` ✅
- `main/display/jarvis_watchface.cc` ✅

### 修改文件
- `main/CMakeLists.txt` ⏳
- `main/display/attitude_display.h` ⏳
- `main/display/attitude_display.cc` ⏳
- `main/application.cc` ⏳（可选，用于状态联动）

---

## 风险与备选方案

| 风险 | 缓解措施 |
|------|----------|
| Canvas 绘制性能 | 可降帧到 50ms 或简化粒子数量 |
| 内存占用 | 360×360×4 = 518KB，已使用 PSRAM |
| 与 AttitudeDisplay 冲突 | 使用独立的 screen_，切换时 lv_screen_load |
