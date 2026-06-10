# 小智 AI 聊天机器人 Code Wiki

## 1. 项目概述

### 1.1 项目简介
小智 AI 聊天机器人是一个基于 ESP32 平台的语音交互项目，利用 Qwen/DeepSeek 等大模型的 AI 能力，通过 MCP（Model Context Protocol）协议实现多端控制。项目支持 Wi-Fi 和 4G（ML307 Cat.1）网络连接，具备离线语音唤醒、OPUS 音频编解码、流式 ASR + LLM + TTS 语音交互等功能。

### 1.2 项目版本
- **当前版本**: 2.2.6
- **v1 稳定版本**: 1.9.2（通过 `git checkout v1` 切换）

### 1.3 支持的平台
- ESP32
- ESP32-S3
- ESP32-C3
- ESP32-C5
- ESP32-P4

---

## 2. 项目架构

### 2.1 整体架构图

```
┌─────────────────────────────────────────────────────────────────┐
│                         Application                              │
│                  (应用层 - 状态机管理)                            │
├─────────────────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐             │
│  │   Audio     │  │   Display   │  │    LED      │             │
│  │   Service   │  │   Service   │  │   Service   │             │
│  └─────────────┘  └─────────────┘  └─────────────┘             │
├─────────────────────────────────────────────────────────────────┤
│  ┌─────────────────────┐  ┌─────────────────────┐              │
│  │  WebsocketProtocol  │  │   MqttProtocol       │              │
│  │     (通信协议)       │  │     (通信协议)        │              │
│  └─────────────────────┘  └─────────────────────┘              │
├─────────────────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐             │
│  │    Board    │  │    OTA     │  │  McpServer  │             │
│  │  (硬件抽象)  │  │  (升级)    │  │ (MCP工具)   │             │
│  └─────────────┘  └─────────────┘  └─────────────┘             │
└─────────────────────────────────────────────────────────────────┘
```

### 2.2 目录结构

```
xiaozhi-esp32/
├── main/                          # 主源代码目录
│   ├── main.cc                    # 应用入口
│   ├── application.cc/.h          # 核心应用类（状态机管理）
│   ├── audio/                     # 音频模块
│   │   ├── audio_service.cc/.h    # 音频服务（编解码、队列管理）
│   │   ├── audio_codec.cc/.h      # 音频编解码器接口
│   │   ├── codecs/                # 具体编解码器实现
│   │   ├── processors/             # 音频处理器（AFE）
│   │   ├── wake_words/            # 唤醒词检测
│   │   └── demuxer/               # OGG解封装
│   ├── display/                   # 显示模块
│   │   ├── display.cc/.h          # 显示接口
│   │   ├── lcd_display.cc/.h     # LCD显示
│   │   ├── oled_display.cc/.h     # OLED显示
│   │   ├── emote_display.cc/.h    # 表情显示
│   │   └── lvgl_display/          # LVGL显示驱动
│   ├── led/                       # LED控制模块
│   │   ├── led.h                  # LED接口
│   │   ├── single_led.cc/.h       # 单LED
│   │   ├── circular_strip.cc/.h   # 环形灯带
│   │   └── gpio_led.cc/.h         # GPIO LED
│   ├── protocols/                 # 通信协议模块
│   │   ├── protocol.cc/.h        # 协议基类
│   │   ├── websocket_protocol.cc/.h  # WebSocket协议
│   │   └── mqtt_protocol.cc/.h    # MQTT协议
│   ├── boards/                    # 板级支持包
│   │   ├── common/                # 公共板级代码
│   │   │   ├── board.cc/.h       # 板级抽象
│   │   │   ├── wifi_board.cc/.h  # WiFi板级支持
│   │   │   ├── ml307_board.cc/.h # 4G模块板级支持
│   │   │   └── ...               # 其他公共组件
│   │   └── <board_name>/          # 各具体开发板适配
│   ├── mcp_server.cc/.h          # MCP服务器实现
│   ├── device_state_machine.cc/.h # 设备状态机
│   ├── device_state.h            # 设备状态枚举
│   ├── ota.cc/.h                 # OTA升级
│   ├── settings.cc/.h            # 设置管理（NVS）
│   ├── system_info.cc/.h         # 系统信息
│   ├── assets.cc/.h              # 资源管理
│   └── assets/                   # 多语言语音资源
├── CMakeLists.txt                # 项目构建配置
├── docs/                         # 文档目录
└── partitions/                   # 分区表配置
```

---

## 3. 核心模块详解

