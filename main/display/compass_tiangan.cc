#include "compass_tiangan.h"
#include <esp_log.h>
#include <cmath>

static const char* TAG = "CompassTiangan";

lv_obj_t* CompassTiangan::tiangan_container_ = nullptr;
lv_obj_t* CompassTiangan::tiangan_labels_[10] = {nullptr};

// 10 天干字符 (从正北/甲 开始, 顺时针)
const char* CompassTiangan::tiangan_chars_[10] = {
    "甲", "乙", "丙", "丁", "戊", "己", "庚", "辛", "壬", "癸"
};

LV_FONT_DECLARE(BUILTIN_TEXT_FONT);

void CompassTiangan::Create(lv_obj_t* parent, int cx, int cy, int radius) {
    if (parent == nullptr) {
        ESP_LOGE(TAG, "Create: parent is null");
        return;
    }

    ESP_LOGI(TAG, "Creating 10 tiangan at (%d, %d) radius=%d", cx, cy, radius);

    tiangan_container_ = lv_obj_create(parent);
    lv_obj_set_size(tiangan_container_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_pos(tiangan_container_, 0, 0);
    lv_obj_set_style_bg_opa(tiangan_container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tiangan_container_, 0, 0);
    lv_obj_clear_flag(tiangan_container_, LV_OBJ_FLAG_CLICKABLE);

    // 10 天干沿圆周均匀分布 (每 36° = 360/10)
    const float angle_step_rad = 2.0f * M_PI / 10.0f;

    for (int i = 0; i < 10; i++) {
        float angle = -M_PI / 2.0f + i * angle_step_rad;

        int px = cx + (int)(radius * cosf(angle));
        int py = cy + (int)(radius * sinf(angle));

        tiangan_labels_[i] = lv_label_create(tiangan_container_);
        lv_label_set_text(tiangan_labels_[i], tiangan_chars_[i]);
        lv_obj_set_style_text_color(tiangan_labels_[i], lv_color_hex(0xD4AF37), 0);
        lv_obj_set_style_text_font(tiangan_labels_[i], &BUILTIN_TEXT_FONT, 0);
        lv_obj_set_style_text_align(tiangan_labels_[i], LV_TEXT_ALIGN_CENTER, 0);

        lv_obj_update_layout(tiangan_labels_[i]);
        lv_coord_t w = lv_obj_get_width(tiangan_labels_[i]);
        lv_coord_t h = lv_obj_get_height(tiangan_labels_[i]);
        lv_obj_set_pos(tiangan_labels_[i], px - w / 2, py - h / 2);
    }

    ESP_LOGI(TAG, "10 tiangan created successfully");
}

void CompassTiangan::Delete() {
    if (tiangan_container_ != nullptr) {
        lv_obj_delete(tiangan_container_);
        tiangan_container_ = nullptr;
    }
    for (int i = 0; i < 10; i++) {
        tiangan_labels_[i] = nullptr;
    }
}
