#include "okay/Graphics/Image.hpp"

#include "okay/Core/ImageDecode.hpp"
#include "okay/Core/ImageWrite.hpp"
#include "okay/Core/Log.hpp"

namespace okay {

bool Image::Load(const std::string& path, std::string* error) {
    // Our own decoder (Core/ImageDecode): PNG, JPG, BMP, TGA, GIF (first frame), HDR (tone-mapped), PSD.
    // It refuses absurd sizes and headers that claim far more pixels than the file could hold.
    // The buffer is always RGBA8.
    DecodedImage img;
    std::string why;
    if (!DecodeImageFile(path, img, &why)) {
        if (why.empty()) why = "load failed";
        if (error) *error = why;
        Log::Warning("[asset] cannot load image '", path, "': ", why);
        m_w = m_h = 0; m_pixels.clear();
        return false;
    }
    m_w = img.width; m_h = img.height;
    m_pixels = std::move(img.rgba);
    return true;
}

bool Image::SavePNG(const std::string& path) const {
    if (!Valid()) return false;
    return ImageWrite::WritePNG(path, m_w, m_h, 4, m_pixels.data());
}

} // namespace okay
