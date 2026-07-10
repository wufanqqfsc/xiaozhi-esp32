#include "bsp/esp-bsp.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lvgl.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define TAG "dynamic_watchface"
#define WATCH_PI 3.14159265358979323846f
#define SCREEN_W 360
#define SCREEN_H 360
#define CX 180
#define CY 180
#define ORBIT_COUNT 12

static lv_obj_t *scan_arc;
static lv_obj_t *pulse_arc;
static lv_obj_t *seconds_arc;
static lv_obj_t *jarvis_label;
static lv_obj_t *jarvis_label_shadow_a;
static lv_obj_t *jarvis_label_shadow_b;
static lv_obj_t *status_label;
static lv_obj_t *orbit_dots[ORBIT_COUNT];
static lv_obj_t *tick_marks[60];
static lv_obj_t *jarvis_bars[5];

static int clamp_i32(int value, int min, int max)
{
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

static int32_t orbit_x(float angle, float radius)
{
    return CX + (int32_t)lroundf(cosf(angle) * radius);
}

static int32_t orbit_y(float angle, float radius)
{
    return CY + (int32_t)lroundf(sinf(angle) * radius);
}

static lv_obj_t *add_box(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h,
                         uint32_t color, int32_t radius)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static lv_obj_t *add_arc(lv_obj_t *parent, int32_t size, int32_t width,
                         uint32_t base_color, uint32_t active_color)
{
    lv_obj_t *arc = lv_arc_create(parent);
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

static void create_tick_marks(lv_obj_t *screen)
{
    for (int i = 0; i < 60; ++i) {
        float rad = ((float)i * 6.0f - 90.0f) * WATCH_PI / 180.0f;
        int major = (i % 5) == 0;
        int32_t outer = major ? 154 : 150;
        int32_t inner = major ? 136 : 144;
        int32_t x1 = CX + (int32_t)lroundf(cosf(rad) * inner);
        int32_t y1 = CY + (int32_t)lroundf(sinf(rad) * inner);
        int32_t x2 = CX + (int32_t)lroundf(cosf(rad) * outer);
        int32_t y2 = CY + (int32_t)lroundf(sinf(rad) * outer);
        int32_t w = abs(x2 - x1) + (major ? 5 : 3);
        int32_t h = abs(y2 - y1) + (major ? 5 : 3);
        int32_t x = x1 < x2 ? x1 : x2;
        int32_t y = y1 < y2 ? y1 : y2;
        uint32_t color = major ? 0x66f6ff : 0x183d5a;
        tick_marks[i] = add_box(screen, x, y, w, h, color, LV_RADIUS_CIRCLE);
        lv_obj_set_style_opa(tick_marks[i], major ? LV_OPA_90 : LV_OPA_50, 0);
    }
}

static void create_dynamic_watchface(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x020611), 0);
    lv_screen_load(screen);

    add_box(screen, 0, 0, SCREEN_W, SCREEN_H, 0x020611, 0);
    add_box(screen, 20, 18, 320, 324, 0x061222, 32);

    lv_obj_t *halo = add_box(screen, 42, 42, 276, 276, 0x07182b, LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_width(halo, 2, 0);
    lv_obj_set_style_border_color(halo, lv_color_hex(0x123b58), 0);
    lv_obj_set_style_shadow_width(halo, 16, 0);
    lv_obj_set_style_shadow_color(halo, lv_color_hex(0x0b6d99), 0);
    lv_obj_set_style_shadow_opa(halo, LV_OPA_40, 0);

    create_tick_marks(screen);

    scan_arc = add_arc(screen, 296, 8, 0x0b1d32, 0x20eaff);
    lv_arc_set_angles(scan_arc, 0, 72);
    pulse_arc = add_arc(screen, 246, 7, 0x102138, 0xff3f93);
    seconds_arc = add_arc(screen, 202, 5, 0x102138, 0xffd447);

    for (int i = 0; i < ORBIT_COUNT; ++i) {
        int sz = (i % 3 == 0) ? 8 : 5;
        orbit_dots[i] = add_box(screen, CX - sz / 2, CY - sz / 2, sz, sz,
                                (i % 3 == 0) ? 0xffd447 : 0x20eaff, LV_RADIUS_CIRCLE);
        lv_obj_set_style_shadow_width(orbit_dots[i], 8, 0);
        lv_obj_set_style_shadow_color(orbit_dots[i], lv_obj_get_style_bg_color(orbit_dots[i], 0), 0);
    }

    lv_obj_t *inner = add_box(screen, 100, 100, 160, 160, 0x04101f, LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_width(inner, 1, 0);
    lv_obj_set_style_border_color(inner, lv_color_hex(0x1f6f93), 0);
    lv_obj_set_style_bg_opa(inner, LV_OPA_80, 0);

    jarvis_label_shadow_a = lv_label_create(screen);
    lv_label_set_text(jarvis_label_shadow_a, "JARVIS");
    lv_obj_set_style_text_font(jarvis_label_shadow_a, &lv_font_montserrat_26, 0);
    lv_obj_set_style_text_color(jarvis_label_shadow_a, lv_color_hex(0x2ff3ff), 0);
    lv_obj_set_style_text_letter_space(jarvis_label_shadow_a, 3, 0);
    lv_obj_align(jarvis_label_shadow_a, LV_ALIGN_CENTER, 1, -10);

    jarvis_label_shadow_b = lv_label_create(screen);
    lv_label_set_text(jarvis_label_shadow_b, "JARVIS");
    lv_obj_set_style_text_font(jarvis_label_shadow_b, &lv_font_montserrat_26, 0);
    lv_obj_set_style_text_color(jarvis_label_shadow_b, lv_color_hex(0x2ff3ff), 0);
    lv_obj_set_style_text_letter_space(jarvis_label_shadow_b, 3, 0);
    lv_obj_align(jarvis_label_shadow_b, LV_ALIGN_CENTER, -1, -10);

    jarvis_label = lv_label_create(screen);
    lv_label_set_text(jarvis_label, "JARVIS");
    lv_obj_set_style_text_font(jarvis_label, &lv_font_montserrat_26, 0);
    lv_obj_set_style_text_color(jarvis_label, lv_color_hex(0xe6fbff), 0);
    lv_obj_set_style_text_letter_space(jarvis_label, 3, 0);
    lv_obj_align(jarvis_label, LV_ALIGN_CENTER, 0, -10);

    for (int i = 0; i < 5; ++i) {
        jarvis_bars[i] = add_box(screen, 128 + i * 22, 218, 14, 4, 0x20eaff, LV_RADIUS_CIRCLE);
        lv_obj_set_style_shadow_width(jarvis_bars[i], 5, 0);
        lv_obj_set_style_shadow_color(jarvis_bars[i], lv_color_hex(0x20eaff), 0);
    }

    lv_obj_t *status_bar = add_box(screen, 54, 284, 252, 36, 0x07182b, LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_width(status_bar, 1, 0);
    lv_obj_set_style_border_color(status_bar, lv_color_hex(0x20eaff), 0);
    lv_obj_set_style_shadow_width(status_bar, 10, 0);
    lv_obj_set_style_shadow_color(status_bar, lv_color_hex(0x0b6d99), 0);

    status_label = lv_label_create(status_bar);
    lv_label_set_text(status_label, "ESP32-S3  PSRAM 8M  BAT 96%");
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xc8f7ff), 0);
    lv_obj_center(status_label);
}

static void watch_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    uint32_t tick = lv_tick_get();
    int scan_pct = (tick / 24) % 100;

    lv_arc_set_rotation(scan_arc, (tick / 16) % 360);
    lv_arc_set_rotation(pulse_arc, 270 - ((tick / 28) % 360));
    lv_arc_set_value(pulse_arc, 45 + (int)(sinf(tick / 280.0f) * 34.0f));
    lv_arc_set_value(seconds_arc, clamp_i32(scan_pct, 0, 100));
    lv_arc_set_rotation(seconds_arc, 210);

    uint32_t jarvis_color = (tick / 400) % 2 ? 0xffffff : 0x7ff7ff;
    lv_obj_set_style_text_color(jarvis_label, lv_color_hex(jarvis_color), 0);
    lv_obj_set_style_text_color(jarvis_label_shadow_a, lv_color_hex(0x2ff3ff), 0);
    lv_obj_set_style_text_color(jarvis_label_shadow_b, lv_color_hex(0x2ff3ff), 0);

    for (int i = 0; i < ORBIT_COUNT; ++i) {
        float angle = tick / (780.0f + i * 29.0f) + i * (2.0f * WATCH_PI / ORBIT_COUNT);
        float radius = 122.0f + sinf(tick / 500.0f + i) * 10.0f;
        int sz = (i % 3 == 0) ? 8 : 5;
        lv_obj_set_pos(orbit_dots[i], orbit_x(angle, radius) - sz / 2, orbit_y(angle, radius) - sz / 2);
        lv_obj_set_style_opa(orbit_dots[i], (lv_opa_t)(120 + (int)(sinf(tick / 260.0f + i) * 80.0f)), 0);
    }

    if ((tick % 250) < 120) {
        int scan_mark = (tick / 100) % 60;
        for (int i = 0; i < 60; i += 5) {
            lv_obj_set_style_bg_color(tick_marks[i], lv_color_hex((i == scan_mark - (scan_mark % 5)) ? 0xffd447 : 0x66f6ff), 0);
        }
    }

    for (int i = 0; i < 5; ++i) {
        int h = 4 + (int)(sinf(tick / 150.0f + i * 0.9f) * 8.0f + 8.0f);
        lv_obj_set_size(jarvis_bars[i], 14, h);
        lv_obj_set_pos(jarvis_bars[i], 128 + i * 22, 230 - h);
        lv_obj_set_style_opa(jarvis_bars[i], (lv_opa_t)(150 + (int)(sinf(tick / 180.0f + i) * 80.0f)), 0);
    }
    lv_label_set_text_fmt(status_label, "ESP32-S3  JARVIS HUD  SCAN %02d%%", scan_pct);
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    lv_display_t *disp = bsp_display_start();
    (void)disp;
    bsp_display_backlight_on();

    bsp_display_lock(-1);
    create_dynamic_watchface();
    lv_timer_create(watch_timer_cb, 33, NULL);
    watch_timer_cb(NULL);
    bsp_display_unlock();

    ESP_LOGI(TAG, "Dynamic neon watchface started");
}
