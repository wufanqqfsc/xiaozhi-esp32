---
name: "xiaozhi-code-wiki"
description: "小智 ESP32 项目代码知识库（蒸馏自 CODE_WIKI.md），覆盖项目架构、模块职责、关键类/文件、依赖关系、构建与运行。Invoke when user asks about xiaozhi-esp32 project structure, module locations, architecture, key classes (Application/AudioService/Protocol/AttitudeDisplay/CompassTaiji/McpServer/BleServer/SnapshotService), build steps, or board configuration."
---

# 小智 ESP32 Code Wiki（项目代码知识库）

> **项目**: 小智 ESP32 AI 罗盘（xiaozhi-esp32）  
> **版本**: `PROJECT_VER "2.2.6"`（[CMakeLists.txt](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/CMakeLists.txt)）  
> **来源**: 蒸馏自 [CODE_WIKI.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/CODE_WIKI.md)  
> **支持芯片**: ESP32 / ESP32-S3 / ESP32-C3/C5 / ESP32-P4  
> **ESP-IDF 要求**: `>= 5.5.2`

---

## 1. 项目概述

小智 ESP32 是基于 ESP32 系列的**语音交互 + 视觉罗盘**开源固件，本仓库是 [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) 上游的二次开发衍生版（参见 [.trae/rules/rule_xiaozhi.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/.trae/rules/rule_xiaozhi.md)）。

**核心目标**：
- 🎙️ **AI 语音入口**：Qwen / DeepSeek 等大模型实现流式 ASR + LLM + TTS
- 🧭 **AI 罗盘（AttitudeDisplay）**：360×360 圆形 LCD + 太极阴阳鱼 + 12 类运势菜单
- 🔌 **MCP 多端控制**：通过 Model Context Protocol 把本地硬件（扬声器/LED/舵机/相机）暴露给云端大模型
- 📡 **多网络接入**：Wi-Fi / ML307 4G / NT26 4G / 双网备份 / RNDIS

**本仓库相对上游的关键增量**（★ 标记为仓库特有）：

| 增量模块 | 文件 |
|---------|------|
| ★ AI 罗盘 | [attitude_display.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc) |
| ★ 太极图组件 | [compass_taiji.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/compass_taiji.cc) |
| ★ 串口截图服务 | [snapshot_service.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/snapshot/snapshot_service.cc) |
| ★ BLE 鱼眼指示 | [ble_server.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/ble/ble_server.cc) |
| ★ 启动自动截图 | [main.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/main.cc)（`XIAOZHI_ENABLE_BOOT_SCREENSHOT`） |
| ★ 自定义编译烧录脚本 | [build_and_flash.sh](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/build_and_flash.sh) |
| ★ SD 卡日志 HTTP 服务 | [sdcard_log_http.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/sdcard_log_http.cc) |

---

## 2. 分层架构

```
app_main() [main/main.cc]
  ├─ NVS 初始化
  ├─ SnapshotService::Initialize()/Start()
  └─ Application::GetInstance()
        ├─ Initialize() ─► Board::GetInstance() (create_board 工厂注入)
        │                    ├─ Display / AudioCodec / Network
        │                    ├─ Buttons / Knobs / Backlight / Camera
        │                    └─ Power / Battery / AXP / SY6970
        ├─ SetCallbacks(AudioService, Network, OTA, MCP)
        └─ Run() ─► FreeRTOS EventGroup 主循环

        │ (状态机: DeviceStateMachine  事件: MAIN_EVENT_*)
        ├─ Protocol (WebSocket | MQTT+UDP)
        ├─ AudioService (麦克风 ↔ 扬声器 ↔ Opus)
        ├─ McpServer (云端工具调用)
        ├─ Ota (升级 + 激活)
        ├─ Settings (NVS 持久化)
        └─ Assets (字体/图标下载)
```

**数据流**：
- **上行（用户→服务器）**：麦克风 PCM → AFE（VAD/AEC/NS）→ Opus → WebSocket/MQTT → 服务器
- **下行（服务器→用户）**：服务器 JSON/Opus → Protocol → Opus 解码 → I2S → 扬声器
- **控制平面**：JSON 消息（hello / listen / stop / abort / mcp / 激活 / 资产更新）
- **本地 UI**：Display 接收状态/表情/字幕；AttitudeDisplay 12 项运势菜单 + 调试信息卡

---

## 3. 目录结构

```
xiaozhi-esp32/
├── CMakeLists.txt              # 项目级 CMake (PROJECT_VER=2.2.6)
├── build_and_flash.sh/.bat/.ps1
├── CODE_WIKI.md / docs/ / doc/
├── partitions/                 # v2 / v1 分区表
├── scripts/                    # gen_lang.py, build_default_assets.py
├── tools/                      # screenshot_with_log.py 等
└── main/
    ├── main.cc                 # app_main() 入口
    ├── application.{cc,h}      # 核心应用类
    ├── device_state_machine.{cc,h} / device_state.h
    ├── assets.{cc,h} / ota.{cc,h} / settings.{cc,h} / system_info.{cc,h}
    ├── mcp_server.{cc,h}
    ├── sdcard_log.{cc,h} / sdcard_log_http.{cc,h}  ★ SD卡日志HTTP
    ├── audio/
    │   ├── audio_service.{cc,h}  / audio_codec.{cc,h}
    │   ├── audio_processor.h / wake_word.h
    │   ├── codecs/           # ES8311/ES8374/ES8388/Box/No/Dummy
    │   ├── processors/       # Afe/No/Debugger
    │   ├── wake_words/       # Afe/Esp/Custom
    │   └── demuxer/          # OGG 解封装
    ├── protocols/
    │   ├── protocol.{cc,h}
    │   ├── websocket_protocol.{cc,h}
    │   └── mqtt_protocol.{cc,h}  # MQTT 控制 + UDP 加密音频
    ├── display/
    │   ├── display.{cc,h} / lcd_display.{cc,h} / oled_display.{cc,h}
    │   ├── emote_display.{cc,h}
    │   ├── attitude_display.{cc,h}    ★ AI 罗盘
    │   ├── compass_taiji.{cc,h}       ★ 太极图
    │   ├── lvgl_display/              # LVGL 9.x 适配层
    │   └── snapshot/                  ★ 串口截图服务
    ├── boards/
    │   ├── common/            # Board 基类、WiFi/ML307 板卡
    │   └── <board_name>/      # 70+ 开发板专属实现
    ├── led/                   # LED 抽象 + single/circular/gpio
    └── ble/ble_server.{cc,h}  ★ BLE 鱼眼指示
```

---

## 4. 应用层核心模块

### 4.1 入口 `app_main()` — [main.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/main.cc)

```
app_main()
  ├── nvs_flash_init() (必要时擦除重建)
  ├── SnapshotService::GetInstance().Initialize()/.Start()
  ├── Application::GetInstance().Initialize()
  │      ├─ Board::GetInstance() (create_board 工厂注入)
  │      ├─ 设置 Display/AudioCodec/Buttons/Knobs 回调
  │      ├─ 初始化 AudioService / Protocol / Ota / McpServer
  │      └─ 启动网络 (异步)
  ├── xTaskCreate(screenshot_task, ...)  # 每 5 秒截图
  └── Application::GetInstance().Run()   # 主事件循环
```

