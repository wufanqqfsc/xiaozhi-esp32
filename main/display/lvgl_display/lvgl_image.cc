#include "lvgl_image.h"
#include <cbin_font.h>

#include <esp_log.h>
#include <stdexcept>
#include <cstring>
#include <esp_heap_caps.h>

#define TAG "LvglImage"


LvglRawImage::LvglRawImage(void* data, size_t size) {
    bzero(&image_dsc_, sizeof(image_dsc_));
    image_dsc_.data_size = size;
    image_dsc_.data = static_cast<uint8_t*>(data);
    image_dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
    image_dsc_.header.cf = LV_COLOR_FORMAT_RAW_ALPHA;
    image_dsc_.header.w = 0;
    image_dsc_.header.h = 0;
}

bool LvglRawImage::IsGif() const {
    auto ptr = (const uint8_t*)image_dsc_.data;
    return ptr[0] == 'G' && ptr[1] == 'I' && ptr[2] == 'F';
}

LvglCBinImage::LvglCBinImage(void* data) {
    image_dsc_ = cbin_img_dsc_create(static_cast<uint8_t*>(data));
}

LvglCBinImage::~LvglCBinImage() {
    if (image_dsc_ != nullptr) {
        cbin_img_dsc_delete(image_dsc_);
    }
}

// 2 参构造：从原始 buffer 构造，支持 PNG / JPG / BMP 等所有 LVGL 内置 decoder 支持的格式
//   GIF 不要走这条路 —— 应该用 LvglRawImage + lv_gif widget（lv_gif 内置 AnimatedGIF 库解码）
// 关键设计：故意把 header.cf 设为 UNKNOWN：
//   - LVGL 内置 BIN decoder 看到 UNKNOWN 直接拒绝（不会"赢"）
//   - decoder chain 会继续交给 lodepng/tjpgd/bmp 等按 magic bytes 识别的 decoder
//   - 例如 lodepng 检查 "\x89PNG\r\n\x1a\n"，tjpgd 检查 "\xFF\xD8\xFF"
//   - 在 lv_image_set_src 阶段，正确的 header.cf / w / h 会被对应 decoder 填充
LvglAllocatedImage::LvglAllocatedImage(void* data, size_t size) {
    bzero(&image_dsc_, sizeof(image_dsc_));
    image_dsc_.data_size = size;
    image_dsc_.data = static_cast<uint8_t*>(data);
    image_dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
    image_dsc_.header.cf = LV_COLOR_FORMAT_UNKNOWN;
    ESP_LOGD(TAG, "LvglAllocatedImage ctor: data=%p size=%u first_byte=0x%02X cf=UNKNOWN (decoder will detect)",
             data, (unsigned)size,
             (size > 0 && data != nullptr) ? (unsigned)((uint8_t*)data)[0] : 0);
}

LvglAllocatedImage::LvglAllocatedImage(void* data, size_t size, int width, int height, int stride, int color_format) {
    bzero(&image_dsc_, sizeof(image_dsc_));
    image_dsc_.data_size = size;
    image_dsc_.data = static_cast<uint8_t*>(data);
    image_dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
    image_dsc_.header.cf = color_format;
    image_dsc_.header.w = width;
    image_dsc_.header.h = height;
    image_dsc_.header.stride = stride;
}

LvglAllocatedImage::~LvglAllocatedImage() {
    if (image_dsc_.data) {
        heap_caps_free((void*)image_dsc_.data);
        image_dsc_.data = nullptr;
    }
}