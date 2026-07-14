#ifndef IMAGE_PREVIEW_VIEW_H
#define IMAGE_PREVIEW_VIEW_H

#include <lvgl.h>

/**
 * 独立图片预览视图：全屏独立 screen，用于显示静态图/GIF。
 * 显示时切换 screen，隐藏时恢复到进入前的 screen。
 */
class ImagePreviewView {
public:
    static ImagePreviewView& GetInstance();

    static constexpr int kCardSize = 354;
    static constexpr int kCardX = (360 - kCardSize) / 2;
    static constexpr int kCardY = (360 - kCardSize) / 2;

    void Show();
    void Hide();
    bool IsVisible() const { return visible_; }

    lv_obj_t* GetGifWidget() const { return preview_gif_; }
    lv_obj_t* GetStaticWidget() const { return preview_image_; }
    lv_obj_t* GetScreen() const { return screen_; }

private:
    ImagePreviewView();
    ~ImagePreviewView();

    ImagePreviewView(const ImagePreviewView&) = delete;
    ImagePreviewView& operator=(const ImagePreviewView&) = delete;

    void CreateUI();

    lv_obj_t* screen_ = nullptr;
    lv_obj_t* overlay_card_ = nullptr;
    lv_obj_t* preview_image_ = nullptr;
    lv_obj_t* preview_gif_ = nullptr;
    lv_obj_t* prev_screen_ = nullptr;
    bool visible_ = false;
};

#endif  // IMAGE_PREVIEW_VIEW_H