`XIAOZHI_ENABLE_BOOT_SCREENSHOT`（默认 1）控制截图任务；历史限制 20 条。

### 4.2 `Application` 单例 — [application.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/application.h)

**FreeRTOS EventGroup + 主循环**，是设备唯一指挥中枢。

| 事件位 | 含义 |
|--------|------|
| `MAIN_EVENT_SCHEDULE` (1<<0) | 调度 main_tasks 回调 |
| `MAIN_EVENT_SEND_AUDIO` (1<<1) | 取一个 Opus 包发送 |
| `MAIN_EVENT_WAKE_WORD_DETECTED` (1<<2) | 唤醒词命中 |
| `MAIN_EVENT_VAD_CHANGE` (1<<3) | VAD 状态变化 |
| `MAIN_EVENT_ERROR` (1<<4) | 协议错误 |
| `MAIN_EVENT_ACTIVATION_DONE` (1<<5) | 激活完成 |
| `MAIN_EVENT_CLOCK_TICK` (1<<6) | 时钟滴答 |
| `MAIN_EVENT_NETWORK_CONNECTED/_DISCONNECTED` | 网络事件 |
| `MAIN_EVENT_TOGGLE_CHAT/_START_LISTENING/_STOP_LISTENING` | 对话控制 |
| `MAIN_EVENT_STATE_CHANGED` (1<<12) | 状态机变化 |

**关键方法**：

| 方法 | 作用 |
|------|------|
| `Initialize() / Run() / GetInstance()` | 生命周期 |
| `Schedule(std::function)` | 投递回调到主循环 |
| `ToggleChatState() / StartListening() / StopListening()` | 对话状态切换 |
| `Alert() / DismissAlert()` | 状态条 / 通知 UI |
| `PlaySound() / PlayUiSound()` | 播放 OGG 音效 |
| `AbortSpeaking(reason)` | 中断 TTS 播报 |
| `WakeWordInvoke(text)` | 程序化触发唤醒 |
| `UpgradeFirmware(url, version)` | 启动 OTA |
| `Reboot()` | 软重启 |
| `SendMcpMessage(payload)` | 发送 MCP 消息 |
| `SetAecMode() / GetAecMode()` | 切换 AEC 模式 |
| `RequestDebugTts(text)` | 调试 TTS (仅 Idle+WiFi) |
| `ResetProtocol()` | 释放协议资源 |
| `HandleFortuneBootKey/HandleFortuneBootLongPress/HandlePowerKey()` | ★ 罗盘按键入口 |

### 4.3 设备状态机 `DeviceStateMachine` — [device_state_machine.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/device_state_machine.h)

**状态枚举**（[device_state.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/device_state.h)）：
```cpp
enum DeviceState {
    kDeviceStateUnknown, kDeviceStateStarting, kDeviceStateWifiConfiguring,
    kDeviceStateIdle, kDeviceStateConnecting, kDeviceStateListening,
    kDeviceStateSpeaking, kDeviceStateUpgrading, kDeviceStateActivating,
    kDeviceStateAudioTesting, kDeviceStateFatalError
};
```

**能力**：`TransitionTo()`（原子切换，校验合法性）/ `CanTransitionTo()`（仅查询）/ `AddStateChangeListener()`（观察者）/ `GetStateName()`（调试日志）。内部使用 `std::atomic<DeviceState>` + `std::mutex`。

**状态切换合法性**：
```
Unknown → Starting → Activating → Idle ⇄ Listening ⇄ Speaking
              ↓              ↓
         WifiConfiguring   Connecting (Mqtt/WS 重连)
              ↓              ↓
              └─── Upgrading ┘
                     ↓
                 FatalError
```

### 4.4 `Settings` — [settings.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/settings.h)

NVS 包装：`Settings(ns, read_write=false)` 默认只读；提供 `GetString/SetString/GetInt/SetInt/GetBool/SetBool/EraseKey/EraseAll`。

### 4.5 `SystemInfo` — [system_info.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/system_info.h)

收集 `BoardJson` / `DeviceStatusJson` 等，被 MCP 工具调用或协议 JSON 消息使用。

---

## 5. 音频子系统

### 5.1 数据通道

```
AudioCodec → AudioProcessor (AFE: VAD/AEC/NS) → AudioService → Protocol
  (I2S 麦克风)                                          (WS/MQTT) → Server
                                                                        
Speaker ← AudioService ← Protocol ← Server
  (I2S)         (Opus Decode)   (Decode Queue)
```

### 5.2 `AudioService` — [audio_service.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/audio/audio_service.h)

| 类别 | 接口 |
|------|------|
| 生命周期 | `Initialize(codec) / Start() / Stop()` |
| 唤醒 | `EnableWakeWordDetection() / EncodeWakeWord() / PopWakeWordPacket() / GetLastWakeWord()` |
| 语音 | `EnableVoiceProcessing() / EnableDeviceAec()` |
| 测试 | `EnableAudioTesting()` (回环最长 10s) |
| 数据 | `PushPacketToDecodeQueue() / PopPacketFromSendQueue() / ReadAudioData() / ResetDecoder()` |
| 音效 | `PlaySound(std::string_view)` |
| 回调 | `SetCallbacks({on_send_queue_available, on_wake_word_detected, on_vad_change, on_audio_testing_queue_full, on_playback_finished})` |

**常量**：`OPUS_FRAME_DURATION_MS=60` / `MAX_DECODE_PACKETS_IN_QUEUE=40` / `MAX_ENCODE_TASKS_IN_QUEUE=2` / `MAX_TIMESTAMPS_IN_QUEUE=3`。

**实现**：三个 FreeRTOS 任务 — `AudioInputTask` / `AudioOutputTask` / `OpusCodecTask`。

### 5.3 `AudioCodec` — [audio_codec.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/audio/audio_codec.h)

抽象基类，依赖 `i2s_std` 驱动，DMA 配置 `AUDIO_CODEC_DMA_DESC_NUM=6` / `AUDIO_CODEC_DMA_FRAME_NUM=240`。

子类：`BoxAudioCodec` / `Es8311AudioCodec` / `Es8374AudioCodec` / `Es8388AudioCodec` / `Es8389AudioCodec` / `NoAudioCodec` / `DummyAudioCodec`。

### 5.4 `AudioProcessor` — [processors/](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/audio/processors/)

