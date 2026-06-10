# ESP32 LVGL UI 界面构建指导文档

## 1. 概述

本项目使用 **LVGL (Light and Versatile Graphics Library)** 作为 ESP32 设备的图形用户界面库。LVGL 是一个开源的嵌入式图形库，专为资源受限的设备设计，提供丰富的 UI 组件和强大的图形渲染能力。

## 2. 依赖配置

### 2.1 组件依赖声明

在 `main/idf_component.yml` 中声明 LVGL 相关依赖：

```yaml
dependencies:
  lvgl/lvgl: ~9.5.0
  esp_lvgl_port: ~2.7.2
```

### 2.2 CMake 配置

在 `main/CMakeLists.txt` 中确保 LVGL 组件被正确链接。

## 3. 显示类型架构

### 3.1 显示类型继承关系

```
Display (基类)
└── LvglDisplay (LVGL 显示基类)
    └── LcdDisplay (LCD 显示基类)
        ├── SpiLcdDisplay (SPI 接口 LCD)
        ├── RgbLcdDisplay (RGB 接口 LCD)
        └── MipiLcdDisplay (MIPI DSI 接口 LCD)
```

### 3.2 各显示类型适用场景

| 显示类型 | 接口类型 | 适用场景 | 特点 |
|---------|---------|---------|------|
| `SpiLcdDisplay` | SPI | 小尺寸屏 (1.54" - 3.5") | 接线简单，速率适中 |
| `RgbLcdDisplay` | RGB Parallel | 中大屏 (4.3"+) | 高速刷新，适合视频 |
| `MipiLcdDisplay` | MIPI DSI | 高分辨率屏 | 低功耗，高速率 |

## 4. LVGL 初始化流程

### 4.1 标准初始化步骤

```cpp
// 1. 初始化 LVGL 库
lv_init();

// 2. 配置 LVGL port
lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
port_cfg.task_priority = 1;
port_cfg.task_affinity = 1;  // 多核 CPU 设置核心绑定
lvgl_port_init(&port_cfg);

// 3. 添加显示设备
const lvgl_port_display_cfg_t display_cfg = {
    .io_handle = panel_io_,
    .panel_handle = panel_,
    .buffer_size = static_cast<uint32_t>(width_ * 20),
    .double_buffer = false,
    .hres = static_cast<uint32_t>(width_),
    .vres = static_cast<uint32_t>(height_),
    .monochrome = false,
    .rotation = {
        .swap_xy = swap_xy,
        .mirror_x = mirror_x,
        .mirror_y = mirror_y,
    },
    .color_format = LV_COLOR_FORMAT_RGB565,
    .flags = {
        .buff_dma = 1,
        .buff_spiram = 0,
        .sw_rotate = 0,
        .swap_bytes = 1,
        .full_refresh = 0,
        .direct_mode = 0,
    },
};

display_ = lvgl_port_add_disp(&display_cfg);
```

### 4.2 SPI LCD 初始化示例

参考 `main/boards/waveshare/esp32-s3-touch-lcd-1.54/esp32-s3-touch-lcd-1.54.cc`：

```cpp
void InitializeLcdDisplay() {
    // 初始化 SPI 总线
    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
    buscfg.miso_io_num = DISPLAY_MISO_PIN;
    buscfg.sclk_io_num = DISPLAY_CLK_PIN;
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // 创建面板 IO
    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.cs_gpio_num = DISPLAY_CS_PIN;
    io_config.dc_gpio_num = DISPLAY_DC_PIN;
    io_config.pclk_hz = 40 * 1000 * 1000;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

    // 创建面板驱动
    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = DISPLAY_RST_PIN;
    panel_config.bits_per_pixel = 16;
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));

    // 面板初始化
    esp_lcd_panel_reset(panel);
    esp_lcd_panel_init(panel);
    esp_lcd_panel_invert_color(panel, true);

    // 创建 LVGL 显示对象
    display_ = new SpiLcdDisplay(panel_io, panel, 
                                DISPLAY_WIDTH, DISPLAY_HEIGHT, 
                                DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
                                DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
}
```

## 5. UI 组件架构

### 5.1 核心 UI 组件

| 组件 | 变量名 | 功能描述 |
|-----|-------|---------|
| 顶部状态栏 | `top_bar_` | 显示网络状态、静音、电量图标 |
| 状态标签 | `status_label_` | 显示系统状态文本（滚动显示） |
| 通知标签 | `notification_label_` | 显示临时通知消息 |
| 内容区域 | `content_` | 聊天消息显示区域（仅微信风格） |
| 表情标签 | `emoji_label_` | 显示静态表情图标 |
| 表情图片 | `emoji_image_` | 显示动态 GIF 表情 |
| 底部消息栏 | `bottom_bar_` | 显示聊天消息文本 |
| 低电量弹窗 | `low_battery_popup_` | 低电量警告提示 |

### 5.2 UI 层级结构

```
screen (根容器)
├── container_ (背景容器)
│   ├── top_bar_ (顶部状态栏)
│   │   ├── network_label_ (网络图标)
│   │   └── right_icons (右侧图标容器)
│   │       ├── mute_label_ (静音图标)
│   │       └── battery_label_ (电量图标)
│   ├── status_bar_ (状态标签层 - 与 top_bar_ 重叠)
│   │   ├── notification_label_ (通知文本)
│   │   └── status_label_ (状态文本)
│   └── content_ (聊天内容区域 - 微信风格)
│       └── message_bubbles (消息气泡)
├── emoji_box_ (表情显示容器)
│   ├── emoji_label_ (静态表情)
│   └── emoji_image_ (GIF 表情)
├── preview_image_ (预览图片)
├── bottom_bar_ (底部消息栏)
│   └── chat_message_label_ (消息文本)
└── low_battery_popup_ (低电量弹窗)
    └── low_battery_label_ (提示文本)
```

## 6. 主题系统

### 6.1 主题类结构

`LvglTheme` 类管理主题配置：

```cpp
class LvglTheme : public Theme {
    // 颜色配置
    lv_color_t background_color_;      // 背景色
    lv_color_t text_color_;            // 文本色
    lv_color_t chat_background_color_; // 聊天背景色
    lv_color_t user_bubble_color_;     // 用户消息气泡色
    lv_color_t assistant_bubble_color_;// 助手消息气泡色
    lv_color_t system_bubble_color_;   // 系统消息气泡色
    lv_color_t border_color_;          // 边框色
    lv_color_t low_battery_color_;     // 低电量警告色

    // 字体配置
    std::shared_ptr<LvglFont> text_font_;       // 文本字体
    std::shared_ptr<LvglFont> icon_font_;       // 图标字体
    std::shared_ptr<LvglFont> large_icon_font_; // 大图标字体
};
```

### 6.2 内置主题

项目默认提供两种主题：

**Light Theme (亮色主题)**：
- 背景：白色 (#FFFFFF)
- 文本：黑色 (#000000)
- 用户气泡：绿色 (#00FF00)
- 助手气泡：浅灰 (#DDDDDD)

**Dark Theme (暗色主题)**：
- 背景：黑色 (#000000)
- 文本：白色 (#FFFFFF)
- 用户气泡：绿色 (#00FF00)
- 助手气泡：深灰 (#222222)

### 6.3 主题切换

```cpp
void LcdDisplay::SetTheme(Theme* theme) {
    DisplayLockGuard lock(this);
    auto lvgl_theme = static_cast<LvglTheme*>(theme);
    
    // 更新屏幕样式
    lv_obj_t* screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, lvgl_theme->text_font()->font(), 0);
    lv_obj_set_style_text_color(screen, lvgl_theme->text_color(), 0);
    lv_obj_set_style_bg_color(screen, lvgl_theme->background_color(), 0);
    
    // 更新各个组件样式...
}
```

## 7. 消息显示模式

### 7.1 微信消息风格 (CONFIG_USE_WECHAT_MESSAGE_STYLE)

启用此配置后，消息以气泡形式显示在内容区域：

```cpp
void LcdDisplay::SetChatMessage(const char* role, const char* content) {
    // 创建消息气泡
    lv_obj_t* msg_bubble = lv_obj_create(content_);
    lv_obj_set_style_radius(msg_bubble, 8, 0);
    
    lv_obj_t* msg_text = lv_label_create(msg_bubble);
    lv_label_set_text(msg_text, content);
    
    // 根据角色设置样式
    if (strcmp(role, "user") == 0) {
        // 用户消息：右对齐，绿色背景
        lv_obj_set_style_bg_color(msg_bubble, lvgl_theme->user_bubble_color(), 0);
        lv_obj_align(msg_bubble, LV_ALIGN_RIGHT_MID, -25, 0);
    } else if (strcmp(role, "assistant") == 0) {
        // 助手消息：左对齐，灰色背景
        lv_obj_set_style_bg_color(msg_bubble, lvgl_theme->assistant_bubble_color(), 0);
        lv_obj_align(msg_bubble, LV_ALIGN_LEFT_MID, 0, 0);
    }
    
    // 自动滚动到最新消息
    lv_obj_scroll_to_view_recursive(msg_bubble, LV_ANIM_ON);
}
```

### 7.2 单行滚动风格 (默认)

消息显示在底部固定高度的消息栏中，过长文本自动滚动：

```cpp
void LcdDisplay::SetChatMessage(const char* role, const char* content) {
    lv_label_set_text(chat_message_label_, content);
    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    
    // 显示/隐藏底部栏
    if (content == nullptr || content[0] == '\0') {
        lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
    }
}
```

## 8. 表情与动画

### 8.1 表情显示

```cpp
void LcdDisplay::SetEmotion(const char* emotion) {
    // 尝试从 emoji 集合获取图像
    auto emoji_collection = static_cast<LvglTheme*>(current_theme_)->emoji_collection();
    auto image = emoji_collection->GetEmojiImage(emotion);
    
    if (image != nullptr) {
        if (image->IsGif()) {
            // GIF 动画处理
            gif_controller_ = std::make_unique<LvglGif>(image->image_dsc());
            gif_controller_->SetFrameCallback([this]() {
                lv_image_set_src(emoji_image_, gif_controller_->image_dsc());
            });
            gif_controller_->Start();
        } else {
            // 静态图片
            lv_image_set_src(emoji_image_, image->image_dsc());
        }
    } else {
        // 使用字体图标
        const char* utf8 = font_awesome_get_utf8(emotion);
        lv_label_set_text(emoji_label_, utf8);
    }
}
```

### 8.2 支持的表情类型

| 表情类型 | 示例 | 数据源 |
|---------|------|--------|
| Font Awesome 图标 | "happy", "sad", "sleepy" | 字体文件 |
| PNG 图片 | 自定义表情 | SPIFFS 资源 |
| GIF 动画 | 动态表情 | SPIFFS 资源 |

## 9. 状态指示器

### 9.1 电量指示

```cpp
void LvglDisplay::UpdateStatusBar(bool update_all) {
    int battery_level;
    bool charging, discharging;
    
    if (board.GetBatteryLevel(battery_level, charging, discharging)) {
        const char* icon;
        if (charging) {
            icon = FONT_AWESOME_BATTERY_BOLT;
        } else {
            const char* levels[] = {
                FONT_AWESOME_BATTERY_EMPTY,      // 0-19%
                FONT_AWESOME_BATTERY_QUARTER,    // 20-39%
                FONT_AWESOME_BATTERY_HALF,       // 40-59%
                FONT_AWESOME_BATTERY_THREE_QUARTERS, // 60-79%
                FONT_AWESOME_BATTERY_FULL,       // 80-100%
            };
            icon = levels[battery_level / 20];
        }
        lv_label_set_text(battery_label_, icon);
    }
}
```

### 9.2 网络状态指示

```cpp
const char* Board::GetNetworkStateIcon() {
    switch (GetNetworkState()) {
        case kNetworkStateConnected:
            return FONT_AWESOME_WIFI;
        case kNetworkStateConnecting:
            return FONT_AWESOME_WIFI_0;
        case kNetworkStateDisconnected:
            return FONT_AWESOME_WIFI_XMARK;
        default:
            return nullptr;
    }
}
```

## 10. 性能优化

### 10.1 内存优化

```cpp
#if CONFIG_SPIRAM
// 使用 PSRAM 作为图像缓存
size_t psram_size_mb = esp_psram_get_size() / 1024 / 1024;
if (psram_size_mb >= 8) {
    lv_image_cache_resize(2 * 1024 * 1024, true);  // 2MB 缓存
} else if (psram_size_mb >= 2) {
    lv_image_cache_resize(512 * 1024, true);       // 512KB 缓存
}
#endif
```

### 10.2 显示锁机制

使用 `DisplayLockGuard` 确保 LVGL 操作的线程安全：

```cpp
void LcdDisplay::SetStatus(const char* status) {
    DisplayLockGuard lock(this);  // 自动加锁
    lv_label_set_text(status_label_, status);
    // 自动解锁（作用域结束时）
}
```

### 10.3 电源管理

```cpp
// 创建电源管理锁，防止显示更新时 CPU 降频
auto ret = esp_pm_lock_create(ESP_PM_APB_FREQ_MAX, 0, "display_update", &pm_lock_);

// 更新状态栏时获取锁
esp_pm_lock_acquire(pm_lock_);
// ... 更新操作 ...
esp_pm_lock_release(pm_lock_);
```

## 11. 截图功能

```cpp
bool LvglDisplay::SnapshotToJpeg(std::string& jpeg_data, int quality) {
#if CONFIG_LV_USE_SNAPSHOT
    DisplayLockGuard lock(this);
    
    // 获取当前屏幕快照
    lv_obj_t* screen = lv_screen_active();
    lv_draw_buf_t* draw_buffer = lv_snapshot_take(screen, LV_COLOR_FORMAT_RGB565);
    
    // 转换为 JPEG
    bool ret = image_to_jpeg_cb((uint8_t*)draw_buffer->data, 
                                draw_buffer->data_size, 
                                draw_buffer->header.w, 
                                draw_buffer->header.h, 
                                V4L2_PIX_FMT_RGB565, 
                                quality, callback, &jpeg_data);
    
    lv_draw_buf_destroy(draw_buffer);
    return ret;
#endif
}
```

## 12. 配置选项

### 12.1 Kconfig 配置

| 配置项 | 说明 | 默认值 |
|-------|------|--------|
| `CONFIG_USE_WECHAT_MESSAGE_STYLE` | 启用微信消息风格 | n |
| `CONFIG_USE_MULTILINE_CHAT_MESSAGE` | 启用多行消息显示 | n |
| `CONFIG_LV_USE_SNAPSHOT` | 启用截图功能 | y |
| `CONFIG_SPIRAM` | 启用 PSRAM 支持 | 视硬件而定 |

### 12.2 消息数量限制

```cpp
#if CONFIG_IDF_TARGET_ESP32P4
#define MAX_MESSAGES 40  // P4 平台内存较大
#else
#define MAX_MESSAGES 20  // 其他平台
#endif
```

## 13. 实际应用示例

### 13.1 完整的板级配置示例

参考 `main/boards/waveshare/esp32-s3-touch-lcd-1.54/esp32-s3-touch-lcd-1.54.cc`：

```cpp
class CustomBoard : public WifiBoard {
private:
    LcdDisplay* display_;
    
    void InitializeLcdDisplay() {
        // SPI 初始化
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.sclk_io_num = DISPLAY_CLK_PIN;
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));

        // 面板 IO 配置
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.pclk_hz = 40 * 1000 * 1000;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        // 面板初始化
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));
        
        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);

        // 创建 LVGL 显示
        display_ = new SpiLcdDisplay(panel_io, panel, 
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                    DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
                                    DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }
    
public:
    virtual Display* GetDisplay() override {
        return display_;
    }
};

DECLARE_BOARD(CustomBoard);
```

### 13.2 UI 更新示例

```cpp
// 设置状态文本
display->SetStatus("正在连接网络...");

// 显示通知
display->ShowNotification("音量已调至最大", 2000);

// 设置聊天消息
display->SetChatMessage("user", "你好！");
display->SetChatMessage("assistant", "您好！我是智能助手，请问有什么可以帮助您的？");

// 设置表情
display->SetEmotion("happy");

// 清除聊天记录
display->ClearChatMessages();
```

## 14. 常见问题

### 14.1 LVGL 任务优先级

确保 LVGL 任务优先级合理，避免阻塞其他重要任务：

```cpp
port_cfg.task_priority = 1;  // 较低优先级
```

### 14.2 显示闪烁问题

- 确保使用 DMA 缓冲 (`buff_dma = 1`)
- 考虑使用双缓冲 (`double_buffer = true`)
- RGB 显示启用 `avoid_tearing` 选项

### 14.3 内存不足

- 减小 `buffer_size`
- 启用 PSRAM 支持
- 限制最大消息数量

### 14.4 触摸不响应

- 检查触摸控制器初始化顺序（需在 LVGL 显示初始化之后）
- 确认触摸坐标配置正确

```cpp
const lvgl_port_touch_cfg_t touch_cfg = {
    .disp = lv_display_get_default(),  // 必须在显示初始化后调用
    .handle = touch_handle,
};
lvgl_port_add_touch(&touch_cfg);
```

## 15. 参考文档

- [LVGL 官方文档](https://lvgl.io/docs)
- [ESP-LVGL Port 文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/lvgl.html)
- [ESP LCD 驱动文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/lcd.html)

---

**文档版本**: v1.0  
**生成日期**: 2024年  
**适用项目**: xiaozhi-esp32