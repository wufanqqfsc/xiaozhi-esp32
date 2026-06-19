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
// 鱼眼：32px（较原 26px 整体放大约 23%，图标字体 20px，整体视觉 +20%）
#define FISHEYE_ICON_SIZE     32
static_assert(TAIJI_RADIUS == 86, "TAIJI_RADIUS must be 86 (172px canvas, per product spec)");
static_assert(FISHEYE_ICON_SIZE == 32, "FISHEYE_ICON_SIZE must be 32px (~37% of Taiji radius, +20% from 26px)");
#define FISHEYE_PULSE_MS      300
#define TAIJI_GOLD_RING_WIDTH 3
#define FISHEYE_BORDER_WIDTH  2

// 功能显示区（短按功能图标后的提示卡）：356px 直径，覆盖外部圆环边界
#define DEBUG_INFO_CARD_SIZE     LAYER4_OUTER_SIZE
#define DEBUG_INFO_CARD_W        DEBUG_INFO_CARD_SIZE
#define DEBUG_INFO_CARD_H        DEBUG_INFO_CARD_SIZE
#define DEBUG_INFO_CARD_X        (ATTITUDE_CENTER_X - DEBUG_INFO_CARD_W / 2)
#define DEBUG_INFO_CARD_Y        (ATTITUDE_CENTER_Y - DEBUG_INFO_CARD_H / 2)
#define DEBUG_INFO_CARD_RADIUS   (DEBUG_INFO_CARD_W / 2)

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
// 阴鱼圆心 (TAIJI_RADIUS, TAIJI_RADIUS/2) = (86, 43)，20px 鱼眼居中放置
#define FISHEYE_WIFI_LOCAL_X  (TAIJI_RADIUS - FISHEYE_ICON_SIZE / 2)
#define FISHEYE_WIFI_LOCAL_Y  (TAIJI_RADIUS / 2 - FISHEYE_ICON_SIZE / 2)
// 阳鱼圆心 (TAIJI_RADIUS, TAIJI_RADIUS * 3 / 2) = (86, 129)
#define FISHEYE_BLE_LOCAL_X   FISHEYE_WIFI_LOCAL_X
#define FISHEYE_BLE_LOCAL_Y   (TAIJI_RADIUS + TAIJI_RADIUS / 2 - FISHEYE_ICON_SIZE / 2)

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

// 运势菜单状态已彻底简化为：始终为 Idle（Plan A 结果卡与 Animating/Result 状态机已删除）

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

// 心情卦：迷宫游戏子功能
#ifndef MOOD_GUA_SUB_FEATURES_ENABLED
#define MOOD_GUA_SUB_FEATURES_ENABLED 1
#endif

#if MOOD_GUA_SUB_FEATURES_ENABLED
/** 心情卦（序号 4）L0 子态 */
enum class MoodGuaSubState {
    Hidden = 0,      // 显示太极鱼
    MazePlaying = 1, // 迷宫游戏模式
};

#define MAZE_GRID_SIZE       9        // 9x9 网格
#define MAZE_CANVAS_SIZE     (TAIJI_CANVAS_SIZE - 20) // 留出边缘
#define MAZE_CELL_SIZE       (MAZE_CANVAS_SIZE / MAZE_GRID_SIZE)
#define MAZE_WALL_WIDTH      2        // 墙壁线宽
#define MAZE_PLAYER_SIZE     (MAZE_CELL_SIZE - 4)
#endif  // MOOD_GUA_SUB_FEATURES_ENABLED

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

    // 运势菜单（短按选中、长按确认；结果卡 Plan A 已彻底删除）
    void EnterIdleState();

    // 调试信息卡：把与后台交互的关键事件短时显示在太极圈内
    // hold_ms: 显示持续时间；调用方若同步播放音频，应传入音频可覆盖的时长
    void ShowDebugInfo(const std::string& title, const std::string& detail, uint32_t hold_ms = 3000);
    void HideDebugInfo();
    /** Boot 短按：Idle 循环选中运势入口 */
    bool HandleBootKey();
    /** Boot 长按：Idle 触发当前选中运势（确定） */
    bool HandleFortuneBootLongPress();
    /** 电源键短按：返回/取消 - 取消选中、隐藏功能区 */
    bool HandlePowerKey();

