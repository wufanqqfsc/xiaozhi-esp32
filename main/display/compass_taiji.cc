#include "compass_taiji.h"
#include <esp_log.h>
#include <cstring>
#include <cmath>
#include <esp_heap_caps.h>

static const char* TAG = "CompassTaiji";

// 静态成员初始化
lv_obj_t* CompassTaiji::taiji_container_ = nullptr;
lv_obj_t* CompassTaiji::white_circle_ = nullptr;
lv_obj_t* CompassTaiji::black_circle_ = nullptr;
lv_obj_t* CompassTaiji::white_dot_ = nullptr;
lv_obj_t* CompassTaiji::black_dot_ = nullptr;
lv_obj_t* CompassTaiji::outer_ring_ = nullptr;
lv_obj_t* CompassTaiji::outer_glow_ = nullptr;

/**
 * 填充圆 (中点圆算法 + 水平扫描线)
 * 在 (cx, cy) 为中心，半径 r 的圆内用 color 填充
 */
static inline void FillCircle(lv_obj_t* canvas, int cx, int cy, int r,
                              lv_color_t color) {
    int r_sq = r * r;
    for (int y = -r; y <= r; y++) {
        int dy_sq = y * y;
        int x_max = (int)std::sqrt((float)(r_sq - dy_sq));
        for (int x = -x_max; x <= x_max; x++) {
            lv_canvas_set_px(canvas, cx + x, cy + y, color, LV_OPA_COVER);
        }
    }
}

/**
 * 在 (cx, cy) 为圆心创建太极图
 * @param radius 整体半径（外圆半径）
 */
