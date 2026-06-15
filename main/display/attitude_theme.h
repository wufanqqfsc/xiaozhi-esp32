#ifndef ATTITUDE_THEME_H
#define ATTITUDE_THEME_H

#include "lvgl.h"
#include <stdint.h>

struct AttitudeThemeColors {
    lv_color_t bg_outer;
    lv_color_t bg_inner;

    lv_color_t text_main;
    lv_color_t text_sub;
    lv_color_t text_high;

    lv_color_t border_line;
    lv_color_t card_bg;
    lv_color_t point_default;

    lv_color_t state_normal;
    lv_color_t state_light;
    lv_color_t state_mid;
    lv_color_t state_heavy;
    lv_color_t state_danger;
};

class AttitudeTheme {
public:
    static AttitudeTheme& GetInstance();

    const AttitudeThemeColors& GetColors() const { return colors_; }

    lv_color_t GetStateColor(int level) const;

private:
    AttitudeTheme();
    ~AttitudeTheme() = default;
    AttitudeTheme(const AttitudeTheme&) = delete;
    AttitudeTheme& operator=(const AttitudeTheme&) = delete;

    AttitudeThemeColors colors_;
};

#endif // ATTITUDE_THEME_H
