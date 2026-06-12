# AI 姿态平衡仪 — 详细迭代验收标准

**文档版本**: v1.1  
**生成日期**: 2026年6月  
**适用平台**: Waveshare ESP32-S3-Touch-LCD-1.85B (360×360 圆形 LCD)  
**代码位置**: `main/display/attitude_display.{h,cc}`  
**Board 集成**: `main/boards/waveshare/esp32-s3-touch-lcd-1.85b/esp32-s3-touch-lcd-1.85b.cc`（第 330 行 `new AttitudeDisplay(...)`）  
**编译配置**: `main/CMakeLists.txt`（第 19 行已包含 `display/attitude_display.cc`）  
**烧录与运行验证**: `./build_and_flash.sh`（见文件根目录脚本）

---

## 迭代优先级追踪

| 迭代 | 状态 | 完成日期 | 验收人 | 备注 |
|------|------|----------|--------|------|
| 迭代1: 项目基础框架 | ✅ 已完成 | 2026-06-10 | - | AttitudeDisplay基础框架，显示测试文字 |
| 迭代2~11 | 🔄 待开始 | - | - | UI迭代将在截图功能验证后继续 |
| **迭代12: 串口截图功能** | 🔴 **下一个迭代** | - | - | **优先级最高：便于后续UI迭代的调试验证** |

---

## 文档说明

本文件为 **每个迭代阶段** 的 **验收标准（Acceptance Criteria）**。  
每一条验收标准都包含：

- **完成状态追踪**: 在真机上逐项确认，通过即打勾
- **操作步骤**: 明确如何验证（编译 / 串口日志 / 屏幕观察 / 交互操作）
- **预期结果**: 可量化的、可直接观察的输出
- **判定失败条件**: 明确哪些现象代表迭代未通过
- **回归风险**: 记录本迭代容易回归的点，便于下一轮检查

> **项目规则**：每完成一次功能迭代，**必须** 运行 `./build_and_flash.sh` 进行编译、烧录和真机功能测试，人工确认当前验收条目全部通过后，再进入下一个迭代。

---

## 0. 通用前置条件

在每个迭代开始前，都应该满足以下前置环境检查：

| 编号 | 检查项 | 验证方法 | 预期结果 |
|------|--------|----------|----------|
| PRE-1 | ESP-IDF 工具链可用 | 终端执行 `idf.py --version` | 正常输出版本号（如 `ESP-IDF v5.x`） |
| PRE-2 | 开发板连接正常 | `ls /dev/cu.*` | 能看到类似 `/dev/cu.usbserial-*` 或 `/dev/cu.usbmodem*` 的设备 |
| PRE-3 | 项目可完整编译 | `./build_and_flash.sh build` | 退出码 0，输出 `Build complete` |
| PRE-4 | 串口日志可读 | 编译烧录后运行 `idf.py monitor` 或 `screen /dev/cu.usbserial-* 115200` | 看到 FreeRTOS 启动日志及 `AttitudeDisplay` 相关信息 |
| PRE-5 | 系统日志过滤 | 串口监视器中输入 `log level I *AttitudeDisplay*` | 能看到 `AttitudeDisplay constructed` / `SetupUI completed` 等日志 |

> 任何 PRE-* 失败都属于**环境问题**，必须先解决才能进入验收。

---

## 迭代 1: 基础框架与可编译运行

**目标**: `AttitudeDisplay` 类能被 Board 正确实例化并进入 `SetupUI()`，屏幕至少显示一行文字证明"跑起来了"。

### 已实现的代码功能（参考）

| 组件 | 当前状态 | 关键实现点 |
|------|----------|------------|
| `AttitudeDisplay::AttitudeDisplay(...)` | ✅ 已实现 | 继承 `SpiLcdDisplay`，调用父构造函数并打印日志 |
| `AttitudeDisplay::SetupUI()` | ✅ 已实现 | 创建 `attitude_container_` 并调用 `CreateTestLayout()` |
| `CreateTestLayout()` | ✅ 已实现 | 居中显示 "Attitude Display Init OK" 及屏幕尺寸 |
| `SetAttitudeData()` | 🚧 占位 | 仅记录内部成员 `current_pitch_` 等，无 UI 更新 |
| `SetInterpretation()` | 🚧 占位 | 仅打日志 `ESP_LOGD` |
| `SetTheme()` | 🚧 简化实现 | 调用 `Display::SetTheme()`，`ApplyThemeToAttitudeUI()` 空实现 |
| Board `CreateDisplay()` 集成 | ✅ 已实现 | `display_ = new AttitudeDisplay(panel_io, panel, ...)` |

### 验收标准

