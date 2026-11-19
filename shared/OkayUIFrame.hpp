#pragma once
// OkayUI's end-of-frame on whatever renderer an app has: an SDL renderer (the toolkit's own SDL path) or the software painter of an
// own-backend window (the toolkit's finished frame drawn through the Painter interface). `r` is the handle okay::pb knows.
#include "PainterBridge.hpp"
#include "okay/UI/OkayUI.hpp"
#include "OkayUI_SoftPainter.hpp"

#include <memory>
#include <unordered_map>

namespace okay::pb {

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
