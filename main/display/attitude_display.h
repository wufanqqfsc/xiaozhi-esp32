#ifndef ATTITUDE_DISPLAY_H
#define ATTITUDE_DISPLAY_H

#include "lcd_display.h"
#include "attitude_theme.h"
#include <string>

#define SCREEN_W              360
#define SCREEN_H              360
#define ATTITUDE_CENTER_X     180
#define ATTITUDE_CENTER_Y     180
#define GOLD_RING_ARC_WIDTH   3
#define VALID_RADIUS          (SCREEN_W / 2 - GOLD_RING_ARC_WIDTH / 2)
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
#define LAYER4_BOUNDARY_RADIUS (SCREEN_W / 2 - GOLD_RING_ARC_WIDTH / 2)  // 贴屏幕圆边
#define LAYER4_OUTER_SIZE      (LAYER4_BOUNDARY_RADIUS * 2)

// 太极 + 鱼眼（外径 R=86，直径 172px，与规格/验收一致）
#define TAIJI_RADIUS          86
#define TAIJI_CANVAS_SIZE     (TAIJI_RADIUS * 2)
#define FISHEYE_ICON_SIZE     (TAIJI_RADIUS / 3)   // eye_r = R/6
static_assert(TAIJI_RADIUS == 86, "TAIJI_RADIUS must be 86 (172px canvas, per product spec)");
static_assert(FISHEYE_ICON_SIZE == 28, "FISHEYE_ICON_SIZE must be 28px at R=86");
#define FISHEYE_PULSE_MS      300
#define TAIJI_GOLD_RING_WIDTH 3
#define FISHEYE_BORDER_WIDTH  2

// 结果卡与太极 canvas 同尺寸、同中心
#define FORTUNE_CARD_SIZE       TAIJI_CANVAS_SIZE
#define FORTUNE_CARD_X          (ATTITUDE_CENTER_X - FORTUNE_CARD_SIZE / 2)
#define FORTUNE_CARD_Y          (ATTITUDE_CENTER_Y - FORTUNE_CARD_SIZE / 2)

// 运势功能环：35px 视觉；环心相对中点外偏 3px
#define FORTUNE_MENU_COUNT           12
#define FORTUNE_MENU_ICON_GLYPH_PX   35
#define FORTUNE_MENU_ICON_BASE_PX     30
#define FORTUNE_MENU_ICON_SCALE      ((FORTUNE_MENU_ICON_GLYPH_PX * 256 + FORTUNE_MENU_ICON_BASE_PX / 2) / FORTUNE_MENU_ICON_BASE_PX)
#define FORTUNE_MENU_ICON_SCALE_SELECTED ((FORTUNE_MENU_ICON_GLYPH_PX * 11 * 256 / 10 + FORTUNE_MENU_ICON_BASE_PX / 2) / FORTUNE_MENU_ICON_BASE_PX)
#define FORTUNE_MENU_RING_OUTWARD_PX 3
#define FORTUNE_MENU_RING_RADIUS     ((TAIJI_RADIUS + LAYER4_BOUNDARY_RADIUS) / 2 - GOLD_RING_ARC_WIDTH + FORTUNE_MENU_RING_OUTWARD_PX)
#define FORTUNE_MENU_START_ANGLE_DEG (-90)  // 12 点钟起，顺时针
// 太极外缘 ~ L4 内缘整环可点（略放宽便于触摸）
#define FORTUNE_MENU_TOUCH_INNER_R   (TAIJI_RADIUS - 4)
#define FORTUNE_MENU_TOUCH_OUTER_R   LAYER4_BOUNDARY_RADIUS

enum class FortuneMenuType : int {
    Today = 0,      // fortune.today
    Wealth = 1,     // fortune.wealth
    Career = 2,     // fortune.career
    Love = 3,       // fortune.love
    MoodGua = 4,    // fortune.mood_gua
    Huangli = 5,    // fortune.huangli
    SolarTerm = 6,  // fortune.solar_term
    Custom = 7,     // fortune.custom
    Health = 8,     // fortune.health
    Study = 9,      // fortune.study
    Travel = 10,    // fortune.travel
    Noble = 11,     // fortune.noble
};

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

