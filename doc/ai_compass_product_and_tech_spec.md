# AI 罗盘 - 产品功能与技术实现方案

> **目标硬件**: Waveshare ESP32-S3-Touch-LCD-1.85B (ESP32-S3R8, 360x360 QSPI LCD, 8MB PSRAM + 16MB Flash)
> **产品定位**: 基于 ESP32-S3 的 AI 语音交互设备，融合太极罗盘视觉与 Xiaozhi 语音协议；WiFi 已可用，BLE / 触控 / 传感器驱动按路线图接入。

> **📋 文档状态 (v1.6)**:
> - **【已实现】** 章节 1–3（部分）、5.1–5.5、6、7.1、8–11：描述当前 Target2 固件（4 层同心圆 + 太极 30s/圈 + 4 方位圆点 + 状态进度环）。
> - **【规划中】** 章节 4、7.2–7.6、附录 A：鱼眼、AI 运势、BLE 等；附录 A 中的八卦名/卦象/方位大字为 **Target1 历史参考，不再实施**。
> - **【硬件有 · 固件未集成】** 见 §1.1.1：板卡自带外设与当前板级驱动的差异。
> - **【验收清单】** 迭代拆分与勾选验收见 [`ai_compass_iteration_acceptance.md`](ai_compass_iteration_acceptance.md)。

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

### 1.1.1 固件集成状态（1.85B 板级驱动）

> 上表为 Waveshare 板卡**物理规格**；下表反映 `main/boards/waveshare/esp32-s3-touch-lcd-1.85b/` **当前已接入固件** 的状态。

| 外设 | 板卡硬件 | 固件已集成 | 说明 |
|-----|---------|-----------|------|
| ST77916 QSPI LCD 360×360 | ✅ | ✅ | `AttitudeDisplay` + `CompassTaiji` |
| I2S 麦克风 / 扬声器 | ✅ | ✅ | `NoAudioCodecSimplex`（非 ES8311/ES7210 专用驱动） |
| WiFi | ✅ | ✅ | `WifiBoard` 基类 |
| Boot / 电源按键 | ✅ | ✅ | GPIO 按键 |
| 背光 PWM | ✅ | ✅ | `PwmBacklight` |
| CST816S 触控 | ✅ | ❌ | `config.h` 有引脚定义，板级未初始化触控 |
| ES8311 + ES7210 编解码 | ✅ | ❌ | 硬件存在，当前走简易 I2S 通路 |
| QMI8658 IMU | ✅ | ❌ | `SetAttitudeData()` API 预留，UI 未消费 |
| BQ27220 电量计 | ✅ | ❌ | 未接入板级驱动 |
| PCF85363 RTC | ✅ | ❌ | 未接入板级驱动 |
| Bluetooth BLE | ✅ | ❌ | 芯片支持，固件未启用 NimBLE |

### 1.2 产品定位

| 维度 | 描述 |
|-----|-----|
| **核心概念** | AI 智能罗盘（Xiaozhi Compass） |
| **硬件基础** | Waveshare ESP32-S3-Touch-LCD-1.85B（固定目标） |
| **交互方式** | 语音唤醒 + 自然语言对话 + 按键（触控待集成） |
| **连接方式** | WiFi (主力)；BLE 为规划能力 |
| **文化元素** | 太极图 |
| **AI 能力** | 实时语音对话 (TTS+ASR)、AI 工具调用 (MCP) |
| **目标用户** | 科技爱好者、DIY 玩家、传统文化爱好者、小众礼品市场 |

### 1.3 产品亮点

- **炫酷的视觉交互**: 动态旋转的太极图（30秒/圈） + 鎏金色同心圆环 + 状态进度指示
- **流畅的语音对话**: 基于 Xiaozhi AI 协议的端到端实时语音交互（I2S 录音/播放）
- **灵活的扩展能力**: 通过 MCP 工具链轻松接入更多 AI 能力（查天气、系统控制）
- **精致的硬件工艺**: 1.85" 360×360 圆屏，适合罗盘 UI；板载 IMU/触控等待驱动接入（§1.1.1）

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
    ├── 设备状态查询 (`self.get_device_status`)
    ├── 音量 / 屏幕亮度 / 主题控制
    └── 太极旋转控制 (`self.attitude.taiji_*`，5 个工具)
