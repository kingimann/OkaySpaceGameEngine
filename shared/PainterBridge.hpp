#pragma once
// The apps' 2D renderer: an okay::platform::Painter per window (the OpenGL painter, or the software painter when a window has no
// GL; shared/OwnPainter.hpp picks), made by CreateRenderer and owned by the small table here until DestroyRenderer. The free
// functions below are the calls the apps make on it, so their code reads the same whichever painter stands behind the window.
// Textures are okay::ui2d::Tex handles made by the painter; give them back only to these functions or to the same painter.
// (Until 2026-11-27 the handle was an SDL_Renderer* and these functions were the SDL calls they replaced; SDL2 is gone.)
#include "okay/Platform/Painter.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace okay::pb {

/// The renderer handle the apps keep: the painter itself.
using Renderer = okay::platform::Painter;
using Tex = okay::ui2d::Tex;

namespace detail {
inline std::unordered_map<okay::platform::Painter*, std::unique_ptr<okay::platform::Painter>>& Table() {
    // Never destroyed on purpose: a renderer that is still open at exit (the launcher's chat window keeps its own until the process
    // ends) must not be freed by a static destructor after the platform backend has shut down.
    static auto* t = new std::unordered_map<okay::platform::Painter*, std::unique_ptr<okay::platform::Painter>>();
    return *t;
}
}

/// A function that makes a painter for a window (the software painter lives in okayui, the OpenGL one in the engine; the app that
/// links them registers one, usually okay::pb::MakeOwnPainter from OwnPainter.hpp). It gets CreateRenderer's vsync and accelerated
/// wishes. Without one, CreateRenderer returns null.
using OwnPainterFactory = std::unique_ptr<okay::platform::Painter> (*)(okay::platform::Window&, const okay::platform::PainterDesc&);
inline OwnPainterFactory& OwnFactory() { static OwnPainterFactory f = nullptr; return f; }
inline void SetOwnPainterFactory(OwnPainterFactory f) { OwnFactory() = f; }

/// The painter that stands for `ren` (it is the painter; kept so call sites read the same as before).
inline okay::platform::Painter* For(Renderer* ren) { return ren; }
/// One function told about a renderer that is about to be destroyed, while it still works (OkayUI's frame drawer frees its
/// textures there: shared/OkayUIFrame.hpp). Null = none.
using ForgetHook = void (*)(Renderer*);
inline ForgetHook& OnForget() { static ForgetHook h = nullptr; return h; }
/// Tell the hook that `ren` is about to go and drop it from the table (destroying it when the table owns it).
inline void Forget(Renderer* ren) { if (ren && OnForget()) OnForget()(ren); detail::Table().erase(ren); }

enum class Access { Static, Streaming, Target };

/// A new RGBA8 texture. `rgba` (w * h * 4, top row first) may be null.
inline Tex CreateTexture(Renderer* ren, int w, int h, Access acc = Access::Static, bool blend = true, bool linear = false,
                         const std::uint8_t* rgba = nullptr) {
    if (!ren) return nullptr;
    okay::platform::TextureDesc d;
    d.width = w; d.height = h;
    d.blend = blend ? okay::platform::BlendMode::Blend : okay::platform::BlendMode::None;
    d.scale = linear ? okay::platform::ScaleMode::Linear : okay::platform::ScaleMode::Nearest;
    d.streaming = acc == Access::Streaming; d.target = acc == Access::Target;
    return ren->CreateTexture(d, rgba);
}
inline void DestroyTexture(Renderer* ren, Tex t) { if (t && ren) ren->DestroyTexture(t); }
/// Replace the pixels of `t` (rect null = all) with RGBA8 laid out with `pitch` bytes per row. `rect` is {x, y, w, h}.
inline bool UpdateTexture(Renderer* ren, Tex t, const int* rect, const void* px, int pitch) {
    if (!ren || !t) return false;
    okay::ui2d::Rect r; if (rect) { r.x = rect[0]; r.y = rect[1]; r.w = rect[2]; r.h = rect[3]; }
    return ren->UpdateTexture(t, rect ? &r : nullptr, px, pitch);
}
inline void QueryTexture(Renderer* ren, Tex t, int* w, int* h) {
    if (w) *w = 0;
    if (h) *h = 0;
    if (t && ren) ren->QueryTexture(t, w, h);
}
inline void SetTextureBlend(Renderer* ren, Tex t, bool blend) {
    if (t && ren) ren->SetTextureBlendMode(t, blend ? okay::platform::BlendMode::Blend : okay::platform::BlendMode::None);
}
inline void SetTextureLinear(Renderer* ren, Tex t, bool linear) {
    if (t && ren) ren->SetTextureScaleMode(t, linear ? okay::platform::ScaleMode::Linear : okay::platform::ScaleMode::Nearest);
}

// ---- the frame ----
inline void Clear(Renderer* ren) { if (ren) ren->Clear(); }
inline void Present(Renderer* ren) { if (ren) ren->Present(); }
inline void OutputSize(Renderer* ren, int* w, int* h) { if (ren) ren->OutputSize(w, h); }
inline void SetVSync(Renderer* ren, bool on) { if (ren) ren->SetVSync(on); }
inline void SetScale(Renderer* ren, float sx, float sy) { if (ren) ren->SetScale(sx, sy); }
inline void SetDrawColor(Renderer* ren, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) { if (ren) ren->SetDrawColor(r, g, b, a); }
inline void GetDrawColor(Renderer* ren, std::uint8_t* r, std::uint8_t* g, std::uint8_t* b, std::uint8_t* a) {
    if (ren) ren->GetDrawColor(r, g, b, a);
}
inline void DrawPoint(Renderer* ren, int x, int y) { if (ren) ren->DrawPoint(x, y); }
inline void Flush(Renderer* ren) { if (ren) ren->Flush(); }
inline bool ReadPixels(Renderer* ren, std::vector<std::uint8_t>& rgba, int& w, int& h) { return ren && ren->ReadPixels(rgba, w, h); }
inline std::string DriverName(Renderer* ren) { return ren ? ren->DriverName() : std::string(); }
inline bool IsAccelerated(Renderer* ren) { return ren && ren->IsAccelerated(); }
inline Tex GetRenderTarget(Renderer* ren) { return ren ? ren->GetRenderTarget() : nullptr; }
inline bool SetRenderTarget(Renderer* ren, Tex t) { return ren && ren->SetRenderTarget(t); }

/// The painter for window `w`, made by the registered factory (OwnPainter.hpp: OpenGL, else software). Null on failure (`err` says
/// why). The table owns it: end it with DestroyRenderer (before the window goes).
inline Renderer* CreateRenderer(okay::platform::Window& w, bool vsync = false, std::string* err = nullptr, bool requireAccelerated = false) {
    okay::platform::PainterDesc d; d.vsync = vsync; d.requireAccelerated = requireAccelerated;
    std::unique_ptr<okay::platform::Painter> p;
    if (OwnFactory()) p = OwnFactory()(w, d);
    else if (err) *err = "no painter factory is registered (okay::pb::SetOwnPainterFactory)";
    if (!p) return nullptr;
    Renderer* h = p.get();
    detail::Table()[h] = std::move(p);
    return h;
}
/// Destroys a renderer made by CreateRenderer.
inline void DestroyRenderer(Renderer* ren) { Forget(ren); }

} // namespace okay::pb