### 3.1 应用层（Application）

**文件**: `main/application.cc`, `main/application.h`

**职责**: 核心应用类，负责整体状态机管理、事件调度、网络连接管理和协议初始化。

**主要功能**:
- 设备状态管理（启动→激活→空闲→对话→说话）
- 事件驱动架构（FreeRTOS EventGroup）
- 协议生命周期管理
- 音频服务与协议的协调

**关键类**:

```cpp
class Application {
public:
    static Application& GetInstance();  // 单例模式
    
    void Initialize();    // 初始化（显示、音频、网络回调）
    void Run();          // 主事件循环
    
    // 状态管理
    DeviceState GetDeviceState() const;
    bool SetDeviceState(DeviceState state);
    
    // 事件调度
    void Schedule(std::function<void()>&& callback);
    
    // 音频控制
    void ToggleChatState();
    void StartListening();
    void StopListening();
    void AbortSpeaking(AbortReason reason);
    
    // 网络与协议
    void ResetProtocol();
    void SendMcpMessage(const std::string& payload);
    
    // AEC模式
    void SetAecMode(AecMode mode);
    AecMode GetAecMode() const;
};
```

**设备状态枚举** (`device_state.h`):
```cpp
enum DeviceState {
    kDeviceStateUnknown,         // 未知
    kDeviceStateStarting,        // 启动中
    kDeviceStateWifiConfiguring, // WiFi配置模式
    kDeviceStateIdle,            // 空闲
    kDeviceStateConnecting,      // 连接中
    kDeviceStateListening,      // 监听中
    kDeviceStateSpeaking,       // 说话中
    kDeviceStateUpgrading,      // 升级中
    kDeviceStateActivating,     // 激活中
    kDeviceStateAudioTesting,   // 音频测试
    kDeviceStateFatalError       // 致命错误
};
```

**事件定义**:
```cpp
#define MAIN_EVENT_SCHEDULE             (1 << 0)
#define MAIN_EVENT_SEND_AUDIO           (1 << 1)
#define MAIN_EVENT_WAKE_WORD_DETECTED   (1 << 2)
#define MAIN_EVENT_VAD_CHANGE           (1 << 3)
#define MAIN_EVENT_ERROR                (1 << 4)
#define MAIN_EVENT_ACTIVATION_DONE      (1 << 5)
#define MAIN_EVENT_CLOCK_TICK           (1 << 6)
#define MAIN_EVENT_NETWORK_CONNECTED    (1 << 7)
#define MAIN_EVENT_NETWORK_DISCONNECTED (1 << 8)
#define MAIN_EVENT_TOGGLE_CHAT         (1 << 9)
#define MAIN_EVENT_START_LISTENING       (1 << 10)
#define MAIN_EVENT_STOP_LISTENING        (1 << 11)
#define MAIN_EVENT_STATE_CHANGED        (1 << 12)
```

### 3.2 设备状态机（DeviceStateMachine）

**文件**: `main/device_state_machine.cc`, `main/device_state_machine.h`

**职责**: 管理设备状态转换，确保状态转换的合法性和线程安全。

**关键方法**:
```cpp
class DeviceStateMachine {
    DeviceState GetState() const;                    // 获取当前状态
    bool TransitionTo(DeviceState new_state);        // 状态转换
    bool CanTransitionTo(DeviceState target) const; // 检查转换合法性
    
    // 状态变化监听器（观察者模式）
    int AddStateChangeListener(StateCallback callback);
    void RemoveStateChangeListener(int listener_id);
};
```

### 3.3 音频服务（AudioService）

**文件**: `main/audio/audio_service.cc`, `main/audio/audio_service.h`

**职责**: 核心音频管理，处理麦克风输入、扬声器输出、Opus编解码、VAD检测。

**音频数据流**:
```
MIC → [Processors] → {Encode Queue} → [Opus Encoder] → {Send Queue} → Server
Server → {Decode Queue} → [Opus Decoder] → {Playback Queue} → Speaker
```

**关键类**:
```cpp
class AudioService {
    // 初始化与生命周期
    void Initialize(AudioCodec* codec);
    void Start();
    void Stop();
    
    // 唤醒词
    void EncodeWakeWord();
    std::unique_ptr<AudioStreamPacket> PopWakeWordPacket();
    void EnableWakeWordDetection(bool enable);
    
    // 音频处理
    void EnableVoiceProcessing(bool enable);
    void EnableAudioTesting(bool enable);
    void EnableDeviceAec(bool enable);
    
    // 编解码
    bool PushPacketToDecodeQueue(std::unique_ptr<AudioStreamPacket> packet);
    std::unique_ptr<AudioStreamPacket> PopPacketFromSendQueue();
    
    // 播放
    void PlaySound(const std::string_view& sound);
    void ResetDecoder();
    
    // 状态查询
    bool IsVoiceDetected() const;
    bool IsIdle();
    bool IsWakeWordRunning() const;
    bool IsAudioProcessorRunning() const;
};
```

