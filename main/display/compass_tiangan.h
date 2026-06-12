#ifndef COMPASS_TIANGAN_H
#define COMPASS_TIANGAN_H

#include <lvgl.h>

/**
 * CompassTiangan - 10 天干层 (迭代 17)
 *
 * 10 天干 (甲乙丙丁戊己庚辛壬癸) 沿圆周均匀分布
 * 默认使用鎏金 #D4AF37
 * 起始位置: 子 (北方顶部), 每 36° 一个
 */
class CompassTiangan {
public:
    /**
     * 创建 10 天干层
     * @param parent 父容器
     * @param cx 圆心 X
     * @param cy 圆心 Y
     * @param radius 10 天干所在的圆周半径
     */
    static void Create(lv_obj_t* parent, int cx, int cy, int radius);

    /**
     * 删除所有 10 天干
     */
    static void Delete();

private:
    static lv_obj_t* tiangan_labels_[10];
    static lv_obj_t* tiangan_container_;

    // 10 天干字符 (顺序: 从正北/甲 开始, 顺时针)
    static const char* tiangan_chars_[10];
};

#endif // COMPASS_TIANGAN_H
