# AI姿态平衡仪 - 详细迭代步骤拆分文档

**文档版本**: v1.0  
**生成日期**: 2026年6月  
**适用平台**: Waveshare ESP32-S3-Touch-LCD-1.85B (360×360 圆形 LCD)  
**目标框架**: ESP-IDF + LVGL  
**现有架构**: `Display` → `LvglDisplay` → `LcdDisplay`

---

## 迭代总览

### 迭代路线图

```
迭代 1: 项目基础框架 (2-3 天)
├── Step 1.1: 分析现有代码结构
├── Step 1.2: 创建 AttitudeDisplay 类基础框架
└── Step 1.3: 编译环境配置

迭代 2: 圆形背景和基础 UI 布局 (3-5 天)
├── Step 2.1: 深色径向渐变背景绘制
├── Step 2.2: 同心圆装饰边框
├── Step 2.3: 顶部信息环（时间/网络/电量）
├── Step 2.4: 底部解读区域
└── Step 2.5: 整体布局验证

迭代 3: 水平仪气泡组件 (5-7 天)
├── Step 3.1: 设计气泡 UI 组件类
├── Step 3.2: 气泡静态绘制
├── Step 3.3: 十字刻度线
├── Step 3.4: 气泡光晕效果
├── Step 3.5: 气泡平滑移动动画
└── Step 3.6: 气泡颜色随倾斜变化

迭代 4: 姿态数据卡片 (3-4 天)
├── Step 4.1: 设计卡片 UI 组件类
├── Step 4.2: Pitch/Roll/Yaw 三张卡片布局
├── Step 4.3: 角度数值显示（带方向符号）
├── Step 4.4: 倾斜程度进度条
└── Step 4.5: 数字滚动动画效果

迭代 5: 主题系统扩展 (2-3 天)
├── Step 5.1: 平衡仪专属主题
├── Step 5.2: 动态颜色管理
├── Step 5.3: 气泡/卡片/背景统一配色
└── Step 5.4: 主题切换验证

迭代 6: IMU 驱动集成 (4-5 天)
├── Step 6.1: 分析板载 IMU (QMI8658)
├── Step 6.2: 创建 IMU 数据读取模块
├── Step 6.3: 实现互补滤波算法
├── Step 6.4: 姿态角解算 (Pitch/Roll/Yaw)
└── Step 6.5: 数据更新频率优化

迭代 7: 数据绑定与动态 UI (3-4 天)
├── Step 7.1: IMU 数据驱动气泡位置
├── Step 7.2: IMU 数据驱动角度数值
├── Step 7.3: 倾斜程度判断与颜色变化
├── Step 7.4: 性能优化（帧率/功耗）
└── Step 7.5: 实时数据更新验证

迭代 8: AI 解读系统 (4-5 天)
├── Step 8.1: 设计平衡状态枚举
├── Step 8.2: 实现平衡程度判断逻辑
├── Step 8.3: 创建解读文案库
├── Step 8.4: 五行平衡映射规则
├── Step 8.5: 解读文字动态更新
└── Step 8.6: 本地解读系统验证

迭代 9: 动画效果增强 (3-4 天)
├── Step 9.1: 气泡呼吸动画
├── Step 9.2: 卡片进入/退出动画
├── Step 9.3: 背景波纹效果
├── Step 9.4: 状态切换过渡动画
└── Step 9.5: 动画性能优化

迭代 10: 语音交互集成 (5-7 天)
├── Step 10.1: 分析现有语音唤醒机制
├── Step 10.2: 姿态查询语音指令
├── Step 10.3: 语音播报姿态信息
├── Step 10.4: 对话消息显示
├── Step 10.5: 交互状态动画
└── Step 10.6: 完整语音交互测试

迭代 11: 系统整合与优化 (3-5 天)
├── Step 11.1: 全功能集成测试
├── Step 11.2: 内存优化（PSRAM 使用）
├── Step 11.3: 功耗优化（空闲模式/休眠）
├── Step 11.4: Bug 修复
└── Step 11.5: 性能基准测试
```

---

## 详细迭代步骤

---

### 迭代 1: 项目基础框架

**目标**: 创建新的 AttitudeDisplay 显示类基础框架，确保可以编译运行

**关键文件参考**:
- `main/display/display.h` - Display 基类，核心接口定义
- `main/display/lvgl_display/lvgl_display.h` - LvglDisplay 基类
- `main/display/lcd_display.h` - LcdDisplay 类定义（LCD 专有实现）
- `main/display/lcd_display.cc` - **关键**: SetupUI() 实现、主题系统、消息显示
- `main/display/lvgl_display/lvgl_theme.h` - 主题系统，颜色/字体管理
- `main/display/lvgl_display/emoji_collection.h` - 表情/图标系统
- `main/CMakeLists.txt` - 编译配置

**关键代码模式参考**:
- `DisplayLockGuard` 的使用方式：`DisplayLockGuard lock(this)` 用于线程安全
- `SetupUI()` 的实现模式：先调用基类 `Display::SetupUI()` 标记，再创建 UI 元素
- LVGL 对象创建：`lv_obj_create(parent)`, `lv_label_create(parent)` 等
- 主题获取：`auto lvgl_theme = static_cast<LvglTheme*>(current_theme_)`
- 屏幕尺寸：`LV_HOR_RES` × `LV_VER_RES` (360×360)

**交付物**:
- 新文件：`main/display/attitude_display.h` (约 80 行)
- 新文件：`main/display/attitude_display.cc` (约 150 行)
- 修改：`main/CMakeLists.txt` (添加 2 行源文件)
- 编译通过并能运行，显示测试文字 "Attitude Display Init OK"

---

**Step 1.1: 分析现有代码结构（已完成）**

需要理解的关键点：

1. **类继承关系**
   ```
   Display (抽象基类，定义 SetStatus, SetEmotion, UpdateStatusBar 等接口)
     ↑
   LvglDisplay (实现 LVGL 基础功能，管理 display_, network_label_, battery_label_ 等)
     ↑
   LcdDisplay (实现 LCD 专有 SetupUI，创建 top_bar_, status_bar_, content_, bottom_bar_, emoji 等)
     ↑
   SpiLcdDisplay / RgbLcdDisplay / MipiLcdDisplay (硬件面板差异)
   ```

2. **关键方法签名**
   ```
   virtual void SetStatus(const char* status)          // 设置顶部状态文字
   virtual void ShowNotification(const char*, int ms)   // 显示通知
   virtual void SetEmotion(const char* emotion)         // 设置表情
   virtual void SetChatMessage(const char* role, const char* content)  // 聊天消息
   virtual void UpdateStatusBar(bool update_all)        // 更新状态栏
   virtual void SetupUI()                               // 初始化 UI（关键入口）
   virtual void SetTheme(Theme* theme)                  // 切换主题
   ```

3. **线程安全机制**
   - `DisplayLockGuard lock(this)` → 调用 `Lock(int timeout_ms)`
   - `LcdDisplay::Lock()` 实现为 `return lvgl_port_lock(timeout_ms)`
   - `Unlck()` 实现为 `lvgl_port_unlock()`
   - **所有 LVGL 操作必须在 Lock/Unlock 包裹内执行**