| 编号 | 验收项 | 操作步骤 | 预期结果 | 失败判定 | 回归风险 |
|------|--------|----------|----------|----------|----------|
| I1-1 | 代码可完整编译 | `./build_and_flash.sh build` | 退出码 0，无 `error:` 行；`attitude_display.cc` 正常被编译 | 有任何编译错误（包括 `-Werror` 警告提升） | 新增成员变量 / 方法签名改动会破坏继承链 |
| I1-2 | 固件可成功烧录到开发板 | `./build_and_flash.sh`（含 monitor） | 输出 `Writing at 0x... (100 %)`，随后板子重启 | 串口提示 `No device found` 或烧录中途断开 | USB 线接触不良；IDE 占用串口 |
| I1-3 | 屏幕点亮并显示测试文字 | 观察开发板圆形屏幕 | 深蓝背景（`#0d1b2a`），中央显示绿字 "Attitude Display Init OK"，下方显示浅灰 "Screen: 360x360" | 屏幕全黑 / 白屏 / 花屏 / 无任何文字 | LVGL 初始化失败；`lv_port_add_disp()` 异常 |
| I1-4 | 串口日志完整 | 串口监视器中搜索 `AttitudeDisplay` | 至少看到 3 行：<br>1. `AttitudeDisplay constructed, 360x360`<br>2. `SetupUI completed`<br>3. `Test layout created, label=0x...` | 缺少任何一行；或出现 `abort()` / `Guru Meditation` | 父类 `SpiLcdDisplay` 初始化路径改变 |
| I1-5 | `SetupUI()` 不被重复调用 | 重启板子连续观察 30 秒 | 只出现一次 `SetupUI completed` 日志 | 日志重复出现 2 次以上 | 主循环误调用 `SetupUI()`；没有 `IsSetupUICalled()` 保护 |
| I1-6 | 屏幕无闪烁 / 无异常抖动 | 观察屏幕 30 秒 | 画面稳定，不出现文字跳动或闪烁 | 屏幕周期性闪黑；或文字位置偶尔跳变 | LVGL `timer_handler` 与刷新速率不匹配 |
| I1-7 | 系统稳定运行 60 秒无崩溃 | `idf.py monitor` 观察 60 秒 | 无 `Guru Meditation` / `abort()` / `Stack canary` 错误；无 watchdog 重启 | 出现任何异常重启日志 | 内存泄漏；栈溢出；未加锁访问 LVGL 对象 |
| I1-8 | 不破坏 LcdDisplay 对其他板的支持 | （若有条件）切换到其他板编译 | `LcdDisplay` / `SpiLcdDisplay` 基类未被破坏 | 基类修改导致其他项目编译失败 | 修改 `display.h` / `lcd_display.h` 公共接口时需谨慎 |

> **迭代 1 完成判定**: I1-1 ~ I1-7 **全部通过**，I1-8 至少通过本地编译。

---

## 迭代 2: 圆形背景 + 基础 UI 布局

**目标**: 屏幕显示完整的圆形布局（径向渐变背景 / 3圈装饰圆 / 顶部信息栏 / 底部解读区域），但还没有真正的气泡动态效果。

### 需要在 `attitude_display.cc` 新增的方法和成员

| 方法 / 成员 | 职责 |
|-------------|------|
| `CreateBackground()` | 创建 3 层背景圆：深色底 → 中层 → 中心高光 |
| `CreateDecorationCircles()` | 创建 ring_outer_ / ring_mid_ / ring_inner_ 3圈金色装饰圆 + N/E/S/W 方向文字 |
| `CreateTopInfoRing()` | 创建顶部容器 + network_icon_ + time_label_ + battery_icon_ |
| `CreateBottomInterpretation()` | 创建底部圆角矩形 + 状态图标 + 解读文字 |
| `UpdateStatusBar(bool) override` | 定时刷新网络 / 时间 / 电量图标 |

### 验收标准

| 编号 | 验收项 | 操作步骤 | 预期结果 | 失败判定 | 回归风险 |
|------|--------|----------|----------|----------|----------|
| I2-1 | 径向渐变背景 | 观察屏幕 | 中心 `#0d1b2a` 深蓝 → 外缘 `#050a14` 近黑；呈圆形渐变 | 纯色背景；或方块色块边缘硬过渡 | 绘制顺序 / `z-order` 错误 |
| I2-2 | 3圈装饰圆完整可见 | 观察屏幕 | 半径约 160 / 140 / 120 像素；金色（`#ffd700`）；外粗内细、外亮内暗；无截断 | 任何一圈被裁掉；或 3 圈重合 | `DISPLAY_WIDTH/HEIGHT` 不是 360；圆心不是 (180, 180) |
| I2-3 | 方向标记 N / E / S / W | 观察屏幕 4 个正方向 | 正上方 "N"、正右 "E"、正下 "S"、正左 "W"，金色文字 | 缺少任何一个；或位置偏离 ≥ 10 像素 | 圆形半径计算错误；字体尺寸过大溢出 |
| I2-4 | 顶部信息栏 | 观察顶端 | 左: 网络图标（或空）；中: 时间 HH:MM；右: 电量图标（或空） | 三项中有任何一项不可见 / 重叠 / 被屏幕边缘截断 | `lv_obj_set_pos` 坐标超出圆形可见区域 |
| I2-5 | `UpdateStatusBar()` 能更新时间 | 等待系统时间校准后（或手动 `sntp` 成功后）观察 2 分钟 | 时间从 `00:00` 或初始值变为正确的当前时间，并每秒刷新 | 时间一直不变；或刷新后乱码 | `snprintf` / `strftime` 格式错误；未获得网络时间 |
| I2-6 | 电量 / 网络图标正常显示 | 系统联网 + 有电量后观察顶端 | 网络图标显示 Wi-Fi 信号；电量图标显示当前电量等级 | 图标为空白或显示问号 | `Board::GetNetworkStateIcon()` / `Board::GetBatteryLevel()` 返回 null；字体宏未正确声明 |
| I2-7 | 底部解读区域 | 观察屏幕底部 | 显示圆角矩形背景（宽度约 300~320 px），左有绿色小圆点，右有文字 "当前状态：基本平衡..." | 完全不显示；或被屏幕边缘截断；或文字溢出容器 | `lv_label_set_long_mode` 未设置；y 坐标 > 340 超出可见区 |
| I2-8 | 整体布局无重叠 | 距离拍照观察 | 顶部 / 中部 / 底部 3 块彼此分离，无元素压在另一个元素上 | 任何一处重叠；或某一区域文字与装饰圆混在一起 | 创建顺序（z-order）错误；或坐标计算太近 |
| I2-9 | 系统 2 分钟运行稳定 | 串口 monitor 观察 2 分钟 | 无 crash；内存 heap 在同一水平（无持续下降） | heap 持续下降表示泄漏；或 2 分钟内出现重启 | `UpdateStatusBar` 未加 `DisplayLockGuard`；频繁分配 lv_obj 不释放 |
| I2-10 | 迭代 1 功能不退化 | 重启观察 | I1-3、I1-4 测试文字依然可切换为 "圆形布局版本"（测试布局可删除或隐藏） | 测试文字消失且没有新布局 — 证明新代码未被调用 | `SetupUI()` 中忘记调用新方法 |

