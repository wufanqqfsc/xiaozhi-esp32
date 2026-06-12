#ifndef COMPASS_DIZHI_H
#define COMPASS_DIZHI_H

#include <lvgl.h>

/**
 * CompassDizhi - 12 地支层 (迭代 16)
 *
 * 12 地支 (子丑寅卯辰巳午未申酉戌亥) 沿圆周均匀分布
 * 默认使用鎏金 #D4AF37
 * 默认 18px font_puhui 字体
 */
class CompassDizhi {
public:
    /**
     * 创建 12 地支层
     * @param parent 父容器
     * @param cx 圆心 X
     * @param cy 圆心 Y
     * @param radius 12 地支所在的圆周半径 (默认 125)
     */
    static void Create(lv_obj_t* parent, int cx, int cy, int radius);

    /**
     * 删除所有 12 地支 (用于重新创建)
     */
    static void Delete();

private:
    static lv_obj_t* dizhi_labels_[12];  // 12 个地支 label
    static lv_obj_t* dizhi_container_;

    // 12 地支字符 (顺序: 从正北/子开始 顺时针)
    static const char* dizhi_chars_[12];
};

#endif // COMPASS_DIZHI_H
