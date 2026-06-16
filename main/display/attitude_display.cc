#include "attitude_display.h"
#include "lvgl_theme.h"
#include "application.h"
#include "board.h"
#include "compass_taiji.h"
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <cstdio>
#include <cstring>
#include <inttypes.h>
#include <ctime>
#include <cmath>
#include <font_awesome.h>

#define TAG "AttitudeDisplay"

#define FORTUNE_CARD_W            200
#define FORTUNE_CARD_H            240
#define TAIJI_ROTATION_PERIOD_NORMAL_MS  60000   // 常态 60s/圈（减慢旋转 + 降低刷屏）
#define FORTUNE_TAIJI_PHASE_MS           4000   // 每 4s 一档
#define FORTUNE_TAIJI_PHASE_COUNT        5
#define FORTUNE_ANIM_DURATION_MS         (FORTUNE_TAIJI_PHASE_MS * FORTUNE_TAIJI_PHASE_COUNT)  // 20s
#define FORTUNE_TAIJI_TICK_MS            200    // 运势快转步进间隔
#define FORTUNE_RESULT_CARD_DELAY_MS     1000   // 恢复常态转速后再出结果卡
#define FORTUNE_RESULT_TIMEOUT_MS 30000
#define FORTUNE_PULSE_MS          1000   // 中心亮度 / 外圈脉冲周期
#define FORTUNE_PULSE_CENTER_SIZE (TAIJI_CANVAS_SIZE + 16)
#define FORTUNE_PROGRESS_STEP_MS  2000   // 20s 进度环分 10 步
#define BG_LAYER_CENTER_SIZE      270
#define COMPASS_DOT_SIZE          6
#define FORTUNE_GUA_CANVAS_W      72
#define FORTUNE_GUA_CANVAS_H      48

LV_FONT_DECLARE(BUILTIN_TEXT_FONT);
LV_FONT_DECLARE(BUILTIN_ICON_FONT);

namespace {

static const lv_color_t kFisheyeGrayBg = lv_color_hex(0x505050);       // WiFi 断开：叠在白鱼区域
static const lv_color_t kFisheyeGrayBorder = lv_color_hex(0x606060);
static const lv_color_t kFisheyeGrayIcon = lv_color_hex(0x909090);
static const lv_color_t kFisheyeBleBlue = lv_color_hex(0x2196F3);    // BLE 已连接：蓝边
static const uint32_t kBleBorderGray = 0x909090;
static const uint32_t kBleBorderWhite = 0xFFFFFF;
static const uint32_t kBleBorderBlue = 0x2196F3;
static const lv_color_t kFisheyeGold = lv_color_hex(0xD4AF37);
static const lv_color_t kFisheyeWhite = lv_color_hex(0xFFFFFF);
static const lv_color_t kFisheyeDark = lv_color_hex(0x0A0A0A);

static void FisheyeOpaAnimCb(void* obj, int32_t value)
{
    lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), static_cast<lv_opa_t>(value), 0);
}

static void FisheyeBorderColorAnimCb(void* obj, int32_t value)
{
    lv_obj_set_style_border_color(static_cast<lv_obj_t*>(obj),
                                  lv_color_hex(static_cast<uint32_t>(value)), 0);
}

static void ContainerOpaAnimCb(void* obj, int32_t value)
{
    lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), static_cast<lv_opa_t>(value), 0);
}

static void ArcColorAnimCb(void* obj, int32_t value)
{
    lv_obj_t* arc = static_cast<lv_obj_t*>(obj);
    lv_color_t color = lv_color_hex(static_cast<uint32_t>(value));
    lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
}

static void ProgressArcValueAnimCb(void* obj, int32_t value)
{
    lv_obj_t* arc = static_cast<lv_obj_t*>(obj);
    lv_arc_set_value(arc, value);
    lv_arc_set_angles(arc, 0, value);
}

static void DotColorAnimCb(void* obj, int32_t value)
{
    lv_obj_set_style_bg_color(static_cast<lv_obj_t*>(obj),
                              lv_color_hex(static_cast<uint32_t>(value)), 0);
}

static void LabelColorAnimCb(void* obj, int32_t value)
{
    lv_obj_set_style_text_color(static_cast<lv_obj_t*>(obj),
                                lv_color_hex(static_cast<uint32_t>(value)), 0);
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
    lv_obj_set_style_bg_opa(attitude_container_, LV_OPA_COVER, 0);
    lv_obj_set_style_clip_corner(attitude_container_, false, 0);

    CreateBackground();
    CreateLayer0Taiji();
    CreateWifiFisheye();
    CreateBleFisheye();
    UpdateWifiFisheye(WifiStatus::DISCONNECTED);
    UpdateBleFisheye(BleStatus::DISABLED);
    CompassTaiji::StartAutoRotation(TAIJI_ROTATION_PERIOD_NORMAL_MS);
    ESP_LOGI(TAG, "Taiji auto rotation started (period=30s, fisheyes co-rotate)");

    CreateLayer1CoreInfo();
    // L3 状态进度环暂时移除，对比视觉效果
    CreateLayer4Boundary();
    // 方位圆点已移除（v1.2+ 视觉简化，运势高亮见迭代 2 再定）

    ESP_LOGI(TAG, "SetupUI completed (taiji+fisheye 90%%, L4 outer ring only, L3 disabled)");
}

