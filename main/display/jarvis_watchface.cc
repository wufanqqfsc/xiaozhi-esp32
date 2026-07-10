#include "jarvis_watchface.h"

#include <esp_lvgl_port.h>
#include <esp_log.h>
#include <cmath>
#include <cstdlib>
#include <cstring>

#define TAG "JarvisWatchface"
LV_FONT_DECLARE(BUILTIN_TEXT_FONT);

JarvisWatchface& JarvisWatchface::GetInstance() {
    static JarvisWatchface instance;
    return instance;
}

JarvisWatchface::JarvisWatchface() = default;

JarvisWatchface::~JarvisWatchface() {
    if (!lvgl_port_lock(300)) {
        return;
    }
    DestroyUI();
    lvgl_port_unlock();
}

int JarvisWatchface::ClampI32(int value, int min, int max) {
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

int32_t JarvisWatchface::OrbitX(float angle, float radius) {
    return kCenterX + static_cast<int32_t>(lroundf(cosf(angle) * radius));
}

int32_t JarvisWatchface::OrbitY(float angle, float radius) {
    return kCenterY + static_cast<int32_t>(lroundf(sinf(angle) * radius));
}

lv_obj_t* JarvisWatchface::AddBox(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h,
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

lv_obj_t* JarvisWatchface::AddArc(lv_obj_t* parent, int32_t size, int32_t width,
                                  uint32_t base_color, uint32_t active_color) {
    lv_obj_t* arc = lv_arc_create(parent);
    lv_obj_set_size(arc, size, size);
    lv_obj_center(arc);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(arc, width, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(base_color), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(active_color), LV_PART_INDICATOR);
    return arc;
}

void JarvisWatchface::CreateTickMarks() {
    for (int i = 0; i < 60; ++i) {
        const float rad = (static_cast<float>(i) * 6.0f - 90.0f) * kPi / 180.0f;
        const bool major = (i % 5) == 0;
        const int32_t outer = major ? 154 : 150;
        const int32_t inner = major ? 136 : 144;
        const int32_t x1 = kCenterX + static_cast<int32_t>(lroundf(cosf(rad) * inner));
        const int32_t y1 = kCenterY + static_cast<int32_t>(lroundf(sinf(rad) * inner));
        const int32_t x2 = kCenterX + static_cast<int32_t>(lroundf(cosf(rad) * outer));
        const int32_t y2 = kCenterY + static_cast<int32_t>(lroundf(sinf(rad) * outer));
        const int32_t w = abs(x2 - x1) + (major ? 5 : 3);
        const int32_t h = abs(y2 - y1) + (major ? 5 : 3);
        const int32_t x = x1 < x2 ? x1 : x2;
        const int32_t y = y1 < y2 ? y1 : y2;
        const uint32_t color = major ? 0x66f6ff : 0x183d5a;

        tick_marks_[i] = AddBox(screen_, x, y, w, h, color, LV_RADIUS_CIRCLE);
        lv_obj_set_style_opa(tick_marks_[i], major ? LV_OPA_90 : LV_OPA_50, 0);
    }
}

void JarvisWatchface::CreateUI() {
    if (screen_ != nullptr) {
        return;
    }

    screen_ = lv_obj_create(nullptr);
    lv_obj_clear_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen_, lv_color_hex(0x020611), 0);

    AddBox(screen_, 0, 0, kScreenW, kScreenH, 0x020611, 0);
    AddBox(screen_, 20, 18, 320, 324, 0x061222, 32);

    lv_obj_t* halo = AddBox(screen_, 42, 42, 276, 276, 0x07182b, LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_width(halo, 2, 0);
    lv_obj_set_style_border_color(halo, lv_color_hex(0x123b58), 0);
    lv_obj_set_style_shadow_width(halo, 16, 0);
    lv_obj_set_style_shadow_color(halo, lv_color_hex(0x0b6d99), 0);
    lv_obj_set_style_shadow_opa(halo, LV_OPA_40, 0);

    CreateTickMarks();

    scan_arc_ = AddArc(screen_, 296, 8, 0x0b1d32, 0x20eaff);
    lv_arc_set_angles(scan_arc_, 0, 72);
    pulse_arc_ = AddArc(screen_, 246, 7, 0x102138, 0xff3f93);
    seconds_arc_ = AddArc(screen_, 202, 5, 0x102138, 0xffd447);

    for (int i = 0; i < kOrbitCount; ++i) {
        const int size = (i % 3 == 0) ? 8 : 5;
        orbit_dots_[i] = AddBox(screen_, kCenterX - size / 2, kCenterY - size / 2, size, size,
                                (i % 3 == 0) ? 0xffd447 : 0x20eaff, LV_RADIUS_CIRCLE);
        lv_obj_set_style_shadow_width(orbit_dots_[i], 8, 0);
        lv_obj_set_style_shadow_color(
            orbit_dots_[i],
            lv_obj_get_style_bg_color(orbit_dots_[i], LV_PART_MAIN),
            0);
    }

    lv_obj_t* inner = AddBox(screen_, 100, 100, 160, 160, 0x04101f, LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_width(inner, 1, 0);
    lv_obj_set_style_border_color(inner, lv_color_hex(0x1f6f93), 0);
    lv_obj_set_style_bg_opa(inner, LV_OPA_80, 0);

    jarvis_label_shadow_a_ = lv_label_create(screen_);
    lv_label_set_text(jarvis_label_shadow_a_, "JARVIS");
    lv_obj_set_style_text_font(jarvis_label_shadow_a_, &BUILTIN_TEXT_FONT, 0);
    lv_obj_set_style_text_color(jarvis_label_shadow_a_, lv_color_hex(0x2ff3ff), 0);
    lv_obj_set_style_text_letter_space(jarvis_label_shadow_a_, 3, 0);
    lv_obj_align(jarvis_label_shadow_a_, LV_ALIGN_CENTER, 1, -10);

    jarvis_label_shadow_b_ = lv_label_create(screen_);
    lv_label_set_text(jarvis_label_shadow_b_, "JARVIS");
    lv_obj_set_style_text_font(jarvis_label_shadow_b_, &BUILTIN_TEXT_FONT, 0);
    lv_obj_set_style_text_color(jarvis_label_shadow_b_, lv_color_hex(0x2ff3ff), 0);
    lv_obj_set_style_text_letter_space(jarvis_label_shadow_b_, 3, 0);
    lv_obj_align(jarvis_label_shadow_b_, LV_ALIGN_CENTER, -1, -10);

    jarvis_label_ = lv_label_create(screen_);
    lv_label_set_text(jarvis_label_, "JARVIS");
    lv_obj_set_style_text_font(jarvis_label_, &BUILTIN_TEXT_FONT, 0);
    lv_obj_set_style_text_color(jarvis_label_, lv_color_hex(0xe6fbff), 0);
    lv_obj_set_style_text_letter_space(jarvis_label_, 3, 0);
    lv_obj_align(jarvis_label_, LV_ALIGN_CENTER, 0, -10);

    for (int i = 0; i < 5; ++i) {
        jarvis_bars_[i] = AddBox(screen_, 128 + i * 22, 218, 14, 4, 0x20eaff, LV_RADIUS_CIRCLE);
        lv_obj_set_style_shadow_width(jarvis_bars_[i], 5, 0);
        lv_obj_set_style_shadow_color(jarvis_bars_[i], lv_color_hex(0x20eaff), 0);
    }

    lv_obj_t* status_bar = AddBox(screen_, 54, 284, 252, 36, 0x07182b, LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_width(status_bar, 1, 0);
    lv_obj_set_style_border_color(status_bar, lv_color_hex(0x20eaff), 0);
    lv_obj_set_style_shadow_width(status_bar, 10, 0);
    lv_obj_set_style_shadow_color(status_bar, lv_color_hex(0x0b6d99), 0);

    status_label_ = lv_label_create(status_bar);
    lv_label_set_text(status_label_, "ESP32-S3  PSRAM 8M  BAT 96%");
    lv_obj_set_style_text_font(status_label_, &BUILTIN_TEXT_FONT, 0);
    lv_obj_set_style_text_color(status_label_, lv_color_hex(0xc8f7ff), 0);
    lv_obj_center(status_label_);

    timer_ = lv_timer_create(OnTimer, 33, this);
    ESP_LOGI(TAG, "JARVIS watchface UI created");
}

void JarvisWatchface::DestroyUI() {
    if (timer_ != nullptr) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
    if (screen_ != nullptr) {
        lv_obj_del(screen_);
        screen_ = nullptr;
    }
    previous_screen_ = nullptr;
    scan_arc_ = nullptr;
    pulse_arc_ = nullptr;
    seconds_arc_ = nullptr;
    jarvis_label_ = nullptr;
    jarvis_label_shadow_a_ = nullptr;
    jarvis_label_shadow_b_ = nullptr;
    status_label_ = nullptr;
    memset(orbit_dots_, 0, sizeof(orbit_dots_));
    memset(tick_marks_, 0, sizeof(tick_marks_));
    memset(jarvis_bars_, 0, sizeof(jarvis_bars_));
}

void JarvisWatchface::Show() {
    if (!lvgl_port_lock(300)) {
        ESP_LOGW(TAG, "Show: LVGL lock timeout");
        return;
    }

    if (screen_ == nullptr) {
        CreateUI();
    }
    if (screen_ != nullptr) {
        lv_obj_t* active = lv_screen_active();
        if (active != screen_) {
            previous_screen_ = active;
            lv_screen_load(screen_);
        }
        visible_ = true;
        if (state_ == State::Sleep) {
            state_ = State::Starting;
        }
        UpdateFrame();
        ESP_LOGI(TAG, "JARVIS watchface shown");
    }

    lvgl_port_unlock();
}

void JarvisWatchface::Hide() {
    if (!lvgl_port_lock(300)) {
        ESP_LOGW(TAG, "Hide: LVGL lock timeout");
        return;
    }

    visible_ = false;
    state_ = State::Sleep;
    if (previous_screen_ != nullptr && previous_screen_ != screen_) {
        lv_screen_load(previous_screen_);
    }
    ESP_LOGI(TAG, "JARVIS watchface hidden");
    lvgl_port_unlock();
}

void JarvisWatchface::SetState(State state) {
    state_ = state;
}

void JarvisWatchface::OnTimer(lv_timer_t* timer) {
    auto* self = static_cast<JarvisWatchface*>(lv_timer_get_user_data(timer));
    if (self == nullptr || !self->visible_) {
        return;
    }
    self->UpdateFrame();
}

void JarvisWatchface::UpdateFrame() {
    if (screen_ == nullptr || !visible_) {
        return;
    }

    const uint32_t tick = lv_tick_get();
    const int scan_pct = static_cast<int>((tick / 24) % 100);

    lv_arc_set_rotation(scan_arc_, static_cast<int>((tick / 16) % 360));
    lv_arc_set_rotation(pulse_arc_, static_cast<int>(270 - ((tick / 28) % 360)));
    lv_arc_set_value(pulse_arc_, 45 + static_cast<int>(sinf(tick / 280.0f) * 34.0f));
    lv_arc_set_value(seconds_arc_, ClampI32(scan_pct, 0, 100));
    lv_arc_set_rotation(seconds_arc_, 210);

    const uint32_t jarvis_color = ((tick / 400) % 2) ? 0xffffff : 0x7ff7ff;
    lv_obj_set_style_text_color(jarvis_label_, lv_color_hex(jarvis_color), 0);
    lv_obj_set_style_text_color(jarvis_label_shadow_a_, lv_color_hex(0x2ff3ff), 0);
    lv_obj_set_style_text_color(jarvis_label_shadow_b_, lv_color_hex(0x2ff3ff), 0);

    for (int i = 0; i < kOrbitCount; ++i) {
        const float angle = tick / (780.0f + i * 29.0f) + i * (2.0f * kPi / kOrbitCount);
        const float radius = 122.0f + sinf(tick / 500.0f + i) * 10.0f;
        const int size = (i % 3 == 0) ? 8 : 5;
        lv_obj_set_pos(orbit_dots_[i], OrbitX(angle, radius) - size / 2, OrbitY(angle, radius) - size / 2);
        lv_obj_set_style_opa(orbit_dots_[i], static_cast<lv_opa_t>(120 + static_cast<int>(sinf(tick / 260.0f + i) * 80.0f)), 0);
    }

    if ((tick % 250) < 120) {
        const int scan_mark = static_cast<int>((tick / 100) % 60);
        for (int i = 0; i < 60; i += 5) {
            const bool highlight = (i == scan_mark - (scan_mark % 5));
            lv_obj_set_style_bg_color(tick_marks_[i], lv_color_hex(highlight ? 0xffd447 : 0x66f6ff), 0);
        }
    }

    for (int i = 0; i < 5; ++i) {
        const int h = 4 + static_cast<int>(sinf(tick / 150.0f + i * 0.9f) * 8.0f + 8.0f);
        lv_obj_set_size(jarvis_bars_[i], 14, h);
        lv_obj_set_pos(jarvis_bars_[i], 128 + i * 22, 230 - h);
        lv_obj_set_style_opa(jarvis_bars_[i], static_cast<lv_opa_t>(150 + static_cast<int>(sinf(tick / 180.0f + i) * 80.0f)), 0);
    }

    lv_label_set_text_fmt(status_label_, "ESP32-S3  JARVIS HUD  SCAN %02d%%", scan_pct);
}