> **迭代 2 完成判定**: I2-1 ~ I2-10 全部通过，且 UI 在真机屏幕上视觉比例协调、无截断。

---

## 迭代 3: 水平仪气泡组件（核心 UI）

**目标**: 屏幕中央显示气泡 + 十字准星 + 刻度，具备 **平滑移动动画** 和 **随倾斜改变颜色** 的能力。

### 需要新增的方法和成员

| 方法 / 成员 | 职责 |
|-------------|------|
| `bubble_h_axis_`, `bubble_v_axis_`, `bubble_center_marker_`, `bubble_obj_` | 刻度线 / 准星 / 气泡主体 |
| `bubble_glow_outer_`, `bubble_inner_glow_` | 2 层呼吸光晕（透明度周期变化） |
| `CreateBubbleAndCrosshair()` | 一次性创建所有刻度和气泡对象 |
| `SetBubbleOffset(x, y)` | 设置目标偏移，触发 lv_anim 动画 |
| `SetBubbleLevel(level)` | 根据 0~4 等级改变气泡颜色 |
| `TickBubbleAnimation()` | 每 30~50ms 被 `esp_timer` 调用一次，处理呼吸和数据更新 |

### 验收标准

| 编号 | 验收项 | 操作步骤 | 预期结果 | 失败判定 | 回归风险 |
|------|--------|----------|----------|----------|----------|
| I3-1 | 十字准星居中且比例正确 | 观察屏幕中心 | 水平 + 垂直两条金线各 100 px 长，中心 6 px 金色小圆 | 线长短 < 80 或 > 120 px；中心偏位 ≥ 5 px | `lv_obj_set_pos` 偏移错误 |
| I3-2 | 方向标记 N / E / S / W 与十字线对齐 | 观察 | N 在正上方水平线外延长线上，W 在正左方垂直线外延长线上，依此类推 | 与十字延伸线偏差 ≥ 15 像素 | 坐标系 / 旋转逻辑不一致 |
| I3-3 | 气泡在中心默认位置 | 静止放置（或 mock pitch=roll=0） | 气泡圆形（30 px 直径）停在准星中心 | 气泡不在中心 | `SetAttitudeData` 被初始值错误调用 |
| I3-4 | 气泡颜色等级正确 | 模拟 SetBubbleLevel(0/1/2/3/4) | 0 级翠绿 `#00ff88` → 1 浅绿 → 2 金黄 → 3 橙红 → 4 红色 `#ff3344` | 颜色不是 5 档；或等级变化无视觉差异 | 颜色宏 / HEX 值拼写错误 |
| I3-5 | 气泡位置平滑移动 | 手动 `SetBubbleOffset(+40, +40)` → `(0, 0)` | 气泡从目标点 **平滑** 移动，无瞬移；动画时长 ≈ 200~500 ms | 瞬移；或动画有明显卡顿 | LVGL 动画 API 误用（`lv_anim_start` 返回 null） |
| I3-6 | 气泡移动范围被正确限制 | 手动 `SetBubbleOffset(+200, +200)`（远超设计值 ±60） | 气泡最终停在最外圈边界（距中心 ≈ 60 px），不会飞出装饰圆 | 气泡跑到屏幕外 | `clamp` 范围逻辑缺失；或坐标系单位错误 |
| I3-7 | 呼吸光晕效果 | 观察气泡 3~5 秒 | 外层光晕透明度有周期变化（约 3~5 秒一个完整呼吸周期） | 无呼吸；或呼吸周期 < 1 秒导致闪烁 | `esp_timer` 回调频率过高；`lv_anim_set_time` 过小 |
| I3-8 | 动画定时器性能 | 观察 30 秒串口日志，记录 `heap_caps_get_free_size(MALLOC_CAP_8BIT)` 差值 | heap 变化 ≤ 4 KB；无 crash；动画流畅 | heap 持续下降；动画帧率 < 20 FPS | 每帧都 `lv_obj_create`；未复用 lv 对象 |
| I3-9 | 动画线程安全 | 故意在另一个任务（如 button 回调）调用 `SetBubbleOffset` | 不会 crash；屏幕仍正常刷新 | `Guru Meditation` 或 `assert failed: lv_obj...` | 未使用 `DisplayLockGuard` / `lvgl_port_lock` 包裹 |
| I3-10 | 保留迭代 2 背景与状态栏 | 全图观察 | 背景、装饰圆、顶部时间、底部解读区域完整保留，无被气泡覆盖或破坏 | 背景消失 / 文字消失 | `z-order` / 父对象（`attitude_container_`）设置错误 |

> **迭代 3 完成判定**: I3-1 ~ I3-10 全部通过。特别注意 **I3-5（平滑移动）** 和 **I3-8（内存与帧率）**。

---

## 迭代 4: 姿态数据卡片

**目标**: 在气泡下方增加 3 张卡片，实时显示 Pitch / Roll / Yaw 的数值与角度条。

### 需要新增的方法和成员

| 方法 / 成员 | 职责 |
|-------------|------|
| `card_pitch_`, `label_pitch_val_`, `bar_pitch_` | Pitch 卡片 / 文字 / 进度条 |
| `card_roll_`, `label_roll_val_`, `bar_roll_` | Roll 卡片 / 文字 / 进度条 |
| `card_yaw_`, `label_yaw_val_`, `bar_yaw_` | Yaw 卡片 / 文字 / 进度条 |
| `CreateDataCards()` | 创建 3 张卡片，水平排列，总宽 ≤ 320 px |
| `UpdateDataCards(pitch, roll, yaw)` | 每帧刷新文字与角度条位置 |