**关键配置**:
```cpp
#define OPUS_FRAME_DURATION_MS 60      // Opus帧时长
#define MAX_ENCODE_TASKS_IN_QUEUE 2
#define MAX_PLAYBACK_TASKS_IN_QUEUE 2
#define MAX_DECODE_PACKETS_IN_QUEUE 40   // 2400ms / 60ms
#define MAX_SEND_PACKETS_IN_QUEUE 40
```

### 3.4 音频编解码器

**文件**: `main/audio/audio_codec.h`, `main/audio/codecs/*.cc`

**职责**: 硬件音频编解码器的抽象接口和实现。

**接口定义**:
```cpp
class AudioCodec {
public:
    virtual ~AudioCodec() = default;
    
    virtual bool Initialize() = 0;
    virtual void Configure(int sample_rate, int bits, int channels) = 0;
    virtual int GetInputSampleRate() const = 0;
    virtual int output_sample_rate() const = 0;
    
    virtual size_t Read(void* buffer, size_t size) = 0;  // 麦克风输入
    virtual size_t Write(const void* buffer, size_t size) = 0;  // 扬声器输出
    
    virtual void SetVolume(int volume) = 0;  // 0-100
    virtual int GetVolume() const = 0;
    
    virtual void SetInputGain(float gain) = 0;
    virtual float GetInputGain() const = 0;
};
```

**支持的编解码器**:
- `DummyAudioCodec` - 空实现
- `NoAudioCodec` - 无编解码
- `BoxAudioCodec` - ESP-BOX系列
- `Es8311AudioCodec` - ES8311芯片
- `Es8374AudioCodec` - ES8374芯片
- `Es8388AudioCodec` - ES8388芯片
- `Es8389AudioCodec` - ES8389芯片

### 3.5 音频处理器

**文件**: `main/audio/processors/afe_audio_processor.cc`, `main/audio/processors/audio_debugger.h`

**职责**: 音频信号处理，包括回声消除（AEC）、降噪、语音活动检测（VAD）。

**类型**:
- `AfeAudioProcessor` - 使用ESP-SR的AFE处理
- `NoAudioProcessor` - 无处理
- `AudioDebugger` - 调试用

### 3.6 唤醒词检测

**文件**: `main/audio/wake_words/*.h`

**类型**:
- `AfeWakeWord` - AFE唤醒词（ESP32-S3/P4）
- `EspWakeWord` - ESP唤醒词（ESP32-C3）
- `CustomWakeWord` - 自定义唤醒词

### 3.7 通信协议（Protocol）

**文件**: `main/protocols/protocol.h`, `main/protocols/websocket_protocol.h`, `main/protocols/mqtt_protocol.h`

#### 3.7.1 协议基类

```cpp
class Protocol {
public:
    virtual ~Protocol() = default;
    
    // 生命周期
    virtual bool Start() = 0;
    virtual bool OpenAudioChannel() = 0;
    virtual void CloseAudioChannel(bool send_goodbye = true) = 0;
    virtual bool IsAudioChannelOpened() const = 0;
    
    // 数据传输
    virtual bool SendAudio(std::unique_ptr<AudioStreamPacket> packet) = 0;
    virtual void SendWakeWordDetected(const std::string& wake_word);
    virtual void SendStartListening(ListeningMode mode);
    virtual void SendStopListening();
    virtual void SendAbortSpeaking(AbortReason reason);
    virtual void SendMcpMessage(const std::string& message);
    
    // 回调设置
    void OnIncomingAudio(std::function<void(...)> callback);
    void OnIncomingJson(std::function<void(const cJSON*)> callback);
    void OnAudioChannelOpened(std::function<void()> callback);
    void OnAudioChannelClosed(std::function<void()> callback);
    void OnNetworkError(std::function<void(const string&)> callback);
    void OnConnected(std::function<void()> callback);
    void OnDisconnected(std::function<void()> callback);
};
```

#### 3.7.2 WebSocket协议

