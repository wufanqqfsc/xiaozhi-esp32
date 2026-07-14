#pragma once

#include <cstddef>
#include <memory>

class LvglImage;

namespace GifImageLoader {

/** Max GIF/image file size for preview (512 KB). */
constexpr size_t kDefaultMaxBytes = 512 * 1024;

/** Read a file into PSRAM (fallback internal) and wrap as LvglAllocatedImage. */
std::unique_ptr<LvglImage> LoadFromFile(const char* path, size_t max_bytes = kDefaultMaxBytes);

}  // namespace GifImageLoader
