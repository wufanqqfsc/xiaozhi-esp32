#ifndef FORTUNE_WATCHFACE_VIEW_H
#define FORTUNE_WATCHFACE_VIEW_H

#include <lvgl.h>
#include <cstdint>
#include <string>
/**
 * FortuneWatchfaceView - JARVIS 启动界面特效视图
 *
 * 功能：在选择"今日运势"时显示 JARVIS 风格的 HUD 动画特效
 * 来源：移植自动态表盘项目
 * 特性：
 *   - 360×360 圆形屏幕适配
 *   - 扫描弧动画 (Scan Arc)
 *   - 脉冲弧动画 (Pulse Arc)
 *   - 轨道点动画 (Orbit Dots)
 *   - JARVIS 文字闪烁效果
 *   - 频谱音频条动画
 *   - 状态栏滚动显示
 */
class FortuneWatchfaceView {
public:
    static FortuneWatchfaceView& GetInstance();

    // 显示/隐藏（调用方已持有 LVGL 锁时使用 Unlocked 版本）
    void Show();
    void Hide();
    bool ShowUnlocked();
    void HideUnlocked();
    void EnsureAnimatingUnlocked();
    bool IsVisible() const { return visible_; }

    // 生命周期绑定到 attitude_display
    void SetParentContainer(lv_obj_t* container);

    // 空闲时释放 JARVIS LVGL 屏幕树与语音状态文本，下次 Show 时重建
    void ReleaseIdleResources();
    // 调用方已持有 LVGL 锁（DisplayLockGuard）时使用，避免重复加锁
    void ReleaseIdleResourcesUnlocked();

    // 语音交互状态文本（覆盖默认扫描进度显示）
    void SetStatusText(const char* text);
    void ClearStatusText();

    // 设置语音交互消息（专用接口，自动启用滚动模式）
    void SetVoiceMessage(const char* text);
    void ClearVoiceMessage();

    // 更新外环颜色（与罗盘主界面保持一致）
    void UpdateOuterRingColor(lv_color_t color);
    void UpdateOuterRingColorUnlocked(lv_color_t color);

    // 获取覆盖层独立屏幕（供 AttitudeDisplay 做视图切换淡入淡出使用）
    lv_obj_t* GetOverlayScreen() { return overlay_screen_; }

private:
    FortuneWatchfaceView();
    ~FortuneWatchfaceView();

    // 禁用拷贝
    FortuneWatchfaceView(const FortuneWatchfaceView&) = delete;
    FortuneWatchfaceView& operator=(const FortuneWatchfaceView&) = delete;

    // 状态栏显示模式
    enum StatusMode {
        kModeDefault,      // 默认模式：显示扫描进度
        kModeVoiceActive   // 语音交互模式：显示交互文本
    };

    // UI 创建
    void CreateUI();
    void DestroyUI();
    void EnsureTimer();

    // 定时器回调
    static void OnTimer(lv_timer_t* timer);
    void UpdateAnimation();

    // 辅助函数
    static int32_t ClampI32(int32_t value, int32_t min, int32_t max);
    static int32_t OrbitX(float angle, float radius);
    static int32_t OrbitY(float angle, float radius);

    // 创建 UI 元素
    lv_obj_t* AddBox(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h,
                     uint32_t color, int32_t radius);
    lv_obj_t* AddArc(lv_obj_t* parent, int32_t size, int32_t width,
                     uint32_t base_color, uint32_t active_color);

    void CreateTickMarks(lv_obj_t* screen);
    void CreateDynamicWatchface();

    // 坐标常量
    static constexpr int W_ = 360;
    static constexpr int H_ = 360;
    static constexpr int CX_ = W_ / 2;
    static constexpr int CY_ = H_ / 2;
    static constexpr int ORBIT_COUNT_ = 12;

    // JARVIS 金色主题（与外环 0xD4AF37 一致）
    static constexpr uint32_t kJarvisGold_ = 0xD4AF37;
    static constexpr uint32_t kJarvisGoldBright_ = 0xFFD700;
    static constexpr uint32_t kJarvisGoldShadow_ = 0xB8860B;

    // 状态栏：上缘贴内核环下缘 (y=260)，底边中心贴外环内缘（圆屏两侧自然裁切）
    // 外环 r=179、线宽 3 → 内缘半径 178，底边中心 y = CY + 178 = 358
    static constexpr int GOLD_RING_ARC_WIDTH_ = 3;
    static constexpr int OUTER_RING_INNER_R_ =
        (W_ / 2 - GOLD_RING_ARC_WIDTH_ / 2) - (GOLD_RING_ARC_WIDTH_ / 2);
    static constexpr int STATUS_BAR_X_ = 51;
    static constexpr int STATUS_BAR_Y_ = 260;
    static constexpr int STATUS_BAR_W_ = 258;
    static constexpr int STATUS_BAR_BOTTOM_Y_ = CY_ + OUTER_RING_INNER_R_;
    static constexpr int STATUS_BAR_H_ = STATUS_BAR_BOTTOM_Y_ - STATUS_BAR_Y_;

    // 成员变量
    lv_obj_t* parent_container_ = nullptr;  // 父容器（attitude_container_）
    lv_obj_t* overlay_screen_ = nullptr;    // 覆盖层独立屏幕
    lv_obj_t* prev_screen_ = nullptr;       // 显示前的原始屏幕（用于Hide时恢复）

    // LVGL UI 元素
    lv_obj_t* scan_arc_ = nullptr;
    lv_obj_t* pulse_arc_ = nullptr;
    lv_obj_t* seconds_arc_ = nullptr;
    lv_obj_t* outer_ring_ = nullptr;
    lv_obj_t* jarvis_label_ = nullptr;
    lv_obj_t* jarvis_label_shadow_a_ = nullptr;
    lv_obj_t* jarvis_label_shadow_b_ = nullptr;
    lv_obj_t* status_label_ = nullptr;
    lv_obj_t* orbit_dots_[ORBIT_COUNT_];
    lv_obj_t* tick_marks_[60];
    lv_obj_t* jarvis_bars_[5];

    lv_timer_t* timer_ = nullptr;

    // 状态栏模式
    StatusMode status_mode_ = kModeDefault;
    std::string voice_status_text_;

    bool visible_ = false;
};

#endif  // FORTUNE_WATCHFACE_VIEW_H
