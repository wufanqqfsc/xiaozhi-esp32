#ifndef ATTITUDE_DISPLAY_H
#define ATTITUDE_DISPLAY_H

#include "lcd_display.h"
#include "lvgl_image.h"
#include "lvgl_display/gif/gif_preview_player.h"
#include <string>
#include <deque>
#include <vector>
#include <cstddef>
#include <lvgl.h>

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

#define COLOR_BT_BLUE         lv_color_hex(0x2196F3)
#define COLOR_WIFI_GREEN      lv_color_hex(0x00FFFF) // 提高亮度，原为 0x00C8C8

// ---------------------------------------------------------------------------
// 视图状态：spec 6.1 视图状态枚举
// ---------------------------------------------------------------------------
enum class ActiveView {
    Compass,        // 罗盘主界面
    JarvisWatchface,// JARVIS HUD 视图
    Divination,     // 占卜视图
};

// ---------------------------------------------------------------------------
// 视图栈：spec 6.2 视图栈结构
// ---------------------------------------------------------------------------
struct ViewStack {
    std::vector<ActiveView> stack;
    ActiveView current() const { return stack.empty() ? ActiveView::Compass : stack.back(); }
    void push(ActiveView view) { stack.push_back(view); }
    void pop() {
        if (!stack.empty()) {
            stack.pop_back();
        }
    }
    // 仅当 view 在栈顶时才弹出，避免 contains()+pop() 误删其它元素
    bool pop_if_top(ActiveView view) {
        if (!stack.empty() && stack.back() == view) {
            stack.pop_back();
            return true;
        }
        return false;
    }
    void clear() { stack.clear(); }
    bool contains(ActiveView view) const {
        for (auto v : stack) {
            if (v == view) return true;
        }
        return false;
    }
};

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
// 注：功能图标选中不再弹出信息卡，此区域仅用于系统通知/状态等 DebugInfo
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

// 今日占卜：太极中心 / Boot 长按 3s 触发，15s 时间轴驱动跑马灯，按住可延长
#define FORTUNE_DIVINATION_HOLD_MS           3000
#define FORTUNE_DIVINATION_DURATION_MS       30000
#define FORTUNE_DIVINATION_RELEASE_FINISH_MS 5000
#define FORTUNE_DIVINATION_TICK_MS           25
#define FORTUNE_DIVINATION_SOUND_INTERVAL_MS 8745
#define FORTUNE_DIVINATION_HIGHLIGHT_COUNT   3

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

enum class FortuneDivinationState {
    Idle = 0,
    Animating = 1,
    Result = 2,
};

// 调试信息优先级（数值越大优先级越高）
enum class DebugInfoPriority : int {
    LOW = 0,      // 工具调用(2500ms)
    MEDIUM = 1,   // 识别到/失败(5000ms)
    HIGH = 2,     // WiFi已连接/握手成功(5000ms)
    CRITICAL = 3  // 唤醒成功(30000ms)
};

// 调试信息队列项
struct DebugInfoItem {
    std::string title;
    std::string detail;
    uint32_t hold_ms;
    DebugInfoPriority priority;
    lv_timer_t* timer;
    uint64_t enqueue_tick;  // 入队时间戳，用于去重
};

#define DEBUG_INFO_MAX_QUEUE_SIZE 5

class AttitudeDisplay : public SpiLcdDisplay {
public:
    AttitudeDisplay(esp_lcd_panel_io_handle_t panel_io,
                   esp_lcd_panel_handle_t panel,
                   int width, int height,
                   int offset_x, int offset_y,
                   bool mirror_x, bool mirror_y, bool swap_xy);

    virtual ~AttitudeDisplay();

    // 释放鱼眼预渲染 buffer 资源
    void DestroyFisheyeResources();

    virtual void SetupUI() override;
    virtual void SetTheme(Theme* theme) override;
    virtual void UpdateStatusBar(bool update_all = false) override;

