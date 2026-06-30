#include "attitude_display.h"
#include "lvgl_theme.h"
#include "application.h"
#include "assets/lang_config.h"
#include "board.h"
#include "compass_taiji.h"
#include <esp_lvgl_port.h>
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

// =================================================================
// 一级功能分类内容（用于 DebugInfo 视图展示）
// 来源：doc/ai_compass_feature_expansion.md v1.18
// 索引与 kFortuneMenuDefs 一一对应（FortuneMenuType 0..11）
// 展示策略：
//   - title    = "图标 + 主功能名"（如 "🧮 财运"）
//   - details  = 一级功能分类列表（每行一类）
// =================================================================
struct FortuneMenuFeatureCategory {
    const char* title;   // DebugInfo 标题
    const char* details; // DebugInfo 详情（一级分类文本）
};

static const FortuneMenuFeatureCategory kFortuneMenuFeatureCategories[FORTUNE_MENU_COUNT] = {
    // 0 · 今日运势
    {
        FONT_AWESOME_SUN " 今日运势",
        "1. 信息聚合\n"
        "2. 运势延伸\n"
        "3. 互动延伸"
    },
    // 1 · 财运 — 财富罗盘
    {
        FONT_AWESOME_CALCULATOR " 财运",
        "1. 股票预测\n"
        "2. 理财延伸\n"
        "3. 传统财运"
    },
    // 2 · 事业运势 — 职场指南针
    {
        FONT_AWESOME_GEAR " 事业运势",
        "1. 职场情报\n"
        "2. 职场建议\n"
        "3. 自我提升"
    },
    // 3 · 感情运势 — 情感罗盘
    {
        FONT_AWESOME_HEART " 感情运势",
        "1. 情感状态\n"
        "2. 互动内容\n"
        "3. 心理健康\n"
        "4. 社交延伸"
    },
    // 4 · 心情卦 — 内心卦象 & 放松娱乐
    {
        FONT_AWESOME_MUSIC " 心情卦",
        "1. 娱乐节奏\n"
        "2. 卦象互动\n"
        "3. 深度心理\n"
        "4. 情绪复盘"
    },
    // 5 · 黄历宜忌 — 万年历助手
    {
        FONT_AWESOME_CALENDAR " 黄历宜忌",
        "1. 传统历法\n"
        "2. 核心历法"
    },
    // 6 · 节气提示 — 时节养生
    {
        FONT_AWESOME_CLOUD_SUN " 节气提示",
        "1. 节气核心\n"
        "2. 养生建议\n"
        "3. 气候数据\n"
        "4. 节气文化"
    },
    // 7 · 系统设置 — 设备管家 & YIXING 控制台
    {
        FONT_AWESOME_LOCK " 系统设置",
        "1. YIXING AI 配置\n"
        "2. 配网与连接\n"
        "3. 音频设置\n"
        "4. 设备管理"
    },
    // 8 · 健康运势 — 健康助手
    {
        FONT_AWESOME_TEMPERATURE_HALF " 健康运势",
        "1. 身体状态\n"
        "2. 中医养生\n"
        "3. 健康资讯"
    },
    // 9 · 学业运势 — 学习罗盘
    {
        FONT_AWESOME_GLASSES " 学业运势",
        "1. 专注计时\n"
        "2. 学习建议\n"
        "3. 考试辅助\n"
        "4. 趣味学习"
    },
    // 10 · 出行吉日 — 出行助手
    {
        FONT_AWESOME_GLOBE " 出行吉日",
        "1. 吉日查询\n"
        "2. 出行必备\n"
        "3. 行程助手\n"
        "4. 安全提示"
    },
    // 11 · 贵人运势 — 人际罗盘
    {
        FONT_AWESOME_STAR " 贵人运势",
        "1. 贵人信息\n"
        "2. 人脉管理\n"
        "3. 社交建议\n"
        "4. 人际关系"
    },
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
    // 方位圆点已移除（v1.2+ 视觉简化，运势高亮见迭代 2 再定）

    // ============================================================
    // 图片浮层（仿 DebugInfoCard 风格）
    //   image_overlay_card_  圆形深色面板（300x300，金色边框，圆形遮罩）
    //   preview_image_       lv_image widget（用于 PNG / JPG / BIN 静态图）
    //   preview_gif_         lv_image widget（用于 GIF/PNG/JPG 静态预览，LVGL 9.x 已移除 lv_gif，改用 lv_image）
    // ============================================================
    image_overlay_card_ = lv_obj_create(screen);
    lv_obj_set_size(image_overlay_card_, DEBUG_INFO_CARD_W, DEBUG_INFO_CARD_H);
    lv_obj_set_pos(image_overlay_card_, DEBUG_INFO_CARD_X, DEBUG_INFO_CARD_Y);
    lv_obj_set_style_radius(image_overlay_card_, DEBUG_INFO_CARD_RADIUS, 0);
    lv_obj_set_style_clip_corner(image_overlay_card_, true, 0);
    lv_obj_set_style_bg_color(image_overlay_card_, lv_color_hex(0x0A1414), 0);
    lv_obj_set_style_bg_opa(image_overlay_card_, LV_OPA_90, 0);
    lv_obj_set_style_border_color(image_overlay_card_, DEBUG_INFO_BORDER_COLOR, 0);
    lv_obj_set_style_border_width(image_overlay_card_, 2, 0);
    lv_obj_set_style_pad_all(image_overlay_card_, 0, 0);
    lv_obj_clear_flag(image_overlay_card_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(image_overlay_card_, LV_OBJ_FLAG_HIDDEN);

    // 静态图 widget（PNG / JPG / BIN 等）
    preview_image_ = lv_image_create(image_overlay_card_);
    lv_obj_set_size(preview_image_, DEBUG_INFO_CARD_W, DEBUG_INFO_CARD_H);
    lv_obj_center(preview_image_);
    lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);

    // 静态图 widget（GIF/PNG/JPG）—— 与 preview_image_ 互斥显示
    preview_gif_ = lv_image_create(image_overlay_card_);
    lv_obj_set_size(preview_gif_, DEBUG_INFO_CARD_W, DEBUG_INFO_CARD_H);
    lv_obj_center(preview_gif_);
    lv_obj_add_flag(preview_gif_, LV_OBJ_FLAG_HIDDEN);

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
    // 将时长限制在合理区间，避免动画任务异常
    uint32_t hold_ms = (duration_ms > 0) ? static_cast<uint32_t>(duration_ms) : DEBUG_INFO_SHOW_MS;
    if (hold_ms < 500) hold_ms = 500;
    if (hold_ms > DEBUG_INFO_HOLD_MAX_MS) hold_ms = DEBUG_INFO_HOLD_MAX_MS;
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
    // 状态信息短暂显示 5 秒，避免和通知抢占显示
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

// 功能：在 AttitudeDisplay 上显示一张外部图片（PNG / JPG / GIF / BIN 等）
//   - 隐藏在 attitude_container_ 之后的太极/鱼眼等 UI
//   - 在 300x300 的 image_overlay_card_ 圆形浮层（仿 DebugInfoCard 样式）上居中渲染
//   - 自动按格式分发：GIF → lv_image widget（显示首帧）；其它 → lv_image widget（静态）
//   - image == nullptr 时隐藏浮层、恢复 attitude_container_
// 参数：image 已构造好的 LvglImage；析构时由本对象接管（unique_ptr）
// 线程：内部加 LVGL 互斥锁（lvgl_port_lock 100ms），可以从其他 task 安全调用
void AttitudeDisplay::SetPreviewImage(std::unique_ptr<LvglImage> image)
{
    if (!lvgl_port_lock(100)) {
        ESP_LOGW(TAG, "SetPreviewImage: LVGL lock timeout, skipping");
        return;
    }

    if (image == nullptr) {
        if (preview_image_hide_timer_ != nullptr) {
            lv_timer_del(preview_image_hide_timer_);
            preview_image_hide_timer_ = nullptr;
        }
        if (preview_image_ != nullptr) {
            lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
        }
        if (preview_gif_ != nullptr) {
            lv_obj_add_flag(preview_gif_, LV_OBJ_FLAG_HIDDEN);
        }
        if (image_overlay_card_ != nullptr) {
            lv_obj_add_flag(image_overlay_card_, LV_OBJ_FLAG_HIDDEN);
        }
        preview_image_cache_.reset();
        if (attitude_container_ != nullptr) {
            lv_obj_remove_flag(attitude_container_, LV_OBJ_FLAG_HIDDEN);
        }
        lvgl_port_unlock();
        return;
    }

    if (image_overlay_card_ == nullptr) {
        ESP_LOGE(TAG, "SetPreviewImage: image_overlay_card_ not created (SetupUI not called?)");
        lvgl_port_unlock();
        return;
    }

    preview_image_cache_ = std::move(image);
    auto img_dsc = preview_image_cache_->image_dsc();
    bool is_gif = preview_image_cache_->IsGif();

    ESP_LOGI(TAG, "SetPreviewImage: %dx%d cf=%d is_gif=%d",
             img_dsc->header.w, img_dsc->header.h, (int)img_dsc->header.cf, is_gif ? 1 : 0);

    if (is_gif) {
        // GIF：改用 lv_image widget 显示第一帧（LVGL 9.x 已移除 lv_gif，暂不支持动画）
        if (preview_gif_ == nullptr) {
            ESP_LOGE(TAG, "SetPreviewImage: preview_gif_ not created");
            preview_image_cache_.reset();
            lvgl_port_unlock();
            return;
        }
        lv_image_set_src(preview_gif_, img_dsc);
        lv_obj_remove_flag(preview_gif_, LV_OBJ_FLAG_HIDDEN);
        if (preview_image_ != nullptr) {
            lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        // 静态图（PNG / JPG / BIN）：用 lv_image widget（走 LVGL decoder chain）
        if (preview_image_ == nullptr) {
            ESP_LOGE(TAG, "SetPreviewImage: preview_image_ not created");
            preview_image_cache_.reset();
            lvgl_port_unlock();
            return;
        }
        lv_image_set_src(preview_image_, img_dsc);
        if (img_dsc->header.w > 0 && img_dsc->header.h > 0) {
            lv_image_set_scale(preview_image_, 128 * DEBUG_INFO_CARD_W / img_dsc->header.w);
        }
        lv_obj_center(preview_image_);
        lv_obj_remove_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
        if (preview_gif_ != nullptr) {
            lv_obj_add_flag(preview_gif_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // 显示浮层、隐藏主界面
    lv_obj_remove_flag(image_overlay_card_, LV_OBJ_FLAG_HIDDEN);
    if (attitude_container_ != nullptr) {
        lv_obj_add_flag(attitude_container_, LV_OBJ_FLAG_HIDDEN);
    }
    ESP_LOGI(TAG, "SetPreviewImage: displayed %s %dx%d",
             is_gif ? "GIF" : "image",
             img_dsc->header.w, img_dsc->header.h);

    // 图片显示 10s 后自动隐藏并返回罗盘
    if (preview_image_hide_timer_ != nullptr) {
        lv_timer_del(preview_image_hide_timer_);
    }
    preview_image_hide_timer_ = lv_timer_create(OnPreviewImageHideTimer, 10000, this);

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

    fortune_menu_selected_index_ = 0;
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

void AttitudeDisplay::PlayFortuneMenuSelectSound()
{
    Application::GetInstance().PlayUiSound(Lang::Sounds::OGG_POPUP);
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
        PlayFortuneMenuSelectSound();
    } else if (!was_active) {
        PlayFortuneMenuSelectSound();
    }
    UpdateFortuneMenuItemVisual(index, true);
    // 通过 DebugInfo 卡展示当前主功能的一级分类（Boot 选中/循环选中均触发）
    ShowFortuneFeatureCategoryUnlocked(index);

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
    lv_color_t normal_color = (ble_status_ == BleStatus::CONNECTED) ? COLOR_BT_BLUE : COLOR_TEXT_MAIN;
    lv_obj_set_style_text_color(fortune_menu_labels_[index],
        selected ? COLOR_TEXT_HIGH : normal_color, 0);
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
    PlayFortuneMenuSelectSound();
    // 循环选中同样展示当前主功能的一级分类
    ShowFortuneFeatureCategoryUnlocked(fortune_menu_selected_index_);
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


// ShowFortuneMenuFeatureCardUnlocked 已彻底删除：功能区提示卡触发事件已全部移除
// ShowFortuneMenuFeatureCard 已彻底删除：公共 API 已被下游禁用

// 在 DebugInfo 卡上展示指定索引主功能的一级分类（持锁状态下调用）
// 调用前必须已持有 DisplayLockGuard（外部调用方 SelectFortuneMenuItemUnlocked
// / CycleFortuneMenuSelectionUnlocked / HandleFortuneBootLongPress 均已加锁）
// 注意：直接走 PresentDebugInfoCardUnlocked，绕过 ShowDebugInfo 的
// "fortune_menu_selection_active_ 时被短路"的逻辑，确保选中态也能展示
void AttitudeDisplay::ShowFortuneFeatureCategoryUnlocked(int index)
{
    if (index < 0 || index >= FORTUNE_MENU_COUNT) {
        return;
    }
    // 清掉上一次的"通知/状态"去重上下文，避免本卡被同标题抑制
    debug_info_last_title_.clear();
    debug_info_last_show_ms_ = 0;

    DebugInfoPresentOpts opts;
    // 长按确认场景会在调用方覆盖为 persistent；此处默认 transient（5s 后自动收）
    PresentDebugInfoCardUnlocked(
        kFortuneMenuFeatureCategories[index].title,
        kFortuneMenuFeatureCategories[index].details,
        /*hold_ms=*/0,
        opts);
}

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
    UpdateOuterRingColor();
    ESP_LOGI(TAG, "WiFi fisheye status -> %d", static_cast<int>(status));
}

void AttitudeDisplay::UpdateBleFisheye(BleStatus status)
{
    DisplayLockGuard lock(this);
    ble_status_ = status;
    ApplyBleFisheyeStyle(status);
    SetGoldElementsColor(status == BleStatus::CONNECTED);
    UpdateOuterRingColor();
    ESP_LOGI(TAG, "BLE fisheye status -> %d", static_cast<int>(status));
}

void AttitudeDisplay::SetGoldElementsColor(bool is_bt_connected)
{
    lv_color_t color = is_bt_connected ? COLOR_BT_BLUE : COLOR_TEXT_MAIN;
    ApplyGoldColorToElements(color);
    UpdateTaijiGoldRingColor(color);
}

void AttitudeDisplay::UpdateOuterRingColor()
{
    if (layer4_outer_ring_ == nullptr) {
        return;
    }
    lv_color_t color = COLOR_TEXT_MAIN;
    if (wifi_status_ == WifiStatus::CONNECTED) {
        color = COLOR_WIFI_GREEN;
    } else if (ble_status_ == BleStatus::CONNECTED) {
        color = COLOR_BT_BLUE;
    }
    lv_obj_set_style_arc_color(layer4_outer_ring_, color, LV_PART_INDICATOR);
}

void AttitudeDisplay::ApplyGoldColorToElements(lv_color_t color)
{
    for (int i = 0; i < FORTUNE_MENU_COUNT; ++i) {
        if (fortune_menu_labels_[i] != nullptr) {
            bool is_selected = fortune_menu_selection_active_ && (i == fortune_menu_selected_index_);
            lv_obj_set_style_text_color(fortune_menu_labels_[i],
                is_selected ? COLOR_TEXT_HIGH : color, 0);
        }
    }

    if (debug_info_title_ != nullptr) {
        lv_obj_set_style_text_color(debug_info_title_, color, 0);
    }

    auto screen = lv_screen_active();
    if (screen != nullptr) {
        lv_obj_set_style_text_color(screen, color, 0);
    }

    if (wifi_fisheye_icon_ != nullptr) {
        lv_obj_set_style_text_color(wifi_fisheye_icon_, color, 0);
    }
}

void AttitudeDisplay::UpdateTaijiGoldRingColor(lv_color_t color)
{
    CompassTaiji::UpdateGoldRingColor(color);
}

// ---------------------------------------------------------------------------
// 迭代 2: AI 运势三态状态机 + 200×240 结果卡
// ---------------------------------------------------------------------------

void AttitudeDisplay::EnterIdleState()
{
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

    // 关键修复：必须用无限循环（repeat_count = -1）
    // 原代码设为 1，导致 LVGL 在定时器触发后自动 delete 定时器，
    // 但 debug_info_hide_timer_ 成员未被置空，下次 ShowDebugInfo 会
    // 在悬空指针上调用 lv_timer_reset/set_period，卡片永远不消失。
    // 现在定时器永久运行，由 DestroyDebugInfoCard 显式 delete。
    debug_info_hide_timer_ = lv_timer_create(OnDebugInfoHideTimer, DEBUG_INFO_SHOW_MS, this);
    lv_timer_set_repeat_count(debug_info_hide_timer_, -1);

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
    if (debug_info_hide_timer_ != nullptr) {
        lv_timer_delete(debug_info_hide_timer_);
        debug_info_hide_timer_ = nullptr;
    }
    if (preview_image_hide_timer_ != nullptr) {
        lv_timer_delete(preview_image_hide_timer_);
        preview_image_hide_timer_ = nullptr;
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
    // 注意：不能在持锁情况下调用 lv_refr_now()，LVGL 文档禁止从非 LVGL 任务刷新。
    // 此函数由 ShowNotification 等 UI 调用方持有 DisplayLockGuard，本身为非 LVGL 任务上下文，
    // 故移除 lv_refr_now()；下次 LVGL 周期会自动刷新（典型 < 30ms）。

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
    // 优化：定时器触发后自动暂停，避免卡片已隐藏但定时器仍每 5s 触发
    // 下次 ShowDebugInfo → PresentDebugInfoCardUnlocked 中会 lv_timer_resume + reset
    lv_timer_pause(timer);
}

void AttitudeDisplay::OnPreviewImageHideTimer(lv_timer_t* timer) {
    auto* self = static_cast<AttitudeDisplay*>(lv_timer_get_user_data(timer));
    if (self == nullptr) {
        return;
    }
    DisplayLockGuard lock(self);
    if (self->preview_image_ != nullptr) {
        lv_obj_add_flag(self->preview_image_, LV_OBJ_FLAG_HIDDEN);
    }
    if (self->preview_gif_ != nullptr) {
        lv_obj_add_flag(self->preview_gif_, LV_OBJ_FLAG_HIDDEN);
    }
    if (self->image_overlay_card_ != nullptr) {
        lv_obj_add_flag(self->image_overlay_card_, LV_OBJ_FLAG_HIDDEN);
    }
    self->preview_image_cache_.reset();
    if (self->attitude_container_ != nullptr) {
        lv_obj_remove_flag(self->attitude_container_, LV_OBJ_FLAG_HIDDEN);
    }
}

void AttitudeDisplay::ShowDebugInfo(const std::string& title, const std::string& detail, uint32_t hold_ms)
{
    DisplayLockGuard lock(this);

    if (fortune_menu_selection_active_) {
        ESP_LOGD(TAG, "ShowDebugInfo skipped (fortune menu active): %s", title.c_str());
        return;
    }


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

void AttitudeDisplay::RefreshDebugInfoTimer(uint32_t hold_ms)
{
    DisplayLockGuard lock(this);
    if (debug_info_hide_timer_ == nullptr || function_area_card_ == nullptr) {
        return;
    }
    // 卡片已隐藏则不重置，避免 LVGL 对隐藏对象计时
    if (lv_obj_has_flag(function_area_card_, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    const uint32_t actual_hold = (hold_ms == 0) ? DEBUG_INFO_SHOW_MS : hold_ms;
    lv_timer_set_period(debug_info_hide_timer_, actual_hold);
    lv_timer_reset(debug_info_hide_timer_);
    lv_timer_resume(debug_info_hide_timer_);
}

bool AttitudeDisplay::HandleBootKey()
{
    DisplayLockGuard lock(this);




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

    if (!fortune_menu_selection_active_) {
        return false;
    }



    ESP_LOGI(TAG, "Boot long press: fortune menu %d (no result card, Plan A removed)",
             fortune_menu_selected_index_);
    return true;
}



// =================================================================
// 迷宫游戏实现（心情卦）
// =================================================================