### 验收标准

| 编号 | 验收项 | 操作步骤 | 预期结果 | 失败判定 | 回归风险 |
|------|--------|----------|----------|----------|----------|
| I4-1 | 3 张卡片水平均匀分布 | 观察屏幕下方（y ≈ 220 区域） | Pitch（左） / Roll（中） / Yaw（右），等距排列，每张宽度 ≈ 90 px | 任意两张重叠；或总宽 > 340 导致边缘截断 | `card_w × 3 + gap × 2 > 360` |
| I4-2 | 角度值动态更新 | 手动 `SetAttitudeData(15.5f, -7.3f, 90.0f)` | Pitch 显示 `+15.5°`，Roll 显示 `-7.3°`，Yaw 显示 `90.0°`（或类似格式化） | 显示 `nan` / `inf` / 乱码 | `snprintf` 格式符错误；数据为 NaN 未做保护 |
| I4-3 | 角度条随数值滑动 | Pitch=+30° 观察角度条 | 条指示点偏右；Pitch=-30° 偏左 | 条不随数值变化；或方向反了 | 归一化映射（-30~+30 → 0~width）错误 |
| I4-4 | 数值字体颜色等级一致 | Pitch=+20° / Roll=+20° 时 | 卡片数值文字颜色与气泡同为红色等级 | 数值颜色与气泡颜色不同 | 颜色变量未同步到卡片 |
| I4-5 | 数值刷新稳定不闪烁 | 每 50 ms 刷新一次，观察 10 秒 | 数值变化平滑，无闪烁 / 乱码瞬间 | 偶发文字抖动 | 频繁 `lv_label_set_text` 引起局部 repaint；应使用 `lv_label_set_text_fmt` |
| I4-6 | 卡片不被底部解读区域覆盖 | 观察卡片 y 坐标与底部区域的间距 | 卡片底部与解读容器顶部之间留 ≥ 10 px 间隙 | 重叠在一起 | 总 y 空间不足；需要调整气泡区域尺寸 |
| I4-7 | Yaw 条表示方向（而非偏差） | Yaw = 270° 时 | 条指示点偏向某一方向（如 "西"） | 条指示点位置不明 / 与理解相反 | Yaw 使用不同映射逻辑（0~360 → 线性或 4 相） |

> **迭代 4 完成判定**: I4-1 ~ I4-7 全部通过。

---

## 迭代 5: 主题系统扩展

**目标**: `AttitudeDisplay` 能正确响应 `SetTheme()` 调用，整套 UI 颜色 / 字体与项目主题系统一致。

### 需要实现或改进的方法

| 方法 | 预期行为 |
|------|----------|
| `SetTheme(Theme* theme) override` | 先调用 `Display::SetTheme(theme)`；再遍历所有元素应用主题 |
| `ApplyThemeToAttitudeUI()` | 对背景 / 装饰圆 / 气泡 / 卡片 / 解读区统一应用主题颜色 |
| （可选）自定义主题色表 | 定义 `kLevelColors[5]` 的色值通过主题可切换 |

### 验收标准

| 编号 | 验收项 | 操作步骤 | 预期结果 | 失败判定 | 回归风险 |
|------|--------|----------|----------|----------|----------|
| I5-1 | `SetTheme()` 不崩溃 | 启动时观察串口日志 | 无 crash；日志中能看到 `Theme set` 或类似 | `theme` 为 null 时直接 `->` 访问 crash | `static_cast<LvglTheme*>(theme)` 在类型不匹配时应检查 |
| I5-2 | 文字字体随主题变化 | 切到不同主题（如 "large" / "small"） | 时间 / 解读 / 卡片数值字体全部改变为主题指定字体 | 某些文字字体未改 | 遗漏了对 `time_label_` / `interpret_text_` / `label_*_val_` 的字体设置 |
| I5-3 | 背景色随主题变化 | 切到 "light" / "dark" 主题 | 背景色明显不同（如浅色主题下背景变成米白） | 背景始终深蓝 | 未把 `attitude_container_` 加入主题刷新 |
| I5-4 | 金色装饰圆在所有主题下可读 | 切换 2~3 个不同主题 | 装饰圆与背景始终有足够对比度 | 某主题下装饰圆几乎不可见 | 硬编码 `#ffd700` 与浅色背景冲突，需要根据主题选择互补色 |
| I5-5 | 无内存泄漏 | 切换主题 10 次前后 heap 差值 | ≤ 2 KB | heap 持续下降表示每次切换都分配新对象 | `lv_label_set_text_static` 与动态字符串混用 |

> **迭代 5 完成判定**: I5-1 ~ I5-5 全部通过。主题切换至少在 2 个实际主题下测试过。

---

## 迭代 6: IMU 驱动集成

**目标**: 从板载 QMI8658 六轴 IMU 读取加速度和角速度数据，解算成 Pitch / Roll / Yaw。

### 需要新增 / 修改的代码

| 模块 | 职责 |
|------|------|
| `main/boards/waveshare/esp32-s3-touch-lcd-1.85b/` 下新增 `qmi8658.{h,c}`（或已有驱动） | I2C 初始化 + 读取 |
| 主循环中每 10~20 ms 读取一次 IMU | 数据获取 |
| 互补滤波 / 卡尔曼滤波实现 | `float pitch, roll, yaw` |
| `static_cast<AttitudeDisplay*>(display_)->SetAttitudeData(pitch, roll, yaw)` | 把姿态数据推进 UI |

### 验收标准

