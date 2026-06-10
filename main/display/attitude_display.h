#ifndef ATTITUDE_DISPLAY_H
#define ATTITUDE_DISPLAY_H

#include "lcd_display.h"
#include <string>

class AttitudeDisplay : public SpiLcdDisplay {
public:
    // 构造函数：继承 SpiLcdDisplay 的构造签名
    AttitudeDisplay(esp_lcd_panel_io_handle_t panel_io,
                   esp_lcd_panel_handle_t panel,
                   int width, int height,
                   int offset_x, int offset_y,
                   bool mirror_x, bool mirror_y, bool swap_xy);

    virtual ~AttitudeDisplay() = default;

    // 重写关键方法
    virtual void SetupUI() override;
    virtual void SetTheme(Theme* theme) override;

    // 姿态显示专属方法（第一版为空实现，后续迭代完善）
    void SetAttitudeData(float pitch, float roll, float yaw);
    void SetInterpretation(const std::string& text);

private:
    // UI 元素（第一版只需测试容器和标签）
    lv_obj_t* test_label_ = nullptr;       // 测试用标签
    lv_obj_t* attitude_container_ = nullptr;  // 姿态显示主容器

    // 状态数据（第一版为占位）
    float current_pitch_ = 0.0f;
    float current_roll_ = 0.0f;
    float current_yaw_ = 0.0f;

    // 内部辅助方法
    void CreateTestLayout();  // 创建测试布局
    void ApplyThemeToAttitudeUI();  // 应用主题
};

#endif // ATTITUDE_DISPLAY_H
