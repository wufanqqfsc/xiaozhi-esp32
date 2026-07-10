# Xiaozhi ESP32 开发规则

> **项目**: Xiaozhi ESP32 AI 罗盘项目  
> **版本**: v1.6  
> **更新日期**: 2026-07-10

---

## 概述

本文档定义了 AI 罗盘项目的开发流程和规范，确保每次功能迭代都能正确编译、烧录和验证。

### 日志目录

设备运行日志统一存放在项目根目录的 `log/` 文件夹下：

```
xiaozhi-esp32/
└── log/
    └── xiaozhi_boot_*.log    # 设备启动日志文件
```

日志通过 HTTP API 获取并自动保存到本地：
```bash
# 获取设备最新日志
curl -s http://<设备IP>:8080/api/device/logs

# 下载并保存到 log 目录（需手动保存）
curl -s http://<设备IP>:8080/api/device/logs > log/xiaozhi_boot_4.log
```

## 开发流程规则

### 1. 功能迭代流程

每完成一次功能迭代都需要遵循以下流程：

1. **性能优化检查** - 每次代码改动后、**编译之前**，必须评估并落实必要的性能优化（见 §「编译前性能优化」）
2. **编译和烧录** - 确保新的功能正常运行
3. **功能验证** - 真机测试所有功能是否正常
4. **人工确认** - 直到所有功能都正常运行，待人工确认之后再进行下一步迭代
5. **状态更新** - 每个功能迭代完成之后要及时更新最新状态，包括功能描述、代码注释等

### 2. 问题处理

如果在开发过程中发现新的问题或错误：

1. **及时修复** - 发现新的问题或错误，要及时修复
2. **更新状态** - 修复完成后及时更新状态文档
3. **验证确认** - 修复后重新编译烧录验证

### 3. 编译前性能优化（v1.4 新增）

> **重要**: 每次代码改动后，在运行 `build_and_flash.sh` **之前**，必须先完成性能评估与必要优化。功能正确但性能回退的改动不得进入编译烧录。

#### 必须检查的维度

| 维度 | 关注点 | 常见优化 |
|------|--------|----------|
| **内存** | 堆/栈/PSRAM 分配、泄漏、碎片 | 大缓冲放 PSRAM；复用静态缓冲；避免 HTTP/LVGL 路径频繁 `malloc` |
| **CPU 热点** | 主循环、定时器、回调中的重计算 | 降频轮询；缓存结果；将重活移出 LVGL/音频/网络关键路径 |
| **任务与栈** | FreeRTOS 任务数、栈大小、优先级 | 避免无谓新任务；HTTP/截图等栈需求用 PSRAM；防止栈溢出与抢占抖动 |
| **I/O 与阻塞** | SD 卡、HTTP、串口、文件遍历 | 避免在 `httpd` handler 中长时间阻塞；大文件分块；SD 未挂载时快速返回 |
| **显示与音频** | LVGL 刷新、截图、TTS/音效 | LVGL 操作加锁且尽量短；异步截图；避免重复 `PlayUiSound` 叠加 |
| **网络与字符串** | JSON/HTML 拼接、日志输出 | 控制响应体大小；减少热路径 `ESP_LOG`；避免重复 `readdir`/`stat` |

#### 编译前自检清单（每次改动必过）

- [ ] 新增循环/定时器/回调是否必要？频率是否可再降低？
- [ ] 是否在热路径引入了动态分配、大块栈数组或同步 I/O？
- [ ] 是否影响 LVGL、语音唤醒、WebSocket 等实时链路？
- [ ] 若无法优化，是否在代码或提交说明中写明原因与风险？

#### 与编译烧录的关系

```
代码改动 → 性能优化检查（本节）→ build_and_flash.sh → 真机验证
```

未通过性能检查不得编译；若确需保留性能权衡，须在改动说明中记录。

### 4. 编译和烧录规范

> **🔴 强制要求**: 所有的编译和烧录 **必须** 使用项目根目录的 `build_and_flash.sh` 脚本，**禁止使用任何其他方式**。

#### 禁止的编译方式

| 禁止命令 | 原因 |
|---------|------|
| `idf.py build` | 绕过板型切换、资源打包等前置处理 |
| `idf.py flash` | 缺少自动重置、日志监控等完整流程 |
| `idf.py build flash monitor` | 未处理 sdkconfig 板型切换逻辑 |
| `cmake ... && make` | 环境变量、Python 虚拟环境未正确加载 |

#### 为什么必须使用 `build_and_flash.sh`？

1. **板型自动切换**：脚本会自动修改 sdkconfig 中的 `CONFIG_BOARD_TYPE_*`，确保编译正确的板型
2. **错误信息完整性**：不使用 `2>&1` 重定向，确保编译和烧录过程的错误信息完整输出
3. **环境一致性**：自动加载正确的 ESP-IDF 环境和 Python 虚拟环境
4. **流程标准化**：统一执行资源打包、编译、烧录、日志监控的完整流程
5. **烧录后验证**：自动监控启动日志，确认设备正常启动

#### 正确使用方式

