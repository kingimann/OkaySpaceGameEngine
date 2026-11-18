#pragma once
// The editor and launcher still own an SDL_Renderer* and keep SDL_Texture* handles. This header is how their 2D renderer calls reach
// okay::platform::Painter without touching those handles: one wrapper per renderer, kept in a small table, and one function per
// thing the apps do. With the sdl2 painter (the only one these apps use today) each function is the SDL call it replaces; the point
// is that the apps no longer say "SDL" for it, so a different painter can stand behind the same calls later.
//
// A texture made here is an SDL_Texture* only in disguise (the opaque ui2d::Tex of the sdl2 painter); give it back to these functions,
// to okay::platform::Painter, or to ImGui's SDL renderer backend (which draws it with SDL_RenderGeometry).
#include "okay/Platform/Painter.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

struct SDL_Renderer;
struct SDL_Texture;

namespace okay::pb {

namespace detail {
inline std::unordered_map<void*, std::unique_ptr<okay::platform::Painter>>& Table() {
    // Never destroyed on purpose: a renderer that is still open at exit (the launcher's chat window keeps its own until the process
    // ends) must not be freed by a static destructor after SDL has shut down.
    static auto* t = new std::unordered_map<void*, std::unique_ptr<okay::platform::Painter>>();
    return *t;
}
}

/// A function that makes a painter for a window of the own backend (the software painter lives in okayui; the app that links it registers
/// it). Without one, CreateRenderer for such a window returns null.
using OwnPainterFactory = std::unique_ptr<okay::platform::Painter> (*)(okay::platform::Window&);
inline OwnPainterFactory& OwnFactory() { static OwnPainterFactory f = nullptr; return f; }
inline void SetOwnPainterFactory(OwnPainterFactory f) { OwnFactory() = f; }

/// The painter that stands for `ren` (made on first use; null for a null renderer).
inline okay::platform::Painter* For(SDL_Renderer* ren) {
    if (!ren) return nullptr;
    auto& t = detail::Table();
    auto it = t.find(ren);
    if (it == t.end()) it = t.emplace(ren, okay::platform::WrapSdl2Renderer(ren)).first;
    return it->second.get();
}
/// Forget the wrapper of a renderer that is about to be destroyed (a new renderer may get the same address).
inline void Forget(SDL_Renderer* ren) { detail::Table().erase(ren); }

enum class Access { Static, Streaming, Target };

/// A new RGBA8 texture. `rgba` (w * h * 4, top row first) may be null. Like SDL_CreateTexture + SDL_SetTextureBlendMode + SDL_SetTextureScaleMode.
inline SDL_Texture* CreateTexture(SDL_Renderer* ren, int w, int h, Access acc = Access::Static, bool blend = true, bool linear = false,
                                  const std::uint8_t* rgba = nullptr) {
    okay::platform::Painter* p = For(ren);
    if (!p) return nullptr;
    okay::platform::TextureDesc d;
    d.width = w; d.height = h;
    d.blend = blend ? okay::platform::BlendMode::Blend : okay::platform::BlendMode::None;
    d.scale = linear ? okay::platform::ScaleMode::Linear : okay::platform::ScaleMode::Nearest;
    d.streaming = acc == Access::Streaming; d.target = acc == Access::Target;
    return (SDL_Texture*)p->CreateTexture(d, rgba);
}
inline void DestroyTexture(SDL_Renderer* ren, SDL_Texture* t) { if (t) if (auto* p = For(ren)) p->DestroyTexture((okay::ui2d::Tex)t); }
/// Replace the pixels of `t` (rect null = all) with RGBA8 laid out with `pitch` bytes per row. `rect` is {x, y, w, h}.
inline bool UpdateTexture(SDL_Renderer* ren, SDL_Texture* t, const int* rect, const void* px, int pitch) {
    auto* p = For(ren);
    if (!p || !t) return false;
    okay::ui2d::Rect r; if (rect) { r.x = rect[0]; r.y = rect[1]; r.w = rect[2]; r.h = rect[3]; }
    return p->UpdateTexture((okay::ui2d::Tex)t, rect ? &r : nullptr, px, pitch);
}
inline void QueryTexture(SDL_Renderer* ren, SDL_Texture* t, int* w, int* h) {
    if (w) *w = 0;
    if (h) *h = 0;
    if (t) if (auto* p = For(ren)) p->QueryTexture((okay::ui2d::Tex)t, w, h);
}
inline void SetTextureBlend(SDL_Renderer* ren, SDL_Texture* t, bool blend) {
    if (t) if (auto* p = For(ren)) p->SetTextureBlendMode((okay::ui2d::Tex)t, blend ? okay::platform::BlendMode::Blend : okay::platform::BlendMode::None);
}
inline void SetTextureLinear(SDL_Renderer* ren, SDL_Texture* t, bool linear) {
    if (t) if (auto* p = For(ren)) p->SetTextureScaleMode((okay::ui2d::Tex)t, linear ? okay::platform::ScaleMode::Linear : okay::platform::ScaleMode::Nearest);
}

// ---- the frame ----
inline void Clear(SDL_Renderer* ren) { if (auto* p = For(ren)) p->Clear(); }
inline void Present(SDL_Renderer* ren) { if (auto* p = For(ren)) p->Present(); }
inline void OutputSize(SDL_Renderer* ren, int* w, int* h) { if (auto* p = For(ren)) p->OutputSize(w, h); }
inline void SetVSync(SDL_Renderer* ren, bool on) { if (auto* p = For(ren)) p->SetVSync(on); }
inline void SetScale(SDL_Renderer* ren, float sx, float sy) { if (auto* p = For(ren)) p->SetScale(sx, sy); }
inline void SetDrawColor(SDL_Renderer* ren, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) { if (auto* p = For(ren)) p->SetDrawColor(r, g, b, a); }
inline void GetDrawColor(SDL_Renderer* ren, std::uint8_t* r, std::uint8_t* g, std::uint8_t* b, std::uint8_t* a) {
    if (auto* p = For(ren)) p->GetDrawColor(r, g, b, a);
}
inline void DrawPoint(SDL_Renderer* ren, int x, int y) { if (auto* p = For(ren)) p->DrawPoint(x, y); }
inline void Flush(SDL_Renderer* ren) { if (auto* p = For(ren)) p->Flush(); }
inline bool ReadPixels(SDL_Renderer* ren, std::vector<std::uint8_t>& rgba, int& w, int& h) { auto* p = For(ren); return p && p->ReadPixels(rgba, w, h); }
inline std::string DriverName(SDL_Renderer* ren) { auto* p = For(ren); return p ? p->DriverName() : std::string(); }
inline bool IsAccelerated(SDL_Renderer* ren) { auto* p = For(ren); return p && p->IsAccelerated(); }
inline SDL_Texture* GetRenderTarget(SDL_Renderer* ren) { auto* p = For(ren); return p ? (SDL_Texture*)p->GetRenderTarget() : nullptr; }
inline bool SetRenderTarget(SDL_Renderer* ren, SDL_Texture* t) { auto* p = For(ren); return p && p->SetRenderTarget((okay::ui2d::Tex)t); }

/// A renderer for an SDL window made by the sdl2 backend: accelerated, else any (what the apps asked SDL for). Null on failure.
/// For an own-backend window the returned pointer is NOT an SDL_Renderer (it is only the key of the software painter): use it with these functions
/// and the Painter, never with an SDL_* call.
/// The table owns the painter and with it the renderer: end it with DestroyRenderer (before the window goes).
inline SDL_Renderer* CreateRenderer(okay::platform::Window& w, bool vsync = false, std::string* err = nullptr, bool requireAccelerated = false) {
    okay::platform::PainterDesc d; d.vsync = vsync; d.requireAccelerated = requireAccelerated;
    std::unique_ptr<okay::platform::Painter> p;
    if (w.Kind() == okay::platform::BackendKind::Own) {   // an own-backend window: no SDL_Renderer exists; the handle is the painter's own address
        if (OwnFactory()) p = OwnFactory()(w);
        else if (err) *err = "no painter factory for own-backend windows is registered";
        if (!p) return nullptr;
        SDL_Renderer* h = reinterpret_cast<SDL_Renderer*>(p.get());
        detail::Table()[h] = std::move(p);
        return h;
    }
    p = okay::platform::CreateSdl2Painter(w, d, err);
    if (!p) return nullptr;
    SDL_Renderer* r = (SDL_Renderer*)p->NativeRenderer();
    detail::Table()[r] = std::move(p);
    return r;
}
/// Destroys a renderer made by CreateRenderer (its painter owns it); for one the app made another way, only forgets the wrapper.
inline void DestroyRenderer(SDL_Renderer* ren) { Forget(ren); }

} // namespace okay::pb
