#include "attitude_display.h"
#include "lvgl_theme.h"
#include <esp_log.h>
#include <cstdio>
#include <inttypes.h>

#define TAG "AttitudeDisplay"

LV_FONT_DECLARE(BUILTIN_TEXT_FONT);

// 构造函数：调用 SpiLcdDisplay 基类构造，它会自动初始化 LVGL
AttitudeDisplay::AttitudeDisplay(esp_lcd_panel_io_handle_t panel_io,
                                 esp_lcd_panel_handle_t panel,
                                 int width, int height,
                                 int offset_x, int offset_y,
                                 bool mirror_x, bool mirror_y, bool swap_xy)
    : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y, swap_xy)
{
    ESP_LOGI(TAG, "AttitudeDisplay constructed, %dx%d", width, height);
}

void AttitudeDisplay::SetupUI()
{
    // 1. 防止重复调用（参考 LcdDisplay）
    if (IsSetupUICalled()) {
        ESP_LOGW(TAG, "SetupUI() already called, skipping");
        return;
    }

    // 2. 调用基类标记 SetupUI 被调用（Display::SetupUI()）
    // 注意：不要调用 LcdDisplay::SetupUI()，它会创建现有 Lcd UI
    Display::SetupUI();

    // 3. 获取 LVGL 锁（线程安全）
    DisplayLockGuard lock(this);

    // 4. 获取主题
    auto lvgl_theme = static_cast<LvglTheme*>(GetTheme());
    if (lvgl_theme == nullptr) {
        ESP_LOGE(TAG, "Theme is null!");
        return;
    }

    // 5. 设置屏幕基础样式
    auto screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, lvgl_theme->text_font()->font(), 0);
    lv_obj_set_style_text_color(screen, lvgl_theme->text_color(), 0);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);  // 黑色背景

    // 6. 创建主容器（360×360，覆盖整个屏幕）
    attitude_container_ = lv_obj_create(screen);
    lv_obj_set_size(attitude_container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_radius(attitude_container_, 0, 0);  // 方形容器，后续迭代做圆角
    lv_obj_set_style_border_width(attitude_container_, 0, 0);
    lv_obj_set_style_pad_all(attitude_container_, 0, 0);
    lv_obj_set_style_bg_color(attitude_container_, lv_color_hex(0x0d1b2a), 0);  // 深蓝

    // 7. 创建测试布局
    CreateTestLayout();

    ESP_LOGI(TAG, "SetupUI completed");
}

void AttitudeDisplay::CreateTestLayout()
{
    // 创建一个居中的测试标签
    test_label_ = lv_label_create(attitude_container_);
    lv_label_set_text(test_label_, "Attitude Display Init OK");
    lv_obj_set_style_text_font(test_label_, &BUILTIN_TEXT_FONT, 0);
    lv_obj_set_style_text_color(test_label_, lv_color_hex(0x00ff88), 0);  // 翠绿色
    lv_obj_center(test_label_);  // 居中显示

    // 再创建一个副标签，显示屏幕尺寸
    char size_text[64];
    snprintf(size_text, sizeof(size_text), "Screen: %" PRIi32 "x%" PRIi32, LV_HOR_RES, LV_VER_RES);
    lv_obj_t* size_label = lv_label_create(attitude_container_);
    lv_label_set_text(size_label, size_text);
    lv_obj_set_style_text_color(size_label, lv_color_hex(0xaaaaaa), 0);  // 浅灰色
    lv_obj_align_to(size_label, test_label_, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);

    ESP_LOGI(TAG, "Test layout created, label=%p", test_label_);
}

void AttitudeDisplay::SetAttitudeData(float pitch, float roll, float yaw)
{
    // 第一版：仅记录数据，不做任何显示
    current_pitch_ = pitch;
    current_roll_ = roll;
    current_yaw_ = yaw;
    // 后续迭代会在这里更新 UI
}

void AttitudeDisplay::SetInterpretation(const std::string& text)
{
    // 第一版：空实现
    ESP_LOGD(TAG, "Interpretation: %s", text.c_str());
}

void AttitudeDisplay::SetTheme(Theme* theme)
{
    // 先调用基类设置主题
    Display::SetTheme(theme);
    // 然后应用到姿态 UI（第一版空实现）
    ApplyThemeToAttitudeUI();
}

void AttitudeDisplay::ApplyThemeToAttitudeUI()
{
    // 第一版：空实现，后续迭代会在这里更新所有元素的颜色
}
