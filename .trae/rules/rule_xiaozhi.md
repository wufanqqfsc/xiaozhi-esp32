# Xiaozhi ESP32 开发规则

> **项目**: Xiaozhi ESP32 AI 罗盘项目  
> **版本**: v1.2  
> **更新日期**: 2026-06-26

---

## 概述

本文档定义了 AI 罗盘项目的开发流程和规范，确保每次功能迭代都能正确编译、烧录和验证。

## 开发流程规则

### 1. 功能迭代流程

每完成一次功能迭代都需要遵循以下流程：

1. **编译和烧录** - 确保新的功能正常运行
2. **功能验证** - 真机测试所有功能是否正常
3. **人工确认** - 直到所有功能都正常运行，待人工确认之后再进行下一步迭代
4. **状态更新** - 每个功能迭代完成之后要及时更新最新状态，包括功能描述、代码注释等

### 2. 问题处理

如果在开发过程中发现新的问题或错误：

1. **及时修复** - 发现新的问题或错误，要及时修复
2. **更新状态** - 修复完成后及时更新状态文档
3. **验证确认** - 修复后重新编译烧录验证

### 3. 编译和烧录规范

> **重要**: 所有的编译和烧录都需要使用项目中固定的 `build_and_flash.sh` 脚本进行，不能使用其他方式。

#### 为什么必须使用 `build_and_flash.sh`？

- **错误信息完整性** - 不要使用 `2>&1` 重定向，否则会导致编译和烧录过程中输出的错误信息被忽略
- **环境一致性** - 使用固定脚本确保编译环境一致
- **流程标准化** - 统一的编译烧录流程减少出错概率

#### 使用方式

```bash
# 在项目根目录下执行
./build_and_flash.sh
```

或者根据脚本内容指定参数：

```bash
./build_and_flash.sh <board_type> <port>
```

> **注意**: 如果脚本不存在或有问题，请先检查脚本内容或创建符合项目需求的编译烧录脚本。

### 4. 服务器地址同步规则

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

### 5. WiFi 凭据持久化（已实现）

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
