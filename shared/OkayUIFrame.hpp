#pragma once
// OkayUI's end-of-frame on an app's renderer (okay::pb, PainterBridge.hpp): the toolkit's finished frame drawn through the
// Painter interface by OkayUI::soft::FrameDrawer, one drawer per painter (it keeps the glyph atlas uploaded between frames).
#include "PainterBridge.hpp"
#include "okay/UI/OkayUI.hpp"
#include "OkayUI_SoftPainter.hpp"

#include <memory>
#include <unordered_map>

namespace okay::pb {

namespace detail {
inline std::unordered_map<okay::platform::Painter*, std::unique_ptr<OkayUI::soft::FrameDrawer>>& FrameDrawers() {
    static auto* d = new std::unordered_map<okay::platform::Painter*, std::unique_ptr<OkayUI::soft::FrameDrawer>>();   // (leaked on purpose)
    return *d;
}
}

/// Called by okay::pb::Forget (DestroyRenderer) while the painter is still alive: frees the drawer's textures for it.
inline void OkayUIForgetRenderer(Renderer* r) { detail::FrameDrawers().erase(r); }

inline void OkayUIEndFrame(Renderer* r) {
    okay::platform::Painter* p = For(r);
    if (!p) return;
    if (!OnForget()) OnForget() = &OkayUIForgetRenderer;
    int w = 0, h = 0;
    p->OutputSize(&w, &h);
    OkayUI::EndFrameData(w, h);
    auto& d = detail::FrameDrawers()[p];
    if (!d) d.reset(new OkayUI::soft::FrameDrawer(*p));
    d->Draw(OkayUI::GetDrawData());
}

} // namespace okay::pb