4. **颜色与字体**
   - 颜色类型：`lv_color_t`，创建方式：`lv_color_hex(0xFF0000)` 或 `lv_color_make(255,0,0)`
   - 字体：`BUILTIN_TEXT_FONT`, `BUILTIN_ICON_FONT`, `font_awesome_30_4`
   - `LvglTheme` 提供 `background_color()`, `text_color()`, `border_color()` 等

5. **重要 LVGL 函数**
   ```
   lv_screen_active()                    // 获取当前活动屏幕
   lv_obj_create(parent)                 // 创建容器对象
   lv_label_create(parent)               // 创建标签对象
   lv_label_set_text(obj, text)          // 设置标签文字
   lv_obj_set_size(obj, w, h)            // 设置尺寸
   lv_obj_set_style_* (obj, value, 0)    // 设置样式（颜色、边框、圆角、透明度等）
   lv_obj_align(obj, align_mode, x, y)   // 对齐方式
   lv_obj_set_pos(obj, x, y)             // 绝对定位
   lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN)  // 隐藏对象
   lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN)  // 显示对象
   LV_HOR_RES / LV_VER_RES               // 屏幕分辨率（编译时确定）
   ```

6. **现有 UI 结构（LcdDisplay::SetupUI）**
   ```
   screen (根)
     ├── container_ (主容器，flex column 布局)
     │   ├── top_bar_ (顶部栏，flex row，50%透明背景)
     │   │   ├── network_label_ (左侧)
     │   │   └── right_icons
     │   │       ├── mute_label_
     │   │       └── battery_label_
     │   ├── content_ (中间内容区，emoji/预览图等)
     │   └── bottom_bar_ (底部栏，聊天消息)
     │       └── chat_message_label_
     │
     └── status_bar_ (顶部状态栏，独立于 container_, 绝对定位在顶端)
         ├── notification_label_ (通知，隐藏状态)
         └── status_label_ (状态文字，时间显示)
   ```

7. **板级支持要点**
   - 目标板：`main/boards/waveshare/esp32-s3-touch-lcd-1.85b/` 目录下
   - I2C 总线：SDA=GPIO11, SCL=GPIO10（已在其他驱动中使用）
   - I2C 设备：CST816S(触摸,0x15), QMI8658(IMU,0x6B), ES8311(音频,0x30) 等
   - 当前项目 **可能已有 QMI8658 驱动**（需要进一步验证）

---

**Step 1.2: 创建 AttitudeDisplay 类基础框架**

**任务描述**: 创建新的 AttitudeDisplay 类，继承自 LcdDisplay，实现最基本的 SetupUI()，确保能编译和运行。

**文件创建**: `main/display/attitude_display.h`

```
文件结构规划（约 80 行）：
├── #pragma once
├── #include "lcd_display.h"
├──
├── class AttitudeDisplay : public LcdDisplay
├── {
├── public:
│   ├── // 构造函数：继承 LcdDisplay 的构造签名
│   ├── // 接收 panel_io, panel, width, height, offset_x/y, mirror_x/y, swap_xy
│   ├── AttitudeDisplay(esp_lcd_panel_io_handle_t panel_io,
│   │                    esp_lcd_panel_handle_t panel,
│   │                    int width, int height,
│   │                    int offset_x, int offset_y,
│   │                    bool mirror_x, bool mirror_y, bool swap_xy);
│   ├──
│   ├── virtual ~AttitudeDisplay() = default;
│   ├──
│   ├── // 重写关键方法
│   ├── virtual void SetupUI() override;      // 核心 UI 初始化
│   ├── virtual void SetTheme(Theme* theme) override;  // 主题切换
│   ├──
│   ├── // 姿态显示专属方法（第一版为空实现）
│   ├── void SetAttitudeData(float pitch, float roll, float yaw);
│   ├── void SetInterpretation(const std::string& text);
│   ├──
├── private:
│   ├── // UI 元素（第一版只需测试容器和标签）
│   ├── lv_obj_t* test_label_ = nullptr;       // 测试用标签
│   ├── lv_obj_t* attitude_container_ = nullptr;  // 姿态显示主容器
│   ├──
│   ├── // 状态数据（第一版为占位）
│   ├── float current_pitch_ = 0.0f;
│   ├── float current_roll_ = 0.0f;
│   ├── float current_yaw_ = 0.0f;
│   ├──
│   ├── // 内部辅助方法
│   ├── void CreateTestLayout();  // 创建测试布局
│   └── void ApplyThemeToAttitudeUI();  // 应用主题
│   └──
├── };
```

**文件创建**: `main/display/attitude_display.cc`

```
文件结构规划（约 150 行）：
├── #include "attitude_display.h"
├── #include "lvgl_theme.h"
�── #include <esp_log.h>
├──
├── #define TAG "AttitudeDisplay"
├──
├── // 构造函数：直接调用 LcdDisplay 基类构造
├── // 关键: LcdDisplay 的 protected 构造函数接受这些参数
├── AttitudeDisplay::AttitudeDisplay(esp_lcd_panel_io_handle_t panel_io,
│                                    esp_lcd_panel_handle_t panel,
│                                    int width, int height,
│                                    int offset_x, int offset_y,
│                                    bool mirror_x, bool mirror_y, bool swap_xy)
│   : LcdDisplay(panel_io, panel, width, height, offset_x, offset_y,
│                mirror_x, mirror_y, swap_xy)
├── {
│   ├── ESP_LOGI(TAG, "AttitudeDisplay constructed, %dx%d", width, height);
└── }
├──
├── void AttitudeDisplay::SetupUI()
├── {
│   ├── // 1. 防止重复调用（参考 LcdDisplay）
│   ├── if (setup_ui_called_) {
│   │   └── ESP_LOGW(TAG, "SetupUI() already called, skipping");
│   │   └── return;
│   └── }
│   ├──
│   ├── // 2. 调用基类标记 SetupUI 被调用（Display::SetupUI()）
│   ├── // 注意：不要调用 LcdDisplay::SetupUI()，它会创建现有 Lcd UI
│   ├── Display::SetupUI();  // 这会设置 setup_ui_called_ = true
│   ├──
│   ├── // 3. 获取 LVGL 锁（线程安全）
│   ├── DisplayLockGuard lock(this);
│   ├──
│   ├── // 4. 获取主题
│   ├── auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
│   ├── if (lvgl_theme == nullptr) {
│   │   └── ESP_LOGE(TAG, "Theme is null!");
│   │   └── return;
│   └── }
│   ├──
│   ├── // 5. 设置屏幕基础样式
│   ├── auto screen = lv_screen_active();
│   ├── lv_obj_set_style_text_font(screen, lvgl_theme->text_font()->font(), 0);
│   ├── lv_obj_set_style_text_color(screen, lvgl_theme->text_color(), 0);
│   ├── lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);  // 黑色背景
│   ├──
│   ├── // 6. 创建主容器（360×360，覆盖整个屏幕）
│   ├── attitude_container_ = lv_obj_create(screen);
│   ├── lv_obj_set_size(attitude_container_, LV_HOR_RES, LV_VER_RES);
│   ├── lv_obj_set_style_radius(attitude_container_, 0, 0);  // 方形容器，后续迭代做圆角
│   ├── lv_obj_set_style_border_width(attitude_container_, 0, 0);
│   ├── lv_obj_set_style_pad_all(attitude_container_, 0, 0);
│   ├── lv_obj_set_style_bg_color(attitude_container_, lv_color_hex(0x0d1b2a), 0);  // 深蓝
│   ├──
│   ├── // 7. 创建测试标签
│   ├── CreateTestLayout();
│   ├──
│   ├── ESP_LOGI(TAG, "SetupUI completed");
└── }
├──
├── void AttitudeDisplay::CreateTestLayout()
├── {
│   ├── // 创建一个居中的测试标签
│   ├── test_label_ = lv_label_create(attitude_container_);
│   ├── lv_label_set_text(test_label_, "Attitude Display Init OK");
│   ├── lv_obj_set_style_text_font(test_label_, BUILTIN_TEXT_FONT, 0);
│   ├── lv_obj_set_style_text_color(test_label_, lv_color_hex(0x00ff88), 0);  // 翠绿色
│   ├── lv_obj_center(test_label_);  // 居中显示
│   ├──
│   ├── // 再创建一个副标签，显示屏幕尺寸
│   ├── char size_text[64];
│   ├── snprintf(size_text, sizeof(size_text), "Screen: %dx%d", LV_HOR_RES, LV_VER_RES);
│   ├── lv_obj_t* size_label = lv_label_create(attitude_container_);
│   ├── lv_label_set_text(size_label, size_text);
│   ├── lv_obj_set_style_text_color(size_label, lv_color_hex(0xaaaaaa), 0);  // 浅灰色
│   ├── lv_obj_align_to(size_label, test_label_, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);
│   └──
│   ├── ESP_LOGI(TAG, "Test layout created, label=%p", test_label_);
└── }
├──
├── void AttitudeDisplay::SetAttitudeData(float pitch, float roll, float yaw)
├── {
│   ├── // 第一版：仅记录数据，不做任何显示
│   ├── current_pitch_ = pitch;
│   ├── current_roll_ = roll;
│   ├── current_yaw_ = yaw;
│   ├── // 后续迭代会在这里更新 UI
└── }
├──
├── void AttitudeDisplay::SetInterpretation(const std::string& text)
├── {
│   ├── // 第一版：空实现
│   ├── ESP_LOGD(TAG, "Interpretation: %s", text.c_str());
└── }
├──
├── void AttitudeDisplay::SetTheme(Theme* theme)
├── {
│   ├── // 先调用基类设置主题
│   ├── Display::SetTheme(theme);
│   ├── // 然后应用到姿态 UI（第一版空实现）
│   ├── ApplyThemeToAttitudeUI();
└── }
├──
├── void AttitudeDisplay::ApplyThemeToAttitudeUI()
├── {
│   ├── // 第一版：空实现，后续迭代会在这里更新所有元素的颜色
└── }
```

