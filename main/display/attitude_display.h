#ifndef ATTITUDE_DISPLAY_H
#define ATTITUDE_DISPLAY_H

#include "lcd_display.h"
#include "attitude_theme.h"
#include <string>

// ====================== 屏幕固定核心参数 ======================
#define SCREEN_W              360
#define SCREEN_H              360
#define ATTITUDE_CENTER_X     180
#define ATTITUDE_CENTER_Y     180
#define VALID_RADIUS          178
#define ANIM_DURATION         300

// ====================== 背景色系 ======================
#define COLOR_BG_OUTER        lv_color_hex(0x0A0A0A)
#define COLOR_BG_CENTER       lv_color_hex(0x121212)

// ====================== 文本层级色系 ======================
#define COLOR_TEXT_MAIN       lv_color_hex(0xD4AF37)
#define COLOR_TEXT_SUB        lv_color_hex(0xC0C0C0)
#define COLOR_TEXT_HIGH       lv_color_hex(0xFFFFFF)

// ====================== 装饰控件色系 ======================
#define COLOR_BORDER_LINE     lv_color_hex(0xD4AF37)
#define COLOR_CARD_BG         lv_color_hex(0x1A1A1A)
#define COLOR_POINT_DOT       lv_color_hex(0xD4AF37)

// ====================== 姿态五档动态状态色 ======================
#define COLOR_STATE_NORMAL    lv_color_hex(0x2E5E4E)
#define COLOR_STATE_LIGHT     lv_color_hex(0x4A6FA5)
#define COLOR_STATE_MID       lv_color_hex(0xD4AF37)
#define COLOR_STATE_HEAVY     lv_color_hex(0xE67E22)
#define COLOR_STATE_DANGER    lv_color_hex(0xB82601)

// ====================== 同心圆四层布局半径 ======================
#define LAYER1_CORE_RADIUS    54    // 层级一: 核心信息区 (0~54px)
#define LAYER2_INDIC_RADIUS   90    // 层级二: 动态指示区 (54~90px)
#define LAYER3_PROGRESS_RADIUS 144  // 层级三: 状态进度区 (90~144px)
#define LAYER4_BOUNDARY_RADIUS 178  // 层级四: 边界留白区 (144~178px)

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

    // 主题切换接口
    void SwitchTheme(AttitudeThemeType theme);

    // 应用当前主题到所有UI元素
    void ApplyCurrentTheme();

private:
    // 主容器
    lv_obj_t* attitude_container_ = nullptr;

    // 迭代2: 背景相关
    lv_obj_t* background_ = nullptr;           // 背景容器
    lv_obj_t* bg_layer_center_ = nullptr;      // 中心渐变层
    lv_obj_t* bg_inner_glow_ = nullptr;        // 内层高亮

    // 屏幕可视边界 (360×360圆形屏幕有效区域)
    lv_obj_t* screen_border_ = nullptr;         // 可视边界线 (半径150)

    // 迭代2: 装饰圆
    lv_obj_t* circle_outer_ = nullptr;         // 外圈装饰 (半径140)
    lv_obj_t* circle_mid_ = nullptr;           // 中圈装饰 (半径120)
    lv_obj_t* circle_inner_ = nullptr;         // 内圈装饰 (半径100)
    
    // 方向标记
    lv_obj_t* dir_n_label_ = nullptr;          // 北
    lv_obj_t* dir_e_label_ = nullptr;          // 东
    lv_obj_t* dir_s_label_ = nullptr;          // 南
    lv_obj_t* dir_w_label_ = nullptr;          // 西

    // 迭代2: 顶部信息栏
    lv_obj_t* top_info_container_ = nullptr;   // 顶部信息容器
    lv_obj_t* network_icon_ = nullptr;         // 网络状态图标
    lv_obj_t* time_label_ = nullptr;           // 时间显示
    lv_obj_t* battery_icon_ = nullptr;         // 电量图标

    // 迭代2: 底部解读区域
    lv_obj_t* bottom_interpret_ = nullptr;     // 底部解读容器
    lv_obj_t* interpret_icon_ = nullptr;       // 状态图标
    lv_obj_t* interpret_text_ = nullptr;       // 解读文字

    // 迭代3: 气泡水平仪组件
    lv_obj_t* bubble_center_marker_ = nullptr; // 中心准星（十字+小圆）
    lv_obj_t* bubble_h_axis_ = nullptr;       // 水平刻度线
    lv_obj_t* bubble_v_axis_ = nullptr;       // 垂直刻度线
    lv_obj_t* bubble_obj_ = nullptr;          // 气泡主体
    lv_obj_t* bubble_glow_ = nullptr;         // 气泡光晕
    lv_obj_t* bubble_highlight_ = nullptr;     // 气泡高光点

    // 气泡状态
    int bubble_offset_x_ = 0;                  // 当前 X 偏移（-60 ~ +60）
    int bubble_offset_y_ = 0;                  // 当前 Y 偏移（-60 ~ +60）
    int bubble_level_ = 0;                     // 倾斜等级 (0-4)

    // 迭代4: 太极阴阳图组件（已跳过）

    // 状态数据
    float current_pitch_ = 0.0f;
    float current_roll_ = 0.0f;
    float current_yaw_ = 0.0f;

    // 内部辅助方法
    void CreateBackground();                   // 创建背景渐变
    void CreateDecorationCircles();            // 创建装饰圆
    void CreateTopInfoRing();                  // 创建顶部信息栏
    void CreateBottomInterpretation();         // 创建底部解读区域
    void CreateBubbleAndCrosshair();           // 创建气泡和十字准星
    void SetBubblePosition(int offset_x, int offset_y);  // 设置气泡位置
    void SetBubbleLevel(int level);            // 设置倾斜等级（改变颜色）
    void ApplyThemeToAttitudeUI();             // 应用主题
};

#endif // ATTITUDE_DISPLAY_H