```bash
# 在项目根目录下执行
cd /Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32
./build_and_flash.sh
```

指定板型和端口（可选）：
```bash
./build_and_flash.sh waveshare/esp32-s3-touch-lcd-1.85b /dev/cu.usbmodem1101
```

#### 违反规则的处理

如果发现使用了禁止的编译方式：
1. 立即停止当前操作
2. 执行 `idf.py fullclean` 清理构建产物
3. 使用 `./build_and_flash.sh` 重新编译烧录

### 5. 服务器地址同步规则

> **重要**: 每次烧录固件前，**必须**确认并更新 ESP32 固件中链接的服务器地址为 **当前 host 端（运行 `xiaozhi-esp32-server-java` 的 Mac）的 IP 地址**。

#### 为什么需要手动同步？

- **网络切换**：Mac 的 WiFi 可能从 `HUAWEI-9YQAVW` (192.168.3.x) 切换到 iPhone 热点 (10.x.x.x) 等其他网络
- **DHCP 重分配**：每次重连后 IP 地址可能变化
- **服务端口固定**：Java 后端监听 `8091`（HTTP OTA）和 `8092`（WebSocket）

#### 操作步骤

1. **获取当前 host IP**：
   ```bash
   ifconfig en0 | grep "inet " | awk '{print $2}'
   ```
   当前常见 IP：
   - HUAWEI-9YQAVW 网络 → `192.168.3.33`
   - iPhone 热点 (REDMI Turbo 4) → `10.161.227.170`

2. **更新 `sdkconfig`** 中两个变量（用上面查到的 IP 替换）：
   ```
   CONFIG_OTA_URL="http://<CURRENT_HOST_IP>:8091/api/device/ota"
   CONFIG_LOCAL_WEBSOCKET_URL="ws://<CURRENT_HOST_IP>:8092/ws/xiaozhi/v1/"
   ```

3. **重新编译并烧录**：
   ```bash
   ./build_and_flash.sh
   ```

#### 验证

烧录完成后，设备日志应显示：
```
I (xxxxx) Application: Network connected
I (xxxxx) Ota: Current version: 2.x.x
```
且 `Ota: Check update response` 中应出现 `activation` 字段（说明 OTA 检查成功连到了 8091）。

#### 当前 host IP 记录

| 网络名称 | 网段 | 当前 host IP |
|---------|------|------------|
| HUAWEI-9YQAVW | 192.168.3.x | 192.168.3.33 |
| REDMI Turbo 4 (热点) | 10.161.227.x | 10.161.227.170 |

> **注意**：每次烧录前用 `ifconfig en0` 重新确认 IP，不要依赖上一次的记录。

### 6. WiFi 凭据持久化（已实现）

ESP32 设备**已经支持**保存最近成功连接的 WiFi 名（SSID）和密码到 NVS Flash，并在下次启动时自动连接。仅当以下情况才进入配网模式：

1. **NVS 中无任何保存的 SSID**（首次开机或执行过 NVS 清除）
2. **保存的 SSID 连接失败**（WiFi 名改了、密码改了、AP 不在了等）
   - 触发机制：`OnWifiConnectTimeout` 超时回调 → `StartWifiConfigMode()`

#### 实现位置

- 凭据管理：`main/boards/m5stack-cardputer-adv/wifi_config_ui.cc:386` 调用 `SsidManager::AddSsid(ssid, password)` 同时保存 SSID 和密码
- 自动连接判断：`main/boards/common/wifi_board.cc:93-100` 检查 `ssid_manager.GetSsidList().empty()` 后选择 `StartStation()` 或 `StartWifiConfigMode()`

#### 何时需要重新配网？

| 场景 | 是否需要重新配网 | 说明 |
|------|---------------|------|
| 换到同一 WiFi 的不同位置 | 否 | 自动重连 |
| 换到已保存的不同 WiFi | 否 | 自动尝试该 WiFi |
| 新增一个之前没连过的 WiFi | **是** | NVS 没保存，必须配网 |
| 现有 WiFi 改了密码 | **是** | 旧密码连接失败，会自动进配网 |
| 固件更新后 | 否 | NVS 数据保留 |
| 改动了 partition table 重新烧录 | **是** | 改分区表会清 NVS（参考 rules v1.1） |
| 主动清除 NVS | **是** | 通过 `/api/device/clear-nvs` 或长按 BOOT 等 |

#### 如何强制进入配网模式？

1. **HTTP API**（如果设备在线）：
   ```bash
   curl -X POST http://<device_ip>:8080/api/device/clear-nvs
   ```
   这会清除 NVS 全部配置（WiFi + OTA_URL + 设备ID），下次启动自动进配网。

2. **物理操作**：在 `kDeviceStateStarting` 状态下单击 BOOT 键（参考 board.cc 按钮处理）。

---

## 问题排查流程（v1.3 新增）

### 标准排查步骤

当设备出现连接、OTA、唤醒等端到端问题时，**必须按照以下步骤依次排查**：

#### 步骤 1: 性能优化检查（编译前）

对照 §「编译前性能优化」完成自检；必要时先改代码再进入下一步。

