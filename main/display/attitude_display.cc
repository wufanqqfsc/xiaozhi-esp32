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

#define FORTUNE_CARD_W            FORTUNE_CARD_SIZE
#define FORTUNE_CARD_H            FORTUNE_CARD_SIZE

// 调试信息卡（与后台交互事件）配置
#define DEBUG_INFO_SHOW_MS        3000   // 单条事件默认显示时长
#define DEBUG_INFO_HOLD_MAX_MS    10000  // 联动音频播放时的最大允许显示时长（兜底）
#define DEBUG_INFO_DEDUP_MS       1500   // 同一标题的去重间隔
// 调试卡配色：与运势卡（金）区分，使用青/品红强调，便于识别
#define DEBUG_INFO_BORDER_COLOR   lv_color_hex(0x00C8C8)   // 青色描边
#define DEBUG_INFO_TITLE_COLOR    lv_color_hex(0xD4AF37)  // 金色
#define DEBUG_INFO_DETAIL_COLOR   lv_color_hex(0xE0E0E0)
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
#define FORTUNE_GUA_CANVAS_W      56
#define FORTUNE_GUA_CANVAS_H      36

LV_FONT_DECLARE(BUILTIN_TEXT_FONT);
LV_FONT_DECLARE(BUILTIN_ICON_FONT);
LV_FONT_DECLARE(font_awesome_30_4);

namespace {

// WiFi 鱼眼位 canvas 已铺纯黑，与阴鱼底色一致
static const lv_color_t kFisheyeGrayIcon = lv_color_hex(0x909090);
static const lv_color_t kFisheyeBleBlue = lv_color_hex(0x2196F3);    // BLE 已连接：蓝边
static const uint32_t kBleBorderGray = 0x909090;
static const uint32_t kBleBorderWhite = 0xFFFFFF;
static const uint32_t kBleBorderBlue = 0x2196F3;
static const lv_color_t kFisheyeGold = lv_color_hex(0xD4AF37);
static const lv_color_t kFisheyeWhite = lv_color_hex(0xFFFFFF);
static const lv_color_t kFisheyeDark = lv_color_hex(0x0A0A0A);
// 鱼眼描边固定色：WiFi 白边 / BLE 黑边（状态由图标色区分）
static const lv_color_t kFisheyeWifiRingBorder = kFisheyeWhite;
static const lv_color_t kFisheyeBleRingBorder = lv_color_black();

static uint32_t* AllocFisheyeCanvasBuffer(int size)
{
    const uint32_t buf_size = static_cast<uint32_t>(size) * static_cast<uint32_t>(size) *
                              sizeof(uint32_t);
    auto* buf = static_cast<uint32_t*>(heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM));
    if (buf == nullptr) {
        buf = static_cast<uint32_t*>(malloc(buf_size));
    }
    if (buf != nullptr) {
        memset(buf, 0, buf_size);
    }
    return buf;
}

static lv_obj_t* CreateFisheyeCanvas(lv_obj_t* parent, uint32_t*& out_buf)
{
    out_buf = AllocFisheyeCanvasBuffer(FISHEYE_ICON_SIZE);
    if (out_buf == nullptr) {
        ESP_LOGE(TAG, "Fisheye canvas alloc failed (%d px)", FISHEYE_ICON_SIZE);
        return nullptr;
    }

    lv_obj_t* canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(canvas, out_buf, FISHEYE_ICON_SIZE, FISHEYE_ICON_SIZE,
                         LV_COLOR_FORMAT_ARGB8888);
    lv_obj_set_pos(canvas, 0, 0);
    lv_obj_set_style_bg_opa(canvas, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(canvas, 0, 0);
    lv_image_set_antialias(canvas, false);
    lv_obj_clear_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
    return canvas;
}

static void FisheyeOpaAnimCb(void* obj, int32_t value)
{
    lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), static_cast<lv_opa_t>(value), 0);
}

