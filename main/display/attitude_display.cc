#include "attitude_display.h"
#include "lvgl_theme.h"
#include "application.h"
#include "board.h"
#include <esp_log.h>
#include <cstdio>
#include <inttypes.h>
#include <ctime>
#include <font_awesome.h>

#define TAG "AttitudeDisplay"

LV_FONT_DECLARE(BUILTIN_TEXT_FONT);
LV_FONT_DECLARE(BUILTIN_ICON_FONT);

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
    // 1. 防止重复调用
    if (IsSetupUICalled()) {
        ESP_LOGW(TAG, "SetupUI() already called, skipping");
        return;
    }

    // 2. 调用基类标记 SetupUI 被调用
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
    lv_obj_set_style_radius(attitude_container_, 0, 0);
    lv_obj_set_style_border_width(attitude_container_, 0, 0);
    lv_obj_set_style_pad_all(attitude_container_, 0, 0);
    lv_obj_set_style_bg_color(attitude_container_, lv_color_hex(0x050a14), 0);  // 深色底

    // 7. 创建背景渐变（迭代2 Step 2.1）
    CreateBackground();

    // 8. 创建装饰圆（迭代2 Step 2.2）
    CreateDecorationCircles();

    // 9. 创建顶部信息栏（迭代2 Step 2.3）
    CreateTopInfoRing();

    // 10. 创建底部解读区域（迭代2 Step 2.4）
    CreateBottomInterpretation();

    ESP_LOGI(TAG, "SetupUI completed");
}

