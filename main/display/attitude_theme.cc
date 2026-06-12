#include "attitude_theme.h"

AttitudeTheme& AttitudeTheme::GetInstance() {
    static AttitudeTheme instance;
    return instance;
}

AttitudeTheme::AttitudeTheme() : current_theme_(AttitudeThemeType::THEME_TAIJI) {
    ApplyTaijiTheme();
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
    // 背景色（设计文档6.1节：玄黑径向渐变）
    colors_.bg_outer    = lv_color_hex(0x0A0A0A);  // 外圈玄黑
    colors_.bg_inner    = lv_color_hex(0x121212);  // 中心深邃黑

    // 文字色（设计文档6.2节）
    colors_.text_main   = lv_color_hex(0xD4AF37);  // 鎏金黄（主标题、核心数据）
    colors_.text_sub    = lv_color_hex(0xC0C0C0);  // 银灰（辅助说明文字）
    colors_.text_high   = lv_color_hex(0xFFFFFF);  // 纯白（高亮警告）

    // 装饰色（设计文档6.3节）
    colors_.border_line = lv_color_hex(0xD4AF37);  // 鎏金（边框、分割线）
    colors_.card_bg     = lv_color_hex(0x1A1A1A);  // 暗黑金（卡片底色）
    colors_.point_default = lv_color_hex(0xD4AF37); // 鎏金（方位圆点）

    // 姿态五档状态色（设计文档6.4节：国风五行配色）
    colors_.state_normal = lv_color_hex(0x2E5E4E);  // 古玉青-平衡
    colors_.state_light  = lv_color_hex(0x4A6FA5);  // 青花蓝-微倾
    colors_.state_mid    = lv_color_hex(0xD4AF37);  // 鎏金黄-中倾
    colors_.state_heavy  = lv_color_hex(0xE67E22);  // 赭石橙-大倾
    colors_.state_danger = lv_color_hex(0xB82601);  // 朱砂红-危险
}
