// okay::platform::Painter over SDL_Renderer. Every method is the SDL call it stands for; nothing is
// reinterpreted, so an app moved onto the interface draws what it drew on the renderer directly.
#include "okay/Platform/Painter.hpp"

#ifdef OKAY_PLATFORM_HAVE_SDL2
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include <SDL.h>

namespace okay::platform {
namespace {

static_assert(sizeof(ui2d::Rect) == sizeof(SDL_Rect) && sizeof(ui2d::FRect) == sizeof(SDL_FRect) &&
              sizeof(ui2d::Point) == sizeof(SDL_Point) && sizeof(ui2d::FPoint) == sizeof(SDL_FPoint) &&
              sizeof(ui2d::RGBA) == sizeof(SDL_Color) && sizeof(ui2d::Vertex) == sizeof(SDL_Vertex),
              "ui2d types must match SDL's layout");

SDL_BlendMode ToSdl(BlendMode m) { return m == BlendMode::None ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND; }
SDL_ScaleMode ToSdl(ScaleMode m) { return m == ScaleMode::Linear ? SDL_ScaleModeLinear : SDL_ScaleModeNearest; }

class SdlPainter final : public Painter {
public:
    SdlPainter(SDL_Renderer* r, bool owns, SDL_Surface* surface = nullptr)
        : m_r(r), m_owns(owns), m_surface(surface) {}
    ~SdlPainter() override {
        ReleaseCachedTextures();
        if (m_owns && m_r) SDL_DestroyRenderer(m_r);
        if (m_surface) SDL_FreeSurface(m_surface);
    }

    PainterKind Kind() const override { return PainterKind::Sdl2; }
    void* NativeRenderer() override { return m_r; }
    void OutputSize(int* w, int* h) override { SDL_GetRendererOutputSize(m_r, w, h); }
    void Clear() override { SDL_RenderClear(m_r); }
    void SetBlendMode(BlendMode m) override { SDL_SetRenderDrawBlendMode(m_r, ToSdl(m)); }
    BlendMode GetBlendMode() override {
        SDL_BlendMode m = SDL_BLENDMODE_NONE;
        SDL_GetRenderDrawBlendMode(m_r, &m);
        return m == SDL_BLENDMODE_NONE ? BlendMode::None : BlendMode::Blend;
    }

    void SetDrawColor(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) override { SDL_SetRenderDrawColor(m_r, r, g, b, a); }
    void FillRect(const ui2d::Rect* r) override { SDL_RenderFillRect(m_r, (const SDL_Rect*)r); }
    void FillRectF(const ui2d::FRect* r) override { SDL_RenderFillRectF(m_r, (const SDL_FRect*)r); }
    void DrawRect(const ui2d::Rect* r) override { SDL_RenderDrawRect(m_r, (const SDL_Rect*)r); }
    void DrawLine(int x0, int y0, int x1, int y1) override { SDL_RenderDrawLine(m_r, x0, y0, x1, y1); }
    void DrawLineF(float x0, float y0, float x1, float y1) override { SDL_RenderDrawLineF(m_r, x0, y0, x1, y1); }
    void DrawPoint(int x, int y) override { SDL_RenderDrawPoint(m_r, x, y); }
    void SetClipRect(const ui2d::Rect* r) override { SDL_RenderSetClipRect(m_r, (const SDL_Rect*)r); }
    void GetClipRect(ui2d::Rect* r) override { SDL_RenderGetClipRect(m_r, (SDL_Rect*)r); }
    bool IsClipEnabled() override { return SDL_RenderIsClipEnabled(m_r) == SDL_TRUE; }
    void SetScale(float sx, float sy) override { SDL_RenderSetScale(m_r, sx, sy); }