| 编号 | 验收项 | 操作步骤 | 预期结果 | 失败判定 | 回归风险 |
|------|--------|----------|----------|----------|----------|
| I6-1 | I2C 总线上能枚举到 QMI8658 | 启动后在串口日志中 grep `QMI` 或 `I2C` | 能读到 WHO_AM_I = 0x05（或 QMI8658 定义的 ID） | 返回 0xFF / 0x00；或 I2C 超时 | I2C 地址错误（`0x6A` vs `0x6B`）；SDA/SCL GPIO 与其他驱动冲突 |
| I6-2 | 加速度计数据可用 | 静止放置 | `acc_x, acc_y, acc_z` 稳定为 `(0, 0, +1g)`，有 ±0.02g 噪声 | 全为 0；或剧烈跳变 | 量程（±2g / ±4g）配置错误；寄存器地址错 |
| I6-3 | 陀螺仪数据可用 | 静止 & 快速旋转 | 静止时 `gyro_x, gyro_y, gyro_z ≈ 0`（±2 dps 噪声）；旋转时能读到数百 dps 值 | 静止就有 10+ dps；或旋转时无变化 | 陀螺仪未启用；scale 配置错误 |
| I6-4 | Pitch / Roll 解算正确 | 手工将板子前倾 30°、右倾 30°、水平三种姿态各保持 2 秒 | Pitch ≈ +30° / Roll ≈ 0°；Pitch ≈ 0 / Roll ≈ +30°；Pitch & Roll 都 ≈ 0° | 误差 > 10°；或数据抖动 > 5° | 互补滤波 `alpha` 过大 / 过小；单位（rad vs deg）搞反 |
| I6-5 | Yaw 累积误差可接受 | 水平静止 60 秒 | Yaw 漂移 < 5°/分钟 | 每分钟漂移 > 15° | 陀螺仪零偏未校准；温度影响；无磁力计辅助（设备无磁力计，Yaw 只能相对） |
| I6-6 | 数据频率 ≥ 50 Hz | 串口打印每次调用 `SetAttitudeData` 的时间戳 | 间隔 ≈ 20 ms（50 Hz） | 间隔 > 100 ms（< 10 Hz） | 轮询频率过低；I2C 阻塞过久 |
| I6-7 | 不影响 UI 流畅度 | 开启 IMU 后观察气泡动画 | 无明显卡顿；UI 刷新率目测 ≥ 20 FPS | 气泡动画明显变慢 / 卡顿 | IMU 读取在 LVGL 锁内执行，阻塞 UI；需将计算放到独立任务 |
| I6-8 | IMU 初始化失败时优雅降级 | 故意禁用 IMU（如改 I2C 地址） | 屏幕仍正常显示气泡（停在中心）；日志有 `IMU init failed` 警告，不 crash | 不启动 IMU 时系统崩溃 | `SetAttitudeData` 未被调用但 UI 有断言 / 空指针 |

> **迭代 6 完成判定**: I6-1 ~ I6-8 全部通过。注意 I6-5 的 Yaw 只能是相对角度（无磁力计），需要在 UI 中明确标注 "Yaw（相对）"。

---

## 迭代 7: 数据绑定与动态 UI

**目标**: 将 IMU 数据流与气泡 / 卡片 / 颜色等级 做最终整合，达到开箱即用的完整体验。

### 需要实现的联动逻辑

```
IMU 数据  ──►  SetAttitudeData(pitch, roll, yaw)
                  ├─►  bubble_x_ / bubble_y_ （坐标 = pitch/roll × 系数）
                  ├─►  bubble_level = ComputeTiltLevel(pitch, roll)  （决定颜色）
                  ├─►  更新 3 张卡片文字 + 角度条
                  └─►  更新底部解读文字（每 0.5~1 秒刷新一次，防闪烁）
```

### 验收标准

| 编号 | 验收项 | 操作步骤 | 预期结果 | 失败判定 | 回归风险 |
|------|--------|----------|----------|----------|----------|
| I7-1 | 水平放置时气泡居中且绿色 | 将板子平放在桌面 10 秒 | 气泡在中心；颜色翠绿；Pitch/Roll 显示 ±1° 以内 | 气泡不居中；颜色不是绿色 | `SetBubbleOffset` 的零点有偏差；需加 IMU bias 校准 |
| I7-2 | 倾斜板子时气泡向倾斜反方向移动 | 前倾 15° | 气泡向前倾的反方向（或正方向，按约定）移动，位移 ≈ 角度 × 1.2 像素/度 | 移动方向与物理相反；或比例系数 > 2 或 < 0.5 | 坐标系（x↔pitch / y↔roll）映射错误 |
| I7-3 | 颜色等级阈值正确 | 逐渐增加倾角 | 0° 绿 → 3°~8° 浅绿 → 8°~15° 金黄 → 15°~25° 橙红 → 25°+ 红 | 在临界值附近频繁切换颜色（抖动） | 需要 hysteresis（滞回）保护，例如 `level = new_level if abs(new_level - old_level) ≥ 1` |
| I7-4 | 卡片数值实时反映姿态 | 缓慢旋转 | Pitch / Roll 数值 100 ms 内更新一次，Yaw 持续累积变化 | 数值滞后 > 500 ms | 滤波过度；或 UI 刷新频率 < 5 Hz |
| I7-5 | 系统长时间运行稳定 | 打开 IMU + UI，运行 5 分钟 | 无 crash；heap 无持续下降；帧率无明显下降 | 5 分钟内重启；或 heap 下降 > 10 KB | `esp_timer` 回调中的对象创建导致泄漏 |
| I7-6 | 极端姿态不导致异常 | 板子翻转 180° / 快速摇晃 5 秒 | 气泡不会飞出屏幕；数值正常显示（可能饱和但不崩溃） | 气泡位置跑到屏幕外；数值显示 `nan` | `atan2` 的奇异点（Pitch=±90°）未处理 |
| I7-7 | 关闭 IMU 时 UI 保持最后状态 | 软件停止 `SetAttitudeData` 调用 10 秒 | 气泡停在最近一次位置，不回到中心 | 气泡回到中心 | `SetAttitudeData` 未被调用时，是否应该重置位置需要明确约定 |