void AttitudeDisplay::CreateLayer0Taiji()
{
    const int CENTER_X = 180;
    const int CENTER_Y = 180;

    ESP_LOGI(TAG, "Creating Layer0 Taiji (radius=%d, canvas=%d, fisheye=%d)",
             TAIJI_RADIUS, TAIJI_CANVAS_SIZE, FISHEYE_ICON_SIZE);
    CompassTaiji::Create(attitude_container_, CENTER_X, CENTER_Y, TAIJI_RADIUS);
    CreateTaijiGoldPulseArc();
}

void AttitudeDisplay::CreateTaijiGoldPulseArc()
{
    lv_obj_t* container = CompassTaiji::GetContainer();
    if (container == nullptr) {
        return;
    }
    const auto& c = AttitudeTheme::GetInstance().GetColors();
    taiji_gold_pulse_arc_ = lv_arc_create(container);
    lv_obj_set_size(taiji_gold_pulse_arc_, TAIJI_CANVAS_SIZE, TAIJI_CANVAS_SIZE);
    lv_obj_set_pos(taiji_gold_pulse_arc_, 0, 0);
    StyleSmoothGoldArc(taiji_gold_pulse_arc_, c.border_line, TAIJI_GOLD_RING_WIDTH);
    lv_obj_add_flag(taiji_gold_pulse_arc_, LV_OBJ_FLAG_HIDDEN);
    ESP_LOGI(TAG, "Taiji gold pulse arc created (%dx%d, %dpx)", TAIJI_CANVAS_SIZE,
             TAIJI_CANVAS_SIZE, TAIJI_GOLD_RING_WIDTH);
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
    lv_obj_set_size(bg_layer_center_, BG_LAYER_CENTER_SIZE, BG_LAYER_CENTER_SIZE);
    lv_obj_set_style_radius(bg_layer_center_, BG_LAYER_CENTER_SIZE / 2, 0);
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
    lv_obj_set_size(layer1_container_, TAIJI_CANVAS_SIZE, TAIJI_CANVAS_SIZE);
    lv_obj_set_pos(layer1_container_, CENTER_X - TAIJI_RADIUS, CENTER_Y - TAIJI_RADIUS);
    lv_obj_set_style_radius(layer1_container_, TAIJI_RADIUS, 0);
    lv_obj_set_style_bg_opa(layer1_container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(layer1_container_, 0, 0);
    lv_obj_set_style_pad_all(layer1_container_, 0, 0);
    lv_obj_clear_flag(layer1_container_, LV_OBJ_FLAG_CLICKABLE);

    ESP_LOGI(TAG, "Layer1 CoreInfo created (r=%d)", TAIJI_RADIUS);
}

void AttitudeDisplay::CreateLayer3StatusProgress()
{
    // 暂时不创建 L3 背景弧 / 进度弧，便于真机对比
    layer3_bg_arc_ = nullptr;
    layer3_progress_arc_ = nullptr;
    ESP_LOGI(TAG, "Layer3 StatusProgress disabled");
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

void AttitudeDisplay::StartFisheyeBorderPulse(lv_obj_t* obj, uint32_t c1, uint32_t c2)
{
    if (obj == nullptr) {
        return;
    }

    lv_anim_delete(obj, FisheyeBorderColorAnimCb);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, obj);
    lv_anim_set_exec_cb(&anim, FisheyeBorderColorAnimCb);
    lv_anim_set_values(&anim, static_cast<int32_t>(c1), static_cast<int32_t>(c2));
    lv_anim_set_duration(&anim, FISHEYE_PULSE_MS * 2);
    lv_anim_set_playback_duration(&anim, FISHEYE_PULSE_MS * 2);
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&anim);
}

