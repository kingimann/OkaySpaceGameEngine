#pragma once
// ui2d::Painter over an SDL_Renderer: a straight pass-through to the SDL calls the UI
// pass was written against. Header-only so the engine library stays SDL-free; include
// it only from code that links SDL2 (the player, SDL-based tests).
#include "okay/UI/UIPainter.hpp"
#include "okay/Graphics/Image.hpp"
#include "okay/Graphics/TtfFont.hpp"
#include <SDL.h>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <string>
#include <unordered_map>

namespace okay {
namespace ui2d {

static_assert(sizeof(Rect) == sizeof(SDL_Rect) && sizeof(FRect) == sizeof(SDL_FRect) &&
              sizeof(Point) == sizeof(SDL_Point) && sizeof(FPoint) == sizeof(SDL_FPoint) &&
              sizeof(RGBA) == sizeof(SDL_Color) && sizeof(Vertex) == sizeof(SDL_Vertex),
              "ui2d types must match SDL's layout");
static_assert((int)FlipH == (int)SDL_FLIP_HORIZONTAL && (int)FlipV == (int)SDL_FLIP_VERTICAL, "flip flags");

/// Load (and cache) a texture from an image file. A miss is remembered, but only for
/// 500 ms, and a file whose modification time changed is decoded again. `lruCap` > 0
/// drops the oldest entries of THIS cache (the player icon cache); 0 keeps every entry.
inline SDL_Texture* LoadSDLTexture(SDL_Renderer* r, const std::string& path, const std::string& baseDir,
                                   std::unordered_map<std::string, SDL_Texture*>& cache,
                                   int lruCap = 0) {
    if (path.empty() || !r) return nullptr;
    struct Rec { std::uint64_t mtime = 0; Uint32 checked = 0; bool known = false; };
    static std::unordered_map<const void*, std::unordered_map<std::string, Rec>> meta;
    static std::unordered_map<const void*, std::deque<std::string>> order;
    const void* key = &cache;
    Rec& rec = meta[key][path];
    auto it = cache.find(path);
    const Uint32 now = SDL_GetTicks();
    if (it != cache.end() && rec.known && (Uint32)(now - rec.checked) < 500u) {
        if (lruCap > 0) {
            auto& ord = order[key];
            for (auto i = ord.begin(); i != ord.end(); ++i) if (*i == path) { ord.erase(i); break; }
            ord.push_back(path);
        }
        return it->second;
    }
    const bool proc = path.rfind("proc:", 0) == 0;
    std::uint64_t mt = 0;
    if (!proc) {
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::path which = path;
        if (!fs::exists(which, ec)) which = fs::path(baseDir) / path;
        if (fs::exists(which, ec)) {
            const auto t = fs::last_write_time(which, ec);
            if (!ec) mt = (std::uint64_t)t.time_since_epoch().count();
        }
    }
    if (it != cache.end() && rec.known && it->second && (proc || mt == rec.mtime)) {
        rec.checked = now;
        return it->second;
    }
    if (it != cache.end() && it->second) SDL_DestroyTexture(it->second);
    Image img;
    SDL_Texture* tex = nullptr;
    if (img.Load(path) || img.Load(baseDir + path)) {
        tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888,
                                SDL_TEXTUREACCESS_STATIC, img.Width(), img.Height());
        if (tex) {
            SDL_UpdateTexture(tex, nullptr, img.Data(), img.Width() * 4);
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        }
    }
    cache[path] = tex;
    rec.mtime = mt;
    rec.checked = now;
    rec.known = true;
    if (lruCap > 0) {
        auto& ord = order[key];
        for (auto i = ord.begin(); i != ord.end(); ++i) if (*i == path) { ord.erase(i); break; }
        ord.push_back(path);
        int guard = (int)ord.size() + 2;
        while ((int)cache.size() > lruCap && !ord.empty() && guard-- > 0) {
            std::string old = std::move(ord.front());
            ord.pop_front();
            if (old == path) { ord.push_back(std::move(old)); continue; }
            auto gone = cache.find(old);
            if (gone == cache.end()) continue;
            if (gone->second) SDL_DestroyTexture(gone->second);
            cache.erase(gone);
            meta[key].erase(old);
        }
    }
    return tex;
}

/// Upload a TTF glyph atlas as an SDL texture (once per font, cached for the
/// process; one renderer per process, as in the player).
inline SDL_Texture* SDLFontAtlas(SDL_Renderer* r, TtfFont* f) {
    if (!f || !f->Valid()) return nullptr;
    static std::unordered_map<TtfFont*, SDL_Texture*> cache;
    auto it = cache.find(f);
    if (it != cache.end()) return it->second;
    const Image& a = f->Atlas();
    SDL_Texture* tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888,
                                         SDL_TEXTUREACCESS_STATIC, a.Width(), a.Height());
    if (tex) {
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);
        SDL_UpdateTexture(tex, nullptr, a.Data(), a.Width() * 4);
    }
    cache[f] = tex;
    return tex;
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
    void DrawRect(const Rect* r) override { SDL_RenderDrawRect(m_r, (const SDL_Rect*)r); }
    void DrawLine(int x0, int y0, int x1, int y1) override { SDL_RenderDrawLine(m_r, x0, y0, x1, y1); }
    void DrawPoint(int x, int y) override { SDL_RenderDrawPoint(m_r, x, y); }

    void SetClipRect(const Rect* r) override { SDL_RenderSetClipRect(m_r, (const SDL_Rect*)r); }
    void GetClipRect(Rect* r) override { SDL_RenderGetClipRect(m_r, (SDL_Rect*)r); }
    bool IsClipEnabled() override { return SDL_RenderIsClipEnabled(m_r) == SDL_TRUE; }
    void SetScale(float sx, float sy) override { SDL_RenderSetScale(m_r, sx, sy); }

    Tex LoadTexture(const std::string& path) override {
        return m_cache ? LoadSDLTexture(m_r, path, m_baseDir, *m_cache) : nullptr;
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
};

inline const Rect& FromSDL(const SDL_Rect& r) { return *(const Rect*)&r; }
inline RGBA FromSDL(SDL_Color c) { return RGBA{c.r, c.g, c.b, c.a}; }
inline Point FromSDL(SDL_Point p) { return Point{p.x, p.y}; }

} // namespace ui2d
} // namespace okay