**关键风险与注意事项**:

1. **LcdDisplay 构造函数的可见性**
   - 需要确认 `LcdDisplay(LcdDisplay::LcdDisplay(esp_lcd_panel_io_handle_t, ...)` 是否为 protected
   - 如果是 private，需要修改为 protected 才能继承
   - 检查：`main/display/lcd_display.h` 中 `LcdDisplay(...)` 的访问级别

2. **不要调用 LcdDisplay::SetupUI()**
   - 那会创建整个 LcdDisplay 的 UI 结构（top_bar_, content_, emoji 等）
   - 我们要完全重新创建自己的 UI，所以只调用 `Display::SetupUI()` 标记状态

3. **屏幕尺寸的一致性**
   - `LV_HOR_RES` 和 `LV_VER_RES` 是编译时宏定义
   - 在 360×360 的目标板上应该是 360 和 360
   - 但需要确认 LVGL 配置（`lv_conf.h` 或 `lvgl_conf.h`）

4. **主题指针的有效性**
   - `current_theme_` 是从 `Display` 基类继承的
   - 在 `LcdDisplay` 构造函数中会从 `Settings` 加载主题
   - 需要确保主题已初始化

---

**Step 1.3: 编译环境配置**

**修改文件**: `main/CMakeLists.txt`

```
在 SRCS 列表中添加（约 2 行新增）：
├── ...现有条目...
├── "display/attitude_display.cc"
└── ...现有条目...
```

**编译测试流程**:
1. 确认 ESP-IDF 环境已激活（`idf.py --version` 能正常输出版本）
2. 运行：`idf.py build`
3. 观察编译是否有错误
4. 如果成功，记录编译信息
5. 烧录到开发板：`idf.py -p [串口] flash`
6. 监视串口输出：`idf.py -p [串口] monitor`
7. 观察屏幕是否显示 "Attitude Display Init OK" 文字

**预期输出**:
```
编译输出:
  - Build complete
  - 无编译错误
  - 无严重警告

串口日志（关键行）:
  - "AttitudeDisplay constructed, 360x360"
  - "SetupUI completed"
  - "Test layout created, label=0x..."

屏幕显示:
  - 深蓝色背景
  - 中央显示 "Attitude Display Init OK" (翠绿色)
  - 下方显示 "Screen: 360x360" (浅灰色)
```

**常见问题排查**:

| 问题 | 可能原因 | 解决方法 |
|------|---------|---------|
| 编译错误：`LcdDisplay` 的 protected 构造函数无法访问 | 继承权限问题 | 修改 `lcd_display.h`，将构造函数改为 protected 或 public |
| 编译错误：`esp_lcd_panel_io_handle_t` 未定义 | 缺少头文件 | 在 attitude_display.h 中添加 `#include <esp_lcd_panel_io.h>` |
| 运行时崩溃：`test_label_` 为 null | LVGL 对象创建失败 | 检查 `attitude_container_` 是否成功创建 |
| 屏幕无显示 | LVGL 初始化失败 | 检查 `lv_init()`, `lvgl_port_init()`, `lvgl_port_add_disp()` 是否已被正确调用（在基类构造函数中） |
| 屏幕尺寸不对 | LV_HOR_RES/LV_VER_RES 配置错误 | 检查 `lv_conf.h` 中的分辨率配置 |

**验收标准**（迭代 1 完成标志）:
- [x] 代码编译无错误（允许少量警告）
- [x] 固件能正常烧录到 Waveshare 1.85B 开发板
- [x] 屏幕显示测试文字（深蓝色背景 + 绿色/灰色文字）
- [x] 系统能正常启动并进入主循环
- [x] `SetupUI()` 只被调用一次
- [x] 屏幕尺寸正确显示为 360×360
- [x] 串口日志显示 AttitudeDisplay 相关信息

---

### 迭代 2: 圆形背景和基础 UI 布局

**目标**: 实现 360×360 圆形屏幕的整体布局，包括径向渐变背景、装饰元素、状态栏、底部解读区域

**关键文件参考**:
- `main/display/lvgl_display/lvgl_theme.h` - 颜色获取方式
- `main/display/lcd_display.cc` Line 354-450 - 现有 UI 创建模式
- LVGL 文档：`lv_canvas`, `lv_obj_set_style_arc`, `lv_arc_create`

**技术要点**:
- LVGL Canvas 用于绘制圆形和渐变
- 使用 `lv_color_hex()` 或 `lv_color_make()` 创建颜色
- 圆环装饰可用 `lv_arc_create()` 或在 Canvas 上手动绘制
- 360×360 屏幕但视觉区域为圆形，边缘 10-20px 会被裁掉

**交付物**:
- 修改：`main/display/attitude_display.cc`（新增约 300-400 行）
- 修改：`main/display/attitude_display.h`（新增约 20-30 行成员变量和方法）
- 视觉效果：深蓝径向渐变背景 + 3圈金色装饰圆 + 顶部信息 + 底部解读