private:
    lv_obj_t* attitude_container_ = nullptr;

    lv_obj_t* background_ = nullptr;
    lv_obj_t* bg_layer_center_ = nullptr;
    lv_obj_t* bg_inner_glow_ = nullptr;

    lv_obj_t* screen_border_ = nullptr;

    lv_obj_t* circle_outer_ = nullptr;
    lv_obj_t* circle_mid_ = nullptr;
    lv_obj_t* circle_inner_ = nullptr;

    lv_obj_t* layer1_container_ = nullptr;
    lv_obj_t* layer3_bg_arc_ = nullptr;
    lv_obj_t* layer3_progress_arc_ = nullptr;
    lv_obj_t* layer4_outer_ring_ = nullptr;

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
    lv_obj_t* wifi_fisheye_icon_ = nullptr;
    lv_obj_t* wifi_fisheye_canvas_ = nullptr;
    lv_obj_t* ble_fisheye_ = nullptr;
    lv_obj_t* ble_fisheye_icon_ = nullptr;
    lv_obj_t* ble_fisheye_canvas_ = nullptr;
    WifiStatus wifi_status_ = WifiStatus::DISCONNECTED;
    BleStatus ble_status_ = BleStatus::DISABLED;

    void CreateWifiFisheye();
    void CreateBleFisheye();
    void RedrawWifiFisheyeCanvas();
    void RedrawBleFisheyeCanvas();
    void StartFisheyePulse(lv_obj_t* obj);
    void StartFisheyeBorderPulse(lv_obj_t* obj, uint32_t c1, uint32_t c2);
    void StopFisheyePulse(lv_obj_t* obj);
    void ApplyWifiFisheyeStyle(WifiStatus status);
    void ApplyBleFisheyeStyle(BleStatus status);
    void UpdateWifiFisheyeBorderColor(WifiStatus status);
    void UpdateBleFisheyeBorderColor(BleStatus status);

    void CreateBackground();
    void CreateLayer0Taiji();
    void CreateLayer1CoreInfo();
    void CreateLayer3StatusProgress();
    void CreateLayer4Boundary();
    void CreateFortuneMenuRing();
    void CreateFortuneMenuRingTouch();
    void SetFortuneMenuVisible(bool visible);
    void SelectFortuneMenuItem(int index);
    void SelectFortuneMenuItemUnlocked(int index);
    void DeselectFortuneMenuItemUnlocked();
    void UpdateFortuneMenuSelection();
    void UpdateFortuneMenuItemVisual(int index, bool selected);
    void CycleFortuneMenuSelection();
    void CycleFortuneMenuSelectionUnlocked();
    void PlayFortuneMenuSelectSound();
#if STUDY_SUB_FEATURES_ENABLED
    void CreateStudyArea();
#endif
    void SyncStudyAreaWithMenu();
    void SetTaijiCoreVisible(bool visible);
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
    void ApplyStudyAreaOrientation(bool study_active);
    void UpdateStudyFocusDisplay(int remaining_ms);
    void OnStudyFocusComplete();
    void PlayStudyFocusCompleteBgm();
#else
    void ExitStudyArea() {}
#endif
#if MOOD_GUA_SUB_FEATURES_ENABLED
    void CreateMoodGuaArea();
    void EnterMaze();
    void ExitMoodGuaArea();
    void SyncMoodGuaAreaWithMenu();
    void GenerateMaze();
    void DrawMazeOnCanvas();
    void MoveMazePlayer(int drow, int dcol);
    void RedrawMazePlayer();
    void OnMazeTouched(lv_event_t* e);
    static void OnMazeTouchedStatic(lv_event_t* e);
    void OnMazeWon();
#endif
    void UpdateStateColor(int level);

    // 调试信息卡（短时显示在太极圈内）：title + detail 两行
    lv_obj_t* function_area_card_ = nullptr;
    lv_obj_t* debug_info_title_ = nullptr;
    lv_obj_t* debug_info_detail_ = nullptr;
    lv_timer_t* debug_info_hide_timer_ = nullptr;
    uint32_t debug_info_last_show_ms_ = 0;
    std::string debug_info_last_title_;
    void CreateDebugInfoCard();
    void DestroyDebugInfoCard();
    void ApplyDebugInfoCardLayout();
    struct DebugInfoPresentOpts {
        bool persistent = false;
    };
    void PresentDebugInfoCardUnlocked(const std::string& title, const std::string& detail,
                                      uint32_t hold_ms, const DebugInfoPresentOpts& opts);
    void HideDebugInfoUnlocked();
    static void OnDebugInfoHideTimer(lv_timer_t* timer);

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
#if MOOD_GUA_SUB_FEATURES_ENABLED
    MoodGuaSubState mood_gua_sub_state_ = MoodGuaSubState::Hidden;
    lv_obj_t* mood_gua_panel_ = nullptr;
    lv_obj_t* maze_canvas_ = nullptr;
    lv_obj_t* maze_player_obj_ = nullptr;
    lv_obj_t* maze_title_label_ = nullptr;
    lv_obj_t* maze_end_label_ = nullptr;
    bool maze_walls_[MAZE_GRID_SIZE][MAZE_GRID_SIZE][4] = {}; // [row][col] = {top, right, bottom, left}
    int maze_player_row_ = 0;
    int maze_player_col_ = 0;
    int maze_end_row_ = MAZE_GRID_SIZE - 1;
    int maze_end_col_ = MAZE_GRID_SIZE - 1;
    bool mood_gua_had_auto_rotation_ = false;
    int mood_gua_saved_rotation_period_ms_ = 60000;
#endif

#if STUDY_SUB_FEATURES_ENABLED
    static void OnStudyFocusTimer(lv_timer_t* timer);
    static void OnStudyCompleteBgmTimer(lv_timer_t* timer);
    static void OnStudyMenuIconClicked(lv_event_t* e);
#endif
#if MOOD_GUA_SUB_FEATURES_ENABLED
    // 迷宫游戏的回调在上面已声明
#endif
};

#endif // ATTITUDE_DISPLAY_H