> **迭代 7 完成判定**: I7-1 ~ I7-7 全部通过。此时姿态平衡仪功能闭环。

---

## 迭代 8: AI 解读系统

**目标**: 根据当前倾斜等级与方向动态生成解读文字（本地 / 离线版本，不需要云端 AI）。

### 需要实现的方法

| 方法 | 职责 |
|------|------|
| `ComputeTiltLevel(pitch, roll)` | 返回 0~4 整数等级 |
| `GetInterpretationText(level, pitch, roll)` | 返回中文解读字符串 |
| `UpdateInterpretationNow(...)` | 每秒最多刷新 1~2 次（防闪烁） |

### 文案分级示例

| 等级 | 倾斜范围（参考） | 解读文案 |
|------|------------------|----------|
| 0 (完美) | 合成角度 < 3° | "阴阳调和 · 完美平衡 ✨" |
| 1 (基本平衡) | 3° ~ 8° | "基本平衡 · 状态良好" |
| 2 (轻微倾斜) | 8° ~ 15° | "略有倾斜 · 适度调整" |
| 3 (明显倾斜) | 15° ~ 25° | "明显倾斜 · 需要注意" |
| 4 (严重倾斜) | > 25° | "失衡明显 · 请调整姿态" |

### 验收标准

| 编号 | 验收项 | 操作步骤 | 预期结果 | 失败判定 | 回归风险 |
|------|--------|----------|----------|----------|----------|
| I8-1 | 等级计算准确 | 水平放置 / 倾斜 10° / 倾斜 30° 三种姿态各保持 3 秒 | 分别显示等级 0 / 2 / 4 对应的解读文字 | 等级错误（如水平显示等级 2） | `ComputeTiltLevel` 的阈值与 I7-3 不一致 |
| I8-2 | 解读文字可读且不截断 | 观察底部解读区域 | 文字完整显示在圆角矩形内，不溢出；中文无乱码 | 文字截断（右侧消失）；出现 `??` 字符 | LVGL 字体不包含中文字符；容器宽度不足 |
| I8-3 | 解读文字随姿态实时更新 | 慢慢增大倾斜 | 解读文字在 1~2 秒内切换到新等级 | 文字不变；或每秒切换 3+ 次造成闪烁 | 滞回阈值过小；或刷新频率过高 |
| I8-4 | 状态图标颜色与气泡一致 | 观察底部左侧小圆点 | 颜色与气泡主体颜色完全相同（绿/金/橙/红） | 图标颜色和气泡不同 | 颜色更新函数遗漏 `interpret_icon_` |
| I8-5 | 长时间不崩溃 | 运行 10 分钟 | heap 稳定，无重启 | heap 下降 > 5 KB | 每次刷新都 `malloc` 新字符串 |

> **迭代 8 完成判定**: I8-1 ~ I8-5 全部通过。

---

## 迭代 9: 动画效果增强

**目标**: 在已有功能基础上增加进入动画、波纹、状态切换过渡，提升视觉品质。

### 需要新增的动画

- **进入动画**: 屏幕首次点亮后 0~1.5 秒内，装饰圆从中心放大到最终半径
- **呼吸动画优化**: 气泡光晕透明度变化更柔和（sin 曲线）
- **状态切换过渡**: 颜色等级变化时，颜色做 200~400 ms 的过渡，不硬切换
- **（可选）波纹效果**: 气泡移动时留下轻微拖影或圆扩散

### 验收标准

| 编号 | 验收项 | 操作步骤 | 预期结果 | 失败判定 | 回归风险 |
|------|--------|----------|----------|----------|----------|
| I9-1 | 进入动画平滑 | 重启板子 | 背景 -> 装饰圆 -> 气泡 -> 卡片依次出现，无生硬跳动 | 没有动画直接全显；或动画时长 > 3 秒 | `lv_anim_set_time` 过大；动画延迟不合理 |
| I9-2 | 颜色过渡平滑 | 从等级 1 快速倾斜到等级 3 | 颜色在 200~400 ms 内从浅绿过渡到橙红，无硬切换 | 颜色瞬间改变 | 未使用 `lv_anim_path_linear` 对颜色分量插值 |
| I9-3 | 呼吸动画周期稳定 | 观察 10 秒 | 光晕呼吸节奏约 3~5 秒；周期稳定 | 呼吸过快（< 2 秒）造成闪烁；或过慢 | `esp_timer` 频率与呼吸相位耦合错误 |
| I9-4 | 动画不导致帧率明显下降 | 开启所有动画后观察气泡移动 | 目测帧率 ≥ 20 FPS | 帧率 < 10 FPS；气泡拖尾明显 | 每帧过度重绘；需要用 `lv_obj_invalidate` 局部刷新 |
| I9-5 | 不破坏迭代 7 的功能闭环 | 与 I7-1 / I7-2 同样测试 | 颜色 / 等级 / 数值全部正确 | 动画引入导致数据显示滞后 | 动画回调延迟处理 `SetAttitudeData` 新值 |

> **迭代 9 完成判定**: I9-1 ~ I9-5 全部通过。

---

## 迭代 10: 语音交互集成

**目标**: 用户能通过语音指令查询当前姿态，屏幕进入"对话模式"时显示消息；播报时显示反馈。

### 需要新增 / 修改的接口

| 接口 | 说明 |
|------|------|
| `Display::SetChatMessage(role, content)`（已有，可能需要在 `AttitudeDisplay` 中 override） | 显示用户语音 / AI 回复文本 |
| `Display::SetEmotion(emotion)`（已有） | 根据姿态显示对应表情（平衡 / 倾斜 / 警告） |
| Board 中的语音唤醒回调 / 对话状态机 | 在进入查询模式时调用 `display_->SetChatMessage(...)` |

### 验收标准

