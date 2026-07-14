#include "gif_image_loader.h"

#include "../lvgl_image.h"

#include <esp_heap_caps.h>
#include <esp_log.h>

#include <cstdio>
#include <cstring>

static const char* TAG = "GifImageLoader";

namespace GifImageLoader {

std::unique_ptr<LvglImage> LoadFromFile(const char* path, size_t max_bytes) {
    if (path == nullptr || path[0] == '\0') {
        return nullptr;
    }

    FILE* f = fopen(path, "rb");
    if (f == nullptr) {
        ESP_LOGE(TAG, "cannot open %s", path);
        return nullptr;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return nullptr;
    }

    long fsize = ftell(f);
    if (fsize <= 0 || static_cast<size_t>(fsize) > max_bytes) {
        ESP_LOGE(TAG, "invalid size %ld for %s (max %u)", fsize, path, (unsigned)max_bytes);
        fclose(f);
        return nullptr;
    }

    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return nullptr;
    }

    uint8_t* data = static_cast<uint8_t*>(heap_caps_malloc(static_cast<size_t>(fsize), MALLOC_CAP_SPIRAM));
    if (data == nullptr) {
        data = static_cast<uint8_t*>(malloc(static_cast<size_t>(fsize)));
    }
    if (data == nullptr) {
        ESP_LOGE(TAG, "alloc failed for %s (%ld bytes)", path, fsize);
        fclose(f);
        return nullptr;
    }

    if (fread(data, 1, static_cast<size_t>(fsize), f) != static_cast<size_t>(fsize)) {
        heap_caps_free(data);
        fclose(f);
        ESP_LOGE(TAG, "read failed for %s", path);
        return nullptr;
    }
    fclose(f);

    ESP_LOGI(TAG, "loaded %s (%ld bytes)", path, fsize);
    return std::make_unique<LvglAllocatedImage>(data, static_cast<size_t>(fsize));
}

}  // namespace GifImageLoader