```

### 2.2 详细功能说明

#### 2.2.1 语音交互系统

**功能流程**:
```
用户唤醒 → 开始录音 → OPUS 编码 → WebSocket 发送 →
服务器 ASR → LLM 推理 → TTS 合成 → 音频流回传 →
设备解码播放（I2S 扬声器输出）
```

**关键特性**:
- 低延迟：端到端 < 1s（网络良好时）
- 实时打断：语音播放时可随时打断
- 流式响应：边说边听，无需等待完整回复
- 本板音频：使用 `NoAudioCodecSimplex`（见 §1.1.1）；ES8311/ES7210 硬件 AEC 待后续接入

**实现文件**:
- [application.h](../main/application.h) - 事件驱动主循环
- [protocol.h](../main/protocols/protocol.h) - 协议抽象基类
- [websocket_protocol.h](../main/protocols/websocket_protocol.h) - WebSocket 实现

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

**太极图绘制算法** ([compass_taiji.cc](../main/display/compass_taiji.cc)):
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
- [compass_taiji.cc](../main/display/compass_taiji.cc) - 太极图组件（自动旋转 30s/圈）
- [attitude_display.h](../main/display/attitude_display.h) - 罗盘显示参数（半径宏定义）
- [attitude_display.cc](../main/display/attitude_display.cc) - 4 层同心圆布局
- [lvgl_display.cc](../main/display/lvgl_display/lvgl_display.cc) - LVGL 显示基类

#### 2.2.3 消息显示系统

**本板配置**: 360×360 圆屏，使用底部单行滚动模式（气泡模式不优化圆形屏，暂不启用）

**消息类型**:
- **系统消息**（半透明灰底）：设备状态、时间、天气
- **用户消息**（绿色底）：用户语音转录文本
- **AI 消息**（白色底/金色字）：AI 回复内容
- **通知 Toast**（顶部横幅，3秒自动消失）：临时提示如"音量+10"/"已连接 WiFi"

**实现文件**:
- [lcd_display.cc](../main/display/lcd_display.cc) - SetupUI/消息气泡逻辑

#### 2.2.4 设备管理系统

**WiFi 配网**:
- SmartConfig（手机 App 一键配网）
- BLE 辅助配网（可选）
- 失败回退：长按按键进入配网模式

**OTA 升级**:
- 启动时检查服务器版本号（`CONFIG_OTA_URL`）
- 用户可通过 AI 指令触发升级
- ESP32-S3 双分区 OTA（16MB Flash → OTA0/OTA1 分区各约 6MB，完全够用）
- 增量/全量双模式（[ota.h](../main/ota.h)）

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

**已注册工具**（[mcp_server.cc](../main/mcp_server.cc)，本板 `AttitudeDisplay` 时额外注册太极工具）:

| 工具名 | 说明 |
|--------|------|
| `self.get_device_status` | 获取设备状态（音量、屏幕、网络等） |
| `self.audio_speaker.set_volume` | 设置扬声器音量 |
| `self.screen.set_brightness` | 设置屏幕亮度（有背光时） |
| `self.screen.set_theme` | 切换亮/暗主题 |
| `self.attitude.taiji_rotate_cw` | 太极顺时针旋转 15° |
| `self.attitude.taiji_rotate_ccw` | 太极逆时针旋转 15° |
| `self.attitude.taiji_set_rotation` | 设置太极角度（0.1° 单位） |
| `self.attitude.taiji_get_rotation` | 获取当前太极角度 |
| `self.attitude.taiji_reset_rotation` | 重置太极角度为 0° |
| `self.reboot` | 重启设备（UserOnly） |
| `self.upgrade_firmware` | OTA 升级（UserOnly） |

> 天气、RTC 时间等工具依赖板级外设驱动；1.85B 当前未集成相关硬件驱动，故未注册。

**扩展接口**: 用户可通过 `McpServer::AddTool()` 注册新工具

**实现文件**:
- [mcp_server.h](../main/mcp_server.h) - MCP 服务核心

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
│   - GetDisplay() → AttitudeDisplay (QSPI, ST77916, 360×360)   │
│   - GetAudioCodec() → NoAudioCodecSimplex (本板当前实现)      │
│   - GetIMU() / GetBattery() / GetRTC() → 规划接入 (见 §1.1.1)│
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

**状态定义**（[device_state.h](../main/device_state.h)）:
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

> **本板当前实现**：音频走 `NoAudioCodecSimplex`（I2S 简易通路）。板载 ES8311/ES7210 硬件存在但固件未接入，见 §1.1.1；下图括号内为硬件能力，非当前代码路径。

```
  用户声音 (双麦克风 → I2S MIC；硬件侧 ES7210 AEC 待接入)
    │
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
│  Application     │  ← I2S 扬声器（NoAudioCodecSimplex；ES8311 待接入）
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
│   ├── compass_taiji.h/cc        # ⭐ 太极图组件（88×88 canvas, 30s 旋转, r=44）
│   ├── attitude_display.h/cc     # ⭐ 4 层同心圆罗盘 UI（Target2）
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

## 4. 新增功能技术方案（【规划中】）

