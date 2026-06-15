#include "attitude_display.h"
#include "lvgl_theme.h"
#include "application.h"
#include "board.h"
#include "compass_taiji.h"
#include <esp_log.h>
#include <cstdio>
#include <inttypes.h>
#include <ctime>
#include <cmath>
#include <font_awesome.h>

#define TAG "AttitudeDisplay"

LV_FONT_DECLARE(BUILTIN_TEXT_FONT);
LV_FONT_DECLARE(BUILTIN_ICON_FONT);

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
    if (IsSetupUICalled()) {
        ESP_LOGW(TAG, "SetupUI() already called, skipping");
        return;
    }

    Display::SetupUI();
    DisplayLockGuard lock(this);

    auto lvgl_theme = static_cast<LvglTheme*>(GetTheme());
    if (lvgl_theme == nullptr) {
        ESP_LOGE(TAG, "Theme is null!");
        return;
    }

    const auto& c = AttitudeTheme::GetInstance().GetColors();

    auto screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, lvgl_theme->text_font()->font(), 0);
    lv_obj_set_style_text_color(screen, c.text_main, 0);
    lv_obj_set_style_bg_color(screen, c.bg_outer, 0);

    lv_obj_t* round_mask = lv_obj_create(screen);
    lv_obj_set_size(round_mask, 360, 360);
    lv_obj_set_pos(round_mask, 0, 0);
    lv_obj_set_style_radius(round_mask, 180, 0);
    lv_obj_set_style_bg_color(round_mask, c.bg_outer, 0);
    lv_obj_set_style_border_width(round_mask, 0, 0);
    lv_obj_set_style_clip_corner(round_mask, true, 0);
    lv_obj_move_background(round_mask);

    attitude_container_ = lv_obj_create(screen);
    lv_obj_set_size(attitude_container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_radius(attitude_container_, 0, 0);
    lv_obj_set_style_border_width(attitude_container_, 0, 0);
    lv_obj_set_style_pad_all(attitude_container_, 0, 0);
    lv_obj_set_style_bg_color(attitude_container_, c.bg_outer, 0);
    lv_obj_set_style_clip_corner(attitude_container_, false, 0);

    CreateBackground();
    CreateLayer0Taiji();
    CompassTaiji::StartAutoRotation(30000);
    ESP_LOGI(TAG, "Taiji auto rotation started (period=30s)");

    CreateLayer1CoreInfo();
    CreateLayer2DynamicIndicator();
    CreateLayer3StatusProgress();
    CreateLayer4Boundary();
    CreateCompassPoints();

    ESP_LOGI(TAG, "SetupUI completed (4-layer concentric layout)");
}

void AttitudeDisplay::CreateLayer0Taiji()
{
    const int CENTER_X = 180;
    const int CENTER_Y = 180;
    const int TAIJI_RADIUS = 44;

    ESP_LOGI(TAG, "Creating Layer0 Taiji diagram (radius=%d)", TAIJI_RADIUS);
    CompassTaiji::Create(attitude_container_, CENTER_X, CENTER_Y, TAIJI_RADIUS);
}