    // 覆盖基类的 UI 显示方法：AttitudeDisplay 使用自己的 UI 架构（太极 + 鱼眼 + DebugInfoCard）
    // 不使用 LcdDisplay 的 status_bar_/notification_label_/chat_message_label_/emoji_image_
    // 重写这些方法避免调用父类时触发 "label is nullptr" 警告，并改用 ShowDebugInfo 提示用户
    virtual void ShowNotification(const char* notification, int duration_ms = 3000) override;
    virtual void ShowNotification(const std::string& notification, int duration_ms = 3000) override;
    virtual void SetStatus(const char* status) override;
    virtual void SetEmotion(const char* emotion) override;
    virtual void SetChatMessage(const char* role, const char* content) override;
    virtual void ClearChatMessages() override;
    virtual void SetPreviewImage(std::unique_ptr<LvglImage> image, uint32_t timeout_ms = 10000) override;
    virtual void SetPreviewGif(const char* file_path, bool loop, uint32_t timeout_ms = 10000) override;

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

    void UpdateOuterRingColor();

    // 语音唤醒时显示 JARVIS 启动视图（隐藏罗盘主界面）
    void ShowJarvisWatchface();
    // 语音交互结束时隐藏 JARVIS 视图，恢复罗盘主界面
    void HideJarvisWatchface();

    // 运势菜单（短按选中、长按确认；结果卡 Plan A 已彻底删除）
    void EnterIdleState();

    // 调试信息卡：把与后台交互的关键事件短时显示在太极圈内
    // hold_ms: 显示持续时间；调用方若同步播放音频，应传入音频可覆盖的时长
    void ShowDebugInfo(const std::string& title, const std::string& detail, uint32_t hold_ms = 3000);
    void HideDebugInfo();
    // 仅在卡片仍可见时刷新其隐藏计时器（hold_ms=0 使用默认 DEBUG_INFO_SHOW_MS）
    // 用于"唤醒成功后保持至少 N 秒，有语音交互则重计时"场景
    void RefreshDebugInfoTimer(uint32_t hold_ms = 0);
    /** Boot 短按：Idle 循环选中运势入口 */
    bool HandleBootKey();
    /** Boot 长按 3s：触发今日占卜跑马灯 */
    bool HandleFortuneBootLongPress();
    /** 电源键短按：返回/取消 - 取消选中、隐藏功能区 */
    bool HandlePowerKey();
    /** 选中指定运势菜单项（触摸点击使用） */
    void SelectFortuneMenuItem(int index);
    /** 循环选中下一个运势菜单项 */
    void CycleFortuneMenuSelection();
    /** 占卜动画或结果展示进行中 */
    bool IsFortuneDivinationBusy() const;
    /** 开始占卜跑马灯动画（AI 可通过此方法触发占卜） */
    void StartFortuneDivination();
    /** 停止当前占卜（动画或结果），恢复待机状态 */
    void StopFortuneDivination();
    /** 获取当前占卜结果索引（-1 表示无结果） */
    int GetFortuneDivinationResult() const;
    /** 获取当前占卜状态 */
    int GetFortuneDivinationState() const;

    // 在当前活动视图（罗盘或 JARVIS）上显示图片/GIF
    void ShowImageOnActiveView(std::unique_ptr<LvglImage> image, uint32_t timeout_ms = 5000,
                               bool loop = true);

    // 从 JARVIS 视图切换到占卜视图（隐藏 JARVIS，显示罗盘并开始占卜）
    void SwitchToDivination();

    // 占卜结束后切换回 JARVIS 视图
    void SwitchBackFromDivination();

    // 获取当前是否显示 JARVIS 视图
    bool IsJarvisWatchfaceVisible() const { return fortune_watchface_visible_; }

    // 设置占卜结束回调
    void SetDivinationCallback(std::function<void(int)> callback);

    // 获取当前视图栈（spec 6.2 ViewStack）
    const ViewStack& GetViewStack() const { return view_stack_; }
    ActiveView GetCurrentView() const { return view_stack_.current(); }

