#include "okay/Graphics/Image.hpp"

#include "okay/Core/ImageDecode.hpp"
#include "okay/Core/ImageWrite.hpp"
#include "okay/Core/AssetManager.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Debug/DebugUtil.hpp"
#include <map>

namespace okay {

namespace {
thread_local int g_imageDirect = 0;
debugutil::SpinLock g_imageHoldLock;
std::map<std::string, AssetHandle<Image>> g_imageHeld;

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
    if (g_imageDirect == 0) {
        ++g_imageDirect;
        auto handle = AssetManager::Instance().Get<Image>(path, error);
        --g_imageDirect;
        if (!handle) { *this = Image(); return false; }
        {
            debugutil::SpinGuard hold(g_imageHoldLock);
            g_imageHeld[handle.Path()] = handle;
        }
        *this = *handle.Get();
        return true;
    }
    return LoadImageFile(*this, path, error);
}

bool Image::SavePNG(const std::string& path) const {
    if (!Valid()) return false;
    return ImageWrite::WritePNG(path, m_w, m_h, 4, m_pixels.data());
}

} // namespace okay
