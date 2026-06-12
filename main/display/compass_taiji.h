#ifndef COMPASS_TAIJI_H
#define COMPASS_TAIJI_H

#include <lvgl.h>
#include "attitude_theme.h"

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
     * 更新主题色（用于主题切换时）
     */
    static void UpdateTheme();

private:
    // 太极图组件句柄（使用静态变量，便于跨实例访问）
    static lv_obj_t* taiji_container_;
    static lv_obj_t* white_circle_;       // 白色阳鱼
    static lv_obj_t* black_circle_;       // 黑色阴鱼
    static lv_obj_t* white_dot_;          // 阳中白点（不对外暴露，引用即可）
    static lv_obj_t* black_dot_;          // 阴中黑点
    static lv_obj_t* outer_ring_;         // 外圈鎏金高亮环
    static lv_obj_t* outer_glow_;         // 外圈发光

    /**
     * 创建一个圆形 lv_obj
     */
    static lv_obj_t* CreateCircle(lv_obj_t* parent, int x, int y, int r,
                                   lv_color_t color, lv_opa_t opa = LV_OPA_COVER);
};

#endif // COMPASS_TAIJI_H
