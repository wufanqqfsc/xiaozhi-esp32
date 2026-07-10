#ifndef JARVIS_WATCHFACE_H
#define JARVIS_WATCHFACE_H

#include <lvgl.h>
#include <cstdint>

/**
 * JarvisWatchface - JARVIS 风格 HUD 手表脸显示
 *
 * 功能：语音唤醒时显示完整的 JARVIS HUD 动画界面
 * 特性：
 *   - 360×360 圆形屏幕适配
 *   - 多层旋转能量环
 *   - 动态粒子系统
 *   - 状态指示（待机/聆听/说话）
 */
class JarvisWatchface {
public:
    static JarvisWatchface& GetInstance();

    // 显示/隐藏
    void Show();
    void Hide();
    bool IsVisible() const { return visible_; }

    // 状态控制
    enum class State {
        Sleep,      // 待机 - 低功耗状态
        Starting,   // 启动中 - 能量上升
        Active,     // 激活 - 稳定呼吸
        Listening,  // 聆听 - 声波扩散
        Speaking    // 说话 - 能量脉动
    };

    void SetState(State state);
    State GetState() const { return state_; }

private:
    JarvisWatchface();
    ~JarvisWatchface();

    // 禁用拷贝
    JarvisWatchface(const JarvisWatchface&) = delete;
    JarvisWatchface& operator=(const JarvisWatchface&) = delete;

    void CreateUI();
    void DestroyUI();
    void CreateTickMarks();
    void UpdateFrame();

    static void OnTimer(lv_timer_t* timer);
    lv_obj_t* AddBox(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h,
                     uint32_t color, int32_t radius);
    lv_obj_t* AddArc(lv_obj_t* parent, int32_t size, int32_t width,
                     uint32_t base_color, uint32_t active_color);
    static int ClampI32(int value, int min, int max);
    static int32_t OrbitX(float angle, float radius);
    static int32_t OrbitY(float angle, float radius);

    static constexpr float kPi = 3.14159265358979323846f;
    static constexpr int kScreenW = 360;
    static constexpr int kScreenH = 360;
    static constexpr int kCenterX = 180;
    static constexpr int kCenterY = 180;
    static constexpr int kOrbitCount = 12;

    lv_obj_t* screen_ = nullptr;
    lv_obj_t* previous_screen_ = nullptr;
    lv_timer_t* timer_ = nullptr;
    lv_obj_t* scan_arc_ = nullptr;
    lv_obj_t* pulse_arc_ = nullptr;
    lv_obj_t* seconds_arc_ = nullptr;
    lv_obj_t* jarvis_label_ = nullptr;
    lv_obj_t* jarvis_label_shadow_a_ = nullptr;
    lv_obj_t* jarvis_label_shadow_b_ = nullptr;
    lv_obj_t* status_label_ = nullptr;
    lv_obj_t* orbit_dots_[kOrbitCount] = {};
    lv_obj_t* tick_marks_[60] = {};
    lv_obj_t* jarvis_bars_[5] = {};

    bool visible_ = false;
    State state_ = State::Sleep;
};

#endif // JARVIS_WATCHFACE_H
