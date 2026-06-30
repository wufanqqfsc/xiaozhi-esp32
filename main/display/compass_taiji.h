#ifndef COMPASS_TAIJI_H
#define COMPASS_TAIJI_H

#include <lvgl.h>

// ====================== 太极图绘制模块 ======================
// 迭代 13: Target 中心太极图（阴阳鱼）
// 使用 LVGL 基本控件组合模拟阴阳鱼图案

class CompassTaiji {
public:
    /**
     * 在 (cx, cy) 为圆心创建太极图
     * @param parent 父对象
     * @param cx 圆心 X 坐标
     * @param cy 圆心 Y 坐标
     * @param radius 太极图半径（外圆半径）
     */
    static void Create(lv_obj_t* parent, int cx, int cy, int radius);

    /**
     * 顺时针旋转太极图（按键触发）
     * @param delta_angle 旋转角度（0.1°单位），正值=顺时针
     */
    static void Rotate(int delta_angle);

    /**
     * 设置太极图旋转角度
     * @param angle 旋转角度（0.1°单位），0~3600 表示 0°~360°
     */
    static void SetRotation(int angle);

    /**
     * 获取当前旋转角度
     * @return 当前角度（0.1°单位）
     */
    static int GetRotation();

    /**
     * 重置旋转角度为 0
     */
    static void ResetRotation();

    /**
     * 启动自动旋转
     * @param period_ms 旋转周期 (毫秒) - 转 360° 所需时间
     *                  默认 60000ms = 60秒转一圈
     */
    static void StartAutoRotation(int period_ms = 60000);

    /**
     * 运行中调整旋转周期（用于运势 Animating 加速/减速）
     * @param period_ms 新的 360° 周期 (ms)，越小转得越快
     */
    static void SetAutoRotationPeriod(int period_ms);

    /** 当前自动旋转周期 (ms) */
    static int GetAutoRotationPeriod();

    /** 暂停/恢复后台旋转任务（Animating 时由 LVGL 定时器接管步进） */
    static void SetAutoRotationPaused(bool paused);

    /** 按当前 step 旋转一步；调用方须持有 LVGL 锁 */
    static void TickAutoRotationStep();

    /**
     * 停止自动旋转
     */
    static void StopAutoRotation();

    /**
     * 检查是否在自动旋转中
     */
    static bool IsAutoRotating();

    /** 太极容器句柄（鱼眼须创建为其子对象以共旋转） */
    static lv_obj_t* GetContainer();

    /** 阴阳鱼 canvas */
    static lv_obj_t* GetCanvas();

    /**
     * 学业态：仅保留鎏金外圈 + 圈内深色底，不绘阴阳鱼；false 时恢复完整太极图
     */
    static void SetStudyRingMode(bool ring_only);

    /** 当前太极外径（像素） */
    static int GetRadius();

    /**
     * 更新太极外圈鎏金环颜色（重绘 canvas 金环部分）
     */
    static void UpdateGoldRingColor(lv_color_t color);

    /**
     * 鱼眼小圆盘：实心填色 + 抗锯齿描边（外缘向 bg 混合，避免透明叠底产生杂色）
     * @param bg 与所在鱼体底色一致（白鱼眼位=白 / 黑鱼眼位=黑），方形 canvas 外缘与 AA 向此色混合
     */
    static void PaintFisheyeDisc(lv_obj_t* canvas, int size, lv_color_t fill,
                                 lv_color_t ring, lv_color_t bg, int ring_width);

    // 内部访问 (供 auto_rotation_task 调用) - public 让全局函数可访问
    static int GetStepInternal() { return auto_rotation_step_; }
    static int GetIntervalInternal() { return auto_rotation_interval_ms_; }

    static void RecalcAutoRotationStep();

    static void OnAutoRotationTimer(lv_timer_t* timer);

private:
    // 太极图组件句柄（使用静态变量，便于跨实例访问）
    static lv_obj_t* taiji_container_;
    static lv_obj_t* white_circle_;       // 白色阳鱼
    static lv_obj_t* black_circle_;       // 黑色阴鱼
    static lv_obj_t* white_dot_;          // 阳中白点（不对外暴露，引用即可）
    static lv_obj_t* black_dot_;          // 阴中黑点
    static lv_obj_t* outer_ring_;         // 外圈鎏金高亮环
    static lv_obj_t* outer_glow_;         // 外圈发光
    static lv_obj_t* canvas_;             // 太极图画布
    static uint32_t* canvas_buf_;         // canvas 像素缓冲（与 lv_canvas 共享）
    static uint32_t* taiji_canvas_snapshot_;
    static uint32_t* study_ring_canvas_snapshot_;
    static size_t canvas_buf_bytes_;
    static bool canvas_snapshots_ready_;
    static bool study_ring_mode_active_;
    static int taiji_radius_;             // 外圆半径（与鱼眼尺寸联动）
    static int current_rotation_;         // 当前旋转角度 (0.1°单位)

    // 自动旋转控制
    static lv_timer_t* auto_rotation_timer_;
    static bool auto_rotation_running_;       // 是否正在自动旋转
    static int auto_rotation_period_ms_;      // 旋转周期 (ms)
    static int auto_rotation_step_;           // 每步旋转角度 (0.1°单位)
    static int auto_rotation_interval_ms_;    // 步进间隔 (ms)
    static volatile bool auto_rotation_paused_;  // Animating 时由 LVGL 定时器驱动

    /**
     * 创建一个圆形 lv_obj
     */
    static lv_obj_t* CreateCircle(lv_obj_t* parent, int x, int y, int r,
                                   lv_color_t color, lv_opa_t opa = LV_OPA_COVER);

    /** 启动时预渲染太极/学业环两套 canvas 快照，菜单切换时 memcpy 切换 */
    static void BuildCanvasSnapshots(lv_obj_t* canvas, int r);
};

#endif // COMPASS_TAIJI_H
