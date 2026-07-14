#include "image_preview_view.h"

#include <esp_log.h>

static const char* TAG = "ImagePreviewView";

ImagePreviewView& ImagePreviewView::GetInstance() {
    static ImagePreviewView instance;
    return instance;
}

ImagePreviewView::ImagePreviewView() {
    CreateUI();
}

ImagePreviewView::~ImagePreviewView() {
    if (screen_ != nullptr) {
        lv_obj_del(screen_);
        screen_ = nullptr;
    }
    overlay_card_ = nullptr;
    preview_image_ = nullptr;
    preview_gif_ = nullptr;
}

void ImagePreviewView::CreateUI() {
    if (screen_ != nullptr) {
        return;
    }

    screen_ = lv_obj_create(nullptr);
    lv_obj_clear_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen_, lv_color_hex(0x020611), 0);
    lv_obj_set_style_bg_opa(screen_, LV_OPA_COVER, 0);

    overlay_card_ = lv_obj_create(screen_);
    lv_obj_set_size(overlay_card_, kCardSize, kCardSize);
    lv_obj_set_pos(overlay_card_, kCardX, kCardY);
    lv_obj_set_style_radius(overlay_card_, kCardSize / 2, 0);
    lv_obj_set_style_clip_corner(overlay_card_, true, 0);
    lv_obj_set_style_bg_color(overlay_card_, lv_color_hex(0x0A1414), 0);
    lv_obj_set_style_bg_opa(overlay_card_, LV_OPA_90, 0);
    lv_obj_set_style_border_color(overlay_card_, lv_color_hex(0x00C8C8), 0);
    lv_obj_set_style_border_width(overlay_card_, 2, 0);
    lv_obj_set_style_pad_all(overlay_card_, 0, 0);
    lv_obj_clear_flag(overlay_card_, LV_OBJ_FLAG_CLICKABLE);

    preview_image_ = lv_image_create(overlay_card_);
    lv_obj_set_size(preview_image_, kCardSize, kCardSize);
    lv_obj_center(preview_image_);
    lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);

    preview_gif_ = lv_image_create(overlay_card_);
    lv_obj_set_size(preview_gif_, kCardSize, kCardSize);
    lv_obj_center(preview_gif_);
    lv_obj_add_flag(preview_gif_, LV_OBJ_FLAG_HIDDEN);

    ESP_LOGI(TAG, "ImagePreviewView UI created");
}

void ImagePreviewView::Show() {
    if (screen_ == nullptr) {
        ESP_LOGE(TAG, "Show: screen not created");
        return;
    }

    prev_screen_ = lv_screen_active();
    lv_screen_load(screen_);
    visible_ = true;
    ESP_LOGI(TAG, "ImagePreviewView shown, prev_screen=%p", prev_screen_);
}

void ImagePreviewView::Hide() {
    if (!visible_) {
        return;
    }

    if (preview_image_ != nullptr) {
        lv_image_set_src(preview_image_, nullptr);
        lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
    }
    if (preview_gif_ != nullptr) {
        lv_image_set_src(preview_gif_, nullptr);
        lv_obj_add_flag(preview_gif_, LV_OBJ_FLAG_HIDDEN);
    }

    if (prev_screen_ != nullptr) {
        lv_screen_load(prev_screen_);
        ESP_LOGI(TAG, "ImagePreviewView hidden, restored prev_screen=%p", prev_screen_);
    }
    prev_screen_ = nullptr;
    visible_ = false;
}
