// okay::platform::Painter: the parts every implementation shares (the texture cache and the font atlas upload).
#include "okay/Platform/Painter.hpp"
#include "okay/Graphics/Image.hpp"
#include "okay/Graphics/TtfFont.hpp"

namespace okay::platform {

void Painter::ReleaseCachedTextures() {
    for (auto& kv : m_cache) if (kv.second) DestroyTexture(kv.second);
    m_cache.clear();
    for (auto& kv : m_fonts) if (kv.second.tex) DestroyTexture(kv.second.tex);
    m_fonts.clear();
}

ui2d::Tex Painter::CachedTexture(const std::string& path) const {
    auto it = m_cache.find(path);
    return it == m_cache.end() ? nullptr : it->second;
}

ui2d::Tex Painter::LoadTexture(const std::string& path) {
    if (path.empty()) return nullptr;
    auto it = m_cache.find(path);
    if (it != m_cache.end()) return it->second;
    okay::Image img;
    if (!img.Load(path) && !img.Load(m_baseDir + path)) {
        m_cache[path] = nullptr;   // remember the miss so a bad path is not decoded every frame
        return nullptr;
    }
    TextureDesc d;
    d.width = img.Width(); d.height = img.Height();
    ui2d::Tex tex = CreateTexture(d, (const std::uint8_t*)img.Data());
    m_cache[path] = tex;
    return tex;
}

ui2d::Tex Painter::FontAtlas(okay::TtfFont* f) {
    if (!f || !f->Valid()) return nullptr;
    FontUp& u = m_fonts[f];
    const okay::Image& a = f->Atlas();
    if (u.tex && u.gen == f->Generation()) return u.tex;
    if (u.tex && (u.w != a.Width() || u.h != a.Height())) { DestroyTexture(u.tex); u.tex = nullptr; }
    if (!u.tex) {
        TextureDesc d;
        d.width = a.Width(); d.height = a.Height(); d.scale = ScaleMode::Linear;
        u.tex = CreateTexture(d, nullptr);
        u.w = a.Width(); u.h = a.Height();
    }
    if (u.tex) UpdateTexture(u.tex, nullptr, a.Data(), a.Width() * 4);
    u.gen = f->Generation();
    return u.tex;
}

ui2d::Tex Painter::UploadAtlas(const okay::Image& page, int pageIndex, ui2d::Tex tex) {
    int w = 0, h = 0;
    if (tex) QueryTexture(tex, &w, &h);
    if (tex && (w != page.Width() || h != page.Height())) { DestroyTexture(tex); tex = nullptr; }
    if (!tex) {
        TextureDesc d;
        d.width = page.Width(); d.height = page.Height();
        d.scale = pageIndex == 0 ? ScaleMode::Linear : ScaleMode::Nearest;
        tex = CreateTexture(d, nullptr);
        if (!tex) return nullptr;
    }
    UpdateTexture(tex, nullptr, page.Data(), page.Width() * 4);
    return tex;
}

} // namespace okay::platform
