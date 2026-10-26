#include "okay/Graphics/Image.hpp"

// The single translation unit that compiles the vendored stb implementations.
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
// Refuse absurd sizes before decoding: a 40-byte TGA header can claim
// 52032 x 8192 and make stb allocate gigabytes.
#define STBI_MAX_DIMENSIONS 16384
#include "stb_image.h"
#include "stb_image_write.h"

#include <filesystem>

namespace okay {

bool Image::Load(const std::string& path, std::string* error) {
    int w = 0, h = 0, channels = 0;
    // stb rejects anything over STBI_MAX_DIMENSIONS on its own. Within that, a
    // header can still claim far more pixels than the file could encode (a 40-byte
    // TGA "16384 x 16384" decodes into 1 GB of zeros), so compare the claimed RGBA
    // size with the file size first. 4096:1 is beyond any real compression ratio
    // stb decodes (deflate tops out near 1032:1), and only large claims are checked.
    if (stbi_info(path.c_str(), &w, &h, &channels)) {
        std::error_code ec;
        const unsigned long long fileBytes = std::filesystem::file_size(path, ec);
        const unsigned long long rgba = (unsigned long long)w * (unsigned long long)h * 4ULL;
        if (!ec && rgba > (64ULL << 20) && rgba / 4096ULL > fileBytes) {
            if (error) *error = "image header claims " + std::to_string(w) + "x" + std::to_string(h) +
                                " but the file is only " + std::to_string(fileBytes) + " bytes";
            m_w = m_h = 0; m_pixels.clear();
            return false;
        }
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
