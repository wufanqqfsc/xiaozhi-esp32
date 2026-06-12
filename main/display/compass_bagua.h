#ifndef COMPASS_BAGUA_H
#define COMPASS_BAGUA_H

#include <lvgl.h>
#include <cstdint>

/**
 * CompassBagua - 64 卦符号层 (迭代 14b)
 *
 * 在太极图外圈绘制 64 卦符号（每 5.625° 一个）
 * 8 个主卦（八卦）用鎏金 #D4AF37
 * 56 个变卦用白银 #C0C0C0
 *
 * 卦象由 6 个爻组成（自下而上）:
 *  - 阳爻 (—)   = 实线 (1)
 *  - 阴爻 (-- )  = 虚线/两段 (0)
 *
 * 64 卦按先天八卦顺序 (伏羲先天八卦次序) 排列
 * 起始位置：北方 (顶部)
 */
class CompassBagua {
public:
    /**
     * 创建 64 卦符号层
     * @param parent 父容器 (通常是 attitude_container_)
     * @param cx 圆心 X (屏幕中心)
     * @param cy 圆心 Y (屏幕中心)
     * @param radius 64 卦符号所在的圆周半径 (默认 145 = 在 r=80 太极图外, r=178 layer4 内)
     */
    static void Create(lv_obj_t* parent, int cx, int cy, int radius);

    /**
     * 更新主题色 (用于主题切换时)
     */
    static void UpdateTheme();

    /**
     * 删除所有 64 卦符号 (用于重新创建)
     */
    static void Delete();

private:
    // 64 个卦象标签 (每个卦象是一个 lv_label, 内容是汉字)
    static lv_obj_t* bagua_labels_[64];

    // 容器
    static lv_obj_t* bagua_container_;

    // 颜色缓存
    static lv_color_t main_gold_;    // 主卦鎏金 #D4AF37
    static lv_color_t variant_silver_; // 变卦白银 #C0C0C0

    // 64 卦符号表 (按先天八卦顺序)
    // 64 卦由 6 个爻组成, 顺序从下到上 (bit 0 = 底爻, bit 5 = 顶爻)
    // bit = 1 表示阳爻, 0 表示阴爻
    static const uint8_t hexagrams_[64];

    // 主卦索引 (8 个八卦, 在 64 卦中的位置)
    // 先天八卦顺序: 乾兑离震巽坎艮坤
    static const int main_gua_indices_[8];
};

#endif // COMPASS_BAGUA_H
