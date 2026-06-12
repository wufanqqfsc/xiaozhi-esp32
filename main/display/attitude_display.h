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

    // 太极图旋转控制 (按键触发)
    // 顺时针旋转 15°
    void RotateTaiji();
    // 逆时针旋转 15°
    void RotateTaijiCCW();
    // 设置太极图旋转角度
    void SetTaijiRotation(int angle);
    // 获取太极图当前角度
    int GetTaijiRotation();
    // 重置太极图
    void ResetTaijiRotation();

    // 太极图自动旋转控制 (1分钟转一圈)
    // 启动自动旋转, period_ms = 转 360° 所需时间 (默认 60000ms = 1分钟)
    void StartTaijiAutoRotation(int period_ms = 60000);
    // 停止自动旋转
    void StopTaijiAutoRotation();
    // 是否在自动旋转中
    bool IsTaijiAutoRotating();

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

    // 4层同心圆布局成员（迭代2重构）
    // 层级一: 核心信息区 (0~54px) - 已废弃（迭代14清理）
    // 文字 (姿态平衡仪/Balance OK/0.0°) 已被移除, 中心由太极图取代
    lv_obj_t* layer1_container_ = nullptr;     // 核心信息容器 (保留为空)

    // 层级二: 动态指示区 (54~90px)
    lv_obj_t* layer2_inner_ring_ = nullptr;    // 内圈装饰细线 (lv_arc 模拟圆环)
    // 中心角度指示线 (lv_line) - 已废弃（迭代14清理）

    // 层级三: 状态进度区 (90~144px)
    lv_obj_t* layer3_bg_arc_ = nullptr;        // 背景环
    lv_obj_t* layer3_progress_arc_ = nullptr;  // 进度环 (动态变色)
    // 状态文字 "BALANCE" - 已废弃（迭代14清理）

    // 层级四: 边界留白区 (144~178px)
    lv_obj_t* layer4_outer_ring_ = nullptr;    // 1px 鎏金外圆环

    // 状态数据
    float current_pitch_ = 0.0f;
    float current_roll_ = 0.0f;
    float current_yaw_ = 0.0f;
    int current_state_level_ = 0;              // 当前状态等级 0-4

    // 内部辅助方法（4层同心圆布局）
    void CreateBackground();                   // 阶段0: 背景渐变（不属于4层，仅底层）
    void CreateLayer0Taiji();                  // 迭代13: Target 中心太极图
    void CreateLayer1Bagua();                  // 迭代14b: 64 卦符号层
    void CreateLayer1CoreInfo();                // 层级一: 核心信息区
    void CreateLayer2DynamicIndicator();       // 层级二: 动态指示区
    void CreateLayer3StatusProgress();         // 层级三: 状态进度区
    void CreateLayer4Boundary();               // 层级四: 边界留白区
    void CreateCompassPoints();                // 4个方位实心圆点（设计文档第5节）
    void ApplyThemeToAttitudeUI();             // 应用主题
    void UpdateStateColor(int level);          // 更新状态颜色
};

#endif // ATTITUDE_DISPLAY_H