void AttitudeDisplay::StopFisheyePulse(lv_obj_t* obj)
{
    if (obj == nullptr) {
        return;
    }

    lv_anim_delete(obj, FisheyeOpaAnimCb);
    lv_anim_delete(obj, FisheyeBorderColorAnimCb);
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
    lv_obj_set_style_bg_color(ble_fisheye_, kFisheyeWhite, 0);
    lv_obj_set_style_bg_opa(ble_fisheye_, LV_OPA_COVER, 0);
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
        // 白底 + 灰边灰标：BLE 关闭
        lv_obj_set_style_bg_color(ble_fisheye_, kFisheyeWhite, 0);
        lv_obj_set_style_bg_opa(ble_fisheye_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(ble_fisheye_, kFisheyeGrayIcon, 0);
        lv_obj_set_style_text_color(ble_fisheye_icon_, kFisheyeGrayIcon, 0);
        lv_label_set_text(ble_fisheye_icon_, FONT_AWESOME_BLUETOOTH);
        break;
    case BleStatus::ADVERTISING:
        // 白底 + 白/灰边框脉冲：广播中
        lv_obj_set_style_bg_color(ble_fisheye_, kFisheyeWhite, 0);
        lv_obj_set_style_bg_opa(ble_fisheye_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(ble_fisheye_, kFisheyeWhite, 0);
        lv_obj_set_style_text_color(ble_fisheye_icon_, kFisheyeGrayIcon, 0);
        lv_label_set_text(ble_fisheye_icon_, FONT_AWESOME_BLUETOOTH);
        StartFisheyeBorderPulse(ble_fisheye_, kBleBorderGray, kBleBorderWhite);
        break;
    case BleStatus::CONNECTED:
        // 白底 + 蓝边蓝标：已连接
        lv_obj_set_style_bg_color(ble_fisheye_, kFisheyeWhite, 0);
        lv_obj_set_style_bg_opa(ble_fisheye_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(ble_fisheye_, kFisheyeBleBlue, 0);
        lv_obj_set_style_text_color(ble_fisheye_icon_, kFisheyeBleBlue, 0);
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

// ---------------------------------------------------------------------------
// 迭代 2: AI 运势三态状态机 + 200×240 结果卡
// ---------------------------------------------------------------------------

namespace {

static const uint32_t kPulseGold = 0xFFD700;
static const uint32_t kPulseBrightGold = 0xFFED4E;
static const uint32_t kBorderGold = 0xD4AF37;
static const uint32_t kBorderBrightGold = 0xFFD700;
static const lv_color_t kFortuneGreen = lv_color_hex(0x4CAF50);
static const lv_color_t kFortuneRed = lv_color_hex(0xE53935);

static const uint8_t kHexagramBits[64] = {
    0b000000, 0b000001, 0b000010, 0b000011, 0b000100, 0b000101, 0b000110, 0b000111,
    0b001000, 0b001001, 0b001010, 0b001011, 0b001100, 0b001101, 0b001110, 0b001111,
    0b010000, 0b010001, 0b010010, 0b010011, 0b010100, 0b010101, 0b010110, 0b010111,
    0b011000, 0b011001, 0b011010, 0b011011, 0b011100, 0b011101, 0b011110, 0b011111,
    0b100000, 0b100001, 0b100010, 0b100011, 0b100100, 0b100101, 0b100110, 0b100111,
    0b101000, 0b101001, 0b101010, 0b101011, 0b101100, 0b101101, 0b101110, 0b101111,
    0b110000, 0b110001, 0b110010, 0b110011, 0b110100, 0b110101, 0b110110, 0b110111,
    0b111000, 0b111001, 0b111010, 0b111011, 0b111100, 0b111101, 0b111110, 0b111111,
};

static void StartColorPulse(lv_obj_t* obj, lv_anim_exec_xcb_t exec_cb,
                            uint32_t c1, uint32_t c2, int repeat_count)
{
    if (obj == nullptr) {
        return;
    }

    lv_anim_delete(obj, exec_cb);
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, obj);
    lv_anim_set_exec_cb(&anim, exec_cb);
    lv_anim_set_values(&anim, static_cast<int32_t>(c1), static_cast<int32_t>(c2));
    lv_anim_set_duration(&anim, FORTUNE_PULSE_MS);
    lv_anim_set_playback_duration(&anim, FORTUNE_PULSE_MS);
    lv_anim_set_repeat_count(&anim, repeat_count);
    lv_anim_start(&anim);
}

static int FortuneTaijiStepForElapsed(uint32_t elapsed_ms)
{
    // 每 4s 一档：0.2→0.4→0.4→0.4→0.2 圈（0.1 圈精度，分子/10）
    static constexpr int kRotationTenths[] = {2, 4, 4, 4, 2};
    if (elapsed_ms >= FORTUNE_ANIM_DURATION_MS) {
        return 0;
    }
    const int phase = static_cast<int>(elapsed_ms / FORTUNE_TAIJI_PHASE_MS);
    if (phase < 0 || phase >= FORTUNE_TAIJI_PHASE_COUNT) {
        return 0;
    }
    const int rev_tenths = kRotationTenths[phase];
    return 3600 * rev_tenths * FORTUNE_TAIJI_TICK_MS / (FORTUNE_TAIJI_PHASE_MS * 10);
}

static lv_obj_t* CreateHexagramWidget(lv_obj_t* parent, int gua_index)
{
    if (gua_index < 0 || gua_index > 63) {
        gua_index = 63;
    }
    const uint8_t bits = kHexagramBits[gua_index];
    const lv_color_t gold = lv_color_hex(kPulseGold);

    lv_obj_t* box = lv_obj_create(parent);
    lv_obj_set_size(box, FORTUNE_GUA_CANVAS_W, FORTUNE_GUA_CANVAS_H);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    const int line_w = 52;
    const int line_h = 3;
    const int gap_y = 5;
    const int yin_gap = 8;
    int y = 4;
    for (int i = 5; i >= 0; --i) {
        const bool yang = (bits >> i) & 0x01;
        const int x0 = (FORTUNE_GUA_CANVAS_W - line_w) / 2;
        if (yang) {
            lv_obj_t* bar = lv_obj_create(box);
            lv_obj_set_size(bar, line_w, line_h);
            lv_obj_set_pos(bar, x0, y);
            lv_obj_set_style_radius(bar, 1, 0);
            lv_obj_set_style_bg_color(bar, gold, 0);
            lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(bar, 0, 0);
        } else {
            const int half = (line_w - yin_gap) / 2;
            lv_obj_t* left = lv_obj_create(box);
            lv_obj_set_size(left, half, line_h);
            lv_obj_set_pos(left, x0, y);
            lv_obj_set_style_radius(left, 1, 0);
            lv_obj_set_style_bg_color(left, gold, 0);
            lv_obj_set_style_bg_opa(left, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(left, 0, 0);

            lv_obj_t* right = lv_obj_create(box);
            lv_obj_set_size(right, half, line_h);
            lv_obj_set_pos(right, x0 + half + yin_gap, y);
            lv_obj_set_style_radius(right, 1, 0);
            lv_obj_set_style_bg_color(right, gold, 0);
            lv_obj_set_style_bg_opa(right, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(right, 0, 0);
        }
        y += line_h + gap_y;
    }
    return box;
}

} // namespace

lv_obj_t* AttitudeDisplay::GetDirectionDot(int dir)
{
    switch (dir) {
    case 0: return dir_n_label_;
    case 1: return dir_e_label_;
    case 2: return dir_s_label_;
    case 3: return dir_w_label_;
    default: return nullptr;
    }
}

void AttitudeDisplay::EnsureDirectionDot(int dir)
{
    static const int kPos[4][2] = {
        {177, 106}, {249, 177}, {177, 249}, {105, 177},
    };

    if (dir < 0 || dir > 3) {
        return;
    }

    lv_obj_t** slot = nullptr;
    switch (dir) {
    case 0: slot = &dir_n_label_; break;
    case 1: slot = &dir_e_label_; break;
    case 2: slot = &dir_s_label_; break;
    case 3: slot = &dir_w_label_; break;
    default: return;
    }

    if (*slot != nullptr) {
        return;
    }

    const auto& c = AttitudeTheme::GetInstance().GetColors();
    *slot = lv_obj_create(attitude_container_);
    lv_obj_set_size(*slot, COMPASS_DOT_SIZE, COMPASS_DOT_SIZE);
    lv_obj_set_pos(*slot, kPos[dir][0], kPos[dir][1]);
    lv_obj_set_style_radius(*slot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(*slot, 0, 0);
    lv_obj_set_style_pad_all(*slot, 0, 0);
    lv_obj_set_style_bg_color(*slot, c.point_default, 0);
    lv_obj_set_style_bg_opa(*slot, LV_OPA_COVER, 0);
    lv_obj_clear_flag(*slot, LV_OBJ_FLAG_CLICKABLE);
    ESP_LOGI(TAG, "Direction dot created dir=%d at (%d,%d)", dir, kPos[dir][0], kPos[dir][1]);
}

void AttitudeDisplay::HideDirectionDots()
{
    lv_obj_t* dots[] = {dir_n_label_, dir_e_label_, dir_s_label_, dir_w_label_};
    for (lv_obj_t* dot : dots) {
        if (dot != nullptr) {
            lv_anim_delete(dot, DotColorAnimCb);
            lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void AttitudeDisplay::StopFortuneAnimatingEffects(bool restore_taiji_rotation)
{
    if (attitude_container_ != nullptr) {
        lv_obj_set_style_opa(attitude_container_, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_opa(attitude_container_, LV_OPA_COVER, 0);
    }
    if (bg_layer_center_ != nullptr) {
        lv_anim_delete(bg_layer_center_, ContainerOpaAnimCb);
        lv_obj_set_style_opa(bg_layer_center_, LV_OPA_COVER, 0);
        lv_obj_set_size(bg_layer_center_, BG_LAYER_CENTER_SIZE, BG_LAYER_CENTER_SIZE);
        lv_obj_set_style_radius(bg_layer_center_, BG_LAYER_CENTER_SIZE / 2, 0);
        lv_obj_center(bg_layer_center_);
    }
    if (layer4_outer_ring_ != nullptr) {
        const auto& c = AttitudeTheme::GetInstance().GetColors();
        lv_obj_set_style_arc_color(layer4_outer_ring_, c.border_line, LV_PART_INDICATOR);
    }
    if (taiji_gold_pulse_arc_ != nullptr) {
        lv_anim_delete(taiji_gold_pulse_arc_, ArcColorAnimCb);
        const auto& c = AttitudeTheme::GetInstance().GetColors();
        lv_obj_set_style_arc_color(taiji_gold_pulse_arc_, c.border_line, LV_PART_INDICATOR);
        lv_obj_add_flag(taiji_gold_pulse_arc_, LV_OBJ_FLAG_HIDDEN);
    }
    if (layer3_progress_arc_ != nullptr) {
        lv_anim_delete(layer3_progress_arc_, ProgressArcValueAnimCb);
        lv_arc_set_value(layer3_progress_arc_, 0);
        lv_arc_set_angles(layer3_progress_arc_, 0, 0);
    }
    if (fortune_progress_timer_ != nullptr) {
        lv_timer_delete(fortune_progress_timer_);
        fortune_progress_timer_ = nullptr;
    }
    fortune_progress_step_ = 0;
    if (fortune_fisheye_pulse_timer_ != nullptr) {
        lv_timer_delete(fortune_fisheye_pulse_timer_);
        fortune_fisheye_pulse_timer_ = nullptr;
    }
    if (fortune_taiji_ramp_timer_ != nullptr) {
        lv_timer_delete(fortune_taiji_ramp_timer_);
        fortune_taiji_ramp_timer_ = nullptr;
    }
    if (fortune_result_delay_timer_ != nullptr) {
        lv_timer_delete(fortune_result_delay_timer_);
        fortune_result_delay_timer_ = nullptr;
    }
    fortune_fisheye_pulse_count_ = 0;
    if (restore_taiji_rotation) {
        if (CompassTaiji::IsAutoRotating()) {
            CompassTaiji::SetAutoRotationPeriod(TAIJI_ROTATION_PERIOD_NORMAL_MS);
            CompassTaiji::SetAutoRotationPaused(false);
        } else {
            CompassTaiji::StartAutoRotation(TAIJI_ROTATION_PERIOD_NORMAL_MS);
        }
    }
    StopFisheyePulse(wifi_fisheye_);
    StopFisheyePulse(ble_fisheye_);
    ApplyWifiFisheyeStyle(wifi_status_);
    ApplyBleFisheyeStyle(ble_status_);
}

void AttitudeDisplay::StartFortuneTaijiPhasedRotation()
{
    if (fortune_taiji_ramp_timer_ != nullptr) {
        lv_timer_delete(fortune_taiji_ramp_timer_);
        fortune_taiji_ramp_timer_ = nullptr;
    }

    fortune_taiji_ramp_start_tick_ = lv_tick_get();
    if (CompassTaiji::IsAutoRotating()) {
        CompassTaiji::StopAutoRotation();
    }
    fortune_taiji_ramp_timer_ = lv_timer_create(OnFortuneTaijiRampTimer, FORTUNE_TAIJI_TICK_MS, this);
    ESP_LOGI(TAG, "Taiji phased rotation: 0.2+0.4+0.4+0.4+0.2 rev / 20s");
}

void AttitudeDisplay::OnFortuneProgressStepTimer(lv_timer_t* timer)
{
    auto* self = static_cast<AttitudeDisplay*>(lv_timer_get_user_data(timer));
    if (self == nullptr || self->fortune_state_ != FortuneState::Animating) {
        return;
    }
    self->fortune_progress_step_++;
    const int value = self->fortune_progress_step_ * 36;
    if (self->layer3_progress_arc_ != nullptr) {
        lv_arc_set_value(self->layer3_progress_arc_, value);
        lv_arc_set_angles(self->layer3_progress_arc_, 0, value);
    }
    if (value >= 360) {
        lv_timer_delete(timer);
        self->fortune_progress_timer_ = nullptr;
    }
}

void AttitudeDisplay::OnFortuneTaijiRampTimer(lv_timer_t* timer)
{
    auto* self = static_cast<AttitudeDisplay*>(lv_timer_get_user_data(timer));
    if (self == nullptr || self->fortune_state_ != FortuneState::Animating) {
        return;
    }
    const uint32_t elapsed = lv_tick_elaps(self->fortune_taiji_ramp_start_tick_);
    const int step = FortuneTaijiStepForElapsed(elapsed);
    if (step > 0) {
        CompassTaiji::Rotate(step);
    }
}

void AttitudeDisplay::OnFortuneAnimTimer(lv_timer_t* timer)
{
    auto* self = static_cast<AttitudeDisplay*>(lv_timer_get_user_data(timer));
    if (self == nullptr) {
        return;
    }
    self->fortune_anim_timer_ = nullptr;
    lv_timer_delete(timer);

    if (self->fortune_state_ != FortuneState::Animating) {
        return;
    }

    // 20s 动画结束：停视觉效果，恢复常态转速，约 1s 后再出结果卡
    self->StopFortuneAnimatingEffects(false);
    if (!CompassTaiji::IsAutoRotating()) {
        CompassTaiji::StartAutoRotation(TAIJI_ROTATION_PERIOD_NORMAL_MS);
    } else {
        CompassTaiji::SetAutoRotationPeriod(TAIJI_ROTATION_PERIOD_NORMAL_MS);
        CompassTaiji::SetAutoRotationPaused(false);
    }

    if (self->fortune_result_delay_timer_ != nullptr) {
        lv_timer_delete(self->fortune_result_delay_timer_);
        self->fortune_result_delay_timer_ = nullptr;
    }
    self->fortune_result_delay_timer_ = lv_timer_create(OnFortuneResultDelayTimer,
                                                        FORTUNE_RESULT_CARD_DELAY_MS, self);
    lv_timer_set_repeat_count(self->fortune_result_delay_timer_, 1);
    ESP_LOGI(TAG, "Fortune anim done, normal taiji %dms, card in %dms",
             TAIJI_ROTATION_PERIOD_NORMAL_MS, FORTUNE_RESULT_CARD_DELAY_MS);
}

void AttitudeDisplay::OnFortuneResultDelayTimer(lv_timer_t* timer)
{
    auto* self = static_cast<AttitudeDisplay*>(lv_timer_get_user_data(timer));
    if (self == nullptr) {
        return;
    }
    self->fortune_result_delay_timer_ = nullptr;
    lv_timer_delete(timer);
    if (self->fortune_state_ == FortuneState::Animating) {
        self->EnterResultState();
    }
}

void AttitudeDisplay::OnFortuneResultTimer(lv_timer_t* timer)
{
    auto* self = static_cast<AttitudeDisplay*>(lv_timer_get_user_data(timer));
    if (self == nullptr) {
        return;
    }
    self->fortune_result_timer_ = nullptr;
    lv_timer_delete(timer);
    ESP_LOGI(TAG, "Fortune result timeout -> Idle");
    self->EnterIdleState();
}

void AttitudeDisplay::OnFortuneFisheyePulseTimer(lv_timer_t* timer)
{
    auto* self = static_cast<AttitudeDisplay*>(lv_timer_get_user_data(timer));
    if (self == nullptr || self->fortune_state_ != FortuneState::Animating) {
        return;
    }

    self->fortune_fisheye_pulse_gold_ = !self->fortune_fisheye_pulse_gold_;
    const lv_color_t bg = self->fortune_fisheye_pulse_gold_ ? kFisheyeGold : kFisheyeGrayBg;
    const lv_color_t border = self->fortune_fisheye_pulse_gold_ ? kFisheyeGold : kFisheyeGrayBorder;
    const lv_color_t icon = self->fortune_fisheye_pulse_gold_ ? kFisheyeDark : kFisheyeGrayIcon;

    if (self->wifi_fisheye_ != nullptr) {
        lv_obj_set_style_bg_color(self->wifi_fisheye_, bg, 0);
        lv_obj_set_style_border_color(self->wifi_fisheye_, border, 0);
        lv_obj_set_style_text_color(self->wifi_fisheye_icon_, icon, 0);
    }
    if (self->ble_fisheye_ != nullptr) {
        lv_obj_set_style_bg_color(self->ble_fisheye_, kFisheyeWhite, 0);
        lv_obj_set_style_bg_opa(self->ble_fisheye_, LV_OPA_COVER, 0);
        const lv_color_t ble_border = self->fortune_fisheye_pulse_gold_
            ? kFisheyeWhite : kFisheyeGrayIcon;
        lv_obj_set_style_border_color(self->ble_fisheye_, ble_border, 0);
        lv_obj_set_style_text_color(self->ble_fisheye_icon_, kFisheyeGrayIcon, 0);
    }

    self->fortune_fisheye_pulse_count_++;
    if (self->fortune_fisheye_pulse_count_ >= 5) {
        lv_timer_delete(timer);
        self->fortune_fisheye_pulse_timer_ = nullptr;
    }
}

void AttitudeDisplay::ShowFortune(const std::string& func_label, const std::string& gua_name,
                                  const std::string& core_text, const std::string& yi,
                                  const std::string& ji, int gua_index, int dir_index)
{
    DisplayLockGuard lock(this);

    if (fortune_state_ != FortuneState::Idle) {
        EnterIdleState();
    }

    fortune_func_label_text_ = func_label;
    fortune_gua_name_text_ = gua_name;
    fortune_core_text_ = core_text;
    fortune_yi_text_ = yi;
    fortune_ji_text_ = ji;
    fortune_gua_index_ = gua_index;
    fortune_highlight_dir_ = dir_index;

    ESP_LOGI(TAG, "ShowFortune: %s / %s dir=%d gua=%d",
             func_label.c_str(), gua_name.c_str(), dir_index, gua_index);
    EnterAnimatingState();
}

void AttitudeDisplay::EnterAnimatingState()
{
    fortune_state_ = FortuneState::Animating;
    StopFortuneAnimatingEffects();

    if (bg_layer_center_ != nullptr) {
        lv_obj_set_size(bg_layer_center_, FORTUNE_PULSE_CENTER_SIZE, FORTUNE_PULSE_CENTER_SIZE);
        lv_obj_set_style_radius(bg_layer_center_, FORTUNE_PULSE_CENTER_SIZE / 2, 0);
        lv_obj_center(bg_layer_center_);
        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, bg_layer_center_);
        lv_anim_set_exec_cb(&anim, ContainerOpaAnimCb);
        lv_anim_set_values(&anim, 180, 255);
        lv_anim_set_duration(&anim, FORTUNE_PULSE_MS);
        lv_anim_set_playback_duration(&anim, FORTUNE_PULSE_MS);
        lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&anim);
    }

    // 亮度 / 太极金边 / 鱼眼与 20s Animating 同步（L4 外圈保持静态）
    if (taiji_gold_pulse_arc_ != nullptr) {
        lv_obj_remove_flag(taiji_gold_pulse_arc_, LV_OBJ_FLAG_HIDDEN);
        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, taiji_gold_pulse_arc_);
        lv_anim_set_exec_cb(&anim, ArcColorAnimCb);
        lv_anim_set_values(&anim, static_cast<int32_t>(kBorderGold),
                           static_cast<int32_t>(kBorderBrightGold));
        lv_anim_set_duration(&anim, FORTUNE_PULSE_MS);
        lv_anim_set_playback_duration(&anim, FORTUNE_PULSE_MS);
        lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&anim);
    }

    if (layer3_progress_arc_ != nullptr) {
        lv_arc_set_value(layer3_progress_arc_, 0);
        lv_arc_set_angles(layer3_progress_arc_, 0, 0);
        fortune_progress_step_ = 0;
        if (fortune_progress_timer_ != nullptr) {
            lv_timer_delete(fortune_progress_timer_);
            fortune_progress_timer_ = nullptr;
        }
        fortune_progress_timer_ = lv_timer_create(OnFortuneProgressStepTimer,
                                                FORTUNE_PROGRESS_STEP_MS, this);
    }

    fortune_fisheye_pulse_count_ = 0;
    fortune_fisheye_pulse_gold_ = true;
    fortune_fisheye_pulse_timer_ = lv_timer_create(OnFortuneFisheyePulseTimer,
                                                   FORTUNE_PULSE_MS, this);

    StartFortuneTaijiPhasedRotation();

    if (fortune_anim_timer_ != nullptr) {
        lv_timer_delete(fortune_anim_timer_);
    }
    fortune_anim_timer_ = lv_timer_create(OnFortuneAnimTimer, FORTUNE_ANIM_DURATION_MS, this);
    lv_timer_set_repeat_count(fortune_anim_timer_, 1);

    ESP_LOGI(TAG, "Fortune -> Animating (%dms, taiji 0.2+0.4+0.4+0.4+0.2 rev/4s, card after %dms normal)",
             FORTUNE_ANIM_DURATION_MS, FORTUNE_RESULT_CARD_DELAY_MS);
}

void AttitudeDisplay::CreateFortuneCard()
{
    DestroyFortuneCard();

    const auto& c = AttitudeTheme::GetInstance().GetColors();
    auto lvgl_theme = static_cast<LvglTheme*>(GetTheme());
    const lv_font_t* text_font = (lvgl_theme != nullptr && lvgl_theme->text_font() != nullptr)
        ? lvgl_theme->text_font()->font() : &BUILTIN_TEXT_FONT;

    fortune_card_ = lv_obj_create(attitude_container_);
    lv_obj_set_size(fortune_card_, FORTUNE_CARD_W, FORTUNE_CARD_H);
    lv_obj_set_pos(fortune_card_, 80, 60);
    lv_obj_set_style_radius(fortune_card_, 100, 0);
    lv_obj_set_style_bg_color(fortune_card_, c.card_bg, 0);
    lv_obj_set_style_bg_opa(fortune_card_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(fortune_card_, c.border_line, 0);
    lv_obj_set_style_border_width(fortune_card_, 2, 0);
    lv_obj_set_style_pad_all(fortune_card_, 8, 0);
    lv_obj_set_style_pad_row(fortune_card_, 4, 0);
    lv_obj_set_style_opa(fortune_card_, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(fortune_card_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(fortune_card_, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(fortune_card_, LV_OBJ_FLAG_CLICKABLE);

    fortune_func_label_ = lv_label_create(fortune_card_);
    lv_obj_set_style_text_font(fortune_func_label_, text_font, 0);
    lv_obj_set_style_text_color(fortune_func_label_, c.text_main, 0);
    lv_label_set_text(fortune_func_label_, fortune_func_label_text_.c_str());

    fortune_gua_widget_ = CreateHexagramWidget(fortune_card_, fortune_gua_index_);

    fortune_gua_name_label_ = lv_label_create(fortune_card_);
    lv_obj_set_style_text_font(fortune_gua_name_label_, text_font, 0);
    lv_obj_set_style_text_color(fortune_gua_name_label_, c.text_main, 0);
    lv_label_set_text(fortune_gua_name_label_, fortune_gua_name_text_.c_str());

    fortune_core_label_ = lv_label_create(fortune_card_);
    lv_obj_set_style_text_font(fortune_core_label_, text_font, 0);
    lv_obj_set_style_text_color(fortune_core_label_, c.text_high, 0);
    lv_obj_set_width(fortune_core_label_, FORTUNE_CARD_W - 24);
    lv_label_set_long_mode(fortune_core_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(fortune_core_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(fortune_core_label_, fortune_core_text_.c_str());

    fortune_yi_label_ = lv_label_create(fortune_card_);
    lv_obj_set_style_text_font(fortune_yi_label_, text_font, 0);
    lv_obj_set_style_text_color(fortune_yi_label_, kFortuneGreen, 0);
    lv_obj_set_width(fortune_yi_label_, FORTUNE_CARD_W - 24);
    lv_label_set_long_mode(fortune_yi_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(fortune_yi_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(fortune_yi_label_, fortune_yi_text_.c_str());

    fortune_ji_label_ = lv_label_create(fortune_card_);
    lv_obj_set_style_text_font(fortune_ji_label_, text_font, 0);
    lv_obj_set_style_text_color(fortune_ji_label_, kFortuneRed, 0);
    lv_obj_set_width(fortune_ji_label_, FORTUNE_CARD_W - 24);
    lv_label_set_long_mode(fortune_ji_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(fortune_ji_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(fortune_ji_label_, fortune_ji_text_.c_str());

    fortune_close_label_ = lv_label_create(fortune_card_);
    lv_obj_set_style_text_font(fortune_close_label_, text_font, 0);
    lv_obj_set_style_text_color(fortune_close_label_, c.text_sub, 0);
    lv_label_set_text(fortune_close_label_, "按 Boot 关闭");

    lv_obj_move_foreground(fortune_card_);
    ESP_LOGI(TAG, "Fortune card shown 200x240 at (80,60)");
}

void AttitudeDisplay::DestroyFortuneCard()
{
    if (fortune_gua_name_label_ != nullptr) {
        lv_anim_delete(fortune_gua_name_label_, LabelColorAnimCb);
    }

    if (fortune_card_ != nullptr) {
        lv_obj_delete(fortune_card_);
        fortune_card_ = nullptr;
    }

    fortune_func_label_ = nullptr;
    fortune_gua_widget_ = nullptr;
    fortune_gua_name_label_ = nullptr;
    fortune_core_label_ = nullptr;
    fortune_yi_label_ = nullptr;
    fortune_ji_label_ = nullptr;
    fortune_close_label_ = nullptr;
}

void AttitudeDisplay::EnterResultState()
{
    if (fortune_anim_timer_ != nullptr) {
        lv_timer_delete(fortune_anim_timer_);
        fortune_anim_timer_ = nullptr;
    }
    if (fortune_result_delay_timer_ != nullptr) {
        lv_timer_delete(fortune_result_delay_timer_);
        fortune_result_delay_timer_ = nullptr;
    }

    // 先停动画效果，暂不恢复后台旋转，避免与建卡同帧抢 LVGL
    StopFortuneAnimatingEffects(false);
    fortune_state_ = FortuneState::Result;

    CreateFortuneCard();
    HighlightDirection(fortune_highlight_dir_);
    HighlightGua(fortune_gua_index_);

    if (!CompassTaiji::IsAutoRotating()) {
        CompassTaiji::StartAutoRotation(TAIJI_ROTATION_PERIOD_NORMAL_MS);
    } else {
        CompassTaiji::SetAutoRotationPeriod(TAIJI_ROTATION_PERIOD_NORMAL_MS);
    }

    if (fortune_result_timer_ != nullptr) {
        lv_timer_delete(fortune_result_timer_);
    }
    fortune_result_timer_ = lv_timer_create(OnFortuneResultTimer, FORTUNE_RESULT_TIMEOUT_MS, this);
    lv_timer_set_repeat_count(fortune_result_timer_, 1);

    ESP_LOGI(TAG, "Fortune -> Result card shown (%ds timeout)", FORTUNE_RESULT_TIMEOUT_MS / 1000);
}

void AttitudeDisplay::EnterIdleState()
{
    if (fortune_anim_timer_ != nullptr) {
        lv_timer_delete(fortune_anim_timer_);
        fortune_anim_timer_ = nullptr;
    }
    if (fortune_result_delay_timer_ != nullptr) {
        lv_timer_delete(fortune_result_delay_timer_);
        fortune_result_delay_timer_ = nullptr;
    }
    if (fortune_result_timer_ != nullptr) {
        lv_timer_delete(fortune_result_timer_);
        fortune_result_timer_ = nullptr;
    }

    StopFortuneAnimatingEffects();
    DestroyFortuneCard();
    HideDirectionDots();

    fortune_state_ = FortuneState::Idle;
    ESP_LOGI(TAG, "Fortune -> Idle");
}

void AttitudeDisplay::DismissFortune()
{
    DisplayLockGuard lock(this);
    if (fortune_state_ == FortuneState::Idle) {
        return;
    }
    EnterIdleState();
}

void AttitudeDisplay::HighlightDirection(int dir)
{
    HideDirectionDots();
    EnsureDirectionDot(dir);

    lv_obj_t* dot = GetDirectionDot(dir);
    if (dot == nullptr) {
        return;
    }

    lv_obj_clear_flag(dot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(dot, lv_color_hex(kPulseGold), 0);
    StartColorPulse(dot, DotColorAnimCb, kPulseGold, kPulseBrightGold, 3);
    ESP_LOGI(TAG, "HighlightDirection dir=%d", dir);
}

void AttitudeDisplay::HighlightGua(int gua_idx)
{
    if (fortune_gua_name_label_ != nullptr) {
        StartColorPulse(fortune_gua_name_label_, LabelColorAnimCb, kPulseGold, kPulseBrightGold, 3);
    }

    ESP_LOGI(TAG, "HighlightGua idx=%d (card inner pulse)", gua_idx);
}

bool AttitudeDisplay::HandleBootKey()
{
    DisplayLockGuard lock(this);

    if (fortune_state_ == FortuneState::Result) {
        EnterIdleState();
        return true;
    }
    if (fortune_state_ == FortuneState::Animating) {
        return true;
    }

    fortune_func_label_text_ = "今日运势 ☀";
    fortune_gua_name_text_ = "乾为天";
    fortune_core_text_ = "今日宜进取，顺势而行。";
    fortune_yi_text_ = "宜：签约、出行";
    fortune_ji_text_ = "忌：熬夜、口舌";
    fortune_gua_index_ = 63;
    fortune_highlight_dir_ = 0;
    ESP_LOGI(TAG, "Boot key demo ShowFortune");
    EnterAnimatingState();
    return true;
}