| 编号 | 验收项 | 操作步骤 | 预期结果 | 失败判定 | 回归风险 |
|------|--------|----------|----------|----------|----------|
| I10-1 | 语音唤醒后屏幕进入对话模式 | 说出唤醒词（如 "小质小质"） | 顶部出现唤醒响应动画或状态变化 | 无任何视觉反馈 | 唤醒事件未到达 Display；`ToggleChatState()` 没触发 UI 更新 |
| I10-2 | 姿态查询语音指令响应 | 对板子说"当前姿态"（或对应唤醒词） | AI 回复中包含当前 Pitch / Roll 数值（或中文解读） | 无识别；识别后无回复 | ASR 指令匹配错误；TTS 失败 |
| I10-3 | 对话消息显示 | 观察底部解读区或新增对话框 | 用户说的话和 AI 回复都正确显示，中文无乱码 | 对话框被气泡 / 卡片遮挡 | `lv_obj_move_foreground` 未调用 |
| I10-4 | 对话模式不中断气泡功能 | 一边说话一边倾斜板子 | 气泡 / 卡片仍实时更新 | 对话期间气泡冻结 | `SetChatMessage` 执行耗时过长阻塞主循环 |
| I10-5 | 对话结束后自动回到姿态显示 | 对话结束 3~5 秒后 | 屏幕恢复完整气泡 / 卡片 / 解读区 | 停留在对话界面 | 状态机未正确切换回 idle |
| I10-6 | 唤醒按钮（BOOT 键）功能正确 | 短按 BOOT 键 | 进入对话模式（与语音唤醒相同效果） | 无响应；或触发错误行为（如进入 Wi-Fi 配置） | `iot_button_register_cb` 回调中误调用了 `EnterWifiConfigMode`；需要区分启动态 / 运行态 |

> **迭代 10 完成判定**: I10-1 ~ I10-6 全部通过。

---

## 迭代 11: 系统整合与优化

**目标**: 性能调优、内存分析、Bug 清理、长期稳定性测试。

### 验收标准

| 编号 | 验收项 | 操作步骤 | 预期结果 | 失败判定 | 回归风险 |
|------|--------|----------|----------|----------|----------|
| I11-1 | 空闲内存充足 | 启动完成 + 运行 1 分钟后，`heap_caps_get_free_size(MALLOC_CAP_8BIT)` | 剩余 heap ≥ 40 KB | heap 持续下降（泄漏） | `lv_obj_create` 未复用；字符串不断分配 |
| I11-2 | PSRAM 使用合理 | 统计所有 `lv_obj` / 缓冲大小 | 大型缓冲区（如 Canvas）必须走 `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` | 大量大对象走内部 RAM 导致 OOM | 未在 LVGL `lv_conf.h` 中启用 `LV_MEM_CUSTOM` 走 PSRAM |
| I11-3 | CPU 占用率低 | `uxTaskGetSystemState()` 统计 | 空闲任务占比 ≥ 70%；动画 + IMU + 对话 合计 < 30% | CPU 占用 > 80% | IMU 轮询过频；动画刷新帧频过高 |
| I11-4 | 7x24 小时稳定性测试（至少跑 2 小时） | 长时间 monitor | 无重启；无 Guru Meditation；heap 稳定 | 2 小时内出现任何异常 | 周期性泄漏；watchdog 超时 |
| I11-5 | 回归测试：完整验收一次 I1-I10 | 逐项重复 I1-I10 的关键用例 | 全部通过 | 任何一项回归 | 后期重构引入的副作用 |
| I11-6 | 文档 / 注释齐全 | 代码审查 | `attitude_display.h` 中所有方法有简要注释；`attitude_display.cc` 中复杂逻辑（滤波 / 动画 / 坐标系）有说明 | 关键逻辑无注释 / 无设计说明 | 后续维护困难 |

> **迭代 11 完成判定**: I11-1 ~ I11-6 全部通过，项目可交付。

---

## 迭代 12: 串口截图功能

**目标**: 通过 USB-UART 模块实现 Host 主机获取 ESP32 屏幕截图，便于 UI Debug 和测试验证。

### 验收标准

| 编号 | 验收项 | 操作步骤 | 预期结果 | 失败判定 | 回归风险 |
|------|--------|----------|----------|----------|----------|
| I12-1 | ESP32 端初始化成功 | 烧录后观察串口日志 | 显示 "ScreenCapture initialized on GPIO17/18 at 921600 baud" | 无此日志或出现初始化失败错误 | UART1 GPIO 与其他外设冲突 |
| I12-2 | UART1 命令解析正常 | 使用 Python 脚本发送 "SCR\n" | ESP32 日志显示 "Screenshot command received" | 无响应；或命令无法触发截图 | 命令解析状态机逻辑错误 |
| I12-3 | JPEG 截图生成成功 | 发送命令后等待 | 日志显示 "JPEG captured: XXXX bytes" 和 "Base64 encoded: YYYY bytes" | 出现 "JPEG encoding failed" 或 "Base64 encode failed" | LVGL snapshot 未启用；内存不足 |
| I12-4 | Host 端成功接收并保存 | 运行 `python3 scripts/screenshot_client.py --port xxx --output ./shots` | 脚本显示 "SCR command sent" 和 "Saved: screenshot_xxx.jpg" | 超时；或文件无法打开 | 帧解析状态机错误；波特率不匹配 |
| I12-5 | 截图分辨率正确 | 打开保存的 JPEG 文件 | 图片尺寸为 360×360，内容与 ESP32 屏幕一致 | 分辨率错误；图片花屏/乱码 | LVGL snapshot 格式配置错误 |
| I12-6 | 连续截图稳定性 | 使用 `--count 10` 连续截图 | 10 张全部成功保存，无 crash | 中途 crash；或部分图片损坏 | 内存泄漏；LVGL 锁冲突 |
| I12-7 | 不影响主 UI 性能 | 截图时观察气泡动画 | 气泡仍流畅移动，无卡顿 | 截图期间 UI 冻结 > 500ms | LVGL 锁持有时间过长 |
| I12-8 | 帧同步恢复 | 在传输过程中插拔 USB | 重新连接后再次发送命令仍能成功 | 无法重新同步，需要重启 ESP32 | 帧解析器状态机未正确重置 |

