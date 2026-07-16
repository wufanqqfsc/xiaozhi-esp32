#include "fortune_watchface_view.h"
#include "application.h"
#include "lvgl_theme.h"
#include "assets/lang_config.h"
#include <esp_lvgl_port.h>
#include <esp_log.h>
#include <esp_random.h>
#include <cmath>
#include <cstdlib>
#include <cstring>

static const char* TAG = "FortuneWatchfaceView";

#define WATCH_PI 3.14159265358979323846f

// 字体声明（使用项目中已有的字体）
LV_FONT_DECLARE(BUILTIN_TEXT_FONT);
LV_FONT_DECLARE(font_puhui_14_1);

// 状态栏外框颜色常量（与 JARVIS 主题保持一致）
//   默认/扫描态   : 青色 0x20eaff（与脉冲弧/扫描弧同色）
//   listening 态   : 粉红 0xff3f93（与内环 pulse_arc_ 同色，表达"听")
//   speaking  态   : 金色 0xD4AF37（与 JARVIS 字体/外环同色，表达"说"）
static constexpr uint32_t kStatusBarBorderDefault_  = 0x20eaff;
static constexpr uint32_t kStatusBarBorderListen_   = 0xff3f93;
static constexpr uint32_t kStatusBarBorderSpeak_    = 0xD4AF37;

// 根据语音状态文案推断当前设备状态并切换状态栏外框颜色（调用方需持有 LVGL 锁）
//   - "聆听中..." → listening → 粉红
//   - "说话中..." → speaking  → 金色
//   - 其它（如通知、占卜提示） → 保持默认青色
static inline void ApplyStatusBarBorderByTextUnlocked(const char* text) {
    if (text == nullptr) {
        return;
    }
    if (strstr(text, "聆听中") != nullptr) {
        FortuneWatchfaceView::GetInstance().SetStatusBarBorderColorUnlocked(
            lv_color_hex(kStatusBarBorderListen_));
    } else if (strstr(text, "说话中") != nullptr) {
        FortuneWatchfaceView::GetInstance().SetStatusBarBorderColorUnlocked(
            lv_color_hex(kStatusBarBorderSpeak_));
    } else {
        FortuneWatchfaceView::GetInstance().SetStatusBarBorderColorUnlocked(
            lv_color_hex(kStatusBarBorderDefault_));
    }
}

FortuneWatchfaceView& FortuneWatchfaceView::GetInstance() {
    static FortuneWatchfaceView instance;
    return instance;
}

FortuneWatchfaceView::FortuneWatchfaceView() {
    memset(tick_marks_, 0, sizeof(tick_marks_));
    memset(jarvis_bars_, 0, sizeof(jarvis_bars_));
    memset(orbit_dots_, 0, sizeof(orbit_dots_));

    // UI 与定时器延迟到 Show() 创建，避免无锁构造时破坏 LVGL 对象树
    ESP_LOGI(TAG, "FortuneWatchfaceView initialized (lazy UI)");
}

FortuneWatchfaceView::~FortuneWatchfaceView() {
    if (timer_ != nullptr) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
    DestroyUI();
}

void FortuneWatchfaceView::SetParentContainer(lv_obj_t* container) {
    parent_container_ = container;
}

