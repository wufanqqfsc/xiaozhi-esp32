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

namespace {

static const lv_color_t kFisheyeGrayBg = lv_color_hex(0x505050);
static const lv_color_t kFisheyeGrayBorder = lv_color_hex(0x606060);
static const lv_color_t kFisheyeGrayIcon = lv_color_hex(0x909090);
static const lv_color_t kFisheyeGold = lv_color_hex(0xD4AF37);
static const lv_color_t kFisheyeWhite = lv_color_hex(0xFFFFFF);
static const lv_color_t kFisheyeDark = lv_color_hex(0x0A0A0A);

static void FisheyeOpaAnimCb(void* obj, int32_t value)
{
    lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), static_cast<lv_opa_t>(value), 0);
}

static const lv_font_t* GetIconFont(AttitudeDisplay* display)
{
    auto lvgl_theme = static_cast<LvglTheme*>(display->GetTheme());
    if (lvgl_theme == nullptr || lvgl_theme->icon_font() == nullptr) {
        return &BUILTIN_ICON_FONT;
    }
    return lvgl_theme->icon_font()->font();
}

static void StyleSmoothGoldArc(lv_obj_t* arc, lv_color_t color, int width)
{
    lv_arc_set_range(arc, 0, 360);
    lv_arc_set_value(arc, 360);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_angles(arc, 0, 360);
    lv_obj_set_style_arc_width(arc, 0, 0);
    lv_obj_set_style_arc_color(arc, color, 0);
    lv_obj_set_style_arc_width(arc, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
}

} // namespace

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
    CreateWifiFisheye();
    CreateBleFisheye();
    UpdateWifiFisheye(WifiStatus::DISCONNECTED);
    UpdateBleFisheye(BleStatus::DISABLED);
    CompassTaiji::StartAutoRotation(30000);
    ESP_LOGI(TAG, "Taiji auto rotation started (period=30s, fisheyes co-rotate)");

    CreateLayer1CoreInfo();
    CreateLayer2DynamicIndicator();
    CreateLayer3StatusProgress();
    CreateLayer4Boundary();
    // 方位圆点已移除（v1.2+ 视觉简化，运势高亮见迭代 2 再定）

    ESP_LOGI(TAG, "SetupUI completed (4-layer layout + co-rotating fisheye taiji)");
}

void AttitudeDisplay::CreateLayer0Taiji()
{
    const int CENTER_X = 180;
    const int CENTER_Y = 180;

    ESP_LOGI(TAG, "Creating Layer0 Taiji (radius=%d, canvas=%d, fisheye=%d)",
             TAIJI_RADIUS, TAIJI_CANVAS_SIZE, FISHEYE_ICON_SIZE);
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
    const int arc_r = LAYER2_ARC_RADIUS;
    const int arc_size = arc_r * 2;

    layer2_inner_ring_ = lv_arc_create(attitude_container_);
    lv_obj_set_size(layer2_inner_ring_, arc_size, arc_size);
    lv_obj_set_pos(layer2_inner_ring_, CENTER_X - arc_r, CENTER_Y - arc_r);
    StyleSmoothGoldArc(layer2_inner_ring_, c.border_line, GOLD_RING_ARC_WIDTH);

    ESP_LOGI(TAG, "Layer2 gold ring at r=%d (smooth arc, %dpx)",
             arc_r, GOLD_RING_ARC_WIDTH);
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
    const int outer_r = LAYER4_BOUNDARY_RADIUS;
    const int outer_size = outer_r * 2;

    layer4_outer_ring_ = lv_arc_create(attitude_container_);
    lv_obj_set_size(layer4_outer_ring_, outer_size, outer_size);
    lv_obj_set_pos(layer4_outer_ring_, CENTER_X - outer_r, CENTER_Y - outer_r);
    StyleSmoothGoldArc(layer4_outer_ring_, c.border_line, GOLD_RING_ARC_WIDTH);

    ESP_LOGI(TAG, "Layer4 Boundary gold arc at r=%d (smooth, %dpx)", outer_r, GOLD_RING_ARC_WIDTH);
}

