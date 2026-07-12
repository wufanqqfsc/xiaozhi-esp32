# 小智 ESP32 界面交互逻辑汇总

> **项目**: xiaozhi-esp32（AI 罗盘派生分支）  
> **版本**: PROJECT_VER "2.2.6"  
> **生效板型**: waveshare/esp32-s3-touch-lcd-1.85b 主屏 360×360（AI 罗盘）  
> **文档定位**: 把本仓库所有用户可感知的界面（视觉层级、按键、触摸、状态机、网络/AI 事件、调试入口）一次性梳理清楚，作为 UI 迭代、问题排查、新界面扩展的统一参考

---

## 目录

1. [显示架构与 UI 分层](#1-显示架构与-ui-分层)
2. [核心屏幕：AI 罗盘 AttitudeDisplay](#2-核心屏幕ai-罗盘-attitudedisplay)
3. [覆盖层：JARVIS 启动视图 FortuneWatchfaceView](#3-覆盖层jarvis-启动视图-fortunewatchfaceview)
4. [上层调度：Application 中的 UI 反馈](#4-上层调度application-中的-ui-反馈)
5. [其它显示分支的简化交互](#5-其它显示分支的简化交互)
6. [串口调试入口：SnapshotService](#6-串口调试入口snapshotservice)
7. [完整交互时序图](#7-完整交互时序图)
8. [关键交互类/方法索引](#8-关键交互类方法索引)
9. [设计要点（实现时易踩的坑）](#9-设计要点实现时易踩的坑)
10. [后续可扩展点](#10-后续可扩展点)

---

## 1. 显示架构与 UI 分层

Display 类的继承体系：

```
Display (基类, display.h)
├── NoDisplay
├── SpiLcdDisplay / RgbLcdDisplay / MipiLcdDisplay (lcd_display.h)
├── OledDisplay (oled_display.cc)
└── EmoteDisplay (emote_display.cc)
        └── AttitudeDisplay (attitude_display.{h,cc}) ★ AI 罗盘（项目派生分支）
```

主要文件与职责：

| 组件 | 文件 | 主要职责 |
| --- | --- | --- |
| Display | [display.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/display.h) / [display.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/display.cc) | 统一接口 `SetStatus` / `ShowNotification` / `SetEmotion` / `SetChatMessage` / `SetTheme` / `UpdateStatusBar` / `SetPreviewImage` / `SetupUI`；提供 `DisplayLockGuard` 线程锁 |
| SpiLcdDisplay / LcdDisplay | [lcd_display.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/lcd_display.h) / [lcd_display.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/lcd_display.cc) | 状态栏 + 通知气泡 + 聊天消息气泡 + 表情 GIF + 预览图 |
| OledDisplay | [oled_display.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/oled_display.cc) | 小屏文字/Glyph 主题（注册 `"dark"` 主题） |
| EmoteDisplay | [emote_display.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/emote_display.cc) | 仅 `SetEmotion` 生效，用于表情机 |
| ★ AttitudeDisplay（AI 罗盘） | [attitude_display.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.h) / [attitude_display.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc) | 360×360 圆形屏；4 层背景 + 12 菜单环 + 太极图 + 调试信息卡 |
| ★ CompassTaiji（太极图组件） | [compass_taiji.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/compass_taiji.h) / [compass_taiji.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/compass_taiji.cc) | 阴阳鱼 LVGL canvas，支持旋转、自动转动、鱼眼绘制 |
| ★ FortuneWatchfaceView（JARVIS 启动视图） | [fortune_watchface_view.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/fortune_watchface_view.h) / [fortune_watchface_view.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/fortune_watchface_view.cc) | 选中"今日运势"时显示的 JARVIS HUD 全屏覆盖 |
| 串口调试 SnapshotService | [snapshot_service.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/snapshot/snapshot_service.h) / [snapshot_service.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/snapshot/snapshot_service.cc) | USB-Serial 提供 `SNAP` / `CLICK` / `PING` 协议 |
| BLE 鱼眼 BleServer | [ble_server.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/ble/ble_server.h) / [ble_server.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/ble/ble_server.cc) | NimBLE 外设，广播状态到罗盘「阳中阴」鱼眼 |

> 线程安全：所有写屏方法通过 `DisplayLockGuard` 入口加锁（`Lock(30000)`）；AttitudeDisplay 内部单独维护 LVGL 全局锁处理（见 [attitude_display.cc:1881](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L1881) 处的 `LockFn_` / `UnlockFn_` 重写）。

---

## 2. 核心屏幕：AI 罗盘 `AttitudeDisplay`

### 2.1 视觉分层（[attitude_display.h:6-37](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.h#L6-L37)）

| 层 | 内容 | 关键常量 |
| --- | --- | --- |
| L0 | 太极阴阳鱼 + 鎏金外圈（由 `CompassTaiji` 渲染） | `TAIJI_RADIUS=86` / `TAIJI_CANVAS_SIZE=172` / `TAIJI_GOLD_RING_WIDTH=3` |
| L0 鱼眼 | 阴中阳（WiFi, 上）/ 阳中阴（BLE, 下） | `FISHEYE_ICON_SIZE=32` / `FISHEYE_BORDER_WIDTH=2` / `FISHEYE_PULSE_MS=300` |
| L1-L3 | 三层同心圆装饰（外圈/中圈/内圈） | `BG_LAYER_CENTER_SIZE=270` |
| L4 | 贴屏幕圆边的金色边界圆环 | `LAYER4_BOUNDARY_RADIUS = SCREEN_W/2 - GOLD_RING_ARC_WIDTH/2` |
| ★ 菜单环 | 围绕太极的 12 个运势图标（35px 视觉 / 选中放大 10%） | `FORTUNE_MENU_COUNT=12` / `FORTUNE_MENU_RING_RADIUS` |
| ★ Debug Info 卡 | 中心 356px 圆盘，半透明青边标题 + 副文 + 进度环 | `DEBUG_INFO_CARD_SIZE = LAYER4_OUTER_SIZE` |

> 关键 invariant（编译期断言）：`static_assert(TAIJI_RADIUS == 86, ...)`、`static_assert(FISHEYE_ICON_SIZE == 32, ...)`。尺寸与产品规格强绑定，需修改时同步产品文档。

### 2.2 配色与层叠（取自 [attitude_display.cc:23-31](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L23-L31)）

```
COLOR_BG_OUTER  = 0x0A0A0A   外层黑底
COLOR_BG_CENTER = 0x121212   中心暗灰
COLOR_TEXT_MAIN = 0xD4AF37   鎏金（主文）
COLOR_TEXT_SUB  = 0xC0C0C0   银灰（副文）
COLOR_TEXT_HIGH = 0xFFFFFF   亮白（高亮）
COLOR_BORDER_LINE = 0xD4AF37 鎏金描边
COLOR_STATE_HEAVY  = 0xE67E22 橙色 (heavy)
COLOR_STATE_DANGER = 0xB82601 暗红 (danger)
COLOR_BT_BLUE         = 0x2196F3 蓝牙蓝
COLOR_WIFI_GREEN      = 0x00FFFF  WiFi 青（较旧版提升亮度）

DEBUG_INFO_BORDER_COLOR = 0x00C8C8  青色描边
DEBUG_INFO_TITLE_COLOR  = 0xD4AF37  金色
DEBUG_INFO_DETAIL_COLOR = 0xE0E0E0  银白副文
```

> UI 主题已固定为单主题黑金色系；`SetTheme` 调用被重写以避免主题切换破坏图面。

### 2.3 12 项运势菜单环（12 点钟起顺时针）

| Idx | 枚举（`FortuneMenuType`） | 名称 | 默认交互 |
| --- | --- | --- | --- |
| 0 | `Today` | 今日运势 | 选中 → `FortuneWatchfaceView::Show()` 覆盖 JARVIS HUD |
| 1 | `Wealth` | 财运 | — |
| 2 | `Career` | 事业 | — |
| 3 | `Love` | 爱情 | — |
| 4 | `MoodGua` | 心情卦 | 预留迷宫小游戏入口 |
| 5 | `Huangli` | 黄历 | — |
| 6 | `SolarTerm` | 节气 | — |
| 7 | `Custom` | 自定义 | — |
| 8 | `Health` | 健康 | — |
| 9 | `Study` | 学业 | — |
| 10 | `Travel` | 出行 | — |
| 11 | `Noble` | 贵人 | — |

触摸命中区：`FORTUNE_MENU_TOUCH_INNER_R = TAIJI_RADIUS - 4` 到 `LAYER4_BOUNDARY_RADIUS`（[attitude_display.cc:758-811](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L758-L811)）。

```cpp
// 环形触摸热区，命中后转极坐标选 idx
lv_obj_add_event_cb(fortune_menu_ring_touch_, OnFortuneMenuRingTouched, LV_EVENT_CLICKED, this);
```

选中态视觉：图标放大 10% + 颜色脉动反馈（`FORTUNE_MENU_ICON_SCALE` / `FORTUNE_MENU_ICON_SCALE_SELECTED`）。

### 2.4 按键与触摸交互入口

| 触发 | 入口函数 | 行为 |
| --- | --- | --- |
| **短按 BOOT 键**（Idle） | [HandleBootKey](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L2147-L2171) | 首次进入选中态（默认 0=今日运势）；之后循环 `CycleFortuneMenuSelectionUnlocked`；若处于 `Result` 态则退出占卜结果 |
| **长按 BOOT 键 ≥ 3s** | [HandleFortuneBootLongPress](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L2172-L2183) | 启动「今日占卜」跑马灯 |
| **短按电源键** | [HandlePowerKey](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L1474) | 取消选中态 + 隐藏功能区 + 中断占卜动画 |
| **触摸点击菜单环** | `OnFortuneMenuRingTouched` | 极坐标换算 → `SelectFortuneMenuItem(index)` |
| **触摸按住太极中心** | `OnTaijiDivinationPressed` / `OnTaijiDivinationReleased` | PRESSED 启动 hold timer (3s) → 占卜；RELEASED 视时长决定是否触发 |
| **AI 触发占卜** | [StartFortuneDivination](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L946) | 由 MCP 工具或语音指令调用；启动跑马灯动画 |

应用层封装（[application.cc:1119-1137](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/application.cc#L1119-L1137)）：

```cpp
bool Application::HandleFortuneBootKey()        { /* 转发给 AttitudeDisplay::HandleBootKey */        }
bool Application::HandleFortuneBootLongPress()  { /* 转发给 AttitudeDisplay::HandleFortuneBootLongPress */ }
bool Application::HandlePowerKey()              { /* 转发给 AttitudeDisplay::HandlePowerKey */ }
```

所有入口都先取 `Board::GetInstance().GetDisplay()`，若返回 `AttitudeDisplay` 再下发。

### 2.5 调试信息卡 `ShowDebugInfo`（[attitude_display.cc:2065-2125](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L2065-L2125)）

```cpp
void ShowDebugInfo(const std::string& title, const std::string& detail, uint32_t hold_ms = 3000);
void HideDebugInfo();
void RefreshDebugInfoTimer(uint32_t hold_ms = 0);  // 唤醒成功场景保持 N 秒
```

| 配置项 | 默认值 | 用途 |
| --- | --- | --- |
| `DEBUG_INFO_SHOW_MS` | 5000 | 默认隐藏计时 |
| `DEBUG_INFO_HOLD_MAX_MS` | 10000 | 联动音频播放的兜底上限 |
| `DEBUG_INFO_DEDUP_MS` | 1500 | 同一标题去重间隔 |

特性：
- 中心 356px 圆盘（`DEBUG_INFO_CARD_SIZE = LAYER4_OUTER_SIZE`）。
- 与 `RequestDebugTts` 联动：仅 `kDeviceStateIdle` + WiFi 已连接时真正下发 TTS。
- 卡片仍可见时可调用 `RefreshDebugInfoTimer` 重置倒计时，用于"唤醒成功后保持至少 N 秒"场景。

### 2.6 网络 / 握手 / 唤醒事件 → Debug 卡映射

由 [application.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/application.cc) 统一调度：

| 场景 | 触发点 | UI 反馈 |
| --- | --- | --- |
| 配网成功 | `HandleNetworkConnectedEvent`（约 [行 462](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/application.cc#L462)） | `ShowNotification("已连接 SSID", 30000)` + `ShowDebugInfo("WiFi 已连接", SSID, 5000)` + `OGG_SUCCESS` + `RequestDebugTts` |
| 联网失败 | `HandleInternetFailed`（行 [1085](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/application.cc#L1085)） | `ShowNotification(INTERNET_FAILED, 30000)` + `ShowDebugInfo(INTERNET_FAILED, 原因, 5000)` + `OGG_EXCLAMATION`；同 WiFi 周期内只显示一次（`internet_failed_shown_`） |
| OTA 握手成功 | `OnProtocolConnected` 通道回调（约 [行 861](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/application.cc#L861)） | `ShowDebugInfo("握手成功", ws_url, 5000)` + `RequestDebugTts` |
| 助手文本（TTS 字幕） | 收到 `tts` 消息（约 [行 940](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/application.cc#L940)） | `SetChatMessage("assistant", message)`；过长或含数字时改为 `ShowDebugInfo(title, 清洗, 8000)` |
| 用户语音识别 | 收到 `stt` 消息（约 [行 960](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/application.cc#L960)） | `SetChatMessage("user", message)` + `ShowDebugInfo("识别到", 预览, 5000)` |
| MCP 工具回调 | 收到 `mcp` 消息（约 [行 982](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/application.cc#L982)） | `ShowDebugInfo("工具调用", method, 2500)` |
| 唤醒成功 | 唤醒回调（约 [行 1268](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/application.cc#L1268)） | `ShowDebugInfo("唤醒成功", 详情, 30000)` + `RefreshDebugInfoTimer` |

### 2.7 太极图控制（`AttitudeDisplay` → `CompassTaiji`）

| 接口 | 默认值 | 说明 |
| --- | --- | --- |
| `RotateTaiji()` / `RotateTaijiCCW()` | — | 单步旋转 0.1°（CCW 减） |
| `SetTaijiRotation(int angle)` | — | 0~3600，`CompassTaiji::SetRotation` 落到 canvas |
| `StartTaijiAutoRotation(int period_ms=60000)` | 60s/圈 | LVGL 定时器回调推进 `auto_rotation_step_` |
| `StopTaijiAutoRotation()` | — | 暂停自动旋转 |
| `SetAutoRotationPaused(bool)` | — | 仅暂停，不删除定时器 |
| `SetStudyRingMode(bool ring_only)` | — | 学习态：仅保留环，去掉中心太极 |

鱼眼独立更新入口：

```cpp
// application.cc:111
if (auto* attitude = dynamic_cast<AttitudeDisplay*>(Board::GetInstance().GetDisplay())) {
    attitude->UpdateWifiFisheye(status);    // 上鱼眼 = 阴中阳
    attitude->UpdateBleFisheye(status);     // 下鱼眼 = 阳中阴
}
```

调用方：`Board` 网络回调（`NetworkEvent::Connected/Connecting/Disconnected` → `WifiStatus`）与 `BleServer::SetStatusCallback`（`BleStatus`）。

### 2.8 AttitudeDisplay 对基类 UI 的屏蔽

[attitude_display.h:89-122](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.h#L89-L122) 显式重写：

```cpp
virtual void ShowNotification(...);
virtual void SetStatus(const char*);
virtual void SetEmotion(const char*);
virtual void SetChatMessage(const char*, const char*);
virtual void ClearChatMessages();
virtual void SetPreviewImage(std::unique_ptr<LvglImage>, uint32_t);
```

不再使用基类的 `status_bar_` / `notification_label_` / `chat_message_label_` / `emoji_image_`，避免触发 `label is nullptr` 警告。所有交互反馈统一走 `ShowDebugInfo`。

---

## 3. 覆盖层：JARVIS 启动视图 `FortuneWatchfaceView`

> 来源：移植自动态表盘项目。[fortune_watchface_view.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/fortune_watchface_view.h) / [fortune_watchface_view.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/fortune_watchface_view.cc)

### 3.1 控件清单（基于 [fortune_watchface_view.cc:104-203](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/fortune_watchface_view.cc#L104-L203)）

| 控件 | 描述 |
| --- | --- |
| 背景 | `0x020611` 纯黑 + 半透明蓝色面板 + 圆形光晕 |
| 60 刻度 | 一周 60 根亮暗刻度（每 5 根主刻度切色） |
| `scan_arc_` | 296×8 / `0x20eaff`，按 `tick/16` 旋转 |
| `pulse_arc_` | 246×7 / `0xff3f93`，sin 波形值变化 |
| `seconds_arc_` | 202×5 / `0xffd447`，转 210° + 当前值 |
| 12 轨道点 `orbit_dots_[12]` | 半径 122±10 圆周轨道，sin 透明度脉动 |
| JARVIS 标签 | 主标签 + 阴影 AB 模拟霓虹效果，文本颜色 0.4s 切换 |
| 5 频谱条 | 高度 `4 + sin(tick/150)*8`，底部居中 |
| 状态栏 | 显示 `ESP32-S3  JARVIS HUD  SCAN %02d%%` |

### 3.2 生命周期

| 步骤 | 行为 |
| --- | --- |
| 第一次 `Show()` | 调 `CreateUI` → 创建独立 `lv_obj_t` 全屏覆盖层 + 启动定时器（`lv_timer_create(OnTimer, 33, this)`，约 30fps） |
| `Hide()` | `lv_obj_add_flag(..., HIDDEN)`，**当前不自动 DestroyUI**（仅隐藏） |
| 视觉一致性 | 已对齐 AI 罗盘配色（黑底青蓝元素），但独立屏幕不会破坏罗盘层级 |

### 3.3 入口

```cpp
// 在 AttitudeDisplay::SelectFortuneMenuItemUnlocked(0) 选中"今日运势"时
if (index == 0) {
    auto& view = FortuneWatchfaceView::GetInstance();
    view.SetParentContainer(attitude_container_);
    view.Show();
}
```

---

## 4. 上层调度：Application 中的 UI 反馈

[application.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/application.cc) 是 UI 的唯一指挥中枢，所有写屏操作都先 `Schedule` 到主循环。

### 4.1 状态机 → UI 联动（`OnStateChanged` 监听器）

| DeviceState | 状态栏 `SetStatus` | 表情 `SetEmotion` | 聊天 `SetChatMessage` |
| --- | --- | --- | --- |
| `kDeviceStateIdle` | `Lang::Strings::STANDBY` | `"neutral"` | `""` |
| `kDeviceStateConnecting` | `CONNECTING` | `neutral` | `""` |
| `kDeviceStateListening` | `LISTENING` | `neutral` | — |
| `kDeviceStateSpeaking` | `SPEAKING` | — | — |
| `kDeviceStateUpgrading` | — | — | `"Upgrade successful, rebooting..."` |
| `kDeviceStateStarting` | `CHECKING_NEW_VERSION` / `ACTIVATION` / `LOADING_PROTOCOL` | — | — |
| 启动完成 | — | `"microchip_ai"` | — |

参见 [application.cc:1062-1449](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/application.cc#L1062-L1449)。

### 4.2 协议事件 → UI/音效

| 事件 | UI 反馈 | 行号 |
| --- | --- | --- |
| 收到 `hello` / `system` 消息 | `SetChatMessage("system", msg)` | 713 |
| 收到 `tts` 助手文本 | `SetChatMessage("assistant", message)`；过长/含数字时改 `ShowDebugInfo` | 927 / 940 |
| 收到 `stt` 用户文本 | `SetChatMessage("user", message)` + `ShowDebugInfo("识别到", 5000)` | 951 / 960 |
| `tts` 中 `emotion` 字段 | `SetEmotion(emotion_str)` | 968 |
| 收到 MCP 响应 | `SetChatMessage("system", payload_str)` + `ShowDebugInfo("工具调用", method, 2500)` | 982 / 1015 |
| OTA 激活 | `SetStatus(ACTIVATION)` + `ShowNotification` (Activate 6 位码) | 707 / 795 |

### 4.3 按钮事件

| 入口 | 行为 |
| --- | --- |
| `ToggleChatState`（MAIN_EVENT_TOGGLE_CHAT，行 1119） | 复用 `HandleBootKey`：Idle 选中、Listening 中断 |
| `HandlePowerKey` | 先关协议，再调 `AttitudeDisplay::HandlePowerKey` |
| RTC 模式切换 | `ShowNotification(RTC_MODE_ON/OFF, 3000)` |

### 4.4 调试 TTS

```cpp
void Application::RequestDebugTts(const std::string& text);  // application.cc:1691
```

- 仅 `kDeviceStateIdle + WiFi Connected` 时真正下发。
- 与 `ShowDebugInfo` 联动：调用方传入音频可覆盖的 `hold_ms`。
- 链路：`RequestDebugTts` → `OpenAudioChannel` → `Protocol::SendUserPrompt` → 服务器返回 TTS 字幕 + Audio。

### 4.5 HTTP API 触发的 UI

参考 [docs/HTTP_API.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/docs/HTTP_API.md)：

| 路由 | UI 副作用 |
| --- | --- |
| `POST /api/sdcard/shots` | 触发一次 LVGL 截图保存到 SD 卡 |
| `POST /api/device/reboot` | 关机动画 → ESP 重启 |
| `POST /api/device/clear-nvs` | 清 NVS，重启自动进入配网模式 |

---

## 5. 其它显示分支的简化交互

### 5.1 LCD 类（[lcd_display.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/lcd_display.cc)）

- 三个区域：顶部 `status_bar_`（含 `notification_label_`）/ 中部 `preview_image_` 与 `emoji_image_` / 底部 `bottom_bar_`（含 `chat_message_label_`，单行跑马灯或多行 wrap）。
- `SetChatMessage` 自动决定 wrap 还是 circular scroll（`CONFIG_USE_EMOTE_MESSAGE_STYLE` 关闭时切换）。
- `SetPreviewImage` 启动 `preview_timer_`，到时 `LV_OBJ_FLAG_HIDDEN`。

### 5.2 OLED（[oled_display.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/oled_display.cc)）

- 仅注册 `"dark"` 主题，使用 `font_awesome_30_1` 大图标 + 内置文本字体。
- 文本/表情/通知共用小屏布局，不支持大型预览。

### 5.3 表情（[emote_display.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/emote_display.cc)）

- 只继承 `SetEmotion`，其余 `SetStatus` / `ShowNotification` / `SetChatMessage` 全部 no-op。
- 适用于仅展示 GIF 的简化板（如 [otto-robot](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/boards/otto-robot/otto_emoji_display.cc) 系列）。

### 5.4 BLE 鱼眼（[ble_server.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/ble/ble_server.cc)）

- 通过 `BleServer::SetStatusCallback` → `AttitudeDisplay::UpdateBleFisheye(BleStatus)`。
- 鱼眼描边/底色随 `DISABLED` / `ADVERTISING` / `CONNECTED` 切换。
- BluFi 配网前必须 `Stop()` 释放 NimBLE 资源。

---

## 6. 串口调试入口：`SnapshotService`

独立 UART 任务（UART1, GPIO17/18）：

| 命令 | 行为 |
| --- | --- |
| `SNAP` | 单帧 LVGL → JPEG → base64 推回 |
| `CLICK <idx>` | 触发 0..3 的罗盘功能按钮（默认映射：0=今日运势 / 1=财运 / 2=健康 / 3=求财） |
| `PING` | 回 `PONG`，存活探测 |

封装脚本：[tools/screenshot_with_log.py](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/tools/screenshot_with_log.py)。详见 [doc/SNAPSHOT_USAGE.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/doc/SNAPSHOT_USAGE.md)。

> 重要：`XIAOZHI_ENABLE_BOOT_SCREENSHOT`（默认 1）控制启动自动截图任务（每 5 秒一次，限制 20 条）。

---

## 7. 完整交互时序图

### 7.1 启动 → 配网 → 握手 → 唤醒 → 占卜

```
开机
  └─ Application::Initialize
        ├─ Board::create_board() 创建 AttitudeDisplay
        │     └─ AttitudeDisplay::SetupUI()
        │          ├─ CreateBackground (L0-L4 + 金边)
        │          ├─ CreateLayer0Taiji (CompassTaiji + WiFi/BLE 鱼眼)
        │          ├─ CreateLayer4Boundary
        │          ├─ CreateFortuneMenuRing (12 图标)
        │          ├─ CreateFortuneMenuRingTouch (环形触摸热区)
        │          └─ CreateTaijiDivinationTouch (太极中心 PRESSED/RELEASED)
        ├─ SnapshotService::Start (可选)
        └─ AppMain → Schedule 主循环

[WiFi Connecting] ShowNotification("搜索 WiFi...", 30000)
    └─ 成功 → ShowNotification("已连接 SSID", 30000)
              ShowDebugInfo("WiFi 已连接", SSID, 5000)
              OGG_SUCCESS + RequestDebugTts("已连接到 ...")
              NetworkEvent::Connected → UpdateWifiFisheye(CONNECTED)

[Idle]  SetStatus(STANDBY) + SetEmotion("neutral")
    ├─ 短按 BOOT → CycleFortuneMenuSelection
    │    └─ 选中 idx==0 → FortuneWatchfaceView::Show() (HUD overlay)
    ├─ 长按 BOOT → StartFortuneDivinationUnlocked (跑马灯)
    │    └─ 30s 内 → FinishFortuneDivination → ShowDebugInfo("今日卦象", "...", hold)
    ├─ 触摸点击菜单位 → SelectFortuneMenuItem(idx)
    └─ 触摸按住太极 → taiji_hold_timer (3s) → 启动占卜
                        ↑ RELEASED 延后 5 秒等待 (FORTUNE_DIVINATION_RELEASE_FINISH_MS)

[唤醒] "你好小智" → AFE → MAIN_EVENT_WAKE_WORD_DETECTED
    └─ ContinueWakeWordInvoke
          ├─ OpenAudioChannel → 握手 → ShowDebugInfo("握手成功", "...", 5000) + RequestDebugTts
          └─ 唤醒成功 → ShowDebugInfo("唤醒成功", "...", 30000) + RefreshDebugInfoTimer

[Listening] SetStatus("聆听中") + SetEmotion("neutral")
    └─ AFE VAD 触发出话 → Protocol::SendAudio

[Speaking] SetStatus("回答中")
    ├─ 收到 tts 字幕 → SetChatMessage("assistant", text) 或 ShowDebugInfo
    └─ on_playback_finished → SetChatMessage("system", "")

[Upgrading] SetChatMessage("system", "Upgrade successful, rebooting...")

[电源键短按] HandlePowerKey → 取消选中 / 隐藏功能区 / 中断占卜
```

### 7.2 网络中断 / 失败

```
Network.Disconnected
  └─ HandleNetworkDisconnectedEvent
        ├─ SetStatus(... REGISTERING_NETWORK)
        └─ 配网模式 → ShowNotification(SCANNING_WIFI, 30000)

首次 OTA 失败 (CheckNewVersion)
  └─ HandleInternetFailed(reason)
        ├─ ShowNotification(INTERNET_FAILED, 30000)
        ├─ ShowDebugInfo(INTERNET_FAILED, why, 5000)  ← 核心 UI 反馈
        ├─ OGG_EXCLAMATION
        └─ internet_failed_shown_ = true (同周期不再弹)
```

---

## 8. 关键交互类 / 方法索引

| 入口 | 文件位置 | 用途 |
| --- | --- | --- |
| [AttitudeDisplay::HandleBootKey](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L2147-L2171) | attitude_display.cc:2147 | BOOT 短按 |
| [AttitudeDisplay::HandleFortuneBootLongPress](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L2172-L2183) | attitude_display.cc:2172 | BOOT 长按 |
| [AttitudeDisplay::HandlePowerKey](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L1474) | attitude_display.cc:1474 | 电源键 |
| [AttitudeDisplay::SelectFortuneMenuItem](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L1322) | attitude_display.cc:1322 | 触摸选中（外部 API） |
| [AttitudeDisplay::CycleFortuneMenuSelection](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L1394) | attitude_display.cc:1394 | 循环下一个 |
| [AttitudeDisplay::StartFortuneDivination](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L946) | attitude_display.cc:946 | AI 触发占卜 |
| [AttitudeDisplay::ShowDebugInfo](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L2065) | attitude_display.cc:2065 | 关键事件提示卡 |
| [AttitudeDisplay::EnterIdleState](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L1794) | attitude_display.cc:1794 | 复位为 Idle |
| [Application::HandleFortuneBootKey](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/application.cc#L1119-L1125) | application.cc:1119 | 应用层桥接 |
| [Application::HandleNetworkConnectedEvent](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/application.cc#L462) | application.cc:462 | 网络成功 |
| [Application::HandleInternetFailed](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/application.cc#L1085) | application.cc:1085 | 联网失败 |
| `CompassTaiji::StartAutoRotation` | [compass_taiji.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/compass_taiji.cc) | 太极自转 |
| [FortuneWatchfaceView::Show](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/fortune_watchface_view.cc#L295-L311) | fortune_watchface_view.cc:295 | JARVIS HUD 覆盖 |
| [Display::ShowNotification](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/display.h#L31-L38) | display.h:31 | 基类接口 |

---

## 9. 设计要点（实现时易踩的坑）

1. **AttitudeDisplay 屏蔽基类 UI**：`ShowNotification` / `SetStatus` / `SetEmotion` / `SetChatMessage` / `SetPreviewImage` 全部被改写（[attitude_display.h:89-122](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.h#L89-L122)），不再使用基类的 `status_bar_` / `chat_message_label_` / `emoji_image_`，否则会触发 `label is nullptr` 警告。所有提示统一走 `ShowDebugInfo`（[attitude_display.cc:2065](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L2065)）。
2. **鱼眼坐标固定**：即使 `RotateTaiji`，鱼眼 (`FISHEYE_WIFI_LOCAL_*` / `FISHEYE_BLE_LOCAL_*`) 也不跟随旋转，避免破坏"阴阳对立"语义。
3. **去重与节奏**：`DEBUG_INFO_DEDUP_MS=1500` 防止音频/网络同时弹卡刷屏；`DEBUG_INFO_HOLD_MAX_MS=10000` 兜底连续延长。
4. **`OnTaijiDivinationPressed` 翻面**：按下启动 3s `taiji_hold_timer_`，RELEASED 时若动画未启动则 `CancelTaijiHoldTimerUnlocked`。`fortune_divination_from_taiji_` 区分触摸触发还是 BOOT 长按触发。
5. **`fortune_menu_ring_touch_` 显隐**：选中态生成后 `lv_obj_remove_flag(..., HIDDEN)`；保持空闲时 `lv_obj_add_flag(..., HIDDEN)` 让触摸事件穿透。
6. **联网失败抑制**：`internet_failed_shown_` 标志位与 `WiFi Connected → Disconnected → Connected` 周期绑定，重复配网不会骚扰用户。
7. **Boot/状态机互锁**：占卜动画 `Animating` 时拒绝 `HandleBootKey/HandlePowerKey` 状态切换以免动画被丢弃。
8. **LVGL 全局锁**：AttitudeDisplay 内部重写 `LockFn_` / `UnlockFn_`，所有方法在持有 `DisplayLockGuard` 后再调用 `lvgl_port_lock`；切忌绕过两把锁直接操作 LVGL。
9. **截图前必须 `lvgl_port_unlock`**：`SnapshotService::CaptureAndEncode` 在截图前主动解锁，避免 LVGL 任务死锁。

---

## 10. 后续可扩展点

如果想增加界面交互，建议沿用现有「基类方法 + AttitudeDisplay 重写 + ShowDebugInfo 集中调度」模式：

1. **新增运势子菜单**（如"求签"）：在 [attitude_display.cc:166](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L166) 附近扩充 `FortuneMenuItemDef`，再用相同 `SelectFortuneMenuItemUnlocked` 路径分发。
2. **新增调试事件类型**：直接复用 `ShowDebugInfo(title, detail, hold_ms)`；AI 工具回调可同步调用。
3. **新增状态条 UI**：参照 `Application::OnStateChanged` 注册新监听器（约 [application.cc:1062](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/application.cc#L1062)）。
4. **新增覆盖视图**：参考 `FortuneWatchfaceView` 模式，`lv_screen_load` 切换 + LVGL 锁统一管理。
5. **新增硬件交互**（如旋钮）：在 `attitude_display.h` 添加 `OnKnobLeft` / `OnKnobRight` 接口，在 `application.cc` 中桥接到 Board 的 Knob 回调。

---

## 附录 A：可视化结构图

```
360×360 圆形屏
┌──────────────────────────────┐ ← 屏幕边界
│        L4 鎏金外边界         │
│      ╭──────────────╮       │
│     ╱   12 运势菜单  ╲      │
│   ╱  (35px 图标环)     ╲    │
│ │  ┌─ L3 ──────────┐  │   │
│ │  │  L2  ┌─────┐  │  │   │
│ │  │      │ 上鱼 │  │  │   │ ← 阴中阳 (WiFi)
│ │  │ 太极 │  眼  │  │  │   │
│ │  │  R=86│ ─── │  │  │   │
│ │  │      │ 下鱼 │  │  │   │ ← 阳中阴 (BLE)
│ │  │      │  眼  │  │  │   │
│ │  │      └─────┘  │  │   │
│ │  │   Debug Info 卡 │   │ ← 中心 356px (默认隐藏)
│ │  └────────────────┘  │   │
│   ╲                    ╱    │
│     ╲                  ╱     │
│      ╰────────────────╯      │
└──────────────────────────────┘
```

## 附录 B：相关文档

- [README.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/README.md) — 项目总览
- [TROUBLESHOOTING.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/TROUBLESHOOTING.md) — 故障排查
- [doc/JARVIS.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/doc/JARVIS.md) — JARVIS 视觉规范
- [doc/JARVIS_COMPASS.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/doc/JARVIS_COMPASS.md) — JARVIS 罗盘
- [doc/JARVIS_PROMPT.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/doc/JARVIS_PROMPT.md) — JARVIS 提示词
- [doc/fortune_divination.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/doc/fortune_divination.md) — 占卜功能设计
- [doc/SNAPSHOT_USAGE.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/doc/SNAPSHOT_USAGE.md) — 串口截图使用
- [docs/HTTP_API.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/docs/HTTP_API.md) — HTTP API 文档
- [doc/小智AI与后台服务器交互协议汇总.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/doc/小智AI与后台服务器交互协议汇总.md) — 协议消息流向
- [doc/ESP32与JavaServer技术方案.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/doc/ESP32与JavaServer技术方案.md) — 整体技术方案
- [doc/WAVESHARE_ESP32_S3_TOUCH_LCD_1_85B.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/doc/WAVESHARE_ESP32_S3_TOUCH_LCD_1_85B.md) — 主屏板卡规格
- [doc/ai_compass_feature_expansion.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/doc/ai_compass_feature_expansion.md) — AI 罗盘功能扩展