- `AfeAudioProcessor`（基于 [esp_afe_sr_models.h](https://github.com/espressif/esp-sr)，提供 VAD/AEC/降噪）
- `NoAudioProcessor`（透传）
- `AudioDebugger`（调试数据回调）

### 5.5 `WakeWord` — [wake_words/](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/audio/wake_words/)

- `AfeWakeWord`（ESP32-S3/P4，使用 `esp_afe_sr` 多唤醒词，可向服务器回传唤醒词 Opus 流）
- `EspWakeWord`（ESP32/C3，使用 `esp-sr` 简化路径）
- `CustomWakeWord`（用户自定义）

CMake 按目标芯片自动选择。

---

## 6. 通信协议层

### 6.1 协议基类 `Protocol` — [protocol.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/protocols/protocol.h)

```cpp
class Protocol {
public:
    virtual bool Start() = 0;
    virtual bool OpenAudioChannel() = 0;
    virtual void CloseAudioChannel(bool send_goodbye = true) = 0;
    virtual bool IsAudioChannelOpened() const = 0;
    virtual bool SendAudio(std::unique_ptr<AudioStreamPacket>) = 0;

    void SendWakeWordDetected(const std::string&);
    void SendStartListening(ListeningMode);
    void SendStopListening();
    void SendAbortSpeaking(AbortReason);
    void SendMcpMessage(const std::string&);
    virtual void SendUserPrompt(const std::string&);  // 调试 TTS

    void OnIncomingAudio(cb); void OnIncomingJson(cb);
    void OnAudioChannelOpened(cb); void OnAudioChannelClosed(cb);
    void OnNetworkError(cb); void OnConnected(cb); void OnDisconnected(cb);
protected:
    int  server_sample_rate_ = 24000;
    int  server_frame_duration_ = 60;
    virtual bool SendText(const std::string&) = 0;
};
```

**音频包与二进制协议**：

```cpp
struct AudioStreamPacket {
    int sample_rate, frame_duration;
    uint32_t timestamp;
    std::vector<uint8_t> payload;  // Opus
};

struct BinaryProtocol2 {  // 历史版本 (带 timestamp)
    uint16_t version, type;  // 0:OPUS, 1:JSON
    uint32_t reserved, timestamp, payload_size;
    uint8_t payload[];
} __attribute__((packed));

struct BinaryProtocol3 {  // 新版本
    uint8_t type, reserved;
    uint16_t payload_size;
    uint8_t payload[];
} __attribute__((packed));
```

**枚举**：
```cpp
enum AbortReason { kAbortReasonNone, kAbortReasonWakeWordDetected };
enum ListeningMode { kListeningModeAutoStop, kListeningModeManualStop, kListeningModeRealtime };
```

### 6.2 WebSocket — [websocket_protocol.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/protocols/websocket_protocol.h)

`Start()` 建立到 OTA 配置的 WS URL，发送 `hello`；`OpenAudioChannel()` 协商 SR/帧长；`SendAudio()` 用 `BinaryProtocol2/3` 封装 Opus 帧。

### 6.3 MQTT+UDP — [mqtt_protocol.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/protocols/mqtt_protocol.h)

控制消息走 MQTT，实时音频走 UDP（AES-CTR 加密）。`MQTT_PING_INTERVAL_SECONDS=90` / `MQTT_RECONNECT_INTERVAL_MS=60000`。`mbedtls_aes_context` + `aes_nonce_` 实现 UDP 加密。

---

## 7. 显示子系统

### 7.1 类层次

```
Display (基类, display.h)
 ├── NoDisplay
 ├── LcdDisplay (lcd_display.h)
 │    ├── SpiLcdDisplay / RgbLcdDisplay / MipiLcdDisplay
 ├── OledDisplay
 └── EmoteDisplay
        └── ★ AttitudeDisplay (attitude_display.h)  // AI 罗盘
```

### 7.2 `Display` 接口（精简）

| 方法 | 说明 |
|------|------|
| `SetStatus(const char*)` | 状态栏文本 |
| `ShowNotification(text, duration_ms=3000)` | 短暂通知 |
| `SetEmotion(const char*)` | 表情 |
| `SetChatMessage(role, content) / ClearChatMessages()` | 对话字幕 |
| `SetTheme(Theme*) / GetTheme()` | 主题 |
| `UpdateStatusBar(bool all=false)` | 电量/网络/时间 |
| `SetPowerSaveMode(bool)` | 节能 |
| `SetupUI()` | 一次性 UI 构建 |
| `Lock(timeout_ms) / Unlock()` | 多线程安全（`DisplayLockGuard`） |

### 7.3 LVGL 适配 — [lvgl_display/](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/lvgl_display/)

- `lvgl_display.{cc,h}`：把 ESP-LCD 面板接入 LVGL 9.x
- `lvgl_theme.{cc,h}` / `lvgl_font.{cc,h}` / `lvgl_image.{cc,h}` / `emoji_collection.{cc,h}`
- `gif/`（gifdec）/ `jpg/`（JPEG 编解码）

### 7.4 ★ AI 罗盘 `AttitudeDisplay` — [attitude_display.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.h)

**屏幕规格**：360×360 圆形 LCD，主元素为**阴阳鱼太极图**。

#### 视觉分层

| 层 | 内容 | 关键常量 |
|----|------|---------|
| L0 | 太极阴阳鱼 + 鎏金外圈 | `TAIJI_RADIUS=86` / `TAIJI_CANVAS_SIZE=172` / `TAIJI_GOLD_RING_WIDTH=3` |
| L0·鱼眼 | 阴中阳（WiFi）/ 阳中阴（BLE） | `FISHEYE_ICON_SIZE=32` / `FISHEYE_PULSE_MS=300` / `FISHEYE_BORDER_WIDTH=2` |
| L4 | 外边界（与屏幕圆边贴齐） | `LAYER4_BOUNDARY_RADIUS = SCREEN_W/2 - GOLD_RING_ARC_WIDTH/2` |

> `static_assert(TAIJI_RADIUS == 86, ...)`

#### 12 运势菜单环（`FORTUNE_MENU_COUNT = 12`，12 点钟起顺时针）

| Idx | 类型 | 名称 |
|-----|------|------|
| 0 | `FortuneMenuType::Today` | 今日运势 |
| 1 | `FortuneMenuType::Wealth` | 财运 |
| 2 | `FortuneMenuType::Career` | 事业 |
| 3 | `FortuneMenuType::Love` | 爱情 |
| 4 | `FortuneMenuType::MoodGua` | 心情卦 |
| 5 | `FortuneMenuType::Huangli` | 黄历 |
| 6 | `FortuneMenuType::SolarTerm` | 节气 |
| 7 | `FortuneMenuType::Custom` | 自定义 |
| 8 | `FortuneMenuType::Health` | 健康 |
| 9 | `FortuneMenuType::Study` | 学业 |
| 10 | `FortuneMenuType::Travel` | 出行 |
| 11 | `FortuneMenuType::Noble` | 贵人 |

环触摸区：`FORTUNE_MENU_TOUCH_INNER_R = TAIJI_RADIUS - 4` ~ `LAYER4_BOUNDARY_RADIUS`。选中态图标放大 10%，未选中正常。环心相对中点外偏 3px。

#### 按键/触摸入口

| 方法 | 行为 |
|------|------|
| `HandleBootKey()` | 循环选中运势菜单项 |
| `HandleFortuneBootLongPress()` | 仅落日志 |
| `HandlePowerKey()` | 返回/取消 - 取消选中、隐藏功能区 |

#### 调试信息卡

```cpp
void ShowDebugInfo(title, detail, hold_ms = 3000);
void HideDebugInfo();
```

- 用于显示与后台的关键交互事件（激活成功 / WiFi 已连接 / MCP 收到工具调用 / 联网失败）
- 调试 TTS 期间叠加播放
- 功能区提示卡 5000ms (`DEBUG_INFO_SHOW_MS=5000`) 后自动关闭

**网络状态通知**（[application.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/application.cc)）：

| 场景 | 触发点 | UI 反馈 |
|------|--------|---------|
| 配网成功 | `NetworkEvent::Connected` | `ShowNotification("已连接 SSID")` 30s + `ShowDebugInfo("WiFi 已连接", SSID)` 5s + `OGG_SUCCESS` 音 + `RequestDebugTts` |
| 联网失败 | `CheckNewVersion()` 首次 OTA 失败 | `ShowNotification("联网失败")` 30s + `ShowDebugInfo("联网失败", "WiFi 已连，服务不可达")` 5s + `OGG_EXCLAMATION` 音 |
| 重复抑制 | `internet_failed_shown_` 标志位 | 每个 WiFi 连接周期内 OTA 失败仅弹卡一次 |

#### 颜色系统（[attitude_display.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc) 顶部匿名命名空间）

```
COLOR_BG_OUTER  = 0x0A0A0A   // 外层黑底
COLOR_BG_CENTER = 0x121212   // 中心暗灰
COLOR_TEXT_MAIN = 0xD4AF37   // 鎏金（主文）
COLOR_TEXT_SUB  = 0xC0C0C0   // 银灰（副文）
COLOR_TEXT_HIGH = 0xFFFFFF   // 亮白（高亮）
COLOR_BORDER_LINE = 0xD4AF37 // 鎏金描边
COLOR_STATE_HEAVY  = 0xE67E22 // 橙色 (heavy)
COLOR_STATE_DANGER = 0xB82601 // 暗红 (danger)
```

### 7.5 ★ 太极图组件 `CompassTaiji` — [compass_taiji.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/compass_taiji.h)

```cpp
class CompassTaiji {
public:
    static void   Create(lv_obj_t* parent, int cx, int cy, int radius);
    static void   Rotate(int delta_angle);          // 0.1° 单位
    static void   SetRotation(int angle);           // 0~3600
    static int    GetRotation();
    static void   ResetRotation();

    static void   StartAutoRotation(int period_ms = 60000);  // 默认 60s/圈
    static void   SetAutoRotationPeriod(int period_ms);
    static int    GetAutoRotationPeriod();
    static void   StopAutoRotation();
    static bool   IsAutoRotating();

    static void   SetAutoRotationPaused(bool paused);
    static void   TickAutoRotationStep();
    static void   SetStudyRingMode(bool ring_only);
    static lv_obj_t* GetContainer();
    static lv_obj_t* GetCanvas();
    static int    GetRadius();

    static void   PaintFisheyeDisc(canvas, size, fill, ring, bg, ring_width);
};
```

- 内部使用预渲染 canvas 快照实现零开销切换
- 描边抗锯齿：方形 canvas 外缘向 `bg` 色混合以避免透明叠底产生杂色
- `OnAutoRotationTimer` 是 LVGL 定时器回调，按 `auto_rotation_interval_ms_` 节拍推进 `auto_rotation_step_`

---

## 8. MCP 工具服务

### 8.1 协议对象

- `ImageContent`（[mcp_server.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/mcp_server.h)）：base64 编码图片 + MIME
- `ReturnValue = std::variant<bool, int, std::string, cJSON*, ImageContent*>`
- `PropertyType { kPropertyTypeBoolean, kPropertyTypeInteger, kPropertyTypeString }`
- `Property`（必填/可选，整数支持 `[min,max]` 范围）/ `PropertyList`
- `McpTool`（name / description / properties / callback / user_only）

### 8.2 `McpServer` — [mcp_server.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/mcp_server.h)

```cpp
class McpServer {
public:
    static McpServer& GetInstance();
    void AddCommonTools();       // AI 可调
    void AddUserOnlyTools();     // 仅用户可见
    void AddTool(McpTool*);
    void AddTool(name, desc, properties, callback);
    void AddUserOnlyTool(name, desc, properties, callback);
    void ParseMessage(const cJSON* json);
    void ParseMessage(const std::string& message);
};
```

**典型工具**（来自 [application.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/application.cc)）：`self.set_volume`、`self.screen.set_emotion`、`self.led.set_brightness`、`self.camera.take_photo` 等。详见 [docs/mcp-usage_zh.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/docs/mcp-usage_zh.md)。

---

## 9. 板级抽象层（Board）

### 9.1 `Board` 单例 — [board.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/boards/common/board.h)

```cpp
class Board {
public:
    static Board& GetInstance();
    virtual std::string GetBoardType() = 0;
    virtual std::string GetUuid() { return uuid_; }
    virtual Backlight*  GetBacklight();
    virtual Led*        GetLed();
    virtual AudioCodec* GetAudioCodec() = 0;
    virtual bool        GetTemperature(float& esp32temp);
    virtual Display*    GetDisplay();
    virtual Camera*     GetCamera();
    virtual NetworkInterface* GetNetwork() = 0;
    virtual void        StartNetwork() = 0;
    virtual void        SetNetworkEventCallback(NetworkEventCallback);
    virtual const char* GetNetworkStateIcon() = 0;
    virtual bool        GetBatteryLevel(int& level, bool& charging, bool& discharging);
    virtual std::string GetSystemInfoJson();
    virtual void        SetPowerSaveLevel(PowerSaveLevel) = 0;
    virtual std::string GetBoardJson() = 0;
    virtual std::string GetDeviceStatusJson() = 0;
};
```

**事件**：
```cpp
enum class NetworkEvent {
    Scanning, Connecting, Connected, Disconnected,
    WifiConfigModeEnter, WifiConfigModeExit,
    ModemDetecting, ModemErrorNoSim, ModemErrorRegDenied,
    ModemErrorInitFailed, ModemErrorTimeout
};
enum class PowerSaveLevel { LOW_POWER, BALANCED, PERFORMANCE };
using NetworkEventCallback = std::function<void(NetworkEvent, const std::string& data)>;
```

**派生类**：通用（Board / I2cDevice / Camera）、输入（Button / Knob / PowerSaveTimer / SleepTimer / SystemReset）、网络（WifiBoard / Ml307Board / Nt26Board / DualNetworkBoard / RndisBoard）、电源（AdcBatteryMonitor / Axp2101 / Sy6970）、机器人（OttoRobot / ElectronBot / EdaRobotPro / EdaSuperBear）。

### 9.2 板卡工厂

```cpp
#define DECLARE_BOARD(BOARD_CLASS_NAME) \
void* create_board() { return new BOARD_CLASS_NAME(); }
```

每个板卡目录的 `*.cc` 末尾写 `DECLARE_BOARD(XxxBoard)`，由 [main/CMakeLists.txt](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/CMakeLists.txt) 在选择 `BOARD_TYPE` 后把该目录全部 `.cc/.c` 链接进固件。

### 9.3 `WifiBoard` — [wifi_board.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/boards/common/wifi_board.h)

- `StartNetwork()`：异步连接已保存 SSID；超时进入配网模式
- `EnterWifiConfigMode()`：线程安全
- 配网走 `esp-wifi-connect` 或 BluFi（Kconfig 切换）

---

## 10. OTA / 资源 / 设置 / 系统信息

### 10.1 OTA — [ota.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/ota.h)

```cpp
class Ota {
public:
    esp_err_t CheckVersion();
    esp_err_t Activate();
    bool HasActivationChallenge();
    bool HasNewVersion();
    bool HasMqttConfig();
    bool HasWebsocketConfig();
    bool HasActivationCode();
    bool HasServerTime();
    bool StartUpgrade(std::function<void(int, size_t)>);
    static bool Upgrade(const std::string& url, std::function<void(int, size_t)>);
    void MarkCurrentVersionValid();
};
```

OTA URL 通过 Kconfig `OTA_URL` 配置（默认 `https://api.tenclass.net/xiaozhi/ota/`）。

**`SERVER_MODE` 选择**：
- `SERVER_MODE_LOCAL`（默认）：
  - OTA URL：`http://192.168.3.24:8003/xiaozhi/ota/`
  - WebSocket URL：`ws://192.168.3.24:8000/xiaozhi/v1/`
- `SERVER_MODE_OFFICIAL`：官方服务器

### 10.2 资源管理 `Assets` — [assets.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/assets.h)

`Download(url, progress_cb)` / `Apply(refresh_display_theme=true)` / `GetAssetData(name, ptr, size)` / `partition_valid()`。

构建期通过 [build_default_assets.py](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/scripts/build_default_assets.py) 把字体/emoji/表情打成 `assets.bin`，按 `CONFIG_FLASH_DEFAULT_ASSETS / _CUSTOM_ASSETS / _EXPRESSION_ASSETS / _NONE_ASSETS` 选择烧录策略。

### 10.3 `Settings` / `SystemInfo`

参见 §4.4 / §4.5。

---

## 11. BLE 子系统

### ★ `BleServer` — [ble_server.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/ble/ble_server.h)

轻量 NimBLE 外设，把 BLE 状态广播给罗盘「阳中阴」鱼眼。

```cpp
class BleServer {
public:
    using StatusCallback = std::function<void(BleStatus status)>;
    static BleServer& GetInstance();
    esp_err_t Start();
    esp_err_t Stop();   // BluFi 配网前须 Stop()
    void SetStatusCallback(cb);
    BleStatus GetStatus() const;
    bool IsRunning() const;
    void NotifyStatus(BleStatus);
};
```

Kconfig `XIAOZHI_ENABLE_BLE_FISHEYE` 开启（CMake 才会把 `ble/ble_server.cc` 编入）。

**鱼眼图标位置**（固定不旋转）：
- 上鱼眼（WiFi）：pos(162,126)，圆心(180,144)
- 下鱼眼（BLE）：pos(162,198)，圆心(180,216)
- 尺寸：32px（约占太极半径 86px 的 37%），2px 金色描边

---

## 12. 串口截图验证服务

### `SnapshotService` — [snapshot_service.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/snapshot/snapshot_service.h)

调试/自动化验证用：通过 USB-Serial/JTAG 串口输出 JPEG 截图与按钮触发命令。

```cpp
class SnapshotService {
public:
    static SnapshotService& GetInstance();
    esp_err_t Initialize();
    esp_err_t Start();
    esp_err_t Stop();
    esp_err_t TakeSnapshot();
    esp_err_t TriggerButtonClick(int index);  // 触发罗盘 4 个调试按钮之一
    bool IsRunning() const;
private:
    static void UARTTask(void*);
    esp_err_t ExecuteSnapshot();
    bool CaptureAndEncode(uint8_t** jpeg_data, size_t* jpeg_len);
    esp_err_t SendData(const uint8_t*, size_t);
    size_t Base64Encode(const uint8_t*, size_t, uint8_t*);
    size_t Base64EncodeLength(size_t);
    void SendACK(snapshot_error_t);
    void SendPONG();
};
```

**UART1 配置**：GPIO17 (TX) / GPIO18 (RX)。

### 命令协议 — [snapshot_protocol.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/snapshot/snapshot_protocol.h)

- `SNAP`：单帧截图
- `CLICK <idx>`：触发 0..3 索引的罗盘功能按钮（默认 0=今日运势 / 1=财运 / 2=健康 / 3=求财）
- `PING`：回 `PONG` 用于存活探测

详见 [SNAPSHOT_USAGE.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/doc/SNAPSHOT_USAGE.md) 与 [screenshot_with_log.py](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/tools/screenshot_with_log.py)。

`XIAOZHI_ENABLE_BOOT_SCREENSHOT`（默认 1）控制启动自动截图任务（每 5 秒一次，限制 20 条）。

---

## 13. 依赖关系

### 13.1 CMake 模块（[main/CMakeLists.txt](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/CMakeLists.txt)）

`esp_pm`, `esp_psram`, `esp_netif`, `esp_driver_gpio`, `esp_driver_uart`, `esp_driver_spi`, `esp_driver_i2c`, `esp_driver_i2s`, `esp_driver_jpeg`, `esp_driver_ppa`, `esp_app_format`, `app_update`, `spi_flash`, `console`, `efuse`, `bt`, `fatfs`。

### 13.2 组件依赖（[main/idf_component.yml](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/idf_component.yml)）

| 类别 | 组件 |
|------|------|
| LCD 面板 | `waveshare/esp_lcd_sh8601`, `espressif/esp_lcd_co5300`, `esp_lcd_ili9341`, `esp_lcd_gc9a01`, `esp_lcd_st77916`, `esp_lcd_axs15231b`, `esp_lcd_st7701`, `esp_lcd_st7796`, `esp_lcd_spd2010`, `78/esp_lcd_nv3023`, `espressif/esp_lcd_jd9365`, `waveshare/esp_lcd_st7703`, `espressif/esp_lcd_ili9881c`, `espressif/esp_lcd_ek79007` |
| IO Expander / Touch | `esp_io_expander_tca9554`, `waveshare/custom_io_expander_ch32v003`, `esp_io_expander_tca95xx_16bit`, `esp_lcd_touch_ft5x06`, `esp_lcd_touch_gt911`, `esp_lcd_touch_gt1151`, `waveshare/esp_lcd_touch_cst9217`, `esp_lcd_touch_cst816s`, `esp_lcd_touch_st7123`, `tny-robotics/sh1106-esp-idf` |
| 网络/4G | `78/esp-wifi-connect`, `78/esp-ml307`, `78/uart-eth-modem`, `espressif/esp_hosted`, `espressif/esp_wifi_remote`, `espressif/iot_usbh_rndis` |
| 音频 | `espressif/esp_audio_effects`, `espressif/esp_audio_codec`, `espressif/esp_codec_dev` |
| AI/语音 | `espressif/esp-sr`, `espressif/esp_image_effects`, `espressif/adc_battery_estimation`, `espressif/esp_new_jpeg`, `espressif2022/image_player`, `espressif2022/esp_emote_expression` |
| 显示 | `lvgl/lvgl`, `esp_lvgl_port`, `espressif/esp_mmap_assets`, `78/xiaozhi-fonts`, `txp666/otto-emoji-gif-component` |
| 输入 | `espressif/button`, `espressif/knob`, `espressif/bmi270_sensor`, `espressif/touch_slider_sensor`, `espressif/touch_button_sensor` |
| LED | `espressif/led_strip`, `esphome/esp-hub75` |
| 视觉/相机 | `espressif/esp32-camera`, `espressif/esp_video`, `espressif/esp32_p4_function_ev_board` |
| 机器人 | `espfriends/servo_dog_ctrl`, `llgok/cpp_bus_driver` |
| SenseCAP | `wvirgil123/sscma_client` |

### 13.3 上游/三方服务（参考性）

- 官方服务端：[xiaozhi.me](https://xiaozhi.me)
- 自部署服务端（[README.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/README.md)）：
  - [xinnan-tech/xiaozhi-esp32-server](https://github.com/xinnan-tech/xiaozhi-esp32-server)（Python）
  - [joey-zhou/xiaozhi-esp32-server-java](https://github.com/joey-zhou/xiaozhi-esp32-server-java)（Java）
  - [AnimeAIChat/xiaozhi-server-go](https://github.com/AnimeAIChat/xiaozhi-server-go)、[hackers365/xiaozhi-esp32-server-golang](https://github.com/hackers365/xiaozhi-esp32-server-golang)（Go）
- 其他客户端：`huangjunsen0406/py-xiaozhi` / `TOM88812/xiaozhi-android-client` / `100askTeam/xiaozhi-linux` / `78/xiaozhi-sf32` / `QuecPython/solution-xiaozhiAI`

---

## 14. 构建与运行

### 14.1 通用步骤

```bash
git clone https://github.com/78/xiaozhi-esp32.git
cd xiaozhi-esp32
git submodule update --init --recursive   # 重要：初始化子模块

# ESP-IDF >= 5.5.2
idf.py set-target esp32s3        # 或 esp32 / esp32c3 / esp32p4
idf.py menuconfig                # 关键项: Board Type / Language / OTA URL

# 编译烧录（必须使用项目根目录脚本，参见 .trae/rules/rule_xiaozhi.md）
./build_and_flash.sh
# 或
idf.py build flash monitor
```

**首次启动**：默认连接 `xiaozhi.me` 官方服务器；未激活设备显示 6 位激活码，到 [xiaozhi.me 控制台](https://xiaozhi.me) 完成绑定。

### 14.2 一键烧录脚本 — [build_and_flash.sh](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/build_and_flash.sh)

**⚠️ 重要规则**（来自 `.trae/rules/rule_xiaozhi.md`）：
- **必须**使用项目根目录的 `build_and_flash.sh`
- **不要**使用 `2>&1` 重定向（会丢失错误信息）
- Windows 版本：[build_and_flash.bat](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/build_and_flash.bat) / [build_and_flash.ps1](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/build_and_flash.ps1)

**目标开发板**：`waveshare/esp32-s3-touch-lcd-1.85b`（ST77916 LCD 驱动）。

**关键 GPIO**：
- LCD_RST：GPIO3
- TP_SCL/SDA/RST：GPIO10/11/1
- PWR_BUTTON：GPIO7
- TCA9554 IO 扩展器：跳过（1.85B 版本）

#### 板型切换原理（重要：`-D` 参数无效）

⚠️ **历史坑：原版 `build_and_flash.sh` 传 `-DBOARD_TYPE_WAVESHARE_ESP32_S3_TOUCH_LCD_1_85B=y` 是无效的**。

- `idf.py -DXXX=y` 设置的是 **CMake 变量**，不会写入 `sdkconfig`
- `main/CMakeLists.txt` 用的是 Kconfig 形式 `if(CONFIG_BOARD_TYPE_BREAD_COMPACT_WIFI)`，**真正控制板型的是 `sdkconfig` 中的 `CONFIG_BOARD_TYPE_*=y`**
- 旧脚本即使硬编码 `-DBOARD_TYPE_WAVESHARE_...=y`，实际编译目标仍由 `sdkconfig` 决定；曾导致烧录的 `merged-binary.bin` 实际是 `bread-compact-wifi` 固件，**完全黑屏**（SSD1306 I2C init 失败）

**修复方案**（已合入 `build_and_flash.sh` 的 `build_firmware()`）：
1. `fullclean` 后用 `sed` 把 `sdkconfig` 里所有 `CONFIG_BOARD_TYPE_*=y` 改为 `# ... is not set`
2. 再把目标 `CONFIG_BOARD_TYPE_<BOARD_KCONFIG_NAME>=y` 启用
3. 转换规则：`waveshare/esp32-s3-touch-lcd-1.85b` → `WAVESHARE_ESP32_S3_TOUCH_LCD_1_85B`

> 手动切换板型时直接编辑 `sdkconfig` 即可。

### 14.3 常用板型

| Kconfig | 目录 | 备注 |
|---------|------|------|
| `BREAD_COMPACT_WIFI` | `bread-compact-wifi` | 默认面包板 |
| `ESP_BOX_3` | `esp-box-3` | 乐鑫 ESP-BOX-3 |
| `M5STACK_CORE_S3` | `m5stack-core-s3` | M5Stack CoreS3 |
| `LILYGO_T_CIRCLE_S3` | `lilygo-t-circle-s3` | LILYGO T-Circle-S3（圆形屏，AI 罗盘备选板） |
| `LICHUANG_DEV_S3` | `lichuang-dev` | 立创实战派 ESP32-S3 |
| `WAVESHARE_ESP32_S3_TOUCH_AMOLED_*` | `esp32-s3-touch-amoled-*` | 微雪多款 AMOLED |
| `OTTO_ROBOT` / `ELECTRON_BOT` | `otto-robot` / `electron-bot` | 机器人 Otto / Electron Bot |
| ... | ... | 共 70+ |

完整列表见 [main/CMakeLists.txt](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/CMakeLists.txt) 和 [main/Kconfig.projbuild](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/Kconfig.projbuild)。

### 14.4 自动化验证闭环

```bash
# 1. 启动后台截图脚本（默认每 5s 一张）
# 2. 单次截图
python3 tools/screenshot_with_log.py --snap
# 3. 触发按钮索引 0..3
python3 tools/screenshot_with_log.py --click 0
python3 tools/screenshot_with_log.py --click 1
python3 tools/screenshot_with_log.py --click 2
```

- 输出路径：`screenshots/screenshot_latest.jpg`、`screenshots/history/screenshot_<ts>_<idx>.jpg`、`screenshots/logs/run_<ts>.log`
- 串口占用：`lsof /dev/cu.usbmodem1101`，`kill -9 <PID>` 释放
- CLICK 无响应：检查 UART 接收缓冲区（建议 ≥ 8192 字节）

---

## 15. 关键数据流与状态机

### 15.1 语音交互流程

```
1) 用户："你好小智"
   ↓
2) AudioCodec → AFE (VAD/AEC/NS) → WakeWord 命中 → MAIN_EVENT_WAKE_WORD_DETECTED
   ↓
3) Application::ContinueWakeWordInvoke() → Protocol::OpenAudioChannel()
   ↓
4) ServerHello → 协商 sample_rate=24000 / frame_duration=60
   ↓
5) ListeningMode=AutoStop → SendStartListening
   ↓
6) AudioInputTask → AudioService → OpusCodecTask → Protocol::SendAudio (BinaryProtocol2/3)
   ↓
7) 服务器：ASR → LLM (Qwen/...) → TTS → Opus 流回传
   ↓
8) Protocol::OnIncomingAudio → DecodeQueue → OpusCodecTask 解码 → PlaybackQueue
   ↓
9) AudioOutputTask → AudioCodec (I2S) → 扬声器
   期间 on_playback_finished → UI 反馈
```

### 15.2 MCP 工具调用流程

```
服务器 ──► JSON-RPC { method:"tools/call", params:{name, arguments} }
   ↓
Application::OnIncomingJson()
   ↓
McpServer::ParseMessage(json) → DoToolCall(id, name, arguments)
   ↓
McpTool::Call(PropertyList) → 同步执行本地回调
   ↓
ReplyResult(id, json_text) ──► 服务器
```

### 15.3 AI 罗盘交互流程

```
Idle (Boot 短按)  ─►  CycleFortuneMenuSelectionUnlocked()      // 选中项移动
                    └─► ShowFortuneMenuFeatureCardUnlocked(index)  // 5s 自动消失
Idle (Boot 长按)  ─►  HandleFortuneBootLongPress() → 仅落日志
调试信息卡
  - ShowDebugInfo(title, detail, hold_ms)
  - 与 Application::RequestDebugTts() 联动：仅 Idle + WiFi 已连接时真正发声
网络状态卡
  - 配网成功  → "WiFi 已连接" + SSID
  - 联网失败  → "联网失败" + "WiFi 已连，服务不可达"（首次 OTA 失败时）
```

**设备唤醒方式**：
- 语音唤醒：默认「Hey VANVIS」
- 按键唤醒：短按 BOOT 键

**唤醒后状态流转**：待命(Idle) → 连接中(Connecting) → 聆听中(Listening) → 说话中(Speaking) → 回到 Listening 或 Idle

**BOOT 键交互**：
- BOOT 短按：选择 - 进入选中态/循环选择下一个运势项
- BOOT 长按：确定 - 触发当前选中运势，直接显示结果卡
- 电源键短按：返回/取消 - 取消选中、关闭结果卡、隐藏功能区

---

## 16. Kconfig 配置选项

来自 [main/Kconfig.projbuild](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/Kconfig.projbuild)：

| 选项 | 说明 |
|------|------|
| `BOARD_TYPE_*` | 70+ 板型之一 |
| `SERVER_MODE` | LOCAL（本地） / OFFICIAL（官方） |
| `OTA_URL` | OTA 校验/升级服务地址 |
| `LOCAL_WEBSOCKET_URL` | 本地 WebSocket 地址（SERVER_MODE_LOCAL 时有效） |
| `LANGUAGE_*` | 32 种语言之一（决定打包的 OGG 资源子集，缺失回退 en-US） |
| `FLASH_DEFAULT_ASSETS / _CUSTOM_ASSETS / _EXPRESSION_ASSETS / _NONE_ASSETS` | 资产烧录策略 |
| `USE_ESP_BLUFI_WIFI_PROVISIONING` | BluFi 配网 |
| `CONFIG_USE_AUDIO_PROCESSOR` | 启用 AFE（VAD/AEC/NS） |
| `CONFIG_SEND_WAKE_WORD_DATA` | 把唤醒词片段发给服务器辅助 ASR |
| `XIAOZHI_ENABLE_BLE_FISHEYE` | 启用 BLE 鱼眼指示 |
| `XIAOZHI_ENABLE_BOOT_SCREENSHOT` | 启用启动自动截图 |

---

## 17. 板型一览

完整板型列表见 [main/CMakeLists.txt](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/CMakeLists.txt) 和 [main/Kconfig.projbuild](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/Kconfig.projbuild)。

常用：bread-compact-wifi / bread-compact-ml307 / bread-compact-nt26 / esp-box-3 / esp-box / esp-box-lite / esp-spot-s3 / m5stack-core-s3 / m5stack-tab5 / m5stack-cardputer-adv / lilygo-t-circle-s3 / lilygo-t-cameraplus-s3 / esp32-s3-touch-amoled-1.8 / esp32-s3-touch-lcd-1.85b / lichuang-dev / lichuang-c3-dev / xingzhi-cube / xmini-c3 / magiclick-2p4 / otto-robot / electron-bot 等 70+ 种。

---

## 18. 故障排查

📕 **故障排查已拆分为独立文档：[TROUBLESHOOTING.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/TROUBLESHOOTING.md)**

涵盖：§18.1 黑屏（SSD1306 I2C init 失败）、§18.2 MCP 工具缺失、§18.3 截图服务、§18.4 启动 nullptr 警告、§18.5 binary 体积异常等 5 个高频问题。

---

## 19. 附录：AI 罗盘 UI 设计规范

**UI 适配**：360×360 圆形原型屏，内容需在可见圆形区域内。

**配色方案**（固定单主题，黑金色系）：
- 背景色：固定深色（0x0A0A0A / 0x121212）
- 所有文字、符号、环形边框：金色（0xD4AF37），不受主题切换影响
- 主题切换功能已移除，使用单一主题

**旋转速度**（以 [attitude_display.cc:27](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L27) / [compass_taiji.cc:35](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/compass_taiji.cc#L35) 为准）：
- 太极鱼：顺时针自动旋转，**60 秒**每转一圈（`TAIJI_ROTATION_PERIOD_NORMAL_MS=60000`，步进 1.2°/200ms）
- 运势菜单环：静态 12 项，**不自转**；固定 r=132 圆周上（`FORTUNE_MENU_RING_RADIUS`），选中项通过颜色脉动反馈

**UI 边界**：
- 2px 圆形边界线分隔独立区域
- 350px 直径红色边界线（中心(180,180)，3px 宽，#ff0000，100% 不透明度，最顶层）

---

## 20. SD 卡日志服务

### 模块结构

| 文件 | 说明 |
|------|------|
| [sdcard_log.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/sdcard_log.h) | SD 卡日志核心接口 |
| [sdcard_log.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/sdcard_log.cc) | 日志重定向（`esp_log_set_vprintf`） |
| [sdcard_log_http.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/sdcard_log_http.h) | HTTP 服务接口 |
| [sdcard_log_http.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/sdcard_log_http.cc) | RESTful API + Web 界面 + 截图 |

### 核心接口

```cpp
// sdcard_log.h
bool SdCardLogStart(const char* filename);
void SdCardLogStop();
bool SdCardLogIsActive();

// sdcard_log_http.h
bool SdCardLogHttpStart(const char* base_path, uint16_t port);
void SdCardLogHttpStop();
bool SdCardLogHttpIsRunning(void);
uint16_t SdCardLogHttpGetPort(void);
bool SdCardLogHttpTriggerSnapshot(void);
```

### HTTP API

| 接口 | 方法 | 说明 |
|------|------|------|
| `/api/sdcard/info` | GET | SD 卡信息（日志状态等） |
| `/api/sdcard/logs` | GET | 日志文件列表 |
| `/api/sdcard/logs/<filename>` | GET | 下载日志 |
| `/api/sdcard/logs/<filename>` | DELETE | 删除日志 |
| `/api/sdcard/shots` | GET | 截图列表 |
| `/api/sdcard/shots` | POST | 触发截图（保存 SD 卡） |
| `/api/sdcard/shots/<filename>` | GET | 下载截图（JPEG） |
| `/api/sdcard/shots/<filename>` | DELETE | 删除截图 |
| `/` | GET | Web 管理界面 |

### 使用方法

**Web 界面**：浏览器打开 `http://<设备IP>:8080`。

**命令行**：
```bash
# 日志管理
curl http://<IP>:8080/api/sdcard/logs
curl -o boot.log http://<IP>:8080/api/sdcard/logs/xiaozhi_boot_1.log
curl -X DELETE http://<IP>:8080/api/sdcard/logs/xiaozhi_boot_1.log

# 截图管理
curl http://<IP>:8080/api/sdcard/shots
curl -X POST http://<IP>:8080/api/sdcard/shots
curl -o screenshot.jpg http://<IP>:8080/api/sdcard/shots/shot_20250624_143052.jpg
curl -X DELETE http://<IP>:8080/api/sdcard/shots/shot_20250624_143052.jpg

# SD 卡信息
curl http://<IP>:8080/api/sdcard/info
```

### 自动启停

WiFi 连接后自动启动（SD 卡已挂载时），断开后自动停止：
```cpp
// application.cc - HandleNetworkConnectedEvent
struct stat st;
if (stat("/sdcard", &st) == 0 && S_ISDIR(st.st_mode)) {
    if (!SdCardLogHttpIsRunning()) {
        SdCardLogHttpStart("/sdcard", 8080);
    }
}
```

### 技术实现

- **日志重定向**：`esp_log_set_vprintf()` 同时输出到串口和 TF 卡文件
- **截图功能**：LVGL 快照 API → JPEG → SD 卡
- **线程安全**：FreeRTOS 互斥量保护文件写入
- **文件名格式**：日志 `xiaozhi_boot_<nn>.log`（8.3 短名）；截图 `shot_YYYYMMDD_HHMMSS.jpg`
- **HTTP 服务**：`esp_http_server` 组件 + 通配符路由

### 编译配置

```
CONFIG_FATFS_LFN_HEAP=y           # 启用长文件名支持（可选）
CONFIG_ESP_HTTP_SERVER=y           # 启用 HTTP 服务器
CONFIG_FATFS_CODEPAGE=437         # FATFS 代码页
```

### 串口截图服务（可选）

默认关闭。如需启用，在 `main/main.cc` 中设置：
```cpp
#define XIAOZHI_ENABLE_SNAPSHOT_SERVICE 1
```

---

## 21. HTTP API 文档

完整文档：[docs/HTTP_API.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/docs/HTTP_API.md)

### API 接口一览

| 接口 | 方法 | 说明 |
|------|------|------|
| `/api/device/status` | GET | 获取设备状态（WiFi / SD卡 / 内存 / uptime） |
| `/api/device/logs` | GET | 获取设备日志 |
| `/api/device/reboot` | POST | 重启设备 |
| `/api/device/ota-url` | GET | 查询 OTA/WS URL（NVS vs build config） |
| `/api/device/clear-nvs` | POST | 清除 NVS 中 ota_url/websocket_url |
| `/api/sdcard/info` | GET | SD 卡信息 |
| `/api/sdcard/logs` | GET | 日志文件列表 |
| `/api/sdcard/logs/<filename>` | GET/DELETE | 下载/删除日志 |
| `/api/sdcard/shots` | GET/POST | 截图列表 / 触发截图 |
| `/api/sdcard/shots/<filename>` | GET/DELETE | 下载/删除截图 |
| `/api/sdcard/files/<filename>` | DELETE | 删除 SD 卡任意文件 |

### 快速使用

```bash
# 查看设备状态
curl http://<设备IP>:8080/api/device/status

# 触发截图
curl -X POST http://<设备IP>:8080/api/sdcard/shots

# 查看日志
curl http://<设备IP>:8080/api/device/logs

# 重启设备
curl -X POST http://<设备IP>:8080/api/device/reboot

# 查询 OTA URL（NVS 覆盖 sdkconfig）
curl http://<设备IP>:8080/api/device/ota-url

# 清除 NVS URL（恢复 sdkconfig 默认）
curl -X POST http://<设备IP>:8080/api/device/clear-nvs
curl -X POST "http://<设备IP>:8080/api/device/clear-nvs?key=ota_url"
```

---

## 关联 Skills

- **esp32-http-api**：设备 HTTP API 详细参考（端口 8080、状态查询、日志/截图管理）
- **xiaozhi-server**：后端 Java 服务（端口 8091 HTTP / 8092 WebSocket）参考

## 相关文档

- [CODE_WIKI.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/CODE_WIKI.md) — 本 skill 来源（更详细）
- [README.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/README.md) / [README_zh.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/README_zh.md) / [README_ja.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/README_ja.md)
- [TROUBLESHOOTING.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/TROUBLESHOOTING.md) — 故障排查
- [docs/HTTP_API.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/docs/HTTP_API.md) — HTTP API 文档
- [docs/websocket.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/docs/websocket.md) — WebSocket 协议
- [docs/mqtt-udp.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/docs/mqtt-udp.md) — MQTT+UDP 协议
- [docs/mcp-protocol.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/docs/mcp-protocol.md) — MCP 协议
- [docs/mcp-usage_zh.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/docs/mcp-usage_zh.md) — MCP 工具使用
- [doc/小智AI与后台服务器交互协议汇总.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/doc/小智AI与后台服务器交互协议汇总.md) — 协议汇总
- [doc/ESP32与JavaServer技术方案.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/doc/ESP32与JavaServer技术方案.md) — 技术方案
- [doc/SNAPSHOT_USAGE.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/doc/SNAPSHOT_USAGE.md) — 串口截图使用