    ui2d::Tex CreateTexture(const TextureDesc& d, const std::uint8_t* rgba) override {
        if (d.width <= 0 || d.height <= 0) return nullptr;
        SDL_Texture* t = SDL_CreateTexture(m_r, SDL_PIXELFORMAT_ABGR8888,
                                           d.streaming ? SDL_TEXTUREACCESS_STREAMING : SDL_TEXTUREACCESS_STATIC, d.width, d.height);
        if (!t) return nullptr;
        if (rgba) SDL_UpdateTexture(t, nullptr, rgba, d.width * 4);
        SDL_SetTextureBlendMode(t, ToSdl(d.blend));
        if (d.scale == ScaleMode::Linear) SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
        return t;
    }
    bool UpdateTexture(ui2d::Tex t, const ui2d::Rect* r, const void* rgba, int pitch) override {
        return t && SDL_UpdateTexture((SDL_Texture*)t, (const SDL_Rect*)r, rgba, pitch) == 0;
    }
    void DestroyTexture(ui2d::Tex t) override { if (t) SDL_DestroyTexture((SDL_Texture*)t); }
    void SetTextureBlendMode(ui2d::Tex t, BlendMode m) override { SDL_SetTextureBlendMode((SDL_Texture*)t, ToSdl(m)); }
    void SetTextureScaleMode(ui2d::Tex t, ScaleMode m) override { SDL_SetTextureScaleMode((SDL_Texture*)t, ToSdl(m)); }
    void QueryTexture(ui2d::Tex t, int* w, int* h) override { SDL_QueryTexture((SDL_Texture*)t, nullptr, nullptr, w, h); }
    void SetTextureColorMod(ui2d::Tex t, std::uint8_t r, std::uint8_t g, std::uint8_t b) override { SDL_SetTextureColorMod((SDL_Texture*)t, r, g, b); }
    void SetTextureAlphaMod(ui2d::Tex t, std::uint8_t a) override { SDL_SetTextureAlphaMod((SDL_Texture*)t, a); }
    void Copy(ui2d::Tex t, const ui2d::Rect* s, const ui2d::Rect* d) override {
        SDL_RenderCopy(m_r, (SDL_Texture*)t, (const SDL_Rect*)s, (const SDL_Rect*)d);
    }
    void CopyF(ui2d::Tex t, const ui2d::Rect* s, const ui2d::FRect* d) override {
        SDL_RenderCopyF(m_r, (SDL_Texture*)t, (const SDL_Rect*)s, (const SDL_FRect*)d);
    }
    void CopyEx(ui2d::Tex t, const ui2d::Rect* s, const ui2d::Rect* d, double a, const ui2d::Point* c, int flip) override {
        SDL_RenderCopyEx(m_r, (SDL_Texture*)t, (const SDL_Rect*)s, (const SDL_Rect*)d, a, (const SDL_Point*)c, (SDL_RendererFlip)flip);
    }
    void CopyExF(ui2d::Tex t, const ui2d::Rect* s, const ui2d::FRect* d, double a, const ui2d::FPoint* c, int flip) override {
        SDL_RenderCopyExF(m_r, (SDL_Texture*)t, (const SDL_Rect*)s, (const SDL_FRect*)d, a, (const SDL_FPoint*)c, (SDL_RendererFlip)flip);
    }
    void Geometry(ui2d::Tex t, const ui2d::Vertex* v, int nv, const int* idx, int ni) override {
        SDL_RenderGeometry(m_r, (SDL_Texture*)t, (const SDL_Vertex*)v, nv, idx, ni);
    }

    void Present() override { SDL_RenderPresent(m_r); }
    bool SetVSync(bool on) override { return SDL_RenderSetVSync(m_r, on ? 1 : 0) == 0 && on; }
    bool ReadPixels(std::vector<std::uint8_t>& rgba, int& w, int& h) override {
        OutputSize(&w, &h);
        if (w <= 0 || h <= 0) return false;
        rgba.assign((size_t)w * (size_t)h * 4, 0);
        return SDL_RenderReadPixels(m_r, nullptr, SDL_PIXELFORMAT_ABGR8888, rgba.data(), w * 4) == 0;
    }

private:
    SDL_Renderer* m_r;
    bool m_owns;
    SDL_Surface* m_surface;
};

} // namespace

std::unique_ptr<Painter> CreateSdl2Painter(Window& w, const PainterDesc& d, std::string* err) {
    // An own window's handle is an X11 id or an HWND, which SDL must never see.
    if (w.Kind() != BackendKind::Sdl2) {
        if (err) *err = "the sdl2 painter needs a window made by the sdl2 platform backend";
        return nullptr;
    }
    SDL_Window* sw = (SDL_Window*)w.NativeHandle();
    Uint32 flags = SDL_RENDERER_ACCELERATED | (d.vsync ? SDL_RENDERER_PRESENTVSYNC : 0);
    SDL_Renderer* r = SDL_CreateRenderer(sw, -1, flags);
    if (!r) r = SDL_CreateRenderer(sw, -1, 0);
    if (!r) { if (err) *err = SDL_GetError(); return nullptr; }
    return std::unique_ptr<Painter>(new SdlPainter(r, true));
}

std::unique_ptr<Painter> WrapSdl2Renderer(void* sdlRenderer) {
    if (!sdlRenderer) return nullptr;
    return std::unique_ptr<Painter>(new SdlPainter((SDL_Renderer*)sdlRenderer, false));
}

std::unique_ptr<Painter> CreateSdl2SurfacePainter(int w, int h) {
    if (w <= 0 || h <= 0) return nullptr;
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ABGR8888);
    if (!s) return nullptr;
    SDL_Renderer* r = SDL_CreateSoftwareRenderer(s);
    if (!r) { SDL_FreeSurface(s); return nullptr; }
    return std::unique_ptr<Painter>(new SdlPainter(r, true, s));
}

} // namespace okay::platform

#else // no SDL2: the sdl2 painter does not exist

namespace okay::platform {
std::unique_ptr<Painter> CreateSdl2Painter(Window&, const PainterDesc&, std::string* err) {
    if (err) *err = "this build has no SDL2";
    return nullptr;
}
std::unique_ptr<Painter> WrapSdl2Renderer(void*) { return nullptr; }
std::unique_ptr<Painter> CreateSdl2SurfacePainter(int, int) { return nullptr; }
} // namespace okay::platform

#endif
