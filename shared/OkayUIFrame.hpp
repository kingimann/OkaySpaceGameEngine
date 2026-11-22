#pragma once
// OkayUI's end-of-frame on whatever renderer an app has: an SDL renderer (the toolkit's own SDL path) or the software painter of an
// own-backend window (the toolkit's finished frame drawn through the Painter interface). `r` is the handle okay::pb knows.
#include "PainterBridge.hpp"
#include "okay/UI/OkayUI.hpp"
#include "OkayUI_SoftPainter.hpp"
#include "okay/UI/OkayUIRenderSDL.hpp"

#include <memory>
#include <unordered_map>

namespace okay::pb {

/// OkayUI's own renderer state for the app's SDL renderers (OKAY_UI_OWN_RENDER, OkayUIRenderSDL.hpp).
inline OkayUI::render::SdlHost& OkayUIHostState() { static auto* h = new OkayUI::render::SdlHost(); return *h; }
/// Which renderer drew the last OkayUI frame and why (tests, the About line).
inline const OkayUI::render::HostFrame& OkayUILastFrame() { return OkayUIHostState().Last(); }
/// Called by okay::pb::Forget (DestroyRenderer) while the renderer is still alive: frees the own renderer's GL objects for it.
inline void OkayUIForgetRenderer(SDL_Renderer* r) { OkayUIHostState().Forget(r); }

inline void OkayUIEndFrame(SDL_Renderer* r) {
    okay::platform::Painter* p = For(r);
    if (!p) return;
    if (p->Kind() == okay::platform::PainterKind::Sdl2) {
        // OKAY_UI_OWN_RENDER=1 (off by default): OkayUI's own renderer draws the frame when SDL's renderer is OpenGL or Direct3D 11;
        // otherwise, and with the switch off, OkayUI::EndFrame(r) exactly as before.
        OkayUI::render::SdlHost& host = OkayUIHostState();
        if (OkayUI::render::OwnRenderSwitchOn()) {
            if (!OnForget()) OnForget() = &OkayUIForgetRenderer;
            host.EndFrame(r);
        } else {
            host.EndFrameSdl(r);
        }
        return;
    }
    int w = 0, h = 0;
    p->OutputSize(&w, &h);
    OkayUI::EndFrameData(w, h);
    static auto* drawers = new std::unordered_map<okay::platform::Painter*, std::unique_ptr<OkayUI::soft::FrameDrawer>>();
    auto& d = (*drawers)[p];
    if (!d) d.reset(new OkayUI::soft::FrameDrawer(*p));
    d->Draw(OkayUI::GetDrawData());
}

} // namespace okay::pb