#ifndef STUDY_SUB_FEATURES_ENABLED
#define STUDY_SUB_FEATURES_ENABLED 0
#endif

#if STUDY_SUB_FEATURES_ENABLED
/** 学业运势（序号 9）L0 子功能 */
enum class StudyMenuItem {
    Timer = 0,  // 专注计时（默认选中）
    Drum = 1,   // 架子鼓（占位）
};

/** 学业运势（序号 9）L0 子态 */
enum class StudySubState {
    Hidden = 0,       // 显示太极鱼
    Menu = 1,         // 时钟 + 架子鼓图标
    FocusRunning = 2, // 60s 专注计时环
    CompleteBgm = 3,  // 计时结束，播放约 5s 背景音乐
    DrumPad = 4,      // 架子鼓模式（8 扇区触摸 + 中心 Kick）
};

#define STUDY_FOCUS_DURATION_MS   60000
#define STUDY_FOCUS_BGM_DURATION_MS 5000
#define STUDY_FOCUS_TICK_MS       200
#define STUDY_FOCUS_ARC_RING_GAP  5   // 计时环外缘与太极鎏金环内缘的最小间距 (px)
#define STUDY_FOCUS_ARC_OUTER_MAX_R (TAIJI_RADIUS - TAIJI_GOLD_RING_WIDTH - STUDY_FOCUS_ARC_RING_GAP)
#define STUDY_FOCUS_ARC_TRACK_WIDTH    24   // 底轨 +50%（16→24）
#define STUDY_FOCUS_ARC_INDICATOR_WIDTH 27  // 进度条 +50%（18→27）
#define STUDY_FOCUS_ARC_RADIUS_NOMINAL ((TAIJI_RADIUS * 78 + 50) / 100)  // 约 78% 太极外径
#define STUDY_FOCUS_ARC_RADIUS_TARGET  ((STUDY_FOCUS_ARC_RADIUS_NOMINAL * 15 + 5) / 10)
#define STUDY_FOCUS_ARC_RADIUS  \
    ((STUDY_FOCUS_ARC_RADIUS_TARGET <= STUDY_FOCUS_ARC_OUTER_MAX_R) \
        ? STUDY_FOCUS_ARC_RADIUS_TARGET : STUDY_FOCUS_ARC_OUTER_MAX_R)
