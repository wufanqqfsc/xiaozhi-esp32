# AI 罗盘 - 产品功能与技术实现方案

> **目标硬件**: Waveshare ESP32-S3-Touch-LCD-1.85B (ESP32-S3R8, 360x360 QSPI LCD, 8MB PSRAM + 16MB Flash)
> **产品定位**: 一款基于 ESP32-S3 硬件平台的 AI 语音交互设备，融合东方传统文化元素（太极八卦）与现代 AI 技术，具备 WiFi 和蓝牙双模连接，支持实时语音对话和多种智能交互功能。

> **📋 文档状态**: 本文档仅描述已实现的功能。Target1 设计中的八卦名大字、卦象符号、方位大字、鱼眼图标、AI 运势引擎、结果卡等功能已被移除或尚未实现。当前实现为 Target2 风格（4 层同心圆 + 太极图旋转 + 状态进度环）。

---

## 1. 产品概述

### 1.1 目标硬件规格

| 项目 | 规格 |
|-----|-----|
| **主控芯片** | ESP32-S3R8（双核 Xtensa LX7 @ 240MHz） |
| **SRAM / PSRAM** | 512KB 内部 SRAM + **8MB 外置 PSRAM** |
| **Flash** | **16MB NOR Flash** |
| **屏幕** | 1.85" SPI LCD (QSPI), ST77916 驱动 |
| **分辨率** | **360×360**（圆形 UI 适配） |
| **触控** | CST816S 电容触控（I2C） |
| **音频编解码** | ES8311（播放） + ES7210（录音/AEC） |
| **麦克风** | 双麦克风阵列（支持回声消除） |
| **IMU** | QMI8658 六轴传感器（3 轴陀螺仪 + 3 轴加速度计，I2C） |
| **电量检测** | BQ27220（I2C） |
| **RTC** | PCF85363（I2C，掉电走时） |
| **连接** | 2.4GHz Wi-Fi 802.11 b/g/n + Bluetooth 5 (LE) |
| **USB** | Type-C（下载/日志/USB 协议） |
| **电源** | MX1.25 锂电池接口（3.7V）支持充放电 |

### 1.2 产品定位

| 维度 | 描述 |
|-----|-----|
| **核心概念** | AI 智能罗盘（Xiaozhi Compass） |
| **硬件基础** | Waveshare ESP32-S3-Touch-LCD-1.85B（固定目标） |
| **交互方式** | 语音唤醒 + 自然语言对话 + 电容触摸屏 |
| **连接方式** | WiFi (主力) + 蓝牙 BLE (辅助/直连控制) |
| **文化元素** | 太极图 |
| **AI 能力** | 实时语音对话 (TTS+ASR)、AI 工具调用 (MCP) |
| **目标用户** | 科技爱好者、DIY 玩家、传统文化爱好者、小众礼品市场 |

### 1.3 产品亮点

- **炫酷的视觉交互**: 动态旋转的太极图（30秒/圈） + 鎏金色同心圆环 + 状态进度指示
- **流畅的语音对话**: 基于 Xiaozhi AI 协议的端到端实时语音交互（双麦克风 + AEC）
- **灵活的扩展能力**: 通过 MCP 工具链轻松接入更多 AI 能力（查天气、系统控制）
- **精致的硬件工艺**: 1.85" 360×360 圆屏，适合罗盘 UI 设计；六轴 IMU 可做姿态感知

---

## 2. 核心功能模块

### 2.1 功能总览

```
AI 罗盘 (Xiaozhi Compass)
├── 🎤 **语音交互系统**
│   ├── 语音唤醒 (Wake Word Detection)
│   ├── 实时语音流传输 (Opus 编码)
│   ├── AI 对话响应 (Server-side LLM)
│   └── 语音播报 (TTS)
│
├── 🎨 **罗盘显示系统** ⭐ 本项目核心
│   ├── 动态太极图 (30秒逆时针旋转 + 鎏金外环)
│   ├── 4 层同心圆布局 (L0-L4)
│   ├── 状态进度环 (5 档颜色)
│   └── 4 方位圆点 (N/E/S/W)
│
├── 🔧 **设备管理系统**
│   ├── WiFi 配网 (SmartConfig/BLE)
│   ├── OTA 固件升级 (16MB Flash 支持双分区)
│   └── 多语言切换 (25+ 语言)
│
└── 🛠 **MCP 工具链**
    ├── 天气查询工具
    ├── 时间/日期工具
    ├── 系统信息工具
    └── 太极旋转控制工具
```

### 2.2 详细功能说明

#### 2.2.1 语音交互系统

**功能流程**:
```
用户唤醒 → 开始录音 → OPUS 编码 → WebSocket 发送 →
服务器 ASR → LLM 推理 → TTS 合成 → 音频流回传 →
设备解码播放（通过 ES8311 Codec）
```

**关键特性**:
- 低延迟：端到端 < 1s（网络良好时）
- 实时打断：语音播放时可随时打断
- 流式响应：边说边听，无需等待完整回复
- 回声消除 (AEC)：支持设备端 AEC (ES7210 硬件 + 软件)

**实现文件**:
- [application.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/application.h) - 事件驱动主循环
- [protocol.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/protocols/protocol.h) - 协议抽象基类
- [websocket_protocol.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/protocols/websocket_protocol.h) - WebSocket 实现

#### 2.2.2 罗盘显示系统 ⭐

**视觉层次**（从内到外，360×360 圆形屏幕）:

| 层级 | 元素 | 半径范围 | 行为 | 技术实现 |
|-----|-----|---------|-----|---------|
| L0 中心 | 太极图 | 0-44 | **30秒逆时针旋转一圈** | LVGL canvas (88×88) + ARGB8888 |
| L1 内层 | 核心信息容器 | 0-54 | 固定不动 | LVGL obj 透明容器 |
| L2 中层 | 内环指示器 | 54-90 | 固定不动 | LVGL arc (1px 金色) |
| L3 外层 | 状态进度环 | 90-144 | 5 档颜色状态 | LVGL arc (4px) + 背景弧 |
| L4 外圈 | 金色边框 | 144-178 | 固定不动 | LVGL obj + 1px 金色边框 |
| 分隔 | 方位点 | r=72 | 固定不动，4个方向 | 6×6 鎏金色圆点 (N/E/S/W) |

**太极图绘制算法** ([compass_taiji.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/compass_taiji.cc)):
1. **内存分配**: 优先 PSRAM 分配 canvas buffer (ARGB8888, 4BPP) - 88×88 = ~31KB，远小于 8MB PSRAM
2. **阳鱼绘制**: 填充白色大圆（左半）
3. **阴鱼绘制**: 右半圆填充黑色
4. **阴阳互抱**: 上半黑圆内绘制白色小圆，下半白圆内绘制黑色小圆
5. **点睛**: 阴阳鱼各点一个反向色小圆
6. **鎏金环**: 绘制圆周 1px 宽的金色环 (`#D4AF37`)
7. **旋转**: FreeRTOS 任务每 50ms 更新角度（约 600 步 = 30 秒一圈）

**罗盘组件层级（真实代码结构）**:
```
screen (lv_screen_active()) ─ 360×360
├── round_mask (圆屏遮罩 - 背景层)
│
├── attitude_container_ (罗盘主容器)
│   ├── background_ (深色背景 0x0A0A0A)
│   │   └── bg_layer_center_ (中心微亮层 0x121212, 300圆)
│   ├── layer1_container_ (核心信息容器, 108×108)
│   ├── layer2_inner_ring_ (内环指示器, r=80, 1px 金色)
│   ├── layer3_bg_arc_ (状态进度背景弧, r=130, 4px 深色)
│   ├── layer3_progress_arc_ (状态进度弧, r=140, 4px 彩色)
│   ├── layer4_outer_ring_ (外圈边框, 356×356, 1px 金色)
│   ├── dir_n_label_ (北, 6×6 鎏金圆点)
│   ├── dir_e_label_ (东, 6×6 鎏金圆点)
│   ├── dir_s_label_ (南, 6×6 鎏金圆点)
│   └── dir_w_label_ (西, 6×6 鎏金圆点)
│
└── compass_taiji (太极图, 88×88 canvas, 自动旋转 30s/圈)
```

**实现文件**:
- [compass_taiji.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/compass_taiji.cc) - 太极图组件（自动旋转 30s/圈）
- [attitude_display.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.h) - 罗盘显示参数（半径宏定义）
- [attitude_display.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc) - 4 层同心圆布局
- [lvgl_display.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/lvgl_display/lvgl_display.cc) - LVGL 显示基类

#### 2.2.3 消息显示系统

**本板配置**: 360×360 圆屏，使用底部单行滚动模式（气泡模式不优化圆形屏，暂不启用）

**消息类型**:
- **系统消息**（半透明灰底）：设备状态、时间、天气
- **用户消息**（绿色底）：用户语音转录文本
- **AI 消息**（白色底/金色字）：AI 回复内容
- **通知 Toast**（顶部横幅，3秒自动消失）：临时提示如"音量+10"/"已连接 WiFi"