int32_t FortuneWatchfaceView::ClampI32(int32_t value, int32_t min, int32_t max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

int32_t FortuneWatchfaceView::OrbitX(float angle, float radius) {
    return CX_ + static_cast<int32_t>(lroundf(cosf(angle) * radius));
}

int32_t FortuneWatchfaceView::OrbitY(float angle, float radius) {
    return CY_ + static_cast<int32_t>(lroundf(sinf(angle) * radius));
}

lv_obj_t* FortuneWatchfaceView::AddBox(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h,
                                       uint32_t color, int32_t radius) {
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

lv_obj_t* FortuneWatchfaceView::AddArc(lv_obj_t* parent, int32_t size, int32_t width,
                                        uint32_t base_color, uint32_t active_color) {
    lv_obj_t* arc = lv_arc_create(parent);
    lv_obj_set_size(arc, size, size);
    lv_obj_center(arc);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(arc, width, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(base_color), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(active_color), LV_PART_INDICATOR);
    return arc;
}

void FortuneWatchfaceView::CreateTickMarks(lv_obj_t* screen) {
    for (int i = 0; i < 60; ++i) {
        float rad = ((float)i * 6.0f - 90.0f) * WATCH_PI / 180.0f;
        int major = (i % 5) == 0;
        int32_t outer = major ? 154 : 150;
        int32_t inner = major ? 136 : 144;
        int32_t x1 = CX_ + static_cast<int32_t>(lroundf(cosf(rad) * inner));
        int32_t y1 = CY_ + static_cast<int32_t>(lroundf(sinf(rad) * inner));
        int32_t x2 = CX_ + static_cast<int32_t>(lroundf(cosf(rad) * outer));
        int32_t y2 = CY_ + static_cast<int32_t>(lroundf(sinf(rad) * outer));
        int32_t w = std::abs(x2 - x1) + (major ? 5 : 3);
        int32_t h = std::abs(y2 - y1) + (major ? 5 : 3);
        int32_t x = x1 < x2 ? x1 : x2;
        int32_t y = y1 < y2 ? y1 : y2;
        uint32_t color = major ? 0x66f6ff : 0x183d5a;
        tick_marks_[i] = AddBox(screen, x, y, w, h, color, LV_RADIUS_CIRCLE);
        lv_obj_set_style_opa(tick_marks_[i], major ? LV_OPA_90 : LV_OPA_50, 0);
    }
}

void FortuneWatchfaceView::CreateDynamicWatchface() {
    lv_obj_t* screen = lv_obj_create(NULL);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x020611), 0);

    // 背景层
    AddBox(screen, 0, 0, W_, H_, 0x020611, 0);
    AddBox(screen, 20, 18, 320, 324, 0x061222, 32);

    // 光晕效果
    lv_obj_t* halo = AddBox(screen, 42, 42, 276, 276, 0x07182b, LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_width(halo, 2, 0);
    lv_obj_set_style_border_color(halo, lv_color_hex(0x123b58), 0);
    lv_obj_set_style_shadow_width(halo, 16, 0);
    lv_obj_set_style_shadow_color(halo, lv_color_hex(0x0b6d99), 0);
    lv_obj_set_style_shadow_opa(halo, LV_OPA_40, 0);

    CreateTickMarks(screen);

    // 轨道点：用轻量 lv_obj 圆点替代全屏 canvas（避免每帧 memset 518KB 卡死 LVGL 任务）
    for (int i = 0; i < ORBIT_COUNT_; ++i) {
        orbit_dots_[i] = lv_obj_create(screen);
        lv_obj_set_size(orbit_dots_[i], 6, 6);
        lv_obj_set_style_radius(orbit_dots_[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(orbit_dots_[i],
                                  lv_color_hex((i % 3 == 0) ? 0xffd447 : 0x20eaff), 0);
        lv_obj_set_style_bg_opa(orbit_dots_[i],
                                static_cast<lv_opa_t>((i % 3 == 0) ? LV_OPA_COVER : LV_OPA_70), 0);
        lv_obj_set_style_border_width(orbit_dots_[i], 0, 0);
        lv_obj_clear_flag(orbit_dots_[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(orbit_dots_[i], LV_OBJ_FLAG_CLICKABLE);
    }

    // 扫描弧
    scan_arc_ = AddArc(screen, 296, 8, 0x0b1d32, 0x20eaff);
    lv_arc_set_angles(scan_arc_, 0, 72);

    // 脉冲弧
    pulse_arc_ = AddArc(screen, 246, 7, 0x102138, 0xff3f93);

    // 秒弧
    seconds_arc_ = AddArc(screen, 202, 5, 0x102138, 0xffd447);

    // 外环（与罗盘主界面一致：贴屏幕圆边，3px金色描边）
    const int outer_r = W_ / 2 - 3 / 2;
    const int outer_size = outer_r * 2;
    outer_ring_ = lv_arc_create(screen);
    lv_obj_set_size(outer_ring_, outer_size, outer_size);
    lv_obj_set_pos(outer_ring_, CX_ - outer_r, CY_ - outer_r);
    lv_arc_set_range(outer_ring_, 0, 360);
    lv_arc_set_value(outer_ring_, 360);
    lv_arc_set_bg_angles(outer_ring_, 0, 360);
    lv_arc_set_angles(outer_ring_, 0, 360);
    lv_obj_set_style_arc_width(outer_ring_, 0, 0);
    lv_obj_set_style_arc_color(outer_ring_, lv_color_hex(0xD4AF37), 0);
    lv_obj_set_style_arc_width(outer_ring_, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(outer_ring_, lv_color_hex(0xD4AF37), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(outer_ring_, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(outer_ring_, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_opa(outer_ring_, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_clear_flag(outer_ring_, LV_OBJ_FLAG_CLICKABLE);

    // 内部光晕
    lv_obj_t* inner = AddBox(screen, 100, 100, 160, 160, 0x04101f, LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_width(inner, 1, 0);
    lv_obj_set_style_border_color(inner, lv_color_hex(0x1f6f93), 0);
    lv_obj_set_style_bg_opa(inner, LV_OPA_80, 0);

    // JARVIS 文字阴影 A
    jarvis_label_shadow_a_ = lv_label_create(screen);
    lv_label_set_text(jarvis_label_shadow_a_, "JARVIS");
    lv_obj_set_style_text_font(jarvis_label_shadow_a_, &BUILTIN_TEXT_FONT, 0);
    lv_obj_set_style_text_color(jarvis_label_shadow_a_, lv_color_hex(kJarvisGoldShadow_), 0);
    lv_obj_set_style_text_letter_space(jarvis_label_shadow_a_, 3, 0);
    lv_obj_align(jarvis_label_shadow_a_, LV_ALIGN_CENTER, 1, -10);

    // JARVIS 文字阴影 B
    jarvis_label_shadow_b_ = lv_label_create(screen);
    lv_label_set_text(jarvis_label_shadow_b_, "JARVIS");
    lv_obj_set_style_text_font(jarvis_label_shadow_b_, &BUILTIN_TEXT_FONT, 0);
    lv_obj_set_style_text_color(jarvis_label_shadow_b_, lv_color_hex(kJarvisGoldShadow_), 0);
    lv_obj_set_style_text_letter_space(jarvis_label_shadow_b_, 3, 0);
    lv_obj_align(jarvis_label_shadow_b_, LV_ALIGN_CENTER, -1, -10);

    // JARVIS 文字主标签
    jarvis_label_ = lv_label_create(screen);
    lv_label_set_text(jarvis_label_, "JARVIS");
    lv_obj_set_style_text_font(jarvis_label_, &BUILTIN_TEXT_FONT, 0);
    lv_obj_set_style_text_color(jarvis_label_, lv_color_hex(kJarvisGold_), 0);
    lv_obj_set_style_text_letter_space(jarvis_label_, 3, 0);
    lv_obj_align(jarvis_label_, LV_ALIGN_CENTER, 0, -10);

    // 频谱音频条 (5个)
    for (int i = 0; i < 5; ++i) {
        jarvis_bars_[i] = AddBox(screen, 128 + i * 22, 218, 14, 4, 0x20eaff, LV_RADIUS_CIRCLE);
        lv_obj_set_style_shadow_width(jarvis_bars_[i], 5, 0);
        lv_obj_set_style_shadow_color(jarvis_bars_[i], lv_color_hex(0x20eaff), 0);
    }

    // 状态栏：椭圆形完全在外环内（W=200, H=80），底边接壤外环内壁（y=358）
    // 圆角半径 = H/2 = 40，呈横向椭圆胶囊形
    status_bar_ = AddBox(screen, STATUS_BAR_X_, STATUS_BAR_Y_, STATUS_BAR_W_, STATUS_BAR_H_,
                         0x07182b, LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_width(status_bar_, 2, 0);  // 加粗边框让颜色变化更明显
    lv_obj_set_style_border_color(status_bar_, lv_color_hex(kStatusBarBorderDefault_), 0);  // 默认青色
    lv_obj_set_style_shadow_width(status_bar_, 10, 0);
    lv_obj_set_style_shadow_color(status_bar_, lv_color_hex(0x0b6d99), 0);
    // 状态栏上下左右内边距（缩窄上下边距以容纳文本）
    lv_obj_set_style_pad_left(status_bar_, 10, 0);
    lv_obj_set_style_pad_right(status_bar_, 10, 0);
    lv_obj_set_style_pad_top(status_bar_, 2, 0);
    lv_obj_set_style_pad_bottom(status_bar_, 2, 0);
    // 设置文字行间距，让两行文字更紧凑
    lv_obj_set_style_text_line_space(status_bar_, 2, 0);

    status_label_ = lv_label_create(status_bar_);
    lv_label_set_text(status_label_, "ESP32-S3  PSRAM 8M  BAT 96%");
    // 使用 SCROLL_CIRCULAR 模式：单行超出自动左右循环滚动
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    // 字体缩小到 14px 以容纳更多字符（约 14px/中文字符）
    lv_obj_set_style_text_font(status_label_, &font_puhui_14_1, 0);
    lv_obj_set_style_text_color(status_label_, lv_color_hex(0xc8f7ff), 0);
    // 单行文本区域（高度 H=80 - pad_top=2 - pad_bottom=2 = 76 足够单行垂直居中显示）
    lv_obj_set_height(status_label_, STATUS_BAR_H_ - 4);
    lv_obj_set_width(status_label_, STATUS_BAR_W_ - 20);
    lv_obj_center(status_label_);

    overlay_screen_ = screen;
}

void FortuneWatchfaceView::ClearOverlayChildPointersUnlocked() {
    scan_arc_ = nullptr;
    pulse_arc_ = nullptr;
    seconds_arc_ = nullptr;
    outer_ring_ = nullptr;
    jarvis_label_ = nullptr;
    jarvis_label_shadow_a_ = nullptr;
    jarvis_label_shadow_b_ = nullptr;
    status_bar_ = nullptr;
    status_label_ = nullptr;
    for (int i = 0; i < ORBIT_COUNT_; ++i) {
        orbit_dots_[i] = nullptr;
    }
    for (int i = 0; i < 60; ++i) {
        tick_marks_[i] = nullptr;
    }
    for (int i = 0; i < 5; ++i) {
        jarvis_bars_[i] = nullptr;
    }
    visible_ = false;
}

void FortuneWatchfaceView::InvalidateStaleOverlayUnlocked() {
    if (overlay_screen_ == nullptr) {
        return;
    }
    if (lv_obj_is_valid(overlay_screen_)) {
        return;
    }
    ESP_LOGW(TAG, "InvalidateStaleOverlay: overlay was deleted, resetting pointers");
    overlay_screen_ = nullptr;
    ClearOverlayChildPointersUnlocked();
}

void FortuneWatchfaceView::CreateUI() {
    InvalidateStaleOverlayUnlocked();
    if (overlay_screen_ != nullptr) {
        return;
    }

    CreateDynamicWatchface();

    ESP_LOGI(TAG, "FortuneWatchfaceView UI created");
}

void FortuneWatchfaceView::EnsureTimer() {
    if (timer_ == nullptr) {
        timer_ = lv_timer_create(OnTimer, 50, this);
        lv_timer_pause(timer_);
    }
}

void FortuneWatchfaceView::DestroyUI() {
    if (timer_ != nullptr) {
        lv_timer_pause(timer_);
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }

    if (overlay_screen_ != nullptr) {
        if (lv_screen_active() == overlay_screen_ && prev_screen_ != nullptr) {
            lv_screen_load(prev_screen_);
        }
        lv_obj_del(overlay_screen_);
        overlay_screen_ = nullptr;
    }

    ClearOverlayChildPointersUnlocked();
}

void FortuneWatchfaceView::EnsureAnimatingUnlocked() {
    InvalidateStaleOverlayUnlocked();
    if (overlay_screen_ == nullptr) {
        CreateUI();
    }
    EnsureTimer();

    if (overlay_screen_ == nullptr) {
        return;
    }
    if (!lv_obj_is_valid(overlay_screen_)) {
        ESP_LOGW(TAG, "EnsureAnimatingUnlocked: stale overlay, recreating");
        overlay_screen_ = nullptr;
        CreateUI();
        if (overlay_screen_ == nullptr || !lv_obj_is_valid(overlay_screen_)) {
            return;
        }
    }

    if (lv_screen_active() != overlay_screen_) {
        if (prev_screen_ == nullptr) {
            prev_screen_ = lv_screen_active();
        }
        lv_obj_clear_flag(overlay_screen_, LV_OBJ_FLAG_HIDDEN);
        lv_screen_load(overlay_screen_);
    }

    visible_ = true;
    if (timer_ != nullptr) {
        lv_timer_resume(timer_);
    }
}

bool FortuneWatchfaceView::ShowUnlocked() {
    InvalidateStaleOverlayUnlocked();

    // idle Hide 后保留的隐藏 overlay 二次唤醒时易失效，销毁后重建
    if (overlay_screen_ != nullptr && !visible_) {
        if (lv_screen_active() != overlay_screen_) {
            ESP_LOGI(TAG, "ShowUnlocked: recreating hidden overlay");
            if (lv_obj_is_valid(overlay_screen_)) {
                lv_obj_del(overlay_screen_);
            }
            overlay_screen_ = nullptr;
            ClearOverlayChildPointersUnlocked();
        }
    }

    if (overlay_screen_ == nullptr) {
        CreateUI();
    }
    EnsureTimer();

    if (overlay_screen_ == nullptr || !lv_obj_is_valid(overlay_screen_)) {
        ESP_LOGE(TAG, "ShowUnlocked: overlay_screen_ invalid after CreateUI");
        return false;
    }

    prev_screen_ = lv_screen_active();
    lv_obj_clear_flag(overlay_screen_, LV_OBJ_FLAG_HIDDEN);
    lv_screen_load(overlay_screen_);
    visible_ = true;
    if (timer_ != nullptr) {
        lv_timer_resume(timer_);
    }
    ESP_LOGI(TAG, "FortuneWatchfaceView shown, prev_screen=%p", prev_screen_);
    return true;
}

void FortuneWatchfaceView::Show() {
    if (!lvgl_port_lock(300)) {
        ESP_LOGW(TAG, "Show: LVGL lock timeout");
        return;
    }
    ShowUnlocked();
    lvgl_port_unlock();
}

void FortuneWatchfaceView::HideUnlocked() {
    visible_ = false;
    if (timer_ != nullptr) {
        lv_timer_pause(timer_);
    }

    InvalidateStaleOverlayUnlocked();
    if (overlay_screen_ != nullptr && lv_obj_is_valid(overlay_screen_)) {
        lv_obj_add_flag(overlay_screen_, LV_OBJ_FLAG_HIDDEN);
    }

    if (prev_screen_ != nullptr) {
        lv_screen_load(prev_screen_);
        ESP_LOGI(TAG, "FortuneWatchfaceView hidden, restored to prev_screen=%p", prev_screen_);
    } else if (overlay_screen_ != nullptr && lv_screen_active() == overlay_screen_) {
        ESP_LOGW(TAG, "HideUnlocked: prev_screen_ is nullptr, cannot restore");
    }
}

void FortuneWatchfaceView::Hide() {
    if (!lvgl_port_lock(300)) {
        ESP_LOGW(TAG, "Hide: LVGL lock timeout");
        return;
    }
    HideUnlocked();
    lvgl_port_unlock();
}

void FortuneWatchfaceView::ReleaseIdleResourcesUnlocked() {
    if (overlay_screen_ == nullptr) {
        return;
    }

    HideUnlocked();
    prev_screen_ = nullptr;
    status_mode_ = kModeDefault;
    voice_status_text_.clear();

    DestroyUI();
    ESP_LOGI(TAG, "ReleaseIdleResources: JARVIS UI destroyed");
}

void FortuneWatchfaceView::ReleaseIdleResources() {
    if (!lvgl_port_lock(300)) {
        ESP_LOGW(TAG, "ReleaseIdleResources: LVGL lock timeout");
        return;
    }
    ReleaseIdleResourcesUnlocked();
    lvgl_port_unlock();
}

void FortuneWatchfaceView::OnTimer(lv_timer_t* timer) {
    auto* self = static_cast<FortuneWatchfaceView*>(lv_timer_get_user_data(timer));
    if (self != nullptr) {
        self->UpdateAnimation();
    }
}

void FortuneWatchfaceView::UpdateAnimation() {
    if (!visible_ || overlay_screen_ == nullptr) {
        return;
    }

    uint32_t tick = lv_tick_get();
    int scan_pct = (tick / 24) % 100;

    // 扫描弧动画
    if (scan_arc_ != nullptr) {
        lv_arc_set_rotation(scan_arc_, (tick / 16) % 360);
    }

    // 脉冲弧动画
    if (pulse_arc_ != nullptr) {
        lv_arc_set_rotation(pulse_arc_, 270 - ((tick / 28) % 360));
        lv_arc_set_value(pulse_arc_, 45 + static_cast<int>(sinf(tick / 280.0f) * 34.0f));
    }

    // 秒弧动画
    if (seconds_arc_ != nullptr) {
        lv_arc_set_value(seconds_arc_, ClampI32(scan_pct, 0, 100));
        lv_arc_set_rotation(seconds_arc_, 210);
    }

    // JARVIS 金色闪烁
    if (jarvis_label_ != nullptr) {
        uint32_t jarvis_color = (tick / 400) % 2 ? kJarvisGoldBright_ : kJarvisGold_;
        lv_obj_set_style_text_color(jarvis_label_, lv_color_hex(jarvis_color), 0);
    }
    if (jarvis_label_shadow_a_ != nullptr) {
        lv_obj_set_style_text_color(jarvis_label_shadow_a_, lv_color_hex(kJarvisGoldShadow_), 0);
    }
    if (jarvis_label_shadow_b_ != nullptr) {
        lv_obj_set_style_text_color(jarvis_label_shadow_b_, lv_color_hex(kJarvisGoldShadow_), 0);
    }

    // 轨道点动画
    for (int i = 0; i < ORBIT_COUNT_; ++i) {
        if (orbit_dots_[i] == nullptr) {
            continue;
        }
        float angle = tick / (780.0f + i * 29.0f) + i * (2.0f * WATCH_PI / ORBIT_COUNT_);
        float radius = 122.0f + sinf(tick / 500.0f + i) * 10.0f;
        int sz = (i % 3 == 0) ? 8 : 5;
        int32_t center_x = OrbitX(angle, radius);
        int32_t center_y = OrbitY(angle, radius);
        lv_obj_set_size(orbit_dots_[i], sz, sz);
        lv_obj_set_pos(orbit_dots_[i], center_x - sz / 2, center_y - sz / 2);
        lv_obj_set_style_opa(orbit_dots_[i],
                              static_cast<lv_opa_t>(150 + static_cast<int>(sinf(tick / 180.0f + i) * 80.0f)), 0);
    }

    // 刻度闪烁
    if ((tick % 250) < 120) {
        int scan_mark = (tick / 100) % 60;
        for (int i = 0; i < 60; i += 5) {
            if (tick_marks_[i] != nullptr) {
                lv_obj_set_style_bg_color(tick_marks_[i],
                                        lv_color_hex((i == scan_mark - (scan_mark % 5)) ? 0xffd447 : 0x66f6ff), 0);
            }
        }
    }

    // 频谱音频条动画
    for (int i = 0; i < 5; ++i) {
        if (jarvis_bars_[i] == nullptr) continue;

        int h = 4 + static_cast<int>(sinf(tick / 150.0f + i * 0.9f) * 8.0f + 8.0f);
        lv_obj_set_size(jarvis_bars_[i], 14, h);
        lv_obj_set_pos(jarvis_bars_[i], 128 + i * 22, 230 - h);
        lv_obj_set_style_opa(jarvis_bars_[i],
                            static_cast<lv_opa_t>(150 + static_cast<int>(sinf(tick / 180.0f + i) * 80.0f)), 0);
    }

    // 状态栏文本：仅在默认模式下显示扫描进度
    if (status_label_ != nullptr && status_mode_ == kModeDefault) {
        lv_label_set_text_fmt(status_label_, "ESP32-S3  JARVIS HUD  SCAN %02d%%", scan_pct);
    }
}

void FortuneWatchfaceView::SetStatusText(const char* text) {
    // lock timeout 由 100ms 提升到 300ms：
    // AttitudeDisplay::SetStatus 在 JARVIS 视图活跃时走该函数，
    // AttitudeDisplay 内多处用 DisplayLockGuard 默认 30s 持锁，
    // 100ms 短锁会多次撞到持锁方并丢失状态文本，直接表现为"白屏重启"前兆。
    if (!lvgl_port_lock(300)) {
        ESP_LOGW(TAG, "SetStatusText: LVGL lock timeout");
        return;
    }

    status_mode_ = kModeVoiceActive;
    voice_status_text_ = text ? text : "";

    if (status_label_ != nullptr) {
        lv_label_set_text(status_label_, voice_status_text_.c_str());
    }

    // 根据状态文案切换状态栏外框颜色（listening 粉红 / speaking 金色 / 其它默认青）
    ApplyStatusBarBorderByTextUnlocked(voice_status_text_.c_str());

    ESP_LOGD(TAG, "SetStatusText: %s", voice_status_text_.c_str());
    lvgl_port_unlock();
}

void FortuneWatchfaceView::ClearStatusText() {
    if (!lvgl_port_lock(300)) {
        ESP_LOGW(TAG, "ClearStatusText: LVGL lock timeout");
        return;
    }

    status_mode_ = kModeDefault;
    voice_status_text_.clear();

    // 恢复状态栏外框为默认青色
    if (status_bar_ != nullptr) {
        lv_obj_set_style_border_color(status_bar_, lv_color_hex(kStatusBarBorderDefault_), 0);
    }

    ESP_LOGD(TAG, "ClearStatusText: restored to default mode");
    lvgl_port_unlock();
    // UpdateAnimation() 会在下次定时器回调中恢复扫描进度显示
}

void FortuneWatchfaceView::UpdateOuterRingColorUnlocked(lv_color_t color) {
    if (outer_ring_ != nullptr) {
        lv_obj_set_style_arc_color(outer_ring_, color, LV_PART_INDICATOR);
    }
}

void FortuneWatchfaceView::UpdateOuterRingColor(lv_color_t color) {
    if (!lvgl_port_lock(300)) {
        ESP_LOGW(TAG, "UpdateOuterRingColor: LVGL lock timeout");
        return;
    }
    UpdateOuterRingColorUnlocked(color);
    lvgl_port_unlock();
}

void FortuneWatchfaceView::SetStatusBarBorderColorUnlocked(lv_color_t color) {
    if (status_bar_ != nullptr) {
        lv_obj_set_style_border_color(status_bar_, color, 0);
    }
}

void FortuneWatchfaceView::SetStatusBarBorderColor(lv_color_t color) {
    if (!lvgl_port_lock(300)) {
        ESP_LOGW(TAG, "SetStatusBarBorderColor: LVGL lock timeout");
        return;
    }
    SetStatusBarBorderColorUnlocked(color);
    lvgl_port_unlock();
}

void FortuneWatchfaceView::SetVoiceMessage(const char* text) {
    if (!lvgl_port_lock(300)) {
        ESP_LOGW(TAG, "SetVoiceMessage: LVGL lock timeout");
        return;
    }
    SetVoiceMessageUnlocked(text);
    lvgl_port_unlock();
}

void FortuneWatchfaceView::SetVoiceMessageUnlocked(const char* text) {
    status_mode_ = kModeVoiceActive;
    voice_status_text_ = text ? text : "";

    if (status_label_ != nullptr) {
        lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_label_set_text(status_label_, voice_status_text_.c_str());
    }

    // 根据状态文案切换状态栏外框颜色
    ApplyStatusBarBorderByTextUnlocked(voice_status_text_.c_str());

    ESP_LOGD(TAG, "SetVoiceMessage: %s", voice_status_text_.c_str());
}

void FortuneWatchfaceView::ClearVoiceMessage() {
    if (!lvgl_port_lock(300)) {
        ESP_LOGW(TAG, "ClearVoiceMessage: LVGL lock timeout");
        return;
    }
    ClearVoiceMessageUnlocked();
    lvgl_port_unlock();
}

void FortuneWatchfaceView::ClearVoiceMessageUnlocked() {
    status_mode_ = kModeDefault;
    voice_status_text_.clear();

    if (status_label_ != nullptr) {
        lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_label_set_text(status_label_, "ESP32-S3  JARVIS HUD  SCAN 00%");
    }

    // 恢复状态栏外框为默认青色
    if (status_bar_ != nullptr) {
        lv_obj_set_style_border_color(status_bar_, lv_color_hex(kStatusBarBorderDefault_), 0);
    }

    ESP_LOGD(TAG, "ClearVoiceMessage: restored to default mode");
}


