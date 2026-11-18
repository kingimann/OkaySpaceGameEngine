#include "okay/UI/OkayUICompat.hpp"
#include "okay/UI/OkayUIDock.hpp"

#include <cmath>
#include <cstring>

namespace OkayUI {

namespace {
float Clamp01(float v) { return std::isfinite(v) ? (v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v)) : 0.0f; }
float Finite(float v) { return std::isfinite(v) ? v : 0.0f; }
}

Viewport GetMainViewport() {
    Viewport vp;
    const IO& io = GetIO();
    vp.size = Vec2{ Finite(io.displaySize.x), Finite(io.displaySize.y) };
    return vp;
}

Vec2 GetContentRegionMax() {
    const Vec2 p = GetCursorPos(), a = GetContentRegionAvail();
    return Vec2{ p.x + a.x, p.y + a.y };
}
Vec2 GetWindowContentRegionMax() { return GetContentRegionMax(); }

void SetScrollHereY(float centerRatio) {
    const float view = GetChildViewHeight();
    if (!(view > 0.0f)) return;   // not inside a scrolling child
    // Inside a child the cursor position is measured from the top of the unscrolled content, so it is where the next item goes.
    SetScrollY(GetCursorPosY() - Clamp01(centerRatio) * view);
}
float GetScrollX() { return 0.0f; }
void SetScrollX(float) {}

void SetNextWindowSizeConstraints(Vec2 minSize, Vec2) { ResizableNextWindow(Finite(minSize.x), Finite(minSize.y)); }

void SetItemDefaultFocus() {}
void SetNextItemAllowOverlap() {}

} // namespace OkayUI

namespace OkayUI {
WindowInfo FindWindowByName(const char* title) {
    WindowInfo w;
    w.found = FindWindowSlot(title, w.pos, w.size, w.focused);
    if (w.found) w.docked = IsWindowDocked(title);
    return w;
}
bool IsWindowDocked(const char* title) {
    if (!title || !*title) return false;
    const DockLayout& l = GetDockLayout();
    return l.Contains(title) && !l.IsFloating(title);
}
} // namespace OkayUI
