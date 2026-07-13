#include "fortune_watchface_view.h"
#include "application.h"
#include "lvgl_theme.h"
#include "attitude_display.h"
#include "assets/lang_config.h"
#include <esp_lvgl_port.h>
#include <esp_log.h>
#include <esp_random.h>
#include <esp_heap_caps.h>
#include <cmath>
#include <cstdlib>
#include <cstring>

static const char* TAG = "FortuneWatchfaceView";

#define WATCH_PI 3.14159265358979323846f
#define CANVAS_SIZE 32
#define CANVAS_BUFFER_SIZE (CANVAS_SIZE * CANVAS_SIZE * 4)

// 字体声明（使用项目中已有的字体）
LV_FONT_DECLARE(BUILTIN_TEXT_FONT);
LV_FONT_DECLARE(font_puhui_14_1);

FortuneWatchfaceView& FortuneWatchfaceView::GetInstance() {
    static FortuneWatchfaceView instance;
    return instance;
}

FortuneWatchfaceView::FortuneWatchfaceView() {
    memset(tick_marks_, 0, sizeof(tick_marks_));
    memset(jarvis_bars_, 0, sizeof(jarvis_bars_));

    // 在 PSRAM 分配 canvas buffer
    canvas_buffer_ = (uint8_t*)heap_caps_malloc(CANVAS_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
    if (canvas_buffer_ != nullptr) {
        memset(canvas_buffer_, 0, CANVAS_BUFFER_SIZE);
    }

    // 创建所有静态 UI 元素
    CreateUI();

    // 创建定时器
    timer_ = lv_timer_create(OnTimer, 33, this);
    lv_timer_pause(timer_);

    ESP_LOGI(TAG, "FortuneWatchfaceView initialized");
}

FortuneWatchfaceView::~FortuneWatchfaceView() {
    if (timer_ != nullptr) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
    DestroyUI();
    if (canvas_buffer_ != nullptr) {
        heap_caps_free(canvas_buffer_);
        canvas_buffer_ = nullptr;
    }
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

    // 轨道点 - 使用单个 canvas 替代多个对象
    if (canvas_buffer_ != nullptr) {
        orbit_canvas_ = lv_canvas_create(screen);
        lv_canvas_set_buffer(orbit_canvas_, canvas_buffer_, CANVAS_SIZE, CANVAS_SIZE, LV_COLOR_FORMAT_ARGB8888);
        lv_obj_set_pos(orbit_canvas_, CX_ - CANVAS_SIZE / 2, CY_ - CANVAS_SIZE / 2);
        lv_obj_remove_style(orbit_canvas_, NULL, 0);
        lv_obj_set_style_border_width(orbit_canvas_, 0, 0);
    }

    // 内部光晕
    lv_obj_t* inner = AddBox(screen, 100, 100, 160, 160, 0x04101f, LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_width(inner, 1, 0);
    lv_obj_set_style_border_color(inner, lv_color_hex(0x1f6f93), 0);
    lv_obj_set_style_bg_opa(inner, LV_OPA_80, 0);

    // JARVIS 文字阴影 A
    jarvis_label_shadow_a_ = lv_label_create(screen);
    lv_label_set_text(jarvis_label_shadow_a_, "JARVIS");
    lv_obj_set_style_text_font(jarvis_label_shadow_a_, &BUILTIN_TEXT_FONT, 0);
    lv_obj_set_style_text_color(jarvis_label_shadow_a_, lv_color_hex(0x2ff3ff), 0);
    lv_obj_set_style_text_letter_space(jarvis_label_shadow_a_, 3, 0);
    lv_obj_align(jarvis_label_shadow_a_, LV_ALIGN_CENTER, 1, -10);

    // JARVIS 文字阴影 B
    jarvis_label_shadow_b_ = lv_label_create(screen);
    lv_label_set_text(jarvis_label_shadow_b_, "JARVIS");
    lv_obj_set_style_text_font(jarvis_label_shadow_b_, &BUILTIN_TEXT_FONT, 0);
    lv_obj_set_style_text_color(jarvis_label_shadow_b_, lv_color_hex(0x2ff3ff), 0);
    lv_obj_set_style_text_letter_space(jarvis_label_shadow_b_, 3, 0);
    lv_obj_align(jarvis_label_shadow_b_, LV_ALIGN_CENTER, -1, -10);

    // JARVIS 文字主标签
    jarvis_label_ = lv_label_create(screen);
    lv_label_set_text(jarvis_label_, "JARVIS");
    lv_obj_set_style_text_font(jarvis_label_, &BUILTIN_TEXT_FONT, 0);
    lv_obj_set_style_text_color(jarvis_label_, lv_color_hex(0xe6fbff), 0);
    lv_obj_set_style_text_letter_space(jarvis_label_, 3, 0);
    lv_obj_align(jarvis_label_, LV_ALIGN_CENTER, 0, -10);

    // 频谱音频条 (5个)
    for (int i = 0; i < 5; ++i) {
        jarvis_bars_[i] = AddBox(screen, 128 + i * 22, 218, 14, 4, 0x20eaff, LV_RADIUS_CIRCLE);
        lv_obj_set_style_shadow_width(jarvis_bars_[i], 5, 0);
        lv_obj_set_style_shadow_color(jarvis_bars_[i], lv_color_hex(0x20eaff), 0);
    }

    // 状态栏（向上移动 20px，加宽加高，支持两行显示和滚动）
    // 位置 y=264（原 284），宽度 290（原 252），高度 56（原 36），容纳两行 14px 文本
    lv_obj_t* status_bar = AddBox(screen, 35, 264, 290, 56, 0x07182b, LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_width(status_bar, 1, 0);
    lv_obj_set_style_border_color(status_bar, lv_color_hex(0x20eaff), 0);
    lv_obj_set_style_shadow_width(status_bar, 10, 0);
    lv_obj_set_style_shadow_color(status_bar, lv_color_hex(0x0b6d99), 0);
    // 状态栏上下左右内边距
    lv_obj_set_style_pad_left(status_bar, 10, 0);
    lv_obj_set_style_pad_right(status_bar, 10, 0);
    lv_obj_set_style_pad_top(status_bar, 4, 0);
    lv_obj_set_style_pad_bottom(status_bar, 4, 0);
    // 设置文字行间距，让两行文字更紧凑
    lv_obj_set_style_text_line_space(status_bar, 2, 0);

    status_label_ = lv_label_create(status_bar);
    lv_label_set_text(status_label_, "ESP32-S3  PSRAM 8M  BAT 96%");
    // 使用 SCROLL_CIRCULAR 模式：单行超出自动左右循环滚动
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    // 字体缩小到 14px 以容纳更多字符（约 14px/中文字符）
    lv_obj_set_style_text_font(status_label_, &font_puhui_14_1, 0);
    lv_obj_set_style_text_color(status_label_, lv_color_hex(0xc8f7ff), 0);
    // 设置高度支持两行（14px 行高 * 2 + 行间距 = ~30px）
    lv_obj_set_height(status_label_, 44);
    lv_obj_set_width(status_label_, 270);
    lv_obj_center(status_label_);

    // 图片覆盖层 - 居中显示，300x300 圆形
    image_overlay_ = lv_obj_create(screen);
    lv_obj_set_size(image_overlay_, 300, 300);
    lv_obj_center(image_overlay_);
    lv_obj_set_style_radius(image_overlay_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(image_overlay_, lv_color_hex(0x0A1414), 0);
    lv_obj_set_style_bg_opa(image_overlay_, LV_OPA_80, 0);
    lv_obj_set_style_border_width(image_overlay_, 2, 0);
    lv_obj_set_style_border_color(image_overlay_, lv_color_hex(0xD4AF37), 0);
    lv_obj_clear_flag(image_overlay_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(image_overlay_, LV_OBJ_FLAG_HIDDEN);

    // 图片控件 - 居中放置，260x260
    image_widget_ = lv_image_create(image_overlay_);
    lv_obj_set_size(image_widget_, 260, 260);
    lv_obj_center(image_widget_);
    lv_img_set_zoom(image_widget_, 256);
    lv_obj_add_flag(image_widget_, LV_OBJ_FLAG_HIDDEN);

    overlay_screen_ = screen;
}

void FortuneWatchfaceView::CreateUI() {
    if (overlay_screen_ != nullptr) {
        return;
    }

    CreateDynamicWatchface();

    ESP_LOGI(TAG, "FortuneWatchfaceView UI created");
}

void FortuneWatchfaceView::DestroyUI() {
    if (image_hide_timer_ != nullptr) {
        lv_timer_del(image_hide_timer_);
        image_hide_timer_ = nullptr;
    }

    if (gif_controller_ != nullptr) {
        if (image_widget_ != nullptr) {
            lv_img_set_src(image_widget_, NULL);
        }
        gif_controller_->Stop();
        delete gif_controller_;
        gif_controller_ = nullptr;
    }

    image_widget_ = nullptr;
    image_overlay_ = nullptr;
    image_visible_ = false;

    if (overlay_screen_ != nullptr) {
        lv_obj_del(overlay_screen_);
        overlay_screen_ = nullptr;
    }

    scan_arc_ = nullptr;
    pulse_arc_ = nullptr;
    seconds_arc_ = nullptr;
    jarvis_label_ = nullptr;
    jarvis_label_shadow_a_ = nullptr;
    jarvis_label_shadow_b_ = nullptr;
    status_label_ = nullptr;

    orbit_canvas_ = nullptr;
    for (int i = 0; i < 60; ++i) {
        tick_marks_[i] = nullptr;
    }
    for (int i = 0; i < 5; ++i) {
        jarvis_bars_[i] = nullptr;
    }
}

void FortuneWatchfaceView::Show() {
    if (!lvgl_port_lock(300)) {
        ESP_LOGW(TAG, "Show: LVGL lock timeout");
        return;
    }

    // 保存当前活动屏幕，用于 Hide 时恢复
    prev_screen_ = lv_screen_active();

    if (overlay_screen_ != nullptr) {
        lv_obj_clear_flag(overlay_screen_, LV_OBJ_FLAG_HIDDEN);
        lv_screen_load(overlay_screen_);
        visible_ = true;
        if (timer_ != nullptr) {
            lv_timer_resume(timer_);
        }
        ESP_LOGI(TAG, "FortuneWatchfaceView shown, prev_screen=%p", prev_screen_);
    }

    lvgl_port_unlock();
}

void FortuneWatchfaceView::Hide() {
    if (!lvgl_port_lock(300)) {
        ESP_LOGW(TAG, "Hide: LVGL lock timeout");
        return;
    }

    visible_ = false;
    if (timer_ != nullptr) {
        lv_timer_pause(timer_);
    }

    if (overlay_screen_ != nullptr) {
        lv_obj_add_flag(overlay_screen_, LV_OBJ_FLAG_HIDDEN);
    }

    // 恢复到显示前的原始屏幕，解决白屏问题
    if (prev_screen_ != nullptr) {
        lv_screen_load(prev_screen_);
        ESP_LOGI(TAG, "FortuneWatchfaceView hidden, restored to prev_screen=%p", prev_screen_);
    } else {
        ESP_LOGW(TAG, "Hide: prev_screen_ is nullptr, cannot restore");
    }

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

    // JARVIS 文字颜色闪烁
    if (jarvis_label_ != nullptr) {
        uint32_t jarvis_color = (tick / 400) % 2 ? 0xffffffu : 0x7ff7ffu;
        lv_obj_set_style_text_color(jarvis_label_, lv_color_hex(jarvis_color), 0);
    }
    if (jarvis_label_shadow_a_ != nullptr) {
        lv_obj_set_style_text_color(jarvis_label_shadow_a_, lv_color_hex(0x2ff3ff), 0);
    }
    if (jarvis_label_shadow_b_ != nullptr) {
        lv_obj_set_style_text_color(jarvis_label_shadow_b_, lv_color_hex(0x2ff3ff), 0);
    }

    // 轨道点动画 - 使用 canvas 绘制
    if (orbit_canvas_ != nullptr && canvas_buffer_ != nullptr) {
        memset(canvas_buffer_, 0, CANVAS_BUFFER_SIZE);

        for (int i = 0; i < ORBIT_COUNT_; ++i) {
            float angle = tick / (780.0f + i * 29.0f) + i * (2.0f * WATCH_PI / ORBIT_COUNT_);
            float radius = 122.0f + sinf(tick / 500.0f + i) * 10.0f;
            int sz = (i % 3 == 0) ? 8 : 5;
            int32_t center_x = OrbitX(angle, radius);
            int32_t center_y = OrbitY(angle, radius);

            // 将全局坐标转换为 canvas 局部坐标
            int32_t local_x = center_x - (CX_ - CANVAS_SIZE / 2);
            int32_t local_y = center_y - (CY_ - CANVAS_SIZE / 2);

            // 绘制圆形点
            for (int dy = -sz/2; dy <= sz/2; dy++) {
                for (int dx = -sz/2; dx <= sz/2; dx++) {
                    if (dx*dx + dy*dy <= (sz/2)*(sz/2)) {
                        int px = local_x + dx;
                        int py = local_y + dy;
                        if (px >= 0 && px < CANVAS_SIZE && py >= 0 && py < CANVAS_SIZE) {
                            uint32_t idx = (py * CANVAS_SIZE + px) * 4;
                            uint8_t alpha = (i % 3 == 0) ? 0xff : 0xb4;
                            canvas_buffer_[idx + 0] = alpha;     // A
                            canvas_buffer_[idx + 1] = 0xff;     // R
                            canvas_buffer_[idx + 2] = (i % 3 == 0) ? 0xd4 : 0xea;  // G
                            canvas_buffer_[idx + 3] = (i % 3 == 0) ? 0x47 : 0xff;   // B
                        }
                    }
                }
            }
        }

        lv_canvas_set_buffer(orbit_canvas_, canvas_buffer_, CANVAS_SIZE, CANVAS_SIZE, LV_COLOR_FORMAT_ARGB8888);
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

void FortuneWatchfaceView::ShowImage(const lv_img_dsc_t* img_dsc, bool is_gif, uint32_t timeout_ms) {
    if (!lvgl_port_lock(300)) {
        ESP_LOGW(TAG, "ShowImage: LVGL lock timeout");
        return;
    }

    if (image_hide_timer_ != nullptr) {
        lv_timer_del(image_hide_timer_);
        image_hide_timer_ = nullptr;
    }

    if (gif_controller_ != nullptr) {
        gif_controller_->Stop();
        delete gif_controller_;
        gif_controller_ = nullptr;
    }

    if (image_widget_ != nullptr && img_dsc != nullptr) {
        lv_image_set_src(image_widget_, img_dsc);
    }

    if (is_gif && img_dsc != nullptr) {
        gif_controller_ = new LvglGif(img_dsc);
        gif_controller_->Start();
        if (gif_controller_->IsLoaded()) {
            lv_image_set_src(image_widget_, gif_controller_->image_dsc());
        }
    }

    if (timer_ != nullptr) {
        lv_timer_pause(timer_);
    }

    if (image_overlay_ != nullptr) {
        lv_obj_remove_flag(image_overlay_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_opa(image_overlay_, 0, 0);
        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, image_overlay_);
        lv_anim_set_values(&anim, 0, LV_OPA_COVER);
        lv_anim_set_time(&anim, 300);
        lv_anim_set_exec_cb(&anim, [](void* var, int32_t value) {
            lv_obj_set_style_opa((lv_obj_t*)var, value, 0);
        });
        lv_anim_start(&anim);
    }

    if (image_widget_ != nullptr) {
        lv_obj_remove_flag(image_widget_, LV_OBJ_FLAG_HIDDEN);
    }

    image_hide_timer_ = lv_timer_create([](lv_timer_t* timer) {
        auto* self = static_cast<FortuneWatchfaceView*>(lv_timer_get_user_data(timer));
        if (self != nullptr) {
            self->HideImage();
        }
    }, timeout_ms, this);

    image_visible_ = true;
    ESP_LOGI(TAG, "ShowImage: displayed image, timeout=%dms", timeout_ms);

    lvgl_port_unlock();
}

void FortuneWatchfaceView::HideImage() {
    if (!lvgl_port_lock(300)) {
        ESP_LOGW(TAG, "HideImage: LVGL lock timeout");
        return;
    }

    if (image_hide_timer_ != nullptr) {
        lv_timer_del(image_hide_timer_);
        image_hide_timer_ = nullptr;
    }

    if (gif_controller_ != nullptr) {
        gif_controller_->Stop();
        delete gif_controller_;
        gif_controller_ = nullptr;
    }

    if (image_overlay_ != nullptr) {
        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, image_overlay_);
        lv_anim_set_values(&anim, LV_OPA_COVER, 0);
        lv_anim_set_time(&anim, 300);
        lv_anim_set_exec_cb(&anim, [](void* var, int32_t value) {
            lv_obj_set_style_opa((lv_obj_t*)var, value, 0);
        });
        lv_anim_set_ready_cb(&anim, [](lv_anim_t* anim) {
            lv_obj_add_flag((lv_obj_t*)lv_anim_get_user_data(anim), LV_OBJ_FLAG_HIDDEN);
        });
        lv_anim_set_user_data(&anim, image_overlay_);
        lv_anim_start(&anim);
    }

    if (image_widget_ != nullptr) {
        lv_obj_add_flag(image_widget_, LV_OBJ_FLAG_HIDDEN);
    }

    if (timer_ != nullptr && visible_) {
        lv_timer_resume(timer_);
    }

    image_visible_ = false;
    ESP_LOGI(TAG, "HideImage: image hidden");

    lvgl_port_unlock();
}

bool FortuneWatchfaceView::IsImageVisible() const {
    return image_visible_;
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

    ESP_LOGD(TAG, "ClearStatusText: restored to default mode");
    lvgl_port_unlock();
    // UpdateAnimation() 会在下次定时器回调中恢复扫描进度显示
}

void FortuneWatchfaceView::UpdateOuterRingColor(lv_color_t color) {
    if (!lvgl_port_lock(300)) {
        ESP_LOGW(TAG, "UpdateOuterRingColor: LVGL lock timeout");
        return;
    }

    if (outer_ring_ != nullptr) {
        lv_obj_set_style_arc_color(outer_ring_, color, LV_PART_INDICATOR);
    }

    lvgl_port_unlock();
}

void FortuneWatchfaceView::SetVoiceMessage(const char* text) {
    if (!lvgl_port_lock(300)) {
        // listening 中 WS 回调与 AttitudeDisplay::SetStatus 同时调用
        // 100ms 短锁会直接放弃并丢失消息；提升到 300ms 大概率能取得锁。
        ESP_LOGW(TAG, "SetVoiceMessage: LVGL lock timeout");
        return;
    }

    status_mode_ = kModeVoiceActive;
    voice_status_text_ = text ? text : "";

    if (status_label_ != nullptr) {
        // 使用长文本滚动模式，自动循环滚动超出部分
        lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_label_set_text(status_label_, voice_status_text_.c_str());
    }

    ESP_LOGD(TAG, "SetVoiceMessage: %s", voice_status_text_.c_str());
    lvgl_port_unlock();
}

void FortuneWatchfaceView::ClearVoiceMessage() {
    if (!lvgl_port_lock(300)) {
        ESP_LOGW(TAG, "ClearVoiceMessage: LVGL lock timeout");
        return;
    }

    status_mode_ = kModeDefault;
    voice_status_text_.clear();

    if (status_label_ != nullptr) {
        // 恢复默认文本模式，由 UpdateAnimation() 接管扫描进度显示
        lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_label_set_text(status_label_, "ESP32-S3  JARVIS HUD  SCAN 00%");
    }

    ESP_LOGD(TAG, "ClearVoiceMessage: restored to default mode");
    lvgl_port_unlock();
}


