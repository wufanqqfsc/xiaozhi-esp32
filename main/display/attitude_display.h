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
#define LAYER3_PROGRESS_RADIUS 144
#define LAYER4_BOUNDARY_RADIUS 178

// 迭代 1: 鱼眼状态图标 — 作为太极阴阳鱼眼，与太极一体旋转
#define FISHEYE_ICON_SIZE     36
#define FISHEYE_PULSE_MS      300
// 太极外径：鱼眼直径 = ICON_SIZE，按经典太极比例 eye_r = R/6 → R = ICON_SIZE/2 * 6 = 108
#define TAIJI_RADIUS          ((FISHEYE_ICON_SIZE / 2) * 6)
#define TAIJI_CANVAS_SIZE     (TAIJI_RADIUS * 2)
// Layer2 鎏金细环：太极外缘与外圈边框之间的中线 (108+178)/2=143
#define LAYER2_ARC_RADIUS     ((TAIJI_RADIUS + LAYER4_BOUNDARY_RADIUS) / 2)
#define LAYER2_INDIC_RADIUS   LAYER2_ARC_RADIUS
#define GOLD_RING_ARC_WIDTH   2   // 鎏金 arc 线宽（LVGL 抗锯齿 + 圆角端点）
// 鱼眼在 taiji_container_ 内的局部坐标（上眼=阴中阳/WiFi，下眼=阳中阴/BLE）
#define FISHEYE_WIFI_LOCAL_X  (TAIJI_RADIUS - (FISHEYE_ICON_SIZE / 2))
#define FISHEYE_WIFI_LOCAL_Y  (TAIJI_RADIUS / 2 - (FISHEYE_ICON_SIZE / 2))
#define FISHEYE_BLE_LOCAL_X   FISHEYE_WIFI_LOCAL_X
#define FISHEYE_BLE_LOCAL_Y   (TAIJI_RADIUS + TAIJI_RADIUS / 2 - (FISHEYE_ICON_SIZE / 2))

enum class WifiStatus {
    DISCONNECTED = 0,
    CONNECTING = 1,
    CONNECTED = 2,
};

enum class BleStatus {
    DISABLED = 0,
    ADVERTISING = 1,
    CONNECTED = 2,
};

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

    void UpdateWifiFisheye(WifiStatus status);
    void UpdateBleFisheye(BleStatus status);
    WifiStatus GetWifiFisheyeStatus() const { return wifi_status_; }
    BleStatus GetBleFisheyeStatus() const { return ble_status_; }

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

    lv_obj_t* wifi_fisheye_ = nullptr;
    lv_obj_t* wifi_fisheye_icon_ = nullptr;
    lv_obj_t* ble_fisheye_ = nullptr;
    lv_obj_t* ble_fisheye_icon_ = nullptr;
    WifiStatus wifi_status_ = WifiStatus::DISCONNECTED;
    BleStatus ble_status_ = BleStatus::DISABLED;

    void CreateWifiFisheye();
    void CreateBleFisheye();
    void StartFisheyePulse(lv_obj_t* obj);
    void StopFisheyePulse(lv_obj_t* obj);
    void ApplyWifiFisheyeStyle(WifiStatus status);
    void ApplyBleFisheyeStyle(BleStatus status);

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