```cpp
class WebsocketProtocol : public Protocol {
    bool Start() override;
    bool SendAudio(...) override;
    bool OpenAudioChannel() override;
    void CloseAudioChannel(...) override;
    bool IsAudioChannelOpened() const override;
};
```

#### 3.7.3 MQTT协议

MQTT+UDP混合协议:
- MQTT: 控制消息、状态同步、JSON数据
- UDP: 实时音频数据传输（加密）

#### 3.7.4 音频数据包格式

```cpp
struct AudioStreamPacket {
    int sample_rate = 0;
    int frame_duration = 0;
    uint32_t timestamp = 0;
    std::vector<uint8_t> payload;  // Opus编码数据
};

struct BinaryProtocol2 {
    uint16_t version;
    uint16_t type;       // 0: OPUS, 1: JSON
    uint32_t reserved;
    uint32_t timestamp;  // 用于服务端AEC
    uint32_t payload_size;
    uint8_t payload[];
};
```

### 3.8 显示模块

**文件**: `main/display/display.h`, `main/display/lcd_display.h`, `main/display/oled_display.h`, `main/display/lvgl_display/`

**职责**: 设备屏幕显示，支持LCD、OLED、LVGL等多种显示方案。

**接口**:
```cpp
class Display {
public:
    virtual ~Display() = default;
    
    virtual void SetupUI() = 0;
    virtual void SetStatus(const char* status) = 0;
    virtual void SetEmotion(const char* emotion) = 0;
    virtual void SetChatMessage(const char* role, const char* message) = 0;
    virtual void ClearChatMessages() = 0;
    virtual void ShowNotification(const char* message, int duration_ms = 30000) = 0;
    virtual void UpdateStatusBar(bool force = false) = 0;
};
```

**显示类型**:
- `LcdDisplay` - LCD显示驱动
- `OledDisplay` - OLED显示驱动
- `LvglDisplay` - LVGL图形库显示
- `EmoteDisplay` - 表情显示

### 3.9 LED控制模块

**文件**: `main/led/led.h`, `main/led/single_led.h`, `main/led/circular_strip.h`

**职责**: LED灯控制，支持单LED、环形灯带、GPIO LED等。

```cpp
class Led {
public:
    virtual ~Led() = default;
    virtual void OnStateChanged() = 0;  // 根据设备状态改变LED效果
    virtual void SetBrightness(int brightness) = 0;
};
```

### 3.10 MCP服务器

**文件**: `main/mcp_server.h`

**职责**: 实现MCP（Model Context Protocol）协议，处理来自服务器的工具调用。

**核心类**:
```cpp
class McpTool {
    std::string name_;                    // 工具名称
    std::string description_;             // 工具描述
    PropertyList properties_;            // 参数定义
    std::function<ReturnValue(const PropertyList&)> callback_;  // 回调
};

class McpServer {
public:
    static McpServer& GetInstance();
    
    void AddTool(McpTool* tool);
    void AddCommonTools();    // 添加通用工具
    void AddUserOnlyTools();  // 添加用户专用工具
    void ParseMessage(const cJSON* json);  // 解析MCP消息
};
```

### 3.11 OTA升级

**文件**: `main/ota.h`

**职责**: 固件版本检查和OTA升级。

```cpp
class Ota {
public:
    Ota();
    
    esp_err_t CheckVersion();           // 检查新版本
    esp_err_t Activate();               // 激活设备
    bool HasNewVersion();               // 是否有新版本
    bool StartUpgrade(callback);        // 开始升级
    static bool Upgrade(url, callback); // 静态升级方法
    
    bool HasMqttConfig();               // 是否有MQTT配置
    bool HasWebsocketConfig();          // 是否有WebSocket配置
    bool HasActivationCode();            // 是否有激活码
};
```

### 3.12 设置管理

**文件**: `main/settings.h`

**职责**: 基于NVS的键值存储管理。

```cpp
class Settings {
public:
    Settings(const std::string& ns, bool read_write = false);
    
    std::string GetString(const std::string& key, const std::string& default_value = "");
    void SetString(const std::string& key, const std::string& value);
    
    int32_t GetInt(const std::string& key, int32_t default_value = 0);
    void SetInt(const std::string& key, int32_t value);
    
    bool GetBool(const std::string& key, bool default_value = false);
    void SetBool(const std::string& key, bool value);
    
    void EraseKey(const std::string& key);
    void EraseAll();
};
```

### 3.13 Board抽象层

**文件**: `main/boards/common/board.h`

**职责**: 硬件抽象层，统一管理各开发板的特定配置。

