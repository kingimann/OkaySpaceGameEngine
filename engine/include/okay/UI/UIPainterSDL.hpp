#pragma once
// ui2d::Painter over an SDL_Renderer: a straight pass-through to the SDL calls the UI
// pass was written against. Header-only so the engine library stays SDL-free; include
// it only from code that links SDL2 (the player, SDL-based tests).
#include "okay/UI/UIPainter.hpp"
#include "okay/UI/UIImageScale.hpp"
#include "okay/Graphics/Image.hpp"
#include "okay/Graphics/TtfFont.hpp"
#include <SDL.h>
#include <string>
#include <unordered_map>

namespace okay {
namespace ui2d {

static_assert(sizeof(Rect) == sizeof(SDL_Rect) && sizeof(FRect) == sizeof(SDL_FRect) &&
              sizeof(Point) == sizeof(SDL_Point) && sizeof(FPoint) == sizeof(SDL_FPoint) &&
              sizeof(RGBA) == sizeof(SDL_Color) && sizeof(Vertex) == sizeof(SDL_Vertex),
              "ui2d types must match SDL's layout");
static_assert((int)FlipH == (int)SDL_FLIP_HORIZONTAL && (int)FlipV == (int)SDL_FLIP_VERTICAL, "flip flags");

/// Load (and cache) a texture from an image file. Returns nullptr if the image can't
/// be read; a null entry is cached for misses so a bad path isn't decoded every frame.
inline SDL_Texture* LoadSDLTexture(SDL_Renderer* r, const std::string& path, const std::string& baseDir,
                                   std::unordered_map<std::string, SDL_Texture*>& cache) {
    if (path.empty()) return nullptr;
    auto it = cache.find(path);
    if (it != cache.end()) return it->second;
    Image img;
    if (!img.Load(path) && !img.Load(baseDir + path)) {
        cache[path] = nullptr;   // remember the miss
        return nullptr;
    }
    SDL_Texture* tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888,
                                         SDL_TEXTUREACCESS_STATIC, img.Width(), img.Height());
    if (tex) {
        SDL_UpdateTexture(tex, nullptr, img.Data(), img.Width() * 4);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    }
    cache[path] = tex;
    return tex;
}

/// The uploaded glyph atlases: one per (renderer, font), so a second renderer in the same process (a test, a second window) gets its own texture
/// instead of the first one's. A renderer that is about to be destroyed calls SDLForgetRenderer, which drops its entries (the textures go with it).
struct SDLAtlasUp { SDL_Texture* tex = nullptr; std::uint32_t gen = 0; int w = 0, h = 0; };
inline std::unordered_map<std::string, SDLAtlasUp>& SDLAtlasCache() { static std::unordered_map<std::string, SDLAtlasUp> c; return c; }
inline std::string SDLAtlasKey(SDL_Renderer* r, TtfFont* f) { return std::to_string((unsigned long long)(std::uintptr_t)r) + ":" + std::to_string((unsigned long long)(std::uintptr_t)f); }
inline void SDLForgetRenderer(SDL_Renderer* r) {
    const std::string prefix = std::to_string((unsigned long long)(std::uintptr_t)r) + ":";
    auto& c = SDLAtlasCache();
    for (auto it = c.begin(); it != c.end();) it = it->first.compare(0, prefix.size(), prefix) == 0 ? c.erase(it) : std::next(it);
}

/// Upload a TTF glyph atlas as an SDL texture (cached per renderer and font; the player has one renderer for the process). Uploaded again when the
/// font baked new characters (TtfFont::Generation).
inline SDL_Texture* SDLFontAtlas(SDL_Renderer* r, TtfFont* f) {
    if (!f || !f->Valid()) return nullptr;
    SDLAtlasUp& u = SDLAtlasCache()[SDLAtlasKey(r, f)];
    const Image& a = f->Atlas();
    if (u.tex && u.gen == f->Generation()) return u.tex;
    if (u.tex && (u.w != a.Width() || u.h != a.Height())) { SDL_DestroyTexture(u.tex); u.tex = nullptr; }
    if (!u.tex) {
        u.tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, a.Width(), a.Height());
        if (u.tex) {
            SDL_SetTextureBlendMode(u.tex, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(u.tex, SDL_ScaleModeLinear);
        }
        u.w = a.Width(); u.h = a.Height();
    }
    if (u.tex) SDL_UpdateTexture(u.tex, nullptr, a.Data(), a.Width() * 4);
    u.gen = f->Generation();
    return u.tex;
}

class SDLPainter final : public Painter {
public:
    /// `cache`/`baseDir` resolve LoadTexture; without a cache LoadTexture returns null.
    explicit SDLPainter(SDL_Renderer* r, std::unordered_map<std::string, SDL_Texture*>* cache = nullptr,
                        std::string baseDir = {})
        : m_r(r), m_cache(cache), m_baseDir(std::move(baseDir)) {}

    void SetDrawColor(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) override {
        SDL_SetRenderDrawColor(m_r, r, g, b, a);
    }
    void FillRect(const Rect* r) override { SDL_RenderFillRect(m_r, (const SDL_Rect*)r); }
    void FillRectF(const FRect* r) override { SDL_RenderFillRectF(m_r, (const SDL_FRect*)r); }
    void DrawRect(const Rect* r) override { SDL_RenderDrawRect(m_r, (const SDL_Rect*)r); }
    void DrawLine(int x0, int y0, int x1, int y1) override { SDL_RenderDrawLine(m_r, x0, y0, x1, y1); }
    void DrawPoint(int x, int y) override { SDL_RenderDrawPoint(m_r, x, y); }

