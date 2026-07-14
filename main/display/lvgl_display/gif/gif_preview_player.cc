#include "gif_preview_player.h"

#include <esp_log.h>

extern "C" void lv_image_cache_drop(const void* src);

static const char* TAG = "GifPreviewPlayer";

GifPreviewPlayer& GifPreviewPlayer::GetInstance() {
    static GifPreviewPlayer instance;
    return instance;
}

bool GifPreviewPlayer::IsActive() const {
    return image_cache_ != nullptr || gif_controller_ != nullptr;
}

void GifPreviewPlayer::Hide() {
    HideUnlocked();
}

void GifPreviewPlayer::HideUnlocked() {
    if (hide_timer_ != nullptr) {
        lv_timer_del(hide_timer_);
        hide_timer_ = nullptr;
    }

    if (gif_controller_ != nullptr) {
        gif_controller_->Stop();
        gif_controller_.reset();
    }

    if (image_cache_ != nullptr) {
        const void* src = image_cache_->image_dsc();
        if (src != nullptr) {
            lv_image_cache_drop(src);
        }
        image_cache_.reset();
    }

    if (active_widget_ != nullptr) {
        lv_image_set_src(active_widget_, nullptr);
        lv_obj_add_flag(active_widget_, LV_OBJ_FLAG_HIDDEN);
        active_widget_ = nullptr;
    }

    if (target_.static_widget != nullptr && target_.static_widget != target_.gif_widget) {
        lv_image_set_src(target_.static_widget, nullptr);
        lv_obj_add_flag(target_.static_widget, LV_OBJ_FLAG_HIDDEN);
    }

    if (target_.on_after_hide) {
        target_.on_after_hide();
    }

    target_ = {};
}

void GifPreviewPlayer::OnHideTimer(lv_timer_t* timer) {
    auto* self = static_cast<GifPreviewPlayer*>(lv_timer_get_user_data(timer));
    if (self != nullptr) {
        self->HideUnlocked();
    }
}

bool GifPreviewPlayer::Show(std::unique_ptr<LvglImage> image, const GifPreviewTarget& target,
                            uint32_t timeout_ms, bool loop) {
    if (image == nullptr || target.gif_widget == nullptr) {
        ESP_LOGW(TAG, "Show: invalid image or widget");
        return false;
    }

    HideUnlocked();
    target_ = target;

    if (target_.on_before_show) {
        target_.on_before_show();
    }

    image_cache_ = std::move(image);
    const lv_img_dsc_t* img_dsc = image_cache_->image_dsc();
    if (img_dsc == nullptr) {
        ESP_LOGE(TAG, "Show: null image descriptor");
        HideUnlocked();
        return false;
    }

    const bool is_gif = image_cache_->IsGif();
    lv_obj_t* widget = target_.gif_widget;
    if (!is_gif && target_.static_widget != nullptr) {
        widget = target_.static_widget;
    }
    active_widget_ = widget;

    if (is_gif) {
        gif_controller_ = std::make_unique<LvglGif>(img_dsc);
        if (!gif_controller_->IsLoaded()) {
            ESP_LOGE(TAG, "Show: LvglGif load failed");
            HideUnlocked();
            return false;
        }

        gif_controller_->SetFrameCallback([this]() {
            if (gif_controller_ && active_widget_ != nullptr) {
                lv_image_set_src(active_widget_, gif_controller_->image_dsc());
                lv_obj_invalidate(active_widget_);
            }
        });

        if (loop) {
            gif_controller_->SetLoopCount(-1);
        } else {
            gif_controller_->SetLoopCount(1);
        }

        lv_image_set_src(active_widget_, gif_controller_->image_dsc());
        gif_controller_->Start();

        ESP_LOGI(TAG, "Show GIF %ux%u loop=%d timeout=%ums",
                 gif_controller_->width(), gif_controller_->height(), loop ? 1 : 0, timeout_ms);
    } else {
        const char* lvgl_path = image_cache_->GetLvglPath();
        if (lvgl_path != nullptr) {
            lv_image_set_src(active_widget_, lvgl_path);
        } else {
            lv_image_set_src(active_widget_, img_dsc);
        }

        if (target_.static_image_scale > 0 && img_dsc->header.w > 0) {
            lv_image_set_scale(active_widget_, target_.static_image_scale);
        }
        lv_obj_center(active_widget_);

        if (target_.gif_widget != nullptr && target_.gif_widget != active_widget_) {
            lv_obj_add_flag(target_.gif_widget, LV_OBJ_FLAG_HIDDEN);
        }

        ESP_LOGI(TAG, "Show static image cf=%d %ux%u timeout=%ums",
                 (int)img_dsc->header.cf, img_dsc->header.w, img_dsc->header.h, timeout_ms);
    }

    lv_obj_remove_flag(active_widget_, LV_OBJ_FLAG_HIDDEN);

    if (timeout_ms > 0) {
        hide_timer_ = lv_timer_create(OnHideTimer, timeout_ms, this);
    }

    return true;
}