---

**Step 2.1: 深色径向渐变背景绘制**

**设计规格**:

| 属性 | 规格 |
|------|------|
| 画布尺寸 | 360×360（整个屏幕） |
| 中心点 | (180, 180) |
| 中心颜色 | 深蓝 `#0d1b2a` |
| 边缘颜色 | 近黑 `#050a14` |
| 渐变方向 | 径向（中心向外） |
| 边缘阴影 | 额外的暗色光晕（模拟屏幕圆角） |

**实现思路**:

```
方案 A（推荐）: 使用 LVGL Canvas 手动绘制
├── 创建 lv_draw_buf_t 缓冲（360×360×2字节 = 259,200字节 → 约 253KB，用 PSRAM）
├── 逐像素计算距中心距离
├── 根据距离插值颜色
├── distance = sqrt((x-180)² + (y-180)²)
├── ratio = distance / 180
├── color = lerp(#0d1b2a, #050a14, ratio)
└── 创建 lv_canvas，将缓冲绑定

优点：效果最佳
缺点：绘制耗时，需要 PSRAM

方案 B（简化）: 使用多层半透明圆
├── 创建 3-5 个同心椭圆
├── 外层深色 → 内层渐亮
├── 设置不同透明度
└── LVGL 对象组合模拟渐变

优点：速度快，内存小
缺点：视觉效果不如逐像素

迭代 2 采用方案 B，先确保功能可用，后续迭代优化
```

**代码添加到 AttitudeDisplay::SetupUI()**:

```
在 CreateTestLayout() 调用之前添加：
├── CreateBackground();  // 新方法

新增成员变量（attitude_display.h）:
├── lv_obj_t* background_ = nullptr;      // 背景容器
└── lv_obj_t* bg_layer_center_ = nullptr; // 中心渐变层（方案 B）

新增方法声明（attitude_display.h）:
└── void CreateBackground();

实现 CreateBackground()（attitude_display.cc）:
├── // 创建背景容器（全屏）
├── background_ = lv_obj_create(attitude_container_);
├── lv_obj_set_size(background_, LV_HOR_RES, LV_VER_RES);
├── lv_obj_set_style_radius(background_, 0, 0);
├── lv_obj_set_style_border_width(background_, 0, 0);
├── lv_obj_set_style_pad_all(background_, 0, 0);
├── lv_obj_set_style_bg_color(background_, lv_color_hex(0x050a14), 0);  // 深色底
│
├── // 创建中心高亮层（模拟径向渐变中心）
├── bg_layer_center_ = lv_obj_create(background_);
├── lv_obj_set_size(bg_layer_center_, 280, 280);  // 直径 280，半径 140
├── lv_obj_set_style_radius(bg_layer_center_, 140, 0);  // 圆形（半径）
├── lv_obj_set_style_border_width(bg_layer_center_, 0, 0);
├── lv_obj_set_style_bg_color(bg_layer_center_, lv_color_hex(0x0d1b2a), 0);
├── lv_obj_set_style_bg_opa(bg_layer_center_, LV_OPA_100, 0);
├── lv_obj_center(bg_layer_center_);  // 居中
│
├── // 创建内层高亮（更小的圆，更亮一点）
├── lv_obj_t* inner_glow = lv_obj_create(bg_layer_center_);
├── lv_obj_set_size(inner_glow, 200, 200);
├── lv_obj_set_style_radius(inner_glow, 100, 0);
├── lv_obj_set_style_border_width(inner_glow, 0, 0);
├── lv_obj_set_style_bg_color(inner_glow, lv_color_hex(0x142a40), 0);
├── lv_obj_set_style_bg_opa(inner_glow, LV_OPA_80, 0);
├── lv_obj_center(inner_glow);
│
├── // 设置背景层为底部（z-index 最低）
└── lv_obj_move_background(background_);
```

**关键点**:
- `lv_obj_set_style_radius()` 的参数是 **单角圆角半径**，不是直径
- 要画完整的圆：radius = width/2 = height/2
- `LV_OPA_*` 宏控制透明度（0-100）
- `lv_obj_center()` 将对象相对于父容器居中
- `lv_obj_move_background()` 将对象移到最底层（不遮挡其他元素）

**验证**: 烧录后看到中心亮、边缘暗的圆形渐变效果

---

**Step 2.2: 同心圆装饰边框**

**设计**: 3圈金色装饰圆

| 圈编号 | 半径（像素） | 颜色 | 线宽 | 样式 |
|-------|------------|------|------|------|
| 外圈 | 160 px | 金色 `#ffd700` 半透明 50% | 2 px | 实线 |
| 中圈 | 140 px | 金色 `#ffd700` 半透明 30% | 1 px | 虚线 |
| 内圈 | 120 px | 金色 `#ffd700` 半透明 20% | 1 px | 点线 |

**实现思路**:

```
使用 lv_arc_create()（圆弧对象）:
├── 每个圆是一个 lv_obj_t*（arc）
├── 设置起止角度为 0~360 度（完整圆）
├── 设置背景弧透明（不显示填充）
├── 只显示前景弧（圆环）
└── 线宽 = 圆弧宽度

或者使用 lv_obj 的 border 样式（更简单）：
├── 创建透明填充的圆形容器
├── 设置 border_width 和 border_color
├── 背景透明 (bg_opa = 0)
└── 边框不透明

迭代 2 采用 border 方案（简单可靠）
```

**代码添加到 CreateBackground() 之后或单独方法**:

```
新增成员变量:
├── lv_obj_t* circle_outer_ = nullptr;  // 外圈装饰
├── lv_obj_t* circle_mid_ = nullptr;    // 中圈装饰
└── lv_obj_t* circle_inner_ = nullptr;  // 内圈装饰

新增方法:
└── void CreateDecorationCircles();

实现 CreateDecorationCircles():
├── // 外圈：半径 160 → 直径 320 → 位置 (180-160, 180-160) = (20, 20)
├── circle_outer_ = lv_obj_create(attitude_container_);
├── lv_obj_set_size(circle_outer_, 320, 320);
├── lv_obj_set_pos(circle_outer_, 20, 20);  // 手动定位
├── lv_obj_set_style_radius(circle_outer_, 160, 0);  // 圆形
├── lv_obj_set_style_bg_opa(circle_outer_, LV_OPA_TRANSP, 0);  // 透明填充
├── lv_obj_set_style_border_width(circle_outer_, 2, 0);  // 2px 边框
├── lv_obj_set_style_border_color(circle_outer_, lv_color_hex(0xffd700), 0);
├── lv_obj_set_style_border_opa(circle_outer_, LV_OPA_50, 0);  // 50% 透明
│
├── // 中圈：半径 140 → 直径 280 → 位置 (40, 40)
├── circle_mid_ = lv_obj_create(attitude_container_);
├── lv_obj_set_size(circle_mid_, 280, 280);
├── lv_obj_set_pos(circle_mid_, 40, 40);
├── lv_obj_set_style_radius(circle_mid_, 140, 0);
├── lv_obj_set_style_bg_opa(circle_mid_, LV_OPA_TRANSP, 0);
├── lv_obj_set_style_border_width(circle_mid_, 1, 0);
├── lv_obj_set_style_border_color(circle_mid_, lv_color_hex(0xffd700), 0);
├── lv_obj_set_style_border_opa(circle_mid_, LV_OPA_30, 0);  // 30% 透明
│
├── // 内圈：半径 120 → 直径 240 → 位置 (60, 60)
├── circle_inner_ = lv_obj_create(attitude_container_);
├── lv_obj_set_size(circle_inner_, 240, 240);
├── lv_obj_set_pos(circle_inner_, 60, 60);
├── lv_obj_set_style_radius(circle_inner_, 120, 0);
├── lv_obj_set_style_bg_opa(circle_inner_, LV_OPA_TRANSP, 0);
├── lv_obj_set_style_border_width(circle_inner_, 1, 0);
├── lv_obj_set_style_border_color(circle_inner_, lv_color_hex(0xffd700), 0);
├── lv_obj_set_style_border_opa(circle_inner_, LV_OPA_20, 0);  // 20% 透明
│
├── // 将装饰圆移到背景之上、内容之下
└── // (z-order 由创建顺序决定，无需特殊处理)
```

