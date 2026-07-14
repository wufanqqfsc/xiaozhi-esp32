#pragma once

#include "../lvgl_image.h"
#include "lvgl_gif.h"

#include <lvgl.h>

#include <cstdint>
#include <functional>
#include <memory>

/**
 * Unified GIF/static image preview player.
 * Only one active preview session at a time (globally).
 */
struct GifPreviewTarget {
    lv_obj_t* gif_widget = nullptr;
    lv_obj_t* static_widget = nullptr;
    int static_image_scale = 0;
    std::function<void()> on_before_show;
    std::function<void()> on_after_hide;
};

class GifPreviewPlayer {
public:
    static GifPreviewPlayer& GetInstance();

    bool Show(std::unique_ptr<LvglImage> image, const GifPreviewTarget& target,
              uint32_t timeout_ms, bool loop = true);
    void Hide();
    bool IsActive() const;

private:
    GifPreviewPlayer() = default;
    GifPreviewPlayer(const GifPreviewPlayer&) = delete;
    GifPreviewPlayer& operator=(const GifPreviewPlayer&) = delete;

    void HideUnlocked();
    static void OnHideTimer(lv_timer_t* timer);

    std::unique_ptr<LvglImage> image_cache_;
    std::unique_ptr<LvglGif> gif_controller_;
    lv_timer_t* hide_timer_ = nullptr;
    GifPreviewTarget target_;
    lv_obj_t* active_widget_ = nullptr;
};
