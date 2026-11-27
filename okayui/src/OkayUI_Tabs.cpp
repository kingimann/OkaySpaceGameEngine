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


void TabBar(const char* const* labels, int count, int* current) {
    if (!g_lay.active || !labels || !current || count <= 0) return;
    const float h = rowH();
    // Reserve the full row; lay tabs left-to-right within it.
    float x, y; place(fullW(), h, x, y);
    tabBarBackground(x, y, fullW(), h);
    float tx = x;
    for (int i = 0; i < count; ++i) {
        const char* lbl = labels[i] ? labels[i] : "";
        const float tw = labelW(lbl) + (g_look ? 28.0f : 20.0f);
        const unsigned uid = ((unsigned)hashLabel(lbl) ^ ((unsigned)i * 2654435761u)) & 0x7fffffffu;
        Tab((int)uid, tx, y, tw, h, lbl, current, i);
        tx += tw + (g_look ? 6.0f : 2.0f);
    }
}


// What a tab row draws under its tabs, by the design's tab shape: a hairline under the row (underline), nothing (pill), or a
// sunken rounded track the chosen segment sits in (segmented).
void ui_core::tabBarBackground(float x, float y, float w, float h) {
    if (g_theme.tabShape == 2) controlBox(x, y, w, h, g_theme.rounding, g_theme.track);
    else if (g_theme.tabShape == 0) quad(x, y + h - 1.0f, w, 1.0f, g_theme.border);
}

// ---- Tab bar with per-tab bodies (BeginTabBar / BeginTabItem) ---------------------------------------
namespace {
int* tabSelSlot(int bar) {
    for (auto& s : g_tabSel) if (s.bar == bar) return &s.sel;
    for (auto& s : g_tabSel) if (s.bar == 0) { s.bar = bar; s.sel = 0; return &s.sel; }
    g_tabSel[0].bar = bar; g_tabSel[0].sel = 0; return &g_tabSel[0].sel;
}
// A tab bar begun inside a tab item of another one (the Dialogue window's side panel inside its Dialogue tab): the outer bar waits here
// until the inner one ends, as Dear ImGui's tab bars nest.
TabBarCtx g_tabBarOuter[4];
int g_tabBarOuterTop = 0;
} // namespace
void ui_core::tabBarsReset() { g_tabBar.active = false; g_tabBarOuterTop = 0; }

bool BeginTabBar(const char* id, int tabBarFlags) {
    if (!g_lay.active || !id) return false;
    if (g_tabBar.active) {
        if (g_tabBarOuterTop >= 4) return false;
        g_tabBarOuter[g_tabBarOuterTop++] = g_tabBar;
    }
    const float h = rowH();
    float x, y; place(fullW(), h, x, y);
    g_tabBar = TabBarCtx();
    g_tabBar.active = true; g_tabBar.id = hashLabel(id) | 1;
    g_tabBar.x = x; g_tabBar.y = y; g_tabBar.w = fullW(); g_tabBar.h = h;
    g_tabBar.cx = x; g_tabBar.tr = x + fullW();
    g_tabBar.sel = tabSelSlot(g_tabBar.id);
    g_tabBar.flags = tabBarFlags;
    if (tabBarFlags & TabBar_Reorderable) {   // the order slot of this bar (or the least recently used one)
        TabOrder* slot = nullptr; TabOrder* oldest = &g_tabOrders[0];
        for (TabOrder& t : g_tabOrders) {
            if (t.bar == g_tabBar.id) { slot = &t; break; }
            if (t.lastFrame < oldest->lastFrame) oldest = &t;
        }
        if (!slot) { slot = oldest; *slot = TabOrder(); slot->bar = g_tabBar.id; g_tabBar.fresh = true; }
        slot->lastFrame = g_frameCount;
        g_tabBar.ord = slot;
    }
    tabBarBackground(x, y, fullW(), h);
    return true;
}
bool BeginTabBar(const char* id) { return BeginTabBar(id, 0); }

int TabDragTarget(const float* widths, int n, int from, float left, float gap, float mouseX) {
    if (!widths || n <= 0 || from < 0 || from >= n) return from;
    if (!std::isfinite(mouseX) || !std::isfinite(left) || !std::isfinite(gap)) return from;
    float x = left;
    for (int i = 0; i < n; ++i) {
        const float w = std::isfinite(widths[i]) && widths[i] > 0.0f ? widths[i] : 0.0f;
        if (mouseX < x + w + gap * 0.5f) return i;
        x += w + gap;
    }
    return n - 1;   // past the last tab
}

