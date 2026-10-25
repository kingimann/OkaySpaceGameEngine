#include "okay/Graphics/Image.hpp"

// The single translation unit that compiles the vendored stb implementations.
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
// Refuse absurd sizes before decoding: a 40-byte TGA header can claim
// 52032 x 8192 and make stb allocate gigabytes.
#define STBI_MAX_DIMENSIONS 16384
#include "stb_image.h"
#include "stb_image_write.h"

namespace okay {

bool Image::Load(const std::string& path, std::string* error) {
    int w = 0, h = 0, channels = 0;
    // Check the header first, so an oversized image is rejected without decoding
    // (most stb decoders also enforce STBI_MAX_DIMENSIONS; this covers the rest).
    if (stbi_info(path.c_str(), &w, &h, &channels) &&
        (w <= 0 || h <= 0 || w > STBI_MAX_DIMENSIONS || h > STBI_MAX_DIMENSIONS)) {
        if (error) *error = "image too large (" + std::to_string(w) + "x" + std::to_string(h) + ")";
        m_w = m_h = 0; m_pixels.clear();
        return false;
    }
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