    void SetClipRect(const Rect* r) override { SDL_RenderSetClipRect(m_r, (const SDL_Rect*)r); }
    void GetClipRect(Rect* r) override { SDL_RenderGetClipRect(m_r, (SDL_Rect*)r); }
    bool IsClipEnabled() override { return SDL_RenderIsClipEnabled(m_r) == SDL_TRUE; }
    void SetScale(float sx, float sy) override { SDL_RenderSetScale(m_r, sx, sy); }

    Tex LoadTexture(const std::string& path) override {
        SDL_Texture* t = m_cache ? LoadSDLTexture(m_r, path, m_baseDir, *m_cache) : nullptr;
        if (t && m_filterSet) SDL_SetTextureScaleMode(t, m_linear ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
        return t;
    }
    Tex LoadTextureSized(const std::string& path, int w, int h) override {
        if (!m_cache || path.empty() || w <= 0 || h <= 0) return LoadTexture(path);
        const std::string key = path + "@" + std::to_string(w) + "x" + std::to_string(h);
        auto it = m_cache->find(key);
        if (it != m_cache->end()) return it->second ? (Tex)it->second : LoadTexture(path);
        Image img;
        if (!img.Load(path) && !img.Load(m_baseDir + path)) return LoadTexture(path);
        const Image small = UIDownscaleImage(img, w, h, false);
        SDL_Texture* tex = SDL_CreateTexture(m_r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, small.Width(), small.Height());
        if (!tex) return LoadTexture(path);
        SDL_UpdateTexture(tex, nullptr, small.Data(), small.Width() * 4);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);
        (*m_cache)[key] = tex;
        return tex;
    }
    void SetImageFilter(bool linear) override { m_linear = linear; m_filterSet = true; }
    void SetTextureFilter(Tex t, bool linear) override {
        if (t) SDL_SetTextureScaleMode((SDL_Texture*)t, linear ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
    }
    Tex FontAtlas(TtfFont* font) override { return SDLFontAtlas(m_r, font); }
    void QueryTexture(Tex t, int* w, int* h) override { SDL_QueryTexture((SDL_Texture*)t, nullptr, nullptr, w, h); }
    void SetTextureColorMod(Tex t, std::uint8_t r, std::uint8_t g, std::uint8_t b) override {
        SDL_SetTextureColorMod((SDL_Texture*)t, r, g, b);
    }
    void SetTextureAlphaMod(Tex t, std::uint8_t a) override { SDL_SetTextureAlphaMod((SDL_Texture*)t, a); }
    void Copy(Tex t, const Rect* src, const Rect* dst) override {
        SDL_RenderCopy(m_r, (SDL_Texture*)t, (const SDL_Rect*)src, (const SDL_Rect*)dst);
    }
    void CopyF(Tex t, const Rect* src, const FRect* dst) override {
        SDL_RenderCopyF(m_r, (SDL_Texture*)t, (const SDL_Rect*)src, (const SDL_FRect*)dst);
    }
    void CopyEx(Tex t, const Rect* src, const Rect* dst, double angle, const Point* center, int flip) override {
        SDL_RenderCopyEx(m_r, (SDL_Texture*)t, (const SDL_Rect*)src, (const SDL_Rect*)dst, angle,
                         (const SDL_Point*)center, (SDL_RendererFlip)flip);
    }
    void CopyExF(Tex t, const Rect* src, const FRect* dst, double angle, const FPoint* center, int flip) override {
        SDL_RenderCopyExF(m_r, (SDL_Texture*)t, (const SDL_Rect*)src, (const SDL_FRect*)dst, angle,
                          (const SDL_FPoint*)center, (SDL_RendererFlip)flip);
    }
    void Geometry(Tex t, const Vertex* v, int nv, const int* idx, int ni) override {
        SDL_RenderGeometry(m_r, (SDL_Texture*)t, (const SDL_Vertex*)v, nv, idx, ni);
    }
    // Atlas pages for BatchPainter: page 0 (fonts) samples linearly, page 1 (images)
    // nearest, matching the textures above. A page that grew gets a new texture.
    Tex UploadAtlas(const Image& page, int pageIndex, Tex existing) override {
        SDL_Texture* tex = (SDL_Texture*)existing;
        int w = 0, h = 0;
        if (tex) SDL_QueryTexture(tex, nullptr, nullptr, &w, &h);
        if (tex && (w != page.Width() || h != page.Height())) { SDL_DestroyTexture(tex); tex = nullptr; }
        if (!tex) {
            tex = SDL_CreateTexture(m_r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, page.Width(), page.Height());
            if (!tex) return nullptr;
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(tex, pageIndex == 0 ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
        }
        SDL_UpdateTexture(tex, nullptr, page.Data(), page.Width() * 4);
        return tex;
    }

private:
    SDL_Renderer* m_r;
    std::unordered_map<std::string, SDL_Texture*>* m_cache;
    std::string m_baseDir;
    bool m_linear = false, m_filterSet = false;
};

inline const Rect& FromSDL(const SDL_Rect& r) { return *(const Rect*)&r; }
inline RGBA FromSDL(SDL_Color c) { return RGBA{c.r, c.g, c.b, c.a}; }
inline Point FromSDL(SDL_Point p) { return Point{p.x, p.y}; }

} // namespace ui2d
} // namespace okay
