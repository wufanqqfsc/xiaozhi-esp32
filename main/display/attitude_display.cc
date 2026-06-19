#include "attitude_display.h"
#include "lvgl_theme.h"
#include "application.h"
#include "assets/lang_config.h"
#include "board.h"
#include "compass_taiji.h"
#if STUDY_SUB_FEATURES_ENABLED
#include "drum/drum_synth.h"
#endif
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <inttypes.h>
#include <ctime>
#include <cmath>
#include <font_awesome.h>

#define TAG "AttitudeDisplay"

// 调试信息卡（与后台交互事件）配置
#define DEBUG_INFO_SHOW_MS        5000   // 功能区提示卡默认显示时长
#define DEBUG_INFO_HOLD_MAX_MS    10000  // 联动音频播放时的最大允许显示时长（兜底）
#define DEBUG_INFO_DEDUP_MS       1500   // 同一标题的去重间隔
// 调试卡配色：与运势卡（金）区分，使用青/品红强调，便于识别
#define DEBUG_INFO_BORDER_COLOR   lv_color_hex(0x00C8C8)   // 青色描边
#define DEBUG_INFO_TITLE_COLOR    lv_color_hex(0xD4AF37)  // 金色
#define DEBUG_INFO_DETAIL_COLOR   lv_color_hex(0xE0E0E0)
#define TAIJI_ROTATION_PERIOD_NORMAL_MS  60000   // 常态 60s/圈（减慢旋转 + 降低刷屏）
#define BG_LAYER_CENTER_SIZE      270

LV_FONT_DECLARE(BUILTIN_TEXT_FONT);
LV_FONT_DECLARE(BUILTIN_ICON_FONT);
LV_FONT_DECLARE(font_awesome_30_4);
LV_FONT_DECLARE(font_awesome_16_4);
LV_FONT_DECLARE(font_awesome_20_4);

namespace {

// WiFi 鱼眼位：黑底白描边金色图标
// BLE 鱼眼位：白底黑描边蓝色图标
static const lv_color_t kFisheyeGrayIcon = lv_color_hex(0x909090);
static const lv_color_t kFisheyeBleBlue = lv_color_hex(0x2196F3);    // BLE 已连接：蓝色
static const lv_color_t kFisheyeGold = lv_color_hex(0xD4AF37);
static const lv_color_t kFisheyeWhite = lv_color_hex(0xFFFFFF);
static const lv_color_t kFisheyeDark = lv_color_hex(0x0A0A0A);
// WiFi 鱼眼：黑色底 + 白色描边 + 金色图标
// BLE 鱼眼：白色底 + 黑色描边 + 蓝色图标
static const lv_color_t kFisheyeWifiBg = lv_color_black();
static const lv_color_t kFisheyeWifiBorder = kFisheyeWhite;
static const lv_color_t kFisheyeBleBg = lv_color_white();
static const lv_color_t kFisheyeBleBorder = lv_color_black();

// 鱼眼专用字体：32px widget + 20px icon（较原 16px 整体视觉 +20%）
static const lv_font_t* GetFisheyeIconFont()
{
    return &font_awesome_20_4;
}

static void FisheyeOpaAnimCb(void* obj, int32_t value)
{
    lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), static_cast<lv_opa_t>(value), 0);
}

static void FisheyeBorderColorAnimCb(void* obj, int32_t value)
{
    lv_obj_set_style_border_color(static_cast<lv_obj_t*>(obj),
                                   lv_color_hex(static_cast<uint32_t>(value)), 0);
}

struct FortuneMenuItemDef {
    const char* icon;
    const char* func_label;
    int gua_index;
    int dir_index;
};

/** Boot 长按完整运势卡文案已删除：结果卡（Plan A）已下线，长按不再展示完整运势 */

static const FortuneMenuItemDef kFortuneMenuDefs[FORTUNE_MENU_COUNT] = {
    {FONT_AWESOME_SUN, "今日运势", 63, 0},
    {FONT_AWESOME_CALCULATOR, "财运", 2, 1},
    {FONT_AWESOME_GEAR, "事业运势", 51, 2},
    {FONT_AWESOME_HEART, "感情运势", 58, 3},
    {FONT_AWESOME_MUSIC, "心情卦", 3, 4},
    {FONT_AWESOME_CALENDAR, "黄历宜忌", 30, 5},
    {FONT_AWESOME_CLOUD_SUN, "节气提示", 52, 6},
    {FONT_AWESOME_LOCK, "系统设置", 12, 7},
    {FONT_AWESOME_TEMPERATURE_HALF, "健康运势", 57, 0},
    {FONT_AWESOME_GLASSES, "学业运势", 63, 1},
    {FONT_AWESOME_GLOBE, "出行吉日", 44, 2},
    {FONT_AWESOME_STAR, "贵人运势", 11, 3},
};