> 本章描述运势与 BLE 等 **Target2 规划能力**。环上八卦名/卦象/方位大字属 Target1，仅见 [附录 A](#附录-a-target1-显示设计参考未实现)，**不再实施**。

> **状态**: 以下功能为规划中的待实现功能，尚未在代码中实现。

### 4.1 AI 运势功能（规划中）

#### 方案 A：服务端 MCP 工具 + 客户端 UI 扩展

```
1. 服务端新增 MCP "Fortune Engine" 工具
   ├─ fortune.today(date, zodiac) → 今日宜/忌+幸运色/数字
   ├─ fortune.wealth(date, zodiac) → 财运方位+建议
   ├─ fortune.career(date, zodiac) → 事业分析
   ├─ fortune.gua(seed, question) → 生成卦象+解读
   └─ fortune.huangli(date) → 黄历宜忌

2. 客户端 (Target2): 扩展 AttitudeDisplay
   ├─ EnterAnimatingState() → 3s：太极保持 30s/圈 + 亮度/外圈/鱼眼/进度环脉冲
   ├─ HighlightDirection(dir) → 指定 N/E/S/W 方位 **6×6 圆点** 颜色脉冲（金↔白，不改位置）
   ├─ HighlightGua(index) → **结果卡内** 卦名/卦象小图颜色脉冲（环上无八卦组件）
   ├─ ShowFortune(...) + CreateFortuneCard() → 200×240 胶囊结果卡（7 行）
   └─ 语音触发见 §7.5（阶段五），阶段三仅手动 ShowFortune() 演示

3. AI Prompt 设计:
   "你是一个有趣的玄学大师。用户问你问题时，先调用 fortune 工具获取数据，
    然后用轻松幽默的风格给出解读。永远保持好玩、不迷信、不贩卖焦虑的原则。"
```

**实现要点**:
- 在 `AttitudeDisplay` 上实现三态状态机与结果卡；高亮 **仅改颜色/透明度**，不改尺寸与位置
- 卦象展示在结果卡内 72×48 canvas，**不**恢复 Target1 环上 `bagua_symbol_canvases_[8]`
- 复用 `SetChatMessage()` 作辅助文本；端到端语音在阶段五联调

#### 方案 B：纯客户端规则引擎（离线可用，保底方案）

- 内置一套简易的"占卜算法"（基于日期 hash + 伪随机）
- 完全离线运行，无需网络
- 适合作为低配功能/演示模式

### 4.2 蓝牙 BLE 连接（规划中）

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

### 5.1 LVGL 初始化流程（【已实现】Target2）

本板: QSPI LCD + ST77916, 360×360。流程基于 [attitude_display.cc](../main/display/attitude_display.cc) 当前实现。

```
1. CustomBoard 构造 → AttitudeDisplay(panel_io, panel, 360, 360, ...)
   └─ SpiLcdDisplay 基类完成 LVGL / QSPI 显示初始化

2. Application 启动后调用 AttitudeDisplay::SetupUI()（仅执行一次）
   ├─ round_mask: 360×360 圆屏遮罩 (radius=180)
   ├─ attitude_container_: 罗盘主容器
   ├─ CreateBackground() → 深色底 + 中心微亮圆 (300×300)
   ├─ CreateLayer0Taiji() → CompassTaiji::Create(cx=180, cy=180, r=44)
   │   └─ 88×88 ARGB8888 canvas，30s/圈自动旋转
   ├─ CompassTaiji::StartAutoRotation(30000)
   ├─ CreateLayer1CoreInfo() → 透明核心容器 108×108 (r=54)
   ├─ CreateLayer2DynamicIndicator() → 内环细弧 r=80, 1px 鎏金
   ├─ CreateLayer3StatusProgress() → 背景弧 r=130 + 进度弧 r=140, 4px
   ├─ CreateLayer4Boundary() → 外圈边框 356×356, 1px 鎏金 (r=178)
   └─ CreateCompassPoints() → N/E/S/W 四个 6×6 鎏金圆点, r=72 圆周
```

### 5.2 罗盘 UI 组件层级（【已实现】真实坐标）

```
屏幕 360×360 (cx=180, cy=180)
├── round_mask (圆屏遮罩)
└── attitude_container_ (360×360)
    ├── background_ (0x0A0A0A)
    │   └── bg_layer_center_ (300×300, 0x121212)
    ├── compass_taiji (Layer0, 88×88 canvas, r=44, 30s/圈旋转)
    ├── layer1_container_ (108×108 透明核心区, r=54)
    ├── layer2_inner_ring_ (160×160 arc, 有效半径 r=80, 1px 鎏金)
    ├── layer3_bg_arc_ (260×260, r=130, 4px 深色背景弧)
    ├── layer3_progress_arc_ (280×280, r=140, 4px 状态色进度弧)
    ├── layer4_outer_ring_ (356×356, r=178, 1px 鎏金外边框)
    └── dir_n/e/s/w_label_ (各 6×6 圆点, r=72 圆周, 固定不动)
```

### 5.3 罗盘参数宏（【已实现】）

来源: [attitude_display.h](../main/display/attitude_display.h)（当前约 98 行）

```c
#define SCREEN_W                  360
#define SCREEN_H                  360
#define ATTITUDE_CENTER_X         180
#define ATTITUDE_CENTER_Y         180
#define VALID_RADIUS              178

#define LAYER1_CORE_RADIUS        54    // Layer1 核心容器半径
#define LAYER2_INDIC_RADIUS       90    // Layer2 内环指示（arc 外径参考）
#define LAYER3_PROGRESS_RADIUS    144   // Layer3 进度环外径参考
#define LAYER4_BOUNDARY_RADIUS    178   // Layer4 外圈边框

// 太极图: CreateLayer0Taiji() 内 TAIJI_RADIUS = 44 → canvas 88×88
// 方位点: CreateCompassPoints() 内 POINTS_RADIUS = 72, POINT_SIZE = 6
```

### 5.4 旋转与状态参数（【已实现】）

| 旋转对象 | 周期 | 每 50ms 步进 (0.1°单位) | 说明 |
|---------|------|------------------------|------|
| 太极图 | **30 秒/圈** | 3600 / (30000/50) = **6** (0.6°/step) | `compass_taiji.cc` FreeRTOS 任务 ✅ |
| 方位圆点 | **固定不动** | — | 6×6 圆点，r=72 |
| 八卦名 / 卦象 | — | — | ❌ 未实现（见 [附录 A](#附录-a-target1-显示设计参考未实现)） |

**自动旋转实现**（[compass_taiji.cc](../main/display/compass_taiji.cc)）:
```cpp
// 每 50ms 执行一次: 角度步进 = 3600 / (period_ms / 50)
// 30 秒一圈: 3600 / 600 = 6 units/step = 0.6°/step
// lv_image_set_rotation() 以 0.1° 为单位, 范围 0~3600
```

### 5.5 主题系统（固定: 玄黑+鎏金）

**`LvglTheme` ([lvgl_theme.h](../main/display/lvgl_display/lvgl_theme.h)) 字段**:
- `background_color_` - 背景: **0x0A0A0A 深色** (玄黑)
- `text_color_` - 文字: **0xD4AF37 金色** (鎏金)
- `chat_background_color_` - 消息区背景: **0x121212 深邃黑**
- `user_bubble_color_` - 用户气泡: **绿色** (保持一致)
- `assistant_bubble_color_` - 助手气泡: **深灰**
- `border_color_` - 边框: **金色 0xD4AF37**
- `low_battery_color_` - 低电量警告: **红色**
- `text_font_` / `icon_font_` / `large_icon_font_` - 字体指针: `font_puhui_basic_30_4`

**`AttitudeTheme` ([attitude_theme.h](../main/display/attitude_theme.h)) 固定色值**:
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

**IDF 组件管理** ([idf_component.yml](../main/idf_component.yml)):
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

### 6.3 构建与烧录（本板）

**推荐**：使用项目根目录固定脚本（与 `.trae/rules/rule_xiaozhi.md` 工作流一致）：

| 平台 | 命令 | 说明 |
|------|------|------|
| Linux / macOS | `./build_and_flash.sh` | 默认编译 + 烧录 + 监视 |
| Linux / macOS | `./build_and_flash.sh build` | 仅编译 |
| Windows | `.\build_and_flash.ps1` | 默认编译 + 烧录（自动检测 COM 口） |
| Windows | `.\build_and_flash.ps1 -Port COM9` | 指定串口 |
| Windows | `.\build_and_flash.ps1 -BuildOnly` | 仅编译 |
| Windows | `.\build_and_flash.bat` | 调用上述 PowerShell 脚本的入口 |

> **注意**：编译/烧录时不要使用 `2>&1` 重定向，以免丢失错误输出。

**手动 idf.py 流程**（备选）:

```bash
# 0. 确认 ESP-IDF 环境已加载
export IDF_PATH=$HOME/.espressif/v5.5.4/esp-idf
source $IDF_PATH/export.sh

# 1. 设置目标芯片
idf.py set-target esp32s3

# 2. 清理旧配置（如之前构建过其他板）
idf.py fullclean

# 3. 构建（板型在 menuconfig / sdkconfig 中选定 1.85B）
idf.py build

# 4. 烧录 + 监控串口
#   Linux: /dev/ttyUSB0
#   macOS: /dev/cu.usbmodem101
#   Windows: COM9
idf.py -p <PORT> flash monitor

# 5. 擦除 (如需完全重置)
idf.py -p <PORT> erase-flash
```

Windows 下可先加载 IDF 环境：`.\scripts\idf_env.ps1`

### 6.4 资产/资源处理

**字体/图像资产**:
- 字体文件: 通过 `LV_FONT_DECLARE(BUILTIN_TEXT_FONT)` 方式使用 - font_puhui_basic_30_4
- 图像资产: 通过 `scripts/build_default_assets.py` 打包为 SPIFFS 镜像（如有自定义卦象 PNG）
- 多语言提示音: `main/assets/locales/xx-XX/*.ogg` → 编译进固件（可存 OTA 分区）

**脚本工具**:
- `scripts/Image_Converter/lvgl_tools_gui.py` - LVGL 图像转换工具
- `scripts/spiffs_assets/build.py` - SPIFFS 资产打包

---

## 7. 开发路线图

> **更新说明**：阶段二至六为 **【规划中】** 能力；运势视觉按 **Target2**（同心圆 + 结果卡）实施，Target1 环上八卦仅作附录参考。每阶段以 **先视觉骨架 → 再真实驱动 → 最后联调** 推进，经 `build_and_flash.sh` / `build_and_flash.ps1` 验证。
>
> **验收清单**：可勾选条目与 Definition of Done 见 [`ai_compass_iteration_acceptance.md`](ai_compass_iteration_acceptance.md)。

### 7.0 阶段与验收迭代对照

| 本文「阶段」 | 验收文档「迭代」 | 说明 |
|-------------|----------------|------|
| 阶段一 | 迭代 0 | 基础罗盘 + Xiaozhi 基线 ✅ |
| 阶段二 | 迭代 1 | 鱼眼 UI（手动状态） |
| 阶段三 | 迭代 2 | 运势三态 + 结果卡（手动 `ShowFortune`） |
| 阶段四 | 迭代 3 | 真实 WiFi/BLE 驱动鱼眼 |
| 阶段五 | 迭代 4 | AI 服务端 Fortune MCP + 语音联调 |
| 阶段六 | 迭代 6 | 增强优化（可选） |
| — | **迭代 5** | 板载外设驱动（触控/编解码/IMU/电量/RTC），可与阶段二/三 **并行** |

---

### 阶段一：基础罗盘（✅ 已完成，核心代码已存在）
**当前代码状态**：[attitude_display.h](../main/display/attitude_display.h) / [compass_taiji.h](../main/display/compass_taiji.h)

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

### 阶段二：视觉框架扩展 — 鱼眼状态图标（🟡 【规划中】）

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
- 使用 `lv_obj_move_foreground()` 保证鱼眼在最上层，不被结果卡或罗盘层遮挡

**资源影响**：
- PSRAM 增量：约 15KB（2 个 36×36 对象 + 2 个 18px 符号 label）
- CPU 增量：+0.5%（仅状态变化时更新颜色，不每 50ms 更新位置）

---

### 阶段三：AI 运势引擎 — 三态状态机 + 结果卡（🟡 【规划中】）

**目标**：在 Target2 罗盘上实现 Idle / Animating / Result 三态，配合 200×240 胶囊结果卡；方位与卦象高亮 **不依赖** Target1 环上八卦/方位大字。

**★ Target2 设计原则**：
- 所有高亮仅修改颜色/透明度，不修改尺寸和位置
- 鱼眼位置永久固定（伪旋转），符号正立可读
- **Animating 态**：太极保持 30s/圈（不加速）；**不**恢复八卦环旋转
- `HighlightDirection(dir)` → 脉冲 r=72 上对应 **N/E/S/W 6×6 圆点**（`dir_*_label_`）
- `HighlightGua(idx)` → 脉冲 **结果卡内** 卦名 label 与 72×48 卦象 canvas（非环上八卦）
- 关闭结果卡：**MVP** 用 Boot 键 / MCP / 30s 超时；CST816S 触控关闭见验收文档 **迭代 5A**（可与本阶段并行）

**开发任务（预估 2 周）**：

| # | 具体任务 | 修改文件 | 验收标准 |
|---|---------|---------|---------|
| 1 | FortuneState 成员 + 计时器 + 高亮索引 | `display/attitude_display.h` | `fortune_state_` / `fortune_anim_timer_` / `fortune_highlight_dir_` |
| 2 | 公共 API：`ShowFortune` / `Enter*State` / `CreateFortuneCard` 等 | `display/attitude_display.h` | 签名覆盖 7 行结果卡字段 |
| 3 | `EnterAnimatingState()`（Target2 简化版） | `display/attitude_display.cc` | 3 秒：太极 30s/圈不变；全局亮度脉冲 opacity 200↔255（600ms）；外圈 border 金↔亮金；鱼眼金↔深灰脉冲约 5 次；**可选**进度环短暂填充动画；结束后自动切 Result |
| 4 | `HighlightDirection(int dir)` | `display/attitude_display.cc` | 指定方位 **圆点** bg_color 在 0xFFD700↔0xFFFFFF 间 600ms×3 脉冲；圆点 6×6 与 pos 不变 |
| 5 | `HighlightGua(int gua_idx)` | `display/attitude_display.cc` | **结果卡内**卦名/卦象颜色金↔白脉冲 3 次；不创建环上八卦 label |
| 6 | 中心结果卡 200×240 胶囊形 | `CreateFortuneCard()` | pos(80,60)；圆角 100px；7 行：功能标识 / 卦象 72×48 / 卦名 / 解读 / 宜 / 忌 / 关闭提示 |
| 7 | 关闭结果卡 | `attitude_display.cc` | Boot 键或 MCP 或（迭代 5A 后）触摸 → 销毁卡片 → Idle |
| 8 | Result 态 30s 超时 | `attitude_display.cc` | `fortune_result_timer_` 超时 → `EnterIdleState()` |
| 9 | 编译 & 真机验证 | `build_and_flash.ps1` | **手动** `ShowFortune(...)`：Animating 3s → 结果卡 + 圆点/卡内卦象脉冲 → 关闭/超时 → Idle；≥5 次无崩溃 |

> **范围说明**：语音指令触发运势流程归属 **阶段五**（§7.5），本阶段不包含端到端语音联调。

**核心状态机流程**：
```
Idle → Animating(3s, 亮度/边框/鱼眼/进度环脉冲) → Result(30s, 结果卡+圆点/卡内高亮) → Idle
```

**资源影响**：
- PSRAM 增量：约 150KB（结果卡 + 子元素 + 动画对象）
- CPU 增量：约 +2~4%（颜色脉冲与进度环动画，无八卦旋转）

---

### 阶段四：真实 WiFi / BLE 状态驱动鱼眼（🟡 【规划中】）

> 前置依赖：阶段二（鱼眼 UI）完成。

**目标**：将阶段二的"手动设置鱼眼状态"替换为真实 ESP-IDF WiFi / NimBLE 状态驱动。

**开发任务（预估 1.5 周）**：

| # | 具体任务 | 修改文件 | 验收标准 |
|---|---------|---------|---------|
| 1 | WiFi 状态集成 | `main/application.cc` / `display/attitude_display.cc` | WiFi 启动 → 鱼眼变灰 (DISCONNECTED)；开始连接 → 金色脉冲 (CONNECTING)；连接成功 → 金色常亮 (CONNECTED) |
| 2 | BLE 广播/连接集成 | `main/application.cc` 或新增 `main/ble/ble_server.*` + `display/attitude_display.cc` | 启动 BLE 广播 → 白色脉冲 (ADVERTISING)；配对成功 → 白底金色符号 (CONNECTED)；关闭 → 白底灰色符号 (DISABLED) |
| 3 | 电量告警（可选：WiFi 鱼眼叠加 12px 电量百分比） | `main/application.cc` + 板级 BQ27220 驱动 | **前置**：迭代 5D 电量计驱动已接入；电量 <20% → 鱼眼红色脉冲 |
| 4 | RTC / IMU 基础读取（可选） | 板级 PCF85363 / QMI8658 驱动 | **前置**：迭代 5C/5E；日志可读时间/IMU，供后续「举起罗盘」等（§7.6） |
| 5 | 编译 & 烧录 & 真机验证 | `build_and_flash.sh` | 真实环境测试：断开 WiFi → 鱼眼变灰；连接 WiFi → 脉冲→金色；BLE 广播 → 下方鱼眼脉冲；各种状态切换无异常/无遮挡 |

**注意**：ESP-IDF v5.x 自带 NimBLE，需配置 `CONFIG_BT_NIMBLE_ENABLED=y` + `CONFIG_BT_ENABLED=y`；WiFi 事件回调在 `esp_event_handler_instance_register()` 中注入鱼眼状态更新；**鱼眼图标位置永久固定不旋转**（伪旋转方案）。

---

### 阶段五：AI 服务端运势工具对接 + 联调（🟡 【规划中】）

> 前置依赖：阶段三（运势引擎 UI）完成。

**目标**：将阶段三的"手动触发 ShowFortune()"升级为由 AI 对话/工具调用自动触发。

**开发任务（预估 1 周）**：

| # | 具体任务 | 文件/模块 | 说明 |
|---|---------|---------|-----|
| 1 | 服务端 Fortune Engine MCP 工具注册 | MCP server / 服务端 Python | 暴露 fortunes: today / wealth / career / love / mood_gua / huangli / solar_term / custom 共 8 个 tool |
| 2 | AI 对话模板 / 系统 Prompt | 服务端配置 | "有趣的玄学大师"人设；识别 8 类意图并返回结构化结果（gua_index + dir_index + func_label + gua_name + core_text + yi_text + ji_text） |
| 3 | 设备端协议扩展：处理 `FortuneData` 消息 | `main/mcp_server.*` / `main/application.cc` | 接收服务端返回的结构化 JSON → 调用 `attitude_display.ShowFortune(...)` |
| 4 | 语音播报与视觉同步 | `main/application.cc` | Animating 阶段 TTS 前奏 → Result 阶段播报解读；失败时提示网络异常，不卡在 Animating |
| 5 | **语音驱动**：对话 / FortuneData 触发 `ShowFortune()` | `main/mcp_server.*` / `main/application.cc` | 「小知，看看今天运势」→ Animating(3s) → Result 卡 + 圆点/卡内脉冲 → TTS |
| 6 | 多场景端到端验证 | `build_and_flash.ps1` + 真机 | 「今天的财运方位」等 ≥3 场景：动画 + 7 行结果卡 + 语音 + 30s 恢复 |

---

### 阶段六：增强与优化（🟡 【规划中 · 可选】）

> 部分任务（如 IMU 姿态）可在阶段二/三之外独立开展；`SetAttitudeData()` API 已存在但 UI 未消费。

**目标**：根据用户反馈和设备资源余量，逐步添加高级功能。

**开发任务列表**：

| 功能 | 优先级 | 预计工作量 | 说明 |
|-----|-------|---------|-----|
| 节气自动提醒 | 中 | 1 天 | 启动时检测 RTC 日期，若当日为节气 → 鱼眼脉冲 + 语音播报"今日为立春" |
| 运势历史记录 | 中 | 2 天 | 保存最近 20 次占卜到 NVS（key: `fortune_history_00..19`），可语音查询"上次运势" |
| 用户自定义占卜文本 | 中 | 3 天 | 支持用户语音输入自定义问题，调用 fortunes:custom tool，返回定制解读 |
| IMU 姿态感知 | 低 | 2 天 | QMI8658（迭代 5C）→「举起罗盘」唤醒；可选让方位 **圆点** 随朝向更新（非恢复方位大字） |
| 远程截图分享 | 低 | 2 天 | 已有 `SnapshotToJpeg()` → 通过 MCP 上传，支持"小知分享当前屏幕" |
| 手势控制 | 低 | 3 天 | CST816S 支持滑动手势 → 可用于"滑动关闭结果卡"等辅助操作 |
| 多用户个性设置 | 低 | 1 周 | 需要服务器配合，通过 UUID 区分用户（延后） |

---

### 总工作量 & 交付节奏

| 阶段 | 内容 | 预估时间 | 交付物 |
|-----|-----|---------|-------|
| 一 | 基础罗盘 Target2（太极+4层环+方位圆点） | ✅ 已完成 | 正常显示罗盘 + 太极 30s/圈旋转 |
| 二 | 鱼眼状态图标（伪旋转，WiFi/BLE 状态） | **~1 周** | 2 个鱼眼可见 + 位置固定 + 手动状态切换正常 |
| 三 | AI 运势引擎三态状态机 + 200×240 结果卡 | **~2 周** | 手动触发 ShowFortune() 可完整演示三态 + 7行内容结果卡 |
| 四 | 真实 WiFi/BLE 状态驱动鱼眼 | **~1.5 周** | 鱼眼状态随真实网络状态变化 + 伪旋转正常 |
| 五 | AI 服务端工具对接 + 联调 | **~1 周** | 语音指令 → 全流程可用（动画+结果卡+播报） |
| 六 | 增强与优化 | **~2-3 周**（可选） | 节气提醒 / 历史记录 / IMU 等 |

**最短路径（MVP，不含阶段六）**：约 5-6 周完成 鱼眼状态 + 三态引擎 + 结果卡 + 真实网络驱动 + AI 联调

**★ 建议实施顺序**：阶段二（鱼眼）→ 编译烧录验证 → 阶段三（核心三态+结果卡） → 编译烧录验证 → 阶段四（真实网络） → 阶段五（AI 联调） → 阶段六（可选）

**★ 已废弃方案总结**：
- ❌ 8 个常驻功能图标：视觉密度过高，改为结果卡内文字标识「今日运势 ☀」
- ❌ 环上八卦名/卦象与尺寸放大高亮：Target2 已移除，高亮改为 **圆点 + 结果卡内** 纯颜色脉冲
- ❌ 太极 Animating 加速：改为全程保持 30s/圈
- ❌ Animating 八卦环加速：Target2 无八卦环，**不再实施**
- ❌ 鱼眼随太极旋转：改为伪旋转（位置固定）

---

## 当前进度报告（代码审计基线）

> **审计结论 (v1.6)**: 阶段一 Target2 **已实现**；阶段二至六为 **规划中**。外设驱动见验收文档 **迭代 5**。

### 实际实现状态

| 阶段 | 内容 | 状态 | 证据 |
|------|------|------|------|
| 阶段一 基础罗盘 (Target2) | 4 层同心圆 + 太极图 + 4 方位点 | ✅ **已完成** | `attitude_display.cc` (~335 行) + 设备日志 2026-06-14 验证 |
| 阶段二 鱼眼状态图标 | WiFi/BLE 36×36 圆形 + 伪旋转 | ❌ **未实现** | 无 `CreateWifiFisheye()` / `CreateBleFisheye()` |
| 阶段三 AI 运势引擎 | 三态状态机 + 200×240 结果卡 | ❌ **未实现** | 无 `FortuneState` / `ShowFortune()` |
| 阶段四 真实 WiFi/BLE 驱动 | esp_event → 鱼眼状态 | ❌ **未开始** | 依赖阶段二 |
| 阶段五 AI 服务端对接 | MCP Fortune 工具 | ❌ **未开始** | 依赖阶段三 |
| 阶段六 增强与优化 | IMU/手势/历史/截图 MCP | 🟡 **部分** | 截图已有；`SetAttitudeData()` 预留 |
| 外设驱动（验收迭代 5） | CST816S/Codec/IMU/电量/RTC | ⬜ **未开始** | 见 §1.1.1 |

### 已实现功能明细

- ✅ **4 层同心圆布局**: `SetupUI()` → `CreateLayer0Taiji` … `CreateCompassPoints`
- ✅ **太极图 30s/圈**: `CompassTaiji::StartAutoRotation(30000)`，日志 `Taiji rotation set to XXX.X°`
- ✅ **4 方位圆点**: 6×6 鎏金圆点，r=72
- ✅ **5 档状态色 + 固定主题**: `attitude_theme.h`
- ✅ **太极 MCP 工具 ×5**: `self.attitude.taiji_*`（`mcp_server.cc`）
- ✅ **状态进度环**: r=130 背景 + r=140 进度，4px `lv_arc`

### 质量评估

| 维度 | 评级 | 说明 |
|-----|-----|-----|
| 编译 | ✅ 通过 | `build/xiaozhi.bin` ~2.79MB (2026-06-14 构建) |
| 线程安全 | ✅ 良好 | `DisplayLockGuard` 包裹 UI 修改 |
| 资源使用 | ✅ 健康 | PSRAM 充裕，太极 canvas ~31KB |
| SetAttitudeData() | 🟡 预留 | 存储姿态值，UI 未消费 |
| SetInterpretation() | 🟡 空实现 | 保留 API 兼容 |

### v1.6 文档修订摘要

| 修订项 | 处理方式 |
|--------|---------|
| §3.3 音频数据流 | 标明 `NoAudioCodecSimplex` 为当前路径，ES8311/ES7210 为待接入 |
| §4.1 / §7.3 运势视觉 | 对齐 Target2：圆点脉冲 + 结果卡内卦象；移除八卦环加速 |
| §7.0 | 新增阶段 ↔ 验收迭代对照表 |
| §7.3 任务 9 语音驱动 | 移至 §7.5；阶段三仅手动 `ShowFortune` |
| §7.4 外设任务 | 修正「已有 BQ27220/RTC/IMU」表述，改为依赖迭代 5 |
| 互链 | 链至 [`ai_compass_iteration_acceptance.md`](ai_compass_iteration_acceptance.md) |

### v1.5 文档修订摘要

| 修订项 | 处理方式 |
|--------|---------|
| §1.1 硬件 vs 固件 | 新增 §1.1.1 固件集成状态表 |
| §2.2.5 MCP 工具 | 更新为 `mcp_server.cc` 真实工具名 |
| §5.1–5.4 | 重写为 Target2 实现；Target1 移至附录 A |
| §6.3 构建 | 补充 `build_and_flash.sh` / `.ps1` |
| §12 总结 | 与进度报告对齐 |
| 失效 `file://` 链接 | 改为 `../main/...` 相对路径 |

## 8. 性能与资源评估

### 8.1 资源估算（本板: Waveshare ESP32-S3-Touch-LCD-1.85B）

| 资源 | 占用 | 可用总量 | 利用率 | 说明 |
|-----|-----|--------|------|-----|
| Flash (固件) | ~2-3MB | 16MB | **~15-19%** | 主程序+字体+图像资产 |
| Flash (OTA/数据分区) | ~1MB | 6MB+ | **~17%** | 图像/字体/提示音 NVS/OTA 数据 |
| RAM (内部 SRAM) | ~200-250KB | 512KB | **~40-49%** | 运行时对象栈 + heap |
| RAM (PSRAM) | ~500KB-1MB | 8MB | **~6-13%** | LVGL 缓冲 + 太极 canvas ~31KB (88×88 ARGB8888) |
| CPU 占用 | ~15-30% | 240MHz 双核 | **低** | LVGL 刷新(30fps) + 旋转任务(20Hz) + 音频处理 |

**非常充裕的资源，足以支持全部功能（运势引擎+BLE+OTA）**

### 8.2 关键性能优化点

1. **PSRAM 优先分配**:
   ```cpp
   // 在 compass_taiji.cc Canvas 分配时:
   uint32_t* buf = (uint32_t*)heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
   if (!buf) buf = (uint32_t*)malloc(size);  // fallback 内部 RAM
   ```

2. **LVGL 图像缓存** (可选 2MB):
   - PSRAM 8MB 可用，可分配较大图像缓存
   - 加速表情 / 自定义 PNG 首次绘制（Target1 卦象资源适用）

3. **任务优先级设计** (`FreeRTOS`):
   - **AudioTask** (最高): 确保音频流不中断
   - **LVGL Task** (中): 刷新 UI (30fps 刷新)
   - **AutoRotation Task** (最低): 旋转动画，20Hz 位置更新，延迟不影响功能
   - **Main Event Loop** (中): 事件分发/协议处理

4. **显示锁机制** (`DisplayLockGuard`):
   - 任何修改 LVGL 对象的操作必须持有锁
   - 避免多任务并发导致 UI 崩溃

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
| build_and_flash | **推荐** 编译+烧录 | `./build_and_flash.sh` / `.\build_and_flash.ps1` |
| ESP-IDF v5.5.4 | 手动构建 | `idf.py build/flash/monitor` |
| 串口 Monitor | 日志查看 | `idf.py -p /dev/cu.usbmodem101 monitor` (macOS) |
| LVGL Image Converter | PNG → C 数组 | `scripts/Image_Converter/lvgl_tools_gui.py` |
| SPIFFS Asset Builder | 资源打包 | `scripts/spiffs_assets/build.py` |
| JTAG 调试 (可选) | 单步调试 | 需要官方调试器 + OpenOCD |
| 通用 BLE 调试 App | BLE 测试 | iOS/Android 商店搜索 "BLE Scanner" |

---

## 12. 总结与下一步

AI 罗盘基于 Xiaozhi 开源项目，针对 **Waveshare ESP32-S3-Touch-LCD-1.85B** 落地 Target2 罗盘 UI。

### 已完成（阶段一）

- 360×360 QSPI 圆屏 + LVGL + `AttitudeDisplay` 四层同心圆
- 太极图 88×88 canvas，30s/圈自动旋转
- 4 方位 6×6 圆点 + 状态进度环 + 鎏金外环
- 语音交互 / OTA / 多语言 / MCP 基础工具（含 `self.attitude.taiji_*`）

### 规划中（阶段二至六）

| 优先级 | 内容 | 参考 |
|--------|------|------|
| P1 | 鱼眼 WiFi/BLE 状态图标 | §7.2 |
| P2 | AI 运势三态 + 结果卡（Target2） | §7.3、§4.1 |
| P3 | 真实网络驱动鱼眼 | §7.4 |
| P4 | 服务端 Fortune MCP + 语音联调 | §7.5 |
| P5 | 板载外设驱动 | 验收文档 **迭代 5**、§1.1.1 |
| P6 | IMU / 手势 / 节气等 | §7.6 |

**验收与迭代勾选**：[`ai_compass_iteration_acceptance.md`](ai_compass_iteration_acceptance.md)

### 建议下一步

1. **维持基线**：每次迭代后 `.\build_and_flash.ps1` 编译烧录，截图回归（`snapshot_recv.py --reset`）
2. **阶段二**：鱼眼 UI（伪旋转）
3. **外设驱动**（可并行）：验收文档迭代 5A~5E
4. **阶段三**：手动 `ShowFortune()` 三态演示；语音联调留待阶段五

**预估周期**：阶段二至五约 5–6 周（不含阶段六可选项）

---

## 附录 A：Target1 显示设计参考（【未实现 · 不实施】）

> 以下为早期 Target1 视觉方案，自迭代 18–20 起已从代码中移除。**阶段二/三不再恢复环上八卦**；鱼眼与结果卡按 Target2 实施，本附录仅供历史对照。

### A.1 扩展 UI 元素

- **8 八卦名大字** (r=86, 48×48 label，45s/圈旋转)
- **8 卦象符号** (r=122, 36×24 canvas)
- **4 方位大字** (r=150, 48×48，固定) — 现改为 6×6 圆点 (r=72)
- **鱼眼图标** ×2：WiFi (162,126) / BLE (162,198)，36×36，伪旋转（位置固定）
- **运势结果卡**：200×240 胶囊形，7 行内容，Result 态显示

### A.2 Target1 状态机（运势）

```
Idle → Animating(3s) → Result(30s, 可触摸关闭) → Idle
```

### A.3 已废弃设计决策

- ❌ 8 个常驻功能图标：密度过高，改为结果卡内文字标识
- ❌ 尺寸放大高亮：与旋转位置同步冲突，改为纯颜色脉冲
- ❌ 太极 Animating 加速：改为太极保持 30s/圈
- ❌ 鱼眼随太极旋转：改为伪旋转（位置固定）

---

*文档版本: v1.6*
*更新日期: 2026-06-15*
*修订说明: v1.6 对齐 Target2 运势规格、修正外设/音频表述、阶段↔迭代对照、链至验收清单*
*适用硬件: Waveshare ESP32-S3-Touch-LCD-1.85B (ESP32-S3R8, 8MB PSRAM, 16MB Flash, 360×360 QSPI LCD)*