**LVGL 虚线/点线说明**:
- 标准 LVGL 不直接支持虚线边框
- 可以改用 `lv_arc_create()` + 自定义绘制实现虚线
- 或者：第一版全部使用实线，后续迭代再优化虚线效果
- **迭代 2 决策**: 3圈全部使用实线，只通过透明度和线宽区分层次

**验证**: 屏幕显示3圈金色圆环，从外到内越来越暗、越来越细

---

**Step 2.3: 顶部信息环（时间/网络/电量）**

**布局设计**:

```
屏幕顶部 (y = 10 ~ 40 区域)
┌─────────────────────────────────────────────────────┐
│ [wifi_icon]          [HH:MM]           [battery_icon] │
│    (30, 20)            (180, 20)          (330, 20)   │
│  左上角图标            顶部中央时间       右上角图标   │
└─────────────────────────────────────────────────────┘
```

**实现要点**:

参考现有 `LcdDisplay::SetupUI()` 中 `top_bar_` 和 `status_bar_` 的创建方式。

**新增成员变量**:
```
lv_obj_t* top_info_container_ = nullptr;  // 顶部信息容器
lv_obj_t* network_icon_ = nullptr;         // 网络状态图标
lv_obj_t* time_label_ = nullptr;           // 时间显示
lv_obj_t* battery_icon_ = nullptr;         // 电量图标
```

**新增方法**:
```
void CreateTopInfoRing();      // 创建顶部信息栏
void UpdateTimeDisplay();      // 更新时间（被 UpdateStatusBar 调用）
```

**实现 CreateTopInfoRing()**:

```
├── // 创建顶部信息容器（透明背景，便于放置在圆形屏幕顶部）
├── top_info_container_ = lv_obj_create(attitude_container_);
├── lv_obj_set_size(top_info_container_, LV_HOR_RES, 40);  // 高度 40px
├── lv_obj_set_pos(top_info_container_, 0, 10);  // 距顶部 10px
├── lv_obj_set_style_radius(top_info_container_, 0, 0);
├── lv_obj_set_style_bg_opa(top_info_container_, LV_OPA_TRANSP, 0);  // 透明
├── lv_obj_set_style_border_width(top_info_container_, 0, 0);
├── lv_obj_set_style_pad_all(top_info_container_, 0, 0);
│
├── // 网络图标（左上角，参考 LcdDisplay::SetupUI 中 network_label_）
├── network_icon_ = lv_label_create(top_info_container_);
├── lv_label_set_text(network_icon_, "");  // 初始为空，UpdateStatusBar 会设置
├── lv_obj_set_style_text_font(network_icon_, BUILTIN_ICON_FONT, 0);
├── lv_obj_set_style_text_color(network_icon_, lv_color_hex(0xaaaaaa), 0);
├── lv_obj_set_pos(network_icon_, 30, 10);  // 左上角区域
│
├── // 时间显示（顶部中央）
├── time_label_ = lv_label_create(top_info_container_);
├── lv_label_set_text(time_label_, "00:00");
├── lv_obj_set_style_text_font(time_label_, BUILTIN_TEXT_FONT, 0);
├── lv_obj_set_style_text_color(time_label_, lv_color_hex(0xffffff), 0);
├── lv_obj_center(time_label_);  // 在容器内水平垂直居中
│
├── // 电量图标（右上角）
├── battery_icon_ = lv_label_create(top_info_container_);
├── lv_label_set_text(battery_icon_, "");  // 初始为空
├── lv_obj_set_style_text_font(battery_icon_, BUILTIN_ICON_FONT, 0);
├── lv_obj_set_style_text_color(battery_icon_, lv_color_hex(0xaaaaaa), 0);
├── lv_obj_set_pos(battery_icon_, LV_HOR_RES - 50, 10);  // 右上角 (x = 310)
│
├── // 设置 z-order：信息栏在装饰圆之上（由创建顺序保证）
└── ESP_LOGI(TAG, "Top info ring created");
```

**重写 UpdateStatusBar()**:

```
// 在 attitude_display.h 中声明：
virtual void UpdateStatusBar(bool update_all = false) override;

// 在 attitude_display.cc 中实现（参考 LvglDisplay::UpdateStatusBar）：
void AttitudeDisplay::UpdateStatusBar(bool update_all)
{
    // 调用基类实现更新网络和电量图标（复用现有逻辑）
    // 注意：基类操作的是 network_label_ 等，我们需要用自己的 network_icon_
    // 所以这里需要重写，而不是调用基类

    auto& app = Application::GetInstance();
    auto& board = Board::GetInstance();

    DisplayLockGuard lock(this);

    // 1. 更新网络图标（直接复制自 LvglDisplay::UpdateStatusBar 逻辑）
    static int seconds_counter = 0;
    if (update_all || seconds_counter++ % 10 == 0) {
        const char* icon = board.GetNetworkStateIcon();
        if (network_icon_ != nullptr && icon != nullptr) {
            lv_label_set_text(network_icon_, icon);
        }
    }

    // 2. 更新时间显示（参考基类的 time 逻辑）
    if (app.GetDeviceState() == kDeviceStateIdle) {
        time_t now = time(NULL);
        struct tm* tm_info = localtime(&now);
        if (tm_info->tm_year >= 2025 - 1900) {  // 时间已校准
            char time_str[16];
            strftime(time_str, sizeof(time_str), "%H:%M", tm_info);
            if (time_label_ != nullptr) {
                lv_label_set_text(time_label_, time_str);
            }
        }
    }

    // 3. 更新电量图标
    int battery_level;
    bool charging, discharging;
    if (board.GetBatteryLevel(battery_level, charging, discharging)) {
        const char* icon = nullptr;
        if (charging) {
            icon = FONT_AWESOME_BATTERY_BOLT;  // 需要确认这个宏的定义
        } else {
            // 根据电量等级选择图标
            // 参考 LvglDisplay::UpdateStatusBar 中的 battery levels
            const char* levels[] = {
                FONT_AWESOME_BATTERY_EMPTY,
                FONT_AWESOME_BATTERY_QUARTER,
                FONT_AWESOME_BATTERY_HALF,
                FONT_AWESOME_BATTERY_THREE_QUARTERS,
                FONT_AWESOME_BATTERY_FULL,
            };
            icon = levels[battery_level / 20];
        }
        if (battery_icon_ != nullptr && icon != nullptr) {
            lv_label_set_text(battery_icon_, icon);
        }
    }
}
```

