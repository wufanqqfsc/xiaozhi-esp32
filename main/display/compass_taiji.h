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
     *                  默认 30000ms = 30秒转一圈
     */
    static void StartAutoRotation(int period_ms = 30000);

    /**
     * 停止自动旋转
     */
    static void StopAutoRotation();

    /**
     * 检查是否在自动旋转中
     */
    static bool IsAutoRotating();

    // 内部访问 (供 auto_rotation_task 调用) - public 让全局函数可访问
    static int GetStepInternal() { return auto_rotation_step_; }
    static int GetIntervalInternal() { return auto_rotation_interval_ms_; }

    // 自动旋转 FreeRTOS 任务入口 (类静态成员, 可访问 private 成员)
    static void AutoRotationTaskEntry(void* arg);

private:
    // 太极图组件句柄（使用静态变量，便于跨实例访问）
    static lv_obj_t* taiji_container_;
    static lv_obj_t* white_circle_;       // 白色阳鱼
    static lv_obj_t* black_circle_;       // 黑色阴鱼
    static lv_obj_t* white_dot_;          // 阳中白点（不对外暴露，引用即可）
    static lv_obj_t* black_dot_;          // 阴中黑点
    static lv_obj_t* outer_ring_;         // 外圈鎏金高亮环
    static lv_obj_t* outer_glow_;         // 外圈发光
    static lv_obj_t* canvas_;             // 太极图画布（用于旋转）
    static int current_rotation_;         // 当前旋转角度 (0.1°单位)

    // 自动旋转控制
    static void* auto_rotation_task_handle_;  // FreeRTOS 任务句柄
    static bool auto_rotation_running_;       // 是否正在自动旋转
    static int auto_rotation_period_ms_;      // 旋转周期 (ms)
    static int auto_rotation_step_;           // 每步旋转角度 (0.1°单位)
    static int auto_rotation_interval_ms_;    // 步进间隔 (ms)

    /**
     * 创建一个圆形 lv_obj
     */
    static lv_obj_t* CreateCircle(lv_obj_t* parent, int x, int y, int r,
                                   lv_color_t color, lv_opa_t opa = LV_OPA_COVER);
};

#endif // COMPASS_TAIJI_H
