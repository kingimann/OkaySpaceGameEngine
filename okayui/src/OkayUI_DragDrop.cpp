#include "OkayUI_ContextState.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <SDL.h>
#include "OkayUIInternal.hpp"
#include "okay/UI/OkayUI.hpp"
#include "okay/UI/OkayUICompat.hpp"
#include "okay/UI/OkayUIInternals.hpp"

namespace OkayUI {
using namespace ui_core;


const DragPayload* GetDragDropPayload() {
    if (!g_tdd.active || !g_tdd.hasPayload) return nullptr;
    static DragPayload copy;
    copy = g_tdd.pay; copy.delivery = false; copy.Data = copy.data; copy.DataSize = copy.size;
    return &copy;
}

bool IsDragging() { return g_dd.active || g_tdd.active; }

bool DragSource(int payload) {
    if (!g_lay.active) return false;
    const float px = g_lay.prevX, py = g_lay.prevY, pw = g_lay.prevW, ph = g_lay.prevH;
    const bool overPrev = hit(px, py, pw, ph);
    // Arm on a press inside the source widget.
    if (!g_dd.active && !g_dd.armed && g_pressed && overPrev) {
        g_dd.armed = true; g_dd.armPayload = payload;
        g_dd.armX = g_in.mouseX; g_dd.armY = g_in.mouseY;
    }
    // Promote to an active drag once the cursor has moved far enough.
    if (g_dd.armed && g_dd.armPayload == payload && g_in.mouseDown) {
        float dx = g_in.mouseX - g_dd.armX, dy = g_in.mouseY - g_dd.armY;
        if ((dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy) > 6.0f) {
            g_dd.active = true; g_dd.payload = payload; g_dd.armed = false;
        }
    }
    return g_dd.active && g_dd.payload == payload;
}

// What a drag source draws between BeginDragDropSource and EndDragDropSource is its preview: a tooltip at the pointer (Dear ImGui's documented
// behaviour), unless DragDrop_SourceNoPreviewTooltip. It used to be laid out in the window, where it pushed the rows below it down while a
// row was dragged (the editor's Hierarchy dropped onto the row above the one under the pointer).
static bool g_ddTip = false;
bool BeginDragDropSource(int flags) {
    if (!g_lay.active) return false;
    const float px = g_lay.prevX, py = g_lay.prevY, pw = g_lay.prevW, ph = g_lay.prevH;
    // Hash the float bits (a NaN, infinity or 1e30 position must not go through a float-to-int cast: undefined).
    auto bitsOf = [](float f) { std::uint32_t u; std::memcpy(&u, &f, sizeof u); return u; };
    const int sid = (int)((bitsOf(px) * 73856093u) ^ (bitsOf(py) * 19349663u) ^ (unsigned)g_lay.seed) | 1;
    if (!g_tdd.active && !g_tdd.armed && g_pressed && hit(px, py, pw, ph)) {
        g_tdd.armed = true; g_tdd.srcId = sid; g_tdd.armX = g_in.mouseX; g_tdd.armY = g_in.mouseY;
    }
    if (g_tdd.armed && g_tdd.srcId == sid && g_in.mouseDown) {
        const float dx = g_in.mouseX - g_tdd.armX, dy = g_in.mouseY - g_tdd.armY;
        if ((dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy) > 6.0f) { g_tdd.active = true; g_tdd.armed = false; g_tdd.hasPayload = false; g_tdd.pay = DragPayload(); }
    }
    const bool on = g_tdd.active && g_tdd.srcId == sid;
    if (on && !(flags & DragDrop_SourceNoPreviewTooltip) && !g_ddTip) g_ddTip = BeginTooltip();
    return on;
}
bool SetDragDropPayload(const char* type, const void* data, int size) {
    if (!g_tdd.active || !type || !*type || size < 0 || size > kDragPayloadMax || (size > 0 && !data)) return false;
    std::snprintf(g_tdd.pay.type, sizeof g_tdd.pay.type, "%s", type);
    if (size > 0) std::memcpy(g_tdd.pay.data, data, (std::size_t)size);
    g_tdd.pay.size = size; g_tdd.hasPayload = true;
    return true;
}
void SetDragDropPreview(const char* text) { if (g_tdd.active) std::snprintf(g_tdd.preview, sizeof g_tdd.preview, "%s", text ? text : ""); }
void EndDragDropSource() { if (g_ddTip) { g_ddTip = false; EndTooltip(); } }

static bool g_tddOver = false;   // BeginDragDropTarget said yes for the item being asked about
bool BeginDragDropTarget() { return BeginDragDropTarget(0); }
bool BeginDragDropTarget(int flags) {
    g_tddOver = false;
    if (!g_lay.active || !g_tdd.active || !g_tdd.hasPayload) return false;
    const float px = g_lay.prevX, py = g_lay.prevY, pw = g_lay.prevW, ph = g_lay.prevH;
    if (g_tddExt) {   // the other toolkit owns the mouse (Input::blocked): the pointer's position, the clip and the windows still count
        if (g_disCount > 0 || !pointIn(g_in.mouseX, g_in.mouseY, px, py, pw, ph) || !inClip()) return false;
        if (g_hitWinOn && coveredByHigher(g_in.mouseX, g_in.mouseY, g_hitWinZ, g_hitWinId)) return false;
    } else if (!hit(px, py, pw, ph)) return false;
    if (!(flags & DragDrop_NoDefaultRect)) {
        const unsigned char* dc = pick(Col_DragDropTarget, g_theme.accent);
        quad(px, py, pw, 2.0f, dc); quad(px, py + ph - 2.0f, pw, 2.0f, dc);   // an accent outline marks the drop spot
    }
    g_tddOver = true;
    return true;
}
void SetExternalDragDrop(const char* type, const void* data, int size, bool released) {
    if (!type || !*type || size < 0 || size > kDragPayloadMax || (size > 0 && !data)) return;
    if (g_tdd.active && !g_tddExt) return;   // OkayUI's own drag is in flight: it wins
    g_tddExt = true; g_tddExtSeen = true; g_tddExtReleased = released;
    g_tdd.active = true; g_tdd.armed = false; g_tdd.srcId = 0; g_tdd.preview[0] = 0;
    std::snprintf(g_tdd.pay.type, sizeof g_tdd.pay.type, "%s", type);
    if (size > 0) std::memcpy(g_tdd.pay.data, data, (std::size_t)size);
    g_tdd.pay.size = size; g_tdd.hasPayload = true;
}
bool IsDragDropExternal() { return g_tddExt && g_tdd.active; }
void CancelDragDrop() {
    g_dd = DragDrop(); g_tdd = TypedDrag(); g_tddExt = false; g_tddExtSeen = g_tddExtReleased = false;
}
const DragPayload* AcceptDragDropPayload(const char* type, int flags) {
    if (!g_tddOver || !g_tdd.active || !g_tdd.hasPayload) return nullptr;
    if (type && *type && std::strncmp(type, g_tdd.pay.type, sizeof g_tdd.pay.type - 1) != 0) return nullptr;
    static DragPayload out;
    out = g_tdd.pay; out.Data = out.data; out.DataSize = out.size;
    if (g_tddExt ? g_tddExtReleased : g_released) { out.delivery = true; g_tdd.active = false; g_tdd.hasPayload = false; return &out; }
    if (flags & DragDrop_AcceptBeforeDelivery) { out.delivery = false; return &out; }
    return nullptr;
}
void EndDragDropTarget() { g_tddOver = false; }
// A drop target over a rectangle given by the caller (an item with no auto-layout slot: a node, a timeline lane). Same rules as
// BeginDragDropTarget: true while a drag with a payload is over `bb`; the accent outline marks it; AcceptDragDropPayload works after it.
bool BeginDragDropTargetCustom(Rect bb, unsigned id) {
    (void)id;
    g_tddOver = false;
    if (!g_lay.active || !g_tdd.active || !g_tdd.hasPayload) return false;
    if (!std::isfinite(bb.x) || !std::isfinite(bb.y) || !std::isfinite(bb.w) || !std::isfinite(bb.h) || bb.w <= 0.0f || bb.h <= 0.0f) return false;
    if (!hit(bb.x, bb.y, bb.w, bb.h)) return false;
    const unsigned char* dc = pick(Col_DragDropTarget, g_theme.accent);
    quad(bb.x, bb.y, bb.w, 2.0f, dc); quad(bb.x, bb.y + bb.h - 2.0f, bb.w, 2.0f, dc);
    g_tddOver = true;
    return true;
}

bool DropTarget(int* outPayload) {
    if (!g_lay.active || !g_dd.active) return false;
    const float px = g_lay.prevX, py = g_lay.prevY, pw = g_lay.prevW, ph = g_lay.prevH;
    const bool over = hit(px, py, pw, ph);
    if (over) {
        // Highlight the drop location with an accent outline.
        quad(px, py, pw, 2.0f, g_theme.accent);
        quad(px, py + ph - 2.0f, pw, 2.0f, g_theme.accent);
        if (g_released) {
            if (outPayload) *outPayload = g_dd.payload;
            g_dd.active = false;
            return true;
        }
    }
    return false;
}


} // namespace OkayUI

// ---- What belongs to one context (OkayUI_ContextState.hpp: each context keeps its own copy of these) ----
namespace OkayUI {
OKAYUI_CONTEXT_STATE(
    OKAYUI_CTX_VAR(g_tddOver),
    OKAYUI_CTX_VAR(g_ddTip))
} // namespace OkayUI
