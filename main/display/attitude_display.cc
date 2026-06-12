#include "attitude_display.h"
#include "lvgl_theme.h"
#include "application.h"
#include "board.h"
#include <esp_log.h>
#include <cstdio>
#include <inttypes.h>
#include <ctime>
#include <cmath>
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

    // 5. 获取当前主题色值
    const auto& theme_colors = AttitudeTheme::GetInstance().GetColors();

    // 6. 设置屏幕基础样式
    auto screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, lvgl_theme->text_font()->font(), 0);
    lv_obj_set_style_text_color(screen, theme_colors.text_main, 0);
    lv_obj_set_style_bg_color(screen, theme_colors.bg_outer, 0);

    // 7. 创建圆形屏幕遮罩（360×360圆形屏幕适配）
    // 原理：绘制一个背景色大圆覆盖整个屏幕，遮住四角，让整个显示呈现圆形
    // 圆心：(180, 180)，半径：180（覆盖整个360×360屏幕）
    lv_obj_t* round_mask = lv_obj_create(screen);
    lv_obj_set_size(round_mask, 360, 360);
    lv_obj_set_pos(round_mask, 0, 0);
    lv_obj_set_style_radius(round_mask, 180, 0);  // 圆角等于边长的一半，形成完整圆
    lv_obj_set_style_bg_color(round_mask, theme_colors.bg_outer, 0);  // 背景色遮罩
    lv_obj_set_style_border_width(round_mask, 0, 0);  // 无边框
    lv_obj_set_style_clip_corner(round_mask, true, 0);  // 启用裁剪
    lv_obj_move_background(round_mask);  // 移到最底层

    // 8. 创建主容器（360×360，覆盖整个屏幕）
    attitude_container_ = lv_obj_create(screen);
    lv_obj_set_size(attitude_container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_radius(attitude_container_, 0, 0);
    lv_obj_set_style_border_width(attitude_container_, 0, 0);
    lv_obj_set_style_pad_all(attitude_container_, 0, 0);
    lv_obj_set_style_bg_color(attitude_container_, theme_colors.bg_outer, 0);
    lv_obj_set_style_clip_corner(attitude_container_, false, 0);  // 禁用裁剪

    // 9. 创建背景渐变
    CreateBackground();

    // 10. 创建装饰圆
    CreateDecorationCircles();

    // 11. 创建顶部信息栏
    CreateTopInfoRing();

    // 12. 创建底部解读区域
    CreateBottomInterpretation();

    // 13. 创建气泡和十字准星
    CreateBubbleAndCrosshair();

    // 14. 创建屏幕可视边界线（1px 鎏金外圆环，半径 178px）
    const int SCREEN_CENTER_X = 180;
    const int SCREEN_CENTER_Y = 180;
    screen_border_ = lv_obj_create(attitude_container_);
    lv_obj_set_size(screen_border_, 356, 356);  // 直径356px（半径178px）
    lv_obj_set_pos(screen_border_, SCREEN_CENTER_X - 178, SCREEN_CENTER_Y - 178);
    lv_obj_set_style_radius(screen_border_, 178, 0);
    lv_obj_set_style_bg_opa(screen_border_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(screen_border_, 1, 0);  // 1px 边框
    lv_obj_set_style_border_color(screen_border_, theme_colors.border_line, 0);
    lv_obj_set_style_border_opa(screen_border_, LV_OPA_100, 0);

    ESP_LOGI(TAG, "SetupUI completed with theme: %s",
             AttitudeTheme::GetInstance().GetThemeName());
}

// 浅灰渐变背景（主题色值驱动）
void AttitudeDisplay::CreateBackground()
{
    const auto& theme_colors = AttitudeTheme::GetInstance().GetColors();

    // 创建背景容器（全屏）
    background_ = lv_obj_create(attitude_container_);
    lv_obj_set_size(background_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_radius(background_, 0, 0);
    lv_obj_set_style_border_width(background_, 0, 0);
    lv_obj_set_style_pad_all(background_, 0, 0);
    lv_obj_set_style_bg_color(background_, theme_colors.bg_outer, 0);
    lv_obj_set_style_bg_opa(background_, LV_OPA_100, 0);

    // 创建中心微亮层（模拟径向渐变）
    bg_layer_center_ = lv_obj_create(background_);
    lv_obj_set_size(bg_layer_center_, 300, 300);  // 直径 300
    lv_obj_set_style_radius(bg_layer_center_, 150, 0);  // 圆形
    lv_obj_set_style_border_width(bg_layer_center_, 0, 0);
    lv_obj_set_style_bg_color(bg_layer_center_, theme_colors.bg_inner, 0);
    lv_obj_set_style_bg_opa(bg_layer_center_, LV_OPA_100, 0);
    lv_obj_center(bg_layer_center_);

    bg_inner_glow_ = nullptr;

    // 将背景层移到最底层
    lv_obj_move_background(background_);

    ESP_LOGI(TAG, "Background created with theme colors");
}

// 简化装饰圆（主题色值驱动）
void AttitudeDisplay::CreateDecorationCircles()
{
    const auto& theme_colors = AttitudeTheme::GetInstance().GetColors();
    const int CENTER_X = 180;
    const int CENTER_Y = 180;

    // 极简风格 - 仅保留一条极细边框圆
    circle_outer_ = lv_obj_create(attitude_container_);
    lv_obj_set_size(circle_outer_, 280, 280);
    lv_obj_set_pos(circle_outer_, CENTER_X - 140, CENTER_Y - 140);
    lv_obj_set_style_radius(circle_outer_, 140, 0);
    lv_obj_set_style_bg_opa(circle_outer_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(circle_outer_, 1, 0);
    lv_obj_set_style_border_color(circle_outer_, theme_colors.border_line, 0);
    lv_obj_set_style_border_opa(circle_outer_, LV_OPA_60, 0);

    // 移除中圈和内圈，保持极简
    circle_mid_ = nullptr;
    circle_inner_ = nullptr;

    // 方向标记：小圆点指示
    // 北 N
    dir_n_label_ = lv_label_create(attitude_container_);
    lv_label_set_text(dir_n_label_, "•");
    lv_obj_set_style_text_font(dir_n_label_, &BUILTIN_TEXT_FONT, 0);
    lv_obj_set_style_text_color(dir_n_label_, theme_colors.point_default, 0);
    lv_obj_set_style_text_opa(dir_n_label_, LV_OPA_100, 0);
    lv_obj_set_pos(dir_n_label_, CENTER_X - 6, CENTER_Y - 150);

    // 东 E
    dir_e_label_ = lv_label_create(attitude_container_);
    lv_label_set_text(dir_e_label_, "•");
    lv_obj_set_style_text_font(dir_e_label_, &BUILTIN_TEXT_FONT, 0);
    lv_obj_set_style_text_color(dir_e_label_, theme_colors.point_default, 0);
    lv_obj_set_style_text_opa(dir_e_label_, LV_OPA_100, 0);
    lv_obj_set_pos(dir_e_label_, CENTER_X + 140, CENTER_Y - 6);

    // 南 S
    dir_s_label_ = lv_label_create(attitude_container_);
    lv_label_set_text(dir_s_label_, "•");
    lv_obj_set_style_text_font(dir_s_label_, &BUILTIN_TEXT_FONT, 0);
    lv_obj_set_style_text_color(dir_s_label_, theme_colors.point_default, 0);
    lv_obj_set_style_text_opa(dir_s_label_, LV_OPA_100, 0);
    lv_obj_set_pos(dir_s_label_, CENTER_X - 6, CENTER_Y + 140);

    // 西 W
    dir_w_label_ = lv_label_create(attitude_container_);
    lv_label_set_text(dir_w_label_, "•");
    lv_obj_set_style_text_font(dir_w_label_, &BUILTIN_TEXT_FONT, 0);
    lv_obj_set_style_text_color(dir_w_label_, theme_colors.point_default, 0);
    lv_obj_set_style_text_opa(dir_w_label_, LV_OPA_100, 0);
    lv_obj_set_pos(dir_w_label_, CENTER_X - 150, CENTER_Y - 6);

    ESP_LOGI(TAG, "Decoration circles created with theme colors");
}

// 顶部信息环（适配360×360圆形屏幕，主题色值驱动）
void AttitudeDisplay::CreateTopInfoRing()
{
    const auto& theme_colors = AttitudeTheme::GetInstance().GetColors();
    const int CENTER_X = 180;
    const int CENTER_Y = 180;
    const int VISIBLE_RADIUS = 150;

    const int BAR_HEIGHT = 26;
    const int BAR_WIDTH = 240;
    const int BAR_Y = CENTER_Y - VISIBLE_RADIUS + 20;

    // 创建顶部信息容器（透明背景）
    top_info_container_ = lv_obj_create(attitude_container_);
    lv_obj_set_size(top_info_container_, BAR_WIDTH, BAR_HEIGHT);
    lv_obj_set_pos(top_info_container_, CENTER_X - BAR_WIDTH / 2, BAR_Y);
    lv_obj_set_style_radius(top_info_container_, 15, 0);
    lv_obj_set_style_bg_opa(top_info_container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top_info_container_, 0, 0);
    lv_obj_set_style_pad_all(top_info_container_, 0, 0);

    // 使用flex布局水平排列
    lv_obj_set_flex_flow(top_info_container_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_info_container_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // 网络图标（左侧）
    network_icon_ = lv_label_create(top_info_container_);
    lv_label_set_text(network_icon_, "");
    lv_obj_set_style_text_font(network_icon_, &BUILTIN_ICON_FONT, 0);
    lv_obj_set_style_text_color(network_icon_, theme_colors.text_sub, 0);

    // 时间显示（中央）
    time_label_ = lv_label_create(top_info_container_);
    lv_label_set_text(time_label_, "--:--");
    lv_obj_set_style_text_font(time_label_, &BUILTIN_TEXT_FONT, 0);
    lv_obj_set_style_text_color(time_label_, theme_colors.text_high, 0);

    // 电量图标（右侧）
    battery_icon_ = lv_label_create(top_info_container_);
    lv_label_set_text(battery_icon_, "");
    lv_obj_set_style_text_font(battery_icon_, &BUILTIN_ICON_FONT, 0);
    lv_obj_set_style_text_color(battery_icon_, theme_colors.text_sub, 0);

    ESP_LOGI(TAG, "Top info ring created with theme colors");
}

// 底部解读区域（适配360×360圆形屏幕，主题色值驱动）
void AttitudeDisplay::CreateBottomInterpretation()
{
    const auto& theme_colors = AttitudeTheme::GetInstance().GetColors();
    const int CENTER_X = 180;
    const int CENTER_Y = 180;
    const int VISIBLE_RADIUS = 150;

    const int BAR_HEIGHT = 32;
    const int BAR_WIDTH = 240;
    const int BAR_Y = CENTER_Y + VISIBLE_RADIUS - BAR_HEIGHT - 20;

    // 创建底部容器
    bottom_interpret_ = lv_obj_create(attitude_container_);
    lv_obj_set_size(bottom_interpret_, BAR_WIDTH, BAR_HEIGHT);
    lv_obj_set_pos(bottom_interpret_, CENTER_X - BAR_WIDTH / 2, BAR_Y);
    lv_obj_set_style_radius(bottom_interpret_, BAR_HEIGHT / 2, 0);
    lv_obj_set_style_bg_color(bottom_interpret_, theme_colors.card_bg, 0);
    lv_obj_set_style_bg_opa(bottom_interpret_, LV_OPA_100, 0);
    lv_obj_set_style_border_width(bottom_interpret_, 0, 0);
    lv_obj_set_style_pad_hor(bottom_interpret_, 16, 0);
    lv_obj_set_style_pad_ver(bottom_interpret_, 0, 0);

    // 使用flex布局让内容水平居中
    lv_obj_set_flex_flow(bottom_interpret_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bottom_interpret_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // 状态图标（左侧小圆点）
    interpret_icon_ = lv_obj_create(bottom_interpret_);
    lv_obj_set_size(interpret_icon_, 8, 8);
    lv_obj_set_style_radius(interpret_icon_, 4, 0);
    lv_obj_set_style_bg_color(interpret_icon_, theme_colors.state_normal, 0);
    lv_obj_set_style_border_width(interpret_icon_, 0, 0);

    // 间距
    lv_obj_t* gap = lv_obj_create(bottom_interpret_);
    lv_obj_set_size(gap, 8, 1);
    lv_obj_set_style_bg_opa(gap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(gap, 0, 0);

    // 解读文字
    interpret_text_ = lv_label_create(bottom_interpret_);
    lv_label_set_text(interpret_text_, "状态：基本平衡");
    lv_obj_set_style_text_font(interpret_text_, &BUILTIN_TEXT_FONT, 0);
    lv_obj_set_style_text_color(interpret_text_, theme_colors.text_high, 0);
    lv_obj_set_flex_grow(interpret_text_, 1);
    lv_label_set_long_mode(interpret_text_, LV_LABEL_LONG_SCROLL);

    ESP_LOGI(TAG, "Bottom interpretation area created with theme colors");
}

// 气泡和十字准星创建（主题色值驱动）
void AttitudeDisplay::CreateBubbleAndCrosshair()
{
    const auto& theme_colors = AttitudeTheme::GetInstance().GetColors();
    const int CENTER_X = 180;
    const int CENTER_Y = 180;

    // ========== 1. 中心准星（十字线）==========
    bubble_h_axis_ = lv_obj_create(attitude_container_);
    lv_obj_set_size(bubble_h_axis_, 100, 2);
    lv_obj_set_pos(bubble_h_axis_, CENTER_X - 50, CENTER_Y - 1);
    lv_obj_set_style_radius(bubble_h_axis_, 1, 0);
    lv_obj_set_style_bg_color(bubble_h_axis_, theme_colors.border_line, 0);
    lv_obj_set_style_border_width(bubble_h_axis_, 0, 0);
    lv_obj_set_style_bg_opa(bubble_h_axis_, LV_OPA_60, 0);

    bubble_v_axis_ = lv_obj_create(attitude_container_);
    lv_obj_set_size(bubble_v_axis_, 2, 100);
    lv_obj_set_pos(bubble_v_axis_, CENTER_X - 1, CENTER_Y - 50);
    lv_obj_set_style_radius(bubble_v_axis_, 1, 0);
    lv_obj_set_style_bg_color(bubble_v_axis_, theme_colors.border_line, 0);
    lv_obj_set_style_border_width(bubble_v_axis_, 0, 0);
    lv_obj_set_style_bg_opa(bubble_v_axis_, LV_OPA_50, 0);

    bubble_center_marker_ = lv_obj_create(attitude_container_);
    lv_obj_set_size(bubble_center_marker_, 6, 6);
    lv_obj_set_pos(bubble_center_marker_, CENTER_X - 3, CENTER_Y - 3);
    lv_obj_set_style_radius(bubble_center_marker_, 3, 0);
    lv_obj_set_style_bg_color(bubble_center_marker_, theme_colors.point_default, 0);
    lv_obj_set_style_border_width(bubble_center_marker_, 0, 0);
    lv_obj_set_style_bg_opa(bubble_center_marker_, LV_OPA_100, 0);

    // ========== 2. 气泡主体 ==========
    bubble_obj_ = lv_obj_create(attitude_container_);
    lv_obj_set_size(bubble_obj_, 24, 24);
    lv_obj_set_pos(bubble_obj_, CENTER_X - 12, CENTER_Y - 12);
    lv_obj_set_style_radius(bubble_obj_, 12, 0);
    lv_obj_set_style_bg_color(bubble_obj_, theme_colors.state_normal, 0);
    lv_obj_set_style_border_width(bubble_obj_, 0, 0);
    lv_obj_set_style_bg_opa(bubble_obj_, LV_OPA_90, 0);

    bubble_glow_ = nullptr;
    bubble_highlight_ = nullptr;

    ESP_LOGI(TAG, "Bubble created with theme colors");
}

// Step 3.5: 设置气泡位置（带动画）
void AttitudeDisplay::SetBubblePosition(int offset_x, int offset_y)
{
    // 限制移动范围 ±60px
    offset_x = (offset_x > 60) ? 60 : (offset_x < -60) ? -60 : offset_x;
    offset_y = (offset_y > 60) ? 60 : (offset_y < -60) ? -60 : offset_y;

    bubble_offset_x_ = offset_x;
    bubble_offset_y_ = offset_y;

    DisplayLockGuard lock(this);

    const int CENTER_X = 180;
    const int CENTER_Y = 180;

    // 移动气泡主体（直径24px，所以偏移12px）
    if (bubble_obj_ != nullptr) {
        lv_obj_set_pos(bubble_obj_, CENTER_X - 12 + offset_x, CENTER_Y - 12 + offset_y);
    }
}

// Step 3.6: 设置气泡颜色等级（主题色值驱动）
void AttitudeDisplay::SetBubbleLevel(int level)
{
    if (level < 0) level = 0;
    if (level > 4) level = 4;
    bubble_level_ = level;

    DisplayLockGuard lock(this);

    // 从主题管理器获取对应等级颜色
    lv_color_t bubble_color = AttitudeTheme::GetInstance().GetStateColor(level);

    // 更新气泡颜色
    if (bubble_obj_ != nullptr) {
        lv_obj_set_style_bg_color(bubble_obj_, bubble_color, 0);
    }

    // 同步更新状态指示点
    if (interpret_icon_ != nullptr) {
        lv_obj_set_style_bg_color(interpret_icon_, bubble_color, 0);
    }
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
    // 由 ApplyCurrentTheme 实现
    ApplyCurrentTheme();
}

// 主题切换接口（公开API）
void AttitudeDisplay::SwitchTheme(AttitudeThemeType theme)
{
    AttitudeTheme::GetInstance().SetTheme(theme);
    ApplyCurrentTheme();
}

// 应用当前主题到所有UI元素
void AttitudeDisplay::ApplyCurrentTheme()
{
    DisplayLockGuard lock(this);
    const auto& theme_colors = AttitudeTheme::GetInstance().GetColors();

    // 1. 背景
    if (background_ != nullptr) {
        lv_obj_set_style_bg_color(background_, theme_colors.bg_outer, 0);
    }
    if (bg_layer_center_ != nullptr) {
        lv_obj_set_style_bg_color(bg_layer_center_, theme_colors.bg_inner, 0);
    }

    // 2. 装饰圆边框
    if (circle_outer_ != nullptr) {
        lv_obj_set_style_border_color(circle_outer_, theme_colors.border_line, 0);
    }

    // 3. 方位点
    auto apply_point_color = [&](lv_obj_t* obj) {
        if (obj != nullptr) {
            lv_obj_set_style_text_color(obj, theme_colors.point_default, 0);
        }
    };
    apply_point_color(dir_n_label_);
    apply_point_color(dir_e_label_);
    apply_point_color(dir_s_label_);
    apply_point_color(dir_w_label_);

    // 4. 顶部信息栏
    if (network_icon_ != nullptr) {
        lv_obj_set_style_text_color(network_icon_, theme_colors.text_sub, 0);
    }
    if (time_label_ != nullptr) {
        lv_obj_set_style_text_color(time_label_, theme_colors.text_high, 0);
    }
    if (battery_icon_ != nullptr) {
        lv_obj_set_style_text_color(battery_icon_, theme_colors.text_sub, 0);
    }

    // 5. 底部解读区
    if (bottom_interpret_ != nullptr) {
        lv_obj_set_style_bg_color(bottom_interpret_, theme_colors.card_bg, 0);
    }
    if (interpret_text_ != nullptr) {
        lv_obj_set_style_text_color(interpret_text_, theme_colors.text_high, 0);
    }

    // 6. 十字准星
    if (bubble_h_axis_ != nullptr) {
        lv_obj_set_style_bg_color(bubble_h_axis_, theme_colors.border_line, 0);
    }
    if (bubble_v_axis_ != nullptr) {
        lv_obj_set_style_bg_color(bubble_v_axis_, theme_colors.border_line, 0);
    }
    if (bubble_center_marker_ != nullptr) {
        lv_obj_set_style_bg_color(bubble_center_marker_, theme_colors.point_default, 0);
    }

    // 7. 气泡颜色（根据当前等级）
    if (bubble_obj_ != nullptr) {
        lv_color_t bubble_color = AttitudeTheme::GetInstance().GetStateColor(bubble_level_);
        lv_obj_set_style_bg_color(bubble_obj_, bubble_color, 0);
    }
    if (interpret_icon_ != nullptr) {
        lv_color_t bubble_color = AttitudeTheme::GetInstance().GetStateColor(bubble_level_);
        lv_obj_set_style_bg_color(interpret_icon_, bubble_color, 0);
    }

    ESP_LOGI(TAG, "Theme applied: %s", AttitudeTheme::GetInstance().GetThemeName());
}