#ifndef ATTITUDE_DISPLAY_H
#define ATTITUDE_DISPLAY_H

#include "lcd_display.h"
#include "attitude_theme.h"
#include <string>

#define SCREEN_W              360
#define SCREEN_H              360
#define ATTITUDE_CENTER_X     180
#define ATTITUDE_CENTER_Y     180
#define VALID_RADIUS          170
#define ANIM_DURATION         300

#define COLOR_BG_OUTER        lv_color_hex(0x0A0A0A)
#define COLOR_BG_CENTER       lv_color_hex(0x121212)

#define COLOR_TEXT_MAIN       lv_color_hex(0xD4AF37)
#define COLOR_TEXT_SUB        lv_color_hex(0xC0C0C0)
#define COLOR_TEXT_HIGH       lv_color_hex(0xFFFFFF)

#define COLOR_BORDER_LINE     lv_color_hex(0xD4AF37)
#define COLOR_STATE_HEAVY     lv_color_hex(0xE67E22)
#define COLOR_STATE_DANGER    lv_color_hex(0xB82601)

#define LAYER1_CORE_RADIUS    49
#define LAYER3_PROGRESS_RADIUS 130
#define LAYER3_BG_ARC_RADIUS   117
#define LAYER3_PROGRESS_ARC_RADIUS 126
#define LAYER4_BOUNDARY_RADIUS 170   // 外圈直径 340
#define LAYER4_OUTER_SIZE      (LAYER4_BOUNDARY_RADIUS * 2)

// 迭代 1: 鱼眼 + 太极（R=86，相对原 96 再缩 90%）
#define TAIJI_RADIUS          86
#define TAIJI_CANVAS_SIZE     (TAIJI_RADIUS * 2)
#define FISHEYE_ICON_SIZE     (TAIJI_RADIUS / 3)   // eye_r = R/6
#define FISHEYE_PULSE_MS      300
#define TAIJI_GOLD_RING_WIDTH 4
#define GOLD_RING_ARC_WIDTH   3
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

// 迭代 2: AI 运势三态状态机
enum class FortuneState {
    Idle = 0,
    Animating = 1,
    Result = 2,
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

    // 迭代 2: 运势三态 + 结果卡（dir_index: 0=N 1=E 2=S 3=W；gua_index: 0~63）
    void ShowFortune(const std::string& func_label, const std::string& gua_name,
                     const std::string& core_text, const std::string& yi, const std::string& ji,
                     int gua_index, int dir_index);
    void EnterAnimatingState();
    void EnterResultState();
    void EnterIdleState();
    void HighlightDirection(int dir);
    void HighlightGua(int gua_idx);
    void CreateFortuneCard();
    void DismissFortune();
    FortuneState GetFortuneState() const { return fortune_state_; }
    /** Boot 键：Idle 触发演示运势，Result 关闭卡片；返回 true 表示已消费按键 */
    bool HandleBootKey();

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
    lv_obj_t* layer3_bg_arc_ = nullptr;
    lv_obj_t* layer3_progress_arc_ = nullptr;
    lv_obj_t* layer4_outer_ring_ = nullptr;
    lv_obj_t* taiji_gold_pulse_arc_ = nullptr;  // 运势 Animating 时太极金边脉冲（随 taiji_container_ 旋转）

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
    void StartFisheyeBorderPulse(lv_obj_t* obj, uint32_t c1, uint32_t c2);
    void StopFisheyePulse(lv_obj_t* obj);
    void ApplyWifiFisheyeStyle(WifiStatus status);
    void ApplyBleFisheyeStyle(BleStatus status);

    void CreateBackground();
    void CreateLayer0Taiji();
    void CreateTaijiGoldPulseArc();
    void CreateLayer1CoreInfo();
    void CreateLayer3StatusProgress();
    void CreateLayer4Boundary();
    void CreateCompassPoints();
    void UpdateStateColor(int level);

    FortuneState fortune_state_ = FortuneState::Idle;
    lv_timer_t* fortune_anim_timer_ = nullptr;
    lv_timer_t* fortune_result_timer_ = nullptr;
    lv_timer_t* fortune_fisheye_pulse_timer_ = nullptr;
    lv_timer_t* fortune_progress_timer_ = nullptr;
    int fortune_progress_step_ = 0;

    lv_obj_t* fortune_card_ = nullptr;
    lv_obj_t* fortune_func_label_ = nullptr;
    lv_obj_t* fortune_gua_widget_ = nullptr;
    lv_obj_t* fortune_gua_name_label_ = nullptr;
    lv_obj_t* fortune_core_label_ = nullptr;
    lv_obj_t* fortune_yi_label_ = nullptr;
    lv_obj_t* fortune_ji_label_ = nullptr;
    lv_obj_t* fortune_close_label_ = nullptr;

    std::string fortune_func_label_text_;
    std::string fortune_gua_name_text_;
    std::string fortune_core_text_;
    std::string fortune_yi_text_;
    std::string fortune_ji_text_;
    int fortune_gua_index_ = 0;
    int fortune_highlight_dir_ = 0;
    int fortune_fisheye_pulse_count_ = 0;
    bool fortune_fisheye_pulse_gold_ = true;
    lv_timer_t* fortune_taiji_ramp_timer_ = nullptr;
    lv_timer_t* fortune_result_delay_timer_ = nullptr;
    uint32_t fortune_taiji_ramp_start_tick_ = 0;

    void DestroyFortuneCard();
    void StopFortuneAnimatingEffects(bool restore_taiji_rotation = true);
    void StartFortuneTaijiPhasedRotation();
    void EnsureDirectionDot(int dir);
    void HideDirectionDots();
    lv_obj_t* GetDirectionDot(int dir);
    static void OnFortuneAnimTimer(lv_timer_t* timer);
    static void OnFortuneResultDelayTimer(lv_timer_t* timer);
    static void OnFortuneResultTimer(lv_timer_t* timer);
    static void OnFortuneFisheyePulseTimer(lv_timer_t* timer);
    static void OnFortuneProgressStepTimer(lv_timer_t* timer);
    static void OnFortuneTaijiRampTimer(lv_timer_t* timer);
};

#endif // ATTITUDE_DISPLAY_H
