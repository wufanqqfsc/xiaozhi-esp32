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

    // UI 创建
    void CreateUI();
    void DestroyUI();

    // 定时器回调
    static void OnTimer(lv_timer_t* timer);
    void UpdateAnimation();
    void RedrawCanvas();

    // 粒子系统
    void InitParticles();
    void UpdateParticles(uint32_t dt);
    struct Particle {
        int ring_idx;
        float angle;
        float speed;
        float size;
        float brightness;
        float drift;
        float life;
        float flicker;
    };

    // 坐标辅助
    static constexpr int W_ = 360;
    static constexpr int H_ = 360;
    static constexpr int CX_ = W_ / 2;
    static constexpr int CY_ = H_ / 2;

    // 环形层配置
    struct Ring {
        int r;
        float speed;
        int dir;
        int width;
        int hue;
        int segments;
    };
    static constexpr int RING_COUNT = 3;
    static constexpr Ring RINGS[RING_COUNT] = {
        {158, 0.3f,  1, 2, 180, 36},  // 外环
        {110, 0.8f,  1, 1, 170, 22},  // 中环
        { 95, 1.2f, -1, 2, 200, 16},  // 内环
    };

    // 成员变量
    lv_obj_t* screen_ = nullptr;
    lv_obj_t* canvas_ = nullptr;
    static uint16_t* canvas_buf_;
    lv_timer_t* timer_ = nullptr;

    bool visible_ = false;
    State state_ = State::Sleep;

    // 动画参数
    uint32_t state_time_ = 0;
    float global_energy_ = 0.05f;
    float target_energy_ = 0.05f;
    float breath_phase_ = 0;

    float ring_angles_[RING_COUNT] = {0, 0, 0};
    float ring_intensity_[RING_COUNT] = {0, 0, 0};

    // 粒子
    static constexpr int MAX_PARTICLES = 45;
    Particle particles_[MAX_PARTICLES];
};

#endif // JARVIS_WATCHFACE_H
