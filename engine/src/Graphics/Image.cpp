#include "okay/Graphics/Image.hpp"

#include "okay/Core/ImageDecode.hpp"
#include "okay/Core/ImageWrite.hpp"
#include "okay/Core/AssetManager.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Debug/DebugUtil.hpp"
#include <fstream>
#include <map>

namespace okay {

namespace {
thread_local int g_imageDirect = 0;
debugutil::SpinLock g_imageHoldLock;
struct HeldImage {
    AssetHandle<Image> handle;
    Image copy;
    std::uint64_t hash = 0;
    bool has = false;
};

std::uint64_t FileHash(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::uint64_t h = 14695981039346656037ull;
    char buf[4096];
    while (f) {
        f.read(buf, sizeof(buf));
        const auto n = f.gcount();
        for (std::streamsize i = 0; i < n; ++i) { h ^= (unsigned char)buf[i]; h *= 1099511628211ull; }
    }
    return h;
}
std::map<std::string, HeldImage> g_imageHeld;

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
    const std::uint64_t hash = FileHash(path);
    if (g_imageDirect == 0 && hash != 0) {
        debugutil::SpinGuard hold(g_imageHoldLock);
        auto it = g_imageHeld.find(path);
        if (it != g_imageHeld.end() && it->second.has && it->second.hash == hash) {
            *this = it->second.copy;
            return true;
        }
    }
    if (g_imageDirect == 0) {
        bool fresh = false;
        {
            debugutil::SpinGuard hold(g_imageHoldLock);
            fresh = g_imageHeld.find(path) == g_imageHeld.end();
        }
        if (fresh) {
            ++g_imageDirect;
            auto handle = AssetManager::Instance().Get<Image>(path, error);
            --g_imageDirect;
            if (!handle) { *this = Image(); return false; }
            *this = *handle.Get();
            debugutil::SpinGuard hold(g_imageHoldLock);
            g_imageHeld[path] = HeldImage{handle, *this, hash, true};
            return true;
        }
    }
    if (!LoadImageFile(*this, path, error)) return false;
    if (g_imageDirect == 0) {
        debugutil::SpinGuard hold(g_imageHoldLock);
        HeldImage& slot = g_imageHeld[path];
        slot.copy = *this;
        slot.hash = hash;
        slot.has = true;
    }
    return true;
}

bool Image::SavePNG(const std::string& path) const {
    if (!Valid()) return false;
    return ImageWrite::WritePNG(path, m_w, m_h, 4, m_pixels.data());
}

} // namespace okay
