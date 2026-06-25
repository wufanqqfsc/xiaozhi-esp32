# 小智 ESP32 故障排查

> 独立维护的故障排查手册，与 [CODE_WIKI.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/CODE_WIKI.md) 解耦。
> 适用版本：`PROJECT_VER "2.2.6"`（Waveshare ESP32-S3 Touch LCD 1.85B 派生固件）。

---

## 目录

- [18.1 烧录后 LCD 黑屏，USB 设备从系统消失](#181-烧录后-lcd-黑屏usb-设备从系统消失)
- [18.2 烧录后 `attitude.*` MCP 工具缺失或 AttitudeDisplay 未生效](#182-烧录后-attitude-mcp-工具缺失或-attitudedisplay-未生效)
- [18.3 截图服务不响应 `SNAP` / `CLICK:<n>` 命令](#183-截图服务不响应-snap--clickn-命令)
- [18.4 启动报 `LcdDisplay: SetChatMessage/SetEmotion/SetStatus failed: ... is nullptr`](#184-启动报-lcddisplay-setchatmessagesetemotionsetstatus-failed--is-nullptr)
- [18.5 编译成功但 `merged-binary.bin` 大小与上一次几乎相同](#185-编译成功但-merged-binarybin-大小与上一次几乎相同)

---

## 18.1 烧录后 LCD 黑屏，USB 设备从系统消失

**典型症状**：
- 烧录 1.85B 固件后，设备 LCD 全黑，旋转 / 触摸均无反应
- macOS / Linux 设备列表短暂识别 `/dev/cu.usbmodem*` 后又消失
- 串口抓不到任何 `Board: ... SKU=...` 启动日志

**根因**：**board 类型与硬件不匹配**——`sdkconfig` 实际启用的板型不是当前硬件。例如 `sdkconfig` 启用 `CONFIG_BOARD_TYPE_BREAD_COMPACT_WIFI=y`，但实际连接的是 Waveshare 1.85B（ST77916 LCD）。

错误日志典型片段：
```
I (217) Board: UUID=... SKU=bread-compact-wifi
I (227) CompactWifiBoard: Install SSD1306 driver
E (227) lcd_panel.io.i2c: panel_io_i2c_tx_buffer(193): i2c transaction failed
E (237) lcd_panel.ssd1306: panel_ssd1306_init(151): io tx param SSD1306_CMD_SET_MULTIPLEX failed
E (247) CompactWifiBoard: Failed to initialize display
```

**根因 #1（最常见）**：`build_and_flash.sh` 用 `idf.py -DBOARD_TYPE_WAVESHARE_ESP32_S3_TOUCH_LCD_1_85B=y` 传参，**该参数是 CMake 变量，不是 Kconfig 选项**，被 Kconfig 体系忽略；实际编译目标由 `sdkconfig` 决定。

**根因 #2**：手动编辑过 `sdkconfig.defaults*` 或 `main/Kconfig.projbuild`，残留了旧 `CONFIG_BOARD_TYPE_*=y`。

**根因 #3**：编译链切换 / 多 board 共享源码树时 `sdkconfig` 没切干净。

**修复步骤**：

1. 确认当前 `sdkconfig` 启用的是哪个板：
   ```bash
   grep -E "^CONFIG_BOARD_TYPE_[A-Z0-9_]+=y" sdkconfig
   # 应该只输出 1 行，且匹配实际硬件
   ```

2. 切换到 1.85B（路径 → Kconfig 名转换规则见 [§14.2.1](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/CODE_WIKI.md#1421-板型切换原理重要-d-参数无效)）：
   ```bash
   sed -i.bak -E "s/^(CONFIG_BOARD_TYPE_[A-Z0-9_]+)=y$/# \1 is not set/" sdkconfig
   sed -i.bak -E "s/^# (CONFIG_BOARD_TYPE_WAVESHARE_ESP32_S3_TOUCH_LCD_1_85B) is not set$/\1=y/" sdkconfig
   grep -E "^CONFIG_BOARD_TYPE" sdkconfig
   ```

3. 完全清理并重新编译：
   ```bash
   rm -rf build
   ./build_and_flash.sh build
   ```

4. 用 `esptool.py` 烧录（不依赖 `idf.py flash` 的 reset 时序）：
   ```bash
   esptool.py --chip esp32s3 -p /dev/cu.usbmodem1101 -b 460800 \
     --before default_reset --after hard_reset write_flash --flash_mode dio \
     --flash_size 16MB --flash_freq 80m 0x0 build/merged-binary.bin
   ```

5. 验证启动日志应出现：
   ```
   I (217) Board: UUID=... SKU=esp32-s3-touch-lcd-1.85b
   I (227) waveshare_lcd_1_85: ...
   I (...) BoxAudioCodec: Duplex channels created
   I (...) AttitudeDisplay: WiFi fisheye status -> 0
   ```

**预防**：
- 修改 `build_and_flash.sh` 改用 `sed` 切换 `sdkconfig`（已在 [§14.2.1](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/CODE_WIKI.md#1421-板型切换原理重要-d-参数无效) 中实现）；
- 编译前用 `head -c 0 sdkconfig && grep -E '^CONFIG_BOARD_TYPE' sdkconfig` 自检；
- 多板共用源码树时为每个板维护独立的 `sdkconfig.<board>` 文件，通过 `idf.py -DSDKCONFIG_DEFAULTS=sdkconfig.<board>` 切换。

---

## 18.2 烧录后 `attitude.*` MCP 工具缺失或 AttitudeDisplay 未生效

**症状**：MCP 工具列表只看到 `self.get_device_status`、`self.audio_speaker.set_volume` 等基类工具，没有 `self.attitude.taiji_rotate_cw` / `self.attitude.set_wifi_fisheye`。

**根因**：板型不是 1.85B，或 1.85B 但未启用 AttitudeDisplay 板型抽象。

**修复**：
- 确认 `sdkconfig` 启用了 1.85B（见 [§18.1](#181-烧录后-lcd-黑屏usb-设备从系统消失)）；
- 确认 [main/boards/waveshare/esp32-s3-touch-lcd-1.85b/waveshare_esp32_s3_touch_lcd_1_85b.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/boards/waveshare/esp32-s3-touch-lcd-1.85b/waveshare_esp32_s3_touch_lcd_1_85b.cc) 中创建了 `AttitudeDisplay` 而非 `SpiLcdDisplay` / `LcdDisplay`：
  ```bash
  grep "new AttitudeDisplay\|new SpiLcdDisplay" main/boards/waveshare/esp32-s3-touch-lcd-1.85b/*.cc
  ```

---

## 18.3 截图服务不响应 `SNAP` / `CLICK:<n>` 命令

**症状**：`python3 tools/screenshot_with_log.py --snap` 报 `Screenshot markers not found` 或 `No SCREENSHOT_START received`。

**根因**：截图服务默认编译期关闭（`XIAOZHI_ENABLE_BOOT_SCREENSHOT=0`），未注册 UART 命令回调。

**修复**：
1. 编译期打开：
   ```bash
   ./build_and_flash.sh clean
   idf.py -DXIAOZHI_ENABLE_BOOT_SCREENSHOT=1 build
   ```
   > 注：截图服务的开关是 `main/main.cc` 顶部的宏定义，`-DXIAOZHI_ENABLE_BOOT_SCREENSHOT=1` 是 CMake `add_definitions` 形式，可以被传递（这是 Kconfig 体系之外的 `add_definitions`，与 [§18.1](#181-烧录后-lcd-黑屏usb-设备从系统消失) 的 `-D` 区别）。
2. 重新烧录后，`SNAP` / `CLICK:<n>` 命令才会被识别。

---

## 18.4 启动报 `LcdDisplay: SetChatMessage/SetEmotion/SetStatus failed: ... is nullptr`

**症状**：启动后日志中反复出现：
```
W (...) Display: SetChatMessage('system', '...') failed: chat_message_label_ is nullptr
W (...) Display: SetEmotion('gear') failed: emoji_image_ is nullptr
W (...) Display: Status('配网模式') failed: status_label_ is nullptr
W (...) Display: ShowNotification(...) failed: notification_label_ is nullptr
```

**说明**：这是**预期行为**，**不是错误**。AI 罗盘（AttitudeDisplay）用八卦 / 太极 / 鱼眼 UI 取代了基类 `LcdDisplay` 的「表情 + 聊天消息 + 状态栏 + 通知」UI。这些基类方法被调用时检测到对应 label 还未创建，因此输出 warning，但**不影响主功能**。系统会继续正常运行（WiFi 配网、OTA、语音等都正常）。

如果完全不想看到这些 warning，需要在 [attitude_display.cc](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc) 中显式重写这些方法（提供空实现），但通常没必要。

---

## 18.5 编译成功但 `merged-binary.bin` 大小与上一次几乎相同

**症状**：切换板型后 `ls -lh build/merged-binary.bin` 体积没变化（典型 8.5 MB → 仍是 8.5 MB）。

**根因**：`build/` 缓存未清理，CMake 复用上次结果。

**修复**：
```bash
rm -rf build
./build_and_flash.sh build
```
1.85B 板（含 AttitudeDisplay / LVGL / 太极组件）正常 `merged-binary.bin` 约 **11 MB**；面包板（OLED 128x32）约 **8.5 MB**。可作为板型是否正确生效的快速参考。
