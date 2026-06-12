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
    virtual void UpdateStatusBar(bool update_all = false) override;

    // 姿态显示专属方法
    void SetAttitudeData(float pitch, float roll, float yaw);
    void SetInterpretation(const std::string& text);

private:
    // 主容器
    lv_obj_t* attitude_container_ = nullptr;

    // 迭代2: 背景相关
    lv_obj_t* background_ = nullptr;           // 背景容器
    lv_obj_t* bg_layer_center_ = nullptr;      // 中心渐变层
    lv_obj_t* bg_inner_glow_ = nullptr;        // 内层高亮

    // 迭代2: 装饰圆
    lv_obj_t* circle_outer_ = nullptr;         // 外圈装饰 (半径160)
    lv_obj_t* circle_mid_ = nullptr;           // 中圈装饰 (半径140)
    lv_obj_t* circle_inner_ = nullptr;         // 内圈装饰 (半径120)

    // 迭代2: 顶部信息栏
    lv_obj_t* top_info_container_ = nullptr;   // 顶部信息容器
    lv_obj_t* network_icon_ = nullptr;         // 网络状态图标
    lv_obj_t* time_label_ = nullptr;           // 时间显示
    lv_obj_t* battery_icon_ = nullptr;         // 电量图标

    // 迭代2: 底部解读区域
    lv_obj_t* bottom_interpret_ = nullptr;     // 底部解读容器
    lv_obj_t* interpret_icon_ = nullptr;       // 状态图标
    lv_obj_t* interpret_text_ = nullptr;       // 解读文字

    // 状态数据
    float current_pitch_ = 0.0f;
    float current_roll_ = 0.0f;
    float current_yaw_ = 0.0f;

    // 内部辅助方法
    void CreateBackground();                   // 创建背景渐变
    void CreateDecorationCircles();            // 创建装饰圆
    void CreateTopInfoRing();                  // 创建顶部信息栏
    void CreateBottomInterpretation();         // 创建底部解读区域
    void ApplyThemeToAttitudeUI();             // 应用主题
};

#endif // ATTITUDE_DISPLAY_H