**Font Awesome 图标宏验证**:
需要确认这些宏在项目中存在：
- `FONT_AWESOME_BATTERY_EMPTY`
- `FONT_AWESOME_BATTERY_QUARTER`
- `FONT_AWESOME_BATTERY_HALF`
- `FONT_AWESOME_BATTERY_THREE_QUARTERS`
- `FONT_AWESOME_BATTERY_FULL`
- `FONT_AWESOME_BATTERY_BOLT`
- `FONT_AWESOME_WIFI` (或类似的网络图标)

搜索项目中 `font_awesome.h` 或类似文件确认。

**验证**: 顶部显示时间（00:00 或实际时间），两侧有网络和电量图标（或空白，取决于系统状态）

---

**Step 2.4: 底部解读区域**

**布局设计**:

```
屏幕底部 (y = 300 ~ 350 区域)
┌─────────────────────────────────────────────────────┐
│  [状态图标] 当前状态：基本平衡 · 建议保持当前位置       │
└─────────────────────────────────────────────────────┘
```

**实现要点**:
- 圆角矩形背景（比屏幕稍窄，留出边缘空间）
- 半透明背景，不遮挡气泡显示
- 左侧小图标表示状态（绿/黄/红色）
- 中央显示解读文字（可滚动）
- 第一版显示固定测试文字

**新增成员变量**:
```
lv_obj_t* bottom_interpret_ = nullptr;    // 底部解读容器
lv_obj_t* interpret_icon_ = nullptr;       // 状态图标
lv_obj_t* interpret_text_ = nullptr;       // 解读文字
```

**新增方法**:
```
void CreateBottomInterpretation();  // 创建底部解读区域
```

**实现 CreateBottomInterpretation()**:

```
├── // 创建底部容器
├── bottom_interpret_ = lv_obj_create(attitude_container_);
├── lv_obj_set_size(bottom_interpret_, 320, 40);  // 宽度 320，高度 40
├── lv_obj_set_pos(bottom_interpret_, 20, 310);   // 距离顶部 310px (底部区域)
├── lv_obj_set_style_radius(bottom_interpret_, 20, 0);  // 圆角 20px
├── lv_obj_set_style_bg_color(bottom_interpret_, lv_color_hex(0x1a2a3a), 0);  // 深蓝灰
├── lv_obj_set_style_bg_opa(bottom_interpret_, LV_OPA_80, 0);  // 80% 不透明
├── lv_obj_set_style_border_width(bottom_interpret_, 1, 0);
├── lv_obj_set_style_border_color(bottom_interpret_, lv_color_hex(0x334455), 0);
├── lv_obj_set_style_pad_all(bottom_interpret_, 5, 0);
│
├── // 状态图标（左侧小圆点，表示当前状态）
├── interpret_icon_ = lv_obj_create(bottom_interpret_);
├── lv_obj_set_size(interpret_icon_, 10, 10);
├── lv_obj_set_style_radius(interpret_icon_, 5, 0);  // 圆形
├── lv_obj_set_style_bg_color(interpret_icon_, lv_color_hex(0x00ff88), 0);  // 翠绿
├── lv_obj_set_style_border_width(interpret_icon_, 0, 0);
├── lv_obj_set_pos(interpret_icon_, 10, 15);  // 在容器内左侧
│
├── // 解读文字
├── interpret_text_ = lv_label_create(bottom_interpret_);
├── lv_label_set_text(interpret_text_, "当前状态：基本平衡 · 建议保持");
├── lv_obj_set_style_text_font(interpret_text_, BUILTIN_TEXT_FONT, 0);
├── lv_obj_set_style_text_color(interpret_text_, lv_color_hex(0xffffff), 0);
├── lv_obj_set_width(interpret_text_, 280);  // 限制宽度（防止溢出）
├── lv_obj_set_pos(interpret_text_, 25, 12);  // 在图标右侧
│
├── // 允许文字过长时自动滚动
├── lv_label_set_long_mode(interpret_text_, LV_LABEL_LONG_SCROLL);
│
└── ESP_LOGI(TAG, "Bottom interpretation area created");
```

**关键点**:
- `lv_label_set_long_mode()` 设置文字过长时的行为
- `LV_LABEL_LONG_SCROLL` 表示文字横向滚动显示
- `LV_LABEL_LONG_DOT` 表示末尾显示省略号（"..."）
- `LV_LABEL_LONG_WRAP` 表示换行（需要多行显示）
- 容器内对象的 `lv_obj_set_pos()` 是相对于父容器的坐标

**验证**: 底部显示一个圆角矩形背景，左侧绿色圆点，右侧文字"当前状态：基本平衡..."

---

**Step 2.5: 整体布局验证**

**验证清单**:

```
屏幕布局测试清单：
├── [ ] 背景渐变显示正确（中心亮，边缘暗）
├── [ ] 3圈装饰圆完整可见（半径 160/140/120）
├── [ ] 无截断现象（装饰圆边缘完整）
├── [ ] 网络图标在左上角可见
├── [ ] 时间在顶部中央，格式 HH:MM
├── [ ] 电量图标在右上角可见
├── [ ] 底部解读区域在底部可见
├── [ ] 所有元素不重叠（顶部/中心/底部分离）
├── [ ] 所有元素都在可视区域内（距离边缘 > 20px）
├── [ ] 整体配色协调（深蓝+金色+白色文字）
└── [ ] UpdateStatusBar 能被正常调用，时间正常更新
```

**验收标准**（迭代 2 完成标志）:
- [x] 深蓝径向渐变背景显示正确
- [x] 3圈金色装饰圆完整，无截断
- [x] 顶部信息栏显示：时间（中央）+ 图标（两侧）
- [x] 底部解读区域完整显示
- [x] 文字清晰可读
- [x] 图标显示正确（或为空，取决于系统状态）
- [x] 无重叠和错位
- [x] 系统长时间运行无崩溃
- [x] UpdateStatusBar() 能正确更新时间（需要系统时间已校准）

---

### 迭代 3: 水平仪气泡组件

**目标**: 实现核心的气泡水平仪组件，包括气泡、十字准星、刻度线、光晕效果和颜色动画

**关键文件参考**:
- LVGL 动画系统：`lv_anim.h`（动画 API）
- `lvgl_port.h` 中的 `lvgl_port_lock/unlock` 机制
- LVGL 对象样式 API：`lv_obj_set_style_*` 系列函数

**技术要点**:
- LVGL 动画：`lv_anim_init()`, `lv_anim_set_var()`, `lv_anim_set_values()`, `lv_anim_set_exec_cb()`, `lv_anim_set_time()`, `lv_anim_set_path_cb()`, `lv_anim_start()`
- 动画路径：`lv_anim_path_ease_out()`, `lv_anim_path_linear()` 等
- 颜色表示：`lv_color_hex(0xRRGGBB)` 或 `lv_color_make(r, g, b)`
- 呼吸动画：周期性修改透明度 `lv_obj_set_style_bg_opa()`

**交付物**:
- 修改：`main/display/attitude_display.cc`（新增约 400-500 行）
- 修改：`main/display/attitude_display.h`（新增约 30-40 行成员变量和方法）
- 效果：中央显示气泡+准星+刻度，可手动测试（后续迭代绑定 IMU）