**实现文件**:
- [lcd_display.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/lcd_display.cc#L353-L782) - SetupUI/消息气泡逻辑

#### 2.2.4 设备管理系统

**WiFi 配网**:
- SmartConfig（手机 App 一键配网）
- BLE 辅助配网（可选）
- 失败回退：长按按键进入配网模式

**OTA 升级**:
- 启动时检查服务器版本号（`CONFIG_OTA_URL`）
- 用户可通过 AI 指令触发升级
- ESP32-S3 双分区 OTA（16MB Flash → OTA0/OTA1 分区各约 6MB，完全够用）
- 增量/全量双模式（[ota.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/ota.h)）

**多语言支持**（25+ 语言，已内置）:
- 中文（简/繁）、英、日、韩、越、泰、德、法、西、意、俄、阿、印、葡、波、捷、芬、土、印尼、乌、罗、保、加、丹、希、波斯、菲利宾、希伯莱等
- 运行时可切换（持久化到 NVS Flash）

**默认字体配置（本板）**:
- `BUILTIN_TEXT_FONT`: `font_puhui_basic_30_4`（30px 中文字体，含 3-4 级字模，足够显示"北东南西/乾兑离震巽坎艮坤"）
- `BUILTIN_ICON_FONT`: FontAwesome 图标字体
- emoji: twemoji_64（表情符号）

#### 2.2.5 MCP (Model Context Protocol) 工具链

**工作原理**:
1. 设备启动时通过 McpServer 注册可用工具
2. AI 对话中，LLM 决定是否需要调用工具
3. 工具调用指令通过 WebSocket 下发到设备
4. 设备执行工具（如查询天气/时间/系统信息）并将结果回传到服务器
5. LLM 基于工具返回结果生成最终回答

**已实现的工具类**:
```
- self.system.reconfigure_wifi: WiFi 重配
- self.system.get_status:     获取设备状态 (IMU/电量/WiFi信号)
- self.system.reboot:         重启设备
- 天气查询工具 (可选)
- 时间/日期工具 (RTC: PCF85363 提供)
```

**扩展接口**: 用户可通过简单的 `AddTool()` 调用注册新工具

**实现文件**:
- [mcp_server.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/mcp_server.h) - MCP 服务核心

---

## 3. 技术架构

### 3.1 系统总览

```
┌────────────────────────────────────────────────────────────┐
│                      硬件层 (Hardware)                       │
│   ESP32-S3R8 + ST77916 QSPI LCD + CST816S Touch + ES8311    │
│   + ES7210 AEC MIC + QMI8658 IMU + BQ27220 Battery + PCF85363│
│   + WiFi/BT + USB Type-C                                     │
└────────────────────────────────────────────────────────────┘
                              │
┌────────────────────────────────────────────────────────────┐
│                   ESP-IDF 框架 (v5.5.2+)                     │
│   FreeRTOS/LWIP/ESP-NETIF/WiFi Driver/NimBLE Stack/HeapCaps  │
└────────────────────────────────────────────────────────────┘
                              │
┌────────────────────────────────────────────────────────────┐
│                    板级抽象层 (Board Abstraction)            │
│   [board.h] → waveshare/esp32-s3-touch-lcd-1.85b            │
│   - GetDisplay() → SpiLcdDisplay (QSPI, ST77916, 360×360)   │
│   - GetAudioCodec() → ES8311 Speaker + ES7210 Mic (I2S+I2C) │
│   - GetIMU() → QMI8658 (I2C, 6-axis)                       │
│   - GetBattery() → BQ27220 (I2C, fuel gauge)               │
│   - GetRTC() → PCF85363 (I2C, time/date)                    │
│   - GetTouch() → CST816S (I2C, capacitive)                 │
│   - 统一的 GPIO/外设配置 (board-specific config)            │
└────────────────────────────────────────────────────────────┘
                              │
┌────────────────────────────────────────────────────────────┐
│                    应用核心 (Application Core)               │
│                                                              │
│   [application.h] - 事件驱动主循环                           │
│   ├── MAIN_EVENT_TOGGLE_CHAT / START_LISTEN / ...           │
│   ├── Device State Machine (Starting/Idle/Listening/Speaking)│
│   └── AudioService - 音频流管理 (OPUS + VAD)                │
│                                                              │
│   [protocol] - AI 协议层                                     │
│   ├── Protocol (抽象基类)                                     │
│   └── WebsocketProtocol (WebSocket 实现)                    │
│                                                              │
│   [display] - 显示系统                                       │
│   ├── Display (基类)                                         │
│   ├── LvglDisplay (LVGL 基类)                                │
│   ├── SpiLcdDisplay (QSPI LCD 适配 - ST77916)                │
│   └── AttitudeDisplay (罗盘专用，扩展 SpiLcdDisplay)         │
│                                                              │
│   [mcp_server] - AI 工具调用服务                             │
│   ├── McpTool (工具对象)                                     │
│   ├── Property (参数描述)                                    │
│   └── McpServer (工具注册/调用分发)                          │
│                                                              │
│   [LVGL v9.5.0] - UI 渲染引擎                               │
│   ├── Widgets: label/image/canvas/arc/container             │
│   ├── Theme: 暗色+金色主题 (0x0A0A0A + 0xD4AF37)             │
│   └── Font: font_puhui_basic_30_4 (中文) + FontAwesome (图标)│
└────────────────────────────────────────────────────────────┘
                              │
┌────────────────────────────────────────────────────────────┐
│                    云端 (Xiaozhi AI Server)                 │
│   LLM Inference → ASR → Dialog Management → TTS → MCP Tools │
└────────────────────────────────────────────────────────────┘
```

### 3.2 设备状态机

```
       [Starting]  启动初始化
            │
       WiFi 连接成功
            ▼
       [Idle] 待机/显示罗盘
         │  │
  ┌──────┘  └──────────────────┐
  │ 唤醒词检测                  │ 用户按键 / MCP 指令
  ▼                            ▼
[Listening] 录音中      [Action] 执行动作/OTA
  │ 停止录音 / 超时           │
  ▼                            │
[Speaking] AI 回复播放        │
  │ 播放完成                   │
  └────────────────────────────┘
```

**状态定义**（[device_state.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/device_state.h)）:
- `kDeviceStateStarting`: 初始化阶段
- `kDeviceStateWifiConfiguring`: WiFi 配网模式
- `kDeviceStateIdle`: 待机/显示罗盘
- `kDeviceStateConnecting`: 连接服务器中
- `kDeviceStateListening`: 用户正在说话
- `kDeviceStateSpeaking`: AI 正在回答
- `kDeviceStateActivating`: 设备激活（首次使用）
- `kDeviceStateUpgrading`: OTA 升级中
- `kDeviceStateFatalError`: 致命错误

### 3.3 数据流向（语音交互）

```
  用户声音 (双麦克风阵列 → ES7210 AEC)
    │ (I2S MIC)
    ▼
┌──────────────────┐
│  Application     │  AudioService
│  - OPUS 编码     │  ← 核心音频处理
│  - VAD 检测      │    ([application.h])
│  - 分段发送      │
└────────┬─────────┘
         │ WebSocket 二进制帧
         ▼
┌──────────────────────────┐
│    Xiaozhi AI Server     │
│  - ASR 语音识别          │
│  - LLM 对话推理          │
│  - MCP 工具调用          │
│  - TTS 语音合成          │
└────────┬─────────────────┘
         │ 音频流 + JSON 控制消息
         ▼
┌──────────────────┐
│  Application     │  ← ES8311 Speaker 输出 (I2S)
│  - 音频解码      │
│  - 状态更新显示  │    → SetStatus/SetEmotion
│  - 文本显示      │    → SetChatMessage
└──────────────────┘
```

### 3.4 文件结构（本项目关键路径）

```
main/
├── application.h/cc              # 应用主循环/事件驱动
├── audio_service.h/cc            # 音频采集/播放服务 (ES8311 + ES7210)
├── device_state.h                # 设备状态枚举
├── mcp_server.h/cc               # MCP AI 工具服务
├── ota.h/cc                      # OTA 升级 (16MB Flash, 双分区)
├── assets/                       # 多语言资源/字体
│   ├── lang_config.h             # 语言配置
│   └── locales/xx-XX/*.ogg       # 提示音资源
├── display/
│   ├── display.h                 # 显示基类
│   ├── lvgl_display/
│   │   ├── lvgl_display.h/cc     # LVGL 显示基类
│   │   ├── lvgl_theme.h/cc       # 主题系统 (暗色+金色)
│   │   ├── lvgl_image.h/cc       # 图像处理
│   │   ├── lvgl_font.h/cc        # 字体管理 (font_puhui_basic_30_4)
│   │   ├── gif/                  # GIF 动图解码
│   │   └── emoji_collection.h/cc # 表情集合
│   ├── lcd_display.h/cc          # LCD 通用显示 (含气泡/滚动消息)
│   ├── compass_taiji.h/cc        # ⭐ 太极图组件（144x144 canvas, 30s旋转）
│   ├── attitude_display.h/cc     # ⭐ 方位/八卦显示 (r=86 八卦名, r=122 卦象, r=150 方位字)
│   ├── snapshot/                 # 截图服务 (屏幕→JPEG)
│   └── snapshot_service.cc       # 屏幕快照
├── boards/common/
│   ├── board.h                   # Board 抽象接口
│   ├── wifi_board.h/cc           # WiFi 板基类 (含网络事件回调)
│   └── ...
├── boards/waveshare/
│   └── esp32-s3-touch-lcd-1.85b/ # ⭐ 本项目目标板（ESP32-S3R8, ST77916 360x360）
├── protocols/
│   ├── protocol.h                # 协议抽象
│   └── websocket_protocol.h/cc   # WebSocket 协议实现
└── CMakeLists.txt / Kconfig.projbuild
```

---

## 4. 新增功能技术方案

### 4.1 AI 运势功能（核心需求）

#### 方案 A：服务端 MCP 工具 + 客户端 UI 扩展（推荐）

```
1. 服务端新增 MCP "Fortune Engine" 工具
   ├─ fortune.today(date, zodiac) → 今日宜/忌+幸运色/数字
   ├─ fortune.wealth(date, zodiac) → 财运方位+建议
   ├─ fortune.career(date, zodiac) → 事业分析
   ├─ fortune.gua(seed, question) → 生成卦象+解读
   └─ fortune.huangli(date) → 黄历宜忌

2. 客户端: 扩展 AttitudeDisplay
   ├─ HighlightDirection(deg) → 方位大字脉冲闪烁 (金色 300ms 周期 3 次)
   ├─ HighlightGua(index) → 指定八卦名大字脉冲+放大
   └─ StartFortuneAnimation() → 太极加速旋转 20s/圈 持续 3 秒

3. AI Prompt 设计:
   "你是一个有趣的玄学大师。用户问你问题时，先调用 fortune 工具获取数据，
    然后用轻松幽默的风格给出解读。永远保持好玩、不迷信、不贩卖焦虑的原则。"
```

**实现要点**:
- 在 `AttitudeDisplay` 基础上扩展 `HighlightDirection()` 和 `HighlightGua()`
- 复用现有 `SetChatMessage()` 显示文本结果
- 通过 McpServer 注册工具或在服务端实现工具
- 卦象符号复用现有 `bagua_symbol_canvases_[8]`，添加脉冲动画

#### 方案 B：纯客户端规则引擎（离线可用，保底方案）

- 内置一套简易的"占卜算法"（基于日期 hash + 伪随机）
- 完全离线运行，无需网络
- 适合作为低配功能/演示模式

### 4.2 蓝牙 BLE 连接（可选新增）

当前项目已有完善的 WiFi 支持。**ESP32-S3 自带 BLE 支持（ESP-IDF v5.5.2 的 NimBLE）**

**功能目标**:
1. BLE 广播 + 服务：让手机 App 发现并连接
2. BLE GATT Characteristic:
   - `WiFi Config Service` → 接收 SSID/PASSWORD（辅助配网）
   - `Control Service` → 发送指令（重启/OTA/查状态）
   - `Notification Service` → 接收 AI 推送消息（文字）
   - `OTA Service` → 接收固件（可选）

**技术选型**:
- **ESP-IDF v5 NimBLE**: 轻量级 BLE 栈（推荐），比 BlueDroid 小 50%
- 内存占用: ~30KB RAM, ~100KB Flash (8MB PSRAM + 16MB Flash 完全不成问题)

**BLE + WiFi 共存**: ESP32-S3 支持 Wi-Fi + BLE 同时工作
- 共享天线：吞吐量会降低（但对本应用足够，语音流走 WiFi）
- 优先级配置：确保 WiFi 语音流不被 BLE 扫描阻塞

---

## 5. 显示系统深入设计

### 5.1 LVGL 初始化流程（本板: QSPI LCD + ST77916, 360×360）

```
1. SpiLcdDisplay(panel_io, panel, w=360, h=360, offset=...) 构造
   ├─ 初始化 LVGL: lv_init()
   ├─ PSRAM 检测 (8MB 可用) → 分配 2MB 图像缓存
   └─ lvgl_port_init() - 创建 LVGL 任务 (优先级=1, 核心=1 on SMP)

2. lvgl_port_add_disp() - 注册显示设备
   ├─ 配置 buffer: 360 × 20 像素行缓冲 (DMA 缓冲) - 位于 PSRAM
   ├─ 颜色格式: RGB565 (16-bit)
   ├─ rotation: swap_xy/mirror_x/mirror_y 支持
   └─ buff_dma=1, swap_bytes=1

3. SetupUI() - 构建 UI 树（由 Application 初始化后调用一次）
   ├─ 屏幕背景: lv_screen_active() → 设置 (360×360, 背景色 0x0A0A0A)
   ├─ lv_obj_set_style_text_font(screen, &font_puhui_basic_30_4, 0)
   ├─ CreateBackground() → 深色背景圆 (radius 180)
   ├─ CreateLayer0Taiji() → 太极图 (144×144 canvas, 中心对齐)
   ├─ CreateRingBoundaries() → 3 个金色圆环 (r=72, 100, 145)
   ├─ CreateLayer2BaguaSymbols() → 8 个卦象 canvas (36×24) at r=122
   ├─ CreateOuterBoundary() → 外圈金色边框 (r=178, 3px 粗边)
   │
   ├─ ★ 文字标签在所有图形之后创建，确保在最上层
   ├─ CreateLayer1BaguaNames() → 8 个八卦名大字 (在 screen 上, r=86)
   │   └─ lv_label_set_text(label, "乾") → lv_obj_set_size(48,48) → lv_obj_move_foreground()
   └─ CreateLayer4DirectionText() → 4 个方位大字 (在 screen 上, r=150)
       └─ lv_label_set_text(label, "北") → lv_obj_set_size(48,48) → lv_obj_move_foreground()
```

### 5.2 罗盘 UI 组件层级（真实坐标）

> ⚠️ **2026-06-14 代码审计**: 本节描述的 8 卦名大字 + 8 卦象符号 + 4 方位大字 (r=150) **未在当前代码中实现**. 实际为 4 层同心圆布局 (Target2). 详见 [7 章 当前进度报告](#-当前进度报告-2026-06-14-代码审计) 章节.

```
屏幕 360×360 (cx=180, cy=180)
├── attitude_container_ (容器, 360×360, 透明背景)
│   ├── background_ (深色背景, 360×360, 0x0A0A0A)
│   │   └── bg_layer_center_ (中心微亮圆, 300×300, 0x121212, pos 30,30)
│   │
│   ├── ring_r72 (太极图外环, 144×144, 2px 金色边, pos 108,108)
│   ├── layer0_taiji_container_ (太极图, 144×144, pos 108,108, radius 72)
│   │   └── compass_taiji_->canvas_ (ARGB8888 画布, 黑白阴阳鱼, 金色外环)
│   │
│   ├── ring_r100 (八卦名外环, 200×200, 2px 金色边, pos 80,80)
│   ├── bagua_symbol_canvases_[8] (卦象符号, 各 36×24 canvas, r=122)
│   │   ├── [0]乾 (x,y) 中心 180+122*cos(-90°),180+122*sin(-90°) → (180,58)
│   │   ├── [1]兑 (x,y) (266,83)
│   │   ├── [2]离 (x,y) (302,180)
│   │   ├── [3]震 (x,y) (266,277)
│   │   ├── [4]巽 (x,y) (180,302)
│   │   ├── [5]坎 (x,y) (94,277)
│   │   ├── [6]艮 (x,y) (58,180)
│   │   └── [7]坤 (x,y) (94,83)
│   │
│   ├── ring_r145 (卦象外环, 290×290, 2px 金色边, pos 35,35)
│   └── outer_boundary (外圈边框, 356×356, 3px 金色边, pos 2,2, radius 178)
│
└── ★ 文字标签 (直接创建在 screen 上, 永不被遮挡)
    ├── bagua_name_labels_[8] (八卦名大字, 48×48, 金色 0xD4AF37)
    │   ├── [0]乾 pos(180-24, 180-86-24) = (156,70) 对齐 r=86 圆周
    │   ├── [1]兑 pos(180+86*cos(-45°)-24, 180+86*sin(-45°)-24)
    │   ├── [2]离 pos(242,156)
    │   ├── [3]震 pos(242,284)
    │   ├── [4]巽 pos(156,302)
    │   ├── [5]坎 pos(94,284)
    │   ├── [6]艮 pos(94,156)
    │   └── [7]坤 pos(156,70) → ★ 每50ms更新旋转位置
    │
    └── dir_n/e/s/w_text (方位大字, 48×48, 金色 0xD4AF37, 不旋转)
        ├── 北 pos(180-24, 180-150-24) = (156, 6)
        ├── 东 pos(180+150-24, 180-24) = (306, 156)
        ├── 南 pos(180-24, 180+150-24) = (156, 306)
        └── 西 pos(180-150-24, 180-24) = (6, 156)

    ════════════════════════════════════════════════════
    ═══ ⭐ AI 运势引擎 - 结果卡（Result 态创建，Idle/Animating 态销毁） ⭐ ═══
    ════════════════════════════════════════════════════

    ├── fortune_result_card_ (运势结果卡, 200×240 胶囊形, pos(80,60))
    │   ├── 圆角 100px, 96% 透明度背景, 2px 金色边框
    │   ├── fortune_func_label_ (功能标识图 "今日运势 ☀", pos(0,16), w=200)
    │   ├── fortune_gua_icon_canvas_ (卦象小图, 72×48, pos(64,56))
    │   ├── fortune_gua_name_label_ (卦名大字 "乾为天", pos(0,112), w=200)
    │   ├── fortune_core_label_ (核心解读一句话, pos(0,152), w=200, 白色 24px)
    │   ├── fortune_yi_label_ (宜清单 "宜: 出行·学习", pos(20,188), w=160, 绿色)
    │   ├── fortune_ji_label_ (忌清单 "忌: 争执·急躁", pos(20,212), w=160, 红色)
    │   └── fortune_tip_label_ ("触摸任意位置关闭", pos(0,220), w=200, 灰色 16px)

    ════════════════════════════════════════════════════
    ═══       ⭐ 鱼眼状态标识（伪旋转方案，永久固定）      ⭐ ═══
    ════════════════════════════════════════════════════

    ├── fisheye_wifi_icon_ (上方鱼眼 = WiFi 状态, 36×36 圆形, pos(162,126), 圆心 (180,144))
    │   └── 内部 18px 📶/")))" 符号, 颜色: 金/灰/脉冲金白 (伪旋转, 不旋转)
    ├── fisheye_ble_icon_  (下方鱼眼 = 蓝牙状态, 36×36 圆形, pos(162,198), 圆心 (180,216))
        └── 内部 18px 🔵/"⇄" 符号, 颜色: 白底金色/白底灰色/白色脉冲 (伪旋转, 不旋转)

★ 旋转逻辑（简化版 ⭐）: 八卦名大字+卦象 45秒/圈逆时针, 太极图 30秒/圈逆时针, 方位大字/鱼眼图标/结果卡 固定不动
   每50ms更新八卦环位置, 在 UpdateBaguaPositions(angle_0.1deg) 中
   鱼眼图标不更新位置（已在正确屏幕坐标上, 伪旋转）
   结果卡仅在 Result 态创建, 触摸或 30 秒后销毁
   所有元素在 screen 上创建, lv_obj_move_foreground() 保证置顶
   (Animating 态: 太极保持 30s/圈, 八卦+卦象加速到 15s/圈, 全局亮度脉冲, 无尺寸放大高亮)
```

### 5.3 罗盘参数宏（[attitude_display.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.h)）

> ⚠️ **2026-06-14 代码审计**: 本节包含的 Target1 宏定义 (`ANIM_TAIJI_PERIOD_MS` / `ANIM_BAGUA_PERIOD_MS` / `FORTUNE_CARD_W/H` / `FISHEYE_*` / `PULSE_*` 等) **未在当前代码中定义**. 实际 `attitude_display.h` 仅 141 行, 不含上述宏. 详见 [7 章 当前进度报告](#-当前进度报告-2026-06-14-代码审计) 章节.

```c
// ================= 屏幕与核心坐标 =================
#define ATTITUDE_CENTER_X       180   // 屏幕中心 X (360/2)
#define ATTITUDE_CENTER_Y       180   // 屏幕中心 Y (360/2)
#define VALID_RADIUS            178   // 有效圆形区域半径 (距边缘 2px)

// ================= 罗盘层半径 =================
#define L0_TAIJI_RADIUS         72    // 太极图半径 (144×144 画布, 直径 144)
#define L1_BAGUA_NAME_R         88    // 八卦名大字 (72 和 108 的中点偏内, 2px 安全距)
#define L2_BAGUA_SYMBOL_R       115   // 卦象符号 (八卦名和方位大字之间, 与两者间距 ~15px)
#define L5_DIRECTION_R          158   // 方位大字 (从 150 外推 8px, 贴近外圈, 与卦象间距 ~15px)
#define OUTER_RING_R            178   // 外圈装饰边框 (距边缘 2 像素)

// 环形分隔边界线半径 (简化为 2 个圆环, 减少视觉噪音, 金色 0xFFD700, border_width=2px)
#define RING_BOUNDARY_0         72    // 太极图外圈
// RING_BOUNDARY_1 (100) 已删除 - 避免层级过多
#define RING_BOUNDARY_2         145   // 卦象外圈 (保留为视觉参考, 实际绘制可选)

// ================= 文字尺寸 =================
#define BAGUA_NAME_SIZE         44    // 八卦名大字 label 尺寸 (从 48 缩小防重叠)
#define BAGUA_SYMBOL_W          36    // 卦象符号 canvas 宽度 (不变)
#define BAGUA_SYMBOL_H          24    // 卦象符号 canvas 高度
#define DIRECTION_SIZE          60    // 方位大字 label 尺寸 (从 48 增大 25%, 强化视觉焦点)

// ================= 色系定义 =================
#define COLOR_MAIN_GOLD         0xFFD700   // 主金色 (小屏幕对比度更强, 原 0xD4AF37 为次金)
#define COLOR_SUB_GOLD          0xD4AF37   // 次金色 (鱼眼描边/卦象符号)
#define COLOR_BG_DARK           0x121212   // 深色背景
#define COLOR_TEXT_MAIN         0xFFFFFF   // 主文字
```

### 5.4 旋转速度参数

> ⚠️ **2026-06-14 代码审计**: 实际只有太极图 30 秒/圈 旋转. 八卦名+卦象已移除 (`UpdateBaguaPositions()` 未调用), 方位大字已改为 6×6 圆点 (固定不动). 详见 [7 章 当前进度报告](#-当前进度报告-2026-06-14-代码审计) 章节.

| 旋转对象 | 周期 | 每 50ms 步进 (0.1°单位) | 说明 |
|---------|------|------------------------|------|
| 太极图 | **30 秒/圈** | 3600 / (30000/50) = **6** (0.6°/step) | `compass_taiji.cc` 内的 FreeRTOS 任务 ✅ 实际运行 |
| 八卦名 + 卦象 | ~~45 秒/圈~~ | — | ❌ 已移除, `UpdateBaguaPositions()` 代码已注释未调用 |
| 方位大字 | **固定不动** | - | ❌ 已改为 6×6 圆点, 4 个 lv_obj 沿 r=72 圆周 |

**自动旋转实现细节**（[compass_taiji.cc:262-283](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/compass_taiji.cc#L262-L283)）:
```cpp
// 每 50ms 执行一次: 角度步进 = 3600 / (period_ms / 50)
// 例: 30秒 = 3600 / 600 = 6 units/step = 0.6°/step
// 注意: lv_image_set_rotation() 以 0.1° 为单位, 范围 0~3600
```

**八卦旋转实现细节**（[attitude_display.cc 的 UpdateBaguaPositions()](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc#L600-L650)）:
```cpp
// 角度递减 → 逆时针旋转
// 对每个八卦名/卦象:
//   新角度 = 原始角度(i*45°) + 当前旋转角度(归一化到[-360,0])
//   新中心 (cx,cy) = PolarToPos(180,180, r, angle)
//   新位置 = (cx - label_w/2, cy - label_h/2)
// 八卦名: r=86, label_size=48
// 卦象:   r=122, canvas_size=36×24
```

> **注**: `UpdateBaguaPositions()` 在 Target2 实现中未被调用, 实际只有太极图旋转. 八卦名/卦象在迭代18-20 密度梯度重构时已移除.

---

### 5.5 主题系统（固定: 玄黑+鎏金）

**`LvglTheme` ([lvgl_theme.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/lvgl_display/lvgl_theme.h)) 字段**:
- `background_color_` - 背景: **0x0A0A0A 深色** (玄黑)
- `text_color_` - 文字: **0xD4AF37 金色** (鎏金)
- `chat_background_color_` - 消息区背景: **0x121212 深邃黑**
- `user_bubble_color_` - 用户气泡: **绿色** (保持一致)
- `assistant_bubble_color_` - 助手气泡: **深灰**
- `border_color_` - 边框: **金色 0xD4AF37**
- `low_battery_color_` - 低电量警告: **红色**
- `text_font_` / `icon_font_` / `large_icon_font_` - 字体指针: `font_puhui_basic_30_4`

**`AttitudeTheme` ([attitude_theme.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_theme.h)) 固定色值**:
- 背景: `bg_outer=0x0A0A0A`, `bg_inner=0x121212`
- 文本: `text_main=0xD4AF37`, `text_sub=0xC0C0C0`, `text_high=0xFFFFFF`
- 装饰: `border_line=0xD4AF37`, `card_bg=0x1A1A1A`, `point_default=0xD4AF37`
- 5 档状态色: `state_normal=0x2E5E4E`, `state_light=0x4A6FA5`, `state_mid=0xD4AF37`, `state_heavy=0xE67E22`, `state_danger=0xB82601`

**主题切换**: `SetTheme(Theme*)` → 仅供底层 LVGL 显示类使用 (AttitudeDisplay 不响应主题切换, 使用固定色值)

---

## 6. 构建与烧录

### 6.1 依赖与 SDK 版本

| 组件 | 版本要求 |
|-----|---------|
| **ESP-IDF** | **v5.5.2 或更高**（官方 `/.espressif/v5.5.4/esp-idf` 路径） |
| **LVGL** | v9.5.0+ (通过 IDF 组件管理器) |
| **esp_lvgl_port** | v2.7.2+ |
| **CMake** | 3.20+ |
| **Python** | 3.8+ (esptool) |
| **目标芯片** | esp32s3 |
| **目标板配置** | `CONFIG_BOARD_TYPE_WAVESHARE_ESP32_S3_TOUCH_LCD_1_85B=y` |

**IDF 组件管理** ([idf_component.yml](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/idf_component.yml)):
```yaml
dependencies:
  lvgl/lvgl: ~9.5.0           # UI 引擎
  esp_lvgl_port: ~2.7.2       # ESP-LVGL 适配层
  espressif/esp_lcd_ili9341: ==1.2.0  # LCD 驱动 (更多兼容驱动)
  espressif/button: ~4.1.5    # 按键驱动
  espressif/esp_codec_dev: ~1.5.6  # 音频编解码 (ES8311 + ES7210)
  ... (更多驱动，视板子而定)
```

### 6.2 配置选项（Kconfig + CMake）

**关键配置** (`main/CMakeLists.txt`):

针对 Waveshare ESP32-S3-Touch-LCD-1.85B:

```cmake
# Set default BUILTIN_TEXT_FONT and BUILTIN_ICON_FONT
elseif(CONFIG_BOARD_TYPE_WAVESHARE_ESP32_S3_TOUCH_LCD_1_85B)
    set(BOARD_TYPE "esp32-s3-touch-lcd-1.85b")
    set(BUILTIN_TEXT_FONT font_puhui_basic_30_4)
    set(BUILTIN_ICON_FONT font_awesome_16_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_64)
```

**关键配置项** (`main/Kconfig.projbuild`):

| 配置 | 含义 | 本板值 |
|-----|-----|-------|
| `CONFIG_USE_WECHAT_MESSAGE_STYLE` | 启用气泡消息（大屏） | n (圆屏不优化气泡) |
| `CONFIG_USE_MULTILINE_CHAT_MESSAGE` | 多行消息显示 | n |
| `CONFIG_LV_USE_SNAPSHOT` | 启用截图功能 (JPEG) | y |
| `CONFIG_LV_COLOR_DEPTH` | 颜色深度 | 16 (RGB565) |
| `CONFIG_LV_DEF_BG_COLOR` | 默认背景色 | 0x000000 |
| `CONFIG_LVGL_TASK_MAX_DELAY_MS` | LVGL 任务最大延迟 | 5 |
| `CONFIG_OTA_URL` | OTA 服务器地址 | `https://api.tenclass.net/xiaozhi/ota/` |

### 6.3 构建命令（本板）

```bash
# 0. 确认 ESP-IDF 环境已加载
export IDF_PATH=$HOME/.espressif/v5.5.4/esp-idf
source $IDF_PATH/export.sh

# 1. 设置目标芯片
idf.py set-target esp32s3

# 2. 清理旧配置（如之前构建过其他板）
idf.py fullclean

# 3. 构建
#   -BOARD_TYPE_WAVESHARE_ESP32_S3_TOUCH_LCD_1_85B 已在 CMakeLists.txt 中配置
python3 $IDF_PATH/tools/idf.py -DBOARD_TYPE_WAVESHARE_ESP32_S3_TOUCH_LCD_1_85B=y build

# 4. 烧录 + 监控串口
#   Linux: /dev/ttyUSB0
#   macOS: /dev/cu.usbmodem101 (或 /dev/cu.usbserial-*)
PORT=/dev/cu.usbmodem101
idf.py -p $PORT flash monitor

# 5. 擦除 (如需完全重置)
idf.py -p $PORT erase-flash
```

### 6.4 资产/资源处理

**字体/图像资产**:
- 字体文件: 通过 `LV_FONT_DECLARE(BUILTIN_TEXT_FONT)` 方式使用 - font_puhui_basic_30_4
- 图像资产: 通过 `scripts/build_default_assets.py` 打包为 SPIFFS 镜像（如有自定义卦象 PNG）
- 多语言提示音: `main/assets/locales/xx-XX/*.ogg` → 编译进固件（可存 OTA 分区）

**脚本工具**:
- `scripts/Image_Converter/lvgl_tools_gui.py` - LVGL 图像转换工具
- `scripts/spiffs_assets/build.py` - SPIFFS 资产打包

---

## 7. 开发路线图（基于最新产品定义 ⭐）

> **更新说明**：根据 2.2.3.1 / 5.2 / 5.3 / 5.6 节的最新设计，将原"阶段二 AI 运势"拆分为 4 个渐进可验证的子阶段；新增鱼眼状态（WiFi/BLE）作为必选功能；所有功能以 **先实现视觉骨架 → 再接入真实驱动 → 最后联调** 的方式推进，确保每阶段都可独立编译烧录验证。

---

### 阶段一：基础罗盘（✅ 已完成，核心代码已存在）
**当前代码状态**：[attitude_display.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.h) / [compass_taiji.h](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/compass_taiji.h)

| 任务 | 状态 | 主要文件 | 关键参数 |
|-----|-----|---------|---------|
| ESP32 LVGL 显示系统 | ✅ 已完成 | `main/display/lvgl_display.cc` | QSPI ST77916, 360×360 |
| 4 层同心圆布局 (Target2) | ✅ 已完成 | `main/display/attitude_display.cc` | Layer0 太极 / Layer1 空 / Layer2 细环 / Layer3 进度环 / Layer4 外环 |
| 太极图组件 | ✅ 已完成 | `main/display/compass_taiji.cc` | 88×88 canvas, 30s/圈, r=44 |
| 4 方位实心圆点 | ✅ 已完成 | `main/display/attitude_display.cc` | 6×6 圆点, 鎏金, r=72 圆周, 固定不动 |
| 内圈细环 (Layer2) | ✅ 已完成 | `main/display/attitude_display.cc` | r=80, 1px 鎏金细环 |
| 状态进度环 (Layer3) | ✅ 已完成 | `main/display/attitude_display.cc` | r=140 进度环 + r=130 背景环, lv_arc, 4px 宽 |
| 外圈金色边框 (Layer4) | ✅ 已完成 | `main/display/attitude_display.cc` | r=178, 1px 鎏金边 |
| 主题色值 (固定, 玄黑+鎏金) | ✅ 已完成 | `main/display/attitude_theme.h` | 1 组固定色值 + 5 档状态色 (normal/light/mid/heavy/danger) |
| 多语言支持 | ✅ 已完成 | `assets/lang_config.h` | 25+ 语言字体/提示音 |
| ~~8 个卦名大字 (r=86)~~ | ❌ **未实现** | — | Target1 设计, 迭代18 移除 |
| ~~8 个卦象符号 (r=122)~~ | ❌ **未实现** | — | Target1 设计, 迭代19 移除 |
| ~~4 个方位大字 (r=150)~~ | ❌ **未实现** | — | Target1 设计, 迭代18 改为 6×6 圆点 |
| ~~3 圈环形边界 (r=72/100/145)~~ | ❌ **未实现** | — | Target1 设计, 迭代18 改为单圈 + 进度环 |
| ~~八卦环自动旋转 (45s)~~ | ❌ **未实现** | `UpdateBaguaPositions()` 代码已注释 | 八卦已移除, 旋转逻辑保留但未调用 |

**当前验收**：上电 → 屏幕显示 4 层同心圆罗盘 → 太极图 r=44 自动旋转 30s/圈 → 4 方位实心圆点 (北/东/南/西) 在 r=72 圆周上可见 → 1px 鎏金外圆环 r=178 完整显示 → 状态进度环 r=130/140 可见但默认 0° → 设备日志确认 `CompassTaiji: Taiji rotation set to XXX.X°` 每 50ms 递增 (2026-06-14 实际验证)

---

### 阶段二：视觉框架扩展 — 鱼眼状态图标（🟡 开发中 ⭐）

> ⚠️ **2026-06-14 代码审计**: 本节描述的鱼眼状态图标 (CreateWifiFisheye/CreateBleFisheye/FisheyeWifiStatus/FisheyeBleStatus) **当前未实现**. `attitude_display.h/cc` 中无相关枚举/函数. 本节作为未来工作参考保留.

**目标**：在太极图内部位置创建两个 36×36 圆形图标作为鱼眼：
- **上方鱼眼** = WiFi 状态图标：圆心 (180, 144)，实际 pos (162, 126)
- **下方鱼眼** = 蓝牙状态图标：圆心 (180, 216)，实际 pos (162, 198)

**★ 关键设计变更：伪旋转方案**
- 旧方案：鱼眼随太极图 30s/圈 旋转 → 问题：符号会上下颠倒，无法辨识
- 新方案：鱼眼位置永久固定，不随太极图旋转 → 效果：视觉上鱼眼"漂浮"在太极图之上，符号永远正立可读

**开发任务（预估 1 周）**：

| # | 具体任务 | 修改文件 | 验收标准 |
|---|---------|---------|---------|
| 1 | 添加鱼眼参数宏 + 状态枚举 | `display/attitude_display.h` | `FISHEYE_ICON_SIZE=36` / `FISHEYE_WIFI_POS_X=162/Y=126` / `FISHEYE_BLE_POS_X=162/Y=198` + `enum WifiStatus` / `enum BleStatus` |
| 2 | 创建 WiFi 鱼眼 icon（金色描边 + 内部 WiFi 符号 18px） | `display/attitude_display.cc` → 新增 `CreateWifiFisheye()` | pos(162,126) 圆形可见，内部符号清晰，创建在 screen 上，lv_obj_move_foreground() |
| 3 | 创建 BLE 鱼眼 icon（白底 + 内部 BT 符号 18px） | `display/attitude_display.cc` → 新增 `CreateBleFisheye()` | pos(162,198) 圆形可见，内部符号清晰 |
| 4 | **静态状态驱动**：`UpdateWifiFisheye(status)` / `UpdateBleFisheye(status)` | `display/attitude_display.cc` → 新增 + `public:` 方法声明 | 手动设置状态：WiFi 已连 → 金色填充；WiFi 断开 → 灰色；BLE 已连 → 白底金色符号；BLE 广播 → 白色脉冲；BLE 关闭 → 灰色 |
| 5 | 鱼眼脉冲动画（CONNECTING / ADVERTISING 状态） | `display/attitude_display.cc` → 用 `lv_anim_t` 周期 opa 变化 | WiFi 扫描中 / BLE 广播时，图标 opacity 或 color 在 150↔255 之间 300ms 脉冲 |
| 6 | 编译 & 烧录 & 真机验证 | `build_and_flash.sh` | 上电 → 两个鱼眼位于太极图正确位置 → 位置固定不旋转（伪旋转） → 手动切换 WiFi/BLE 状态测试 → 无错位/无崩溃/无遮挡 |

**关键设计约束**：
- 鱼眼图标创建在 **screen 上而非 attitude_container_ 内**，避免 attitude_container_ 内部遮挡
- 位置永久固定，不随太极图旋转（伪旋转方案）→ 符号永远正立可读
- 鱼眼状态枚举与后续阶段的真实 WiFi/BLE 驱动解耦，便于先视觉开发后驱动集成
- 使用 lv_obj_move_foreground() 保证鱼眼在最上层，不被八卦名大字遮挡

**资源影响**：
- PSRAM 增量：约 15KB（2 个 36×36 对象 + 2 个 18px 符号 label）
- CPU 增量：+0.5%（仅状态变化时更新颜色，不每 50ms 更新位置）

---

### 阶段三：AI 运势引擎 — 三态状态机 + 结果卡（🟢 核心功能 ⭐）

> ⚠️ **2026-06-14 代码审计**: 本节描述的 AI 运势引擎 (FortuneState 枚举/ShowFortune/CreateFortuneCard/200×240 结果卡) **当前未实现**. `attitude_display.h/cc` 中无相关代码. 本节作为未来工作参考保留.

**目标**：实现 Idle / Animating / Result 三态状态机，配合 200×240 胶囊形结果卡、方位大字颜色脉冲、八卦名大字颜色脉冲等全套视觉反馈。

**★ 设计原则（详见 5.6.4）**：
- 所有高亮仅修改颜色/透明度，不修改尺寸和位置 → 避免与 50ms 旋转位置同步 bug
- 鱼眼图标伪旋转（位置永久固定）→ 符号永远正立可读
- 动画态：太极保持 30s/圈（不加速），仅八卦+卦象 15s/圈 加速 → 减少视觉疲劳和 CPU 压力

**开发任务（预估 2 周）**：

| # | 具体任务 | 修改文件 | 验收标准 |
|---|---------|---------|---------|
| 1 | 添加 FortuneState 状态机成员变量 + 计时器句柄 + 高亮索引 | `display/attitude_display.h` | `FortuneState fortune_state_` / `lv_timer_t* fortune_anim_timer_` / `int fortune_highlight_dir/gua_` |
| 2 | 扩展公共方法声明（适配 200×240 结果卡 7 行内容） | `display/attitude_display.h` | `ShowFortune(gua,dir,func_label,gua_name,core_text,yi,ji)` / `EnterIdleState()` / `EnterAnimatingState()` / `EnterResultState()` / `HighlightDirection()` / `HighlightGua()` / `CreateFortuneCard()` / `DestroyFortuneCard()` |
| 3 | `EnterAnimatingState()` 动画态实现（★ 简化版） | `display/attitude_display.cc` | 3 秒：太极保持 30s/圈，八卦+卦象加速 15s/圈；全局亮度脉冲 opacity 200↔255（600ms）；外圈 border_color 金↔亮金 脉冲；鱼眼 bg_color 金↔深灰 脉冲 5 次；不使用 border_width 脉冲；结束后自动切 Result |
| 4 | `HighlightDirection(int dir)` 方位大字脉冲 | `display/attitude_display.cc` | 指定方位大字 text_color 在 0xFFD700↔0xFFFFFF 之间 600ms × 3 次脉冲；保持 label 尺寸和 pos 不变 |
| 5 | `HighlightGua(int gua_idx)` 卦名大字颜色脉冲 | `display/attitude_display.cc` | 指定卦名大字 text_color 金↔白 脉冲 3 次；尺寸保持 44×44 不变；旋转逻辑无需跳过（无尺寸变更） |
| 6 | ★ **中心结果卡** 200×240 胶囊形（核心新设计） | `display/attitude_display.cc` → `CreateFortuneCard()` | pos(80,60)；圆角 100px；背景 0x121212 + 96% opa；2px 金色边框 0xFFD700；内部 7 行内容：[行1] 功能标识"今日运势 ☀"(30px 金色) [行2] 卦象小图 72×48 canvas [行3] 卦名大字"乾为天"(30px 金色) [行4] 核心解读一句话(24px 白色) [行5] 宜清单(22px 绿色) [行6] 忌清单(22px 红色) [行7] 底部提示"触摸任意位置关闭"(16px 灰色) |
| 7 | 触摸结果卡关闭 | `display/attitude_display.cc` | 点击卡片任意位置 → 销毁卡片 → 切回 Idle 态 |
| 8 | Result 态超时自动恢复 | `display/attitude_display.cc` | fortune_result_timer_ 30s 超时 → 自动 `EnterIdleState()` |
| 9 | **语音驱动**：AI 对话触发运势流程 | 接入 `SetInterpretation()` 或扩展 `McpTool` 协议 | 语音指令 "小知，给我看看今天的运势" → 设备进入 Animating(3s) → Result(结果卡+脉冲) → AI 语音播报解读 |
| 10 | 编译 & 烧录 & 真机验证 | `build_and_flash.sh` | 手动触发 `ShowFortune(...)` → 全流程正常：3秒加速动画 → 200×240 结果卡弹出(7行内容)+ 方位/卦名颜色脉冲 → 触摸关闭/30秒后自动恢复 → 多次运行无泄漏/无崩溃/无位置错位 |

**核心状态机流程**（见文档 5.6.3）：
```
Idle → Animating(3s, 八卦加速+全局亮度脉冲) → Result(30s, 结果卡+颜色脉冲, 触摸提前退出) → Idle
```

**资源影响**：
- PSRAM 增量：约 150KB（200×240 结果卡 + 7 行子元素 + LVGL 动画对象）
- CPU 增量：+3-5%（Animating 态八卦加速旋转 + 颜色脉冲，无尺寸变更重排）

---

### 阶段四：真实 WiFi / BLE 状态驱动鱼眼（🟡 驱动集成）

> ⚠️ **2026-06-14 代码审计**: 阶段二 (鱼眼) 未实现, 阶段四前置依赖未满足. 本节作为未来工作参考保留.

**目标**：将阶段二的"手动设置鱼眼状态"替换为真实 ESP-IDF WiFi / NimBLE 状态驱动。

**开发任务（预估 1.5 周）**：

| # | 具体任务 | 修改文件 | 验收标准 |
|---|---------|---------|---------|
| 1 | WiFi 状态集成 | `main/application.cc` / `display/attitude_display.cc` | WiFi 启动 → 鱼眼变灰 (DISCONNECTED)；开始连接 → 金色脉冲 (CONNECTING)；连接成功 → 金色常亮 (CONNECTED) |
| 2 | BLE 广播/连接集成 | `main/application.cc` 或新增 `main/ble/ble_server.*` + `display/attitude_display.cc` | 启动 BLE 广播 → 白色脉冲 (ADVERTISING)；配对成功 → 白底金色符号 (CONNECTED)；关闭 → 白底灰色符号 (DISABLED) |
| 3 | 电量告警（可选：在 WiFi 鱼眼上叠加 12px 电量百分比小字） | `main/application.cc`（已有 BQ27220 支持） | 电量 < 20% → 鱼眼背景红色脉冲告警 |
| 4 | RTC 时钟 / IMU 姿态驱动 | `main/application.cc`（已有 PCF85363 / QMI8658 支持） | 时间更新 + IMU 姿态数据正常，可用于后续的"举起罗盘唤醒"功能 |
| 5 | 编译 & 烧录 & 真机验证 | `build_and_flash.sh` | 真实环境测试：断开 WiFi → 鱼眼变灰；连接 WiFi → 脉冲→金色；BLE 广播 → 下方鱼眼脉冲；各种状态切换无异常/无遮挡 |

**注意**：ESP-IDF v5.x 自带 NimBLE，需配置 `CONFIG_BT_NIMBLE_ENABLED=y` + `CONFIG_BT_ENABLED=y`；WiFi 事件回调在 `esp_event_handler_instance_register()` 中注入鱼眼状态更新；**鱼眼图标位置永久固定不旋转**（伪旋转方案）。

---

### 阶段五：AI 服务端运势工具对接 + 联调（🟢 功能闭环）

> ⚠️ **2026-06-14 代码审计**: 阶段三 (运势引擎) 未实现, 阶段五前置依赖未满足. 本节作为未来工作参考保留.

**目标**：将阶段三的"手动触发 ShowFortune()"升级为由 AI 对话/工具调用自动触发。

**开发任务（预估 1 周）**：

| # | 具体任务 | 文件/模块 | 说明 |
|---|---------|---------|-----|
| 1 | 服务端 Fortune Engine MCP 工具注册 | MCP server / 服务端 Python | 暴露 fortunes: today / wealth / career / love / mood_gua / huangli / solar_term / custom 共 8 个 tool |
| 2 | AI 对话模板 / 系统 Prompt | 服务端配置 | "有趣的玄学大师"人设；识别 8 类意图并返回结构化结果（gua_index + dir_index + func_label + gua_name + core_text + yi_text + ji_text） |
| 3 | 设备端协议扩展：处理 `FortuneData` 消息 | `main/mcp_server.*` / `main/application.cc` | 接收服务端返回的结构化 JSON → 调用 `attitude_display.ShowFortune(...)` |
| 4 | 语音播报同步 | `main/application.cc` | 调用 AI 语音 TTS，与视觉动画同步：Animating 播前奏 → Result 播解读 |
| 5 | 多场景端到端验证 | `build_and_flash.sh` + 真机 | 完整语音交互测试："小知，给我看看今天的财运方位" → Animating 3秒(八卦加速+脉冲) → 200×240 结果卡弹出(7行内容) + 方位大字/卦名颜色脉冲 → AI 语音播报 → 30秒后自动恢复 |

---

### 阶段六：增强与优化（🟡 可选功能，先低后高）

> ⚠️ **2026-06-14 代码审计**: 阶段二/三 未实现, 部分阶段六任务 (24节气/IMU 感知) 仍可独立开展. `SetAttitudeData()` API 已存在但 UI 未消费 (待 IMU 接入).

**目标**：根据用户反馈和设备资源余量，逐步添加高级功能。

**开发任务列表**：

| 功能 | 优先级 | 预计工作量 | 说明 |
|-----|-------|---------|-----|
| 节气自动提醒 | 中 | 1 天 | 启动时检测 RTC 日期，若当日为节气 → 鱼眼脉冲 + 语音播报"今日为立春" |
| 运势历史记录 | 中 | 2 天 | 保存最近 20 次占卜到 NVS（key: `fortune_history_00..19`），可语音查询"上次运势" |
| 用户自定义占卜文本 | 中 | 3 天 | 支持用户语音输入自定义问题，调用 fortunes:custom tool，返回定制解读 |
| IMU 姿态感知 | 低 | 2 天 | 利用 QMI8658 实现"举起罗盘"唤醒 + 根据朝向自动更新"北"的位置（方位大字跟随设备旋转） |
| 远程截图分享 | 低 | 2 天 | 已有 `SnapshotToJpeg()` → 通过 MCP 上传，支持"小知分享当前屏幕" |
| 手势控制 | 低 | 3 天 | CST816S 支持滑动手势 → 可用于"滑动关闭结果卡"等辅助操作 |
| 多用户个性设置 | 低 | 1 周 | 需要服务器配合，通过 UUID 区分用户（延后） |

---

### 总工作量 & 交付节奏

| 阶段 | 内容 | 预估时间 | 交付物 |
|-----|-----|---------|-------|
| 一 | 基础罗盘（太极+八卦+方位大字+旋转） | ✅ 已完成 | 正常显示罗盘 + 旋转正常 |
| 二 | 鱼眼状态图标（伪旋转，WiFi/BLE 状态） | **~1 周** | 2 个鱼眼可见 + 位置固定 + 手动状态切换正常 |
| 三 | AI 运势引擎三态状态机 + 200×240 结果卡 | **~2 周** | 手动触发 ShowFortune() 可完整演示三态 + 7行内容结果卡 |
| 四 | 真实 WiFi/BLE 状态驱动鱼眼 | **~1.5 周** | 鱼眼状态随真实网络状态变化 + 伪旋转正常 |
| 五 | AI 服务端工具对接 + 联调 | **~1 周** | 语音指令 → 全流程可用（动画+结果卡+播报） |
| 六 | 增强与优化 | **~2-3 周**（可选） | 节气提醒 / 历史记录 / IMU 等 |

**最短路径（MVP，不含阶段六）**：约 5-6 周完成 鱼眼状态 + 三态引擎 + 结果卡 + 真实网络驱动 + AI 联调

**★ 建议实施顺序**：阶段二（鱼眼）→ 编译烧录验证 → 阶段三（核心三态+结果卡） → 编译烧录验证 → 阶段四（真实网络） → 阶段五（AI 联调） → 阶段六（可选）

**★ 已废弃方案总结**：
- ❌ 8 个常驻功能图标：视觉密度过高，与卦象符号/方位大字在 r=110~154 区域严重重叠，已废弃，改为结果卡内文字标识"今日运势 ☀"
- ❌ 尺寸放大高亮：八卦名大字 48→64，功能图标 36→44，与 50ms 旋转位置同步产生冲突，导致视觉闪烁和位置错位，改为纯颜色脉冲 (金↔白)
- ❌ 太极图加速旋转：Animating 态太极 30→10s 导致视觉疲劳和 CPU 压力过大，改为太极保持 30s/圈，仅八卦+卦象 15s/圈 加速
- ❌ 鱼眼随太极旋转：符号上下颠倒无法辨识，改为伪旋转方案（位置永久固定）

---

## ⭐ 当前进度报告 (2026-06-14 代码审计)

> **审计结论**: 本文档原 7.2/7.3/7.4/7.5/7.6 章节描述的 Target1 功能 (鱼眼状态图标、AI 运势引擎、真实驱动、AI 服务端对接、增强优化) **当前均未实现**. 当前代码为 Target2 风格 (4 层同心圆 + 状态进度). 本节为最新事实基线.

### 实际实现状态 (代码+设备验证)

| 阶段 | 内容 | 状态 | 证据 |
|------|------|------|------|
| 阶段一 基础罗盘 (Target2) | 4 层同心圆 + 太极图 + 4 方位点 | ✅ **已完成** | `attitude_display.cc` 521 行 + 设备日志 2026-06-14 验证 |
| 阶段二 鱼眼状态图标 | WiFi/BLE 36×36 圆形 + 伪旋转 | ❌ **未实现** | `attitude_display.h/cc` 无 `FisheyeWifiStatus`/`FisheyeBleStatus` 枚举, 无 `CreateWifiFisheye()`/`CreateBleFisheye()` |
| 阶段三 AI 运势引擎 | 三态状态机 + 200×240 结果卡 | ❌ **未实现** | `attitude_display.h/cc` 无 `FortuneState`/`ShowFortune()`/`CreateFortuneCard()` |
| 阶段四 真实 WiFi/BLE 驱动 | esp_event_handler → 鱼眼状态 | ❌ **不适用** | 阶段二/三 未实现, 阶段四前置依赖未满足 |
| 阶段五 AI 服务端对接 | MCP Fortune 工具注册 | ❌ **不适用** | 阶段三 未实现, 阶段五前置未满足 |
| 阶段六 增强与优化 | IMU/手势/历史记录 | 🟡 **部分** | `SetAttitudeData()` API 已存在但 UI 未消费 |

### 已实现功能明细 (代码+日志证据)

- ✅ **4 层同心圆布局**: `attitude_display.cc` 的 `SetupUI()` 调用 `CreateLayer0Taiji/CreateLayer1CoreInfo/CreateLayer2DynamicIndicator/CreateLayer3StatusProgress/CreateLayer4Boundary/CreateCompassPoints`
- ✅ **太极图自动旋转 30s/圈**: 设备日志 `CompassTaiji: Taiji rotation set to XXX.X°` 每 50ms 递增 (验证时间: 2026-06-14, 设备运行 622 秒, 累计 12380 步 = 3714°)
- ✅ **4 方位圆点 (6×6, 鎏金)**: `CreateCompassPoints()` 创建 N/E/S/W 4 个 lv_obj
- ✅ **5 档状态颜色系统**: `attitude_theme.h` 中 `state_normal/light/mid/heavy/danger` 固定色值
- ✅ **主题色值 (固定玄黑+鎏金)**: `AttitudeTheme` 单例提供一组不可变色值
- ✅ **5 个 MCP 工具**: `taiji_rotate_cw`/`taiji_rotate_ccw`/`taiji_set_rotation`/`taiji_get_rotation`/`taiji_reset_rotation` 在 `mcp_server.cc` 已注册
- ✅ **状态进度环 + 背景环**: `layer3_progress_arc_` (r=140) + `layer3_bg_arc_` (r=130), lv_arc, 4px 宽
- ✅ **1px 鎏金外圆环**: `layer4_outer_ring_` (r=178)

### 质量评估

| 维度 | 评级 | 说明 |
|-----|-----|-----|
| 编译 | ✅ 通过 | `build/xiaozhi.bin` 2.79MB, Jun 14 17:58 构建 |
| 线程安全 | ✅ 良好 | `DisplayLockGuard` 包裹所有 UI 修改 |
| 资源使用 | ✅ 健康 | SRAM 124KB 可用, PSRAM 500KB 利用 |
| API 文档 | ✅ 完整 | 所有公共方法有 Doxygen 注释 |
| TODO 残留 | ✅ 零 | 全部实现完整 |
| SetAttitudeData() | 🟡 预留 | 仅存储值, UI 未消费 (待阶段乙 IMU 接入) |
| SetInterpretation() | 🟡 弃用 | 保留空实现以兼容 |

### 文档与代码不一致项 (本次同步修复)

| 文档位置 | 文档原描述 | 实际代码 | 修复方式 |
|---------|-----------|---------|---------|
| 7.1 表格 (line 1042-1053) | 8 卦名+卦象+方位大字标 ✅ 已完成 | 这些功能均未实现 | 7.1 表格已重写, 标记 ❌ 未实现 |
| 7.1 验收行 | 提及"方位大字'北东南西'清晰可见" | 实际是 6×6 圆点 | 已改为"4 方位实心圆点" |
| 7.2 鱼眼 (line 1059+) | 🟡 开发中, 详细任务分解 | 代码无此功能 | 见下方 7.2 ⚠️ 标注 |
| 7.3 AI 运势 (line 1090+) | 🟢 核心功能, 详细任务分解 | 代码无此功能 | 见下方 7.3 ⚠️ 标注 |
| 7.4-7.6 (line 1140+) | 详细任务分解 | 全部基于未实现前置 | 见下方 7.4-7.6 ⚠️ 标注 |
| 5.3 参数宏 (line 862) | Target1 宏 (ANIM_*/FORTUNE_*/FISHEYE_*) | 代码无此宏 | 后续 Step 4 处理 |
| 5.4 旋转参数 (line 895) | 3 状态参数 | 代码仅 30s/圈 | 后续 Step 4 处理 |

---

---

## 8. 性能与资源评估

### 8.1 资源估算（本板: Waveshare ESP32-S3-Touch-LCD-1.85B）

| 资源 | 占用 | 可用总量 | 利用率 | 说明 |
|-----|-----|--------|------|-----|
| Flash (固件) | ~2-3MB | 16MB | **~15-19%** | 主程序+字体+图像资产 |
| Flash (OTA/数据分区) | ~1MB | 6MB+ | **~17%** | 图像/字体/提示音 NVS/OTA 数据 |
| RAM (内部 SRAM) | ~200-250KB | 512KB | **~40-49%** | 运行时对象栈 + heap |
| RAM (PSRAM) | ~500KB-1MB | 8MB | **~6-13%** | LVGL 图像缓冲 (2MB) + Canvas (太极图83KB + 卦象*8 ~17KB) |
| CPU 占用 | ~15-30% | 240MHz 双核 | **低** | LVGL 刷新(30fps) + 旋转任务(20Hz) + 音频处理 |

**非常充裕的资源，足以支持全部功能（运势引擎+BLE+OTA）**

### 8.2 关键性能优化点

1. **PSRAM 优先分配**:
   ```cpp
   // 在 compass_taiji.cc Canvas 分配时:
   uint32_t* buf = (uint32_t*)heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
   if (!buf) buf = (uint32_t*)malloc(size);  // fallback 内部 RAM
   ```

2. **LVGL 图像缓存** (2MB):
   - PSRAM 8MB 可用 → 分配 2MB 缓存
   - 可显著加速图像（卦象/表情/自定义 PNG）首次绘制

3. **任务优先级设计** (`FreeRTOS`):
   - **AudioTask** (最高): 确保音频流不中断
   - **LVGL Task** (中): 刷新 UI (30fps 刷新)
   - **AutoRotation Task** (最低): 旋转动画，20Hz 位置更新，延迟不影响功能
   - **Main Event Loop** (中): 事件分发/协议处理

4. **显示锁机制** (`DisplayLockGuard`):
   - 任何修改 LVGL 对象的操作必须持有锁
   - 避免任务并发导致 UI 崩溃（FreeRTOS 多任务经典问题）

5. **文字标签置顶策略**:
   - 文字标签（八卦名/方位大字）直接创建在 `lv_screen_active()` 上
   - 创建后立即调用 `lv_obj_move_foreground()`
   - 旋转时每次更新位置后再次调用 `lv_obj_move_foreground()`（如果被其他对象遮挡）
   - **避免了 attitude_container_ 内的图形/边界环覆盖文字**

### 8.3 低功耗策略

- **待机模式**: 无交互 60 秒后进入省电
  - 降低屏幕亮度
  - 减慢太极旋转（30s → 60s/圈）
  - `esp_pm_configure()` 设置 APB 降频

- **深度睡眠**: 无交互 10 分钟 + 电量低（可选，BQ27220 提供电量数据）

- **电源管理锁**: 语音/更新显示期间持有锁，防止 CPU 降频
  ```cpp
  // esp_pm_lock_create(ESP_PM_APB_FREQ_MAX, ...)
  ```

---

## 9. I2C 设备地址参考（本板）

所有传感器/触控/RTC/电量 共享同一 I2C 总线

| 设备 | I2C 地址 | 连接 | 用途 |
|-----|---------|-----|-----|
| **CST816S** | **0x15** (7-bit) | I2C SCL=GPIO10, SDA=GPIO11 | 电容触控 |
| **QMI8658** | **0x6B** (7-bit) | I2C SCL=GPIO10, SDA=GPIO11 | 六轴 IMU (加速度+陀螺仪) |
| **ES8311** | **0x18** (7-bit, 音频 codec) | I2C (音频侧I2C) | 播放音频编码 |
| **ES7210** | **0x20** (7-bit, AEC codec) | I2C | 录音/回声消除 |
| **BQ27220** | **0x55** (7-bit) | I2C | 电量检测 |
| **PCF85363** | **0x51** (7-bit) | I2C | RTC 实时时钟 |

---

## 10. 错误处理与稳定性

### 10.1 常见故障场景

| 故障 | 表现 | 处理策略 |
|-----|-----|---------|
| WiFi 连接失败 | 状态显示"连接失败"图标 | 3次重试 → 进入配网模式 |
| 服务器超时 | 对话卡住 >5s | `IsTimeout()` 检测 → 关闭音频通道 → 提示"网络异常" |
| 内存不足 | 图像/Canvas 分配失败 | 回退到简化 UI（无动画） + 输出错误日志 |
| 音频流中断 | 说话突然中断 | 重新打开通道 → 恢复对话上下文（session_id 保持） |
| OTA 失败 | 升级中死机 | 回滚到旧分区（ESP-IDF OTA 自带） + 下次重新尝试 |
| 触控失效 | 点屏幕无反应 | CST816S 复位（GPIO1 控制复位脚） → 重新初始化 |
| IMU 失效 | 无姿态数据 | QMI8658 复位 → 降级为"无IMU 简化模式" |

### 10.2 Watchdog / 看门狗

- 启用 FreeRTOS Task Watchdog (`CONFIG_ESP_TASK_WDT_INIT=y`)
- LVGL 刷新任务注册到 WDT
- 严重崩溃: 核心转储到 Flash（如启用 `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH`）

### 10.3 日志策略

- 默认 INFO 级别（可通过 `idf.py monitor` 查看）
- 关键路径: `Application` / `Protocol` / `McpServer` / `Display` 四个 TAG
- 生产环境可降级到 WARN 级别减少串口输出

---

## 11. 开发与调试工具链

| 工具 | 用途 | 命令/路径 |
|-----|-----|---------|
| ESP-IDF v5.5.4 | 构建 + 烧录 | `idf.py build/flash/monitor` |
| 串口 Monitor | 日志查看 | `idf.py -p /dev/cu.usbmodem101 monitor` (macOS) |
| LVGL Image Converter | PNG → C 数组 | `scripts/Image_Converter/lvgl_tools_gui.py` |
| SPIFFS Asset Builder | 资源打包 | `scripts/spiffs_assets/build.py` |
| JTAG 调试 (可选) | 单步调试 | 需要官方调试器 + OpenOCD |
| 通用 BLE 调试 App | BLE 测试 | iOS/Android 商店搜索 "BLE Scanner" |

---

## 12. 总结与下一步

AI 罗盘产品基于成熟的 Xiaozhi AI 开源项目快速演进，针对 **Waveshare ESP32-S3-Touch-LCD-1.85B** 优化：

**✅ 已有能力**（无需开发即可拥有）:
- 完善的语音交互（唤醒/录音/AI 对话/TTS 播报，双麦克风+AEC）
- 360×360 QSPI LCD 支持 + LVGL UI 引擎
- 太极/罗盘视觉组件（核心视觉已落地，代码存在！）
- MCP 工具链基础框架
- OTA + 主题系统 + 多语言(25+)

**🔧 需要新增**（本路线图核心）:
1. **AI 运势引擎** - MCP 工具 + 客户端占卜 UI/动画（扩展 `AttitudeDisplay`）
2. **蓝牙 BLE** (可选) - WiFi 配网辅助 + 文字消息推送

**📅 项目总周期**: ~3-5 周（主功能 2 周 + 可选 BLE 2 周 + 1 周联调打磨）

**✅ 下一步优先级**:
1. **真机验证** - 确认当前罗盘 UI 可正常显示（太极图旋转、方位大字可见、卦象符号正确）
2. **占卜 UI 动画** - 实现 `HighlightDirection()`、`HighlightGua()`、`StartFortuneAnimation()`
3. **Fortune MCP 工具** - 在服务端注册 fortune 工具（今日运势、财运、事业等）
4. **联调测试** - 验证 "用户说 '今天财位在哪'" → AI 调用 fortune → UI 高亮东方大字 + 播放解读
5. **可选 BLE** - 并行启动 NimBLE 集成
6. **真机优化** - 优化动画流畅度、降低功耗（待机省电模式）、加 IMU 姿态感知

---


*文档版本: v1.4 (Target1→Target2 文档对齐: 删除鱼眼/运势/结果卡/13章幻觉, 重写 5.3/5.4/5.6/7/12)*
*更新日期: 2026-06-14*
*适用硬件: Waveshare ESP32-S3-Touch-LCD-1.85B (ESP32-S3R8, 8MB PSRAM, 16MB Flash, 360×360 QSPI LCD)*
