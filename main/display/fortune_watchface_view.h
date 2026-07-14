#ifndef FORTUNE_WATCHFACE_VIEW_H
#define FORTUNE_WATCHFACE_VIEW_H

#include <lvgl.h>
#include <cstdint>
#include <memory>
#include "lvgl_image.h"
#include "lvgl_gif.h"

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

    // 显示/隐藏
    void Show();
    void Hide();
    bool IsVisible() const { return visible_; }

    // 生命周期绑定到 attitude_display
    void SetParentContainer(lv_obj_t* container);

    // 图片显示（在 JARVIS 视图之上覆盖显示；接管 image 所有权直至 HideImage）
    void ShowImage(std::unique_ptr<LvglImage> image, uint32_t timeout_ms = 5000);
    void HideImage();
    bool IsImageVisible() const;

    // 语音交互状态文本（覆盖默认扫描进度显示）
    void SetStatusText(const char* text);
    void ClearStatusText();

    // 设置语音交互消息（专用接口，自动启用滚动模式）
    void SetVoiceMessage(const char* text);
    void ClearVoiceMessage();

    // 更新外环颜色（与罗盘主界面保持一致）
    void UpdateOuterRingColor(lv_color_t color);

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
    lv_obj_t* orbit_canvas_ = nullptr;
    uint8_t* canvas_buffer_ = nullptr;
    lv_obj_t* tick_marks_[60];
    lv_obj_t* jarvis_bars_[5];

    lv_timer_t* timer_ = nullptr;

    // 状态栏模式
    StatusMode status_mode_ = kModeDefault;
    std::string voice_status_text_;

    // 图片覆盖层
    lv_obj_t* image_overlay_ = nullptr;
    lv_obj_t* image_widget_ = nullptr;
    std::unique_ptr<LvglImage> image_cache_;
    LvglGif* gif_controller_ = nullptr;
    lv_timer_t* image_hide_timer_ = nullptr;
    bool image_visible_ = false;

    bool visible_ = false;
};

#endif  // FORTUNE_WATCHFACE_VIEW_H