static const lv_font_t* GetFortuneMenuIconFont()
{
    return &font_awesome_30_4;
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
    current_theme_ = LvglThemeManager::GetInstance().GetTheme("dark");
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

    // 防御：Setup 阶段确保调试卡为干净状态
    DestroyDebugInfoCard();

#if STUDY_SUB_FEATURES_ENABLED
    drum::DrumSynth::GetInstance().Init();
#endif

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
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_t* round_mask = lv_obj_create(screen);
    lv_obj_set_size(round_mask, 360, 360);
    lv_obj_set_pos(round_mask, 0, 0);
    lv_obj_set_style_radius(round_mask, 180, 0);
    lv_obj_set_style_bg_color(round_mask, c.bg_outer, 0);
    lv_obj_set_style_bg_opa(round_mask, LV_OPA_COVER, 0);
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
#if STUDY_SUB_FEATURES_ENABLED
    CreateStudyArea();
#endif
#if MOOD_GUA_SUB_FEATURES_ENABLED
    CreateMoodGuaArea();
#endif
    UpdateWifiFisheye(WifiStatus::DISCONNECTED);
    UpdateBleFisheye(BleStatus::DISABLED);
    CompassTaiji::StartAutoRotation(TAIJI_ROTATION_PERIOD_NORMAL_MS);
    ESP_LOGI(TAG, "Taiji auto rotation started (period=%dms, fisheyes co-rotate)",
             TAIJI_ROTATION_PERIOD_NORMAL_MS);

    CreateLayer1CoreInfo();
    CreateFortuneMenuRing();
    // L3 状态进度环暂时移除，便于真机对比
    CreateLayer4Boundary();
    CreateFortuneMenuRingTouch();
    // 方位圆点已移除（v1.2+ 视觉简化，运势高亮见迭代 2 再定）

    // 首帧全屏铺深色底，避免 SPI 分块刷新露出开机白底
    lv_obj_invalidate(attitude_container_);
    if (display_ != nullptr) {
        lv_refr_now(display_);
    }

    ESP_LOGI(TAG, "SetupUI completed (taiji+fisheye 90%%, fortune menu ring, L4 outer ring only, L3 disabled)");
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
    lv_obj_set_style_clip_corner(layer1_container_, false, 0);
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

void AttitudeDisplay::CreateFortuneMenuRing()
{
    const auto& c = AttitudeTheme::GetInstance().GetColors();
    const lv_font_t* icon_font = GetFortuneMenuIconFont();

    const double start_rad = FORTUNE_MENU_START_ANGLE_DEG * M_PI / 180.0;
    const double step_rad = 2.0 * M_PI / FORTUNE_MENU_COUNT;

    for (int i = 0; i < FORTUNE_MENU_COUNT; ++i) {
        const double angle = start_rad + step_rad * i;
        const int cx = ATTITUDE_CENTER_X + static_cast<int>(std::lround(
            FORTUNE_MENU_RING_RADIUS * std::cos(angle)));
        const int cy = ATTITUDE_CENTER_Y + static_cast<int>(std::lround(
            FORTUNE_MENU_RING_RADIUS * std::sin(angle)));
        fortune_menu_center_x_[i] = cx;
        fortune_menu_center_y_[i] = cy;

        fortune_menu_labels_[i] = lv_label_create(attitude_container_);
        lv_obj_set_style_text_font(fortune_menu_labels_[i], icon_font, 0);
        lv_obj_set_style_text_color(fortune_menu_labels_[i], c.text_main, 0);
        lv_obj_set_style_bg_opa(fortune_menu_labels_[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(fortune_menu_labels_[i], 0, 0);
        lv_obj_set_style_pad_all(fortune_menu_labels_[i], 0, 0);
        lv_obj_set_style_transform_pivot_x(fortune_menu_labels_[i], LV_PCT(50), 0);
        lv_obj_set_style_transform_pivot_y(fortune_menu_labels_[i], LV_PCT(50), 0);
        lv_label_set_text(fortune_menu_labels_[i], kFortuneMenuDefs[i].icon);
        lv_obj_clear_flag(fortune_menu_labels_[i], LV_OBJ_FLAG_CLICKABLE);
    }

    fortune_menu_selected_index_ = 0;
    fortune_menu_selection_active_ = false;
    UpdateFortuneMenuSelection();

    ESP_LOGI(TAG, "Fortune menu ring: %d icons (~%dpx), r=%d, touch %d~%d",
             FORTUNE_MENU_COUNT, FORTUNE_MENU_ICON_GLYPH_PX,
             FORTUNE_MENU_RING_RADIUS, FORTUNE_MENU_TOUCH_INNER_R,
             FORTUNE_MENU_TOUCH_OUTER_R);
}

void AttitudeDisplay::CreateFortuneMenuRingTouch()
{
    fortune_menu_ring_touch_ = lv_obj_create(attitude_container_);
    lv_obj_set_size(fortune_menu_ring_touch_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_pos(fortune_menu_ring_touch_, 0, 0);
    lv_obj_set_style_bg_opa(fortune_menu_ring_touch_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(fortune_menu_ring_touch_, 0, 0);
    lv_obj_set_style_pad_all(fortune_menu_ring_touch_, 0, 0);
    // 菜单环触摸事件已禁用（功能区提示卡触发事件已全部移除）
    lv_obj_clear_flag(fortune_menu_ring_touch_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(fortune_menu_ring_touch_, LV_OBJ_FLAG_SCROLLABLE);

    // 图标仍展示，但不拦截触摸
    for (int i = 0; i < FORTUNE_MENU_COUNT; ++i) {
        if (fortune_menu_labels_[i] != nullptr) {
            lv_obj_move_foreground(fortune_menu_labels_[i]);
        }
    }
    lv_obj_move_foreground(fortune_menu_ring_touch_);

    ESP_LOGI(TAG, "Fortune menu ring touch layer ready (annulus %d~%d)",
             FORTUNE_MENU_TOUCH_INNER_R, FORTUNE_MENU_TOUCH_OUTER_R);
}

// FortuneMenuIndexFromPoint 已彻底删除：菜单环触摸事件已禁用，扇区坐标→索引映射不再需要

void AttitudeDisplay::PlayFortuneMenuSelectSound()
{
    // 环上运势图标选中静音（学业子功能仍可直接 PlayUiSound）
}

void AttitudeDisplay::SelectFortuneMenuItemUnlocked(int index)
{
    if (index < 0 || index >= FORTUNE_MENU_COUNT) {
        return;
    }
    const int prev = fortune_menu_selected_index_;
    const bool was_active = fortune_menu_selection_active_;
    fortune_menu_selection_active_ = true;
    fortune_menu_selected_index_ = index;
    if (was_active && prev != index) {
        UpdateFortuneMenuItemVisual(prev, false);
    }
    UpdateFortuneMenuItemVisual(index, true);
    // 功能区提示卡触发事件已全部移除（仅保留菜单选中态视觉切换）
    SyncStudyAreaWithMenu();
#if MOOD_GUA_SUB_FEATURES_ENABLED
    SyncMoodGuaAreaWithMenu();
#endif
    ESP_LOGI(TAG, "Fortune menu select -> %d (%s)", index,
             kFortuneMenuDefs[index].func_label);
}

void AttitudeDisplay::SelectFortuneMenuItem(int index)
{
    DisplayLockGuard lock(this);
    SelectFortuneMenuItemUnlocked(index);
}

void AttitudeDisplay::DeselectFortuneMenuItemUnlocked()
{
    const int prev = fortune_menu_selected_index_;
    fortune_menu_selection_active_ = false;
    if (prev >= 0 && prev < FORTUNE_MENU_COUNT) {
        UpdateFortuneMenuItemVisual(prev, false);
    }
    HideDebugInfoUnlocked();
    SyncStudyAreaWithMenu();
#if MOOD_GUA_SUB_FEATURES_ENABLED
    SyncMoodGuaAreaWithMenu();
#endif
}

void AttitudeDisplay::UpdateFortuneMenuItemVisual(int index, bool selected)
{
    if (index < 0 || index >= FORTUNE_MENU_COUNT || fortune_menu_labels_[index] == nullptr) {
        return;
    }
    const auto& c = AttitudeTheme::GetInstance().GetColors();
    const int scale = (selected && fortune_menu_selection_active_)
        ? FORTUNE_MENU_ICON_SCALE_SELECTED
        : FORTUNE_MENU_ICON_SCALE;
    const int cx = fortune_menu_center_x_[index];
    const int cy = fortune_menu_center_y_[index];

    if (fortune_menu_applied_scale_[index] != scale) {
        lv_obj_set_style_transform_scale(fortune_menu_labels_[index], scale, 0);
        fortune_menu_applied_scale_[index] = scale;
        lv_obj_update_layout(fortune_menu_labels_[index]);
    }
    lv_obj_set_style_text_color(fortune_menu_labels_[index],
        selected ? c.text_high : c.text_main, 0);
    const int w = lv_obj_get_width(fortune_menu_labels_[index]);
    const int h = lv_obj_get_height(fortune_menu_labels_[index]);
    lv_obj_set_pos(fortune_menu_labels_[index], cx - w / 2, cy - h / 2);
}

void AttitudeDisplay::UpdateFortuneMenuSelection()
{
    for (int i = 0; i < FORTUNE_MENU_COUNT; ++i) {
        UpdateFortuneMenuItemVisual(i, i == fortune_menu_selected_index_);
    }
}

void AttitudeDisplay::CycleFortuneMenuSelectionUnlocked()
{
    const int prev = fortune_menu_selected_index_;
    fortune_menu_selected_index_ = (prev + 1) % FORTUNE_MENU_COUNT;
    UpdateFortuneMenuItemVisual(prev, false);
    UpdateFortuneMenuItemVisual(fortune_menu_selected_index_, true);
    // 功能区提示卡触发事件已全部移除（仅保留菜单环选中态视觉切换）
    SyncStudyAreaWithMenu();
#if MOOD_GUA_SUB_FEATURES_ENABLED
    SyncMoodGuaAreaWithMenu();
#endif
    ESP_LOGI(TAG, "Fortune menu selected -> %d (%s)",
             fortune_menu_selected_index_,
             kFortuneMenuDefs[fortune_menu_selected_index_].func_label);
}

void AttitudeDisplay::CycleFortuneMenuSelection()
{
    DisplayLockGuard lock(this);
    CycleFortuneMenuSelectionUnlocked();
}

void AttitudeDisplay::SetFortuneMenuVisible(bool visible)
{
    if (fortune_menu_ring_touch_ != nullptr) {
        if (visible) {
            lv_obj_remove_flag(fortune_menu_ring_touch_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(fortune_menu_ring_touch_, LV_OBJ_FLAG_HIDDEN);
        }
    }
    for (int i = 0; i < FORTUNE_MENU_COUNT; ++i) {
        if (fortune_menu_labels_[i] == nullptr) {
            continue;
        }
        if (visible) {
            lv_obj_remove_flag(fortune_menu_labels_[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(fortune_menu_labels_[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// OnFortuneMenuRingTouched 已彻底删除：菜单环触摸事件已禁用（功能区提示卡触发事件已全部移除）

#if STUDY_SUB_FEATURES_ENABLED

void AttitudeDisplay::CreateStudyArea()
{
    lv_obj_t* container = CompassTaiji::GetContainer();
    if (container == nullptr) {
        ESP_LOGE(TAG, "CreateStudyArea: taiji container missing");
        return;
    }

    const auto& c = AttitudeTheme::GetInstance().GetColors();
    const lv_font_t* icon_font = GetFortuneMenuIconFont();
    auto* text_theme = static_cast<LvglTheme*>(LvglThemeManager::GetInstance().GetTheme("dark"));
    const lv_font_t* text_font = text_theme != nullptr && text_theme->text_font() != nullptr
        ? text_theme->text_font()->font() : &BUILTIN_TEXT_FONT;

    study_panel_ = lv_obj_create(container);
    lv_obj_set_size(study_panel_, TAIJI_CANVAS_SIZE, TAIJI_CANVAS_SIZE);
    lv_obj_set_pos(study_panel_, 0, 0);
    lv_obj_set_style_radius(study_panel_, TAIJI_RADIUS, 0);
    lv_obj_set_style_bg_opa(study_panel_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(study_panel_, 0, 0);
    lv_obj_set_style_pad_all(study_panel_, 0, 0);
    lv_obj_clear_flag(study_panel_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(study_panel_, LV_OBJ_FLAG_HIDDEN);

    study_clock_label_ = lv_label_create(study_panel_);
    lv_obj_set_style_text_font(study_clock_label_, icon_font, 0);
    lv_obj_set_style_text_color(study_clock_label_, c.text_main, 0);
    lv_obj_set_style_transform_scale(study_clock_label_, STUDY_ICON_SCALE, 0);
    lv_obj_set_style_transform_pivot_x(study_clock_label_, LV_PCT(50), 0);
    lv_obj_set_style_transform_pivot_y(study_clock_label_, LV_PCT(50), 0);
    lv_label_set_text(study_clock_label_, FONT_AWESOME_CLOCK);
    lv_obj_align(study_clock_label_, LV_ALIGN_CENTER, 0, -STUDY_ICON_OFFSET_Y);
    lv_obj_add_flag(study_clock_label_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(study_clock_label_, OnStudyMenuIconClicked, LV_EVENT_CLICKED, this);

    study_drum_label_ = lv_label_create(study_panel_);
    lv_obj_set_style_text_font(study_drum_label_, icon_font, 0);
    lv_obj_set_style_text_color(study_drum_label_, c.text_main, 0);
    lv_obj_set_style_transform_scale(study_drum_label_, STUDY_ICON_SCALE, 0);
    lv_obj_set_style_transform_pivot_x(study_drum_label_, LV_PCT(50), 0);
    lv_obj_set_style_transform_pivot_y(study_drum_label_, LV_PCT(50), 0);
    lv_label_set_text(study_drum_label_, FONT_AWESOME_MUSIC);
    lv_obj_align(study_drum_label_, LV_ALIGN_CENTER, 0, STUDY_ICON_OFFSET_Y);
    lv_obj_add_flag(study_drum_label_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(study_drum_label_, OnStudyMenuIconClicked, LV_EVENT_CLICKED, this);

    study_focus_arc_ = lv_arc_create(study_panel_);
    lv_obj_set_size(study_focus_arc_, STUDY_FOCUS_ARC_SIZE, STUDY_FOCUS_ARC_SIZE);
    lv_obj_align(study_focus_arc_, LV_ALIGN_CENTER, 0, 0);
    lv_arc_set_range(study_focus_arc_, 0, 360);
    lv_arc_set_bg_angles(study_focus_arc_, 0, 360);
    lv_arc_set_value(study_focus_arc_, 360);
    lv_obj_set_style_arc_width(study_focus_arc_, STUDY_FOCUS_ARC_TRACK_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_arc_color(study_focus_arc_, lv_color_hex(0x3A3A3A), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(study_focus_arc_, true, LV_PART_MAIN);
    lv_obj_set_style_arc_width(study_focus_arc_, STUDY_FOCUS_ARC_INDICATOR_WIDTH, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(study_focus_arc_, lv_color_hex(0x4FC3F7), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(study_focus_arc_, true, LV_PART_INDICATOR);
    lv_obj_set_style_opa(study_focus_arc_, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_clear_flag(study_focus_arc_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(study_focus_arc_, LV_OBJ_FLAG_HIDDEN);

    study_time_label_ = lv_label_create(study_panel_);
    lv_obj_set_style_text_font(study_time_label_, text_font, 0);
    lv_obj_set_style_text_color(study_time_label_, c.text_high, 0);
    lv_label_set_text(study_time_label_, "01:00");
    lv_obj_align(study_time_label_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(study_time_label_, LV_OBJ_FLAG_HIDDEN);

    ESP_LOGI(TAG, "Study area created (icons %dpx, focus arc r=%d track=%d ind=%d, gap=%d)",
             STUDY_ICON_GLYPH_PX, STUDY_FOCUS_ARC_RADIUS,
             STUDY_FOCUS_ARC_TRACK_WIDTH, STUDY_FOCUS_ARC_INDICATOR_WIDTH,
             STUDY_FOCUS_ARC_RING_GAP);
}

void AttitudeDisplay::UpdateStudyMenuSelection()
{
    const auto& c = AttitudeTheme::GetInstance().GetColors();
    const bool timer_sel = study_menu_selected_ == StudyMenuItem::Timer;

    if (study_clock_label_ != nullptr) {
        lv_obj_set_style_transform_scale(study_clock_label_,
            timer_sel ? STUDY_ICON_SCALE_SELECTED : STUDY_ICON_SCALE, 0);
        lv_obj_set_style_text_color(study_clock_label_,
            timer_sel ? c.text_high : c.text_main, 0);
    }
    if (study_drum_label_ != nullptr) {
        lv_obj_set_style_transform_scale(study_drum_label_,
            timer_sel ? STUDY_ICON_SCALE : STUDY_ICON_SCALE_SELECTED, 0);
        lv_obj_set_style_text_color(study_drum_label_,
            timer_sel ? c.text_main : c.text_high, 0);
    }
}

void AttitudeDisplay::SelectStudyMenuItem(StudyMenuItem item)
{
    if (study_menu_selected_ == item) {
        return;
    }
    study_menu_selected_ = item;
    UpdateStudyMenuSelection();
    PlayFortuneMenuSelectSound();
    ESP_LOGI(TAG, "Study menu item -> %d", static_cast<int>(item));
}

void AttitudeDisplay::OnStudyMenuIconClicked(lv_event_t* e)
{
    auto* self = static_cast<AttitudeDisplay*>(lv_event_get_user_data(e));
    if (self == nullptr || self->study_sub_state_ != StudySubState::Menu) {
        return;
    }
    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (target == self->study_clock_label_) {
        self->SelectStudyMenuItem(StudyMenuItem::Timer);
    } else if (target == self->study_drum_label_) {
        self->SelectStudyMenuItem(StudyMenuItem::Drum);
        self->EnterDrumPad();
    }
}

void AttitudeDisplay::ApplyStudyAreaOrientation(bool study_active)
{
    if (study_active == study_orientation_applied_) {
        return;
    }
    if (study_active) {
        study_saved_taiji_rotation_ = CompassTaiji::GetRotation();
        const int rotated = (study_saved_taiji_rotation_ + STUDY_AREA_ROTATION_OFFSET_TENTH_DEG) % 3600;
        CompassTaiji::SetRotation(rotated);
        ESP_LOGI(TAG, "Study area rotated +90° CW (%d -> %d)", study_saved_taiji_rotation_, rotated);
    } else {
        CompassTaiji::SetRotation(study_saved_taiji_rotation_);
        ESP_LOGI(TAG, "Study area rotation restored (%d)", study_saved_taiji_rotation_);
    }
    study_orientation_applied_ = study_active;
}

void AttitudeDisplay::EnterStudyMenu()
{
    if (study_panel_ == nullptr || study_sub_state_ == StudySubState::FocusRunning) {
        return;
    }
    if (study_sub_state_ == StudySubState::Menu
        || study_sub_state_ == StudySubState::CompleteBgm) {
        return;
    }

    study_had_auto_rotation_ = CompassTaiji::IsAutoRotating();
    if (study_had_auto_rotation_) {
        study_saved_rotation_period_ms_ = CompassTaiji::GetAutoRotationPeriod();
        CompassTaiji::StopAutoRotation();
    }

    SetTaijiCoreVisible(false);
    study_menu_selected_ = StudyMenuItem::Timer;
    study_sub_state_ = StudySubState::Menu;
    ShowStudyMenuPanel();
    ESP_LOGI(TAG, "Study menu entered (default: timer)");
}

void AttitudeDisplay::ExitStudyArea()
{
    StopStudyFocusTimer();
    StopStudyCompleteBgm();

    if (study_panel_ != nullptr) {
        lv_obj_add_flag(study_panel_, LV_OBJ_FLAG_HIDDEN);
    }

    if (study_sub_state_ != StudySubState::Hidden) {
        SetTaijiCoreVisible(true);
        if (study_had_auto_rotation_) {
            CompassTaiji::StartAutoRotation(study_saved_rotation_period_ms_);
        }
        study_sub_state_ = StudySubState::Hidden;
        ESP_LOGI(TAG, "Study area exited");
    }
}

void AttitudeDisplay::ShowStudyMenuPanel()
{
    if (study_panel_ == nullptr) {
        return;
    }
    lv_obj_remove_flag(study_panel_, LV_OBJ_FLAG_HIDDEN);
    if (study_clock_label_ != nullptr) {
        lv_obj_remove_flag(study_clock_label_, LV_OBJ_FLAG_HIDDEN);
    }
    if (study_drum_label_ != nullptr) {
        lv_obj_remove_flag(study_drum_label_, LV_OBJ_FLAG_HIDDEN);
    }
    if (study_focus_arc_ != nullptr) {
        lv_obj_add_flag(study_focus_arc_, LV_OBJ_FLAG_HIDDEN);
    }
    if (study_time_label_ != nullptr) {
        lv_obj_add_flag(study_time_label_, LV_OBJ_FLAG_HIDDEN);
    }
    study_sub_state_ = StudySubState::Menu;
    UpdateStudyMenuSelection();
}

void AttitudeDisplay::UpdateStudyFocusDisplay(int remaining_ms)
{
    if (remaining_ms < 0) {
        remaining_ms = 0;
    }
    const int total_sec = (remaining_ms + 999) / 1000;
    const int min = total_sec / 60;
    const int sec = total_sec % 60;
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d", min, sec);
    if (study_time_label_ != nullptr) {
        lv_label_set_text(study_time_label_, buf);
    }
    if (study_focus_arc_ != nullptr) {
        const int arc_val = (remaining_ms * 360) / STUDY_FOCUS_DURATION_MS;
        lv_arc_set_value(study_focus_arc_, arc_val);
    }
}

void AttitudeDisplay::StopStudyFocusTimer()
{
    if (study_focus_timer_ != nullptr) {
        lv_timer_delete(study_focus_timer_);
        study_focus_timer_ = nullptr;
    }
}

void AttitudeDisplay::StopStudyCompleteBgm()
{
    if (study_complete_bgm_timer_ != nullptr) {
        lv_timer_delete(study_complete_bgm_timer_);
        study_complete_bgm_timer_ = nullptr;
    }
    if (study_sub_state_ == StudySubState::CompleteBgm) {
        study_sub_state_ = StudySubState::Menu;
    }
}

void AttitudeDisplay::PlayStudyFocusCompleteBgm()
{
    StopStudyCompleteBgm();
    study_sub_state_ = StudySubState::CompleteBgm;
    Application::GetInstance().PlayUiSound(Lang::Sounds::OGG_STUDY_FOCUS_BGM);
    study_complete_bgm_timer_ = lv_timer_create(OnStudyCompleteBgmTimer,
                                                STUDY_FOCUS_BGM_DURATION_MS, this);
    lv_timer_set_repeat_count(study_complete_bgm_timer_, 1);
}

void AttitudeDisplay::OnStudyCompleteBgmTimer(lv_timer_t* timer)
{
    auto* self = static_cast<AttitudeDisplay*>(lv_timer_get_user_data(timer));
    if (self == nullptr) {
        return;
    }
    self->study_complete_bgm_timer_ = nullptr;
    self->ShowStudyMenuPanel();
    ESP_LOGI(TAG, "Study focus BGM finished -> menu");
}

void AttitudeDisplay::CancelStudyFocusToMenu()
{
    StopStudyFocusTimer();
    if (study_focus_arc_ != nullptr) {
        lv_arc_set_value(study_focus_arc_, 360);
    }
    ShowStudyMenuPanel();
}

void AttitudeDisplay::OnStudyFocusComplete()
{
    StopStudyFocusTimer();
    UpdateStudyFocusDisplay(0);
    if (study_focus_arc_ != nullptr) {
        lv_obj_remove_flag(study_focus_arc_, LV_OBJ_FLAG_HIDDEN);
    }
    if (study_time_label_ != nullptr) {
        lv_obj_remove_flag(study_time_label_, LV_OBJ_FLAG_HIDDEN);
    }
    PlayFortuneMenuSelectSound();
    PlayStudyFocusCompleteBgm();
    ESP_LOGI(TAG, "Study focus timer complete -> popup + BGM");
}

void AttitudeDisplay::OnStudyFocusTimer(lv_timer_t* timer)
{
    auto* self = static_cast<AttitudeDisplay*>(lv_timer_get_user_data(timer));
    if (self == nullptr || self->study_sub_state_ != StudySubState::FocusRunning) {
        return;
    }

    const uint32_t elapsed = lv_tick_elaps(self->study_focus_start_tick_);
    const int remaining = static_cast<int>(STUDY_FOCUS_DURATION_MS) - static_cast<int>(elapsed);
    if (remaining <= 0) {
        self->OnStudyFocusComplete();
        return;
    }
    self->UpdateStudyFocusDisplay(remaining);
}

void AttitudeDisplay::StartStudyFocusTimer()
{
    if (study_panel_ == nullptr || study_sub_state_ != StudySubState::Menu) {
        return;
    }
    if (study_menu_selected_ != StudyMenuItem::Timer) {
        ESP_LOGI(TAG, "Study focus: timer not selected, ignored");
        return;
    }

    StopStudyFocusTimer();
    study_sub_state_ = StudySubState::FocusRunning;

    if (study_clock_label_ != nullptr) {
        lv_obj_add_flag(study_clock_label_, LV_OBJ_FLAG_HIDDEN);
    }
    if (study_drum_label_ != nullptr) {
        lv_obj_add_flag(study_drum_label_, LV_OBJ_FLAG_HIDDEN);
    }
    if (study_focus_arc_ != nullptr) {
        lv_obj_remove_flag(study_focus_arc_, LV_OBJ_FLAG_HIDDEN);
        lv_arc_set_value(study_focus_arc_, 360);
    }
    if (study_time_label_ != nullptr) {
        lv_obj_remove_flag(study_time_label_, LV_OBJ_FLAG_HIDDEN);
    }

    study_focus_start_tick_ = lv_tick_get();
    UpdateStudyFocusDisplay(STUDY_FOCUS_DURATION_MS);
    study_focus_timer_ = lv_timer_create(OnStudyFocusTimer, STUDY_FOCUS_TICK_MS, this);
    ESP_LOGI(TAG, "Study focus timer started (%ds)", STUDY_FOCUS_DURATION_MS / 1000);
}

#endif  // STUDY_SUB_FEATURES_ENABLED

void AttitudeDisplay::SyncStudyAreaWithMenu()
{
    if (!fortune_menu_selection_active_) {
#if STUDY_SUB_FEATURES_ENABLED
        ExitStudyArea();
#endif
        return;
    }
#if STUDY_SUB_FEATURES_ENABLED
    if (fortune_menu_selected_index_ == static_cast<int>(FortuneMenuType::Study)) {
        if (study_sub_state_ == StudySubState::Hidden) {
            EnterStudyMenu();
        }
    } else {
        ExitStudyArea();
    }
#endif
}

// ShowFortuneMenuFeatureCardUnlocked 已彻底删除：功能区提示卡触发事件已全部移除
// ShowFortuneMenuFeatureCard 已彻底删除：公共 API 已被下游禁用

bool AttitudeDisplay::HandlePowerKey()
{
    DisplayLockGuard lock(this);

    // 1. 选中状态：取消选中
    if (fortune_menu_selection_active_) {
        DeselectFortuneMenuItemUnlocked();
        ESP_LOGI(TAG, "PWR: selection cancelled");
        return true;
    }

    // 2. 功能区显示状态：隐藏功能区
    if (function_area_card_ != nullptr
        && !lv_obj_has_flag(function_area_card_, LV_OBJ_FLAG_HIDDEN)) {
        HideDebugInfoUnlocked();
        ESP_LOGI(TAG, "PWR: debug info card dismissed");
        return true;
    }

#if STUDY_SUB_FEATURES_ENABLED
    // 5. 学业区：退出
    if (study_sub_state_ != StudySubState::Hidden) {
        ExitStudyArea();
        ESP_LOGI(TAG, "PWR: study area exited -> full taiji");
        return true;
    }
#endif
#if MOOD_GUA_SUB_FEATURES_ENABLED
    // 6. 迷宫游戏：退出
    if (mood_gua_sub_state_ != MoodGuaSubState::Hidden) {
        ExitMoodGuaArea();
        ESP_LOGI(TAG, "PWR: maze game exited -> full taiji");
        return true;
    }
#endif
    return false;
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

    // 使用 LVGL 样式系统：黑色背景 + 白色描边 + 金色图标
    wifi_fisheye_ = lv_obj_create(parent);
    lv_obj_set_size(wifi_fisheye_, FISHEYE_ICON_SIZE, FISHEYE_ICON_SIZE);
    lv_obj_set_pos(wifi_fisheye_, FISHEYE_WIFI_LOCAL_X, FISHEYE_WIFI_LOCAL_Y);
    lv_obj_set_style_radius(wifi_fisheye_, FISHEYE_ICON_SIZE / 2, 0);
    lv_obj_set_style_clip_corner(wifi_fisheye_, true, 0);
    lv_obj_set_style_bg_opa(wifi_fisheye_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(wifi_fisheye_, kFisheyeWifiBg, 0);
    lv_obj_set_style_border_width(wifi_fisheye_, FISHEYE_BORDER_WIDTH, 0);
    lv_obj_set_style_border_color(wifi_fisheye_, kFisheyeWifiBorder, 0);
    lv_obj_set_style_pad_all(wifi_fisheye_, 0, 0);
    lv_obj_clear_flag(wifi_fisheye_, LV_OBJ_FLAG_CLICKABLE);

    wifi_fisheye_icon_ = lv_label_create(wifi_fisheye_);
    lv_obj_set_style_text_font(wifi_fisheye_icon_, GetFisheyeIconFont(), 0);
    lv_obj_set_style_text_color(wifi_fisheye_icon_, kFisheyeGold, 0);
    lv_label_set_text(wifi_fisheye_icon_, FONT_AWESOME_WIFI);
    lv_obj_center(wifi_fisheye_icon_);
    lv_obj_move_foreground(wifi_fisheye_icon_);

    ESP_LOGI(TAG, "WiFi fisheye on taiji at local (%d,%d) size=%d",
             FISHEYE_WIFI_LOCAL_X, FISHEYE_WIFI_LOCAL_Y, FISHEYE_ICON_SIZE);
}

void AttitudeDisplay::CreateBleFisheye()
{
    lv_obj_t* parent = CompassTaiji::GetContainer();
    if (parent == nullptr) {
        ESP_LOGE(TAG, "CreateBleFisheye: taiji container is null");
        return;
    }

    // 使用 LVGL 样式系统：白色背景 + 黑色描边 + 蓝色图标
    ble_fisheye_ = lv_obj_create(parent);
    lv_obj_set_size(ble_fisheye_, FISHEYE_ICON_SIZE, FISHEYE_ICON_SIZE);
    lv_obj_set_pos(ble_fisheye_, FISHEYE_BLE_LOCAL_X, FISHEYE_BLE_LOCAL_Y);
    lv_obj_set_style_radius(ble_fisheye_, FISHEYE_ICON_SIZE / 2, 0);
    lv_obj_set_style_clip_corner(ble_fisheye_, true, 0);
    lv_obj_set_style_bg_opa(ble_fisheye_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(ble_fisheye_, kFisheyeBleBg, 0);
    lv_obj_set_style_border_width(ble_fisheye_, FISHEYE_BORDER_WIDTH, 0);
    lv_obj_set_style_border_color(ble_fisheye_, kFisheyeBleBorder, 0);
    lv_obj_set_style_pad_all(ble_fisheye_, 0, 0);
    lv_obj_clear_flag(ble_fisheye_, LV_OBJ_FLAG_CLICKABLE);

    ble_fisheye_icon_ = lv_label_create(ble_fisheye_);
    lv_obj_set_style_text_font(ble_fisheye_icon_, GetFisheyeIconFont(), 0);
    lv_obj_set_style_text_color(ble_fisheye_icon_, kFisheyeGrayIcon, 0);
    lv_label_set_text(ble_fisheye_icon_, FONT_AWESOME_BLUETOOTH);
    lv_obj_center(ble_fisheye_icon_);
    lv_obj_move_foreground(ble_fisheye_icon_);

    ESP_LOGI(TAG, "BLE fisheye on taiji at local (%d,%d) size=%d",
             FISHEYE_BLE_LOCAL_X, FISHEYE_BLE_LOCAL_Y, FISHEYE_ICON_SIZE);
}

void AttitudeDisplay::ApplyWifiFisheyeStyle(WifiStatus status)
{
    if (wifi_fisheye_ == nullptr || wifi_fisheye_icon_ == nullptr) {
        return;
    }

    StopFisheyePulse(wifi_fisheye_);

    // 恢复默认边框色（白色）
    lv_obj_set_style_border_color(wifi_fisheye_, kFisheyeWifiBorder, 0);

    switch (status) {
    case WifiStatus::DISCONNECTED:
        lv_obj_set_style_text_color(wifi_fisheye_icon_, kFisheyeGrayIcon, 0);
        lv_label_set_text(wifi_fisheye_icon_, FONT_AWESOME_WIFI_SLASH);
        break;
    case WifiStatus::CONNECTING:
        lv_obj_set_style_text_color(wifi_fisheye_icon_, kFisheyeGold, 0);
        lv_label_set_text(wifi_fisheye_icon_, FONT_AWESOME_WIFI);
        StartFisheyePulse(wifi_fisheye_);
        break;
    case WifiStatus::CONNECTED:
        lv_obj_set_style_text_color(wifi_fisheye_icon_, kFisheyeGold, 0);
        lv_label_set_text(wifi_fisheye_icon_, FONT_AWESOME_WIFI);
        break;
    default:
        break;
    }
}

void AttitudeDisplay::UpdateWifiFisheyeBorderColor(WifiStatus status)
{
    if (wifi_fisheye_ == nullptr) {
        return;
    }
    // 预留：可根据状态动态改变边框颜色
    lv_obj_set_style_border_color(wifi_fisheye_, kFisheyeWifiBorder, 0);
}

void AttitudeDisplay::ApplyBleFisheyeStyle(BleStatus status)
{
    if (ble_fisheye_ == nullptr || ble_fisheye_icon_ == nullptr) {
        return;
    }

    StopFisheyePulse(ble_fisheye_);

    // 恢复默认边框色（黑色）
    lv_obj_set_style_border_color(ble_fisheye_, kFisheyeBleBorder, 0);

    switch (status) {
    case BleStatus::DISABLED:
        lv_obj_set_style_text_color(ble_fisheye_icon_, kFisheyeGrayIcon, 0);
        lv_label_set_text(ble_fisheye_icon_, FONT_AWESOME_BLUETOOTH);
        break;
    case BleStatus::ADVERTISING:
        lv_obj_set_style_text_color(ble_fisheye_icon_, kFisheyeGrayIcon, 0);
        lv_label_set_text(ble_fisheye_icon_, FONT_AWESOME_BLUETOOTH);
        break;
    case BleStatus::CONNECTED:
        lv_obj_set_style_text_color(ble_fisheye_icon_, kFisheyeBleBlue, 0);
        lv_label_set_text(ble_fisheye_icon_, FONT_AWESOME_BLUETOOTH);
        break;
    default:
        break;
    }
}

void AttitudeDisplay::UpdateBleFisheyeBorderColor(BleStatus status)
{
    if (ble_fisheye_ == nullptr) {
        return;
    }
    lv_obj_set_style_border_color(ble_fisheye_, kFisheyeBleBorder, 0);
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

} // namespace

void AttitudeDisplay::EnterIdleState()
{
    ExitStudyArea();
    ApplyWifiFisheyeStyle(wifi_status_);
    ApplyBleFisheyeStyle(ble_status_);

    fortune_menu_selected_index_ = 0;
    fortune_menu_selection_active_ = false;
    SetFortuneMenuVisible(true);
    UpdateFortuneMenuSelection();
    ESP_LOGI(TAG, "Fortune -> Idle");
}

void AttitudeDisplay::CreateDebugInfoCard()
{
    if (function_area_card_ != nullptr) {
        return;
    }
    auto lvgl_theme = static_cast<LvglTheme*>(GetTheme());
    const lv_font_t* text_font = (lvgl_theme != nullptr && lvgl_theme->text_font() != nullptr)
        ? lvgl_theme->text_font()->font() : &BUILTIN_TEXT_FONT;
    // 300px 卡片，边距 20px，标签宽度 260px，高度更紧凑，确保内容全部在圆内
    const int card_w = DEBUG_INFO_CARD_W;
    const int text_w = card_w - 40;
    const int text_x = 20;
    // 30px 字体约占 36px 行高，标签高度设为 40-44 以确保完整显示
    const int row_h = 40;
    const int core_h = 60; // core 允许两行

    function_area_card_ = lv_obj_create(attitude_container_);
    lv_obj_set_size(function_area_card_, card_w, card_w); // 正方形，300x300
    lv_obj_set_pos(function_area_card_, DEBUG_INFO_CARD_X, DEBUG_INFO_CARD_Y);
    lv_obj_set_style_radius(function_area_card_, DEBUG_INFO_CARD_RADIUS, 0);
    lv_obj_set_style_clip_corner(function_area_card_, true, 0);
    lv_obj_set_style_bg_color(function_area_card_, lv_color_hex(0x0A1414), 0);
    lv_obj_set_style_bg_opa(function_area_card_, LV_OPA_90, 0);
    lv_obj_set_style_border_color(function_area_card_, DEBUG_INFO_BORDER_COLOR, 0);
    lv_obj_set_style_border_width(function_area_card_, 2, 0);
    lv_obj_set_style_pad_all(function_area_card_, 0, 0);
    lv_obj_set_style_layout(function_area_card_, LV_LAYOUT_NONE, 0);
    lv_obj_clear_flag(function_area_card_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(function_area_card_, LV_OBJ_FLAG_HIDDEN);

    // 调试卡简化版：title（顶部）+ detail（底部），删除 gua/core/yi/ji 四行（功能区提示卡已下线）
    const int start_y = 40;
    int cur_y = start_y;

    debug_info_title_ = lv_label_create(function_area_card_);
    lv_obj_set_style_text_font(debug_info_title_, text_font, 0);
    lv_obj_set_style_text_color(debug_info_title_, DEBUG_INFO_TITLE_COLOR, 0);
    lv_obj_set_style_text_opa(debug_info_title_, LV_OPA_COVER, 0);
    lv_obj_set_width(debug_info_title_, text_w);
    lv_obj_set_height(debug_info_title_, row_h);
    lv_obj_set_x(debug_info_title_, text_x);
    lv_obj_set_y(debug_info_title_, cur_y);
    lv_label_set_long_mode(debug_info_title_, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(debug_info_title_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(debug_info_title_, "");
    cur_y += row_h;

    // detail（第二行）：展示事件具体内容
    debug_info_detail_ = lv_label_create(function_area_card_);
    lv_obj_set_style_text_font(debug_info_detail_, text_font, 0);
    lv_obj_set_style_text_color(debug_info_detail_, DEBUG_INFO_DETAIL_COLOR, 0);
    lv_obj_set_width(debug_info_detail_, text_w);
    lv_obj_set_height(debug_info_detail_, core_h);
    lv_obj_set_x(debug_info_detail_, text_x);
    lv_obj_set_y(debug_info_detail_, cur_y);
    lv_label_set_long_mode(debug_info_detail_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(debug_info_detail_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(debug_info_detail_, "");

    ApplyDebugInfoCardLayout();

    debug_info_hide_timer_ = lv_timer_create(OnDebugInfoHideTimer, DEBUG_INFO_SHOW_MS, this);
    lv_timer_set_repeat_count(debug_info_hide_timer_, 1);

    ESP_LOGD(TAG, "Debug info card created");
}

// EnsureFortunePromptTitle 已彻底删除：screen 顶层短提示路径已废弃
// HideFortunePromptTitle 已彻底删除：同上
// HideDebugInfoCardLabels 已彻底删除：screen_title_overlay 路径已废弃

void AttitudeDisplay::ApplyDebugInfoCardLayout()
{
    if (function_area_card_ == nullptr) {
        return;
    }
    auto lvgl_theme = static_cast<LvglTheme*>(GetTheme());
    const lv_font_t* text_font = (lvgl_theme != nullptr && lvgl_theme->text_font() != nullptr)
        ? lvgl_theme->text_font()->font() : &BUILTIN_TEXT_FONT;
    const int card_w = DEBUG_INFO_CARD_W;
    const int text_w = card_w - 40;
    const int text_x = 20;
    const int row_h = 40;
    const int detail_h = 60;

    const int start_y = 40;
    const int y_title = start_y;
    const int y_detail = start_y + row_h;

    lv_obj_set_style_clip_corner(function_area_card_, true, 0);
    if (debug_info_title_ != nullptr) {
        lv_obj_set_style_text_font(debug_info_title_, text_font, 0);
        lv_obj_set_style_text_color(debug_info_title_, DEBUG_INFO_TITLE_COLOR, 0);
        lv_obj_set_style_text_opa(debug_info_title_, LV_OPA_COVER, 0);
        lv_obj_set_width(debug_info_title_, text_w);
        lv_obj_set_height(debug_info_title_, row_h);
        lv_obj_set_x(debug_info_title_, text_x);
        lv_obj_set_y(debug_info_title_, y_title);
        lv_label_set_long_mode(debug_info_title_, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_align(debug_info_title_, LV_TEXT_ALIGN_CENTER, 0);
    }
    if (debug_info_detail_ != nullptr) {
        lv_obj_set_style_text_font(debug_info_detail_, text_font, 0);
        lv_obj_set_style_text_color(debug_info_detail_, DEBUG_INFO_DETAIL_COLOR, 0);
        lv_obj_set_width(debug_info_detail_, text_w);
        lv_obj_set_height(debug_info_detail_, detail_h);
        lv_obj_set_x(debug_info_detail_, text_x);
        lv_obj_set_y(debug_info_detail_, y_detail);
        lv_label_set_long_mode(debug_info_detail_, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(debug_info_detail_, LV_TEXT_ALIGN_CENTER, 0);
    }
}

void AttitudeDisplay::DestroyDebugInfoCard()
{
    if (debug_info_hide_timer_ != nullptr) {
        lv_timer_delete(debug_info_hide_timer_);
        debug_info_hide_timer_ = nullptr;
    }
    if (function_area_card_ != nullptr) {
        lv_obj_del(function_area_card_);
        function_area_card_ = nullptr;
    }
    debug_info_title_ = nullptr;
    debug_info_detail_ = nullptr;
}

void AttitudeDisplay::PresentDebugInfoCardUnlocked(const std::string& title,
                                                    const std::string& detail,
                                                    uint32_t hold_ms,
                                                    const DebugInfoPresentOpts& opts)
{
    CreateDebugInfoCard();
    if (function_area_card_ == nullptr || debug_info_title_ == nullptr || debug_info_detail_ == nullptr) {
        ESP_LOGW(TAG, "PresentDebugInfoCard: widgets missing");
        return;
    }

    auto lvgl_theme = static_cast<LvglTheme*>(GetTheme());
    const lv_font_t* text_font = (lvgl_theme != nullptr && lvgl_theme->text_font() != nullptr)
        ? lvgl_theme->text_font()->font() : &BUILTIN_TEXT_FONT;
    const bool builtin_font = (text_font == &BUILTIN_TEXT_FONT);

    ApplyDebugInfoCardLayout();
    lv_label_set_text(debug_info_title_, title.c_str());
    lv_label_set_text(debug_info_detail_, detail.c_str());
    lv_obj_remove_flag(debug_info_title_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(debug_info_detail_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(debug_info_title_);
    lv_obj_move_foreground(debug_info_detail_);
    lv_obj_move_foreground(function_area_card_);

    lv_obj_remove_flag(function_area_card_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(function_area_card_);
    lv_obj_update_layout(function_area_card_);
    if (display_ != nullptr) {
        lv_refr_now(display_);
    }

    ESP_LOGI(TAG, "DebugInfoCard shown: title=%s hidden=%d card_hidden=%d",
             title.c_str(),
             lv_obj_has_flag(debug_info_title_, LV_OBJ_FLAG_HIDDEN) ? 1 : 0,
             lv_obj_has_flag(function_area_card_, LV_OBJ_FLAG_HIDDEN) ? 1 : 0);

    if (debug_info_hide_timer_ != nullptr) {
        if (opts.persistent) {
            lv_timer_pause(debug_info_hide_timer_);
        } else {
            const uint32_t actual_hold = (hold_ms == 0) ? DEBUG_INFO_SHOW_MS
                                    : (hold_ms > DEBUG_INFO_HOLD_MAX_MS) ? DEBUG_INFO_HOLD_MAX_MS
                                    : hold_ms;
            lv_timer_resume(debug_info_hide_timer_);
            lv_timer_set_period(debug_info_hide_timer_, actual_hold);
            lv_timer_reset(debug_info_hide_timer_);
        }
    }

    if (opts.persistent) {
        ESP_LOGI(TAG, "Fortune feature card: %s card_title=%dx%d@%d,%d detail=%dx%d builtin_font=%d font=%p",
                 title.c_str(),
                 lv_obj_get_width(debug_info_title_), lv_obj_get_height(debug_info_title_),
                 lv_obj_get_x(debug_info_title_), lv_obj_get_y(debug_info_title_),
                 lv_obj_get_width(debug_info_detail_), lv_obj_get_height(debug_info_detail_),
                 builtin_font ? 1 : 0, text_font);
    } else {
        ESP_LOGI(TAG, "DebugInfo: %s | %s (hold=%ums builtin_font=%d)",
                 title.c_str(), detail.c_str(),
                 (unsigned)((hold_ms == 0) ? DEBUG_INFO_SHOW_MS : hold_ms),
                 builtin_font ? 1 : 0);
    }
}

void AttitudeDisplay::OnDebugInfoHideTimer(lv_timer_t* timer)
{
    auto* self = static_cast<AttitudeDisplay*>(lv_timer_get_user_data(timer));
    if (self == nullptr) {
        return;
    }
    DisplayLockGuard lock(self);
    self->HideDebugInfoUnlocked();
}

void AttitudeDisplay::ShowDebugInfo(const std::string& title, const std::string& detail, uint32_t hold_ms)
{
    DisplayLockGuard lock(this);

    if (fortune_menu_selection_active_) {
        ESP_LOGD(TAG, "ShowDebugInfo skipped (fortune menu active): %s", title.c_str());
        return;
    }
#if STUDY_SUB_FEATURES_ENABLED
    // 与学业区互斥
    if (study_sub_state_ != StudySubState::Hidden) {
        ESP_LOGD(TAG, "ShowDebugInfo skipped (study busy): %s", title.c_str());
        return;
    }
#endif

    const uint32_t now = lv_tick_get();
    // 同标题去重，避免快速连续触发同一事件
    if (!debug_info_last_title_.empty() && debug_info_last_title_ == title &&
        (now - debug_info_last_show_ms_) < DEBUG_INFO_DEDUP_MS) {
        ESP_LOGD(TAG, "ShowDebugInfo dedup: %s", title.c_str());
        return;
    }

    CreateDebugInfoCard();
    if (function_area_card_ == nullptr || debug_info_title_ == nullptr || debug_info_detail_ == nullptr) {
        return;
    }

    DebugInfoPresentOpts opts;
    PresentDebugInfoCardUnlocked(title, detail, hold_ms, opts);

    debug_info_last_title_ = title;
    debug_info_last_show_ms_ = now;
}

void AttitudeDisplay::HideDebugInfoUnlocked()
{
    if (function_area_card_ != nullptr) {
        lv_obj_add_flag(function_area_card_, LV_OBJ_FLAG_HIDDEN);
    }
    // 功能区提示卡触发事件已全部移除：调试卡消失后不再恢复显示
}

void AttitudeDisplay::HideDebugInfo()
{
    DisplayLockGuard lock(this);
    HideDebugInfoUnlocked();
}

bool AttitudeDisplay::HandleBootKey()
{
    DisplayLockGuard lock(this);

#if STUDY_SUB_FEATURES_ENABLED
    if (study_sub_state_ == StudySubState::FocusRunning
        || study_sub_state_ == StudySubState::CompleteBgm) {
        return true;
    }
#endif
#if MOOD_GUA_SUB_FEATURES_ENABLED
    if (mood_gua_sub_state_ == MoodGuaSubState::MazePlaying) {
        // Boot 短按在迷宫游戏中：重新生成一个新迷宫
        GenerateMaze();
        DrawMazeOnCanvas();
        RedrawMazePlayer();
        ESP_LOGI(TAG, "Boot: maze regenerated");
        return true;
    }
#endif

    // Idle状态：进入选中态或循环选择
    if (!fortune_menu_selection_active_) {
        SelectFortuneMenuItemUnlocked(0);
        ESP_LOGI(TAG, "Boot: selection on, default today (index 0)");
        return true;
    }

#if STUDY_SUB_FEATURES_ENABLED
    // 学业区菜单：启动专注计时
    if (fortune_menu_selected_index_ == static_cast<int>(FortuneMenuType::Study)
        && study_sub_state_ == StudySubState::Menu) {
        StartStudyFocusTimer();
        return true;
    }
#endif

    // 循环选择下一个运势项
    CycleFortuneMenuSelectionUnlocked();
    return true;
}

bool AttitudeDisplay::HandleFortuneBootLongPress()
{
    DisplayLockGuard lock(this);

    if (!fortune_menu_selection_active_) {
        return false;
    }
#if STUDY_SUB_FEATURES_ENABLED
    if (fortune_menu_selected_index_ == static_cast<int>(FortuneMenuType::Study)
        && study_sub_state_ != StudySubState::Hidden) {
        ESP_LOGI(TAG, "Boot long press ignored in study area");
        return true;
    }
#endif
#if MOOD_GUA_SUB_FEATURES_ENABLED
    if (mood_gua_sub_state_ == MoodGuaSubState::MazePlaying) {
        ESP_LOGI(TAG, "Boot long press ignored in maze game");
        return true;
    }
#endif

    ESP_LOGI(TAG, "Boot long press: fortune menu %d (no result card, Plan A removed)",
             fortune_menu_selected_index_);
    return true;
}

// =================================================================
// 架子鼓模式实现（8 扇区 + 中心 Kick）
// =================================================================
#if STUDY_SUB_FEATURES_ENABLED

// 8 个扇区相对正上（0°）的起始角度（顺时针，单位：度）
// 扇区 0 = 正上（Kick 备用），1 = 右上 45°，2 = 右，...
// 但这里 0 改为正上 Snare，便于将 Kick 放中心
static constexpr int DRUM_SECTOR_ANGLE_DEG[8] = {
    -90,  // 0: 正上 (Snare)
    -45,  // 1: 右上 (Hi-Hat Closed)
       0, // 2: 右   (Tom Hi)
      45,  // 3: 右下 (Tom Mid)
      90,  // 4: 正下 (Kick 备用)
     135,  // 5: 左下 (Hi-Hat Open)
     180,  // 6: 左   (Crash)
    -135,  // 7: 左上 (Ride)
};

// 8 个扇区的中文标签（按 ID 顺序，对应 drum::Piece 枚举）
static constexpr const char* DRUM_SECTOR_LABELS[8] = {
    "军鼓", "闭镲", "高桶", "中桶",
    "底鼓", "开镲", "强音钹", "叮叮镲"
};

void AttitudeDisplay::EnterDrumPad()
{
    if (study_sub_state_ != StudySubState::Menu) {
        ESP_LOGD(TAG, "EnterDrumPad ignored: state=%d", static_cast<int>(study_sub_state_));
        return;
    }
    auto& app = Application::GetInstance();
    if (app.GetDeviceState() != kDeviceStateIdle) {
        ESP_LOGI(TAG, "EnterDrumPad blocked: device busy");
        // 与 StartStudyFocusTimer 行为一致：忙碌时不进入
        return;
    }

    // 隐藏菜单图标
    if (study_clock_label_ != nullptr) {
        lv_obj_add_flag(study_clock_label_, LV_OBJ_FLAG_HIDDEN);
    }
    if (study_drum_label_ != nullptr) {
        lv_obj_add_flag(study_drum_label_, LV_OBJ_FLAG_HIDDEN);
    }
    if (study_focus_arc_ != nullptr) {
        lv_obj_add_flag(study_focus_arc_, LV_OBJ_FLAG_HIDDEN);
    }
    if (study_time_label_ != nullptr) {
        lv_obj_add_flag(study_time_label_, LV_OBJ_FLAG_HIDDEN);
    }

    study_sub_state_ = StudySubState::DrumPad;
    drum::DrumSynth::GetInstance().SetActive(true);
    ShowDrumPadUI();
    ESP_LOGI(TAG, "DrumPad entered");
}

void AttitudeDisplay::ExitDrumPad()
{
    if (study_sub_state_ != StudySubState::DrumPad) {
        return;
    }
    drum::DrumSynth::GetInstance().SetActive(false);
    HideDrumPadUI();

    // 恢复菜单
    study_sub_state_ = StudySubState::Menu;
    if (study_clock_label_ != nullptr) {
        lv_obj_remove_flag(study_clock_label_, LV_OBJ_FLAG_HIDDEN);
    }
    if (study_drum_label_ != nullptr) {
        lv_obj_remove_flag(study_drum_label_, LV_OBJ_FLAG_HIDDEN);
    }
    if (study_focus_arc_ != nullptr) {
        lv_obj_remove_flag(study_focus_arc_, LV_OBJ_FLAG_HIDDEN);
    }
    UpdateStudyMenuSelection();
    ESP_LOGI(TAG, "DrumPad exited");
}

void AttitudeDisplay::ShowDrumPadUI()
{
    if (drum_pad_ != nullptr) {
        lv_obj_remove_flag(drum_pad_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    auto lvgl_theme = static_cast<LvglTheme*>(GetTheme());
    const lv_color_t accent = (lvgl_theme != nullptr) ? lvgl_theme->border_color()
                                                      : lv_color_hex(0x00C8C8);
    const lv_color_t dim = lv_color_hex(0x333333);

    // 架子鼓触摸板：覆盖 study_panel_，占满整个圆形画布
    drum_pad_ = lv_obj_create(study_panel_);
    lv_obj_set_size(drum_pad_, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(drum_pad_, 0, 0);
    lv_obj_set_style_bg_opa(drum_pad_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(drum_pad_, 0, 0);
    lv_obj_set_style_pad_all(drum_pad_, 0, 0);
    lv_obj_clear_flag(drum_pad_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(drum_pad_, LV_OBJ_FLAG_SCROLLABLE);

    // 8 扇区：以中心为锚点
    // 圆形屏直径 360px，扇区径向覆盖 [80, 170]，中间留出中心按钮
    static constexpr int SECTOR_R_INNER = 80;
    static constexpr int SECTOR_R_OUTER = 168;
    static constexpr int SECTOR_ANGLE_WIDTH = 36;  // 每个扇区 36°，8 扇区覆盖 288°，留 72° 死区
    static constexpr int SECTOR_OFFSET_DEG = 0;    // 旋转偏移：把"正上"对准第一个扇区中心

    for (int i = 0; i < 8; i++) {
        int center_angle = DRUM_SECTOR_ANGLE_DEG[i] + SECTOR_OFFSET_DEG;
        // 扇区中心 = center_angle，扇区覆盖 [center_angle - 18, center_angle + 18]
        int start_angle = (center_angle - SECTOR_ANGLE_WIDTH / 2 + 360) % 360;
        int end_angle = (center_angle + SECTOR_ANGLE_WIDTH / 2 + 360) % 360;

        // 使用 lv_arc 表示扇区
        lv_obj_t* arc = lv_arc_create(drum_pad_);
        lv_obj_set_size(arc, SECTOR_R_OUTER * 2, SECTOR_R_OUTER * 2);
        lv_obj_align(arc, LV_ALIGN_CENTER, 0, 0);
        // lv_arc 0° 在正下方（6 点钟），需要偏移 -90° 让 0° 在正上（12 点钟）
        // 扇区角度转换：用户角度 0=正上 → lv_arc 角度 270
        int lv_start = (start_angle + 90) % 360;
        int lv_end = (end_angle + 90) % 360;
        lv_arc_set_bg_angles(arc, lv_start, lv_end);
        lv_arc_set_angles(arc, lv_start, lv_end);
        lv_arc_set_range(arc, 0, 360);
        lv_arc_set_value(arc, 0);
        // 背景轨道不可见
        lv_obj_set_style_arc_color(arc, dim, LV_PART_MAIN);
        lv_obj_set_style_arc_width(arc, SECTOR_R_OUTER - SECTOR_R_INNER, LV_PART_MAIN);
        lv_obj_set_style_arc_opa(arc, LV_OPA_30, LV_PART_MAIN);
        // 前景扇区
        lv_obj_set_style_arc_color(arc, accent, LV_PART_INDICATOR);
        lv_obj_set_style_arc_width(arc, SECTOR_R_OUTER - SECTOR_R_INNER, LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(arc, LV_OPA_20, LV_PART_INDICATOR);
        // 点击扇区
        lv_obj_add_flag(arc, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(arc, OnDrumPadTouchedStatic, LV_EVENT_CLICKED, this);
        // 存储扇区索引到 user_data 不行（arc 自带），用数组下标
        // 改用自定义机制：在 user_data 不行时，用 arc 对象的坐标存 piece idx
        // 简化：把 arc 直接存到数组
        drum_sectors_[i] = arc;
        // 扇区标签
        lv_obj_t* label = lv_label_create(drum_pad_);
        lv_obj_set_style_text_font(label, &BUILTIN_TEXT_FONT, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(label, DRUM_SECTOR_LABELS[i]);
        // 标签位置 = 扇区中心方向 × 半径 (SECTOR_R_INNER + SECTOR_R_OUTER) / 2
        float mid_r = (SECTOR_R_INNER + SECTOR_R_OUTER) / 2.0f;
        float rad = (center_angle) * 3.14159265f / 180.0f;
        // 角度 -> 屏幕坐标（Y 轴向下，正上对应 -Y）
        int lx = (int)(mid_r * sinf(rad));
        int ly = -(int)(mid_r * cosf(rad));
        lv_obj_align(label, LV_ALIGN_CENTER, lx, ly);
        drum_sector_labels_[i] = label;
    }

    // 中心 Kick 按钮
    drum_center_ = lv_obj_create(drum_pad_);
    lv_obj_set_size(drum_center_, SECTOR_R_INNER * 2, SECTOR_R_INNER * 2);
    lv_obj_align(drum_center_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(drum_center_, SECTOR_R_INNER, 0);
    lv_obj_set_style_bg_color(drum_center_, accent, 0);
    lv_obj_set_style_bg_opa(drum_center_, LV_OPA_30, 0);
    lv_obj_set_style_border_color(drum_center_, accent, 0);
    lv_obj_set_style_border_width(drum_center_, 3, 0);
    lv_obj_set_style_border_opa(drum_center_, LV_OPA_60, 0);
    lv_obj_set_style_pad_all(drum_center_, 0, 0);
    lv_obj_add_flag(drum_center_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(drum_center_, OnDrumPadTouchedStatic, LV_EVENT_CLICKED, this);

    // 中心标签
    lv_obj_t* center_label = lv_label_create(drum_center_);
    lv_obj_set_style_text_font(center_label, &BUILTIN_TEXT_FONT, 0);
    lv_obj_set_style_text_color(center_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(center_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(center_label, "底鼓");
    lv_obj_align(center_label, LV_ALIGN_CENTER, 0, 0);

    // 退出提示（顶部小字）+ 长按退出
    lv_obj_t* hint = lv_label_create(drum_pad_);
    lv_obj_set_style_text_font(hint, &BUILTIN_TEXT_FONT, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(hint, "架子鼓  长按顶边退出");
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 4);

    // 顶部"退出区"：长按 2s 退出架子鼓模式
    lv_obj_t* exit_zone = lv_obj_create(drum_pad_);
    lv_obj_set_size(exit_zone, lv_pct(100), 24);
    lv_obj_align(exit_zone, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(exit_zone, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(exit_zone, 0, 0);
    lv_obj_set_style_pad_all(exit_zone, 0, 0);
    lv_obj_add_flag(exit_zone, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(exit_zone, OnDrumPadTouchedStatic, LV_EVENT_LONG_PRESSED, this);

    ESP_LOGI(TAG, "DrumPad UI created");
}

void AttitudeDisplay::HideDrumPadUI()
{
    if (drum_pad_ != nullptr) {
        lv_obj_add_flag(drum_pad_, LV_OBJ_FLAG_HIDDEN);
    }
    // 释放闪烁定时器
    if (drum_flash_timer_id_ >= 0) {
        lv_timer_del(reinterpret_cast<lv_timer_t*>(static_cast<uintptr_t>(drum_flash_timer_id_)));
        drum_flash_timer_id_ = -1;
    }
}

void AttitudeDisplay::OnDrumPadTouchedStatic(lv_event_t* e)
{
    auto* self = static_cast<AttitudeDisplay*>(lv_event_get_user_data(e));
    if (self != nullptr) {
        self->OnDrumPadTouched(e);
    }
}

void AttitudeDisplay::OnDrumPadTouched(lv_event_t* e)
{
    if (study_sub_state_ != StudySubState::DrumPad) {
        return;
    }
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));

    // 长按顶部退出区 → 退出架子鼓模式
    if (code == LV_EVENT_LONG_PRESSED) {
        // 顶部 24px 高度的 exit_zone
        lv_coord_t y = 0;
        if (target != nullptr) {
            y = lv_obj_get_y(target);
        }
        if (y == 0) {
            ESP_LOGI(TAG, "DrumPad long press top -> exit");
            ExitDrumPad();
            return;
        }
    }

    if (code != LV_EVENT_CLICKED) {
        return;
    }
    int piece_idx = -1;

    // 判断是哪个扇区/中心
    if (target == drum_center_) {
        piece_idx = static_cast<int>(drum::Piece::KICK);
    } else {
        for (int i = 0; i < 8; i++) {
            if (drum_sectors_[i] == target) {
                piece_idx = i;
                break;
            }
        }
    }
    if (piece_idx < 0) {
        return;
    }

    ESP_LOGD(TAG, "DrumPad touch piece=%d", piece_idx);
    drum::DrumSynth::GetInstance().Trigger(
        static_cast<drum::Piece>(piece_idx));
    FlashDrumSector(piece_idx);
}

void AttitudeDisplay::FlashDrumSector(int piece_idx)
{
    // 简单的视觉反馈：被点击的扇区前景色不透明度 20% -> 80% -> 20%
    // 200ms 淡入 + 200ms 淡出
    auto lvgl_theme = static_cast<LvglTheme*>(GetTheme());
    const lv_color_t accent = (lvgl_theme != nullptr) ? lvgl_theme->border_color()
                                                      : lv_color_hex(0x00C8C8);
    (void)accent;  // 仅用于未来扩展

    if (piece_idx == static_cast<int>(drum::Piece::KICK)) {
        // 中心：背景不透明度跳到 80% 再回 30%
        if (drum_center_ != nullptr) {
            lv_obj_set_style_bg_opa(drum_center_, LV_OPA_80, 0);
        }
        // 200ms 后恢复
        if (drum_flash_timer_id_ < 0) {
            drum_flash_start_ms_ = lv_tick_get();
            lv_timer_t* t = lv_timer_create([](lv_timer_t* timer) {
                auto* self = static_cast<AttitudeDisplay*>(lv_timer_get_user_data(timer));
                if (self != nullptr) {
                    if (self->drum_center_ != nullptr) {
                        lv_obj_set_style_bg_opa(self->drum_center_, LV_OPA_30, 0);
                    }
                    self->drum_flash_timer_id_ = -1;
                }
                lv_timer_del(timer);
            }, 200, this);
            drum_flash_timer_id_ = static_cast<int>(reinterpret_cast<uintptr_t>(t));
        }
        return;
    }

    // 扇区：前景不透明度短暂提升
    if (piece_idx < 0 || piece_idx >= 8) {
        return;
    }
    lv_obj_t* arc = drum_sectors_[piece_idx];
    if (arc == nullptr) {
        return;
    }
    lv_obj_set_style_arc_opa(arc, LV_OPA_80, LV_PART_INDICATOR);
    // 200ms 后恢复
    lv_timer_t* t = lv_timer_create([](lv_timer_t* timer) {
        lv_obj_t* target_arc = static_cast<lv_obj_t*>(lv_timer_get_user_data(timer));
        if (target_arc != nullptr) {
            lv_obj_set_style_arc_opa(target_arc, LV_OPA_20, LV_PART_INDICATOR);
        }
        lv_timer_del(timer);
    }, 200, arc);
    (void)t;  // 定时器自动释放
}

#endif  // STUDY_SUB_FEATURES_ENABLED

// =================================================================
// 迷宫游戏实现（心情卦）
// =================================================================
#if MOOD_GUA_SUB_FEATURES_ENABLED

#if !STUDY_SUB_FEATURES_ENABLED
void AttitudeDisplay::SetTaijiCoreVisible(bool visible)
{
    if (auto* canvas = CompassTaiji::GetCanvas()) {
        if (visible) {
            lv_obj_remove_flag(canvas, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(canvas, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (wifi_fisheye_ != nullptr) {
        if (visible) {
            lv_obj_remove_flag(wifi_fisheye_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(wifi_fisheye_, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (ble_fisheye_ != nullptr) {
        if (visible) {
            lv_obj_remove_flag(ble_fisheye_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(ble_fisheye_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}
#endif  // !STUDY_SUB_FEATURES_ENABLED

void AttitudeDisplay::CreateMoodGuaArea()
{
    lv_obj_t* container = CompassTaiji::GetContainer();
    if (container == nullptr) {
        ESP_LOGE(TAG, "CreateMoodGuaArea: taiji container missing");
        return;
    }

    const auto& c = AttitudeTheme::GetInstance().GetColors();

    // 主面板 - 覆盖整个太极区域
    mood_gua_panel_ = lv_obj_create(container);
    lv_obj_set_size(mood_gua_panel_, TAIJI_CANVAS_SIZE, TAIJI_CANVAS_SIZE);
    lv_obj_set_pos(mood_gua_panel_, 0, 0);
    lv_obj_set_style_radius(mood_gua_panel_, TAIJI_RADIUS, 0);
    lv_obj_set_style_bg_opa(mood_gua_panel_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mood_gua_panel_, 0, 0);
    lv_obj_set_style_pad_all(mood_gua_panel_, 0, 0);
    lv_obj_clear_flag(mood_gua_panel_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(mood_gua_panel_, LV_OBJ_FLAG_HIDDEN);

    // 迷宫 canvas - 用于绘制墙壁
    maze_canvas_ = lv_canvas_create(mood_gua_panel_);
    lv_obj_set_size(maze_canvas_, MAZE_CANVAS_SIZE, MAZE_CANVAS_SIZE);
    const int maze_offset = (TAIJI_CANVAS_SIZE - MAZE_CANVAS_SIZE) / 2;
    lv_obj_set_pos(maze_canvas_, maze_offset, maze_offset);
    lv_obj_set_style_bg_opa(maze_canvas_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(maze_canvas_, 0, 0);

    // 为 canvas 分配绘制 buffer - 使用静态 buf 避免频繁分配
    static lv_color_t maze_buf[MAZE_CANVAS_SIZE * MAZE_CANVAS_SIZE];
    lv_canvas_set_buffer(maze_canvas_, maze_buf, MAZE_CANVAS_SIZE, MAZE_CANVAS_SIZE, LV_COLOR_FORMAT_NATIVE);

    // 玩家小圆点 - 使用独立的 lv_obj 便于移动
    maze_player_obj_ = lv_obj_create(mood_gua_panel_);
    lv_obj_set_size(maze_player_obj_, MAZE_PLAYER_SIZE, MAZE_PLAYER_SIZE);
    lv_obj_set_style_radius(maze_player_obj_, MAZE_PLAYER_SIZE / 2, 0);
    lv_obj_set_style_bg_color(maze_player_obj_, lv_color_hex(0xFFD700), 0); // 金色
    lv_obj_set_style_bg_opa(maze_player_obj_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(maze_player_obj_, 0, 0);
    lv_obj_clear_flag(maze_player_obj_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(maze_player_obj_, LV_OBJ_FLAG_HIDDEN);

    // 迷宫标题（顶部显示功能名称）
    maze_title_label_ = lv_label_create(mood_gua_panel_);
    lv_obj_set_style_text_font(maze_title_label_, &BUILTIN_TEXT_FONT, 0);
    lv_obj_set_style_text_color(maze_title_label_, c.text_main, 0);
    lv_label_set_text(maze_title_label_, "迷宫游戏");
    lv_obj_align(maze_title_label_, LV_ALIGN_TOP_MID, 0, 2);
    lv_obj_add_flag(maze_title_label_, LV_OBJ_FLAG_HIDDEN);

    // 胜利提示（右下角显示）
    maze_end_label_ = lv_label_create(mood_gua_panel_);
    lv_obj_set_style_text_font(maze_end_label_, &BUILTIN_TEXT_FONT, 0);
    lv_obj_set_style_text_color(maze_end_label_, lv_color_hex(0x00FF00), 0);
    lv_label_set_text(maze_end_label_, "✓");
    lv_obj_add_flag(maze_end_label_, LV_OBJ_FLAG_HIDDEN);

    // 为整个 panel 注册点击事件（检测方向移动）
    lv_obj_add_flag(mood_gua_panel_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(mood_gua_panel_, OnMazeTouchedStatic, LV_EVENT_CLICKED, this);

    // 初始化迷宫墙壁（全部为有墙状态）并生成一个初始迷宫
    GenerateMaze();

    ESP_LOGI(TAG, "MoodGua maze area created (grid=%d, cell=%d, player=%d)",
             MAZE_GRID_SIZE, MAZE_CELL_SIZE, MAZE_PLAYER_SIZE);
}

void AttitudeDisplay::SyncMoodGuaAreaWithMenu()
{
    if (!fortune_menu_selection_active_) {
        if (mood_gua_sub_state_ != MoodGuaSubState::Hidden) {
            ExitMoodGuaArea();
        }
        return;
    }

    if (fortune_menu_selected_index_ == static_cast<int>(FortuneMenuType::MoodGua)) {
#if STUDY_SUB_FEATURES_ENABLED
        // 如果学业区已进入，先退出
        if (study_sub_state_ != StudySubState::Hidden) {
            ExitStudyArea();
        }
#endif
        // 隐藏其他功能卡
        if (function_area_card_ != nullptr
            && !lv_obj_has_flag(function_area_card_, LV_OBJ_FLAG_HIDDEN)) {
            HideDebugInfoUnlocked();
        }
        if (mood_gua_sub_state_ == MoodGuaSubState::Hidden) {
            EnterMaze();
        }
    } else {
        if (mood_gua_sub_state_ != MoodGuaSubState::Hidden) {
            ExitMoodGuaArea();
        }
    }
}

void AttitudeDisplay::EnterMaze()
{
    if (mood_gua_panel_ == nullptr) {
        return;
    }

    // 保存并停止太极旋转
    mood_gua_had_auto_rotation_ = CompassTaiji::IsAutoRotating();
    if (mood_gua_had_auto_rotation_) {
        mood_gua_saved_rotation_period_ms_ = CompassTaiji::GetAutoRotationPeriod();
        CompassTaiji::StopAutoRotation();
    }

    // 隐藏太极核心（保持鱼眼可见与否，与学业区一致）
    SetTaijiCoreVisible(false);

    // 重置玩家位置到起点
    maze_player_row_ = 0;
    maze_player_col_ = 0;

    // 生成新的随机迷宫
    GenerateMaze();

    // 绘制迷宫
    DrawMazeOnCanvas();

    // 绘制玩家
    RedrawMazePlayer();

    // 显示 UI 元素
    lv_obj_remove_flag(mood_gua_panel_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(maze_canvas_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(maze_player_obj_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(maze_title_label_, LV_OBJ_FLAG_HIDDEN);

    // 隐藏胜利标记
    if (maze_end_label_ != nullptr) {
        lv_obj_add_flag(maze_end_label_, LV_OBJ_FLAG_HIDDEN);
    }

    mood_gua_sub_state_ = MoodGuaSubState::MazePlaying;
    ESP_LOGI(TAG, "Maze game entered (player=0,0, end=%d,%d)",
             maze_end_row_, maze_end_col_);
}

void AttitudeDisplay::ExitMoodGuaArea()
{
    if (mood_gua_panel_ != nullptr) {
        lv_obj_add_flag(mood_gua_panel_, LV_OBJ_FLAG_HIDDEN);
    }
    if (maze_canvas_ != nullptr) {
        lv_obj_add_flag(maze_canvas_, LV_OBJ_FLAG_HIDDEN);
    }
    if (maze_player_obj_ != nullptr) {
        lv_obj_add_flag(maze_player_obj_, LV_OBJ_FLAG_HIDDEN);
    }
    if (maze_title_label_ != nullptr) {
        lv_obj_add_flag(maze_title_label_, LV_OBJ_FLAG_HIDDEN);
    }
    if (maze_end_label_ != nullptr) {
        lv_obj_add_flag(maze_end_label_, LV_OBJ_FLAG_HIDDEN);
    }

    if (mood_gua_sub_state_ != MoodGuaSubState::Hidden) {
        SetTaijiCoreVisible(true);
        if (mood_gua_had_auto_rotation_) {
            CompassTaiji::StartAutoRotation(mood_gua_saved_rotation_period_ms_);
        }
        mood_gua_sub_state_ = MoodGuaSubState::Hidden;
        ESP_LOGI(TAG, "Maze game exited");
    }
}

// --- DFS 随机迷宫生成算法 ---
// 使用递归回溯法生成完美迷宫
// 墙壁定义：maze_walls_[row][col] = {top, right, bottom, left}
// true 表示有墙，false 表示无墙

namespace {

// 墙壁方向索引
const int kWallTop = 0;
const int kWallRight = 1;
const int kWallBottom = 2;
const int kWallLeft = 3;

// 递归 DFS 生成
void MazeDfsGenerate(bool walls[MAZE_GRID_SIZE][MAZE_GRID_SIZE][4],
                     int row, int col,
                     bool visited[MAZE_GRID_SIZE][MAZE_GRID_SIZE])
{
    visited[row][col] = true;

    // 随机打乱四个方向的顺序
    int dirs[4] = {0, 1, 2, 3}; // top, right, bottom, left
    for (int i = 0; i < 4; i++) {
        int j = rand() % (4 - i) + i;
        int t = dirs[i];
        dirs[i] = dirs[j];
        dirs[j] = t;
    }

    // 尝试每个方向
    for (int d = 0; d < 4; d++) {
        int dir = dirs[d];
        int nr = row;
        int nc = col;
        int opposite = 0;

        if (dir == kWallTop) { nr = row - 1; opposite = kWallBottom; }
        else if (dir == kWallRight) { nc = col + 1; opposite = kWallLeft; }
        else if (dir == kWallBottom) { nr = row + 1; opposite = kWallTop; }
        else if (dir == kWallLeft) { nc = col - 1; opposite = kWallRight; }

        if (nr < 0 || nr >= MAZE_GRID_SIZE || nc < 0 || nc >= MAZE_GRID_SIZE) {
            continue;
        }
        if (visited[nr][nc]) {
            continue;
        }

        // 拆除当前格子和相邻格子之间的墙
        walls[row][col][dir] = false;
        walls[nr][nc][opposite] = false;

        // 递归访问邻居
        MazeDfsGenerate(walls, nr, nc, visited);
    }
}

} // namespace

void AttitudeDisplay::GenerateMaze()
{
    // 初始化：所有墙壁都存在
    for (int r = 0; r < MAZE_GRID_SIZE; r++) {
        for (int c = 0; c < MAZE_GRID_SIZE; c++) {
            maze_walls_[r][c][kWallTop] = true;
            maze_walls_[r][c][kWallRight] = true;
            maze_walls_[r][c][kWallBottom] = true;
            maze_walls_[r][c][kWallLeft] = true;
        }
    }

    // 使用当前 tick 作为随机种子（保证每次不同）
    srand(lv_tick_get());

    // 访问数组
    bool visited[MAZE_GRID_SIZE][MAZE_GRID_SIZE] = {false};

    // DFS 生成从 (0,0) 开始
    MazeDfsGenerate(maze_walls_, 0, 0, visited);

    // 拆除起点的左上角边框和终点的右下角边框，便于视觉辨识
    maze_walls_[0][0][kWallTop] = false;
    maze_walls_[0][0][kWallLeft] = false;
    maze_walls_[maze_end_row_][maze_end_col_][kWallBottom] = false;
    maze_walls_[maze_end_row_][maze_end_col_][kWallRight] = false;

    ESP_LOGI(TAG, "Maze generated (%dx%d)", MAZE_GRID_SIZE, MAZE_GRID_SIZE);
}

void AttitudeDisplay::DrawMazeOnCanvas()
{
    if (maze_canvas_ == nullptr) {
        return;
    }

    lv_obj_t* canvas = maze_canvas_;

    // 用黑色背景清空
    lv_color_t bg = lv_color_hex(0x0A0A0A);
    lv_color_t wall_color = lv_color_hex(0xD4AF37); // 金色墙壁
    lv_color_t end_color = lv_color_hex(0xFF6B6B);  // 终点标记颜色

    // 清空整个 canvas
    for (int y = 0; y < MAZE_CANVAS_SIZE; y++) {
        for (int x = 0; x < MAZE_CANVAS_SIZE; x++) {
            lv_canvas_set_px(canvas, x, y, bg, LV_OPA_COVER);
        }
    }

    const int cell = MAZE_CELL_SIZE;

    // 使用 LVGL 9.x layer API 进行矩形绘制
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = wall_color;
    dsc.bg_opa = LV_OPA_COVER;
    dsc.border_width = 0;
    dsc.radius = 0;

    lv_area_t area;

    // 绘制每格的右墙和底墙
    for (int r = 0; r < MAZE_GRID_SIZE; r++) {
        for (int c = 0; c < MAZE_GRID_SIZE; c++) {
            // 右墙
            if (maze_walls_[r][c][kWallRight] && c < MAZE_GRID_SIZE - 1) {
                int x1 = (c + 1) * cell - MAZE_WALL_WIDTH / 2;
                int x2 = (c + 1) * cell + MAZE_WALL_WIDTH / 2;
                int y1 = r * cell;
                int y2 = (r + 1) * cell;
                lv_area_set(&area, x1, y1, x2, y2);
                lv_draw_rect(&layer, &dsc, &area);
            }
            // 底墙
            if (maze_walls_[r][c][kWallBottom] && r < MAZE_GRID_SIZE - 1) {
                int x1 = c * cell;
                int x2 = (c + 1) * cell;
                int y1 = (r + 1) * cell - MAZE_WALL_WIDTH / 2;
                int y2 = (r + 1) * cell + MAZE_WALL_WIDTH / 2;
                lv_area_set(&area, x1, y1, x2, y2);
                lv_draw_rect(&layer, &dsc, &area);
            }
        }
    }

    // 绘制外边框（上、下、左、右四周边）
    lv_area_set(&area, 0, 0, MAZE_CANVAS_SIZE, MAZE_WALL_WIDTH);
    lv_draw_rect(&layer, &dsc, &area);
    lv_area_set(&area, 0, 0, MAZE_WALL_WIDTH, MAZE_CANVAS_SIZE);
    lv_draw_rect(&layer, &dsc, &area);
    lv_area_set(&area, MAZE_CANVAS_SIZE - MAZE_WALL_WIDTH, 0, MAZE_CANVAS_SIZE - 1, MAZE_CANVAS_SIZE);
    lv_draw_rect(&layer, &dsc, &area);
    lv_area_set(&area, 0, MAZE_CANVAS_SIZE - MAZE_WALL_WIDTH, MAZE_CANVAS_SIZE, MAZE_CANVAS_SIZE);
    lv_draw_rect(&layer, &dsc, &area);

    // 在终点位置标记一个小方块
    int ex = maze_end_col_ * cell + cell / 4;
    int ey = maze_end_row_ * cell + cell / 4;
    int es = cell / 2;
    dsc.bg_color = end_color;
    lv_area_set(&area, ex, ey, ex + es, ey + es);
    lv_draw_rect(&layer, &dsc, &area);

    // 起点位置用蓝色小方块标记
    int sx = 0 * cell + cell / 4;
    int sy = 0 * cell + cell / 4;
    dsc.bg_color = lv_color_hex(0x4FC3F7);
    lv_area_set(&area, sx, sy, sx + es, sy + es);
    lv_draw_rect(&layer, &dsc, &area);

    lv_canvas_finish_layer(canvas, &layer);

    if (display_ != nullptr) {
        lv_refr_now(display_);
    }
}

void AttitudeDisplay::MoveMazePlayer(int drow, int dcol)
{
    if (mood_gua_sub_state_ != MoodGuaSubState::MazePlaying) {
        return;
    }

    int new_row = maze_player_row_ + drow;
    int new_col = maze_player_col_ + dcol;

    // 检查边界
    if (new_row < 0 || new_row >= MAZE_GRID_SIZE
        || new_col < 0 || new_col >= MAZE_GRID_SIZE) {
        return;
    }

    // 检查墙壁（从当前格移动到新格，需要确保对应方向没有墙）
    int wall_dir = -1;
    if (drow == -1 && dcol == 0) wall_dir = kWallTop;       // 向上走
    if (drow == 1 && dcol == 0) wall_dir = kWallBottom;     // 向下走
    if (drow == 0 && dcol == 1) wall_dir = kWallRight;      // 向右走
    if (drow == 0 && dcol == -1) wall_dir = kWallLeft;      // 向左走

    if (wall_dir == -1) {
        return; // 只支持单方向移动
    }

    if (maze_walls_[maze_player_row_][maze_player_col_][wall_dir]) {
        // 有墙，不能走
        return;
    }

    // 可以移动
    maze_player_row_ = new_row;
    maze_player_col_ = new_col;
    RedrawMazePlayer();
    PlayFortuneMenuSelectSound();

    ESP_LOGD(TAG, "Maze player moved to (%d,%d)", new_row, new_col);

    // 检查是否到达终点
    if (new_row == maze_end_row_ && new_col == maze_end_col_) {
        OnMazeWon();
    }
}

void AttitudeDisplay::RedrawMazePlayer()
{
    if (maze_player_obj_ == nullptr) {
        return;
    }

    // 计算玩家在 canvas 中的位置（相对 panel 的坐标）
    const int maze_offset = (TAIJI_CANVAS_SIZE - MAZE_CANVAS_SIZE) / 2;
    const int cell = MAZE_CELL_SIZE;
    const int px = maze_offset + maze_player_col_ * cell + (cell - MAZE_PLAYER_SIZE) / 2;
    const int py = maze_offset + maze_player_row_ * cell + (cell - MAZE_PLAYER_SIZE) / 2;

    lv_obj_set_pos(maze_player_obj_, px, py);
}

void AttitudeDisplay::OnMazeTouched(lv_event_t* e)
{
    if (mood_gua_sub_state_ != MoodGuaSubState::MazePlaying) {
        return;
    }

    lv_indev_t* indev = lv_indev_active();
    if (indev == nullptr) {
        return;
    }

    lv_point_t pt;
    lv_indev_get_point(indev, &pt);

    // 获取 panel 的位置和尺寸
    lv_obj_t* panel = static_cast<lv_obj_t*>(lv_event_get_target(e));
    lv_area_t panel_area;
    lv_obj_get_coords(panel, &panel_area);

    // 将点击坐标转换为相对 panel 的坐标
    int rel_x = pt.x - panel_area.x1;
    int rel_y = pt.y - panel_area.y1;

    // 计算迷宫区域的中心偏移
    int center_x = TAIJI_CANVAS_SIZE / 2;
    int center_y = TAIJI_CANVAS_SIZE / 2;
    int dx = rel_x - center_x;
    int dy = rel_y - center_y;

    // 根据点击方向（上/下/左/右）移动
    // 优先选择绝对值较大的方向
    if (abs(dx) > abs(dy)) {
        // 水平方向
        if (dx > 0) {
            MoveMazePlayer(0, 1);  // 右
        } else {
            MoveMazePlayer(0, -1); // 左
        }
    } else {
        // 垂直方向
        if (dy > 0) {
            MoveMazePlayer(1, 0);  // 下
        } else {
            MoveMazePlayer(-1, 0); // 上
        }
    }
}

void AttitudeDisplay::OnMazeTouchedStatic(lv_event_t* e)
{
    AttitudeDisplay* self = static_cast<AttitudeDisplay*>(lv_event_get_user_data(e));
    if (self != nullptr) {
        self->OnMazeTouched(e);
    }
}

void AttitudeDisplay::OnMazeWon()
{
    ESP_LOGI(TAG, "Maze won! Player reached end (%d,%d)", maze_player_row_, maze_player_col_);

    // 显示胜利标记
    if (maze_end_label_ != nullptr) {
        lv_obj_remove_flag(maze_end_label_, LV_OBJ_FLAG_HIDDEN);
        const int maze_offset = (TAIJI_CANVAS_SIZE - MAZE_CANVAS_SIZE) / 2;
        const int cell = MAZE_CELL_SIZE;
        const int px = maze_offset + maze_end_col_ * cell;
        const int py = maze_offset + maze_end_row_ * cell - cell / 2 - 4;
        lv_obj_set_pos(maze_end_label_, px, py);
    }

    // 播放完成音效
    Application::GetInstance().PlayUiSound(Lang::Sounds::OGG_SUCCESS);

    // 一段时间后自动生成新的迷宫
    lv_timer_t* t = lv_timer_create([](lv_timer_t* timer) {
        AttitudeDisplay* self = static_cast<AttitudeDisplay*>(lv_timer_get_user_data(timer));
        if (self != nullptr && self->mood_gua_sub_state_ == MoodGuaSubState::MazePlaying) {
            self->maze_player_row_ = 0;
            self->maze_player_col_ = 0;
            self->GenerateMaze();
            self->DrawMazeOnCanvas();
            self->RedrawMazePlayer();
            if (self->maze_end_label_ != nullptr) {
                lv_obj_add_flag(self->maze_end_label_, LV_OBJ_FLAG_HIDDEN);
            }
            ESP_LOGI(TAG, "New maze after win");
        }
        lv_timer_del(timer);
    }, 1500, this);
    (void)t;
}

#endif  // MOOD_GUA_SUB_FEATURES_ENABLED