```cpp
class Board {
public:
    static Board& GetInstance();
    
    virtual std::string GetBoardType() = 0;
    virtual std::string GetUuid();
    
    virtual Backlight* GetBacklight();
    virtual Led* GetLed();
    virtual AudioCodec* GetAudioCodec() = 0;
    virtual Display* GetDisplay();
    virtual Camera* GetCamera();
    
    virtual NetworkInterface* GetNetwork() = 0;
    virtual void StartNetwork() = 0;
    virtual void SetNetworkEventCallback(NetworkEventCallback callback);
    
    virtual bool GetBatteryLevel(int &level, bool& charging, bool& discharging);
    virtual void SetPowerSaveLevel(PowerSaveLevel level) = 0;
};
```

### 3.14 资源管理

**文件**: `main/assets.h`

**职责**: 管理固件资源（字体、图标、语音模型）的下载和应用。

```cpp
class Assets {
public:
    static Assets& GetInstance();
    
    bool Download(std::string url, progress_callback);
    bool Apply(bool refresh_display_theme = true);
    bool GetAssetData(const std::string& name, void*& ptr, size_t& size);
    
    bool partition_valid() const;
};
```

---

## 4. 板级支持

### 4.1 支持的开发板

项目支持70+种开发板，部分示例：

**乐鑫官方开发板**:
- ESP32-S3-BOX3
- ESP32-S3-BOX
- ESP32-S3-Korvo2 V3
- ESP-Spot
- ESP-HI
- ESP-VoCat
- ESP-P4-Function-EV-Board

**M5Stack系列**:
- M5Stack CoreS3
- M5Stack AtomS3R + Echo Base
- M5Stack Cardputer Adv

**其他**:
- 立创·实战派 ESP32-S3
- 微雪电子 多款LCD/AMOLED模块
- LilyGO T-Circle-S3
- Seeed Studio SenseCAP Watcher
- 虾哥 Mini C3
- 各种自定义开发板

### 4.2 公共板级组件

位于 `main/boards/common/`:

| 文件 | 功能 |
|------|------|
| `board.cc/.h` | Board基类 |
| `wifi_board.cc/.h` | WiFi网络支持 |
| `ml307_board.cc/.h` | 4G Cat.1模块支持 |
| `nt26_board.cc/.h` | NT26 4G模块支持 |
| `dual_network_board.cc/.h` | 双网络支持 |
| `button.cc/.h` | 按钮输入 |
| `knob.cc/.h` | 旋钮输入 |
| `backlight.cc/.h` | 屏幕背光控制 |
| `adc_battery_monitor.cc/.h` | 电池电量监测 |
| `axp2101.cc/.h` | AXP2101电源管理 |
| `sy6970.cc/.h` | SY6970充电管理 |
| `esp32_camera.cc/.h` | 摄像头支持 |
| `esp_video.cc/.h` | 视频处理 |
| `sleep_timer.cc/.h` | 睡眠定时器 |
| `power_save_timer.cc/.h` | 功耗管理 |

---

## 5. 依赖关系

### 5.1 ESP-IDF组件

```
esp_pm                - 电源管理
esp_psram            - PSRAM支持
esp_netif            - 网络接口
esp_driver_gpio      - GPIO驱动
esp_driver_uart      - UART驱动
esp_driver_spi       - SPI驱动
esp_driver_i2c       - I2C驱动
esp_driver_i2s       - I2S驱动
esp_driver_jpeg      - JPEG驱动
esp_driver_ppa       - PPA驱动
esp_app_format       - 固件格式
app_update           - APP升级
spi_flash            - SPI闪存
console              - 控制台
efuse                - eFuse
bt                   - 蓝牙
fatfs                - FAT文件系统
```

### 5.2 外部组件

- **esp-sr**: 乐鑫语音识别框架（唤醒词、VAD、ASR）
- **esp-adf**: 乐鑫音频开发框架
- **lvgl**: 轻量级图形库
- **cJSON**: JSON解析库

---

## 6. 构建与运行

### 6.1 开发环境要求

- Cursor 或 VSCode
- ESP-IDF 插件（SDK版本 5.4 或以上）
- Linux/macOS/Windows

### 6.2 构建步骤

1. **克隆项目并初始化子模块**:
```bash
git clone https://github.com/78/xiaozhi-esp32.git
cd xiaozhi-esp32
git submodule update --init --recursive
```

2. **配置项目**:
```bash
idf.py menuconfig
```
在 menuconfig 中选择:
- `Xiaozhi Assistant` → `Board Type` → 选择开发板
- `Xiaozhi Assistant` → `Default Language` → 选择语言