---

**Step 3.1: 设计气泡 UI 组件类**

**设计分析**: 决定是创建独立的 `LvglBubble` 类，还是直接在 `AttitudeDisplay` 内实现。

**决策**: **直接在 AttitudeDisplay 内实现（不创建独立类）**

理由：
1. 第一版优先保证功能，过度设计会增加复杂度
2. 气泡与 AttitudeDisplay 紧密耦合，复用场景少
3. 可以后续迭代再重构为独立类

**组件结构规划**:

```
AttitudeDisplay 中气泡相关的成员：
├── 气泡容器 / 水平仪区域
│   ├── bubble_center_marker_   // 中心准星（十字 + 小圆）
│   ├── bubble_h_axis_          // 水平刻度线
│   ├── bubble_v_axis_          // 垂直刻度线
│   ├── bubble_obj_             // 气泡主体（移动的圆）
│   └── bubble_glow_            // 气泡光晕（呼吸效果）
│
├── 气泡状态
│   ├── bubble_offset_x_        // 当前 X 偏移（-60 ~ +60）
│   ├── bubble_offset_y_        // 当前 Y 偏移（-60 ~ +60）
│   ├── bubble_level_           // 倾斜等级 (0-4)
│   └── bubble_color_level_     // 当前颜色等级（防抖动）
│
└── 方法
    ├── CreateBubbleAndCrosshair()  // 创建气泡和刻度
    ├── SetBubbleOffset(x, y)       // 设置气泡位置（带动画）
    ├── SetBubbleLevel(level)       // 设置倾斜等级（改变颜色）
    └── AnimateBubbleTo(x, y)       // 平滑移动动画（内部实现）
```

**Step 3.2: 气泡静态绘制**

**气泡规格**:

| 属性 | 规格 |
|------|------|
| 气泡直径 | 30 px |
| 气泡中心点 | (180, 180) 初始 |
| 移动范围 | ±60 px（半径 120 的圆内） |
| 主体颜色 | 翠绿色 `#00ff88`（0级倾斜） |
| 边框 | 深绿色 `#00aa55`，2 px |
| 阴影 | 向下偏移 3 px，模糊 5 px |
| 高光圆点 | 左上角小亮点（白色 40%） |

**中心准星规格**:

| 元素 | 规格 |
|------|------|
| 外圆 | 直径 40 px，金色 `#ffd700` 20% 透明，1 px 边框 |
| 水平线 | 长度 100 px，宽度 2 px，金色 `#ffd700` |
| 垂直线 | 长度 100 px，宽度 2 px，金色 `#ffd700` |
| 中心点 | 直径 6 px，金色 `#ffd700` |

**实现 CreateBubbleAndCrosshair()**:

```
├── // 1. 创建水平刻度线（长 100px，位于 y=180）
├── bubble_h_axis_ = lv_obj_create(attitude_container_);
├── lv_obj_set_size(bubble_h_axis_, 100, 2);  // 长 100，宽 2
├── lv_obj_set_pos(bubble_h_axis_, 130, 179);  // x = 180-50, y = 180-1
├── lv_obj_set_style_radius(bubble_h_axis_, 1, 0);
├── lv_obj_set_style_bg_color(bubble_h_axis_, lv_color_hex(0xffd700), 0);
├── lv_obj_set_style_border_width(bubble_h_axis_, 0, 0);
├── lv_obj_set_style_bg_opa(bubble_h_axis_, LV_OPA_60, 0);
│
├── // 2. 创建垂直刻度线（长 100px，位于 x=180）
├── bubble_v_axis_ = lv_obj_create(attitude_container_);
├── lv_obj_set_size(bubble_v_axis_, 2, 100);  // 宽 2，高 100
├── lv_obj_set_pos(bubble_v_axis_, 179, 130);  // x = 180-1, y = 180-50
├── lv_obj_set_style_radius(bubble_v_axis_, 1, 0);
├── lv_obj_set_style_bg_color(bubble_v_axis_, lv_color_hex(0xffd700), 0);
├── lv_obj_set_style_border_width(bubble_v_axis_, 0, 0);
├── lv_obj_set_style_bg_opa(bubble_v_axis_, LV_OPA_60, 0);
│
├── // 3. 创建中心准星标记（外圆）
├── bubble_center_marker_ = lv_obj_create(attitude_container_);
├── lv_obj_set_size(bubble_center_marker_, 40, 40);
├── lv_obj_set_pos(bubble_center_marker_, 160, 160);  // (180-20, 180-20)
├── lv_obj_set_style_radius(bubble_center_marker_, 20, 0);  // 圆形
├── lv_obj_set_style_bg_opa(bubble_center_marker_, LV_OPA_TRANSP, 0);
├── lv_obj_set_style_border_width(bubble_center_marker_, 1, 0);
├── lv_obj_set_style_border_color(bubble_center_marker_, lv_color_hex(0xffd700), 0);
├── lv_obj_set_style_border_opa(bubble_center_marker_, LV_OPA_40, 0);
│
├── // 4. 创建中心点（小圆点，标记精确中心）
├── lv_obj_t* center_dot = lv_obj_create(attitude_container_);
├── lv_obj_set_size(center_dot, 6, 6);
├── lv_obj_set_pos(center_dot, 177, 177);  // (180-3, 180-3)
├── lv_obj_set_style_radius(center_dot, 3, 0);
├── lv_obj_set_style_bg_color(center_dot, lv_color_hex(0xffd700), 0);
├── lv_obj_set_style_border_width(center_dot, 0, 0);
├── lv_obj_set_style_bg_opa(center_dot, LV_OPA_80, 0);
│
├── // 5. 创建气泡主体（直径 30px 的圆）
├── bubble_obj_ = lv_obj_create(attitude_container_);
├── lv_obj_set_size(bubble_obj_, 30, 30);
├── lv_obj_set_pos(bubble_obj_, 165, 165);  // 初始位置：中心 (180-15, 180-15)
├── lv_obj_set_style_radius(bubble_obj_, 15, 0);  // 半径 15
├── lv_obj_set_style_bg_color(bubble_obj_, lv_color_hex(0x00ff88), 0);  // 翠绿
├── lv_obj_set_style_border_width(bubble_obj_, 2, 0);
├── lv_obj_set_style_border_color(bubble_obj_, lv_color_hex(0x00aa55), 0);  // 深绿
├── lv_obj_set_style_bg_opa(bubble_obj_, LV_OPA_90, 0);
│
├── // 6. 气泡内部高光（小圆点，模拟反光）
├── lv_obj_t* highlight = lv_obj_create(bubble_obj_);
├── lv_obj_set_size(highlight, 6, 6);
├── lv_obj_set_pos(highlight, 6, 6);  // 左上角
├── lv_obj_set_style_radius(highlight, 3, 0);
├── lv_obj_set_style_bg_color(highlight, lv_color_hex(0xffffff), 0);
├── lv_obj_set_style_border_width(highlight, 0, 0);
├── lv_obj_set_style_bg_opa(highlight, LV_OPA_40, 0);
│
├── // 初始化气泡位置
├── bubble_offset_x_ = 0.0f;
├── bubble_offset_y_ = 0.0f;
│
└── ESP_LOGI(TAG, "Bubble and crosshair created");
```