#define STUDY_FOCUS_ARC_SIZE      (STUDY_FOCUS_ARC_RADIUS * 2)
// 区内图标比外围运势环大 50%（35px → 53px 视觉）
#define STUDY_ICON_GLYPH_PX       ((FORTUNE_MENU_ICON_GLYPH_PX * 15 + 5) / 10)
#define STUDY_ICON_SCALE          ((STUDY_ICON_GLYPH_PX * 256 + FORTUNE_MENU_ICON_BASE_PX / 2) / FORTUNE_MENU_ICON_BASE_PX)
#define STUDY_ICON_SCALE_SELECTED ((STUDY_ICON_GLYPH_PX * 11 * 256 / 10 + FORTUNE_MENU_ICON_BASE_PX / 2) / FORTUNE_MENU_ICON_BASE_PX)
#define STUDY_ICON_OFFSET_Y       40  // 时钟/鼓图标相对中心的纵向间距
#define STUDY_AREA_ROTATION_OFFSET_TENTH_DEG 900  // 学业功能区整体顺时针旋转 90°
#endif  // STUDY_SUB_FEATURES_ENABLED

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
    /** 按服务端 8 类运势入口触发（菜单环 / MCP） */
    void ShowFortuneFromMenu(FortuneMenuType type);
    void EnterAnimatingState();
    void EnterResultState();
    void EnterIdleState();
    void HighlightDirection(int dir);
    void HighlightGua(int gua_idx);
    void CreateFortuneCard();
    void DismissFortune();
    FortuneState GetFortuneState() const { return fortune_state_; }

    // 调试信息卡：把与后台交互的关键事件短时显示在太极圈内
    // hold_ms: 显示持续时间；调用方若同步播放音频，应传入音频可覆盖的时长
    void ShowDebugInfo(const std::string& title, const std::string& detail, uint32_t hold_ms = 3000);
    void HideDebugInfo();
    /** Boot 短按：Idle 循环选中运势入口；Result 关闭卡片 */
    bool HandleBootKey();
    /** Boot 长按：Idle 触发当前选中运势 */
    bool HandleFortuneBootLongPress();
    /** 电源键短按：学业区内任意子态退出并恢复完整太极圈 */
    bool HandleStudyPowerKey();

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
    lv_obj_t* fortune_prompt_title_ = nullptr;
    lv_obj_t* layer3_bg_arc_ = nullptr;
    lv_obj_t* layer3_progress_arc_ = nullptr;
    lv_obj_t* layer4_outer_ring_ = nullptr;
    lv_obj_t* taiji_gold_pulse_arc_ = nullptr;  // 运势 Animating 时太极金边脉冲（随 taiji_container_ 旋转）

    lv_obj_t* fortune_menu_ring_touch_ = nullptr;
    lv_obj_t* fortune_menu_labels_[FORTUNE_MENU_COUNT] = {};
    int fortune_menu_center_x_[FORTUNE_MENU_COUNT] = {};
    int fortune_menu_center_y_[FORTUNE_MENU_COUNT] = {};
    int fortune_menu_selected_index_ = 0;
    bool fortune_menu_selection_active_ = false;
    int fortune_menu_applied_scale_[FORTUNE_MENU_COUNT] = {};

    float current_pitch_ = 0.0f;
    float current_roll_ = 0.0f;
    float current_yaw_ = 0.0f;
    int current_state_level_ = 0;

    lv_obj_t* wifi_fisheye_ = nullptr;
    lv_obj_t* wifi_fisheye_canvas_ = nullptr;
    lv_obj_t* wifi_fisheye_icon_ = nullptr;
    lv_obj_t* ble_fisheye_ = nullptr;
    lv_obj_t* ble_fisheye_canvas_ = nullptr;
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
    void RedrawWifiFisheyeCanvas();
    void RedrawBleFisheyeCanvas();

    void CreateBackground();
    void CreateLayer0Taiji();
    void CreateTaijiGoldPulseArc();
    void CreateLayer1CoreInfo();
    void CreateLayer3StatusProgress();
    void CreateLayer4Boundary();
    void CreateFortuneMenuRing();
    void CreateFortuneMenuRingTouch();
    int FortuneMenuIndexFromPoint(int x, int y) const;
    void SetFortuneMenuVisible(bool visible);
    void SelectFortuneMenuItem(int index);
    void SelectFortuneMenuItemUnlocked(int index);
    void UpdateFortuneMenuSelection();
    void UpdateFortuneMenuItemVisual(int index, bool selected);
    void CycleFortuneMenuSelection();
    void CycleFortuneMenuSelectionUnlocked();
    void PlayFortuneMenuSelectSound();
#if STUDY_SUB_FEATURES_ENABLED
    void CreateStudyArea();
#endif
    void SyncStudyAreaWithMenu();
    void ShowFortuneMenuFeatureCard(int index);
    void ShowFortuneMenuFeatureCardUnlocked(int index);
