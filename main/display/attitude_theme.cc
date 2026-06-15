#include "attitude_theme.h"

AttitudeTheme& AttitudeTheme::GetInstance() {
    static AttitudeTheme instance;
    return instance;
}

AttitudeTheme::AttitudeTheme() {
    colors_.bg_outer    = lv_color_hex(0x0A0A0A);
    colors_.bg_inner    = lv_color_hex(0x121212);

    colors_.text_main   = lv_color_hex(0xD4AF37);
    colors_.text_sub    = lv_color_hex(0xC0C0C0);
    colors_.text_high   = lv_color_hex(0xFFFFFF);

    colors_.border_line = lv_color_hex(0xD4AF37);
    colors_.card_bg     = lv_color_hex(0x1A1A1A);
    colors_.point_default = lv_color_hex(0xD4AF37);

    colors_.state_normal = lv_color_hex(0x2E5E4E);
    colors_.state_light  = lv_color_hex(0x4A6FA5);
    colors_.state_mid    = lv_color_hex(0xD4AF37);
    colors_.state_heavy  = lv_color_hex(0xE67E22);
    colors_.state_danger = lv_color_hex(0xB82601);
}

lv_color_t AttitudeTheme::GetStateColor(int level) const {
    switch (level) {
        case 0: return colors_.state_normal;
        case 1: return colors_.state_light;
        case 2: return colors_.state_mid;
        case 3: return colors_.state_heavy;
        case 4: return colors_.state_danger;
        default: return colors_.state_normal;
    }
}