void AttitudeDisplay::CreateCompassPoints()
{
    // 常驻 N/E/S/W 6×6 圆点已取消显示；dir_*_label_ 保留供迭代 2 HighlightDirection 按需创建
    ESP_LOGI(TAG, "Compass direction dots disabled (not created)");
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

void AttitudeDisplay::StartFisheyePulse(lv_obj_t* obj)
{
    if (obj == nullptr) {
        return;
    }

    lv_anim_delete(obj, FisheyeOpaAnimCb);
    lv_obj_set_style_opa(obj, 255, 0);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, obj);
    lv_anim_set_exec_cb(&anim, FisheyeOpaAnimCb);
    lv_anim_set_values(&anim, 150, 255);
    lv_anim_set_duration(&anim, FISHEYE_PULSE_MS);
    lv_anim_set_playback_duration(&anim, FISHEYE_PULSE_MS);
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&anim);
}

void AttitudeDisplay::StopFisheyePulse(lv_obj_t* obj)
{
    if (obj == nullptr) {
        return;
    }

    lv_anim_delete(obj, FisheyeOpaAnimCb);
    lv_obj_set_style_opa(obj, LV_OPA_COVER, 0);
}

void AttitudeDisplay::CreateWifiFisheye()
{
    lv_obj_t* parent = CompassTaiji::GetContainer();
    if (parent == nullptr) {
        ESP_LOGE(TAG, "CreateWifiFisheye: taiji container is null");
        return;
    }

    wifi_fisheye_ = lv_obj_create(parent);
    lv_obj_set_size(wifi_fisheye_, FISHEYE_ICON_SIZE, FISHEYE_ICON_SIZE);
    lv_obj_set_pos(wifi_fisheye_, FISHEYE_WIFI_LOCAL_X, FISHEYE_WIFI_LOCAL_Y);
    lv_obj_set_style_radius(wifi_fisheye_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(wifi_fisheye_, 2, 0);
    lv_obj_set_style_pad_all(wifi_fisheye_, 0, 0);
    lv_obj_clear_flag(wifi_fisheye_, LV_OBJ_FLAG_CLICKABLE);

    wifi_fisheye_icon_ = lv_label_create(wifi_fisheye_);
    lv_obj_set_style_text_font(wifi_fisheye_icon_, GetIconFont(this), 0);
    lv_label_set_text(wifi_fisheye_icon_, FONT_AWESOME_WIFI);
    lv_obj_center(wifi_fisheye_icon_);

    ESP_LOGI(TAG, "WiFi fisheye on taiji at local (%d,%d)",
             FISHEYE_WIFI_LOCAL_X, FISHEYE_WIFI_LOCAL_Y);
}

void AttitudeDisplay::CreateBleFisheye()
{
    lv_obj_t* parent = CompassTaiji::GetContainer();
    if (parent == nullptr) {
        ESP_LOGE(TAG, "CreateBleFisheye: taiji container is null");
        return;
    }

    ble_fisheye_ = lv_obj_create(parent);
    lv_obj_set_size(ble_fisheye_, FISHEYE_ICON_SIZE, FISHEYE_ICON_SIZE);
    lv_obj_set_pos(ble_fisheye_, FISHEYE_BLE_LOCAL_X, FISHEYE_BLE_LOCAL_Y);
    lv_obj_set_style_radius(ble_fisheye_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ble_fisheye_, 2, 0);
    lv_obj_set_style_pad_all(ble_fisheye_, 0, 0);
    lv_obj_clear_flag(ble_fisheye_, LV_OBJ_FLAG_CLICKABLE);

    ble_fisheye_icon_ = lv_label_create(ble_fisheye_);
    lv_obj_set_style_text_font(ble_fisheye_icon_, GetIconFont(this), 0);
    lv_label_set_text(ble_fisheye_icon_, FONT_AWESOME_BLUETOOTH);
    lv_obj_center(ble_fisheye_icon_);

    ESP_LOGI(TAG, "BLE fisheye on taiji at local (%d,%d)",
             FISHEYE_BLE_LOCAL_X, FISHEYE_BLE_LOCAL_Y);
}

void AttitudeDisplay::ApplyWifiFisheyeStyle(WifiStatus status)
{
    if (wifi_fisheye_ == nullptr || wifi_fisheye_icon_ == nullptr) {
        return;
    }

    StopFisheyePulse(wifi_fisheye_);

    switch (status) {
    case WifiStatus::DISCONNECTED:
        lv_obj_set_style_bg_color(wifi_fisheye_, kFisheyeGrayBg, 0);
        lv_obj_set_style_border_color(wifi_fisheye_, kFisheyeGrayBorder, 0);
        lv_obj_set_style_text_color(wifi_fisheye_icon_, kFisheyeGrayIcon, 0);
        lv_label_set_text(wifi_fisheye_icon_, FONT_AWESOME_WIFI_SLASH);
        break;
    case WifiStatus::CONNECTING:
        lv_obj_set_style_bg_color(wifi_fisheye_, kFisheyeDark, 0);
        lv_obj_set_style_border_color(wifi_fisheye_, kFisheyeGold, 0);
        lv_obj_set_style_text_color(wifi_fisheye_icon_, kFisheyeGold, 0);
        lv_label_set_text(wifi_fisheye_icon_, FONT_AWESOME_WIFI);
        StartFisheyePulse(wifi_fisheye_);
        break;
    case WifiStatus::CONNECTED:
        lv_obj_set_style_bg_color(wifi_fisheye_, kFisheyeGold, 0);
        lv_obj_set_style_border_color(wifi_fisheye_, kFisheyeGold, 0);
        lv_obj_set_style_text_color(wifi_fisheye_icon_, kFisheyeDark, 0);
        lv_label_set_text(wifi_fisheye_icon_, FONT_AWESOME_WIFI);
        break;
    default:
        break;
    }
}

void AttitudeDisplay::ApplyBleFisheyeStyle(BleStatus status)
{
    if (ble_fisheye_ == nullptr || ble_fisheye_icon_ == nullptr) {
        return;
    }

    StopFisheyePulse(ble_fisheye_);

    switch (status) {
    case BleStatus::DISABLED:
        lv_obj_set_style_bg_color(ble_fisheye_, kFisheyeGrayBg, 0);
        lv_obj_set_style_border_color(ble_fisheye_, kFisheyeGrayBorder, 0);
        lv_obj_set_style_text_color(ble_fisheye_icon_, kFisheyeGrayIcon, 0);
        lv_label_set_text(ble_fisheye_icon_, FONT_AWESOME_BLUETOOTH);
        break;
    case BleStatus::ADVERTISING:
        lv_obj_set_style_bg_color(ble_fisheye_, kFisheyeWhite, 0);
        lv_obj_set_style_border_color(ble_fisheye_, kFisheyeWhite, 0);
        lv_obj_set_style_text_color(ble_fisheye_icon_, kFisheyeDark, 0);
        lv_label_set_text(ble_fisheye_icon_, FONT_AWESOME_BLUETOOTH);
        StartFisheyePulse(ble_fisheye_);
        break;
    case BleStatus::CONNECTED:
        lv_obj_set_style_bg_color(ble_fisheye_, kFisheyeWhite, 0);
        lv_obj_set_style_border_color(ble_fisheye_, kFisheyeWhite, 0);
        lv_obj_set_style_text_color(ble_fisheye_icon_, kFisheyeGold, 0);
        lv_label_set_text(ble_fisheye_icon_, FONT_AWESOME_BLUETOOTH);
        break;
    default:
        break;
    }
}

void AttitudeDisplay::UpdateWifiFisheye(WifiStatus status)
{
    DisplayLockGuard lock(this);
    wifi_status_ = status;
    ApplyWifiFisheyeStyle(status);
    ESP_LOGI(TAG, "WiFi fisheye status -> %d", static_cast<int>(status));
}

void AttitudeDisplay::UpdateBleFisheye(BleStatus status)
{
    DisplayLockGuard lock(this);
    ble_status_ = status;
    ApplyBleFisheyeStyle(status);
    ESP_LOGI(TAG, "BLE fisheye status -> %d", static_cast<int>(status));
}