#if STUDY_SUB_FEATURES_ENABLED
    void EnterStudyMenu();
    void ExitStudyArea();
    void ShowStudyMenuPanel();
    void UpdateStudyMenuSelection();
    void SelectStudyMenuItem(StudyMenuItem item);
    void EnterDrumPad();
    void ExitDrumPad();
    void ShowDrumPadUI();
    void HideDrumPadUI();
    void OnDrumPadTouched(lv_event_t* e);
    static void OnDrumPadTouchedStatic(lv_event_t* e);
    void FlashDrumSector(int piece_idx);
    void StartStudyFocusTimer();
    void StopStudyFocusTimer();
    void CancelStudyFocusToMenu();
    void StopStudyCompleteBgm();
    void SetTaijiCoreVisible(bool visible);
    void ApplyStudyAreaOrientation(bool study_active);
    void UpdateStudyFocusDisplay(int remaining_ms);
    void OnStudyFocusComplete();
    void PlayStudyFocusCompleteBgm();
#else
    void ExitStudyArea() {}
#endif
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

    // 调试信息卡（与后台交互的关键事件）：短时显示在太极圈内
    lv_obj_t* debug_info_card_ = nullptr;
    lv_obj_t* debug_info_title_ = nullptr;
    lv_obj_t* debug_info_detail_ = nullptr;
    lv_obj_t* debug_info_gua_label_ = nullptr;
    lv_obj_t* debug_info_core_label_ = nullptr;
    lv_obj_t* debug_info_yi_label_ = nullptr;
    lv_obj_t* debug_info_ji_label_ = nullptr;
    lv_timer_t* debug_info_hide_timer_ = nullptr;
    uint32_t debug_info_last_show_ms_ = 0;
    std::string debug_info_last_title_;
    std::string debug_info_fortune_title_;
    void CreateDebugInfoCard();
    void DestroyDebugInfoCard();
    void ApplyDebugInfoCardLayout();
    void EnsureFortunePromptTitle();
    void HideFortunePromptTitle();
    void HideDebugInfoCardLabels();
    struct DebugInfoPresentOpts {
        bool persistent = false;
        /** 运势 L0 短提示：文字画在 screen 顶层，卡片仅作半透明底 */
        bool screen_title_overlay = false;
    };
    void PresentDebugInfoCardUnlocked(const std::string& title, const std::string& detail,
                                      uint32_t hold_ms, const DebugInfoPresentOpts& opts);
    void HideDebugInfoUnlocked();
    static void OnDebugInfoHideTimer(lv_timer_t* timer);
    bool debug_info_is_fortune_feature_ = false;

#if STUDY_SUB_FEATURES_ENABLED
    StudySubState study_sub_state_ = StudySubState::Hidden;
    StudyMenuItem study_menu_selected_ = StudyMenuItem::Timer;
    lv_obj_t* study_panel_ = nullptr;
    lv_obj_t* study_clock_label_ = nullptr;
    lv_obj_t* study_drum_label_ = nullptr;
    lv_obj_t* study_focus_arc_ = nullptr;

    lv_obj_t* drum_pad_ = nullptr;
    lv_obj_t* drum_center_ = nullptr;
    lv_obj_t* drum_sectors_[8] = {nullptr};
    lv_obj_t* drum_sector_labels_[8] = {nullptr};
    int drum_flash_timer_id_ = -1;
    uint32_t drum_flash_start_ms_ = 0;
    lv_obj_t* study_time_label_ = nullptr;
    lv_timer_t* study_focus_timer_ = nullptr;
    lv_timer_t* study_complete_bgm_timer_ = nullptr;
    uint32_t study_focus_start_tick_ = 0;
    bool study_had_auto_rotation_ = false;
    int study_saved_rotation_period_ms_ = 60000;
    int study_saved_taiji_rotation_ = 0;
    bool study_orientation_applied_ = false;
#endif
    /** 电源键关闭功能提示卡后抑制重显，直至切换到其他运势项 */
    bool fortune_feature_card_suppressed_ = false;

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
    static void OnFortuneMenuRingTouched(lv_event_t* e);
#if STUDY_SUB_FEATURES_ENABLED
    static void OnStudyFocusTimer(lv_timer_t* timer);
    static void OnStudyCompleteBgmTimer(lv_timer_t* timer);
    static void OnStudyMenuIconClicked(lv_event_t* e);
#endif
};

#endif // ATTITUDE_DISPLAY_H