void AttitudeDisplay::CreateBackground()
{
    const auto& c = AttitudeTheme::GetInstance().GetColors();

    background_ = lv_obj_create(attitude_container_);
    lv_obj_set_size(background_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_radius(background_, 0, 0);
    lv_obj_set_style_border_width(background_, 0, 0);
    lv_obj_set_style_pad_all(background_, 0, 0);
    lv_obj_set_style_bg_color(background_, c.bg_outer, 0);
    lv_obj_set_style_bg_opa(background_, LV_OPA_100, 0);

    bg_layer_center_ = lv_obj_create(background_);
    lv_obj_set_size(bg_layer_center_, 300, 300);
    lv_obj_set_style_radius(bg_layer_center_, 150, 0);
    lv_obj_set_style_border_width(bg_layer_center_, 0, 0);
    lv_obj_set_style_bg_color(bg_layer_center_, c.bg_inner, 0);
    lv_obj_set_style_bg_opa(bg_layer_center_, LV_OPA_100, 0);
    lv_obj_center(bg_layer_center_);

    bg_inner_glow_ = nullptr;
    lv_obj_move_background(background_);

    ESP_LOGI(TAG, "Background created with fixed colors");
}

void AttitudeDisplay::UpdateStatusBar(bool update_all)
{
    (void)update_all;
}

void AttitudeDisplay::SetTheme(Theme* theme)
{
    Display::SetTheme(theme);
}

void AttitudeDisplay::SetAttitudeData(float pitch, float roll, float yaw)
{
    current_pitch_ = pitch;
    current_roll_ = roll;
    current_yaw_ = yaw;
}

void AttitudeDisplay::SetInterpretation(const std::string& text)
{
    (void)text;
}

void AttitudeDisplay::CreateLayer1CoreInfo()
{
    const int CENTER_X = 180;
    const int CENTER_Y = 180;

    layer1_container_ = lv_obj_create(attitude_container_);
    lv_obj_set_size(layer1_container_, 108, 108);
    lv_obj_set_pos(layer1_container_, CENTER_X - 54, CENTER_Y - 54);
    lv_obj_set_style_radius(layer1_container_, 54, 0);
    lv_obj_set_style_bg_opa(layer1_container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(layer1_container_, 0, 0);
    lv_obj_set_style_pad_all(layer1_container_, 0, 0);
    lv_obj_clear_flag(layer1_container_, LV_OBJ_FLAG_CLICKABLE);

    ESP_LOGI(TAG, "Layer1 CoreInfo created (0~54px)");
}

void AttitudeDisplay::CreateLayer2DynamicIndicator()
{
    const auto& c = AttitudeTheme::GetInstance().GetColors();
    const int CENTER_X = 180;
    const int CENTER_Y = 180;

    layer2_inner_ring_ = lv_arc_create(attitude_container_);
    lv_obj_set_size(layer2_inner_ring_, 160, 160);
    lv_obj_set_pos(layer2_inner_ring_, CENTER_X - 80, CENTER_Y - 80);
    lv_arc_set_range(layer2_inner_ring_, 0, 360);
    lv_arc_set_value(layer2_inner_ring_, 360);
    lv_arc_set_bg_angles(layer2_inner_ring_, 0, 360);
    lv_arc_set_angles(layer2_inner_ring_, 0, 360);
    lv_obj_set_style_arc_width(layer2_inner_ring_, 1, 0);
    lv_obj_set_style_arc_color(layer2_inner_ring_, c.border_line, 0);
    lv_obj_set_style_arc_width(layer2_inner_ring_, 1, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(layer2_inner_ring_, c.border_line, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(layer2_inner_ring_, LV_OPA_50, 0);
    lv_obj_set_style_opa(layer2_inner_ring_, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_clear_flag(layer2_inner_ring_, LV_OBJ_FLAG_CLICKABLE);

    ESP_LOGI(TAG, "Layer2 DynamicIndicator created (54~90px)");
}

void AttitudeDisplay::CreateLayer3StatusProgress()
{
    const auto& c = AttitudeTheme::GetInstance().GetColors();
    const int CENTER_X = 180;
    const int CENTER_Y = 180;

    layer3_bg_arc_ = lv_arc_create(attitude_container_);
    lv_obj_set_size(layer3_bg_arc_, 260, 260);
    lv_obj_set_pos(layer3_bg_arc_, CENTER_X - 130, CENTER_Y - 130);
    lv_arc_set_range(layer3_bg_arc_, 0, 360);
    lv_arc_set_value(layer3_bg_arc_, 360);
    lv_arc_set_bg_angles(layer3_bg_arc_, 0, 360);
    lv_arc_set_angles(layer3_bg_arc_, 0, 360);
    lv_obj_set_style_arc_width(layer3_bg_arc_, 4, 0);
    lv_obj_set_style_arc_color(layer3_bg_arc_, c.card_bg, 0);
    lv_obj_set_style_arc_color(layer3_bg_arc_, c.card_bg, LV_PART_INDICATOR);
    lv_obj_set_style_opa(layer3_bg_arc_, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_clear_flag(layer3_bg_arc_, LV_OBJ_FLAG_CLICKABLE);

    layer3_progress_arc_ = lv_arc_create(attitude_container_);
    lv_obj_set_size(layer3_progress_arc_, 280, 280);
    lv_obj_set_pos(layer3_progress_arc_, CENTER_X - 140, CENTER_Y - 140);
    lv_obj_set_style_arc_color(layer3_progress_arc_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_arc_color(layer3_progress_arc_, c.state_normal, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(layer3_progress_arc_, 4, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(layer3_progress_arc_, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(layer3_progress_arc_, 0, 0);
    lv_arc_set_range(layer3_progress_arc_, 0, 360);
    lv_arc_set_value(layer3_progress_arc_, 0);
    lv_arc_set_bg_angles(layer3_progress_arc_, 0, 360);
    lv_arc_set_angles(layer3_progress_arc_, 0, 0);
    lv_obj_set_style_opa(layer3_progress_arc_, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_clear_flag(layer3_progress_arc_, LV_OBJ_FLAG_CLICKABLE);

    ESP_LOGI(TAG, "Layer3 StatusProgress created (90~144px)");
}

void AttitudeDisplay::CreateLayer4Boundary()
{
    const auto& c = AttitudeTheme::GetInstance().GetColors();
    const int CENTER_X = 180;
    const int CENTER_Y = 180;

    layer4_outer_ring_ = lv_obj_create(attitude_container_);
    lv_obj_set_size(layer4_outer_ring_, 356, 356);
    lv_obj_set_pos(layer4_outer_ring_, CENTER_X - 178, CENTER_Y - 178);
    lv_obj_set_style_radius(layer4_outer_ring_, 178, 0);
    lv_obj_set_style_bg_opa(layer4_outer_ring_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(layer4_outer_ring_, 1, 0);
    lv_obj_set_style_border_color(layer4_outer_ring_, c.border_line, 0);
    lv_obj_set_style_border_opa(layer4_outer_ring_, LV_OPA_100, 0);

    ESP_LOGI(TAG, "Layer4 Boundary created (144~178px)");
}

void AttitudeDisplay::CreateCompassPoints()
{
    const auto& c = AttitudeTheme::GetInstance().GetColors();
    const int CENTER_X = 180;
    const int CENTER_Y = 180;
    const int POINT_SIZE = 6;
    const int POINTS_RADIUS = 72;

    dir_n_label_ = lv_obj_create(attitude_container_);
    lv_obj_set_size(dir_n_label_, POINT_SIZE, POINT_SIZE);
    lv_obj_set_pos(dir_n_label_, CENTER_X - POINT_SIZE/2, CENTER_Y - POINTS_RADIUS - POINT_SIZE/2);
    lv_obj_set_style_radius(dir_n_label_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dir_n_label_, c.point_default, 0);
    lv_obj_set_style_border_width(dir_n_label_, 0, 0);

    dir_s_label_ = lv_obj_create(attitude_container_);
    lv_obj_set_size(dir_s_label_, POINT_SIZE, POINT_SIZE);
    lv_obj_set_pos(dir_s_label_, CENTER_X - POINT_SIZE/2, CENTER_Y + POINTS_RADIUS - POINT_SIZE/2);
    lv_obj_set_style_radius(dir_s_label_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dir_s_label_, c.point_default, 0);
    lv_obj_set_style_border_width(dir_s_label_, 0, 0);

    dir_w_label_ = lv_obj_create(attitude_container_);
    lv_obj_set_size(dir_w_label_, POINT_SIZE, POINT_SIZE);
    lv_obj_set_pos(dir_w_label_, CENTER_X - POINTS_RADIUS - POINT_SIZE/2, CENTER_Y - POINT_SIZE/2);
    lv_obj_set_style_radius(dir_w_label_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dir_w_label_, c.point_default, 0);
    lv_obj_set_style_border_width(dir_w_label_, 0, 0);

    dir_e_label_ = lv_obj_create(attitude_container_);
    lv_obj_set_size(dir_e_label_, POINT_SIZE, POINT_SIZE);
    lv_obj_set_pos(dir_e_label_, CENTER_X + POINTS_RADIUS - POINT_SIZE/2, CENTER_Y - POINT_SIZE/2);
    lv_obj_set_style_radius(dir_e_label_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dir_e_label_, c.point_default, 0);
    lv_obj_set_style_border_width(dir_e_label_, 0, 0);

    ESP_LOGI(TAG, "Compass points (N/E/S/W) created with 6x6 dots at r=%d", POINTS_RADIUS);
}

void AttitudeDisplay::UpdateStateColor(int level)
{
    if (level < 0) level = 0;
    if (level > 4) level = 4;
    current_state_level_ = level;

    DisplayLockGuard lock(this);

    if (layer3_progress_arc_ != nullptr) {
        lv_color_t state_color = AttitudeTheme::GetInstance().GetStateColor(level);
        lv_obj_set_style_arc_color(layer3_progress_arc_, state_color, LV_PART_INDICATOR);
    }
}

void AttitudeDisplay::RotateTaiji()
{
    DisplayLockGuard lock(this);
    CompassTaiji::Rotate(150);
}

void AttitudeDisplay::RotateTaijiCCW()
{
    DisplayLockGuard lock(this);
    CompassTaiji::Rotate(-150);
}

void AttitudeDisplay::SetTaijiRotation(int angle)
{
    DisplayLockGuard lock(this);
    CompassTaiji::SetRotation(angle);
}

int AttitudeDisplay::GetTaijiRotation()
{
    DisplayLockGuard lock(this);
    return CompassTaiji::GetRotation();
}

void AttitudeDisplay::ResetTaijiRotation()
{
    DisplayLockGuard lock(this);
    CompassTaiji::ResetRotation();
}

void AttitudeDisplay::StartTaijiAutoRotation(int period_ms)
{
    DisplayLockGuard lock(this);
    CompassTaiji::StartAutoRotation(period_ms);
}

void AttitudeDisplay::StopTaijiAutoRotation()
{
    DisplayLockGuard lock(this);
    CompassTaiji::StopAutoRotation();
}

bool AttitudeDisplay::IsTaijiAutoRotating()
{
    DisplayLockGuard lock(this);
    return CompassTaiji::IsAutoRotating();
}
