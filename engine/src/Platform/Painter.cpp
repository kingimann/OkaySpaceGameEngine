// okay::platform::Painter: the parts every implementation shares (the texture cache and the font atlas upload).
#include "okay/Platform/Painter.hpp"
#include "okay/Graphics/Image.hpp"
#include "okay/Graphics/TtfFont.hpp"
#include "okay/UI/UIFontPolicy.hpp"
#include "okay/UI/UIImageScale.hpp"

#include <atomic>

namespace okay::platform {

namespace {
std::atomic<std::size_t> g_texBytes{0};
std::atomic<int> g_texCount{0};
std::atomic<std::uint64_t> g_texChanges{0};
}
TextureTally PainterTextures() { return TextureTally{g_texBytes.load(), g_texCount.load()}; }
std::uint64_t PainterTextureChanges() { return g_texChanges.load(); }
void NotePainterTextureChange() { g_texChanges.fetch_add(1); }
void CountedTexture::Count(int w, int h) {
    const std::size_t now = w > 0 && h > 0 ? (std::size_t)w * (std::size_t)h * 4 : 0;
    if (now == m_bytes) return;
    NotePainterTextureChange();
    if (m_bytes == 0) g_texCount.fetch_add(1); else if (now == 0) g_texCount.fetch_sub(1);
    g_texBytes.fetch_add(now); g_texBytes.fetch_sub(m_bytes);
    m_bytes = now;
}

void Painter::GeometryRaw(ui2d::Tex t, const float* xy, int xyStride, const std::uint8_t* rgba, int rgbaStride, const float* uv, int uvStride,
                          int numVerts, const void* indices, int numIndices, int indexSize) {
    if (!xy || !rgba || numVerts <= 0 || numVerts > (1 << 24)) return;
    if (indices && indexSize != 1 && indexSize != 2 && indexSize != 4) return;
    std::vector<ui2d::Vertex> v((size_t)numVerts);
    for (int i = 0; i < numVerts; ++i) {
        const float* p = (const float*)((const std::uint8_t*)xy + (size_t)i * (size_t)xyStride);
        const std::uint8_t* c = rgba + (size_t)i * (size_t)rgbaStride;
        v[(size_t)i].position.x = p[0]; v[(size_t)i].position.y = p[1];
        v[(size_t)i].color.r = c[0]; v[(size_t)i].color.g = c[1]; v[(size_t)i].color.b = c[2]; v[(size_t)i].color.a = c[3];
        if (uv) { const float* q = (const float*)((const std::uint8_t*)uv + (size_t)i * (size_t)uvStride); v[(size_t)i].tex_coord.x = q[0]; v[(size_t)i].tex_coord.y = q[1]; }
        else { v[(size_t)i].tex_coord.x = 0; v[(size_t)i].tex_coord.y = 0; }
    }
    if (!indices || numIndices <= 0) { Geometry(t, v.data(), numVerts, nullptr, 0); return; }
    std::vector<int> idx((size_t)numIndices);
    for (int i = 0; i < numIndices; ++i)
        idx[(size_t)i] = indexSize == 1 ? (int)((const std::uint8_t*)indices)[i]
                       : indexSize == 2 ? (int)((const std::uint16_t*)indices)[i] : (int)((const std::uint32_t*)indices)[i];
    Geometry(t, v.data(), numVerts, idx.data(), numIndices);
}


void Painter::ReleaseCachedTextures() {
    for (auto& kv : m_cache) if (kv.second) DestroyTexture(kv.second);
    m_cache.clear();
    for (auto& kv : m_sized) if (kv.second) DestroyTexture(kv.second);
    m_sized.clear();
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
    d.scale = m_imgLinear ? ScaleMode::Linear : ScaleMode::Nearest;
    ui2d::Tex tex = CreateTexture(d, (const std::uint8_t*)img.Data());
    m_cache[path] = tex;
    return tex;
}

ui2d::Tex Painter::LoadTextureSized(const std::string& path, int w, int h) {
    if (path.empty() || w <= 0 || h <= 0) return LoadTexture(path);
    const std::string key = path + "@" + std::to_string(w) + "x" + std::to_string(h);
    auto it = m_sized.find(key);
    if (it != m_sized.end()) return it->second ? it->second : LoadTexture(path);
    okay::Image img;
    if (!img.Load(path) && !img.Load(m_baseDir + path)) { m_sized[key] = nullptr; return LoadTexture(path); }
    const okay::Image scaled = okay::ui2d::UIDownscaleImage(img, w, h, false);
    TextureDesc d;
    d.width = scaled.Width(); d.height = scaled.Height();
    d.scale = ScaleMode::Linear;
    ui2d::Tex tex = CreateTexture(d, (const std::uint8_t*)scaled.Data());
    m_sized[key] = tex;
    return tex ? tex : LoadTexture(path);
}

void Painter::SetImageFilter(bool linear) {
    if (linear == m_imgLinear) return;
    m_imgLinear = linear;
    for (auto& kv : m_cache)
        if (kv.second) SetTextureScaleMode(kv.second, linear ? ScaleMode::Linear : ScaleMode::Nearest);
}

void Painter::SetTextureFilter(ui2d::Tex t, bool linear) {
    if (t) SetTextureScaleMode(t, linear ? ScaleMode::Linear : ScaleMode::Nearest);
}

ui2d::Tex Painter::FontAtlas(okay::TtfFont* f) {
    if (!f || !f->Valid()) return nullptr;
    FontUp& u = m_fonts[f];
    const okay::Image& a = f->Atlas();
    if (u.tex && u.gen == f->Generation()) return u.tex;
    if (u.tex && (u.w != a.Width() || u.h != a.Height())) { DestroyTexture(u.tex); u.tex = nullptr; }
    if (!u.tex) {
        TextureDesc d;
        d.width = a.Width(); d.height = a.Height();
        d.scale = UIGlyphAtlasLinearSampling() ? ScaleMode::Linear : ScaleMode::Nearest;
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