void CompassTaiji::Create(lv_obj_t* parent, int cx, int cy, int radius) {
    const auto& theme_colors = AttitudeTheme::GetInstance().GetColors();

    ESP_LOGI(TAG, "Creating Taiji diagram at (%d, %d) radius=%d", cx, cy, radius);

    int canvas_size = radius * 2;

    // ========== 1. 容器（透明）==========
    taiji_container_ = lv_obj_create(parent);
    lv_obj_set_size(taiji_container_, canvas_size, canvas_size);
    lv_obj_set_pos(taiji_container_, cx - radius, cy - radius);
    lv_obj_set_style_radius(taiji_container_, radius, 0);
    lv_obj_set_style_bg_opa(taiji_container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(taiji_container_, 0, 0);
    lv_obj_set_style_pad_all(taiji_container_, 0, 0);
    lv_obj_clear_flag(taiji_container_, LV_OBJ_FLAG_CLICKABLE);

    // ========== 2. 外圈鎏金高亮环 ==========
    // 关键修复: 移除 lv_obj 边框（会显示矩形外框）
    // 改用 canvas 直接绘制鎏金外圈, 避免矩形边框问题
    outer_ring_ = nullptr;
    outer_glow_ = nullptr;

    // ========== 3. 创建 canvas 画布 ==========
    // 关键: 使用 ARGB8888 格式, 每个像素 4 字节, 包含 alpha 通道
    // 这才能让未绘制的像素真正透明, 避免矩形边框残留
    uint32_t buf_size = canvas_size * canvas_size * sizeof(uint32_t);  // ARGB8888 = 4 bytes/pixel

    // 优先从 PSRAM 分配大块内存
    uint32_t* canvas_buf = (uint32_t*)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    if (!canvas_buf) {
        canvas_buf = (uint32_t*)malloc(buf_size);
    }
    if (!canvas_buf) {
        ESP_LOGE(TAG, "Failed to allocate canvas buffer (%u bytes)", buf_size);
        return;
    }
    // 初始化为透明黑色 (alpha=0 表示完全透明)
    memset(canvas_buf, 0, buf_size);

    // 创建 canvas 对象
    lv_obj_t* canvas = lv_canvas_create(taiji_container_);
    lv_canvas_set_buffer(canvas, canvas_buf, canvas_size, canvas_size, LV_COLOR_FORMAT_ARGB8888);
    lv_obj_set_pos(canvas, 0, 0);
    // 关键修复: 设置画布背景为完全透明, 防止矩形边框残留
    lv_obj_set_style_bg_opa(canvas, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(canvas, 0, 0);
    lv_obj_set_style_outline_width(canvas, 0, 0);
    lv_obj_set_style_shadow_width(canvas, 0, 0);
    // 关键: 关闭 image recolor (防止默认色调覆盖透明像素)
    lv_obj_set_style_image_recolor_opa(canvas, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(canvas, LV_OBJ_FLAG_CLICKABLE);

    // ========== 4. 绘制太极图 ==========
    lv_color_t white = lv_color_white();
    lv_color_t black = lv_color_black();

    int r = radius;
    int center_x = r;  // 画布中心
    int center_y = r;

    int half_r = r / 2;      // 上下小半圆半径
    int dot_r = r / 6;       // 中心点半径

    // 步骤 1: 填充白色大圆 (阳鱼)
    FillCircle(canvas, center_x, center_y, r, white);

    // 步骤 2: 填充黑色右半圆 (阴鱼) - x > center_x 部分
    for (int y = -r; y <= r; y++) {
        int dy_sq = y * y;
        int x_max = (int)std::sqrt((float)(r * r - dy_sq));
        for (int x = 0; x <= x_max; x++) {  // x >= 0
            lv_canvas_set_px(canvas, center_x + x, center_y + y, black, LV_OPA_COVER);
        }
    }

    // 步骤 3: 填充上半小圆(白色) - 阴中有阳点
    // 在阴鱼（黑色右半）的上半添加白色小圆
    for (int y = -half_r; y <= half_r; y++) {
        int dy_sq = y * y;
        int x_max = (int)std::sqrt((float)(half_r * half_r - dy_sq));
        for (int x = -x_max; x <= x_max; x++) {
            int px = center_x + x;
            int py = center_y - half_r + y;
            // 需要在大圆内
            int big_dx = x;
            int big_dy = y - half_r;
            if (big_dx * big_dx + big_dy * big_dy <= r * r) {
                lv_canvas_set_px(canvas, px, py, white, LV_OPA_COVER);
            }
        }
    }

    // 步骤 4: 填充下半小圆(黑色) - 阳中有阴点
    // 在阳鱼（白色左半）的下半添加黑色小圆
    for (int y = -half_r; y <= half_r; y++) {
        int dy_sq = y * y;
        int x_max = (int)std::sqrt((float)(half_r * half_r - dy_sq));
        for (int x = -x_max; x <= x_max; x++) {
            int px = center_x + x;
            int py = center_y + half_r + y;
            // 需要在大圆内
            int big_dx = x;
            int big_dy = y + half_r;
            if (big_dx * big_dx + big_dy * big_dy <= r * r) {
                lv_canvas_set_px(canvas, px, py, black, LV_OPA_COVER);
            }
        }
    }

    // 步骤 5: 绘制阳中黑点 (位于上半小圆中心，即阴鱼区域)
    FillCircle(canvas, center_x, center_y - half_r, dot_r, black);

    // 步骤 6: 绘制阴中白点 (位于下半小圆中心，即阳鱼区域)
    FillCircle(canvas, center_x, center_y + half_r, dot_r, white);

    // 步骤 7: 鎏金外圈 (替代 lv_obj border 避免矩形外框)
    // 鎏金 #D4AF37 = R212 G175 B55 = 0xD4AF37
    lv_color_t gold = lv_color_hex(0xD4AF37);
    // 绘制半径 (r-1) 到 (r+1) 之间的鎏金环 (2px 宽)
    for (int rr = r - 1; rr <= r + 1; rr++) {
        if (rr <= 0) continue;
        int rr_sq = rr * rr;
        for (int y = -rr; y <= rr; y++) {
            int dy_sq = y * y;
            int x_max = (int)std::sqrt((float)(rr_sq - dy_sq));
            // 只画圆周上的点 (最后一圈), 避免填满整个圆
            int inner_y = abs(y);
            int x_max_inner = (rr > 1) ? (int)std::sqrt((float)((rr-1) * (rr-1) - inner_y * inner_y)) : 0;
            for (int x = -x_max; x <= x_max; x++) {
                if (abs(x) > x_max_inner) {
                    lv_canvas_set_px(canvas, center_x + x, center_y + y, gold, LV_OPA_COVER);
                }
            }
        }
    }

    ESP_LOGI(TAG, "Taiji diagram created successfully (%dx%d canvas)", canvas_size, canvas_size);
}

void CompassTaiji::UpdateTheme() {
    const auto& theme_colors = AttitudeTheme::GetInstance().GetColors();

    if (outer_ring_ != nullptr) {
        lv_obj_set_style_border_color(outer_ring_, theme_colors.border_line, 0);
    }
    if (outer_glow_ != nullptr) {
        lv_obj_set_style_border_color(outer_glow_, theme_colors.border_line, 0);
    }
}