**关于 z-order 和绘制顺序**:
- 先创建的对象在底部，后创建的在顶部
- 正确顺序：背景 → 装饰圆 → 刻度线 → 准星 → 中心点 → 气泡（最上层）
- 在 SetupUI() 中按此顺序调用创建方法

**验证**: 屏幕中央显示金色十字线+准星圆环+绿色圆形气泡，气泡在中心位置

---

**Step 3.3: 十字刻度线（增强版）**

设计在 120 像素半径圆周上的小刻度（可选，增加视觉丰富度）

**设计规格**:

```
方向标记:
├── 北 (N): 正上方，(180, 70) → 文字 "N"
├── 东 (E): 正右方，(290, 170) → 文字 "E"
├── 南 (S): 正下方，(175, 290) → 文字 "S"
└── 西 (W): 正左方，(70, 170) → 文字 "W"

小刻度线（每 15 度一条，在半径 120 的圆上）:
└── 创建 8 个小线段（每隔 45° 或 30°）
```

**实现（简化版）**:

```
在 CreateBubbleAndCrosshair() 末尾添加：
├── // 方向文字标记
├── lv_obj_t* dir_n = lv_label_create(attitude_container_);
├── lv_label_set_text(dir_n, "N");
├── lv_obj_set_style_text_color(dir_n, lv_color_hex(0xffd700), 0);
├── lv_obj_set_style_text_font(dir_n, BUILTIN_TEXT_FONT, 0);
├── lv_obj_set_pos(dir_n, 175, 70);  // 正上方
│
├── lv_obj_t* dir_e = lv_label_create(attitude_container_);
├── lv_label_set_text(dir_e, "E");
├── lv_obj_set_style_text_color(dir_e, lv_color_hex(0xffd700), 0);
├── lv_obj_set_style_text_font(dir_e, BUILTIN_TEXT_FONT, 0);
├── lv_obj_set_pos(dir_e, 290, 170);  // 正右方
│
├── lv_obj_t* dir_s = lv_label_create(attitude_container_);
├── lv_label_set_text(dir_s, "S");
├── lv_obj_set_style_text_color(dir_s, lv_color_hex(0xffd700), 0);
├── lv_obj_set_style_text_font(dir_s, BUILTIN_TEXT_FONT, 0);
├── lv_obj_set_pos(dir_s, 175, 290);  // 正下方
│
├── lv_obj_t* dir_w = lv_label_create(attitude_container_);
├── lv_label_set_text(dir_w, "W");
├── lv_obj_set_style_text_color(dir_w, lv_color_hex(0xffd700), 0);
├── lv_obj_set_style_text_font(dir_w, BUILTIN_TEXT_FONT, 0);
├── lv_obj_set_pos(dir_w, 70, 170);  // 正左方
```

**验证**: 屏幕上显示 N/E/S/W 四个金色方向标记

---

**Step 3.4: 气泡光晕效果**

**设计**: 在气泡外部添加一个更大的、半透明的圆作为光晕

| 层 | 尺寸 | 颜色 | 透明度 |
|----|------|------|--------|
| 外层光晕 | 直径 50 px | 气泡颜色 | 15% |
| 中层光晕 | 直径 40 px | 气泡颜色 | 25% |
| 气泡主体 | 直径 30 px | 气泡颜色 | 90% |

**实现思路**:
- 在气泡 `bubble_obj_` 之前（或同一个父容器中）创建 2-3 个透明圆
- 这些圆与气泡同心，但更大
- 透明度梯度递减

**实现代码**:

```
新增成员变量：
├── lv_obj_t* bubble_glow_outer_ = nullptr;  // 外层光晕
└── lv_obj_t* bubble_glow_inner_ = nullptr;  // 内层光晕

在 CreateBubbleAndCrosshair() 中，创建 bubble_obj_ 之前添加：
├── // 外层光晕（最大，最不透明）
├── bubble_glow_outer_ = lv_obj_create(attitude_container_);
├── lv_obj_set_size(bubble_glow_outer_, 50, 50);
├── lv_obj_set_pos(bubble_glow_outer_, 155, 155);  // (180-25, 180-25)
├── lv_obj_set_style_radius(bubble_glow_outer_, 25, 0);
├── lv_obj_set_style_bg_color(bubble_glow_outer_, lv_color_hex(0x00ff88), 0);
├── lv_obj_set_style_border_width(bubble_glow_outer_, 0, 0);
├── lv_obj_set_style_bg_opa(bubble_glow_outer_, LV_OPA_15, 0);  // 15% 透明
│
├── // 内层光晕
├── bubble_glow_inner_ = lv_obj_create(attitude_container_);
├── lv_obj_set_size(bubble_glow_inner_, 40, 40);
├── lv_obj_set_pos(bubble_glow_inner_, 160, 160);  // (180-20, 180-20)
├── lv_obj_set_style_radius(bubble_glow_inner_, 20, 0);
├── lv_obj_set_style_bg_color(bubble_glow_inner_, lv_color_hex(0x00ff88), 0);
├── lv_obj_set_style_border_width(bubble_glow_inner_, 0, 0);
├── lv_obj_set_style_bg_opa(bubble_glow_inner_, LV_OPA_25, 0);  // 25% 透明
```

**光晕跟随气泡移动**:
- 在 `SetBubbleOffset()` 中更新光晕位置时，需要同时移动光晕
- 光晕位置 = 气泡位置 - (光晕尺寸 - 气泡尺寸) / 2

**验证**: 气泡周围有淡绿色的光晕效果（在深色背景上）

---

**Step 3.5: 气泡平滑移动动画**

**动画系统设计**:

```
位置更新规则：
├── 输入：目标 X/Y 偏移（像素）
├── 限制：最大偏移 60 像素（限制在半径 120 的圆内）
├── 当前位置 → 目标位置
├── 动画时长：200-500ms（取决于移动距离）
└── 缓动函数：ease_out（先快后慢，自然感）

手动测试方法：
├── 暂时写一个定时器（esp_timer），每 2 秒切换一次气泡位置
├── 例如：中心 → 右上 → 中心 → 左下 → 中心...
└── 验证动画平滑性
```

**实现 SetBubbleOffset() 和 AnimateBubbleTo()**:

```
新增成员变量：
├── lv_anim_t bubble_x_anim_;  // X 方向动画
├── lv_anim_t bubble_y_anim_;  // Y 方向动画

声明方法：
├── void SetBubbleOffset(float x_offset, float y_offset);  // 设置新目标位置
├── void UpdateBubblePositionNow(float x_offset, float y_offset);  // 立即更新（无动画）

实现 SetBubbleOffset():
├── // 限制偏移量（最大 60 像素）
├── float distance = sqrt(x_offset * x_offset + y_offset * y_offset);
├── if (distance > 60.0f) {
│   ├── float scale = 60.0f / distance;
│   ├── x_offset *= scale;
│   └── y_offset *= scale;
└── }
│
├── // 计算动画时长（距离越远，时间越长，但有上限）
├── uint32_t anim_time = static_cast<uint32_t>(distance * 5);  // 5ms/像素
├── anim_time = std::min(anim_time, (uint32_t)500);  // 最多 500ms
│
├── // 设置 X 动画
├── lv_anim_init(&bubble_x_anim_);
├── lv_anim_set_var(&bubble_x_anim_, this);  // 设置动画关联对象
├── lv_anim_set_values(&b