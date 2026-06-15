#ifndef ATTITUDE_DISPLAY_H
#define ATTITUDE_DISPLAY_H

#include "lcd_display.h"
#include "attitude_theme.h"
#include <string>

#define SCREEN_W              360
#define SCREEN_H              360
#define ATTITUDE_CENTER_X     180
#define ATTITUDE_CENTER_Y     180
#define VALID_RADIUS          178
#define ANIM_DURATION         300

#define COLOR_BG_OUTER        lv_color_hex(0x0A0A0A)
#define COLOR_BG_CENTER       lv_color_hex(0x121212)

#define COLOR_TEXT_MAIN       lv_color_hex(0xD4AF37)
#define COLOR_TEXT_SUB        lv_color_hex(0xC0C0C0)
#define COLOR_TEXT_HIGH       lv_color_hex(0xFFFFFF)

#define COLOR_BORDER_LINE     lv_color_hex(0xD4AF37)
#define COLOR_STATE_HEAVY     lv_color_hex(0xE67E22)
#define COLOR_STATE_DANGER    lv_color_hex(0xB82601)

#define LAYER1_CORE_RADIUS    54
#define LAYER2_INDIC_RADIUS   90
#define LAYER3_PROGRESS_RADIUS 144
#define LAYER4_BOUNDARY_RADIUS 178

class AttitudeDisplay : public SpiLcdDisplay {
public:
    AttitudeDisplay(esp_lcd_panel_io_handle_t panel_io,
                   esp_lcd_panel_handle_t panel,
                   int width, int height,
                   int offset_x, int offset_y,
                   bool mirror_x, bool mirror_y, bool swap_xy);

    virtual ~AttitudeDisplay() = default;

    virtual void SetupUI() override;
    virtual void SetTheme(Theme* theme) override;
    virtual void UpdateStatusBar(bool update_all = false) override;

    void SetAttitudeData(float pitch, float roll, float yaw);
    void SetInterpretation(const std::string& text);

    void RotateTaiji();
    void RotateTaijiCCW();
    void SetTaijiRotation(int angle);
    int GetTaijiRotation();
    void ResetTaijiRotation();

    void StartTaijiAutoRotation(int period_ms = 60000);
    void StopTaijiAutoRotation();
    bool IsTaijiAutoRotating();

private:
    lv_obj_t* attitude_container_ = nullptr;

    lv_obj_t* background_ = nullptr;
    lv_obj_t* bg_layer_center_ = nullptr;
    lv_obj_t* bg_inner_glow_ = nullptr;

    lv_obj_t* screen_border_ = nullptr;

    lv_obj_t* circle_outer_ = nullptr;
    lv_obj_t* circle_mid_ = nullptr;
    lv_obj_t* circle_inner_ = nullptr;

    lv_obj_t* dir_n_label_ = nullptr;
    lv_obj_t* dir_e_label_ = nullptr;
    lv_obj_t* dir_s_label_ = nullptr;
    lv_obj_t* dir_w_label_ = nullptr;

    lv_obj_t* layer1_container_ = nullptr;
    lv_obj_t* layer2_inner_ring_ = nullptr;
    lv_obj_t* layer3_bg_arc_ = nullptr;
    lv_obj_t* layer3_progress_arc_ = nullptr;
    lv_obj_t* layer4_outer_ring_ = nullptr;

    float current_pitch_ = 0.0f;
    float current_roll_ = 0.0f;
    float current_yaw_ = 0.0f;
    int current_state_level_ = 0;

    void CreateBackground();
    void CreateLayer0Taiji();
    void CreateLayer1CoreInfo();
    void CreateLayer2DynamicIndicator();
    void CreateLayer3StatusProgress();
    void CreateLayer4Boundary();
    void CreateCompassPoints();
    void UpdateStateColor(int level);
};

#endif // ATTITUDE_DISPLAY_H
