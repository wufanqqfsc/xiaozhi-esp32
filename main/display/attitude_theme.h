#ifndef ATTITUDE_THEME_H
#define ATTITUDE_THEME_H

#include "lvgl.h"
#include <stdint.h>

// 主题类型枚举
enum class AttitudeThemeType {
    THEME_AURORA,   // 极光罗盘·苹果极简浅色风
    THEME_TAIJI     // 太极风水罗盘·东方古典风
};

// 主题色值结构
struct AttitudeThemeColors {
    // 背景色
    lv_color_t bg_outer;       // 背景外圈
    lv_color_t bg_inner;       // 背景内圈

    // 文字色
    lv_color_t text_main;      // 主文字
    lv_color_t text_sub;       // 次文字
    lv_color_t text_high;      // 高亮文字

    // 装饰色
    lv_color_t border_line;    // 边框线
    lv_color_t card_bg;        // 卡片背景
    lv_color_t point_default;  // 方位点默认

    // 状态色（姿态五档）
    lv_color_t state_normal;   // 平衡稳态
    lv_color_t state_light;    // 轻微倾斜
    lv_color_t state_mid;      // 中度倾斜
    lv_color_t state_heavy;    // 较大倾斜
    lv_color_t state_danger;   // 严重倾斜
};

// 主题管理类（单例）
class AttitudeTheme {
public:
    static AttitudeTheme& GetInstance();

    // 获取当前主题类型
    AttitudeThemeType GetCurrentTheme() const { return current_theme_; }

    // 切换主题
    void SetTheme(AttitudeThemeType theme);

    // 获取当前主题色值
    const AttitudeThemeColors& GetColors() const { return colors_; }

    // 根据状态等级获取颜色（0-4）
    lv_color_t GetStateColor(int level) const;

    // 获取主题名称字符串
    const char* GetThemeName() const;

private:
    AttitudeTheme();
    ~AttitudeTheme() = default;
    AttitudeTheme(const AttitudeTheme&) = delete;
    AttitudeTheme& operator=(const AttitudeTheme&) = delete;

    void ApplyAuroraTheme();   // 应用极光罗盘主题
    void ApplyTaijiTheme();    // 应用太极风水罗盘主题

    AttitudeThemeType current_theme_;
    AttitudeThemeColors colors_;
};

#endif // ATTITUDE_THEME_H
