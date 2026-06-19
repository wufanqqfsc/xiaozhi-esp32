# 小智 ESP32 AI 罗盘 Code Wiki

> 本文档基于仓库当前快照生成，覆盖项目架构、模块职责、关键类/函数、依赖关系与运行方法。
> 项目根版本：`PROJECT_VER "2.2.6"`（见 [CMakeLists.txt](CMakeLists.txt)）

---

## 目录

1. [项目概述](#1-项目概述)
2. [项目架构](#2-项目架构)
3. [目录结构](#3-目录结构)
4. [应用层核心模块](#4-应用层核心模块)
5. [音频子系统](#5-音频子系统)
6. [通信协议层](#6-通信协议层)
7. [显示子系统（含 AI 罗盘 AttitudeDisplay）](#7-显示子系统含-ai-罗盘-attitudedisplay)
8. [MCP 工具服务](#8-mcp-工具服务)
9. [板级抽象层（Board）](#9-板级抽象层board)
10. [OTA / 资源 / 设置 / 系统信息](#10-ota--资源--设置--系统信息)
11. [BLE 与架子鼓子系统](#11-ble-与架子鼓子系统)
12. [串口截图验证服务](#12-串口截图验证服务)
13. [依赖关系](#13-依赖关系)
14. [构建与运行](#14-构建与运行)
15. [关键数据流与状态机](#15-关键数据流与状态机)
16. [配置选项（Kconfig）](#16-配置选项kconfig)
17. [附录：板型一览](#17-附录板型一览)

---

## 1. 项目概述

### 1.1 项目简介

小智 ESP32（Xiaozhi AI Compass / 78/xiaozhi-esp32）是一个基于 ESP32 系列芯片的**语音交互 + 视觉罗盘**开源固件。本仓库是在 78/xiaozhi-esp32 上游基础上的二次开发衍生版（见 `.trae/rules/rule_xiaozhi.md`），核心目标包括：

- **AI 语音入口**：通过 Qwen / DeepSeek 等大模型实现流式 ASR + LLM + TTS 语音交互（`docs/websocket.md`、`docs/mqtt-udp.md`）。
- **MCP 多端控制**：通过 [Model Context Protocol](docs/mcp-protocol.md) 把本地硬件能力（扬声器、LED、舵机、相机等）暴露给云端大模型。
- **AI 罗盘（AttitudeDisplay）**：在 360×360 圆形 LCD 上以**太极阴阳鱼**为核心，提供 12 类运势交互（今日/财运/事业/爱情/心情卦/黄历/节气/自定义/健康/学业/出行/贵人）。
- **多网络接入**：Wi-Fi、ML307 Cat.1 4G、NT26 4G、双网备份、RNDIS 多种接入方式。
- **支持 70+ 开发板**：见 [docs/](docs/) 与 [main/boards/](main/boards/)。

### 1.2 关键版本

| 版本 | 说明 |
|------|------|
| v2.2.6（当前） | 本仓库主线；v2 分区表与 v1 不兼容，需手动烧录升级 |
| v1.9.2 | 上游稳定版本；通过 `git checkout v1` 切换，v1 分支维护至 2026 年 2 月 |

### 1.3 支持的芯片平台

- ESP32（经典款）
- ESP32-S3
- ESP32-C3 / ESP32-C5
- ESP32-P4

ESP-IDF 要求：`>=5.5.2`（见 [main/idf_component.yml](main/idf_component.yml)）。

### 1.4 本仓库相对上游的关键增量

| 增量模块 | 文件 | 说明 |
|---------|------|------|
| AI 罗盘（AttitudeDisplay） | [main/display/attitude_display.cc](main/display/attitude_display.cc) | 360×360 太极阴阳鱼 UI；12 运势菜单环；动效/结果卡 |
| 太极图组件 | [main/display/compass_taiji.cc](main/display/compass_taiji.cc) | LVGL 实现的旋转太极，自动旋转、AA 描边 |
| 主题 | [main/display/attitude_theme.cc](main/display/attitude_theme.cc) | 鎏金/玄黑配色 + 五级状态色 |
| 串口截图服务 | [main/display/snapshot/snapshot_service.cc](main/display/snapshot/snapshot_service.cc) | 通过 UART 输出 JPEG 截图与按钮触发 |
| BLE 鱼眼指示 | [main/ble/ble_server.cc](main/ble/ble_server.cc) | NimBLE 外设，广播 / 连接 / 关闭状态 |
| 架子鼓合成器 | [main/drum/drum_synth.cc](main/drum/drum_synth.cc) | 8 扇区 + 中心 Kick 触摸触发 OGG 鼓声 |
| 启动自动截图 | [main/main.cc](main/main.cc) | `XIAOZHI_ENABLE_BOOT_SCREENSHOT` 宏控制 |
| 自定义编译烧录脚本 | [build_and_flash.sh](build_and_flash.sh) / [build_and_flash.bat](build_and_flash.bat) | 一键编译烧录 |

---

## 2. 项目架构

### 2.1 分层架构图

```
┌───────────────────────────────────────────────────────────────────────────┐
│  app_main()   [main/main.cc]                                               │
│   ├─ NVS 初始化                                                          │
│   ├─ SnapshotService::Initialize()/Start()  (XIAOZHI_ENABLE_BOOT_SCREENSHOT)│
│   └─ Application::GetInstance()                                           │
│         ├─ Initialize()   ──► Board::GetInstance()  ◄── create_board()    │
│         │                       ├─ Display / AudioCodec / Network         │
│         │                       ├─ Buttons / Knobs / Backlight / Camera   │
│         │                       └─ Power / Battery / AXP / SY6970         │
│         ├─ SetCallbacks(AudioService, Network, OTA, MCP)                 │
│         └─ Run() ──► FreeRTOS EventGroup 主循环                          │
└────────────┬──────────────────────────────────────────────────────────────┘
             │
   ┌─────────▼───────────┐   状态机：DeviceStateMachine   事件：MAIN_EVENT_*
   │ Application (单例)  │   ─► Protocol (WebSocket | MQTT)
   │  · 调度 main_tasks  │   ─► AudioService (麦克风↔扬声器↔Opus)
   │  · 网络事件回调     │   ─► McpServer (云端工具调用)
   │  · 设备状态机       │   ─► Ota (升级 + 激活)
   │  · UI 反馈 (Alert)  │   ─► Settings (NVS 持久化)
   └─────────┬───────────┘   ─► Assets (字体/图标下载)
             │
   ┌─────────▼───────────┐   平台抽象
   │ Board (单例)        │   ─► WifiBoard / Ml307Board / DualNetworkBoard ...
   │  · AudioCodec       │   ─► 摄像头 / 屏幕 / 背光 / 电源 IC
   │  · Display          │
   │  · NetworkInterface │
   └─────────────────────┘
```

### 2.2 数据流概览

- **上行（用户→服务器）**：麦克风 PCM → AFE 处理器（VAD/AEC/NS）→ Opus 编码 → 通过 Protocol（WebSocket 或 MQTT+UDP）→ 服务器。
- **下行（服务器→用户）**：服务器 JSON/Opus → Protocol → Opus 解码 → 播放队列 → I2S → 扬声器。
- **控制平面**：JSON 消息（`hello`、`listen`/`stop`/`abort`、`mcp` 工具调用、激活、资产更新）走 WebSocket 或 MQTT。
- **本地 UI**：Display 接收状态/表情/字幕；AttitudeDisplay 增加运势状态机与触摸交互；SnapshotService 周期性导出 JPEG 至串口。

---

## 3. 目录结构

```
xiaozhi-esp32/
├── CMakeLists.txt                 # 项目级 CMake（项目名 xiaozhi，PROJECT_VER=2.2.6）
├── build_and_flash.sh / .bat / .ps1
├── CODE_WIKI.md                   # 本文档
├── README.md / README_zh.md / README_ja.md
├── partitions/                    # v2 / v1 分区表
├── docs/                          # 上游文档（custom-board、mcp-protocol、websocket...）
├── doc/                           # 本仓库增量文档（AI 罗盘功能扩展、协议汇总、SNAPSHOT_USAGE）
├── scripts/                       # gen_lang.py、build_default_assets.py 等
├── tools/                         # screenshot_with_log.py 等验证工具
├── .trae/rules/                   # 项目级 AI 助手规则
└── main/
    ├── CMakeLists.txt             # 源文件列表 + BOARD_TYPE 分发 + 语言/资产打包
    ├── Kconfig.projbuild          # menuconfig 选项（板型/语言/OTA URL 等）
    ├── idf_component.yml          # 组件依赖清单
    ├── main.cc                    # app_main() 入口
    ├── application.{cc,h}         # 核心应用类（状态机、事件循环）
    ├── device_state.h             # DeviceState 枚举
    ├── device_state_machine.{cc,h}
    ├── assets.{cc,h}              # 资源管理
    ├── ota.{cc,h}                 # OTA 升级 + 设备激活
    ├── settings.{cc,h}            # NVS 包装
    ├── system_info.{cc,h}         # 系统信息收集
    ├── mcp_server.{cc,h}          # MCP 协议解析与工具注册
    │
    ├── audio/
    │   ├── audio_codec.{cc,h}     # 编解码器基类
    │   ├── audio_service.{cc,h}   # 音频流服务（双队列 + 编解码任务）
    │   ├── audio_processor.h      # AFE 处理器接口
    │   ├── wake_word.h            # 唤醒词接口
    │   ├── codecs/                # 多种 ES8311/ES8374/ES8388/Box/No/Dummy 实现
    │   ├── processors/            # Afe / No / Debugger
    │   ├── wake_words/            # Afe / Esp / Custom
    │   └── demuxer/               # OGG 解封装
    │
    ├── protocols/
    │   ├── protocol.{cc,h}        # 协议基类 + 回调 + 消息类型
    │   ├── websocket_protocol.{cc,h}
    │   └── mqtt_protocol.{cc,h}   # MQTT 控制 + UDP 加密音频
    │
    ├── display/
    │   ├── display.{cc,h}         # Display 基类 + Theme
    │   ├── lcd_display.{cc,h}     # SpiLcd / RgbLcd / MipiLcd
    │   ├── oled_display.{cc,h}
    │   ├── emote_display.{cc,h}
    │   ├── attitude_display.{cc,h}    # ★ AI 罗盘
    │   ├── attitude_theme.{cc,h}      # ★ AI 罗盘主题
    │   ├── compass_taiji.{cc,h}       # ★ 太极图
    │   ├── lvgl_display/              # LVGL 适配层 + 主题/字体/GIF/JPG
    │   └── snapshot/                  # ★ 串口截图服务
    │
    ├── boards/
    │   ├── common/                 # Board 基类、WiFi/ML307 板卡、背光、电池、按键...
    │   └── <board_name>/           # 各开发板专属配置和实现（70+ 种）
    │
    ├── led/                        # LED 抽象 + single/circular/gpio
    ├── ble/ble_server.{cc,h}       # ★ BLE 鱼眼指示
    └── drum/drum_synth.{cc,h}      # ★ 架子鼓合成器
```

---

## 4. 应用层核心模块

### 4.1 入口 `app_main()` — [main/main.cc](main/main.cc)

```text
app_main()
  ├── nvs_flash_init() → 必要时擦除重建
  ├── SnapshotService::GetInstance().Initialize()/.Start()   // 启动截图后台服务
  ├── Application::GetInstance().Initialize()
  │      ├─ Board::GetInstance()（create_board() 工厂注入）
  │      ├─ 设置 Display/AudioCodec/Buttons/Knobs 回调
  │      ├─ 初始化 AudioService / Protocol / Ota / McpServer
  │      └─ 启动网络（异步）
  ├── xTaskCreate(screenshot_task, ...)        // 每 2 秒截图（调试/验证）
  └── Application::GetInstance().Run()         // 主事件循环（永不返回）
```

`XIAOZHI_ENABLE_BOOT_SCREENSHOT`（默认 1）控制截图任务是否启动；`fortune_demo_task` 已注释保留。

### 4.2 `Application` 单例 — [main/application.{cc,h}](main/application.h)

设备唯一的「指挥中枢」，采用 FreeRTOS EventGroup + 主循环模式。

| 事件位 | 含义 |
|--------|------|
| `MAIN_EVENT_SCHEDULE` (1<<0) | 调度一个 `main_tasks_` 回调 |
| `MAIN_EVENT_SEND_AUDIO` (1<<1) | 通知 `Run()` 取一个 Opus 包发送 |
| `MAIN_EVENT_WAKE_WORD_DETECTED` (1<<2) | 唤醒词命中 |
| `MAIN_EVENT_VAD_CHANGE` (1<<3) | VAD 状态变化 |
| `MAIN_EVENT_ERROR` (1<<4) | 协议错误 |
| `MAIN_EVENT_ACTIVATION_DONE` (1<<5) | 激活流程完成 |
| `MAIN_EVENT_CLOCK_TICK` (1<<6) | 时钟滴答 |
| `MAIN_EVENT_NETWORK_CONNECTED` / `_DISCONNECTED` | 网络事件 |
| `MAIN_EVENT_TOGGLE_CHAT` / `_START_LISTENING` / `_STOP_LISTENING` | 对话控制 |
| `MAIN_EVENT_STATE_CHANGED` (1<<12) | 状态机变化 |

**关键方法**：

| 方法 | 作用 |
|------|------|
| `Initialize()` | 初始化所有子系统（详见 4.1） |
| `Run()` | 主事件循环，处理上述事件 |
| `GetInstance()` | Meyers 单例 |
| `GetDeviceState() / SetDeviceState()` | 状态访问 |
| `Schedule(std::function<void()>&&)` | 将回调投递到主循环 |
| `ToggleChatState() / StartListening() / StopListening()` | 对话状态切换（事件驱动，线程安全） |
| `Alert() / DismissAlert()` | 状态条 / 通知 UI |
| `PlaySound() / PlayUiSound()` | 播放 OGG 音效（主循环，避免 LVGL 任务直播） |
| `AbortSpeaking(reason)` | 中断 TTS 播报 |
| `WakeWordInvoke(text)` | 程序化触发唤醒 |
| `UpgradeFirmware(url, version)` | 启动 OTA |
| `Reboot()` | 软重启 |
| `SendMcpMessage(payload)` | 向服务器发送 MCP 消息 |
| `RegisterMcpBroadcastCallback(cb)` | 注册 MCP 广播接收 |
| `SetAecMode() / GetAecMode()` | 切换 AEC 模式（关闭 / 设备侧 / 服务器侧） |
| `RequestDebugTts(text)` | 调试 TTS：仅在 Idle+WiFi 已连接时真正生效 |
| `ResetProtocol()` | 释放连接后分配的协议资源（线程安全） |
| `HandleFortuneBootKey()` / `HandleFortuneBootLongPress()` / `HandlePowerKey()` | ★ 罗盘按键入口（与 AttitudeDisplay 协作） |

### 4.3 设备状态机 `DeviceStateMachine` — [main/device_state_machine.{cc,h}](main/device_state_machine.h)

- **状态枚举**（[main/device_state.h](main/device_state.h)）：
  ```cpp
  enum DeviceState {
      kDeviceStateUnknown, kDeviceStateStarting, kDeviceStateWifiConfiguring,
      kDeviceStateIdle,    kDeviceStateConnecting, kDeviceStateListening,
      kDeviceStateSpeaking, kDeviceStateUpgrading, kDeviceStateActivating,
      kDeviceStateAudioTesting, kDeviceStateFatalError
  };
  ```
- **能力**：
  - `TransitionTo(new)`：原子切换，校验合法性，返回 bool。
  - `CanTransitionTo(target)`：仅查询。
  - `AddStateChangeListener(cb)` / `RemoveStateChangeListener(id)`：观察者模式。
  - 内部使用 `std::atomic<DeviceState>` + `std::mutex`（监听者列表）。
  - `GetStateName(state)`：调试日志用。

### 4.4 设置 `Settings` — [main/settings.{cc,h}](main/settings.h)

`NVS` 包装：
- 构造 `Settings(ns, read_write=false)` 默认只读。
- `GetString/SetString/GetInt/SetInt/GetBool/SetBool/EraseKey/EraseAll`。

### 4.5 系统信息 `SystemInfo` — [main/system_info.h](main/system_info.h)

收集 `BoardJson` / `DeviceStatusJson` 等，详见 [application.cc](main/application.cc) 中如何序列化为 JSON 上报。

---

## 5. 音频子系统

### 5.1 数据通道

```
  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐
  │ AudioCodec   │ ─► │ AudioProcessor│ ─► │ AudioService │ ─► │ Protocol     │
  │ (I2S 麦克风) │    │ (AFE: VAD/   │    │  Encode Queue│    │  (WS/MQTT)   │
  └──────────────┘    │  AEC/NS)     │    │  Opus Encode │    └──────┬───────┘
                      └──────────────┘    │  Send Queue  │           │
                                        └──────────────┘           │
                                                                   ▼
                                                          ┌──────────────┐
  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐│   Server     │
  │   Speaker    │ ◄──│ AudioService │ ◄──│ Protocol     ││  (LLM/TTS)   │
  │   (I2S)      │    │  Opus Decode │    │  Decode Queue│└──────────────┘
  └──────────────┘    │  Playback Q  │    └──────────────┘
                      └──────────────┘
```

参见 [audio_service.h](main/audio/audio_service.h#L28-L43) 顶部注释。

### 5.2 `AudioService` — [main/audio/audio_service.{cc,h}](main/audio/audio_service.h)

| 类别 | 接口 | 说明 |
|------|------|------|
| 生命周期 | `Initialize(codec)` / `Start()` / `Stop()` | 启动/停止 I/O 任务 |
| 唤醒 | `EnableWakeWordDetection(bool)` / `EncodeWakeWord()` / `PopWakeWordPacket()` / `GetLastWakeWord()` | AFE/ESP 唤醒词 |
| 语音 | `EnableVoiceProcessing(bool)` / `EnableDeviceAec(bool)` | AFE 启用 + 设备侧 AEC |
| 测试 | `EnableAudioTesting(bool)` | 回环测试（最长 10s） |
| 数据入/出 | `PushPacketToDecodeQueue(pkt, wait)` / `PopPacketFromSendQueue()` / `ReadAudioData(...)` / `ResetDecoder()` | 与 Protocol 对接 |
| 音效 | `PlaySound(std::string_view)` | 短音效 |
| 回调 | `SetCallbacks({on_send_queue_available, on_wake_word_detected, on_vad_change, on_audio_testing_queue_full, on_playback_finished})` | |

**常量**：
- `OPUS_FRAME_DURATION_MS = 60`
- `MAX_DECODE_PACKETS_IN_QUEUE = MAX_SEND_PACKETS_IN_QUEUE = 2400 / 60 = 40`
- `MAX_ENCODE_TASKS_IN_QUEUE = MAX_PLAYBACK_TASKS_IN_QUEUE = 2`
- `MAX_TIMESTAMPS_IN_QUEUE = 3`（用于服务器侧 AEC 对齐）

**实现要点**：
- 三个 FreeRTOS 任务：`AudioInputTask`、`AudioOutputTask`、`OpusCodecTask`。
- `event_group_` 用于唤醒 / AFE / 测试 / 播放非空 等状态位。
- `audio_power_timer_` 周期性检查 I/O 是否长时间空闲以节电（15s 超时）。

### 5.3 `AudioCodec` — [main/audio/audio_codec.{cc,h}](main/audio/audio_codec.h)

抽象基类，依赖 ESP-IDF 的 `i2s_std` 驱动，DMA 配置 `AUDIO_CODEC_DMA_DESC_NUM=6` / `AUDIO_CODEC_DMA_FRAME_NUM=240`。

子类实现：
- `BoxAudioCodec` — ESP-BOX 系列
- `Es8311AudioCodec` / `Es8374AudioCodec` / `Es8388AudioCodec` / `Es8389AudioCodec` — 各家 CODEC 芯片
- `NoAudioCodec` / `DummyAudioCodec` — 占位/空实现

### 5.4 `AudioProcessor` — [main/audio/audio_processor.h](main/audio/audio_processor.h) + [main/audio/processors/](main/audio/processors/)

- `AfeAudioProcessor`：基于 ESP-SR AFE 接口（[esp_afe_sr_models.h](https://github.com/espressif/esp-sr)），提供 VAD/AEC/降噪。默认要求 `frame_duration_ms`。
- `NoAudioProcessor`：透传。
- `AudioDebugger`：调试数据回调。

### 5.5 `WakeWord` — [main/audio/wake_word.h](main/audio/wake_word.h) + [main/audio/wake_words/](main/audio/wake_words/)

- `AfeWakeWord`（ESP32-S3/P4）：使用 `esp_afe_sr` 唤醒模型，支持多唤醒词，可向服务器回传唤醒词 Opus 流以辅助 ASR。
- `EspWakeWord`（ESP32/C3）：使用 `esp-sr` 简化路径。
- `CustomWakeWord`：用户自定义。

CMake 中按目标芯片自动选择（见 [main/CMakeLists.txt#L875-L880](main/CMakeLists.txt#L875-L880)）。

### 5.6 OGG 解封装

`audio/demuxer/ogg_demuxer.{cc,h}` — 用于播放 `application.cc::PlaySound()` 引用的 OGG 资源（位于 `main/assets/common/` 与 `main/assets/locales/<lang>/`）。

---

## 6. 通信协议层

### 6.1 协议基类 `Protocol` — [main/protocols/protocol.h](main/protocols/protocol.h)

抽象协议（WebSocket / MQTT+UDP）通用行为。

```cpp
class Protocol {
public:
    virtual ~Protocol() = default;
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
    virtual void SendUserPrompt(const std::string&);   // ★ 调试 TTS

    // 回调注册
    void OnIncomingAudio(cb);
    void OnIncomingJson(cb);
    void OnAudioChannelOpened(cb);
    void OnAudioChannelClosed(cb);
    void OnNetworkError(cb);
    void OnConnected(cb);
    void OnDisconnected(cb);
protected:
    int  server_sample_rate_   = 24000;
    int  server_frame_duration_= 60;
    bool error_occurred_       = false;
    virtual bool SendText(const std::string&) = 0;
};
```

**音频包与二进制协议**：

```cpp
struct AudioStreamPacket {
    int sample_rate = 0;
    int frame_duration = 0;
    uint32_t timestamp = 0;
    std::vector<uint8_t> payload;       // Opus 数据
};

struct BinaryProtocol2 {                // 历史版本（带 timestamp）
    uint16_t version; uint16_t type;     // 0:OPUS, 1:JSON
    uint32_t reserved; uint32_t timestamp; uint32_t payload_size;
    uint8_t payload[];
} __attribute__((packed));

struct BinaryProtocol3 {                // 新版本
    uint8_t type; uint8_t reserved; uint16_t payload_size; uint8_t payload[];
} __attribute__((packed));
```

**枚举**：

```cpp
enum AbortReason { kAbortReasonNone, kAbortReasonWakeWordDetected };
enum ListeningMode { kListeningModeAutoStop, kListeningModeManualStop, kListeningModeRealtime };
```

### 6.2 WebSocket 实现 — [main/protocols/websocket_protocol.{cc,h}](main/protocols/websocket_protocol.h)

- `Start()`：建立到 OTA 配置中的 WebSocket URL，发送 `hello` 消息。
- `OpenAudioChannel()`：协商 SR/帧长；进入 `audio` 通道。
- `SendAudio()`：使用 `BinaryProtocol2/3` 封装 Opus 帧发送。
- `ParseServerHello(root)`：从 JSON 解析采样率、帧长、UDP 配置等。

### 6.3 MQTT+UDP 实现 — [main/protocols/mqtt_protocol.{cc,h}](main/protocols/mqtt_protocol.h)

- 控制消息走 MQTT（`Mqtt` 客户端）。
- 实时音频走 UDP（AES-CTR 加密，本地维护 `local_sequence_` / `remote_sequence_`）。
- `MQTT_PING_INTERVAL_SECONDS = 90`、`MQTT_RECONNECT_INTERVAL_MS = 60000`。
- `mbedtls_aes_context` + `aes_nonce_` 实现 UDP 加密。

---

## 7. 显示子系统（含 AI 罗盘 AttitudeDisplay）

### 7.1 类层次

```
Display (基类, display.h)
 ├── NoDisplay
 ├── LcdDisplay (lcd_display.h)
 │    ├── SpiLcdDisplay
 │    ├── RgbLcdDisplay
 │    └── MipiLcdDisplay
 ├── OledDisplay (oled_display.h)
 └── EmoteDisplay (emote_display.h)
        └── ★ AttitudeDisplay (attitude_display.h)  // AI 罗盘
```

### 7.2 `Display` 接口（精简）

| 方法 | 说明 |
|------|------|
| `SetStatus(const char*)` | 状态栏文本 |
| `ShowNotification(text, duration_ms=3000)` | 短暂通知 |
| `SetEmotion(const char*)` | 表情（emoji / image） |
| `SetChatMessage(role, content)` / `ClearChatMessages()` | 对话字幕 |
| `SetTheme(Theme*)` / `GetTheme()` | 主题 |
| `UpdateStatusBar(bool all=false)` | 电量/网络/时间等 |
| `SetPowerSaveMode(bool)` | 节能 |
| `SetupUI()` | 一次性 UI 构建 |
| `Lock(timeout_ms)` / `Unlock()` | 多线程安全（`DisplayLockGuard` 封装） |

### 7.3 LVGL 适配 — [main/display/lvgl_display/](main/display/lvgl_display/)

- `lvgl_display.{cc,h}`：把 ESP-LCD 面板接入 LVGL 9.x。
- `lvgl_theme.{cc,h}` / `lvgl_font.{cc,h}` / `lvgl_image.{cc,h}` / `emoji_collection.{cc,h}`。
- `gif/`、`jpg/`：GIF 解码（`gifdec`）与 JPEG 编解码。

### 7.4 ★ AI 罗盘 `AttitudeDisplay` — [main/display/attitude_display.{cc,h}](main/display/attitude_display.h)

> 本仓库对上游最重要的差异化模块。屏幕规格 **360×360 圆形 LCD**，主元素为**阴阳鱼太极图**。

#### 7.4.1 视觉分层（设计规范）

| 层 | 内容 | 关键常量 |
|----|------|---------|
| L0 | 太极阴阳鱼 + 鎏金外圈 | `TAIJI_RADIUS=86`、`TAIJI_CANVAS_SIZE=172`、`TAIJI_GOLD_RING_WIDTH=3` |
| L0·鱼眼 | 阴中阳（WiFi）/ 阳中阴（BLE） | `FISHEYE_ICON_SIZE=26`、`FISHEYE_PULSE_MS=300`、`FISHEYE_BORDER_WIDTH=2` |
| L1 | 状态 / 提示核心 | `LAYER1_CORE_RADIUS=49` |
| L2 | 预留 | - |
| L3 | 进度弧（学业计时/运势 Animating） | `LAYER3_PROGRESS_RADIUS=130`、`LAYER3_BG_ARC_RADIUS=117`、`LAYER3_PROGRESS_ARC_RADIUS=126` |
| L4 | 外边界（与屏幕圆边贴齐） | `LAYER4_BOUNDARY_RADIUS = SCREEN_W/2 - GOLD_RING_ARC_WIDTH/2` |

> 静态断言保证尺寸：`static_assert(TAIJI_RADIUS == 86, ...)`。

#### 7.4.2 12 运势菜单环

`FORTUNE_MENU_COUNT = 12`，从 12 点钟起顺时针排列：

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

- 环触摸区：`FORTUNE_MENU_TOUCH_INNER_R = TAIJI_RADIUS - 4` ~ `LAYER4_BOUNDARY_RADIUS`。
- 选中态图标放大 10%（`FORTUNE_MENU_ICON_SCALE_SELECTED`），未选中 `FORTUNE_MENU_ICON_SCALE`。
- 环心相对中点外偏 3px：`FORTUNE_MENU_RING_OUTWARD_PX = 3`。

#### 7.4.3 运势状态机（已简化）

> **重要变更（v2.2.7+）**：完整运势结果卡（Plan A）已**彻底删除**。原本的 `Idle / Animating / Result` 三态状态机已简化为**仅 Idle**：
> - 删除 `FortuneState::Animating` / `FortuneState::Result` 两个状态
> - 删除 `EnterAnimatingState` / `EnterResultState` / `DismissFortune` / `ShowFortune` / `ShowFortuneFromMenu` / `CreateFortuneCard` / `DestroyFortuneCard` / `HighlightDirection` / `HighlightGua` / `StopFortuneAnimatingEffects` / `StartFortuneTaijiPhasedRotation` 全部方法
> - 删除相关定时器、widget、静态回调（`OnFortuneAnimTimer` / `OnFortuneFisheyePulseTimer` / `OnFortuneProgressStepTimer` / `OnFortuneResultDelayTimer` / `OnFortuneResultTimer` / `OnFortuneTaijiRampTimer`）
> - 删除 MCP 工具 `self.attitude.dismiss_fortune`
> - 长按 Boot 仅落日志，不再触发动画或结果卡
> - 短按 Boot 仍可循环选中菜单项（`ShowFortuneMenuFeatureCard` / `ShowFortuneMenuFeatureCardUnlocked`）

#### 7.4.4 结果卡数据结构与接口（已删除）

> 完整结果卡（Plan A）已彻底删除。`ShowFortune` / `ShowFortuneFromMenu` / `HighlightDirection` / `HighlightGua` / `CreateFortuneCard` / `DismissFortune` / `GetFortuneState` 已从公开 API 中移除。
> 短按/长按 Boot 仍使用 `ShowFortuneMenuFeatureCard(int index)` 弹出 12 类运势的**功能区提示卡**（圆形黑底金边+对应 Yi/Ji 文本），并在 5s 后自动消失。

#### 7.4.5 按键/触摸入口

| 方法 | 行为 |
|------|------|
| `HandleBootKey()` | Idle 循环选中运势 / Result 关闭卡 |
| `HandleFortuneBootLongPress()` | Idle 触发当前选中运势（确定） |
| `HandlePowerKey()` | 返回/取消 |

> 对应 [application.h](main/application.h#L99-L102) 的 `HandleFortuneBootKey/HandleFortuneBootLongPress/HandlePowerKey`，由板卡按键回调触发。

#### 7.4.6 调试信息卡

```cpp
void ShowDebugInfo(title, detail, hold_ms = 3000);
void HideDebugInfo();
```

- 用于显示与后台的关键交互事件（如「激活成功」「WiFi 已连接」「MCP 收到工具调用」）。
- 调试 TTS（`Application::RequestDebugTts`）期间叠加播放。

#### 7.4.7 子功能区

通过 `STUDY_SUB_FEATURES_ENABLED` 开关（CMake 选项 `XIAOZHI_STUDY_SUB_FEATURES`，默认 OFF）：
- **学业子态** `StudySubState { Hidden, Menu, FocusRunning, CompleteBgm, DrumPad }`
  - 60s 专注计时 + 5s 完成 BGM。
  - 8 扇区架子鼓触摸 + 中心 Kick（依赖 `drum/drum_synth`）。
- **心情卦子态** `MoodGuaSubState { Hidden, MazePlaying }`（`MOOD_GUA_SUB_FEATURES_ENABLED=1`）
  - 9×9 迷宫（`MAZE_GRID_SIZE`），起点 (0,0)，终点右下角；触摸移动。

#### 7.4.8 颜色系统

见 [main/display/attitude_theme.h](main/display/attitude_theme.h)：

```
COLOR_BG_OUTER  = 0x0A0A0A   // 外层黑底
COLOR_BG_CENTER = 0x121212   // 中心暗灰
COLOR_TEXT_MAIN = 0xD4AF37   // 鎏金（主文）
COLOR_TEXT_SUB  = 0xC0C0C0   // 银灰（副文）
COLOR_TEXT_HIGH = 0xFFFFFF   // 亮白（高亮）
COLOR_BORDER_LINE = 0xD4AF37 // 鎏金描边
COLOR_STATE_HEAVY  = 0xE67E22 // 橙色（heavy）
COLOR_STATE_DANGER = 0xB82601 // 暗红（danger）
```

### 7.5 ★ 太极图组件 `CompassTaiji` — [main/display/compass_taiji.{cc,h}](main/display/compass_taiji.h)

```cpp
class CompassTaiji {
public:
    static void   Create(lv_obj_t* parent, int cx, int cy, int radius);
    static void   Rotate(int delta_angle);          // 0.1° 单位
    static void   SetRotation(int angle);           // 0~3600
    static int    GetRotation();
    static void   ResetRotation();

    static void   StartAutoRotation(int period_ms = 60000); // 默认 60s/圈
    static void   SetAutoRotationPeriod(int period_ms);
    static int    GetAutoRotationPeriod();
    static void   StopAutoRotation();
    static bool   IsAutoRotating();

    static void   SetAutoRotationPaused(bool paused);
    static void   TickAutoRotationStep();           // Animating 时由 LVGL 定时器驱动
    static void   SetStudyRingMode(bool ring_only); // 学业态：仅保留鎏金圈
    static lv_obj_t* GetContainer();                // 鱼眼须作为其子对象共旋转
    static lv_obj_t* GetCanvas();
    static int    GetRadius();

    static void   PaintFisheyeDisc(canvas, size, fill, ring, bg, ring_width);
};
```

- 内部使用预渲染的两份 canvas 快照（`canvas_snapshots_ready_`）在普通/学业模式间 `memcpy` 切换，零开销。
- 描边抗锯齿：方形 canvas 外缘向 `bg` 色混合以避免透明叠底产生杂色。
- `OnAutoRotationTimer` 是 LVGL 定时器回调，按 `auto_rotation_interval_ms_` 节拍推进 `auto_rotation_step_`。

---

## 8. MCP 工具服务

### 8.1 协议对象

- `ImageContent`（[mcp_server.h#L16-L47](main/mcp_server.h#L16-L47)）：base64 编码图片 + MIME。
- `ReturnValue = std::variant<bool, int, std::string, cJSON*, ImageContent*>`（[L50](main/mcp_server.h#L50)）。
- `PropertyType { kPropertyTypeBoolean, kPropertyTypeInteger, kPropertyTypeString }`。
- `Property`（[L58-L156](main/mcp_server.h#L58-L156)）：必填/可选参数，整数支持 `[min,max]` 范围。
- `PropertyList`（[L158-L206](main/mcp_server.h#L158-L206)）：参数集合。
- `McpTool`（[L208-L312](main/mcp_server.h#L208-L312)）：
  - `name / description / properties / callback`
  - `user_only`：是否对 AI 隐藏（`annotations.audience=["user"]`）。
  - `Call(PropertyList)` 返回 `{content:[{type:"text"|"image",...}], isError:false}`。

### 8.2 `McpServer` — [mcp_server.h#L314-L342](main/mcp_server.h#L314-L342)

```cpp
class McpServer {
public:
    static McpServer& GetInstance();

    void AddCommonTools();      // AI 可调
    void AddUserOnlyTools();    // 仅用户可见（不在 AI 工具列表）
    void AddTool(McpTool*);
    void AddTool(name, desc, properties, callback);
    void AddUserOnlyTool(name, desc, properties, callback);

    void ParseMessage(const cJSON* json);
    void ParseMessage(const std::string& message);
private:
    void ParseCapabilities(const cJSON*);
    void ReplyResult(int id, const std::string&);
    void ReplyError(int id, const std::string&);
    void GetToolsList(int id, const std::string& cursor, bool list_user_only_tools);
    void DoToolCall(int id, const std::string& tool_name, const cJSON* tool_arguments);
    std::vector<McpTool*> tools_;
};
```

### 8.3 工具注册典型（来自 [main/application.cc](main/application.cc)）

通过 `McpServer::GetInstance().AddCommonTools()` / `AddUserOnlyTools()` 注册本地工具，例如 `self.set_volume`、`self.screen.set_emotion`、`self.led.set_brightness`、摄像头拍照 `self.camera.take_photo` 等，详见代码与 [docs/mcp-usage_zh.md](docs/mcp-usage_zh.md)。

---

## 9. 板级抽象层（Board）

### 9.1 `Board` 单例 — [main/boards/common/board.h](main/boards/common/board.h)

```cpp
class Board {
public:
    static Board& GetInstance();    // 通过 create_board() 工厂注入

    virtual std::string GetBoardType() = 0;
    virtual std::string GetUuid() { return uuid_; }      // 软件生成 UUID
    virtual Backlight*  GetBacklight();
    virtual Led*        GetLed();
    virtual AudioCodec* GetAudioCodec() = 0;
    virtual bool        GetTemperature(float& esp32temp);
    virtual Display*    GetDisplay();
    virtual Camera*     GetCamera();
    virtual NetworkInterface* GetNetwork() = 0;
    virtual void        StartNetwork() = 0;               // 异步
    virtual void        SetNetworkEventCallback(NetworkEventCallback);
    virtual const char* GetNetworkStateIcon() = 0;
    virtual bool        GetBatteryLevel(int& level, bool& charging, bool& discharging);
    virtual std::string GetSystemInfoJson();
    virtual void        SetPowerSaveLevel(PowerSaveLevel) = 0;
    virtual std::string GetBoardJson() = 0;
    virtual std::string GetDeviceStatusJson() = 0;
protected:
    Board();
    std::string GenerateUuid();
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

**派生类**（位于 [main/boards/common/](main/boards/common/) 与各板卡目录）：

| 类别 | 类 |
|------|------|
| 通用 | `Board`、`I2cDevice`、`Camera` |
| 输入 | `Button`、`Knob`、`PowerSaveTimer`、`SleepTimer`、`SystemReset` |
| 网络 | `WifiBoard`、`Ml307Board`、`Nt26Board`、`DualNetworkBoard`、`RndisBoard` |
| 电源 | `AdcBatteryMonitor`、`Axp2101`、`Sy6970` |
| 音视频 | 板卡专属 `*_audio_codec.cc` |
| 配网 | `Blufi`（`USE_ESP_BLUFI_WIFI_PROVISIONING` 时启用） |
| 机器人 | `OttoRobot`、`ElectronBot`、`EdaRobotPro`、`EdaSuperBear` 等 |

### 9.2 板卡工厂 — [main/boards/common/board.h#L87-L90](main/boards/common/board.h#L87-L90)

```cpp
#define DECLARE_BOARD(BOARD_CLASS_NAME) \
void* create_board() { return new BOARD_CLASS_NAME(); }
```

每个板卡目录的 `*.cc` 在末尾写 `DECLARE_BOARD(XxxBoard)`，由 [main/CMakeLists.txt](main/CMakeLists.txt) 在选择 `BOARD_TYPE` 后把该目录全部 `.cc/.c` 链接进固件。

### 9.3 WiFi 板基类 `WifiBoard` — [main/boards/common/wifi_board.h](main/boards/common/wifi_board.h)

- `StartNetwork()`：异步连接已保存 SSID；超时则进入配网模式。
- `EnterWifiConfigMode()`：线程安全，可从任意任务调用。
- `OnNetworkEvent(event, data)`：把内部 WiFi 事件翻译成统一 `NetworkEvent`，并转发给 Application。
- 配网走 `esp-wifi-connect` 组件或 BluFi（Kconfig 切换）。

---

## 10. OTA / 资源 / 设置 / 系统信息

### 10.1 OTA — [main/ota.{cc,h}](main/ota.h)

```cpp
class Ota {
public:
    esp_err_t CheckVersion();        // 查询新版本
    esp_err_t Activate();            // 完成设备激活
    bool HasActivationChallenge();
    bool HasNewVersion();
    bool HasMqttConfig();
    bool HasWebsocketConfig();
    bool HasActivationCode();
    bool HasServerTime();
    bool StartUpgrade(std::function<void(int progress, size_t speed)>);
    static bool Upgrade(const std::string& url, std::function<void(int, size_t)>);
    void MarkCurrentVersionValid();
    // ... 字段省略
private:
    std::vector<int> ParseVersion(const std::string&);
    bool IsNewVersionAvailable(const std::string& cur, const std::string& new_);
    std::string GetActivationPayload();
    std::unique_ptr<Http> SetupHttp();
};
```

OTA URL 通过 Kconfig `OTA_URL` 配置（默认 `https://api.tenclass.net/xiaozhi/ota/`）。

### 10.2 资源管理 `Assets` — [main/assets.h](main/assets.h)

- `Download(url, progress_cb)` 下载新资源；
- `Apply(refresh_display_theme=true)` 切换到新资源分区；
- `GetAssetData(name, ptr, size)` 按名取数据；
- `partition_valid()` 判断 OTA assets 分区是否合法。

构建期通过 [scripts/build_default_assets.py](scripts/build_default_assets.py) 把字体、emoji、表情等打成 `assets.bin`，按 `CONFIG_FLASH_DEFAULT_ASSETS / _CUSTOM_ASSETS / _EXPRESSION_ASSETS / _NONE_ASSETS` 选择烧录策略（见 [main/CMakeLists.txt#L1252-L1276](main/CMakeLists.txt#L1252-L1276)）。

### 10.3 设置 `Settings` — [main/settings.h](main/settings.h)

参见 4.4 节。

### 10.4 系统信息 `SystemInfo` — [main/system_info.h](main/system_info.h)

- 收集 `BoardJson`、`DeviceStatusJson`，被 MCP 工具调用或协议 JSON 消息使用。
- 通常包含：芯片型号、固件版本、内存、SD 卡、电量等。

---

## 11. BLE 与架子鼓子系统

### 11.1 ★ `BleServer` — [main/ble/ble_server.{cc,h}](main/ble/ble_server.h)

轻量 NimBLE 外设，仅用于把 BLE 状态广播给罗盘「阳中阴」鱼眼。

```cpp
class BleServer {
public:
    using StatusCallback = std::function<void(BleStatus status)>;
    static BleServer& GetInstance();

    esp_err_t Start();            // 启动广播
    esp_err_t Stop();             // BluFi 配网前须 Stop()
    void SetStatusCallback(cb);
    BleStatus GetStatus() const;
    bool IsRunning() const;

    void NotifyStatus(BleStatus);  // NimBLE GAP 回调使用
private:
    StatusCallback status_callback_;
    BleStatus status_ = BleStatus::DISABLED;
    bool running_ = false;
};
```

Kconfig `XIAOZHI_ENABLE_BLE_FISHEYE` 开启（CMake 才会把 `ble/ble_server.cc` 编入）。

### 11.2 ★ `DrumSynth` — [main/drum/drum_synth.{cc,h}](main/drum/drum_synth.h)

> 「单设备多音色」方案的核心——触摸 → OGG 解码 → 入播放队列 → 喇叭发声。

```cpp
namespace drum {
enum class Piece : uint8_t {
    KICK = 0, SNARE, HIHAT_CLOSED, HIHAT_OPEN,
    TOM_HI, TOM_MID, CRASH, RIDE, COUNT = 8
};
using TriggerCallback = std::function<void(Piece)>;

class DrumSynth {
public:
    static DrumSynth& GetInstance();
    void Init();
    bool Trigger(Piece, uint8_t velocity = 100);
    void StopAll();
    void SetActive(bool);
    bool IsActive() const;
    void SetTriggerCallback(TriggerCallback);
};
}
```

- 设计目标：首次触发延迟 < 30 ms，命中缓存后 < 5 ms。
- 与语音系统共用 Idle 态：架子鼓模式下暂停 AFE/VAD 避免误唤醒。
- 资源来自 `main/assets/drum/*.ogg`（kick / snare / hihat_* / tom_* / crash / ride）。
- 由 CMake `XIAOZHI_STUDY_SUB_FEATURES=ON` 打开；UI 由 `AttitudeDisplay` 在 `DrumPad` 子态下渲染 8 扇区 + 中心 Kick。

---

## 12. 串口截图验证服务

### 12.1 `SnapshotService` — [main/display/snapshot/snapshot_service.{cc,h}](main/display/snapshot/snapshot_service.h)

调试 / 自动化验证用：通过 USB-Serial/JTAG 串口输出 JPEG 截图与按钮触发命令。

```cpp
class SnapshotService {
public:
    static SnapshotService& GetInstance();
    esp_err_t Initialize();          // 注册协议 + 创建 UART 任务
    esp_err_t Start();               // 启动 UART 接收任务
    esp_err_t Stop();
    esp_err_t TakeSnapshot();        // 主动截图一次
    esp_err_t TriggerButtonClick(int index); // 触发罗盘 4 个调试按钮之一
    bool IsRunning() const;
private:
    static void UARTTask(void*);     // 接收 SNAP/CLICK/PING 等命令
    esp_err_t ExecuteSnapshot();
    bool CaptureAndEncode(uint8_t** jpeg_data, size_t* jpeg_len); // LVGL→JPEG
    esp_err_t SendData(const uint8_t*, size_t);    // Base64 后按行输出
    size_t Base64Encode(const uint8_t*, size_t, uint8_t*);
    size_t Base64EncodeLength(size_t);
    void SendACK(snapshot_error_t);
    void SendPONG();
};
```

### 12.2 命令协议 — [main/display/snapshot/snapshot_protocol.h](main/display/snapshot/snapshot_protocol.h)

二进制/ASCII 协议：
- `SNAP`：单帧截图
- `CLICK <idx>`：触发 0..3 索引的罗盘功能按钮（默认 0=今日运势 / 1=财运 / 2=健康 / 3=求财）
- `PING`：回 `PONG` 用于存活探测

详见 [doc/SNAPSHOT_USAGE.md](doc/SNAPSHOT_USAGE.md) 与 [tools/screenshot_with_log.py](tools/screenshot_with_log.py)。

### 12.3 启动截图任务 — [main/main.cc#L29-L67](main/main.cc#L29-L67)

```cpp
static void screenshot_task(void* arg) {
    auto& svc = SnapshotService::GetInstance();
    vTaskDelay(pdMS_TO_TICKS(3000));   // 等待屏幕初始化
    while (true) {
        svc.TakeSnapshot();
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
```

由 `XIAOZHI_ENABLE_BOOT_SCREENSHOT`（默认 1）控制是否创建。

---

## 13. 依赖关系

### 13.1 CMake 模块汇总（[main/CMakeLists.txt](main/CMakeLists.txt#L1034-L1057)）

```
esp_pm, esp_psram, esp_netif,
esp_driver_gpio, esp_driver_uart, esp_driver_spi,
esp_driver_i2c, esp_driver_i2s, esp_driver_jpeg, esp_driver_ppa,
esp_app_format, app_update, spi_flash, console, efuse, bt, fatfs
```

### 13.2 组件依赖（[main/idf_component.yml](main/idf_component.yml)）

| 类别 | 组件 |
|------|------|
| LCD 面板 | `waveshare/esp_lcd_sh8601`, `espressif/esp_lcd_co5300`, `esp_lcd_ili9341`, `esp_lcd_gc9a01`, `esp_lcd_st77916`, `esp_lcd_axs15231b`, `esp_lcd_st7701`, `esp_lcd_st7796`, `esp_lcd_spd2010`, `78/esp_lcd_nv3023`, `espressif/esp_lcd_jd9365`, `waveshare/esp_lcd_st7703`, `espressif/esp_lcd_ili9881c`, `espressif/esp_lcd_ek79007` |
| IO Expander / Touch | `esp_io_expander_tca9554`, `waveshare/custom_io_expander_ch32v003`, `esp_io_expander_tca95xx_16bit`, `esp_lcd_touch_ft5x06`, `esp_lcd_touch_gt911`, `esp_lcd_touch_gt1151`, `waveshare/esp_lcd_touch_cst9217`, `esp_lcd_touch_cst816s`, `esp_lcd_touch_st7123`, `tny-robotics/sh1106-esp-idf` |
| 网络 / 4G | `78/esp-wifi-connect`, `78/esp-ml307`, `78/uart-eth-modem`, `espressif/esp_hosted`, `espressif/esp_wifi_remote`, `espressif/iot_usbh_rndis` |
| 音频 | `espressif/esp_audio_effects`, `espressif/esp_audio_codec`, `espressif/esp_codec_dev` |
| AI / 语音 | `espressif/esp-sr`, `espressif/esp_image_effects`, `espressif/adc_battery_estimation`, `espressif/esp_new_jpeg`, `espressif2022/image_player`, `espressif2022/esp_emote_expression` |
| 显示 | `lvgl/lvgl`, `esp_lvgl_port`, `espressif/esp_mmap_assets`, `78/xiaozhi-fonts`, `txp666/otto-emoji-gif-component` |
| 输入 | `espressif/button`, `espressif/knob`, `espressif/bmi270_sensor`, `espressif/touch_slider_sensor`, `espressif/touch_button_sensor` |
| LED | `espressif/led_strip`, `esphome/esp-hub75` |
| 视觉/相机 | `espressif/esp32-camera`, `espressif/esp_video`, `espressif/esp32_p4_function_ev_board` |
| 机器人 | `espfriends/servo_dog_ctrl`, `llgok/cpp_bus_driver` |
| SenseCAP | `wvirgil123/sscma_client` |

### 13.3 上游/三方服务（参考性，不参与编译）

- 官方服务端：[xiaozhi.me](https://xiaozhi.me)
- 自部署服务端（[README.md](README.md)）：
  - [xinnan-tech/xiaozhi-esp32-server](https://github.com/xinnan-tech/xiaozhi-esp32-server)（Python）
  - [joey-zhou/xiaozhi-esp32-server-java](https://github.com/joey-zhou/xiaozhi-esp32-server-java)（Java）
  - [AnimeAIChat/xiaozhi-server-go](https://github.com/AnimeAIChat/xiaozhi-server-go)、[hackers365/xiaozhi-esp32-server-golang](https://github.com/hackers365/xiaozhi-esp32-server-golang)（Go）
- 其他客户端：[huangjunsen0406/py-xiaozhi](https://github.com/huangjunsen0406/py-xiaozhi)、[TOM88812/xiaozhi-android-client](https://github.com/TOM88812/xiaozhi-android-client)、[100askTeam/xiaozhi-linux](http://github.com/100askTeam/xiaozhi-linux)、[78/xiaozhi-sf32](https://github.com/78/xiaozhi-sf32)、[QuecPython/solution-xiaozhiAI](https://github.com/QuecPython/solution-xiaozhiAI)
- 资源生成：[78/xiaozhi-assets-generator](https://github.com/78/xiaozhi-assets-generator)

---

## 14. 构建与运行

### 14.1 通用步骤

1. **克隆并初始化子模块**（重要 — 见 [`.tools/submod.log`](.tools/)）
   ```bash
   git clone https://github.com/78/xiaozhi-esp32.git
   cd xiaozhi-esp32
   git submodule update --init --recursive
   ```
2. **ESP-IDF 环境**
   - 安装 ESP-IDF ≥ 5.5.2（见 [main/idf_component.yml](main/idf_component.yml)）。
   - 推荐 Linux 编译速度更快。
3. **配置**
   ```bash
   idf.py set-target esp32s3        # 或 esp32 / esp32c3 / esp32p4 ...
   idf.py menuconfig
   ```
   关键项：`Xiaozhi Assistant → Board Type / Default Language / OTA URL`、
   `Audio → Use Audio Processor / Send Wake Word Data`、
   `Application → Enable BLE Fisheye`（可选）。
4. **编译烧录**
   ```bash
   ./build_and_flash.sh             # 一键（推荐，见 .trae/rules/rule_xiaozhi.md）
   # 或
   idf.py build flash monitor
   ```
5. **首次启动**：默认连接 `xiaozhi.me` 官方服务器；未激活设备显示 6 位激活码，到 [xiaozhi.me 控制台](https://xiaozhi.me) 完成绑定。

### 14.2 本仓库的一键烧录脚本 — [build_and_flash.sh](build_and_flash.sh)

依据 `.trae/rules/rule_xiaozhi.md` 规范：

- 必须使用项目根目录的 `build_and_flash.sh`，**不要**使用 `2>&1` 重定向以保留完整错误信息。
- 也提供 Windows 版本 [build_and_flash.bat](build_and_flash.bat) / [build_and_flash.ps1](build_and_flash.ps1)。

### 14.3 板型选择（节选 — 完整列表见 [main/CMakeLists.txt#L101-L854](main/CMakeLists.txt#L101-L854)）

| 板型 Kconfig | 目录 | 备注 |
|--------------|------|------|
| `BREAD_COMPACT_WIFI` | `bread-compact-wifi` | 默认面包板（带屏/无屏等变体） |
| `ESP_BOX_3` | `esp-box-3` | 乐鑫 ESP-BOX-3 |
| `M5STACK_CORE_S3` | `m5stack-core-s3` | M5Stack CoreS3 |
| `M5STACK_ATOM_S3R_ECHO_BASE` | `atoms3r-echo-base` | M5Stack AtomS3R + Echo Base |
| `LILYGO_T_CIRCLE_S3` | `lilygo-t-circle-s3` | LILYGO T-Circle-S3（圆形屏，AI 罗盘备选板） |
| `LICHUANG_DEV_S3` | `lichuang-dev` | 立创实战派 ESP32-S3 |
| `XINGZHI_CUBE_1_54TFT_WIFI` | `xingzhi-cube-1.54tft-wifi` | 醒目 1.54 TFT |
| `ESP_P4_FUNCTION_EV_BOARD` | `esp-p4-function-ev-board` | ESP32-P4 评估板 |
| `WAVESHARE_ESP32_S3_TOUCH_AMOLED_*` | `esp32-s3-touch-amoled-*` | 微雪多款 AMOLED |
| `SEEED_STUDIO_SENSECAP_WATCHER` | `sensecap-watcher` | SenseCAP Watcher |
| `OTTO_ROBOT` / `ELECTRON_BOT` | `otto-robot` / `electron-bot` | 机器人 Otto / Electron Bot |
| ... | ... | 共 70+ |

### 14.4 自动化验证闭环（来自 `.trae/rules/rule_xiaozhi.md` §4、§5）

烧录后必须进入验证闭环：

```bash
# 1. 启动后台截图脚本（默认每 5s 一张）
# 2. 单次截图
python3 tools/screenshot_with_log.py --snap
# 3. 触发按钮索引 0..3（见 §CLICK 按钮索引）
python3 tools/screenshot_with_log.py --click 0
python3 tools/screenshot_with_log.py --click 1
python3 tools/screenshot_with_log.py --click 2
```

- 输出路径：`screenshots/screenshot_latest.jpg`、`screenshots/history/screenshot_<ts>_<idx>.jpg`、`screenshots/logs/run_<ts>.log`。
- 串口占用时：`lsof /dev/cu.usbmodem1101`，`kill -9 <PID>` 释放。
- CLICK 无响应时：检查 UART 接收缓冲区（建议 ≥ 8192 字节）。

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
      期间 on_playback_finished → UI 反馈（如调试信息卡隐藏）
```

### 15.2 MCP 工具调用流程

```
  服务器 ──► JSON-RPC { method:"tools/call", params:{name, arguments} }
        ↓
  Application::OnIncomingJson()
        ↓
  McpServer::ParseMessage(json)
        ↓
  DoToolCall(id, name, arguments)
        ↓
  McpTool::Call(PropertyList) → 同步执行本地回调
        ↓
  ReplyResult(id, json_text)   ──► 服务器
```

### 15.3 AI 罗盘交互流程（姿态显示板）

> **v2.2.7+ 变更**：完整 Animating→Result 流程已删除（Plan A）。现仅保留菜单选中/取消 + 功能区提示卡 + 调试信息卡。

```
  Idle (Boot 短按)  ─►  CycleFortuneMenuSelectionUnlocked()      // 选中项移动
                       └─► ShowFortuneMenuFeatureCardUnlocked(index)  // 显示功能区提示卡（5s 后自动消失）
  Idle (Boot 长按)  ─►  HandleFortuneBootLongPress() → 仅落日志（不再触发结果卡）
  调试信息卡
    - ShowDebugInfo(title, detail, hold_ms)
    - 与 Application::RequestDebugTts() 联动：仅 Idle + WiFi 已连接时真正发声
```

### 15.4 状态切换合法性（DeviceStateMachine 内部）

```
  Unknown ─► Starting ─► Activating ─► Idle ◄───► Listening ◄───► Speaking
                 │                       │                               
                 ▼                       ▼                               
              WifiConfiguring       Connecting (Mqtt/WS 重连)            
                 │                       │                               
                 └──────── Upgrading ────┘                               
                                 │                                       
                                 ▼                                       
                            FatalError                                   
```

非法转换将被 `TransitionTo()` 拒绝并记录日志。

---

## 16. 配置选项（Kconfig）

来自 [main/Kconfig.projbuild](main/Kconfig.projbuild)（节选）：

| 选项 | 说明 |
|------|------|
| `BOARD_TYPE_*` | 70+ 板型之一 |
| `OTA_URL` | OTA 校验 / 升级服务地址 |
| `LANGUAGE_*` | 32 种语言之一（决定打包的 OGG 资源子集，缺失文件回退到 en-US） |
| `FLASH_DEFAULT_ASSETS` / `FLASH_CUSTOM_ASSETS` / `FLASH_EXPRESSION_ASSETS` / `FLASH_NONE_ASSETS` | 资产烧录策略 |
| `USE_ESP_BLUFI_WIFI_PROVISIONING` | BluFi 配网 |
| `CONFIG_USE_AUDIO_PROCESSOR` | 启用 AFE（VAD/AEC/NS） |
| `CONFIG_SEND_WAKE_WORD_DATA` | 把唤醒词片段发给服务器辅助 ASR |
| `XIAOZHI_ENABLE_BLE_FISHEYE` | 启用 BLE 鱼眼指示（编译 `ble/ble_server.cc`） |
| `XIAOZHI_STUDY_SUB_FEATURES`（CMake） | 编译架子鼓 / 学业计时（默认 OFF） |
| `XIAOZHI_ENABLE_BOOT_SCREENSHOT`（编译宏） | 启动截图后台任务（默认 1） |

> 额外的「罗盘专属」逻辑（菜单定义 `kFortuneMenuDefs[]`、状态机、`TaijiAutoRotation`）由代码常量 + `attitude_display.cc` 实现，不依赖 Kconfig。

---

## 17. 附录：板型一览

> 完整 70+ 列表见 [main/CMakeLists.txt](main/CMakeLists.txt#L101-L854) 与 [README.md](README.md)。下表为代表性节选：

| 制造商 | 板型示例 | 屏 / 特性 |
|--------|---------|---------|
| 乐鑫官方 | esp-box / esp-box-3 / esp-box-lite | 触摸屏 |
| 乐鑫官方 | esp-spot / esp-sparkbot / esp-hi / esp-vocat | 摄像头/机器人 |
| 乐鑫官方 | esp-s3-lcd-ev-board / esp-p4-function-ev-board | 评估板 |
| 乐鑫官方 | esp32-cgc / esp32-cgc-144 | CGC 系列 |
| 乐鑫官方 | esp32s3-korvo2-v3 | 语音开发板 |
| M5Stack | atoms3-echo-base / atoms3r-echo-base / atom-echos3r / atommatrix-echo-base | Echo Base 麦克风 |
| M5Stack | m5stack-core-s3 / m5stack-cardputer-adv / m5stack-tab5 | Core/Cardputer/Tab5 |
| 立创 | lichuang-dev / lichuang-c3-dev | 实战派 |
| LilyGO | t-circle-s3 / t-cameraplus-s3 / t-display-s3-pro-mvsrlora / t-display-p4 | 圆形 / 摄像头 / 4 寸 |
| 微雪 | esp32-s3-touch-amoled-* / esp32-s3-touch-lcd-* / esp32-p4-wifi6-touch-lcd-* | 多款 AMOLED/LCD |
| SenseCAP | sensecap-watcher | AI 监控 |
| 见闻识物 | esp-hi（机器人） | 4 足 |
| 移柯 | movecall-cuican-esp32s3 / movecall-moji-esp32s3 | 挂坠/手环 |
| Otto | otto-robot / electron-bot | 双足机器人 |
| 立创训练营 | eda-robot-pro / eda-super-bear / eda-tv-pro | 教具 |
| 中科蓝讯 | aipi-lite | 入门 |
| 矽诺微 | df-k10 / df-s3-ai-cam | AI 摄像头 |
| 河洛 | hu-087 | 教育 |
| 其他 | atk-dnesp32s3* / du-chatx / jiuchuan-s3 / kevin-box-2 / kevin-c3 / magiclick-* / mpython-v3 / surfer-c3 / xmini-c3 / yunliao-s3 / zaoque-ai ... | — |

---

*文档基于仓库当前快照自动生成；如需更新模块说明，请优先以源码注释与 `.trae/rules/rule_xiaozhi.md` 为准。*