> **迭代 12 完成判定**: I12-1 ~ I12-8 全部通过。

---

## 编译 / 烧录 / 真机验证流程（每个迭代必做）

每个迭代开发完成后，**必须按以下流程执行**：

```
1.  cd xiaozhi-esp32
2.  ./build_and_flash.sh build
    # 确认: 退出码 0；输出 "Build complete"
3.  ./build_and_flash.sh                      # 编译 + 烧录 + monitor
    # 确认: 看到 "Writing at 0x... (100%)"；板子重启
4.  在 monitor 中观察 30~60 秒:
    #   - 有无 "AttitudeDisplay constructed" 日志
    #   - 有无 SetupUI completed 日志
    #   - 有无任何 abort / Guru Meditation
    #   - 确认堆内存稳定（打印 heap_caps_get_free_size）
5.  真机屏幕确认:
    #   - 屏幕是否点亮
    #   - 当前迭代新增的 UI 元素是否按预期显示
    #   - 是否有截断 / 乱码 / 重叠
6.  对照本文件"验收标准"表格逐项打勾确认
7.  所有项通过 → 进入下一迭代
    存在任何失败 → 回到本迭代继续修复
```

---

## 附录 A: 常见错误与解决方案速查表

| 错误现象 | 可能原因 | 排查点 / 解决 |
|----------|----------|----------------|
| 编译 error: `'BUILTIN_TEXT_FONT' was not declared in this scope` | LVGL 字体宏未在当前 TU 可见 | 在 `attitude_display.cc` 顶部加 `LV_FONT_DECLARE(BUILTIN_TEXT_FONT);` |
| 编译 error: `'SpiLcdDisplay' does not name a type` | 头文件包含链断裂 | `attitude_display.h` 需要 `#include "lcd_display.h"` |
| 启动 crash: `Guru Meditation Error: Core 0 panic'ed (LoadProhibited)` | 访问 null 指针（常见于访问 `network_label_`） | 在 `UpdateStatusBar()` 内检查指针是否 null；或确保 `CreateTopInfoRing()` 先于 `UpdateStatusBar()` 调用 |
| 屏幕全黑但串口日志正常 | LVGL 显示未注册；或 panel_io / panel 参数错误 | 检查 `LvglDisplay` 基类是否正确调用 `lvgl_port_add_disp()`；确认 `DISPLAY_WIDTH = 360` |
| 屏幕花屏 / 镜像 | `DISPLAY_SWAP_XY` / `MIRROR_XY` 不对；或 ST77916 init cmd 错误 | 在 `config.h` / `esp32-s3-touch-lcd-1.85b.cc` 中调整 |
| 中文乱码 | 当前字体不含中文字形 | 使用项目中已定义的中文 LVGL 字体；或改用纯英文解读文案（作为降级） |
| 气泡颜色不改变 | `SetBubbleLevel()` 未被正确调用 | 在 `TickAnimation()` 中确认调用路径；加日志确认 level 值 |
| BOOT 按键无响应 | `gpio_get_level(BOOT_BUTTON_GPIO)` 返回值反转 | 检查 iot_button 的 `get_key_level = !gpio_get_level(...)` 是否需要取反 |

---

## 附录 B: 迭代完成追踪表（模板）

在实际开发过程中，每个迭代都可以复制下面的表格作为本轮验收记录：

```
=== 迭代 X 验收记录 ===
开发完成日期: ____-__-__
验证人: ______________
编译环境: ESP-IDF v____
开发板: Waveshare ESP32-S3-Touch-LCD-1.85B
开始时间: ____:____
结束时间: ____:____

验收项 | 状态 | 备注
-------|------|------
IX-1   | [__] |
IX-2   | [__] |
...    | ...  |
IX-N   | [__] |

结论: □ 通过  □ 不通过（原因: _____________________）

下一步: □ 进入迭代 X+1  □ 本轮继续修复（预计耗时: __ 小时）
```

---

## 附录 C: 关键代码位置速查

| 文件 | 行数 | 内容 |
|------|------|------|
| `main/display/attitude_display.h` | 全文 | `AttitudeDisplay` 类定义 |
| `main/display/attitude_display.cc` | 全文 | `AttitudeDisplay` 实现 |
| `main/display/display.h` | L28~L70 | `Display` 基类 + `DisplayLockGuard` |
| `main/display/lvgl_display/lvgl_display.h` | L47 | `LvglDisplay` 基类（含 LVGL 字体/主题） |
| `main/display/lcd_display.h` | L17, L62 | `LcdDisplay`、`SpiLcdDisplay` |
| `main/boards/waveshare/esp32-s3-touch-lcd-1.85b/esp32-s3-touch-lcd-1.85b.cc` | L330 | `new AttitudeDisplay(...)` — Board 集成点 |
| `main/boards/waveshare/esp32-s3-touch-lcd-1.85b/config.h` | L26~L27 | `DISPLAY_WIDTH` / `DISPLAY_HEIGHT`（必须为 360） |
| `main/CMakeLists.txt` | L19 | `display/attitude_display.cc` 编译目标 |
| `build_and_flash.sh` | 全文 | 统一的编译 / 烧录 / 监视脚本 |
| **迭代12 新增文件** | | |
| `main/screen_capture.cc` | 全文 | `ScreenCapture` 单例：UART1监听 + LVGL截图 + Base64编码 |
| `scripts/screenshot_client.py` | 全文 | Host端Python接收脚本：帧解析 + Base64解码 + JPEG保存 |