    // 视图切换时的 200ms 淡入淡出过渡（需在 DisplayLockGuard 内调用）
    void FadeViewTransitionUnlocked(lv_obj_t* from_view, lv_obj_t* to_view, uint32_t duration_ms = 200);

private:
    lv_obj_t* attitude_container_ = nullptr;

    lv_obj_t* background_ = nullptr;
    lv_obj_t* bg_layer_center_ = nullptr;
    lv_obj_t* bg_inner_glow_ = nullptr;

    lv_obj_t* screen_border_ = nullptr;

    lv_obj_t* circle_outer_ = nullptr;
    lv_obj_t* circle_mid_ = nullptr;
    lv_obj_t* circle_inner_ = nullptr;

    lv_obj_t* layer4_outer_ring_ = nullptr;

    lv_obj_t* fortune_menu_ring_touch_ = nullptr;
    lv_obj_t* fortune_menu_labels_[FORTUNE_MENU_COUNT] = {};
    int fortune_menu_center_x_[FORTUNE_MENU_COUNT] = {};
    int fortune_menu_center_y_[FORTUNE_MENU_COUNT] = {};
    int fortune_menu_selected_index_ = -1;
    bool fortune_menu_selection_active_ = false;
    int fortune_menu_applied_scale_[FORTUNE_MENU_COUNT] = {};

    FortuneDivinationState fortune_divination_state_ = FortuneDivinationState::Idle;
    lv_timer_t* fortune_divination_timer_ = nullptr;
    lv_timer_t* taiji_hold_timer_ = nullptr;
    uint32_t fortune_divination_start_ms_ = 0;
    uint32_t fortune_divination_finish_deadline_ms_ = 0;
    bool taiji_pressed_during_anim_ = false;
    bool fortune_divination_from_taiji_ = false;
    int fortune_divination_last_tick_index_ = -1;
    int fortune_divination_highlight_ = -1;
    int fortune_divination_result_ = -1;
    lv_color_t fortune_divination_current_color_ = lv_color_hex(0x00C8C8);
    bool taiji_hold_pending_ = false;
    bool fortune_divination_sound_playing_ = false;
    uint32_t fortune_divination_sound_next_play_ms_ = 0;
    lv_obj_t* taiji_divination_touch_ = nullptr;
    lv_obj_t* divination_hint_label_ = nullptr;
    lv_obj_t* taiji_press_overlay_ = nullptr;
    bool taiji_rotation_paused_by_press_ = false;
    bool fortune_watchface_visible_ = false;  // 追踪 FortuneWatchfaceView 显示状态
    bool divination_from_jarvis_ = false;     // 记录是否从 JARVIS 进入占卜
    std::function<void(int)> divination_callback_ = nullptr;  // 占卜结束回调
    ViewStack view_stack_;                     // 视图栈：spec 6.2 定义

    float current_pitch_ = 0.0f;
    float current_roll_ = 0.0f;
    float current_yaw_ = 0.0f;
    int current_state_level_ = 0;

    // 鱼眼（原始样式系统）
    lv_obj_t* wifi_fisheye_ = nullptr;
    lv_obj_t* wifi_fisheye_icon_ = nullptr;
    lv_obj_t* ble_fisheye_ = nullptr;
    lv_obj_t* ble_fisheye_icon_ = nullptr;
    WifiStatus wifi_status_ = WifiStatus::DISCONNECTED;
    BleStatus ble_status_ = BleStatus::DISABLED;

    lv_obj_t* preview_image_ = nullptr;
    lv_obj_t* preview_gif_ = nullptr;
    lv_obj_t* image_overlay_card_ = nullptr;

    void CreateWifiFisheye();
    void CreateBleFisheye();
    void ApplyWifiFisheyeStyle(WifiStatus status);
    void ApplyBleFisheyeStyle(BleStatus status);

    void UpdateTaijiGoldRingColor(lv_color_t color);

