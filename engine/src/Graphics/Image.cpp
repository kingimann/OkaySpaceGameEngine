#include "okay/Graphics/Image.hpp"

#include "okay/Core/ImageDecode.hpp"
#include "okay/Core/ImageWrite.hpp"
#include "okay/Core/Log.hpp"

namespace okay {

namespace {
// A direct decode. Image::Load does not go through AssetManager: that route pinned every
// image for the process, handed back the old pixels after a rewrite, and the Xbox export
// (which links this file and not the manager) failed to link.
bool LoadImageFile(Image& img, const std::string& path, std::string* error) {
    DecodedImage dec;
    std::string why;
    if (!DecodeImageFile(path, dec, &why)) {
        if (why.empty()) why = "load failed";
        if (error) *error = why;
        Log::Warning("[asset] cannot load image '", path, "': ", why);
        img = Image();
        return false;
    }
    img.Set(dec.width, dec.height, std::move(dec.rgba));
    return true;
}
}

bool Image::Load(const std::string& path, std::string* error) {
    return LoadImageFile(*this, path, error);
}

bool Image::SavePNG(const std::string& path) const {
    if (!Valid()) return false;
    return ImageWrite::WritePNG(path, m_w, m_h, 4, m_pixels.data());
}

} // namespace okay
