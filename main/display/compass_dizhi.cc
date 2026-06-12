#include "compass_dizhi.h"
#include <esp_log.h>
#include <cmath>

static const char* TAG = "CompassDizhi";

// 静态成员初始化
lv_obj_t* CompassDizhi::dizhi_container_ = nullptr;
lv_obj_t* CompassDizhi::dizhi_labels_[12] = {nullptr};

// 12 地支字符 (从正北/子 开始, 顺时针)
const char* CompassDizhi::dizhi_chars_[12] = {
    "子", "丑", "寅", "卯", "辰", "巳",
    "午", "未", "申", "酉", "戌", "亥"
};

LV_FONT_DECLARE(BUILTIN_TEXT_FONT);

void CompassDizhi::Create(lv_obj_t* parent, int cx, int cy, int radius) {
    if (parent == nullptr) {
        ESP_LOGE(TAG, "Create: parent is null");
        return;
    }

    ESP_LOGI(TAG, "Creating 12 dizhi at (%d, %d) radius=%d", cx, cy, radius);

    dizhi_container_ = lv_obj_create(parent);
    lv_obj_set_size(dizhi_container_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_pos(dizhi_container_, 0, 0);
    lv_obj_set_style_bg_opa(dizhi_container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dizhi_container_, 0, 0);
    lv_obj_clear_flag(dizhi_container_, LV_OBJ_FLAG_CLICKABLE);

    // 12 地支沿圆周均匀分布 (每 30° = 360/12)
    const float angle_step_rad = 2.0f * M_PI / 12.0f;

    for (int i = 0; i < 12; i++) {
        // 起始角度: 0° = 北方 (顶部), 顺时针
        float angle = -M_PI / 2.0f + i * angle_step_rad;

        int px = cx + (int)(radius * cosf(angle));
        int py = cy + (int)(radius * sinf(angle));

        // 创建 label
        dizhi_labels_[i] = lv_label_create(dizhi_container_);
        lv_label_set_text(dizhi_labels_[i], dizhi_chars_[i]);
        lv_obj_set_style_text_color(dizhi_labels_[i], lv_color_hex(0xD4AF37), 0);
        lv_obj_set_style_text_font(dizhi_labels_[i], &BUILTIN_TEXT_FONT, 0);
        lv_obj_set_style_text_align(dizhi_labels_[i], LV_TEXT_ALIGN_CENTER, 0);

        // 计算 label 尺寸 (font_puhui_20_4 的字符宽 ~20px)
        lv_obj_update_layout(dizhi_labels_[i]);
        lv_coord_t w = lv_obj_get_width(dizhi_labels_[i]);
        lv_coord_t h = lv_obj_get_height(dizhi_labels_[i]);

        // 居中
        lv_obj_set_pos(dizhi_labels_[i], px - w / 2, py - h / 2);
    }

    ESP_LOGI(TAG, "12 dizhi created successfully");
}

void CompassDizhi::Delete() {
    if (dizhi_container_ != nullptr) {
        lv_obj_delete(dizhi_container_);
        dizhi_container_ = nullptr;
    }
    for (int i = 0; i < 12; i++) {
        dizhi_labels_[i] = nullptr;
    }
}