    void CreateBackground();
    void CreateLayer0Taiji();
    void CreateLayer4Boundary();
    void CreateFortuneMenuRing();
    void CreateFortuneMenuRingTouch();
    void CreateTaijiDivinationTouch();
    void CreateDivinationHintLabel();
    void SetFortuneMenuVisible(bool visible);
    void SelectFortuneMenuItemUnlocked(int index);
    void DeselectFortuneMenuItemUnlocked();
    void UpdateFortuneMenuSelection();
    void UpdateFortuneMenuItemVisual(int index, bool selected);
    void CycleFortuneMenuSelectionUnlocked();
    void PlayFortuneMenuSelectSound();
    void PlayFortuneDivinationMarqueeSound();
    void StartFortuneDivinationUnlocked();
    void StopFortuneDivinationUnlocked();
    void FinishFortuneDivinationUnlocked(int result_index);
    void UpdateFortuneDivinationMarqueeVisual(int active_index);
    void ResetFortuneMenuIconStyle(int index);
    void CancelTaijiHoldTimerUnlocked();
    static void OnFortuneDivinationTick(lv_timer_t* timer);
    static void OnTaijiHoldTimer(lv_timer_t* timer);
    static void OnTaijiDivinationPressed(lv_event_t* e);
    static void OnTaijiDivinationReleased(lv_event_t* e);
    void ShowDivinationHintUnlocked(const char* text);
    void HideDivinationHintUnlocked();
    void CreateTaijiPressOverlayUnlocked();
    void ShowTaijiPressOverlayUnlocked();
    void HideTaijiPressOverlayUnlocked();
    // 在 DebugInfo 卡上展示指定索引主功能的一级分类（持锁状态下调用）
    void ShowFortuneFeatureCategoryUnlocked(int index);  // 切换 JARVIS/罗盘视图（不弹信息卡）
    // 图片显示（持锁状态下调用，不加锁版本）
    void SetPreviewImageUnlocked(std::unique_ptr<LvglImage> image, uint32_t timeout_ms = 10000);
    void ShowImageOnActiveViewUnlocked(std::unique_ptr<LvglImage> image, uint32_t timeout_ms,
                                       bool loop);
    GifPreviewTarget BuildCompassPreviewTarget(const LvglImage* image) const;
    GifPreviewTarget BuildJarvisPreviewTarget(const LvglImage* image) const;

    void SetTaijiCoreVisible(bool visible);

    void UpdateStateColor(int level);

    // 调试信息卡（短时显示在太极圈内）：title + detail 两行
    lv_obj_t* function_area_card_ = nullptr;
    lv_obj_t* debug_info_title_ = nullptr;
    lv_obj_t* debug_info_detail_ = nullptr;
    // 上一次的调试信息（用于防抖）
    std::string debug_info_last_title_;
    int64_t debug_info_last_show_ms_ = 0;
    void CreateDebugInfoCard();
    void DestroyDebugInfoCard();
    void ApplyDebugInfoCardLayout();
    struct DebugInfoPresentOpts {
        bool persistent = false;
    };
    void PresentDebugInfoCardUnlocked(const std::string& title, const std::string& detail,
                                      uint32_t hold_ms, const DebugInfoPresentOpts& opts);
    void ClearDebugInfoCard();
    // 事件队列相关
    std::deque<DebugInfoItem> debug_info_queue_;
    size_t current_index_ = SIZE_MAX;  // 当前显示事件索引
    DebugInfoItem* EnqueueItem(const std::string& title, const std::string& detail,
                               uint32_t hold_ms, DebugInfoPriority priority);
    void CleanupCurrentItem();
    static void OnDebugInfoTimer(lv_timer_t* timer);
    DebugInfoPriority InferDebugInfoPriority(const std::string& title);
    void DisplayDebugInfoCard(const std::string& title, const std::string& detail);
    void PopAndShowNext();






};

#endif // ATTITUDE_DISPLAY_H