// Step 2.1: 深色径向渐变背景绘制
void AttitudeDisplay::CreateBackground()
{
    // 创建背景容器（全屏）
    background_ = lv_obj_create(attitude_container_);
    lv_obj_set_size(background_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_radius(background_, 0, 0);
    lv_obj_set_style_border_width(background_, 0, 0);
    lv_obj_set_style_pad_all(background_, 0, 0);
    lv_obj_set_style_bg_color(background_, lv_color_hex(0x050a14), 0);  // 深色底（边缘颜色）
    lv_obj_set_style_bg_opa(background_, LV_OPA_100, 0);

    // 创建中心高亮层（模拟径向渐变中心）
    bg_layer_center_ = lv_obj_create(background_);
    lv_obj_set_size(bg_layer_center_, 280, 280);  // 直径 280，半径 140
    lv_obj_set_style_radius(bg_layer_center_, 140, 0);  // 圆形
    lv_obj_set_style_border_width(bg_layer_center_, 0, 0);
    lv_obj_set_style_bg_color(bg_layer_center_, lv_color_hex(0x0d1b2a), 0);  // 中心颜色（较亮）
    lv_obj_set_style_bg_opa(bg_layer_center_, LV_OPA_100, 0);
    lv_obj_center(bg_layer_center_);

    // 创建内层高亮（更小的圆，更亮一点）
    bg_inner_glow_ = lv_obj_create(bg_layer_center_);
    lv_obj_set_size(bg_inner_glow_, 200, 200);
    lv_obj_set_style_radius(bg_inner_glow_, 100, 0);  // 圆形
    lv_obj_set_style_border_width(bg_inner_glow_, 0, 0);
    lv_obj_set_style_bg_color(bg_inner_glow_, lv_color_hex(0x142a40), 0);  // 更亮的蓝色
    lv_obj_set_style_bg_opa(bg_inner_glow_, LV_OPA_80, 0);  // 80%透明度
    lv_obj_center(bg_inner_glow_);

    // 将背景层移到最底层
    lv_obj_move_background(background_);

    ESP_LOGI(TAG, "Background created");
}

// Step 2.2: 同心圆装饰边框
void AttitudeDisplay::CreateDecorationCircles()
{
    // 外圈：半径 160 → 直径 320 → 位置 (20, 20)
    circle_outer_ = lv_obj_create(attitude_container_);
    lv_obj_set_size(circle_outer_, 320, 320);
    lv_obj_set_pos(circle_outer_, 20, 20);
    lv_obj_set_style_radius(circle_outer_, 160, 0);  // 圆形
    lv_obj_set_style_bg_opa(circle_outer_, LV_OPA_TRANSP, 0);  // 透明填充
    lv_obj_set_style_border_width(circle_outer_, 2, 0);  // 2px 边框
    lv_obj_set_style_border_color(circle_outer_, lv_color_hex(0xffd700), 0);  // 金色
    lv_obj_set_style_border_opa(circle_outer_, LV_OPA_50, 0);  // 50% 透明

    // 中圈：半径 140 → 直径 280 → 位置 (40, 40)
    circle_mid_ = lv_obj_create(attitude_container_);
    lv_obj_set_size(circle_mid_, 280, 280);
    lv_obj_set_pos(circle_mid_, 40, 40);
    lv_obj_set_style_radius(circle_mid_, 140, 0);
    lv_obj_set_style_bg_opa(circle_mid_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(circle_mid_, 1, 0);  // 1px 边框
    lv_obj_set_style_border_color(circle_mid_, lv_color_hex(0xffd700), 0);
    lv_obj_set_style_border_opa(circle_mid_, LV_OPA_30, 0);  // 30% 透明

    // 内圈：半径 120 → 直径 240 → 位置 (60, 60)
    circle_inner_ = lv_obj_create(attitude_container_);
    lv_obj_set_size(circle_inner_, 240, 240);
    lv_obj_set_pos(circle_inner_, 60, 60);
    lv_obj_set_style_radius(circle_inner_, 120, 0);
    lv_obj_set_style_bg_opa(circle_inner_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(circle_inner_, 1, 0);
    lv_obj_set_style_border_color(circle_inner_, lv_color_hex(0xffd700), 0);
    lv_obj_set_style_border_opa(circle_inner_, LV_OPA_20, 0);  // 20% 透明

    ESP_LOGI(TAG, "Decoration circles created");
}

// Step 2.3: 顶部信息环
void AttitudeDisplay::CreateTopInfoRing()
{
    // 创建顶部信息容器（透明背景）
    top_info_container_ = lv_obj_create(attitude_container_);
    lv_obj_set_size(top_info_container_, LV_HOR_RES, 40);  // 高度 40px
    lv_obj_set_pos(top_info_container_, 0, 10);  // 距顶部 10px
    lv_obj_set_style_radius(top_info_container_, 0, 0);
    lv_obj_set_style_bg_opa(top_info_container_, LV_OPA_TRANSP, 0);  // 透明
    lv_obj_set_style_border_width(top_info_container_, 0, 0);
    lv_obj_set_style_pad_all(top_info_container_, 0, 0);

    // 网络图标（左上角）
    network_icon_ = lv_label_create(top_info_container_);
    lv_label_set_text(network_icon_, "");  // 初始为空
    lv_obj_set_style_text_font(network_icon_, &BUILTIN_ICON_FONT, 0);
    lv_obj_set_style_text_color(network_icon_, lv_color_hex(0xaaaaaa), 0);
    lv_obj_set_pos(network_icon_, 30, 10);

    // 时间显示（顶部中央）
    time_label_ = lv_label_create(top_info_container_);
    lv_label_set_text(time_label_, "--:--");
    lv_obj_set_style_text_font(time_label_, &BUILTIN_TEXT_FONT, 0);
    lv_obj_set_style_text_color(time_label_, lv_color_hex(0xffffff), 0);
    lv_obj_center(time_label_);

    // 电量图标（右上角）
    battery_icon_ = lv_label_create(top_info_container_);
    lv_label_set_text(battery_icon_, "");  // 初始为空
    lv_obj_set_style_text_font(battery_icon_, &BUILTIN_ICON_FONT, 0);
    lv_obj_set_style_text_color(battery_icon_, lv_color_hex(0xaaaaaa), 0);
    lv_obj_set_pos(battery_icon_, LV_HOR_RES - 50, 10);

    ESP_LOGI(TAG, "Top info ring created");
}

// Step 2.4: 底部解读区域
void AttitudeDisplay::CreateBottomInterpretation()
{
    // 创建底部容器（使用flex布局水平排列内容）
    bottom_interpret_ = lv_obj_create(attitude_container_);
    lv_obj_set_size(bottom_interpret_, 320, 44);  // 宽度 320，高度 44
    lv_obj_set_pos(bottom_interpret_, 20, 308);   // 距离顶部 308px（底部区域）
    lv_obj_set_style_radius(bottom_interpret_, 22, 0);  // 圆角 22px
    lv_obj_set_style_bg_color(bottom_interpret_, lv_color_hex(0x1a2a3a), 0);  // 深蓝灰
    lv_obj_set_style_bg_opa(bottom_interpret_, LV_OPA_80, 0);  // 80% 不透明
    lv_obj_set_style_border_width(bottom_interpret_, 1, 0);
    lv_obj_set_style_border_color(bottom_interpret_, lv_color_hex(0x334455), 0);
    lv_obj_set_style_pad_hor(bottom_interpret_, 16, 0);  // 水平内边距
    lv_obj_set_style_pad_ver(bottom_interpret_, 0, 0);

    // 使用flex布局让内容水平居中
    lv_obj_set_flex_flow(bottom_interpret_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bottom_interpret_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // 状态图标（左侧小圆点，表示当前状态）
    interpret_icon_ = lv_obj_create(bottom_interpret_);
    lv_obj_set_size(interpret_icon_, 12, 12);
    lv_obj_set_style_radius(interpret_icon_, 6, 0);  // 圆形
    lv_obj_set_style_bg_color(interpret_icon_, lv_color_hex(0x00ff88), 0);  // 翠绿色
    lv_obj_set_style_border_width(interpret_icon_, 0, 0);
    // 使用flex布局，图标会自动垂直居中

    // 间距（图标和文字之间的空隙）
    lv_obj_t* gap = lv_obj_create(bottom_interpret_);
    lv_obj_set_size(gap, 8, 1);
    lv_obj_set_style_bg_opa(gap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(gap, 0, 0);

    // 解读文字（占据剩余空间）
    interpret_text_ = lv_label_create(bottom_interpret_);
    lv_label_set_text(interpret_text_, "当前状态：基本平衡 · 建议保持");
    lv_obj_set_style_text_font(interpret_text_, &BUILTIN_TEXT_FONT, 0);
    lv_obj_set_style_text_color(interpret_text_, lv_color_hex(0xffffff), 0);
    lv_obj_set_flex_grow(interpret_text_, 1);  // 占据剩余空间
    lv_label_set_long_mode(interpret_text_, LV_LABEL_LONG_SCROLL);  // 滚动显示

    ESP_LOGI(TAG, "Bottom interpretation area created");
}

// 重写 UpdateStatusBar
void AttitudeDisplay::UpdateStatusBar(bool update_all)
{
    auto& app = Application::GetInstance();
    auto& board = Board::GetInstance();

    DisplayLockGuard lock(this);

    // 1. 更新时间显示
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

    // 2. 更新电量图标
    int battery_level;
    bool charging, discharging;
    if (board.GetBatteryLevel(battery_level, charging, discharging)) {
        const char* icon = nullptr;
        if (charging) {
            icon = FONT_AWESOME_BATTERY_BOLT;
        } else {
            const char* levels[] = {
                FONT_AWESOME_BATTERY_EMPTY,
                FONT_AWESOME_BATTERY_QUARTER,
                FONT_AWESOME_BATTERY_HALF,
                FONT_AWESOME_BATTERY_THREE_QUARTERS,
                FONT_AWESOME_BATTERY_FULL,
                FONT_AWESOME_BATTERY_FULL,
            };
            icon = levels[battery_level / 20];
        }
        if (battery_icon_ != nullptr && icon != nullptr) {
            lv_label_set_text(battery_icon_, icon);
        }
    }

    // 3. 更新网络图标（每10秒）
    static int seconds_counter = 0;
    if (update_all || seconds_counter++ % 10 == 0) {
        const char* icon = board.GetNetworkStateIcon();
        if (network_icon_ != nullptr && icon != nullptr) {
            lv_label_set_text(network_icon_, icon);
        }
    }
}

void AttitudeDisplay::SetAttitudeData(float pitch, float roll, float yaw)
{
    current_pitch_ = pitch;
    current_roll_ = roll;
    current_yaw_ = yaw;
    // 后续迭代会在这里更新 UI
}

void AttitudeDisplay::SetInterpretation(const std::string& text)
{
    DisplayLockGuard lock(this);
    if (interpret_text_ != nullptr) {
        lv_label_set_text(interpret_text_, text.c_str());
    }
}

void AttitudeDisplay::SetTheme(Theme* theme)
{
    Display::SetTheme(theme);
    ApplyThemeToAttitudeUI();
}

void AttitudeDisplay::ApplyThemeToAttitudeUI()
{
    // 后续迭代会在这里更新所有元素的颜色
}