bool BeginTabItem(const char* label, bool* open, int flags) {
    if (!g_lay.active || !g_tabBar.active || !label) return false;
    TabBarCtx& tb = g_tabBar;
    const int id = (hashLabel(label) ^ tb.id) | 1;
    const float closeW = open ? 16.0f : 0.0f;
    const float w = visW(label) + (g_look ? 28.0f : 20.0f) + closeW, gap = g_look ? 6.0f : 2.0f;
    float x;
    int ordIdx = -1;
    if (flags & Tab_Trailing) { tb.tr -= w; x = tb.tr; tb.tr -= gap; }
    else if (tb.ord) {
        TabOrder& o = *tb.ord;
        for (int i = 0; i < o.n; ++i) if (o.ids[i] == id) { ordIdx = i; break; }
        if (ordIdx < 0 && o.n < 32) {   // a tab the bar has not met: it goes last
            ordIdx = o.n++; o.ids[ordIdx] = id; o.w[ordIdx] = w;
            if ((tb.flags & TabBar_AutoSelectNewTabs) && !tb.fresh) *tb.sel = id;
        }
        if (ordIdx >= 0) o.w[ordIdx] = w;
        x = tb.x;
        for (int i = 0; i < ordIdx; ++i) x += o.w[i] + gap;
        if (ordIdx < 0) { x = tb.cx; }
        tb.cx = x + w + gap;
        if (tb.nSeen < 32) tb.seen[tb.nSeen++] = id;
    }
    else { x = tb.cx; tb.cx += w + gap; }
    if (tb.firstId == 0) tb.firstId = id;
    if (*tb.sel == 0) *tb.sel = id;                  // nothing chosen yet: the first tab
    if (flags & Tab_SetSelected) *tb.sel = id;
    Tab(id, x, tb.y, w, tb.h, label, tb.sel, id);
    g_lay.prevX = x; g_lay.prevY = tb.y; g_lay.prevW = w; g_lay.prevH = tb.h;   // the item rect / hover queries that follow are about this tab's header
    if (flags & TabItem_UnsavedDocument) {            // a small dot after the label
        const float dx = x + w - closeW - 9.0f, dy = tb.y + tb.h * 0.5f - 2.0f;
        fillCircle(dx + 2.0f, dy + 2.0f, 3.0f, g_theme.accent, 16);
    }
    if (tb.ord && ordIdx >= 0 && !(flags & (Tab_Trailing | TabItem_NoReorder)) && g_active == id && IsMouseDragging(0, 4.0f) && g_in.mouseDown) {
        TabOrder& o = *tb.ord;   // dragging: the tab goes where the pointer is
        const int to = TabDragTarget(o.w, o.n, ordIdx, tb.x, gap, g_in.mouseX);
        if (to != ordIdx) {
            const int tid = o.ids[ordIdx]; const float tw = o.w[ordIdx];
            if (to > ordIdx) for (int i = ordIdx; i < to; ++i) { o.ids[i] = o.ids[i + 1]; o.w[i] = o.w[i + 1]; }
            else for (int i = ordIdx; i > to; --i) { o.ids[i] = o.ids[i - 1]; o.w[i] = o.w[i - 1]; }
            o.ids[to] = tid; o.w[to] = tw;
        }
    }
    bool visible = (*tb.sel == id);
    if (open) {                                       // a small x on the right closes the tab
        const float cx = x + w - closeW, cy = tb.y + (tb.h - textH()) * 0.5f;
        const int cid = id ^ 0x7ab;
        const bool inside = hit(cx, tb.y, closeW - 2.0f, tb.h);
        if (inside) g_hot = cid;
        if (g_active == cid) { if (g_released) { if (inside) { *open = false; } g_active = 0; } }
        else if (inside && g_pressed) g_active = cid;
        (void)cy;   // a vector cross, on a soft round chip under the pointer
        const float ccx = cx + (closeW - 2.0f) * 0.5f, ccy = tb.y + tb.h * 0.5f;
        if (inside) { unsigned char chip[4]; fillCircle(ccx, ccy, 7.0f, withAlpha(g_theme.ink, 0.12f, chip), 20); }
        xMark(ccx, ccy, 7.0f, inside ? g_theme.text : pick(Col_TextDisabled, g_theme.textDim));
        // a middle click anywhere on the tab closes it too
        if (g_mPressed && !(flags & TabItem_NoCloseWithMiddleMouseButton) && !(tb.flags & TabBar_NoCloseWithMiddleMouseButton) && hit(x, tb.y, w, tb.h)) *open = false;
        if (!*open) visible = false;
    }
    if (visible) tb.seenSel = true;
    return visible;
}