static void FisheyeBorderColorAnimCb(void* obj, int32_t value)
{
    (void)obj;
    (void)value;
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

struct FortuneMenuItemDef {
    const char* icon;
    const char* func_label;
    int gua_index;
    int dir_index;
};

/** Boot 长按完整运势卡文案（与环上功能项一一对应，不用于短提示卡） */
struct FortuneMenuFortuneText {
    const char* gua_name;
    const char* core_text;
    const char* yi;
    const char* ji;
};

static const FortuneMenuItemDef kFortuneMenuDefs[FORTUNE_MENU_COUNT] = {
    {FONT_AWESOME_SUN, "今日运势", 63, 0},
    {FONT_AWESOME_LOCATION_DOT, "财运方位", 2, 1},
    {FONT_AWESOME_GEAR, "事业运势", 51, 2},
    {FONT_AWESOME_HEART, "感情运势", 58, 3},
    {FONT_AWESOME_COMPASS, "心情卦", 3, 4},
    {FONT_AWESOME_CALENDAR, "黄历宜忌", 30, 5},
    {FONT_AWESOME_ARROWS_ROTATE, "节气提示", 52, 6},
    {FONT_AWESOME_PEN_TO_SQUARE, "自定义", 12, 7},
    {FONT_AWESOME_STAR, "健康运势", 57, 0},
    {FONT_AWESOME_GLASSES, "学业运势", 63, 1},
    {FONT_AWESOME_LOCATION_ARROW, "出行吉日", 44, 2},
    {FONT_AWESOME_USER, "贵人运势", 11, 3},
};

static const FortuneMenuFortuneText kFortuneMenuFortuneTexts[FORTUNE_MENU_COUNT] = {
    {"乾为天", "今日宜进取，顺势而行。", "宜：签约、出行", "忌：熬夜、口舌"},
    {"坤为地", "财位东南，守成为上。", "宜：理财、储蓄", "忌：投机、借贷"},
    {"震为雷", "事业有转机，宜主动。", "宜：述职、立项", "忌：冲动决策"},
    {"兑为泽", "情缘渐暖，多沟通。", "宜：表白、约会", "忌：猜疑、冷战"},
    {"水雷屯", "心绪繁杂，宜静思。", "宜：独处、冥想", "忌：争执、赶工"},
    {"离为火", "今日宜祭祀安床。", "宜：嫁娶、开市", "忌：破土、动土"},
    {"艮为山", "惊蛰将至，万物生发。", "宜：播种、养生", "忌：寒凉、懒动"},
    {"天地否", "所问之事，宜待时机。", "宜：蓄力、观望", "忌：强求、冒进"},
    {"风为木", "身心调和，宜养正气。", "宜：早睡、运动", "忌：过劳、贪凉"},
    {"水火既济", "学业渐进，宜温故知新。", "宜：复习、请教", "忌：浮躁、分心"},
    {"天风姤", "远行有利，宜择吉启程。", "宜：出行、访友", "忌：仓促、夜行"},
    {"地天泰", "贵人暗助，宜广结善缘。", "宜：拜访、合作", "忌：孤行、傲慢"},
};

static const lv_font_t* GetFortuneMenuIconFont()
{
    return &font_awesome_30_4;
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
    lv_obj_add_flag(fortune_menu_ring_touch_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(fortune_menu_ring_touch_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(fortune_menu_ring_touch_, OnFortuneMenuRingTouched,
                        LV_EVENT_CLICKED, this);

    // 触摸层置顶：整屏接收点击，再按环带坐标映射扇区（图标仅展示、不拦截）
    for (int i = 0; i < FORTUNE_MENU_COUNT; ++i) {
        if (fortune_menu_labels_[i] != nullptr) {
            lv_obj_move_foreground(fortune_menu_labels_[i]);
        }
    }
    lv_obj_move_foreground(fortune_menu_ring_touch_);

    ESP_LOGI(TAG, "Fortune menu ring touch layer ready (annulus %d~%d)",
             FORTUNE_MENU_TOUCH_INNER_R, FORTUNE_MENU_TOUCH_OUTER_R);
}

int AttitudeDisplay::FortuneMenuIndexFromPoint(int x, int y) const
{
    const int dx = x - ATTITUDE_CENTER_X;
    const int dy = y - ATTITUDE_CENTER_Y;
    const int dist_sq = dx * dx + dy * dy;
    const int inner = FORTUNE_MENU_TOUCH_INNER_R;
    const int outer = FORTUNE_MENU_TOUCH_OUTER_R;
    if (dist_sq < inner * inner || dist_sq > outer * outer) {
        return -1;
    }

    const double angle_deg = std::atan2(static_cast<double>(dy),
                                        static_cast<double>(dx)) * 180.0 / M_PI;
    const double step = 360.0 / FORTUNE_MENU_COUNT;
    double delta = angle_deg - static_cast<double>(FORTUNE_MENU_START_ANGLE_DEG);
    while (delta < 0.0) {
        delta += 360.0;
    }
    while (delta >= 360.0) {
        delta -= 360.0;
    }
    return static_cast<int>((delta + step * 0.5) / step) % FORTUNE_MENU_COUNT;
}

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
    fortune_feature_card_suppressed_ = false;
    if (was_active && prev != index) {
        UpdateFortuneMenuItemVisual(prev, false);
    }
    UpdateFortuneMenuItemVisual(index, true);
    ShowFortuneMenuFeatureCardUnlocked(index);
    SyncStudyAreaWithMenu();
    ESP_LOGI(TAG, "Fortune menu select -> %d (%s)", index,
             kFortuneMenuDefs[index].func_label);
}

void AttitudeDisplay::SelectFortuneMenuItem(int index)
{
    DisplayLockGuard lock(this);
    SelectFortuneMenuItemUnlocked(index);
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
    fortune_feature_card_suppressed_ = false;
    UpdateFortuneMenuItemVisual(prev, false);
    UpdateFortuneMenuItemVisual(fortune_menu_selected_index_, true);
    if (!fortune_feature_card_suppressed_) {
        ShowFortuneMenuFeatureCardUnlocked(fortune_menu_selected_index_);
    }
    SyncStudyAreaWithMenu();
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

void AttitudeDisplay::OnFortuneMenuRingTouched(lv_event_t* e)
{
    auto* self = static_cast<AttitudeDisplay*>(lv_event_get_user_data(e));
    if (self == nullptr || self->fortune_state_ != FortuneState::Idle) {
        return;
    }

    lv_indev_t* indev = lv_indev_active();
    if (indev == nullptr) {
        return;
    }
    lv_point_t pt;
    lv_indev_get_point(indev, &pt);

    const int idx = self->FortuneMenuIndexFromPoint(pt.x, pt.y);
    if (idx < 0 || idx >= FORTUNE_MENU_COUNT) {
        return;
    }

    self->SelectFortuneMenuItem(idx);
    ESP_LOGI(TAG, "Fortune ring touch -> idx=%d (%s)", idx,
             kFortuneMenuDefs[idx].func_label);
}

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

void AttitudeDisplay::SetTaijiCoreVisible(bool visible)
{
    if (visible) {
        CompassTaiji::SetStudyRingMode(false);
        ApplyStudyAreaOrientation(false);
    } else {
        CompassTaiji::SetStudyRingMode(true);
        ApplyStudyAreaOrientation(true);
    }
    if (auto* canvas = CompassTaiji::GetCanvas()) {
        lv_obj_remove_flag(canvas, LV_OBJ_FLAG_HIDDEN);
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
        if (study_had_auto_rotation_ && fortune_state_ == FortuneState::Idle) {
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
    if (fortune_state_ != FortuneState::Idle) {
#if STUDY_SUB_FEATURES_ENABLED
        ExitStudyArea();
#endif
        return;
    }
    if (!fortune_menu_selection_active_) {
#if STUDY_SUB_FEATURES_ENABLED
        ExitStudyArea();
#endif
        return;
    }
#if STUDY_SUB_FEATURES_ENABLED
    if (fortune_menu_selected_index_ == static_cast<int>(FortuneMenuType::Study)) {
        if (study_sub_state_ == StudySubState::Hidden && !fortune_feature_card_suppressed_) {
            EnterStudyMenu();
        }
    } else {
        fortune_feature_card_suppressed_ = false;
        ExitStudyArea();
    }
#endif
}

void AttitudeDisplay::ShowFortuneMenuFeatureCardUnlocked(int index)
{
    if (index < 0 || index >= FORTUNE_MENU_COUNT) {
        return;
    }
    if (fortune_state_ != FortuneState::Idle) {
        return;
    }
#if STUDY_SUB_FEATURES_ENABLED
    if (study_sub_state_ != StudySubState::Hidden) {
        return;
    }
#endif

    const auto& def = kFortuneMenuDefs[index];
    const auto& texts = kFortuneMenuFortuneTexts[index];
    debug_info_is_fortune_feature_ = true;
    DebugInfoPresentOpts opts;
    opts.persistent = true;
    opts.screen_title_overlay = false;
    
    CreateDebugInfoCard();
    if (debug_info_card_ == nullptr) {
        return;
    }
    
    auto lvgl_theme = static_cast<LvglTheme*>(GetTheme());
    const lv_font_t* text_font = (lvgl_theme != nullptr && lvgl_theme->text_font() != nullptr)
        ? lvgl_theme->text_font()->font() : &BUILTIN_TEXT_FONT;

    lv_obj_remove_flag(debug_info_card_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(debug_info_card_);

    if (debug_info_title_ != nullptr) {
        lv_obj_remove_flag(debug_info_title_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_font(debug_info_title_, text_font, 0);
        lv_obj_set_style_text_color(debug_info_title_, DEBUG_INFO_TITLE_COLOR, 0);
        lv_label_set_text(debug_info_title_, def.func_label);
        lv_obj_move_foreground(debug_info_title_);
    }

    if (debug_info_gua_label_ != nullptr) {
        lv_obj_remove_flag(debug_info_gua_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_font(debug_info_gua_label_, text_font, 0);
        lv_obj_set_style_text_color(debug_info_gua_label_, DEBUG_INFO_DETAIL_COLOR, 0);
        lv_label_set_text(debug_info_gua_label_, texts.gua_name);
        lv_obj_move_foreground(debug_info_gua_label_);
    }

    if (debug_info_core_label_ != nullptr) {
        lv_obj_remove_flag(debug_info_core_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_font(debug_info_core_label_, text_font, 0);
        lv_obj_set_style_text_color(debug_info_core_label_, DEBUG_INFO_TITLE_COLOR, 0);
        lv_label_set_text(debug_info_core_label_, texts.core_text);
        lv_obj_move_foreground(debug_info_core_label_);
    }

    if (debug_info_yi_label_ != nullptr) {
        lv_obj_remove_flag(debug_info_yi_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_font(debug_info_yi_label_, text_font, 0);
        lv_obj_set_style_text_color(debug_info_yi_label_, lv_color_hex(0x4CAF50), 0);
        lv_label_set_text(debug_info_yi_label_, texts.yi);
        lv_obj_move_foreground(debug_info_yi_label_);
    }

    if (debug_info_ji_label_ != nullptr) {
        lv_obj_remove_flag(debug_info_ji_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_font(debug_info_ji_label_, text_font, 0);
        lv_obj_set_style_text_color(debug_info_ji_label_, lv_color_hex(0xE53935), 0);
        lv_label_set_text(debug_info_ji_label_, texts.ji);
        lv_obj_move_foreground(debug_info_ji_label_);
    }

    if (debug_info_detail_ != nullptr) {
        lv_obj_add_flag(debug_info_detail_, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(debug_info_detail_, "");
    }

    lv_obj_update_layout(debug_info_card_);
    if (display_ != nullptr) {
        lv_refr_now(display_);
    }

    // 调试日志：串口数据源校验（按故障分析文档 3.1 节）
    ESP_LOGI(TAG, "[Fortune:Today] title='%s' gua='%s' core='%s' yi='%s' ji='%s'",
             def.func_label, texts.gua_name, texts.core_text, texts.yi, texts.ji);

    debug_info_last_title_ = def.func_label;
    debug_info_last_show_ms_ = lv_tick_get();

    if (debug_info_hide_timer_ != nullptr) {
        lv_timer_pause(debug_info_hide_timer_);
    }
}

void AttitudeDisplay::ShowFortuneMenuFeatureCard(int index)
{
    DisplayLockGuard lock(this);
    ShowFortuneMenuFeatureCardUnlocked(index);
}

bool AttitudeDisplay::HandleStudyPowerKey()
{
    DisplayLockGuard lock(this);
#if STUDY_SUB_FEATURES_ENABLED
    if (study_sub_state_ != StudySubState::Hidden) {
        ExitStudyArea();
        fortune_feature_card_suppressed_ = true;
        ESP_LOGI(TAG, "PWR: study area exited -> full taiji (re-select Study to re-enter)");
        return true;
    }
#endif
    if (debug_info_card_ != nullptr
        && !lv_obj_has_flag(debug_info_card_, LV_OBJ_FLAG_HIDDEN)) {
        fortune_feature_card_suppressed_ = true;
        HideDebugInfoUnlocked();
        ESP_LOGI(TAG, "PWR: fortune feature card dismissed");
        return true;
    }
    return false;
}

void AttitudeDisplay::ShowFortuneFromMenu(FortuneMenuType type)
{
    const int idx = static_cast<int>(type);
    if (idx < 0 || idx >= FORTUNE_MENU_COUNT) {
        return;
    }

    const auto& m = kFortuneMenuDefs[idx];
    const auto& t = kFortuneMenuFortuneTexts[idx];
    ShowFortune(m.func_label, t.gua_name, t.core_text, t.yi, t.ji, m.gua_index, m.dir_index);
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

void AttitudeDisplay::RedrawWifiFisheyeCanvas()
{
    if (wifi_fisheye_canvas_ == nullptr) {
        return;
    }
    CompassTaiji::PaintFisheyeDisc(wifi_fisheye_canvas_, FISHEYE_ICON_SIZE,
                                   lv_color_black(), kFisheyeWifiRingBorder,
                                   lv_color_white(), FISHEYE_BORDER_WIDTH);
}

void AttitudeDisplay::RedrawBleFisheyeCanvas()
{
    if (ble_fisheye_canvas_ == nullptr) {
        return;
    }
    CompassTaiji::PaintFisheyeDisc(ble_fisheye_canvas_, FISHEYE_ICON_SIZE,
                                   lv_color_white(), kFisheyeBleRingBorder,
                                   lv_color_black(), FISHEYE_BORDER_WIDTH);
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
    lv_obj_set_style_radius(wifi_fisheye_, FISHEYE_ICON_SIZE / 2, 0);
    lv_obj_set_style_clip_corner(wifi_fisheye_, true, 0);
    lv_obj_set_style_bg_opa(wifi_fisheye_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifi_fisheye_, 0, 0);
    lv_obj_set_style_pad_all(wifi_fisheye_, 0, 0);
    lv_obj_clear_flag(wifi_fisheye_, LV_OBJ_FLAG_CLICKABLE);

    uint32_t* canvas_buf = nullptr;
    wifi_fisheye_canvas_ = CreateFisheyeCanvas(wifi_fisheye_, canvas_buf);
    (void)canvas_buf;
    RedrawWifiFisheyeCanvas();

    wifi_fisheye_icon_ = lv_label_create(wifi_fisheye_);
    lv_obj_set_style_text_font(wifi_fisheye_icon_, GetIconFont(this), 0);
    lv_label_set_text(wifi_fisheye_icon_, FONT_AWESOME_WIFI);
    lv_obj_center(wifi_fisheye_icon_);
    lv_obj_move_foreground(wifi_fisheye_icon_);

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
    lv_obj_set_style_radius(ble_fisheye_, FISHEYE_ICON_SIZE / 2, 0);
    lv_obj_set_style_clip_corner(ble_fisheye_, true, 0);
    lv_obj_set_style_bg_opa(ble_fisheye_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ble_fisheye_, 0, 0);
    lv_obj_set_style_pad_all(ble_fisheye_, 0, 0);
    lv_obj_clear_flag(ble_fisheye_, LV_OBJ_FLAG_CLICKABLE);

    uint32_t* canvas_buf = nullptr;
    ble_fisheye_canvas_ = CreateFisheyeCanvas(ble_fisheye_, canvas_buf);
    (void)canvas_buf;
    RedrawBleFisheyeCanvas();

    ble_fisheye_icon_ = lv_label_create(ble_fisheye_);
    lv_obj_set_style_text_font(ble_fisheye_icon_, GetIconFont(this), 0);
    lv_label_set_text(ble_fisheye_icon_, FONT_AWESOME_BLUETOOTH);
    lv_obj_center(ble_fisheye_icon_);
    lv_obj_move_foreground(ble_fisheye_icon_);

    ESP_LOGI(TAG, "BLE fisheye on taiji at local (%d,%d)",
             FISHEYE_BLE_LOCAL_X, FISHEYE_BLE_LOCAL_Y);
}

void AttitudeDisplay::ApplyWifiFisheyeStyle(WifiStatus status)
{
    if (wifi_fisheye_ == nullptr || wifi_fisheye_icon_ == nullptr) {
        return;
    }

    StopFisheyePulse(wifi_fisheye_);
    RedrawWifiFisheyeCanvas();

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

void AttitudeDisplay::ApplyBleFisheyeStyle(BleStatus status)
{
    if (ble_fisheye_ == nullptr || ble_fisheye_icon_ == nullptr) {
        return;
    }

    StopFisheyePulse(ble_fisheye_);
    RedrawBleFisheyeCanvas();

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

    const int line_w = 40;
    const int line_h = 2;
    const int gap_y = 3;
    const int yin_gap = 6;
    int y = 2;
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
    if (self == nullptr) {
        return;
    }
    if (self->fortune_state_ != FortuneState::Animating) {
        self->fortune_fisheye_pulse_timer_ = nullptr;
        lv_timer_delete(timer);
        self->ApplyWifiFisheyeStyle(self->wifi_status_);
        self->ApplyBleFisheyeStyle(self->ble_status_);
        return;
    }

    self->fortune_fisheye_pulse_gold_ = !self->fortune_fisheye_pulse_gold_;
    const lv_color_t icon = self->fortune_fisheye_pulse_gold_ ? kFisheyeGold : kFisheyeGrayIcon;

    if (self->wifi_fisheye_canvas_ != nullptr) {
        self->RedrawWifiFisheyeCanvas();
        lv_obj_set_style_text_color(self->wifi_fisheye_icon_, icon, 0);
    }
    if (self->ble_fisheye_canvas_ != nullptr) {
        self->RedrawBleFisheyeCanvas();
        const lv_color_t ble_icon = self->fortune_fisheye_pulse_gold_
            ? kFisheyeBleBlue : kFisheyeGrayIcon;
        lv_obj_set_style_text_color(self->ble_fisheye_icon_, ble_icon, 0);
    }

    self->fortune_fisheye_pulse_count_++;
    if (self->fortune_fisheye_pulse_count_ >= 5) {
        lv_timer_delete(timer);
        self->fortune_fisheye_pulse_timer_ = nullptr;
        self->ApplyWifiFisheyeStyle(self->wifi_status_);
        self->ApplyBleFisheyeStyle(self->ble_status_);
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
    ExitStudyArea();
    fortune_state_ = FortuneState::Animating;
    SetFortuneMenuVisible(false);
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

    // 使用绝对定位方式（与 debug_info_card 相同模式）
    // 避免圆形容器 + clip_corner + flex 布局导致子对象尺寸计算异常
    const int card_w = FORTUNE_CARD_W;
    const int text_w = card_w - 40;
    const int text_x = 20;
    const int row_h = 40;
    const int core_h = 60;

    fortune_card_ = lv_obj_create(attitude_container_);
    lv_obj_set_size(fortune_card_, card_w, card_w);
    lv_obj_set_pos(fortune_card_, FORTUNE_CARD_X, FORTUNE_CARD_Y);
    lv_obj_set_style_radius(fortune_card_, FORTUNE_CARD_RADIUS, 0);
    lv_obj_set_style_clip_corner(fortune_card_, true, 0);
    lv_obj_set_style_bg_color(fortune_card_, c.card_bg, 0);
    lv_obj_set_style_bg_opa(fortune_card_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(fortune_card_, c.border_line, 0);
    lv_obj_set_style_border_width(fortune_card_, 2, 0);
    lv_obj_set_style_pad_all(fortune_card_, 0, 0);
    lv_obj_set_style_layout(fortune_card_, LV_LAYOUT_NONE, 0);
    lv_obj_clear_flag(fortune_card_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(fortune_card_, LV_OBJ_FLAG_HIDDEN);

    // 计算各行 y 坐标
    // total content: row_h(func) + row_h(gua_widget_h=36 取 row_h) + row_h(gua_name) +
    //                core_h(core) + row_h(yi) + row_h(ji) + row_h(close)
    //              = 40 + 40 + 40 + 60 + 40 + 40 + 40 = 300
    // card_w = 356, start_y = (356 - 300) / 2 = 28
    const int start_y = 28;
    int cur_y = start_y;

    // func 标题
    fortune_func_label_ = lv_label_create(fortune_card_);
    lv_obj_set_style_text_font(fortune_func_label_, text_font, 0);
    lv_obj_set_style_text_color(fortune_func_label_, c.text_main, 0);
    lv_obj_set_width(fortune_func_label_, text_w);
    lv_obj_set_height(fortune_func_label_, row_h);
    lv_obj_set_x(fortune_func_label_, text_x);
    lv_obj_set_y(fortune_func_label_, cur_y);
    lv_label_set_long_mode(fortune_func_label_, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(fortune_func_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(fortune_func_label_, fortune_func_label_text_.c_str());
    cur_y += row_h;

    // 卦象 widget
    fortune_gua_widget_ = CreateHexagramWidget(fortune_card_, fortune_gua_index_);
    if (fortune_gua_widget_ != nullptr) {
        lv_obj_set_x(fortune_gua_widget_, text_x + (text_w - FORTUNE_GUA_CANVAS_W) / 2);
        lv_obj_set_y(fortune_gua_widget_, cur_y + (row_h - FORTUNE_GUA_CANVAS_H) / 2);
    }
    cur_y += row_h;

    // 卦名
    fortune_gua_name_label_ = lv_label_create(fortune_card_);
    lv_obj_set_style_text_font(fortune_gua_name_label_, text_font, 0);
    lv_obj_set_style_text_color(fortune_gua_name_label_, c.text_main, 0);
    lv_obj_set_width(fortune_gua_name_label_, text_w);
    lv_obj_set_height(fortune_gua_name_label_, row_h);
    lv_obj_set_x(fortune_gua_name_label_, text_x);
    lv_obj_set_y(fortune_gua_name_label_, cur_y);
    lv_label_set_long_mode(fortune_gua_name_label_, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(fortune_gua_name_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(fortune_gua_name_label_, fortune_gua_name_text_.c_str());
    cur_y += row_h;

    // 核心文案（两行高度）
    fortune_core_label_ = lv_label_create(fortune_card_);
    lv_obj_set_style_text_font(fortune_core_label_, text_font, 0);
    lv_obj_set_style_text_color(fortune_core_label_, c.text_high, 0);
    lv_obj_set_width(fortune_core_label_, text_w);
    lv_obj_set_height(fortune_core_label_, core_h);
    lv_obj_set_x(fortune_core_label_, text_x);
    lv_obj_set_y(fortune_core_label_, cur_y);
    lv_label_set_long_mode(fortune_core_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(fortune_core_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(fortune_core_label_, fortune_core_text_.c_str());
    cur_y += core_h;

    // 宜
    fortune_yi_label_ = lv_label_create(fortune_card_);
    lv_obj_set_style_text_font(fortune_yi_label_, text_font, 0);
    lv_obj_set_style_text_color(fortune_yi_label_, kFortuneGreen, 0);
    lv_obj_set_width(fortune_yi_label_, text_w);
    lv_obj_set_height(fortune_yi_label_, row_h);
    lv_obj_set_x(fortune_yi_label_, text_x);
    lv_obj_set_y(fortune_yi_label_, cur_y);
    lv_label_set_long_mode(fortune_yi_label_, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(fortune_yi_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(fortune_yi_label_, fortune_yi_text_.c_str());
    cur_y += row_h;

    // 忌
    fortune_ji_label_ = lv_label_create(fortune_card_);
    lv_obj_set_style_text_font(fortune_ji_label_, text_font, 0);
    lv_obj_set_style_text_color(fortune_ji_label_, kFortuneRed, 0);
    lv_obj_set_width(fortune_ji_label_, text_w);
    lv_obj_set_height(fortune_ji_label_, row_h);
    lv_obj_set_x(fortune_ji_label_, text_x);
    lv_obj_set_y(fortune_ji_label_, cur_y);
    lv_label_set_long_mode(fortune_ji_label_, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(fortune_ji_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(fortune_ji_label_, fortune_ji_text_.c_str());
    cur_y += row_h;

    // 关闭提示
    fortune_close_label_ = lv_label_create(fortune_card_);
    lv_obj_set_style_text_font(fortune_close_label_, text_font, 0);
    lv_obj_set_style_text_color(fortune_close_label_, c.text_sub, 0);
    lv_obj_set_width(fortune_close_label_, text_w);
    lv_obj_set_height(fortune_close_label_, row_h);
    lv_obj_set_x(fortune_close_label_, text_x);
    lv_obj_set_y(fortune_close_label_, cur_y);
    lv_obj_set_style_text_align(fortune_close_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(fortune_close_label_, "按 Boot 关闭");

    lv_obj_remove_flag(fortune_card_, LV_OBJ_FLAG_HIDDEN);

    if (lv_obj_t* taiji = CompassTaiji::GetContainer()) {
        lv_obj_add_flag(taiji, LV_OBJ_FLAG_HIDDEN);
    }
    ESP_LOGI(TAG, "Fortune card shown %dx%d at (%d,%d) (absolute layout)",
             card_w, card_w, FORTUNE_CARD_X, FORTUNE_CARD_Y);
}

void AttitudeDisplay::DestroyFortuneCard()
{
    if (fortune_gua_name_label_ != nullptr) {
        lv_anim_delete(fortune_gua_name_label_, LabelColorAnimCb);
    }

    if (fortune_card_ != nullptr) {
        lv_obj_add_flag(fortune_card_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_delete(fortune_card_);
        fortune_card_ = nullptr;
    }

    if (lv_obj_t* taiji = CompassTaiji::GetContainer()) {
        lv_obj_remove_flag(taiji, LV_OBJ_FLAG_HIDDEN);
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
    ExitStudyArea();

    // 运势脉冲可能改写鱼眼边框/图标色，回到 Idle 时按缓存连接态恢复（勿用 Update*，避免 ShowFortune 持锁重入死锁）
    ApplyWifiFisheyeStyle(wifi_status_);
    ApplyBleFisheyeStyle(ble_status_);

    fortune_state_ = FortuneState::Idle;
    fortune_menu_selected_index_ = 0;
    fortune_menu_selection_active_ = false;
    SetFortuneMenuVisible(true);
    UpdateFortuneMenuSelection();
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

void AttitudeDisplay::CreateDebugInfoCard()
{
    if (debug_info_card_ != nullptr) {
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

    debug_info_card_ = lv_obj_create(attitude_container_);
    lv_obj_set_size(debug_info_card_, card_w, card_w); // 正方形，300x300
    lv_obj_set_pos(debug_info_card_, DEBUG_INFO_CARD_X, DEBUG_INFO_CARD_Y);
    lv_obj_set_style_radius(debug_info_card_, DEBUG_INFO_CARD_RADIUS, 0);
    lv_obj_set_style_clip_corner(debug_info_card_, true, 0);
    lv_obj_set_style_bg_color(debug_info_card_, lv_color_hex(0x0A1414), 0);
    lv_obj_set_style_bg_opa(debug_info_card_, LV_OPA_90, 0);
    lv_obj_set_style_border_color(debug_info_card_, DEBUG_INFO_BORDER_COLOR, 0);
    lv_obj_set_style_border_width(debug_info_card_, 2, 0);
    lv_obj_set_style_pad_all(debug_info_card_, 0, 0);
    lv_obj_set_style_layout(debug_info_card_, LV_LAYOUT_NONE, 0);
    lv_obj_clear_flag(debug_info_card_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(debug_info_card_, LV_OBJ_FLAG_HIDDEN);

    // total content: row_h + row_h + core_h + row_h + row_h = 220px
    // start y = (300 - 220) / 2 = 40
    const int start_y = 40;
    int cur_y = start_y;

    // title（功能标题）：第一行
    debug_info_title_ = lv_label_create(debug_info_card_);
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

    // gua_name（卦名）：第二行
    debug_info_gua_label_ = lv_label_create(debug_info_card_);
    lv_obj_set_style_text_font(debug_info_gua_label_, text_font, 0);
    lv_obj_set_style_text_color(debug_info_gua_label_, DEBUG_INFO_DETAIL_COLOR, 0);
    lv_obj_set_width(debug_info_gua_label_, text_w);
    lv_obj_set_height(debug_info_gua_label_, row_h);
    lv_obj_set_x(debug_info_gua_label_, text_x);
    lv_obj_set_y(debug_info_gua_label_, cur_y);
    lv_label_set_long_mode(debug_info_gua_label_, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(debug_info_gua_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(debug_info_gua_label_, "");
    cur_y += row_h;

    // core（核心文案）：第三行，两行高度，WRAP 模式允许多行
    debug_info_core_label_ = lv_label_create(debug_info_card_);
    lv_obj_set_style_text_font(debug_info_core_label_, text_font, 0);
    lv_obj_set_style_text_color(debug_info_core_label_, DEBUG_INFO_TITLE_COLOR, 0);
    lv_obj_set_width(debug_info_core_label_, text_w);
    lv_obj_set_height(debug_info_core_label_, core_h);
    lv_obj_set_x(debug_info_core_label_, text_x);
    lv_obj_set_y(debug_info_core_label_, cur_y);
    lv_label_set_long_mode(debug_info_core_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(debug_info_core_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(debug_info_core_label_, "");
    cur_y += core_h;

    // yi（宜）：第四行，绿色强调
    debug_info_yi_label_ = lv_label_create(debug_info_card_);
    lv_obj_set_style_text_font(debug_info_yi_label_, text_font, 0);
    lv_obj_set_style_text_color(debug_info_yi_label_, lv_color_hex(0x4CAF50), 0);
    lv_obj_set_width(debug_info_yi_label_, text_w);
    lv_obj_set_height(debug_info_yi_label_, row_h);
    lv_obj_set_x(debug_info_yi_label_, text_x);
    lv_obj_set_y(debug_info_yi_label_, cur_y);
    lv_label_set_long_mode(debug_info_yi_label_, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(debug_info_yi_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(debug_info_yi_label_, "");
    cur_y += row_h;

    // ji（忌）：第五行，红色强调
    debug_info_ji_label_ = lv_label_create(debug_info_card_);
    lv_obj_set_style_text_font(debug_info_ji_label_, text_font, 0);
    lv_obj_set_style_text_color(debug_info_ji_label_, lv_color_hex(0xE53935), 0);
    lv_obj_set_width(debug_info_ji_label_, text_w);
    lv_obj_set_height(debug_info_ji_label_, row_h);
    lv_obj_set_x(debug_info_ji_label_, text_x);
    lv_obj_set_y(debug_info_ji_label_, cur_y);
    lv_label_set_long_mode(debug_info_ji_label_, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(debug_info_ji_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(debug_info_ji_label_, "");
    cur_y += row_h;

    // detail（通用详情备用）：保持兼容，置于最底部（隐藏）
    debug_info_detail_ = lv_label_create(debug_info_card_);
    lv_obj_set_style_text_font(debug_info_detail_, text_font, 0);
    lv_obj_set_style_text_color(debug_info_detail_, DEBUG_INFO_DETAIL_COLOR, 0);
    lv_obj_set_width(debug_info_detail_, text_w);
    lv_obj_set_height(debug_info_detail_, row_h);
    lv_obj_set_x(debug_info_detail_, text_x);
    lv_obj_set_y(debug_info_detail_, cur_y);
    lv_label_set_long_mode(debug_info_detail_, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(debug_info_detail_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(debug_info_detail_, "");
    lv_obj_add_flag(debug_info_detail_, LV_OBJ_FLAG_HIDDEN);

    ApplyDebugInfoCardLayout();

    debug_info_hide_timer_ = lv_timer_create(OnDebugInfoHideTimer, DEBUG_INFO_SHOW_MS, this);
    lv_timer_set_repeat_count(debug_info_hide_timer_, 1);

    ESP_LOGD(TAG, "Debug info card created");
}

void AttitudeDisplay::EnsureFortunePromptTitle()
{
    if (fortune_prompt_title_ != nullptr) {
        return;
    }
    lv_obj_t* screen = lv_screen_active();
    if (screen == nullptr) {
        return;
    }
    auto lvgl_theme = static_cast<LvglTheme*>(GetTheme());
    const lv_font_t* text_font = (lvgl_theme != nullptr && lvgl_theme->text_font() != nullptr)
        ? lvgl_theme->text_font()->font() : &BUILTIN_TEXT_FONT;
    const int text_w = FORTUNE_CARD_W - 24;

    fortune_prompt_title_ = lv_label_create(screen);
    lv_obj_set_style_text_font(fortune_prompt_title_, text_font, 0);
    lv_obj_set_style_text_color(fortune_prompt_title_, DEBUG_INFO_TITLE_COLOR, 0);
    lv_obj_set_style_text_opa(fortune_prompt_title_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(fortune_prompt_title_, LV_OPA_TRANSP, 0);
    lv_obj_set_width(fortune_prompt_title_, text_w);
    lv_obj_set_height(fortune_prompt_title_, 40);  // 固定40px确保中文字体完整显示
    lv_label_set_long_mode(fortune_prompt_title_, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(fortune_prompt_title_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_clear_flag(fortune_prompt_title_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(fortune_prompt_title_, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(fortune_prompt_title_, "");
}

void AttitudeDisplay::HideFortunePromptTitle()
{
    if (fortune_prompt_title_ != nullptr) {
        lv_obj_add_flag(fortune_prompt_title_, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(fortune_prompt_title_, "");
    }
}

void AttitudeDisplay::HideDebugInfoCardLabels()
{
    if (debug_info_title_ != nullptr) {
        lv_obj_add_flag(debug_info_title_, LV_OBJ_FLAG_HIDDEN);
    }
    if (debug_info_detail_ != nullptr) {
        lv_obj_add_flag(debug_info_detail_, LV_OBJ_FLAG_HIDDEN);
    }
    if (debug_info_gua_label_ != nullptr) {
        lv_obj_add_flag(debug_info_gua_label_, LV_OBJ_FLAG_HIDDEN);
    }
    if (debug_info_core_label_ != nullptr) {
        lv_obj_add_flag(debug_info_core_label_, LV_OBJ_FLAG_HIDDEN);
    }
    if (debug_info_yi_label_ != nullptr) {
        lv_obj_add_flag(debug_info_yi_label_, LV_OBJ_FLAG_HIDDEN);
    }
    if (debug_info_ji_label_ != nullptr) {
        lv_obj_add_flag(debug_info_ji_label_, LV_OBJ_FLAG_HIDDEN);
    }
}

void AttitudeDisplay::ApplyDebugInfoCardLayout()
{
    if (debug_info_card_ == nullptr) {
        return;
    }
    auto lvgl_theme = static_cast<LvglTheme*>(GetTheme());
    const lv_font_t* text_font = (lvgl_theme != nullptr && lvgl_theme->text_font() != nullptr)
        ? lvgl_theme->text_font()->font() : &BUILTIN_TEXT_FONT;
    const int card_w = DEBUG_INFO_CARD_W;
    const int text_w = card_w - 40;
    const int text_x = 20;
    const int row_h = 40;
    const int core_h = 60;

    // 计算各行 y 坐标（与 CreateDebugInfoCard 保持一致）
    const int start_y = 40;
    const int y_title = start_y;
    const int y_gua   = start_y + row_h;
    const int y_core  = start_y + row_h * 2;
    const int y_yi    = start_y + row_h * 2 + core_h;
    const int y_ji    = start_y + row_h * 2 + core_h + row_h;

    if (debug_info_card_ != nullptr) {
        lv_obj_set_style_clip_corner(debug_info_card_, true, 0);
    }
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
    if (debug_info_gua_label_ != nullptr) {
        lv_obj_set_style_text_font(debug_info_gua_label_, text_font, 0);
        lv_obj_set_style_text_color(debug_info_gua_label_, DEBUG_INFO_DETAIL_COLOR, 0);
        lv_obj_set_width(debug_info_gua_label_, text_w);
        lv_obj_set_height(debug_info_gua_label_, row_h);
        lv_obj_set_x(debug_info_gua_label_, text_x);
        lv_obj_set_y(debug_info_gua_label_, y_gua);
        lv_label_set_long_mode(debug_info_gua_label_, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_align(debug_info_gua_label_, LV_TEXT_ALIGN_CENTER, 0);
    }
    if (debug_info_core_label_ != nullptr) {
        lv_obj_set_style_text_font(debug_info_core_label_, text_font, 0);
        lv_obj_set_style_text_color(debug_info_core_label_, DEBUG_INFO_TITLE_COLOR, 0);
        lv_obj_set_width(debug_info_core_label_, text_w);
        lv_obj_set_height(debug_info_core_label_, core_h);
        lv_obj_set_x(debug_info_core_label_, text_x);
        lv_obj_set_y(debug_info_core_label_, y_core);
        lv_label_set_long_mode(debug_info_core_label_, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(debug_info_core_label_, LV_TEXT_ALIGN_CENTER, 0);
    }
    if (debug_info_yi_label_ != nullptr) {
        lv_obj_set_style_text_font(debug_info_yi_label_, text_font, 0);
        lv_obj_set_style_text_color(debug_info_yi_label_, lv_color_hex(0x4CAF50), 0);
        lv_obj_set_width(debug_info_yi_label_, text_w);
        lv_obj_set_height(debug_info_yi_label_, row_h);
        lv_obj_set_x(debug_info_yi_label_, text_x);
        lv_obj_set_y(debug_info_yi_label_, y_yi);
        lv_label_set_long_mode(debug_info_yi_label_, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_align(debug_info_yi_label_, LV_TEXT_ALIGN_CENTER, 0);
    }
    if (debug_info_ji_label_ != nullptr) {
        lv_obj_set_style_text_font(debug_info_ji_label_, text_font, 0);
        lv_obj_set_style_text_color(debug_info_ji_label_, lv_color_hex(0xE53935), 0);
        lv_obj_set_width(debug_info_ji_label_, text_w);
        lv_obj_set_height(debug_info_ji_label_, row_h);
        lv_obj_set_x(debug_info_ji_label_, text_x);
        lv_obj_set_y(debug_info_ji_label_, y_ji);
        lv_label_set_long_mode(debug_info_ji_label_, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_align(debug_info_ji_label_, LV_TEXT_ALIGN_CENTER, 0);
    }
}

void AttitudeDisplay::DestroyDebugInfoCard()
{
    if (debug_info_hide_timer_ != nullptr) {
        lv_timer_delete(debug_info_hide_timer_);
        debug_info_hide_timer_ = nullptr;
    }
    HideFortunePromptTitle();
    if (fortune_prompt_title_ != nullptr) {
        lv_obj_del(fortune_prompt_title_);
        fortune_prompt_title_ = nullptr;
    }
    if (debug_info_card_ != nullptr) {
        lv_obj_del(debug_info_card_);
        debug_info_card_ = nullptr;
    }
    debug_info_title_ = nullptr;
    debug_info_detail_ = nullptr;
    debug_info_gua_label_ = nullptr;
    debug_info_core_label_ = nullptr;
    debug_info_yi_label_ = nullptr;
    debug_info_ji_label_ = nullptr;
}

void AttitudeDisplay::PresentDebugInfoCardUnlocked(const std::string& title,
                                                    const std::string& detail,
                                                    uint32_t hold_ms,
                                                    const DebugInfoPresentOpts& opts)
{
    CreateDebugInfoCard();
    if (debug_info_card_ == nullptr || debug_info_title_ == nullptr || debug_info_detail_ == nullptr) {
        ESP_LOGW(TAG, "PresentDebugInfoCard: widgets missing");
        return;
    }

    auto lvgl_theme = static_cast<LvglTheme*>(GetTheme());
    const lv_font_t* text_font = (lvgl_theme != nullptr && lvgl_theme->text_font() != nullptr)
        ? lvgl_theme->text_font()->font() : &BUILTIN_TEXT_FONT;
    const bool builtin_font = (text_font == &BUILTIN_TEXT_FONT);

    if (opts.screen_title_overlay) {
        ApplyDebugInfoCardLayout();
        HideDebugInfoCardLabels();
        EnsureFortunePromptTitle();
        if (fortune_prompt_title_ == nullptr) {
            ESP_LOGW(TAG, "PresentDebugInfoCard: screen title missing");
            return;
        }
        debug_info_fortune_title_ = title;
        lv_obj_set_style_text_font(fortune_prompt_title_, text_font, 0);
        lv_label_set_text(fortune_prompt_title_, debug_info_fortune_title_.c_str());
        lv_obj_align(fortune_prompt_title_, LV_ALIGN_CENTER, 0, 0);
        lv_obj_remove_flag(fortune_prompt_title_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(fortune_prompt_title_);
    } else {
        HideFortunePromptTitle();
        ApplyDebugInfoCardLayout();
        lv_label_set_text(debug_info_title_, title.c_str());
        lv_label_set_text(debug_info_detail_, detail.c_str());
        lv_obj_remove_flag(debug_info_title_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(debug_info_detail_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(debug_info_title_);
        lv_obj_move_foreground(debug_info_detail_);
        lv_obj_move_foreground(debug_info_card_);
        if (debug_info_gua_label_ != nullptr) {
            lv_obj_add_flag(debug_info_gua_label_, LV_OBJ_FLAG_HIDDEN);
        }
        if (debug_info_core_label_ != nullptr) {
            lv_obj_add_flag(debug_info_core_label_, LV_OBJ_FLAG_HIDDEN);
        }
        if (debug_info_yi_label_ != nullptr) {
            lv_obj_add_flag(debug_info_yi_label_, LV_OBJ_FLAG_HIDDEN);
        }
        if (debug_info_ji_label_ != nullptr) {
            lv_obj_add_flag(debug_info_ji_label_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    lv_obj_remove_flag(debug_info_card_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(debug_info_card_);
    if (opts.screen_title_overlay && fortune_prompt_title_ != nullptr) {
        lv_obj_move_foreground(fortune_prompt_title_);
    }
    lv_obj_update_layout(debug_info_card_);
    if (display_ != nullptr) {
        lv_refr_now(display_);
    }

    // 调试日志：确认标题可见性
    ESP_LOGI(TAG, "DebugInfoCard shown: title=%s hidden=%d card_hidden=%d",
             title.c_str(),
             lv_obj_has_flag(debug_info_title_, LV_OBJ_FLAG_HIDDEN) ? 1 : 0,
             lv_obj_has_flag(debug_info_card_, LV_OBJ_FLAG_HIDDEN) ? 1 : 0);

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

    if (opts.screen_title_overlay) {
        ESP_LOGI(TAG, "Fortune feature card: %s screen_title=%dx%d@%d,%d builtin_font=%d font=%p card_hidden=%d",
                 title.c_str(),
                 fortune_prompt_title_ != nullptr ? lv_obj_get_width(fortune_prompt_title_) : 0,
                 fortune_prompt_title_ != nullptr ? lv_obj_get_height(fortune_prompt_title_) : 0,
                 fortune_prompt_title_ != nullptr ? lv_obj_get_x(fortune_prompt_title_) : 0,
                 fortune_prompt_title_ != nullptr ? lv_obj_get_y(fortune_prompt_title_) : 0,
                 builtin_font ? 1 : 0,
                 text_font,
                 lv_obj_has_flag(debug_info_card_, LV_OBJ_FLAG_HIDDEN) ? 1 : 0);
    } else if (opts.persistent) {
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
    if (self->debug_info_is_fortune_feature_) {
        return;
    }
    DisplayLockGuard lock(self);
    self->HideDebugInfoUnlocked();
}

void AttitudeDisplay::ShowDebugInfo(const std::string& title, const std::string& detail, uint32_t hold_ms)
{
    DisplayLockGuard lock(this);

    // 与运势卡互斥：运势正在显示时跳过，避免遮挡用户功能
    if (fortune_state_ != FortuneState::Idle) {
        ESP_LOGD(TAG, "ShowDebugInfo skipped (fortune busy): %s", title.c_str());
        return;
    }
    if (fortune_menu_selection_active_ && !fortune_feature_card_suppressed_) {
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
    if (debug_info_card_ == nullptr || debug_info_title_ == nullptr || debug_info_detail_ == nullptr) {
        return;
    }

    debug_info_is_fortune_feature_ = false;
    DebugInfoPresentOpts opts;
    PresentDebugInfoCardUnlocked(title, detail, hold_ms, opts);

    debug_info_last_title_ = title;
    debug_info_last_show_ms_ = now;
}

void AttitudeDisplay::HideDebugInfoUnlocked()
{
    const bool restore_fortune = fortune_menu_selection_active_
        && !fortune_feature_card_suppressed_
        && fortune_state_ == FortuneState::Idle;
    const int restore_index = fortune_menu_selected_index_;

    debug_info_is_fortune_feature_ = false;
    HideFortunePromptTitle();
    if (debug_info_card_ != nullptr) {
        lv_obj_add_flag(debug_info_card_, LV_OBJ_FLAG_HIDDEN);
    }

    if (restore_fortune) {
        ShowFortuneMenuFeatureCardUnlocked(restore_index);
    }
}

void AttitudeDisplay::HideDebugInfo()
{
    DisplayLockGuard lock(this);
    HideDebugInfoUnlocked();
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
#if STUDY_SUB_FEATURES_ENABLED
    if (study_sub_state_ == StudySubState::FocusRunning
        || study_sub_state_ == StudySubState::CompleteBgm) {
        return true;
    }
#endif

    if (!fortune_menu_selection_active_) {
        SelectFortuneMenuItemUnlocked(0);
        ESP_LOGI(TAG, "Boot: selection on, default today (index 0)");
        return true;
    }

#if STUDY_SUB_FEATURES_ENABLED
    if (fortune_menu_selected_index_ == static_cast<int>(FortuneMenuType::Study)
        && study_sub_state_ == StudySubState::Menu) {
        StartStudyFocusTimer();
        return true;
    }
#endif

    CycleFortuneMenuSelectionUnlocked();
    return true;
}

bool AttitudeDisplay::HandleFortuneBootLongPress()
{
    DisplayLockGuard lock(this);

    if (fortune_state_ != FortuneState::Idle) {
        return true;
    }
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

    ESP_LOGI(TAG, "Boot long press: trigger fortune menu %d",
             fortune_menu_selected_index_);
    ShowFortuneFromMenu(static_cast<FortuneMenuType>(fortune_menu_selected_index_));
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
