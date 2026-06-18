#include "okay/Graphics/Image.hpp"

// The single translation unit that compiles the vendored stb implementations.
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h"

namespace okay {

bool Image::Load(const std::string& path, std::string* error) {
    int w = 0, h = 0, channels = 0;
    // Force 4 channels (RGBA) so the buffer layout is always predictable.
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!data) {
        if (error) *error = stbi_failure_reason() ? stbi_failure_reason() : "load failed";
        m_w = m_h = 0; m_pixels.clear();
        return false;
    }
    m_w = w; m_h = h;
    m_pixels.assign(data, data + static_cast<std::size_t>(w) * h * 4);
    stbi_image_free(data);
    return true;
}

bool Image::SavePNG(const std::string& path) const {
    if (!Valid()) return false;
    int stride = m_w * 4;
    return stbi_write_png(path.c_str(), m_w, m_h, 4, m_pixels.data(), stride) != 0;
}

} // namespace okay