void EndTabItem() {}

// A button on a dock node's strip (DockNodeBeginAmendTabBar in OkayUIDockNodes.cpp): placed right-to-left from the strip's right edge.
namespace { struct TabAmendState { bool on = false; float x = 0, y = 0, w = 0, h = 0, used = 0; } g_amend; }
void SetTabAmend(bool on, Rect strip) { g_amend.on = on; g_amend.x = strip.x; g_amend.y = strip.y; g_amend.w = strip.w; g_amend.h = strip.h; g_amend.used = 0.0f; }
float TabAmendUsed() { return g_amend.used; }
bool TabItemButton(const char* label, int flags) {
    if (g_amend.on && g_lay.active && !g_tabBar.active && label) {
        const int id = (hashLabel(label) ^ 0x7bc) | 1;
        const float w = g_nextItemW > 0.0f ? g_nextItemW : visW(label) + 20.0f, gap = g_look ? 6.0f : 2.0f;
        g_nextItemW = -1.0f;
        const float x = g_amend.x + g_amend.w - g_amend.used - w;
        g_amend.used += w + gap;
        if (w < 1.0f || x < g_amend.x) return false;
        const bool clicked = Button(id, x, g_amend.y, w, g_amend.h, label);
        g_lay.prevX = x; g_lay.prevY = g_amend.y; g_lay.prevW = w; g_lay.prevH = g_amend.h;
        return clicked;
    }
    if (!g_lay.active || !g_tabBar.active || !label) return false;
    TabBarCtx& tb = g_tabBar;
    const int id = (hashLabel(label) ^ tb.id ^ 0x7bb) | 1;
    const float w = visW(label) + 20.0f, gap = g_look ? 6.0f : 2.0f;
    float x;
    if (flags & Tab_Trailing) { tb.tr -= w; x = tb.tr; tb.tr -= gap; } else { x = tb.cx; tb.cx += w + gap; }
    const bool clicked = Button(id, x, tb.y, w, tb.h, label);
    g_lay.prevX = x; g_lay.prevY = tb.y; g_lay.prevW = w; g_lay.prevH = tb.h;
    return clicked;
}

void EndTabBar() {
    if (!g_tabBar.active) return;
    // The chosen tab vanished (closed or removed): fall back to the first.
    if (!g_tabBar.seenSel && g_tabBar.firstId != 0) *g_tabBar.sel = g_tabBar.firstId;
    if (g_tabBar.ord) {   // forget tabs that were not shown this frame (closed or removed), so no gap is left
        TabOrder& o = *g_tabBar.ord; int k = 0;
        for (int i = 0; i < o.n; ++i) {
            bool seen = false;
            for (int j = 0; j < g_tabBar.nSeen; ++j) if (g_tabBar.seen[j] == o.ids[i]) { seen = true; break; }
            if (seen) { o.ids[k] = o.ids[i]; o.w[k] = o.w[i]; ++k; }
        }
        o.n = k;
    }
    g_tabBar.active = false;
    if (g_tabBarOuterTop > 0) g_tabBar = g_tabBarOuter[--g_tabBarOuterTop];   // back to the bar this one was begun in
}

} // namespace OkayUI

// ---- What belongs to one context (OkayUI_ContextState.hpp: each context keeps its own copy of these) ----
namespace OkayUI {
OKAYUI_CONTEXT_STATE(
    OKAYUI_CTX_VAR(g_amend),
    OKAYUI_CTX_VAR(g_tabBarOuter),
    OKAYUI_CTX_VAR(g_tabBarOuterTop))
} // namespace OkayUI