3. **编译项目**:
```bash
idf.py build
```

4. **烧录固件**:
```bash
idf.py flash monitor
```

### 6.3 固件烧录

新手建议使用预编译固件:
- 默认接入 xiaozhi.me 官方服务器
- 注册账号可免费使用 Qwen 实时模型

---

## 7. 通信协议

### 7.1 WebSocket协议

设备通过WebSocket与服务器建立持久连接，用于:
- 设备认证
- 音频数据传输
- JSON消息交换（MCP、控制命令等）

### 7.2 MQTT+UDP混合协议

- **MQTT**: 控制消息、状态同步
- **UDP**: 实时音频数据传输（使用AES-CTR加密）

### 7.3 MCP协议

用于后台API与设备之间的工具调用:
- `initialize`: 初始化MCP会话
- `tools/list`: 获取可用工具列表
- `tools/call`: 调用指定工具

---

## 8. 关键数据流

### 8.1 语音交互流程

```
1. 用户说"小智你好"
   ↓
2. 唤醒词检测识别到唤醒
   ↓
3. 打开音频通道 → 服务器
   ↓
4. 用户语音 → 麦克风 → AFE处理 → Opus编码 → 发送
   ↓
5. 服务器 → ASR → LLM → TTS
   ↓
6. 服务器返回Opus音频
   ↓
7. Opus解码 → 扬声器播放
```

### 8.2 MCP工具调用流程

```
1. 服务器发送JSON-RPC请求
   {
     "jsonrpc": "2.0",
     "method": "tools/call",
     "params": { "name": "set_led", "arguments": {...} },
     "id": 1
   }
   ↓
2. McpServer::ParseMessage() 解析
   ↓
3. 调用对应的McpTool::Call()
   ↓
4. 返回结果给服务器
```

---

## 9. 配置选项

### 9.1 Kconfig主要配置项

| 配置项 | 说明 | 默认值 |
|--------|------|--------|
| `OTA_URL` | OTA服务器地址 | https://api.tenclass.net/xiaozhi/ota/ |
| `BOARD_TYPE` | 开发板类型 | BREAD_COMPACT_WIFI |
| `LANGUAGE_*` | 默认语言 | ZH_CN |
| `FLASH_DEFAULT_ASSETS` | 闪存默认资源 | - |
| `USE_ESP_BLUFI_WIFI_PROVISIONING` | 使用BluFi配网 | - |
| `CONFIG_USE_AUDIO_PROCESSOR` | 使用音频处理器 | - |
| `CONFIG_SEND_WAKE_WORD_DATA` | 发送唤醒词数据 | - |

### 9.2 ListeningMode模式

```cpp
enum ListeningMode {
    kListeningModeAutoStop,    // 自动停止（语音结束后自动停止录音）
    kListeningModeManualStop,  // 手动停止（需用户操作停止）
    kListeningModeRealtime     // 实时模式（需要AEC支持）
};
```

### 9.3 AEC模式

```cpp
enum AecMode {
    kAecOff,            // 关闭AEC
    kAecOnDeviceSide,   // 设备端AEC
    kAecOnServerSide    // 服务端AEC
};
```

---

## 10. 注意事项

1. **内存管理**: 项目大量使用动态内存分配，需注意堆内存使用
2. **线程安全**: 事件处理和协议回调在不同的FreeRTOS任务中
3. **功耗管理**: 根据设备状态调整功耗等级
4. **资源分区**: v2版本分区表与v1不兼容，无法OTA升级
5. **代码风格**: 使用Google C++代码风格

---

## 附录

### A. 相关文档链接

- [自定义开发板指南](docs/custom-board_zh.md)
- [MCP协议物联网控制用法说明](docs/mcp-usage_zh.md)
- [MCP协议交互流程](docs/mcp-protocol_zh.md)
- [MQTT+UDP混合通信协议文档](docs/mqtt-udp_zh.md)
- [WebSocket通信协议文档](docs/websocket_zh.md)

### B. 相关开源项目

- [xinnan-tech/xiaozhi-esp32-server](https://github.com/xinnan-tech/xiaozhi-esp32-server) - Python服务器
- [joey-zhou/xiaozhi-esp32-server-java](https://github.com/joey-zhou/xiaozhi-esp32-server-java) - Java服务器
- [78/xiaozhi-assets-generator](https://github.com/78/xiaozhi-assets-generator) - 自定义资源生成器
