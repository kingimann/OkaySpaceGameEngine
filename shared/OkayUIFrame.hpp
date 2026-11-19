#pragma once
// OkayUI's end-of-frame on whatever renderer an app has: an SDL renderer (the toolkit's own SDL path) or the software painter of an
// own-backend window (the toolkit's finished frame drawn through the Painter interface). `r` is the handle okay::pb knows.
#include "PainterBridge.hpp"
#include "okay/UI/OkayUI.hpp"
#include "OkayUI_SoftPainter.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <unordered_map>

namespace okay::pb {

/// The painter for a window of the own backend: Direct3D 11 on Windows when a device can be made (OKAY_PAINTER=software skips it), else the software painter,
/// which presents through the window (GDI StretchDIBits on Windows, XPutImage on X11), so the program always shows something. The reason for a fallback
/// goes to the log.
inline std::unique_ptr<okay::platform::Painter> OwnWindowPainter(okay::platform::Window& w) {
    const char* want = std::getenv("OKAY_PAINTER");
    const bool soft = want && std::string(want) == "software";
    if (!soft) {
        std::string why;
        if (auto p = okay::platform::CreateD3D11Painter(w, okay::platform::PainterDesc(), &why)) { std::fprintf(stderr, "painter: Direct3D 11\n"); return p; }
        if (why.find("only on Windows") == std::string::npos) std::fprintf(stderr, "painter: Direct3D 11 unavailable (%s); using the software painter\n", why.c_str());
    }
    std::fprintf(stderr, "painter: software\n");
    return OkayUI::soft::CreateWindowPainter(w);
}

inline void OkayUIEndFrame(SDL_Renderer* r) {
    okay::platform::Painter* p = For(r);
    if (!p) return;
    if (p->Kind() == okay::platform::PainterKind::Sdl2) { OkayUI::EndFrame(r); return; }
    int w = 0, h = 0;
    p->OutputSize(&w, &h);
    OkayUI::EndFrameData(w, h);
    static auto* drawers = new std::unordered_map<okay::platform::Painter*, std::unique_ptr<OkayUI::soft::FrameDrawer>>();
    auto& d = (*drawers)[p];
    if (!d) d.reset(new OkayUI::soft::FrameDrawer(*p));
    d->Draw(OkayUI::GetDrawData());
}

} // namespace okay::pb