#### 步骤 2: 重新编译烧录固件

```bash
cd /Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32
./build_and_flash.sh all
```

**验证**：设备应自动重启，HTTP 服务可用。

#### 步骤 3: 检查 HTTP 设备日志

```bash
# 等待设备启动后检查状态
curl -s http://<设备IP>:8080/api/device/status

# 获取设备日志（关键！）
curl -s http://<设备IP>:8080/api/device/logs | tail -100
```

**重点关注**：
- WiFi 连接状态（是否连接到正确的 SSID）
- OTA 检查结果（是否有 `activation` 或 `websocket` 字段）
- 错误信息（如 `Connection reset`、`timeout` 等）

#### 步骤 4: 检查服务器端日志

```bash
# 服务器状态
cd /Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32-server-java
./start.sh status

# OTA 请求日志
grep -E "ota|OTA|activation|192.168" logs/xiaozhi-server.log | tail -30

# WebSocket 连接日志
grep -E "websocket|WebSocket|session|hello" logs/xiaozhi-dialogue.log | tail -30
```

**重点关注**：
- 设备 MAC 地址（用于匹配设备）
- OTA 端点请求（`POST /api/device/ota`）
- WebSocket 连接建立
- 任何错误或异常

#### 步骤 5: 分析并修复问题

根据日志分析根因，常见问题及修复：

| 问题现象 | 根因 | 修复方法 |
|---------|------|---------|
| WiFi 连接失败 | NVS 保存了旧 SSID | 清除 NVS 并重新配网 |
| OTA 检查失败 | 服务器地址配置错误 | 更新 sdkconfig 中的 OTA_URL |
| HTTP 服务超时 | SD 卡未挂载时请求卡死 | 修复 sdcard_log_http.cc 提前返回 |
| 唤醒无响应 | 设备未完成激活 | 检查服务器设备状态（state=2） |

#### 步骤 6: 闭环验证修复结果

修复后必须验证：
1. 设备成功连接 WiFi（正确的 SSID）
2. OTA 检查成功（收到 `websocket` 配置）
3. WebSocket 连接建立
4. 语音唤醒正常响应

### 常用诊断命令

```bash
# 1. 获取设备状态
curl http://<设备IP>:8080/api/device/status

# 2. 获取设备日志
curl http://<设备IP>:8080/api/device/logs

# 3. 检查 OTA URL 配置
curl http://<设备IP>:8080/api/device/ota-url

# 4. 清除 NVS 重新配网
curl -X POST http://<设备IP>:8080/api/device/clear-nvs

# 5. 重启设备
curl -X POST http://<设备IP>:8080/api/device/reboot

# 6. 检查服务器设备列表
docker exec xiaozhi-mysql mysql -uroot -pabc123456 xiaozhi -e "SELECT deviceId, state, ip, wifiName FROM sys_device;"

# 7. 测试 OTA 端点
curl -X POST "http://<服务器IP>:8091/api/device/ota" \
  -H "Device-Id: <MAC地址>" \
  -H "Content-Type: application/json" \
  -d '{"ip":"<设备IP>","chipModelName":"esp32s3","application":{"version":"2.2.6"}}'
```

### 端到端数据流

```
┌─────────────────┐                    ┌─────────────────────────────────────┐
│   ESP32 设备    │                    │        Java 后端服务器              │
│                 │  1. OTA 检查      │  8091: xiaozhi-server              │
│  Wi-Fi 已连接   │ ───────────────►  │  - 验证 deviceId                   │
│  HTTP:8080 正常 │  POST /api/device/ota  │  - 返回 activation/websocket      │
│                 │                    │                                    │
│                 │  2. 设备状态上报    │                                    │
│                 │ ◄───────────────  │  - 更新 sys_device 表              │
│                 │  HTTP POST         │                                    │
│                 │                    │                                    │
│                 │  3. WebSocket     │  8092: xiaozhi-dialogue            │
│                 │ ◄───────────────  │  - 建立语音对话会话                  │
│                 │  ws://.../v1/     │  - 启用唤醒检测                     │
│                 │                    │                                    │
│                 │  4. 唤醒响应       │                                    │
│                 │ ◄───────────────  │  - STT → LLM → TTS                │
│  播放 TTS 音频  │  Opus 流          │                                    │
└─────────────────┘                    └─────────────────────────────────────┘
```

---

## 代码规范

### 1. 功能描述

每个功能的代码应该包含以下描述：

- **功能名称** - 清晰的功能名称
- **功能说明** - 功能的详细说明
- **参数说明** - 输入输出参数的描述
- **使用示例** - 代码使用示例

### 2. 代码注释

- **关键逻辑** - 必须注释复杂的业务逻辑
- **参数含义** - 重要的参数需要有注释说明
- **TODO/FIXME** - 使用 TODO 标记待完成的功能，FIXME 标记需要修复的问题

### 3. 文档更新

每次功能迭代完成后，需要更新以下文档：

- **README.md** - 项目说明文档
- **产品功能与技术实现方案** - 详细的功能和技术文档
- **代码注释** - 代码中的注释和说明
