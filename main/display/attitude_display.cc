#include "attitude_display.h"
#include "lvgl_theme.h"
#include "lvgl_image.h"
#include "gif/gif_image_loader.h"
#include "gif/gif_preview_player.h"
#include "image_preview_view.h"
#include "application.h"
#include "assets.h"
#include "assets/lang_config.h"
#include "board.h"
#include "compass_taiji.h"
#include "fortune_watchface_view.h"
#include <esp_lvgl_port.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
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
LV_FONT_DECLARE(font_puhui_20_4);

namespace {

struct AttitudeColors {
    lv_color_t bg_outer = COLOR_BG_OUTER;
    lv_color_t bg_center = COLOR_BG_CENTER;
    lv_color_t text_main = COLOR_TEXT_MAIN;
    lv_color_t text_sub = COLOR_TEXT_SUB;
    lv_color_t text_high = COLOR_TEXT_HIGH;
    lv_color_t border_line = COLOR_BORDER_LINE;
    lv_color_t state_heavy = COLOR_STATE_HEAVY;
    lv_color_t state_danger = COLOR_STATE_DANGER;
};

static const AttitudeColors& GetAttitudeColors()
{
    static const AttitudeColors colors;
    return colors;
}

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

AttitudeDisplay::~AttitudeDisplay()
{
    DestroyFisheyeResources();
    ESP_LOGI(TAG, "AttitudeDisplay destroyed");
}

void AttitudeDisplay::DestroyFisheyeResources()
{
    // 原始样式系统：wifi_fisheye_ 和 ble_fisheye_ 由 LVGL 自动管理
    ESP_LOGD(TAG, "Fisheye resources destroyed");
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

    auto lvgl_theme = static_cast<LvglTheme*>(GetTheme());
    if (lvgl_theme == nullptr) {
        ESP_LOGE(TAG, "Theme is null!");
        return;
    }

    const auto& c = GetAttitudeColors();

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

    UpdateWifiFisheye(WifiStatus::DISCONNECTED);
    UpdateBleFisheye(BleStatus::DISABLED);
    CompassTaiji::StartAutoRotation(TAIJI_ROTATION_PERIOD_NORMAL_MS);
    ESP_LOGI(TAG, "Taiji auto rotation started (period=%dms, fisheyes co-rotate)",
             TAIJI_ROTATION_PERIOD_NORMAL_MS);

    CreateFortuneMenuRing();
    CreateLayer4Boundary();
    CreateFortuneMenuRingTouch();
    CreateTaijiDivinationTouch();
    CreateDivinationHintLabel();
    // 方位圆点已移除（v1.2+ 视觉简化，运势高亮见迭代 2 再定）

    // 首帧全屏铺深色底，避免 SPI 分块刷新露出开机白底
    lv_obj_invalidate(attitude_container_);
    if (display_ != nullptr) {
        lv_refr_now(display_);
    }

    ESP_LOGI(TAG, "SetupUI completed (taiji+fisheye 90%%, fortune menu ring, L4 outer ring only)");
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
    const auto& c = GetAttitudeColors();

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
    lv_obj_set_style_bg_color(bg_layer_center_, c.bg_center, 0);
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

bool AttitudeDisplay::IsJarvisWatchfaceVisible() const
{
    // T10: 与 IsJarvisHudActive 共享一个底层标志
    return fortune_watchface_visible_;
}

bool AttitudeDisplay::IsJarvisHudActive() const
{
    // T10: 与 IsJarvisWatchfaceVisible 等价 — 统一入口
    return IsJarvisWatchfaceVisible();
}

void AttitudeDisplay::RouteToJarvisStatusBar(const std::string& text)
{
    if (text.empty()) {
        return;
    }
    // T03: 黄金原则兜底 — 链路 A 摇一摇期间 JARVIS 视图不在场时，
    // 任何 AI 返回内容都必须强制确保 JARVIS 视图在场，再写语音气泡，
    // 避免「文字无显示 + 用户看不到 AI 回应」的问题。
    if (!IsJarvisHudActive()) {
        ESP_LOGW(TAG, "RouteToJarvisStatusBar: JARVIS not active, force showing it for text=%.40s%s",
                 text.c_str(), (text.size() > 40 ? "..." : ""));
        ShowJarvisWatchface();
    }
    FortuneWatchfaceView::GetInstance().SetVoiceMessage(text.c_str());
}

void AttitudeDisplay::SuppressDebugInfoCardForJarvisUnlocked()
{
    ClearDebugInfoQueueUnlocked();
}

void AttitudeDisplay::SetTheme(Theme* theme)
{
    Display::SetTheme(theme);
}

// ============================================================================
// 重写基类 UI 显示方法
// 背景：AttitudeDisplay::SetupUI() 完全重写了父类 UI 初始化流程，没有调用
//       LcdDisplay::SetupUI()，因此父类的 notification_label_/status_label_/
//       chat_message_label_/emoji_image_ 都是 nullptr。当 application.cc 调用
//       这些方法时，会触发 "label is nullptr" 警告且无任何 UI 反馈。
// 重写策略：使用 AttitudeDisplay 自带的 DebugInfoCard 来显示通知/状态信息。
//           SetEmotion/ClearChatMessages 不影响 AttitudeDisplay 的太极+鱼眼
//           视觉，直接 no-op。
// ============================================================================

void AttitudeDisplay::ShowNotification(const char* notification, int duration_ms)
{
    if (notification == nullptr || notification[0] == '\0') {
        return;
    }
    ESP_LOGD(TAG, "ShowNotification: %s (duration=%dms)", notification, duration_ms);

    // JARVIS 视图可见时，通知走状态栏，避免 function_area_card_ 在后台屏幕弹出
    if (IsJarvisHudActive()) {
        RouteToJarvisStatusBar(std::string("通知:") + notification);
        return;
    }

    // 将时长限制在合理区间，避免动画任务异常
    uint32_t hold_ms = (duration_ms > 0) ? static_cast<uint32_t>(duration_ms) : DEBUG_INFO_SHOW_MS;
    if (hold_ms < 500) hold_ms = 500;
    if (hold_ms > DEBUG_INFO_HOLD_MAX_MS) hold_ms = DEBUG_INFO_HOLD_MAX_MS;
    
    // 通知消息音效已暂时禁用：避免出错情况下频繁音效通知
    // Application::GetInstance().PlayUiSound(Lang::Sounds::OGG_NOTIFICATION);

    // 加 LVGL 互斥锁：SdCardReportTask 等非 LVGL 任务也会调用 ShowNotification
    // 没有锁的话会在 lv_refr_now 阶段触发 LoadProhibited
    DisplayLockGuard lock(this);
    ShowDebugInfo("通知", std::string(notification), hold_ms);
}

void AttitudeDisplay::ShowNotification(const std::string& notification, int duration_ms)
{
    if (notification.empty()) {
        return;
    }
    ShowNotification(notification.c_str(), duration_ms);
}

void AttitudeDisplay::SetStatus(const char* status)
{
    if (status == nullptr || status[0] == '\0') {
        return;
    }
    ESP_LOGD(TAG, "SetStatus: %s", status);

    // JARVIS 视图可见时，直接更新其状态栏
    if (IsJarvisHudActive()) {
        FortuneWatchfaceView::GetInstance().SetStatusText(status);
        return;
    }

    // 否则显示在罗盘的调试信息卡
    DisplayLockGuard lock(this);
    ShowDebugInfo("状态", std::string(status), 5000);
}

void AttitudeDisplay::SetEmotion(const char* emotion)
{
    if (emotion == nullptr || emotion[0] == '\0') {
        return;
    }
    // AttitudeDisplay 使用太极+鱼眼表达状态，不依赖 LcdDisplay 的表情控件。
    // 这里仅记录日志，保持与 application.cc 状态机同步。
    ESP_LOGD(TAG, "SetEmotion: %s (no-op, taiji+fisheye unchanged)", emotion);
}

void AttitudeDisplay::SetChatMessage(const char* role, const char* content)
{
    if (content == nullptr || content[0] == '\0') {
        return;
    }
    if (role == nullptr) {
        role = "system";
    }
    ESP_LOGD(TAG, "SetChatMessage: role=%s content=%.40s%s",
             role, content, (strlen(content) > 40 ? "..." : ""));

    // JARVIS 视图可见时，所有消息都路由到状态栏显示（支持滚动）
    if (IsJarvisHudActive()) {
        std::string prefixed;
        // T06: role 扩展 — 支持 "tool" 角色（来自 MCP tool_call 返回）
        if (strcmp(role, "assistant") == 0) {
            prefixed = std::string("#AI:") + content;
        } else if (strcmp(role, "user") == 0) {
            prefixed = std::string("#你:") + content;
        } else if (strcmp(role, "tool") == 0 || strcmp(role, "system") == 0) {
            prefixed = std::string("#系统:") + content;
        } else {
            prefixed = std::string("#系统:") + content;
        }
        RouteToJarvisStatusBar(prefixed);
        return;
    }

    // 仅对 system 消息使用 DebugInfoCard 提示，普通对话由 attitude UI 自行表达
    if (strcmp(role, "system") == 0) {
        DisplayLockGuard lock(this);
        ShowDebugInfo("系统消息", std::string(content), 5000);
    }
}

void AttitudeDisplay::ClearChatMessages()
{
    // AttitudeDisplay 没有聊天消息气泡，无需清理
    ESP_LOGD(TAG, "ClearChatMessages (no-op, attitude ui has no message bubbles)");
}

// 功能：显示外部图片（PNG / JPG / GIF），走独立 ImagePreviewView
void AttitudeDisplay::EnterImagePreviewViewUnlocked() {
    if (image_preview_active_) {
        return;
    }
    if (!view_stack_.contains(ActiveView::ImagePreview)) {
        view_stack_.push(ActiveView::ImagePreview);
    }
    ImagePreviewView::GetInstance().Show();
    image_preview_active_ = true;
    ESP_LOGI(TAG, "EnterImagePreviewView: current=%d",
             static_cast<int>(view_stack_.current()));
}

void AttitudeDisplay::ExitImagePreviewViewUnlocked() {
    if (!image_preview_active_) {
        return;
    }
    ImagePreviewView::GetInstance().Hide();
    image_preview_active_ = false;
    view_stack_.pop_if_top(ActiveView::ImagePreview);
    ESP_LOGI(TAG, "ExitImagePreviewView: restored current=%d",
             static_cast<int>(view_stack_.current()));
}

GifPreviewTarget AttitudeDisplay::BuildImagePreviewTarget(const LvglImage* image) {
    auto& view = ImagePreviewView::GetInstance();
    GifPreviewTarget target;
    target.gif_widget = view.GetGifWidget();
    target.static_widget = view.GetStaticWidget();
    if (image != nullptr) {
        const lv_img_dsc_t* img_dsc = image->image_dsc();
        if (img_dsc != nullptr && img_dsc->header.w > 0) {
            target.static_image_scale =
                128 * ImagePreviewView::kCardSize / img_dsc->header.w;
        }
    }
    target.on_before_show = [this]() { EnterImagePreviewViewUnlocked(); };
    target.on_after_hide = [this]() { ExitImagePreviewViewUnlocked(); };
    return target;
}

void AttitudeDisplay::ShowImageOnActiveViewUnlocked(std::unique_ptr<LvglImage> image,
                                                    uint32_t timeout_ms, bool loop) {
    if (image == nullptr) {
        GifPreviewPlayer::GetInstance().Hide();
        return;
    }

    if (timeout_ms == 0) {
        timeout_ms = 5000;
    }

    const LvglImage* preview = image.get();
    GifPreviewTarget target = BuildImagePreviewTarget(preview);
    if (target.gif_widget == nullptr) {
        ESP_LOGE(TAG, "ShowImageOnActiveViewUnlocked: ImagePreviewView not ready");
        return;
    }

    if (!GifPreviewPlayer::GetInstance().Show(std::move(image), target, timeout_ms, loop)) {
        ESP_LOGE(TAG, "ShowImageOnActiveViewUnlocked: GifPreviewPlayer::Show failed");
    }
}

void AttitudeDisplay::HideImagePreview() {
    DisplayLockGuard lock(this);
    GifPreviewPlayer::GetInstance().Hide();
}

void AttitudeDisplay::SetPreviewImage(std::unique_ptr<LvglImage> image, uint32_t timeout_ms)
{
    if (!lvgl_port_lock(300)) {
        ESP_LOGW(TAG, "SetPreviewImage: LVGL lock timeout, skipping");
        return;
    }

    if (image == nullptr) {
        GifPreviewPlayer::GetInstance().Hide();
        lvgl_port_unlock();
        return;
    }

    ShowImageOnActiveViewUnlocked(std::move(image), timeout_ms, true);
    lvgl_port_unlock();
}

void AttitudeDisplay::SetPreviewImageUnlocked(std::unique_ptr<LvglImage> image, uint32_t timeout_ms)
{
    if (image == nullptr) {
        GifPreviewPlayer::GetInstance().Hide();
        return;
    }

    ShowImageOnActiveViewUnlocked(std::move(image), timeout_ms, true);
}

void AttitudeDisplay::SetPreviewGif(const char* file_path, bool loop, uint32_t timeout_ms)
{
    auto image = GifImageLoader::LoadFromFile(file_path);
    if (!lvgl_port_lock(portMAX_DELAY)) {
        return;
    }

    if (image == nullptr) {
        GifPreviewPlayer::GetInstance().Hide();
        lvgl_port_unlock();
        return;
    }

    ShowImageOnActiveViewUnlocked(std::move(image), timeout_ms, loop);
    lvgl_port_unlock();
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

void AttitudeDisplay::CreateLayer4Boundary()
{
    const auto& c = GetAttitudeColors();
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
    const auto& c = GetAttitudeColors();
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

    fortune_menu_selected_index_ = -1;
    fortune_menu_selection_active_ = false;
    UpdateFortuneMenuSelection();

    ESP_LOGI(TAG, "Fortune menu ring: %d icons (~%dpx), r=%d, touch %d~%d",
             FORTUNE_MENU_COUNT, FORTUNE_MENU_ICON_GLYPH_PX,
             FORTUNE_MENU_RING_RADIUS, FORTUNE_MENU_TOUCH_INNER_R,
             FORTUNE_MENU_TOUCH_OUTER_R);
}

static void OnFortuneMenuRingTouched(lv_event_t* e)
{
    auto* self = static_cast<AttitudeDisplay*>(lv_event_get_user_data(e));
    if (self == nullptr) return;
    if (self->IsFortuneDivinationBusy()) return;

    lv_indev_t* indev = lv_indev_get_act();
    if (indev == nullptr) return;

    lv_point_t pt;
    lv_indev_get_point(indev, &pt);

    const int dx = pt.x - ATTITUDE_CENTER_X;
    const int dy = pt.y - ATTITUDE_CENTER_Y;

    const int r = static_cast<int>(sqrt(dx * dx + dy * dy));
    if (r < FORTUNE_MENU_TOUCH_INNER_R || r > FORTUNE_MENU_TOUCH_OUTER_R) {
        return;
    }

    double angle = atan2(dy, dx) * 180.0 / M_PI;
    angle -= FORTUNE_MENU_START_ANGLE_DEG;
    if (angle < 0) angle += 360.0;

    const double step = 360.0 / FORTUNE_MENU_COUNT;
    int index = static_cast<int>(angle / step) % FORTUNE_MENU_COUNT;

    self->SelectFortuneMenuItem(index);
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
    lv_obj_add_event_cb(fortune_menu_ring_touch_, OnFortuneMenuRingTouched, LV_EVENT_CLICKED, this);

    for (int i = 0; i < FORTUNE_MENU_COUNT; ++i) {
        if (fortune_menu_labels_[i] != nullptr) {
            lv_obj_move_foreground(fortune_menu_labels_[i]);
        }
    }
    lv_obj_move_foreground(fortune_menu_ring_touch_);

    ESP_LOGI(TAG, "Fortune menu ring touch layer ready (annulus %d~%d)",
             FORTUNE_MENU_TOUCH_INNER_R, FORTUNE_MENU_TOUCH_OUTER_R);
}

namespace {

bool IsPointInTaijiCenter(int x, int y)
{
    const int dx = x - ATTITUDE_CENTER_X;
    const int dy = y - ATTITUDE_CENTER_Y;
    return (dx * dx + dy * dy) <= (TAIJI_RADIUS * TAIJI_RADIUS);
}

}  // namespace

void AttitudeDisplay::CreateTaijiDivinationTouch()
{
    taiji_divination_touch_ = lv_obj_create(attitude_container_);
    lv_obj_set_size(taiji_divination_touch_, TAIJI_CANVAS_SIZE, TAIJI_CANVAS_SIZE);
    lv_obj_set_pos(taiji_divination_touch_,
                   ATTITUDE_CENTER_X - TAIJI_RADIUS,
                   ATTITUDE_CENTER_Y - TAIJI_RADIUS);
    lv_obj_set_style_radius(taiji_divination_touch_, TAIJI_RADIUS, 0);
    lv_obj_set_style_bg_opa(taiji_divination_touch_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(taiji_divination_touch_, 0, 0);
    lv_obj_set_style_pad_all(taiji_divination_touch_, 0, 0);
    lv_obj_add_flag(taiji_divination_touch_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(taiji_divination_touch_, OnTaijiDivinationPressed, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(taiji_divination_touch_, OnTaijiDivinationReleased, LV_EVENT_RELEASED, this);
    lv_obj_add_event_cb(taiji_divination_touch_, OnTaijiDivinationReleased, LV_EVENT_PRESS_LOST, this);
    if (fortune_menu_ring_touch_ != nullptr) {
        lv_obj_move_foreground(fortune_menu_ring_touch_);
    }
    lv_obj_move_foreground(taiji_divination_touch_);
    ESP_LOGI(TAG, "Taiji divination touch ready (%dx%d, hold %dms)",
             TAIJI_CANVAS_SIZE, TAIJI_CANVAS_SIZE, FORTUNE_DIVINATION_HOLD_MS);
}

void AttitudeDisplay::CreateDivinationHintLabel()
{
    divination_hint_label_ = lv_label_create(attitude_container_);
    lv_obj_set_style_text_font(divination_hint_label_, &font_puhui_20_4, 0);
    lv_obj_set_style_text_color(divination_hint_label_, COLOR_TEXT_MAIN, 0);
    lv_obj_set_style_bg_opa(divination_hint_label_, LV_OPA_TRANSP, 0);
    lv_label_set_text(divination_hint_label_, "");
    lv_label_set_long_mode(divination_hint_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(divination_hint_label_, TAIJI_RADIUS * 2 - 20);
    lv_obj_set_style_text_align(divination_hint_label_, LV_TEXT_ALIGN_CENTER, 0);
    // 垂直居中于太极略微偏上的位置
    lv_obj_align(divination_hint_label_, LV_ALIGN_CENTER, 0, -12);
    lv_obj_add_flag(divination_hint_label_, LV_OBJ_FLAG_HIDDEN);
}

void AttitudeDisplay::ShowDivinationHintUnlocked(const char* text)
{
    if (divination_hint_label_ == nullptr) return;
    lv_label_set_text(divination_hint_label_, text);
    lv_obj_remove_flag(divination_hint_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(divination_hint_label_);
}

void AttitudeDisplay::HideDivinationHintUnlocked()
{
    if (divination_hint_label_ != nullptr) {
        lv_obj_add_flag(divination_hint_label_, LV_OBJ_FLAG_HIDDEN);
    }
}

void AttitudeDisplay::CreateTaijiPressOverlayUnlocked()
{
    if (taiji_press_overlay_ != nullptr) {
        return;
    }
    taiji_press_overlay_ = lv_obj_create(attitude_container_);
    lv_obj_set_size(taiji_press_overlay_, TAIJI_CANVAS_SIZE, TAIJI_CANVAS_SIZE);
    lv_obj_set_pos(taiji_press_overlay_,
                   ATTITUDE_CENTER_X - TAIJI_RADIUS,
                   ATTITUDE_CENTER_Y - TAIJI_RADIUS);
    lv_obj_set_style_radius(taiji_press_overlay_, TAIJI_RADIUS, 0);
    lv_obj_set_style_clip_corner(taiji_press_overlay_, true, 0);
    lv_obj_set_style_bg_color(taiji_press_overlay_, lv_color_hex(0x0A1414), 0);
    lv_obj_set_style_bg_opa(taiji_press_overlay_, LV_OPA_80, 0);
    lv_obj_set_style_border_color(taiji_press_overlay_, DEBUG_INFO_BORDER_COLOR, 0);
    lv_obj_set_style_border_width(taiji_press_overlay_, 2, 0);
    lv_obj_set_style_pad_all(taiji_press_overlay_, 0, 0);
    lv_obj_clear_flag(taiji_press_overlay_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(taiji_press_overlay_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(taiji_press_overlay_);
}

void AttitudeDisplay::ShowTaijiPressOverlayUnlocked()
{
    if (taiji_press_overlay_ == nullptr) {
        CreateTaijiPressOverlayUnlocked();
    }
    if (taiji_press_overlay_ != nullptr) {
        lv_obj_remove_flag(taiji_press_overlay_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(taiji_press_overlay_);
    }
    if (divination_hint_label_ != nullptr) {
        lv_obj_move_foreground(divination_hint_label_);
    }
    if (!taiji_rotation_paused_by_press_) {
        CompassTaiji::SetAutoRotationPaused(true);
        taiji_rotation_paused_by_press_ = true;
    }
    UpdateTaijiGoldRingColor(DEBUG_INFO_BORDER_COLOR);
}

void AttitudeDisplay::HideTaijiPressOverlayUnlocked()
{
    if (taiji_press_overlay_ != nullptr) {
        lv_obj_add_flag(taiji_press_overlay_, LV_OBJ_FLAG_HIDDEN);
    }
    if (taiji_rotation_paused_by_press_) {
        CompassTaiji::SetAutoRotationPaused(false);
        taiji_rotation_paused_by_press_ = false;
    }
    UpdateOuterRingColor();
}

void AttitudeDisplay::PlayFortuneMenuSelectSound()
{
    // 功能图标选中时播放音效已被刻意禁用：
    // 触摸/短按切换选中态时不应有"叮"反馈，避免在用户浏览功能时
    // 频繁打断以及消耗 I2S / Opus 解码路径。
    // 选中态视觉反馈（icon 放大 + 颜色高亮）由 UpdateFortuneMenuItemVisual 提供。
    // 仅个别真正"业务反馈"路径（起卦结果、长按确认）继续播放 OGG_POPUP / OGG_SUCCESS。
}

void AttitudeDisplay::PlayFortuneDivinationMarqueeSound()
{
    Application::GetInstance().PlayUiSound(Lang::Sounds::OGG_ZHANBU);
}

bool AttitudeDisplay::IsFortuneDivinationBusy() const
{
    return fortune_divination_state_ == FortuneDivinationState::Animating;
}

void AttitudeDisplay::StartFortuneDivination()
{
    DisplayLockGuard lock(this);
    if (fortune_divination_state_ == FortuneDivinationState::Animating) {
        ESP_LOGI(TAG, "Fortune divination already running, ignoring request");
        return;
    }
    fortune_divination_from_taiji_ = false;
    StartFortuneDivinationUnlocked();
}

void AttitudeDisplay::StopFortuneDivination()
{
    DisplayLockGuard lock(this);
    StopFortuneDivinationUnlocked();
}

int AttitudeDisplay::GetFortuneDivinationResult() const
{
    if (fortune_divination_state_ != FortuneDivinationState::Result) {
        return -1;
    }
    return fortune_divination_result_;
}

int AttitudeDisplay::GetFortuneDivinationState() const
{
    return static_cast<int>(fortune_divination_state_);
}

void AttitudeDisplay::ResetFortuneMenuIconStyle(int index)
{
    if (index < 0 || index >= FORTUNE_MENU_COUNT || fortune_menu_labels_[index] == nullptr) {
        return;
    }
    lv_obj_t* label = fortune_menu_labels_[index];
    if (fortune_menu_applied_scale_[index] != FORTUNE_MENU_ICON_SCALE) {
        lv_obj_set_style_transform_scale(label, FORTUNE_MENU_ICON_SCALE, 0);
        fortune_menu_applied_scale_[index] = FORTUNE_MENU_ICON_SCALE;
        lv_obj_update_layout(label);
    }
    lv_obj_set_style_text_color(label, COLOR_TEXT_MAIN, 0);
    lv_obj_set_style_shadow_width(label, 0, 0);
    lv_obj_set_style_shadow_spread(label, 0, 0);
    const int cx = fortune_menu_center_x_[index];
    const int cy = fortune_menu_center_y_[index];
    const int w = lv_obj_get_width(label);
    const int h = lv_obj_get_height(label);
    lv_obj_set_pos(label, cx - w / 2, cy - h / 2);
}

void AttitudeDisplay::RandomizeFortuneDivinationMarqueeUnlocked()
{
    bool used[FORTUNE_MENU_COUNT] = {};
    for (int k = 0; k < FORTUNE_DIVINATION_HIGHLIGHT_COUNT; ++k) {
        int idx = 0;
        do {
            idx = static_cast<int>(esp_random() % FORTUNE_MENU_COUNT);
        } while (used[idx]);
        used[idx] = true;
        fortune_divination_active_indices_[k] = idx;
        const uint16_t h = static_cast<uint16_t>(esp_random() % 360);
        const uint8_t s = static_cast<uint8_t>(80 + (esp_random() % 21));
        const uint8_t v = static_cast<uint8_t>(90 + (esp_random() % 11));
        fortune_divination_active_colors_[k] = lv_color_hsv_to_rgb(h, s, v);
    }
}

void AttitudeDisplay::UpdateFortuneDivinationMarqueeVisual(int active_index)
{
    fortune_divination_highlight_ = active_index;
    for (int i = 0; i < FORTUNE_MENU_COUNT; ++i) {
        if (fortune_menu_labels_[i] == nullptr) {
            continue;
        }
        lv_obj_t* label = fortune_menu_labels_[i];
        const int cx = fortune_menu_center_x_[i];
        const int cy = fortune_menu_center_y_[i];

        if (fortune_divination_state_ == FortuneDivinationState::Result) {
            // Result state: active icon is scaled up, but all icons restore to gold
            if (i == active_index) {
                if (fortune_menu_applied_scale_[i] != FORTUNE_MENU_ICON_SCALE_SELECTED) {
                    lv_obj_set_style_transform_scale(label, FORTUNE_MENU_ICON_SCALE_SELECTED, 0);
                    fortune_menu_applied_scale_[i] = FORTUNE_MENU_ICON_SCALE_SELECTED;
                    lv_obj_update_layout(label);
                }
            } else {
                if (fortune_menu_applied_scale_[i] != FORTUNE_MENU_ICON_SCALE) {
                    lv_obj_set_style_transform_scale(label, FORTUNE_MENU_ICON_SCALE, 0);
                    fortune_menu_applied_scale_[i] = FORTUNE_MENU_ICON_SCALE;
                    lv_obj_update_layout(label);
                }
            }
            lv_obj_set_style_text_color(label, COLOR_TEXT_MAIN, 0);
        } else {
            // Animating state: 5 random icons lit with individual random colors
            int color_slot = -1;
            for (int k = 0; k < FORTUNE_DIVINATION_HIGHLIGHT_COUNT; ++k) {
                if (fortune_divination_active_indices_[k] == i) {
                    color_slot = k;
                    break;
                }
            }
            if (color_slot >= 0) {
                if (fortune_menu_applied_scale_[i] != FORTUNE_MENU_ICON_SCALE_SELECTED) {
                    lv_obj_set_style_transform_scale(label, FORTUNE_MENU_ICON_SCALE_SELECTED, 0);
                    fortune_menu_applied_scale_[i] = FORTUNE_MENU_ICON_SCALE_SELECTED;
                    lv_obj_update_layout(label);
                }
                lv_obj_set_style_text_color(label, fortune_divination_active_colors_[color_slot], 0);
            } else {
                if (fortune_menu_applied_scale_[i] != FORTUNE_MENU_ICON_SCALE) {
                    lv_obj_set_style_transform_scale(label, FORTUNE_MENU_ICON_SCALE, 0);
                    fortune_menu_applied_scale_[i] = FORTUNE_MENU_ICON_SCALE;
                    lv_obj_update_layout(label);
                }
                lv_obj_set_style_text_color(label, COLOR_TEXT_MAIN, 0);
            }
        }
        const int w = lv_obj_get_width(label);
        const int h = lv_obj_get_height(label);
        lv_obj_set_pos(label, cx - w / 2, cy - h / 2);
    }
}

void AttitudeDisplay::CancelTaijiHoldTimerUnlocked()
{
    taiji_hold_pending_ = false;
    if (taiji_hold_timer_ != nullptr) {
        lv_timer_delete(taiji_hold_timer_);
        taiji_hold_timer_ = nullptr;
    }
}

void AttitudeDisplay::SetDivinationWaitingForTts(bool waiting) {
    DisplayLockGuard lock(this);
    divination_waiting_for_tts_ = waiting;
}

void AttitudeDisplay::SetDivinationFromShake(bool from_shake) {
    DisplayLockGuard lock(this);
    divination_from_shake_ = from_shake;
}

void AttitudeDisplay::StopMarqueeForTts() {
    DisplayLockGuard lock(this);
    if (fortune_divination_state_ == FortuneDivinationState::Animating) {
        FinishFortuneDivinationUnlocked(fortune_divination_result_);
    }
}

void AttitudeDisplay::ReturnToCompassAfterTts() {
    DisplayLockGuard lock(this);
    if (!divination_from_shake_ && !divination_from_jarvis_) {
        return;
    }

    // T05: 链路 B 必须切回 JARVIS 视图，保持 TTS 后续多轮对话
    // 链路 A 必须回到罗盘主界面
    if (divination_from_jarvis_) {
        // 注意：SwitchBackFromDivination 内部会调 StopFortuneDivination
        // 并重置 divination_from_jarvis_，无需在外部再清
        SwitchBackFromDivination();
        ESP_LOGI(TAG, "ReturnToCompassAfterTts: routed to SwitchBackFromDivination (JARVIS)");
        return;
    }

    // 链路 A: 摇一摇 → 罗盘
    StopFortuneDivinationUnlocked();
    divination_from_shake_ = false;
    divination_waiting_for_tts_ = false;
}

void AttitudeDisplay::StopFortuneDivinationUnlocked()
{
    CancelTaijiHoldTimerUnlocked();
    if (fortune_divination_timer_ != nullptr) {
        lv_timer_delete(fortune_divination_timer_);
        fortune_divination_timer_ = nullptr;
    }
    fortune_divination_state_ = FortuneDivinationState::Idle;
    fortune_divination_start_ms_ = 0;
    fortune_divination_finish_deadline_ms_ = 0;
    fortune_divination_last_tick_index_ = -1;
    fortune_divination_highlight_ = -1;
    fortune_divination_result_ = -1;
    divination_from_shake_ = false;
    divination_waiting_for_tts_ = false;
    for (int k = 0; k < FORTUNE_DIVINATION_HIGHLIGHT_COUNT; ++k) {
        fortune_divination_active_indices_[k] = -1;
    }
    taiji_pressed_during_anim_ = false;
    fortune_divination_sound_playing_ = false;
    Application::GetInstance().StopUiSound();
    Application::GetInstance().AbortSpeaking(kAbortReasonWakeWordDetected);
    for (int i = 0; i < FORTUNE_MENU_COUNT; ++i) {
        ResetFortuneMenuIconStyle(i);
    }
    fortune_menu_selection_active_ = false;
    HideDivinationHintUnlocked();
    HideTaijiPressOverlayUnlocked();
    ClearDebugInfoQueueUnlocked();
}

void AttitudeDisplay::ClearDebugInfoQueueUnlocked()
{
    for (auto& item : debug_info_queue_) {
        if (item.timer != nullptr) {
            lv_timer_del(item.timer);
            item.timer = nullptr;
        }
    }
    debug_info_queue_.clear();
    current_index_ = SIZE_MAX;
    ClearDebugInfoCard();
}

void AttitudeDisplay::StartFortuneDivinationUnlocked()
{
    if (fortune_divination_state_ == FortuneDivinationState::Animating) {
        return;
    }
    CancelTaijiHoldTimerUnlocked();
    if (fortune_divination_timer_ != nullptr) {
        lv_timer_delete(fortune_divination_timer_);
        fortune_divination_timer_ = nullptr;
    }

    DeselectFortuneMenuItemUnlocked();
    fortune_divination_state_ = FortuneDivinationState::Animating;
    fortune_divination_result_ = static_cast<int>(esp_random() % FORTUNE_MENU_COUNT);
    fortune_divination_highlight_ = 0;
    fortune_divination_last_tick_index_ = -1;

    fortune_divination_start_ms_ = lv_tick_get();
    fortune_divination_finish_deadline_ms_ = fortune_divination_start_ms_ + FORTUNE_DIVINATION_DURATION_MS;
    taiji_pressed_during_anim_ = fortune_divination_from_taiji_;

    HideDivinationHintUnlocked(); // Hide the hold hint during marquee if you want, or show "正在感应..."
    // According to plan, we can just hide it during marquee to not block Taiji

    RandomizeFortuneDivinationMarqueeUnlocked();
    UpdateFortuneDivinationMarqueeVisual(0);
    PlayFortuneDivinationMarqueeSound();
    fortune_divination_sound_next_play_ms_ = lv_tick_get() + FORTUNE_DIVINATION_SOUND_INTERVAL_MS;

    fortune_divination_timer_ = lv_timer_create(OnFortuneDivinationTick,
                                                FORTUNE_DIVINATION_TICK_MS, this);
    lv_timer_set_repeat_count(fortune_divination_timer_, -1);

    ESP_LOGI(TAG, "Fortune divination started, result will be index %d",
             fortune_divination_result_);
}

void AttitudeDisplay::FinishFortuneDivinationUnlocked(int result_index)
{
    if (result_index < 0 || result_index >= FORTUNE_MENU_COUNT) {
        return;
    }
    if (fortune_divination_timer_ != nullptr) {
        lv_timer_delete(fortune_divination_timer_);
        fortune_divination_timer_ = nullptr;
    }

    fortune_divination_state_ = FortuneDivinationState::Result;
    fortune_divination_result_ = result_index;
    fortune_menu_selection_active_ = true;
    fortune_menu_selected_index_ = result_index;

    fortune_divination_sound_playing_ = false;
    Application::GetInstance().StopUiSound();
    Application::GetInstance().PlayUiSound(Lang::Sounds::OGG_SUCCESS);

    UpdateFortuneDivinationMarqueeVisual(result_index);
    ShowFortuneFeatureCategoryUnlocked(result_index);
    fortune_divination_last_tick_index_ = result_index;

    ESP_LOGI(TAG, "Fortune divination finished -> %d (%s)",
             result_index, kFortuneMenuDefs[result_index].func_label);

    // T02: 单一职责明确化
    // 链路 B（divination_from_jarvis_=true）：2s 后由 timer 切回 JARVIS。
    //   callback 由 SwitchBackFromDivination 统一触发（mcp_server 端已清空），
    //   此处不再直接调 callback，避免链路 B callback 双触发。
    // 链路 A（divination_from_jarvis_=false）：在跑马灯 finish 时立即调 callback，
    //   因为链路 A 没有 view_stack 切换逻辑。
    if (divination_from_jarvis_) {
        lv_timer_create([](lv_timer_t* timer) {
            auto* self = static_cast<AttitudeDisplay*>(lv_timer_get_user_data(timer));
            if (self != nullptr) {
                self->SwitchBackFromDivination();
            }
            lv_timer_del(timer);
        }, 2000, this);
        // 链路 B 不在此处调 callback，由 SwitchBackFromDivination 内部决定
        return;
    }

    // 链路 A：直接调 callback
    if (divination_callback_ != nullptr) {
        divination_callback_(result_index);
    }
}

void AttitudeDisplay::OnFortuneDivinationTick(lv_timer_t* timer)
{
    auto* self = static_cast<AttitudeDisplay*>(lv_timer_get_user_data(timer));
    if (self == nullptr) {
        return;
    }

    DisplayLockGuard lock(self);
    if (self->fortune_divination_state_ != FortuneDivinationState::Animating) {
        return;
    }

    uint32_t now = lv_tick_get();

    if (now >= self->fortune_divination_sound_next_play_ms_) {
        self->PlayFortuneDivinationMarqueeSound();
        self->fortune_divination_sound_next_play_ms_ = now + FORTUNE_DIVINATION_SOUND_INTERVAL_MS;
    }
    
    if (self->taiji_pressed_during_anim_) {
        // 如果一直按着太极圈，确保 deadline 保持在未来，从而延长动画时间
        uint32_t hold_deadline = now + FORTUNE_DIVINATION_RELEASE_FINISH_MS;
        uint32_t min_deadline = self->fortune_divination_start_ms_ + FORTUNE_DIVINATION_DURATION_MS;
        self->fortune_divination_finish_deadline_ms_ = (hold_deadline > min_deadline) ? hold_deadline : min_deadline;
    }

    if (self->divination_waiting_for_tts_) {
        // 摇一摇触发的占卜，等待后端 TTS 响应。超时兜底。
        // 常量化参见 attitude_display.h: FORTUNE_DIVINATION_DEFERRED_TIMEOUT_MS
        if (now - self->fortune_divination_start_ms_ > FORTUNE_DIVINATION_DEFERRED_TIMEOUT_MS) {
            ESP_LOGW(TAG, "Fortune divination timeout waiting for TTS");
            self->divination_waiting_for_tts_ = false;
            self->StopFortuneDivinationUnlocked();
            self->ShowDebugInfo("占卜超时", "后端未响应", 5000);
            return;
        }
        // 即使在等待 TTS，跑马灯到达 deadline 也应正常结束动画，
        // 以便 get_divination_result 的延迟回调能获取结果
        if (self->fortune_divination_finish_deadline_ms_ != 0 && now >= self->fortune_divination_finish_deadline_ms_) {
            self->FinishFortuneDivinationUnlocked(self->fortune_divination_result_);
            return;
        }
    } else {
        if (self->fortune_divination_finish_deadline_ms_ != 0 && now >= self->fortune_divination_finish_deadline_ms_) {
            self->FinishFortuneDivinationUnlocked(self->fortune_divination_result_);
            return;
        }
    }

    self->RandomizeFortuneDivinationMarqueeUnlocked();
    self->UpdateFortuneDivinationMarqueeVisual(-1);
}

void AttitudeDisplay::OnTaijiHoldTimer(lv_timer_t* timer)
{
    auto* self = static_cast<AttitudeDisplay*>(lv_timer_get_user_data(timer));
    if (self == nullptr) {
        return;
    }

    DisplayLockGuard lock(self);
    self->taiji_hold_timer_ = nullptr;
    // 不重置 taiji_hold_pending_，以判断当前手是否还按着
    if (self->fortune_divination_state_ == FortuneDivinationState::Animating) {
        return;
    }
    self->fortune_divination_from_taiji_ = true;
    self->StartFortuneDivinationUnlocked();
}

void AttitudeDisplay::OnTaijiDivinationPressed(lv_event_t* e)
{
    auto* self = static_cast<AttitudeDisplay*>(lv_event_get_user_data(e));
    if (self == nullptr) {
        return;
    }

    lv_indev_t* indev = lv_indev_get_act();
    if (indev == nullptr) {
        return;
    }

    lv_point_t pt;
    lv_indev_get_point(indev, &pt);
    if (!IsPointInTaijiCenter(pt.x, pt.y)) {
        return;
    }

    DisplayLockGuard lock(self);
    
    // 按住太极圈即显示遮罩层 + 停止旋转 + 边框变青色
    if (self->fortune_divination_state_ != FortuneDivinationState::Animating) {
        Application::GetInstance().PlayUiSound(Lang::Sounds::OGG_POPUP);
    }
    self->ShowTaijiPressOverlayUnlocked();

    // 如果已经在动画中，记录按住延长
    if (self->fortune_divination_state_ == FortuneDivinationState::Animating) {
        self->taiji_pressed_during_anim_ = true;
        return;
    }

    if (self->fortune_divination_state_ == FortuneDivinationState::Result) {
        self->StopFortuneDivinationUnlocked();
        self->ShowTaijiPressOverlayUnlocked(); // 重新显示，因为 Stop 里隐藏了
    } else if (self->fortune_divination_state_ == FortuneDivinationState::Animating) {
        return;
    }

    self->CancelTaijiHoldTimerUnlocked();
    self->taiji_hold_pending_ = true;
    self->ShowDivinationHintUnlocked("今日占卜\n继续按住…");
    self->taiji_hold_timer_ = lv_timer_create(OnTaijiHoldTimer, FORTUNE_DIVINATION_HOLD_MS, self);
    lv_timer_set_repeat_count(self->taiji_hold_timer_, 1);
}

void AttitudeDisplay::OnTaijiDivinationReleased(lv_event_t* e)
{
    auto* self = static_cast<AttitudeDisplay*>(lv_event_get_user_data(e));
    if (self == nullptr) {
        return;
    }

    DisplayLockGuard lock(self);
    
    // 松开太极圈即隐藏遮罩层 + 恢复旋转 + 恢复边框颜色
    self->HideTaijiPressOverlayUnlocked();

    // 如果在动画中松手，则设定额外的缓冲时间后结束跑马灯
    if (self->fortune_divination_state_ == FortuneDivinationState::Animating && self->taiji_pressed_during_anim_) {
        self->taiji_pressed_during_anim_ = false;
        uint32_t hold_deadline = lv_tick_get() + FORTUNE_DIVINATION_RELEASE_FINISH_MS;
        uint32_t min_deadline = self->fortune_divination_start_ms_ + FORTUNE_DIVINATION_DURATION_MS;
        self->fortune_divination_finish_deadline_ms_ = (hold_deadline > min_deadline) ? hold_deadline : min_deadline;
        return;
    }

    if (!self->taiji_hold_pending_) {
        return;
    }
    self->CancelTaijiHoldTimerUnlocked();
    if (self->fortune_divination_state_ != FortuneDivinationState::Animating) {
        self->HideDivinationHintUnlocked();
    }
}

void AttitudeDisplay::SelectFortuneMenuItemUnlocked(int index)
{
    if (index < 0 || index >= FORTUNE_MENU_COUNT) {
        return;
    }
    if (fortune_divination_state_ == FortuneDivinationState::Animating) {
        return;
    }
    if (fortune_divination_state_ == FortuneDivinationState::Result) {
        StopFortuneDivinationUnlocked();
    }
    const int prev = fortune_menu_selected_index_;
    const bool was_active = fortune_menu_selection_active_;
    fortune_menu_selection_active_ = true;
    fortune_menu_selected_index_ = index;
    // 选中态的音效已禁用（见 PlayFortuneMenuSelectSound）
    // 仅保留视觉反馈：UpdateFortuneMenuItemVisual → icon 放大 + 颜色高亮
    if (was_active && prev != index) {
        UpdateFortuneMenuItemVisual(prev, false);
    }
    UpdateFortuneMenuItemVisual(index, true);

    SetPreviewImageUnlocked(nullptr);
    // 触摸功能图标 → 不显示 infocard（仅 0 号会进入 JARVIS watchface 特效，保持）
    if (index == 0) {
        ShowFortuneFeatureCategoryUnlocked(index);
    } else {
        ESP_LOGD(TAG, "Fortune menu touch: skip infocard for index=%d (silent select)", index);
    }
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
    ClearDebugInfoQueueUnlocked();
    SetPreviewImageUnlocked(nullptr);

    // 取消选中时隐藏 JARVIS 特效，恢复罗盘主界面
    if (fortune_watchface_visible_) {
        FortuneWatchfaceView::GetInstance().HideUnlocked();
        fortune_watchface_visible_ = false;
    }
    // 恢复罗盘主界面
    if (attitude_container_ != nullptr) {
        lv_obj_remove_flag(attitude_container_, LV_OBJ_FLAG_HIDDEN);
    }
}

void AttitudeDisplay::UpdateFortuneMenuItemVisual(int index, bool selected)
{
    if (index < 0 || index >= FORTUNE_MENU_COUNT || fortune_menu_labels_[index] == nullptr) {
        return;
    }
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
        selected ? COLOR_TEXT_HIGH : COLOR_TEXT_MAIN, 0);
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
    // 循环切换选中态 → 不再播放音效（PlayFortuneMenuSelectSound 已禁用）
    // 且非 0 号不再显示 infocard，只在 index==0 时进入 JARVIS watchface 特效

    const int idx = fortune_menu_selected_index_;
    SetPreviewImageUnlocked(nullptr);
    if (idx == 0) {
        ShowFortuneFeatureCategoryUnlocked(idx);
    } else {
        ESP_LOGD(TAG, "Fortune menu cycle: skip infocard for idx=%d (silent select)", idx);
    }
    ESP_LOGI(TAG, "Fortune menu selected -> %d (%s)",
             idx, kFortuneMenuDefs[idx].func_label);
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


// ShowFortuneMenuFeatureCardUnlocked 已彻底删除：功能区提示卡触发事件已全部移除
// ShowFortuneMenuFeatureCard 已彻底删除：公共 API 已被下游禁用

// 根据选中项切换 JARVIS / 罗盘主界面（不再弹出功能信息卡）
void AttitudeDisplay::ShowFortuneFeatureCategoryUnlocked(int index)
{
    if (index < 0 || index >= FORTUNE_MENU_COUNT) {
        return;
    }

    if (index == 0) {
        ESP_LOGI(TAG, "Showing JARVIS watchface effect for Fortune Today");
        SuppressDebugInfoCardForJarvisUnlocked();
        if (attitude_container_ != nullptr) {
            lv_obj_add_flag(attitude_container_, LV_OBJ_FLAG_HIDDEN);
        }
        FortuneWatchfaceView::GetInstance().ShowUnlocked();
        fortune_watchface_visible_ = true;
        return;
    }

    if (fortune_watchface_visible_) {
        FortuneWatchfaceView::GetInstance().HideUnlocked();
        fortune_watchface_visible_ = false;
    }
    if (attitude_container_ != nullptr) {
        lv_obj_remove_flag(attitude_container_, LV_OBJ_FLAG_HIDDEN);
    }
}

bool AttitudeDisplay::HandlePowerKey()
{
    DisplayLockGuard lock(this);

    if (fortune_divination_state_ != FortuneDivinationState::Idle) {
        StopFortuneDivinationUnlocked();
        ESP_LOGI(TAG, "PWR: fortune divination cancelled");
        return true;
    }

    // 取消选中
    if (fortune_menu_selection_active_) {
        DeselectFortuneMenuItemUnlocked();
        ESP_LOGI(TAG, "PWR: selection cancelled");
        return true;
    }

    // 2. 功能区显示状态：隐藏功能区
    if (function_area_card_ != nullptr
        && !lv_obj_has_flag(function_area_card_, LV_OBJ_FLAG_HIDDEN)) {
        PopAndShowNext();
        ESP_LOGI(TAG, "PWR: debug info card dismissed");
        return true;
    }



    return false;
}

void AttitudeDisplay::UpdateStateColor(int level)
{
    if (level < 0) level = 0;
    if (level > 4) level = 4;
    current_state_level_ = level;
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
        break;
    case WifiStatus::CONNECTED:
        lv_obj_set_style_text_color(wifi_fisheye_icon_, COLOR_WIFI_GREEN, 0);
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

void AttitudeDisplay::UpdateWifiFisheye(WifiStatus status)
{
    DisplayLockGuard lock(this);
    wifi_status_ = status;
    ApplyWifiFisheyeStyle(status);
    UpdateOuterRingColor();
    ESP_LOGI(TAG, "WiFi fisheye status -> %d", static_cast<int>(status));
}

void AttitudeDisplay::UpdateBleFisheye(BleStatus status)
{
    DisplayLockGuard lock(this);
    ble_status_ = status;
    ApplyBleFisheyeStyle(status);
    UpdateOuterRingColor();
    ESP_LOGI(TAG, "BLE fisheye status -> %d", static_cast<int>(status));
}

void AttitudeDisplay::UpdateOuterRingColor()
{
    lv_color_t color = COLOR_TEXT_MAIN;
    if (wifi_status_ == WifiStatus::CONNECTED) {
        color = COLOR_WIFI_GREEN;
    } else if (ble_status_ == BleStatus::CONNECTED) {
        color = COLOR_BT_BLUE;
    }

    if (layer4_outer_ring_ != nullptr) {
        lv_obj_set_style_arc_color(layer4_outer_ring_, color, LV_PART_INDICATOR);
    }

    // 同步更新 JARVIS HUD 视图的外环颜色（如果可见）
    if (fortune_watchface_visible_) {
        FortuneWatchfaceView::GetInstance().UpdateOuterRingColor(color);
    }

    if (!taiji_rotation_paused_by_press_) {
        UpdateTaijiGoldRingColor(color);
    }
}

void AttitudeDisplay::UpdateTaijiGoldRingColor(lv_color_t color)
{
    CompassTaiji::UpdateGoldRingColor(color);
}

// 语音唤醒时显示 JARVIS 启动视图：切换到 JARVIS 屏幕
void AttitudeDisplay::ShowJarvisWatchface()
{
    DisplayLockGuard lock(this);
    auto& jarvis = FortuneWatchfaceView::GetInstance();

    // 已在显示中：确保动画定时器恢复（Hide/状态切换后 timer 可能仍处于 pause）
    if (fortune_watchface_visible_) {
        jarvis.EnsureAnimatingUnlocked();
        return;
    }

    ESP_LOGI(TAG, "ShowJarvisWatchface: voice wake-up triggered");
    SuppressDebugInfoCardForJarvisUnlocked();
    if (!view_stack_.contains(ActiveView::JarvisWatchface)) {
        view_stack_.push(ActiveView::JarvisWatchface);
    }
    if (!jarvis.ShowUnlocked()) {
        ESP_LOGW(TAG, "ShowJarvisWatchface: ShowUnlocked failed");
        return;
    }
    fortune_watchface_visible_ = true;

    lv_color_t color = COLOR_TEXT_MAIN;
    if (wifi_status_ == WifiStatus::CONNECTED) {
        color = COLOR_WIFI_GREEN;
    } else if (ble_status_ == BleStatus::CONNECTED) {
        color = COLOR_BT_BLUE;
    }
    jarvis.UpdateOuterRingColorUnlocked(color);
}

// 语音交互结束时隐藏 JARVIS 视图：切换回罗盘主屏幕
void AttitudeDisplay::HideJarvisWatchface()
{
    DisplayLockGuard lock(this);
    if (!fortune_watchface_visible_) {
        return;
    }
    ESP_LOGI(TAG, "HideJarvisWatchface: voice interaction ended");

    FortuneWatchfaceView::GetInstance().ClearVoiceMessage();
    GifPreviewPlayer::GetInstance().Hide();

    FortuneWatchfaceView::GetInstance().HideUnlocked();
    fortune_watchface_visible_ = false;
    view_stack_.pop_if_top(ActiveView::JarvisWatchface);
    FortuneWatchfaceView::GetInstance().ReleaseIdleResourcesUnlocked();
}

void AttitudeDisplay::ReturnToCompassIdleView()
{
    DisplayLockGuard lock(this);
    ReturnToCompassIdleViewUnlocked();
}

void AttitudeDisplay::ReturnToCompassIdleViewUnlocked()
{
    const size_t free_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const size_t min_sram = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);

    // 1. 释放图片/GIF 预览（含解码缓存与独立 screen）
    GifPreviewPlayer::GetInstance().Hide();
    if (image_preview_active_) {
        ExitImagePreviewViewUnlocked();
    }

    // 2. 停止占卜动画/结果与关联音效
    if (fortune_divination_state_ != FortuneDivinationState::Idle) {
        StopFortuneDivinationUnlocked();
    }

    // 3. 清空调试信息队列（lv_timer + 字符串）
    ClearDebugInfoQueueUnlocked();

    // 4. 取消运势菜单选中态
    if (fortune_menu_selection_active_) {
        const int prev = fortune_menu_selected_index_;
        fortune_menu_selection_active_ = false;
        if (prev >= 0 && prev < FORTUNE_MENU_COUNT) {
            UpdateFortuneMenuItemVisual(prev, false);
        }
    }

    // 5. 隐藏 JARVIS 并销毁其 LVGL 屏幕树
    if (fortune_watchface_visible_) {
        FortuneWatchfaceView::GetInstance().ClearVoiceMessage();
        FortuneWatchfaceView::GetInstance().HideUnlocked();
        fortune_watchface_visible_ = false;
    }
    FortuneWatchfaceView::GetInstance().ReleaseIdleResourcesUnlocked();

    // 6. 确保罗盘主容器可见
    if (attitude_container_ != nullptr) {
        lv_obj_remove_flag(attitude_container_, LV_OBJ_FLAG_HIDDEN);
    }

    // 7. 视图栈归一为罗盘
    view_stack_.clear();
    view_stack_.push(ActiveView::Compass);
    divination_from_jarvis_ = false;
    image_preview_active_ = false;

    const size_t free_after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    ESP_LOGI(TAG,
             "ReturnToCompassIdleView: free_sram %u->%u bytes, min_sram=%u",
             (unsigned)free_before, (unsigned)free_after, (unsigned)min_sram);
}

void AttitudeDisplay::ShowImageOnActiveView(std::unique_ptr<LvglImage> image, uint32_t timeout_ms,
                                            bool loop) {
    DisplayLockGuard lock(this);
    ShowImageOnActiveViewUnlocked(std::move(image), timeout_ms, loop);
}

void AttitudeDisplay::SwitchToDivination() {
    DisplayLockGuard lock(this);

    if (fortune_watchface_visible_) {
        divination_from_jarvis_ = true;
        HideJarvisWatchface();
        view_stack_.push(ActiveView::Divination);
        ESP_LOGI(TAG, "SwitchToDivination: JARVIS hidden");
    } else {
        divination_from_jarvis_ = false;
        view_stack_.clear();
        view_stack_.push(ActiveView::Compass);
        view_stack_.push(ActiveView::Divination);
    }

    StartFortuneDivination();
    ESP_LOGI(TAG, "SwitchToDivination: divination started, current=%d",
             static_cast<int>(view_stack_.current()));
}

void AttitudeDisplay::SwitchBackFromDivination() {
    DisplayLockGuard lock(this);

    StopFortuneDivination();
    ESP_LOGI(TAG, "SwitchBackFromDivination: divination stopped");

    // T04: 先重置标志位，再 ShowJarvisWatchface()
    // 避免 ShowJarvisWatchface 抛异常时 divination_from_jarvis_ 永远卡 true，
    // 导致后续摇晃被 IsJarvisHudActive() 错误拦截。
    if (divination_from_jarvis_) {
        bool was_from_jarvis = divination_from_jarvis_;
        divination_from_jarvis_ = false;
        view_stack_.pop_if_top(ActiveView::Divination);
        ShowJarvisWatchface();
        ESP_LOGI(TAG, "SwitchBackFromDivination: JARVIS shown, current=%d (was_from_jarvis=%d)",
                 static_cast<int>(view_stack_.current()), was_from_jarvis);
    }

    int result = GetFortuneDivinationResult();
    if (divination_callback_ != nullptr) {
        divination_callback_(result);
        ESP_LOGI(TAG, "SwitchBackFromDivination: callback triggered, result=%d", result);
    }
}

void AttitudeDisplay::SetDivinationCallback(std::function<void(int)> callback) {
    divination_callback_ = callback;
}

void AttitudeDisplay::FadeViewTransitionUnlocked(lv_obj_t* from_view, lv_obj_t* to_view, uint32_t duration_ms) {
    if (from_view == nullptr || to_view == nullptr) {
        return;
    }
    if (from_view == to_view) {
        return;
    }

    // 1) 起始视图淡出：opacity 255 -> 0
    lv_anim_t fade_out_anim;
    lv_anim_init(&fade_out_anim);
    lv_anim_set_var(&fade_out_anim, from_view);
    lv_anim_set_user_data(&fade_out_anim, from_view);
    lv_anim_set_custom_exec_cb(&fade_out_anim, [](lv_anim_t* a, int32_t v) {
        lv_obj_t* view = static_cast<lv_obj_t*>(lv_anim_get_user_data(a));
        if (view != nullptr) {
            lv_obj_set_style_opa(view, v, 0);
        }
    });
    lv_anim_set_values(&fade_out_anim, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_duration(&fade_out_anim, duration_ms);
    lv_anim_set_path_cb(&fade_out_anim, lv_anim_path_ease_in_out);

    // 3) 淡出动画结束后隐藏起始视图（在 start 前注册）
    lv_anim_set_completed_cb(&fade_out_anim, [](lv_anim_t* a) {
        lv_obj_t* view = static_cast<lv_obj_t*>(lv_anim_get_user_data(a));
        if (view != nullptr) {
            lv_obj_add_flag(view, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_opa(view, LV_OPA_COVER, 0);
        }
    });
    lv_anim_start(&fade_out_anim);

    // 2) 目标视图先设为透明，淡入：opacity 0 -> 255
    lv_obj_set_style_opa(to_view, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(to_view, LV_OBJ_FLAG_HIDDEN);

    lv_anim_t fade_in_anim;
    lv_anim_init(&fade_in_anim);
    lv_anim_set_var(&fade_in_anim, to_view);
    lv_anim_set_user_data(&fade_in_anim, to_view);
    lv_anim_set_custom_exec_cb(&fade_in_anim, [](lv_anim_t* a, int32_t v) {
        lv_obj_t* view = static_cast<lv_obj_t*>(lv_anim_get_user_data(a));
        if (view != nullptr) {
            lv_obj_set_style_opa(view, v, 0);
        }
    });
    lv_anim_set_values(&fade_in_anim, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&fade_in_anim, duration_ms);
    lv_anim_set_path_cb(&fade_in_anim, lv_anim_path_ease_in_out);
    lv_anim_start(&fade_in_anim);
}

// ---------------------------------------------------------------------------
// 迭代 2: AI 运势三态状态机 + 200×240 结果卡
// ---------------------------------------------------------------------------

void AttitudeDisplay::EnterIdleState()
{
    DisplayLockGuard lock(this);
    StopFortuneDivinationUnlocked();
    // 鱼眼状态由 UpdateWifiFisheye/UpdateBleFisheye 管理，无需重复刷新

    fortune_menu_selected_index_ = -1;
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
    // 调试卡固定使用 20px 字体（比主题 30px 略小），布局：
    //   标题 y=32 偏上 50px
    //   详情中心 y=150（卡片直径位置）垂直居中
    const lv_font_t* text_font = &font_puhui_20_4;
    const int card_w = DEBUG_INFO_CARD_W;
    const int text_w = card_w - 40;
    const int text_x = 20;
    // 20px 字体行高 ~24px：
    //   - 标题 1 行 → row_h = 32（24+8 缓冲）
    //   - 详情 4 行 → detail_h = 120（4*24+24 缓冲）
    const int row_h = 32;
    const int detail_h = 120;
    // 标题位置（用户要求：原 y=82 上移 50 → y=32）
    const int y_title = 32;
    // 详情中心放在卡片直径位置 y=150
    const int y_detail = 150 - detail_h / 2;

    // 垂直居中：按 font line_height 动态算 pad_top，让单行文字落在 row 中线
    const int line_h = lv_font_get_line_height(text_font);
    const int title_pad = (row_h > line_h) ? (row_h - line_h) / 2 : 0;
    const int detail_pad = (detail_h > line_h) ? (detail_h - line_h) / 2 : 0;

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

    // 标题（顶部偏上）
    debug_info_title_ = lv_label_create(function_area_card_);
    lv_obj_set_style_text_font(debug_info_title_, text_font, 0);
    lv_obj_set_style_text_color(debug_info_title_, DEBUG_INFO_TITLE_COLOR, 0);
    lv_obj_set_style_text_opa(debug_info_title_, LV_OPA_COVER, 0);
    lv_obj_set_width(debug_info_title_, text_w);
    lv_obj_set_height(debug_info_title_, row_h);
    lv_obj_set_x(debug_info_title_, text_x);
    lv_obj_set_y(debug_info_title_, y_title);
    lv_label_set_long_mode(debug_info_title_, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(debug_info_title_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(debug_info_title_, title_pad, 0);
    lv_label_set_text(debug_info_title_, "");

    // 详情（中心 y=150 卡片直径位置，2 行）
    debug_info_detail_ = lv_label_create(function_area_card_);
    lv_obj_set_style_text_font(debug_info_detail_, text_font, 0);
    lv_obj_set_style_text_color(debug_info_detail_, DEBUG_INFO_DETAIL_COLOR, 0);
    lv_obj_set_width(debug_info_detail_, text_w);
    lv_obj_set_height(debug_info_detail_, detail_h);
    lv_obj_set_x(debug_info_detail_, text_x);
    lv_obj_set_y(debug_info_detail_, y_detail);
    lv_label_set_long_mode(debug_info_detail_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(debug_info_detail_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(debug_info_detail_, detail_pad, 0);
    lv_label_set_text(debug_info_detail_, "");

    ApplyDebugInfoCardLayout();

    ESP_LOGD(TAG, "Debug info card created: y_title=%d y_detail=%d (detail center=%d, card center=150)",
             y_title, y_detail, y_detail + detail_h / 2);
}

// EnsureFortunePromptTitle 已彻底删除：screen 顶层短提示路径已废弃
// HideFortunePromptTitle 已彻底删除：同上
// HideDebugInfoCardLabels 已彻底删除：screen_title_overlay 路径已废弃

void AttitudeDisplay::ApplyDebugInfoCardLayout()
{
    if (function_area_card_ == nullptr) {
        return;
    }
    // 调试卡固定使用 20px 字体（与 CreateDebugInfoCard 保持一致）
    const lv_font_t* text_font = &font_puhui_20_4;
    const int card_w = DEBUG_INFO_CARD_W;
    const int text_w = card_w - 40;
    const int text_x = 20;
    // 必须与 CreateDebugInfoCard 保持一致：row_h=32, detail_h=120
    const int row_h = 32;
    const int detail_h = 120;
    // 标题 y=32（偏上 50px），详情中心 y=150（卡片直径位置）
    const int y_title = 32;
    const int y_detail = 150 - detail_h / 2;

    lv_obj_set_style_clip_corner(function_area_card_, true, 0);
    // 垂直居中：按 font line_height 动态算 pad_top
    const int line_h = lv_font_get_line_height(text_font);
    const int title_pad = (row_h > line_h) ? (row_h - line_h) / 2 : 0;
    const int detail_pad = (detail_h > line_h) ? (detail_h - line_h) / 2 : 0;
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
        lv_obj_set_style_pad_top(debug_info_title_, title_pad, 0);
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
        lv_obj_set_style_pad_top(debug_info_detail_, detail_pad, 0);
    }
}

void AttitudeDisplay::DestroyDebugInfoCard()
{
    StopFortuneDivinationUnlocked();
    
    // 清理事件队列中的所有定时器
    for (auto& item : debug_info_queue_) {
        if (item.timer != nullptr) {
            lv_timer_del(item.timer);
            item.timer = nullptr;
        }
    }
    debug_info_queue_.clear();
    current_index_ = SIZE_MAX;

    GifPreviewPlayer::GetInstance().Hide();
    if (function_area_card_ != nullptr) {
        lv_obj_del(function_area_card_);
        function_area_card_ = nullptr;
    }
    debug_info_title_ = nullptr;
    debug_info_detail_ = nullptr;
    if (taiji_press_overlay_ != nullptr) {
        lv_obj_del(taiji_press_overlay_);
        taiji_press_overlay_ = nullptr;
    }
}

void AttitudeDisplay::PresentDebugInfoCardUnlocked(const std::string& title,
                                                    const std::string& detail,
                                                    uint32_t hold_ms,
                                                    const DebugInfoPresentOpts& opts)
{
    // JARVIS HUD 可见时：路由到 status_label_，避免在语音交互过程中弹出功能卡
    if (IsJarvisHudActive()) {
        RouteToJarvisStatusBar(title + "\n" + detail);
        return;
    }

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

void AttitudeDisplay::OnDebugInfoTimer(lv_timer_t* timer)
{
    auto* self = static_cast<AttitudeDisplay*>(lv_timer_get_user_data(timer));
    if (self == nullptr) {
        return;
    }
    DisplayLockGuard lock(self);
    self->PopAndShowNext();
}

// 根据标题自动推断调试信息优先级
DebugInfoPriority AttitudeDisplay::InferDebugInfoPriority(const std::string& title)
{
    if (title.find("唤醒成功") != std::string::npos) return DebugInfoPriority::CRITICAL;
    if (title.find("WiFi 已连接") != std::string::npos || title.find("握手成功") != std::string::npos) return DebugInfoPriority::HIGH;
    if (title.find("识别") != std::string::npos) return DebugInfoPriority::MEDIUM;
    return DebugInfoPriority::LOW; // 默认
}

// 将事件加入队列
DebugInfoItem* AttitudeDisplay::EnqueueItem(const std::string& title, const std::string& detail,
                                           uint32_t hold_ms, DebugInfoPriority priority)
{
    DebugInfoItem item;
    item.title = title;
    item.detail = detail;
    item.hold_ms = hold_ms;
    item.priority = priority;
    item.enqueue_tick = esp_timer_get_time() / 1000;
    item.timer = lv_timer_create(OnDebugInfoTimer, hold_ms, this);
    lv_timer_pause(item.timer); // 先暂停，由 PopAndShowNext 启动
    debug_info_queue_.push_back(item);
    return &debug_info_queue_.back();
}

// 清理当前事件（定时器 + 从队列移除）
void AttitudeDisplay::CleanupCurrentItem()
{
    if (current_index_ != SIZE_MAX && current_index_ < debug_info_queue_.size()) {
        auto it = debug_info_queue_.begin() + current_index_;
        if (it->timer != nullptr) {
            lv_timer_del(it->timer);
            it->timer = nullptr;
        }
        debug_info_queue_.erase(it);
    }
    current_index_ = SIZE_MAX;
}

// 显示调试信息卡（更新 UI）
void AttitudeDisplay::DisplayDebugInfoCard(const std::string& title, const std::string& detail)
{
    // JARVIS HUD 可见时：直接走 status_label_，避免 function_area_card_ 显示
    if (IsJarvisHudActive()) {
        std::string combined;
        if (!title.empty()) {
            combined = title + ":" + detail;
        } else {
            combined = detail;
        }
        RouteToJarvisStatusBar(combined);
        return;
    }

    CreateDebugInfoCard();
    if (function_area_card_ == nullptr || debug_info_title_ == nullptr || debug_info_detail_ == nullptr) {
        return;
    }

    ApplyDebugInfoCardLayout();
    lv_label_set_text(debug_info_title_, title.c_str());
    lv_label_set_text(debug_info_detail_, detail.c_str());
    lv_obj_remove_flag(debug_info_title_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(debug_info_detail_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(function_area_card_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(function_area_card_);
    lv_obj_update_layout(function_area_card_);
    ESP_LOGI(TAG, "DisplayDebugInfoCard: %s | %s", title.c_str(), detail.c_str());
}

// 清除调试信息卡（隐藏 UI）
void AttitudeDisplay::ClearDebugInfoCard()
{
    if (function_area_card_ != nullptr) {
        lv_obj_add_flag(function_area_card_, LV_OBJ_FLAG_HIDDEN);
    }
}

// 弹出当前事件并显示下一个
void AttitudeDisplay::PopAndShowNext()
{
    // 弹出并清理当前事件
    if (current_index_ != SIZE_MAX) {
        CleanupCurrentItem();
    }
    
    // 显示下一个
    if (!debug_info_queue_.empty()) {
        DebugInfoItem& next = debug_info_queue_.front();
        DisplayDebugInfoCard(next.title, next.detail);
        current_index_ = 0;
        lv_timer_reset(next.timer);
        lv_timer_resume(next.timer);
        ESP_LOGD(TAG, "PopAndShowNext: showing next item, queue size=%d", (int)debug_info_queue_.size());
    } else {
        ClearDebugInfoCard();
        ESP_LOGD(TAG, "PopAndShowNext: queue empty, card cleared");
    }
}

void AttitudeDisplay::ShowDebugInfo(const std::string& title, const std::string& detail, uint32_t hold_ms)
{
    // JARVIS HUD 可见时：所有调试信息直接走 status_label_，不入队、不弹 InfoCard
    if (IsJarvisHudActive()) {
        std::string combined;
        if (!title.empty()) {
            combined = title + ":" + detail;
        } else {
            combined = detail;
        }
        RouteToJarvisStatusBar(combined);
        return;
    }

    DisplayLockGuard lock(this);

    if (fortune_divination_state_ == FortuneDivinationState::Animating) {
        ESP_LOGD(TAG, "ShowDebugInfo skipped (divination animating): %s", title.c_str());
        return;
    }

    if (fortune_menu_selection_active_) {
        ESP_LOGD(TAG, "ShowDebugInfo skipped (fortune menu active): %s", title.c_str());
        return;
    }

    // 1. 自动推断优先级
    DebugInfoPriority priority = InferDebugInfoPriority(title);

    // 2. 去重检查：若队列中已有相同标题的事件，跳过
    for (const auto& item : debug_info_queue_) {
        if (item.title == title) {
            ESP_LOGD(TAG, "ShowDebugInfo dedup (in queue): %s", title.c_str());
            return;
        }
    }

    // 3. 队列满时拒绝 LOW 事件
    if (debug_info_queue_.size() >= DEBUG_INFO_MAX_QUEUE_SIZE && priority == DebugInfoPriority::LOW) {
        ESP_LOGD(TAG, "ShowDebugInfo dropped (queue full, LOW priority): %s", title.c_str());
        return;
    }

    // 4. 优先级判断
    if (current_index_ != SIZE_MAX && current_index_ < debug_info_queue_.size() &&
        priority < debug_info_queue_[current_index_].priority) {
        // 新事件优先级更低 → 仅入队，不覆盖当前显示
        ESP_LOGD(TAG, "ShowDebugInfo queued (lower priority): %s (priority=%d)",
                 title.c_str(), (int)priority);
        EnqueueItem(title, detail, hold_ms, priority);
        return;
    }

    // 5. 新事件优先级 >= 当前 → 覆盖当前，显示新事件
    if (current_index_ != SIZE_MAX) {
        CleanupCurrentItem();
    }

    // 6. 显示新事件
    DisplayDebugInfoCard(title, detail);
    DebugInfoItem* new_item = EnqueueItem(title, detail, hold_ms, priority);
    current_index_ = debug_info_queue_.size() - 1;
    lv_timer_resume(new_item->timer);
}

void AttitudeDisplay::HideDebugInfo()
{
    DisplayLockGuard lock(this);
    PopAndShowNext();
}

void AttitudeDisplay::RefreshDebugInfoTimer(uint32_t hold_ms)
{
    DisplayLockGuard lock(this);
    if (current_index_ == SIZE_MAX || current_index_ >= debug_info_queue_.size()) {
        return;
    }
    // 仅当前显示事件仍为队列头部时重置
    if (current_index_ != 0) {
        return;
    }
    auto& front = debug_info_queue_.front();
    if (front.timer != nullptr) {
        const uint32_t actual_hold = (hold_ms == 0) ? DEBUG_INFO_SHOW_MS : hold_ms;
        lv_timer_pause(front.timer);
        lv_timer_set_period(front.timer, actual_hold);
        lv_timer_reset(front.timer);
        lv_timer_resume(front.timer);
        ESP_LOGD(TAG, "RefreshDebugInfoTimer: reset to %ums", actual_hold);
    }
}

bool AttitudeDisplay::HandleBootKey()
{
    DisplayLockGuard lock(this);

    if (fortune_divination_state_ == FortuneDivinationState::Animating) {
        return true;
    }
    if (fortune_divination_state_ == FortuneDivinationState::Result) {
        StopFortuneDivinationUnlocked();
    }

    // Idle状态：进入选中态或循环选择
    if (!fortune_menu_selection_active_) {
        SelectFortuneMenuItemUnlocked(0);
        ESP_LOGI(TAG, "Boot: selection on, default today (index 0)");
        return true;
    }



    // 循环选择下一个运势项
    CycleFortuneMenuSelectionUnlocked();
    return true;
}

bool AttitudeDisplay::HandleFortuneBootLongPress()
{
    DisplayLockGuard lock(this);

    if (fortune_divination_state_ == FortuneDivinationState::Animating) {
        return true;
    }

    fortune_divination_from_taiji_ = false;
    StartFortuneDivinationUnlocked();
    ESP_LOGI(TAG, "Boot long press: fortune divination started");
    return true;
}

// =================================================================
// 迷宫游戏实现（心情卦）
// =================================================================
