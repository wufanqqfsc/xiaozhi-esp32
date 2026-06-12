#include "attitude_theme.h"

AttitudeTheme& AttitudeTheme::GetInstance() {
    static AttitudeTheme instance;
    return instance;
}

AttitudeTheme::AttitudeTheme() : current_theme_(AttitudeThemeType::THEME_AURORA) {
    ApplyAuroraTheme();
}

void AttitudeTheme::SetTheme(AttitudeThemeType theme) {
    current_theme_ = theme;
    if (theme == AttitudeThemeType::THEME_AURORA) {
        ApplyAuroraTheme();
    } else {
        ApplyTaijiTheme();
    }
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

const char* AttitudeTheme::GetThemeName() const {
    if (current_theme_ == AttitudeThemeType::THEME_AURORA) {
        return "Aurora";
    }
    return "Taiji";
}

void AttitudeTheme::ApplyAuroraTheme() {
    // 背景色
    colors_.bg_outer    = lv_color_hex(0xF5F5F7);
    colors_.bg_inner    = lv_color_hex(0xFAFAFA);

    // 文字色
    colors_.text_main   = lv_color_hex(0x3A3A3C);
    colors_.text_sub    = lv_color_hex(0x8E8E93);
    colors_.text_high   = lv_color_hex(0x1C1C1E);

    // 装饰色
    colors_.border_line = lv_color_hex(0xD1D1D6);
    colors_.card_bg     = lv_color_hex(0xF2F2F7);
    colors_.point_default = lv_color_hex(0x3A3A3C);

    // 姿态五档状态色
    colors_.state_normal = lv_color_hex(0x3A3A3C);  // 平衡稳态 - 深灰
    colors_.state_light  = lv_color_hex(0x5E5CE6);  // 轻微倾斜 - 苹果紫
    colors_.state_mid    = lv_color_hex(0x007AFF);  // 中度倾斜 - 苹果蓝
    colors_.state_heavy  = lv_color_hex(0xFF9500);  // 较大倾斜 - 苹果橙
    colors_.state_danger = lv_color_hex(0xFF3B30);  // 严重倾斜 - 苹果红
}

void AttitudeTheme::ApplyTaijiTheme() {
    // 背景色
    colors_.bg_outer    = lv_color_hex(0xF5F0E1);
    colors_.bg_inner    = lv_color_hex(0xF9F5EB);

    // 文字色
    colors_.text_main   = lv_color_hex(0x0A0A0A);
    colors_.text_sub    = lv_color_hex(0x5C5C5C);
    colors_.text_high   = lv_color_hex(0xB82601);

    // 装饰色
    colors_.border_line = lv_color_hex(0xD4C8B8);
    colors_.card_bg     = lv_color_hex(0xF0E9DA);
    colors_.point_default = lv_color_hex(0xD4AF37);

    // 姿态五档状态色（国风五行配色）
    colors_.state_normal = lv_color_hex(0x2E5E4E);  // 古玉青-平衡
    colors_.state_light  = lv_color_hex(0x4A6FA5);  // 青花蓝-微倾
    colors_.state_mid    = lv_color_hex(0xD4AF37);  // 鎏金黄-中倾
    colors_.state_heavy  = lv_color_hex(0xE67E22);  // 赭石橙-大倾
    colors_.state_danger = lv_color_hex(0xB82601);  // 朱砂红-危险
}
