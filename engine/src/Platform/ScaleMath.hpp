#pragma once
// High-DPI and multi-monitor maths for the own backends and the apps. Pure logic, no OS.
// Terms: "logical" = window units the app lays out in; "physical" = device pixels. scale = physical / logical.
#include "okay/Platform/Platform.hpp"
#include <cmath>
#include <vector>

namespace okay::platform::scale {

constexpr unsigned kBaseDpi = 96;
constexpr float kMinScale = 0.5f, kMaxScale = 4.0f;

/// 96 dpi = 1.0. A dpi of 0 (unknown) counts as 96.
inline float FromDpi(unsigned dpi) { return (dpi == 0 ? kBaseDpi : dpi) / (float)kBaseDpi; }

/// Clamp to [kMinScale, kMaxScale] and round to the nearest `step` (default a quarter), the steps Windows offers
/// (100, 125, 150, 175, 200 %), so a font does not land on a fractional size that blurs. NaN / inf give 1.
inline float Snap(float s, float step = 0.25f) {
    if (!(s == s) || std::isinf(s)) return 1.0f;
    if (step > 0.0f) s = std::floor(s / step + 0.5f) * step;
    return s < kMinScale ? kMinScale : (s > kMaxScale ? kMaxScale : s);
}

/// Round half away from zero, so a 1.5 scale of 1 is 2 and of -1 is -2.
inline int RoundToInt(double v) { return (int)(v < 0 ? std::ceil(v - 0.5) : std::floor(v + 0.5)); }

inline int ToPhysical(int logical, float scale) { return RoundToInt((double)logical * scale); }
inline int ToLogical(int physical, float scale) { return scale > 0.0f ? RoundToInt((double)physical / scale) : physical; }

/// The scale between a window's logical size and its drawable (framebuffer) size, from the width, and the
/// height when the width is 0. 1.0 when the window has no size.
inline float FramebufferScale(int logicalW, int logicalH, int drawableW, int drawableH) {
    if (logicalW > 0 && drawableW > 0) return drawableW / (float)logicalW;
    if (logicalH > 0 && drawableH > 0) return drawableH / (float)logicalH;
    return 1.0f;
}

/// Mouse position in logical units -> device pixels of the framebuffer (what a renderer working in pixels needs).
inline void MouseToFramebuffer(float lx, float ly, float scale, float& px, float& py) { px = lx * scale; py = ly * scale; }

/// Font pixel size for a base size at `scale`, never below `minPx`.
inline int FontPixels(float basePx, float scale, int minPx = 6) {
    const int v = RoundToInt((double)basePx * scale);
    return v < minPx ? minPx : v;
}

/// Index of the display a rectangle belongs to: the one it overlaps most; when it overlaps none, the one whose
/// centre is nearest to the rectangle's centre. -1 for an empty list. (Window placement and "which DPI is this
/// window on" use it.)
inline int DisplayForRect(const std::vector<DisplayInfo>& d, int x, int y, int w, int h) {
    if (d.empty()) return -1;
    int best = -1;
    long long bestArea = 0;
    for (size_t i = 0; i < d.size(); i++) {
        const long long ix = (long long)std::fmax(0, std::fmin((double)x + w, (double)d[i].x + d[i].width) - std::fmax(x, d[i].x));
        const long long iy = (long long)std::fmax(0, std::fmin((double)y + h, (double)d[i].y + d[i].height) - std::fmax(y, d[i].y));
        if (ix * iy > bestArea) { bestArea = ix * iy; best = (int)i; }
    }
    if (best >= 0) return best;
    const double cx = x + w / 2.0, cy = y + h / 2.0;
    double bd = 0;
    for (size_t i = 0; i < d.size(); i++) {
        const double dx = d[i].x + d[i].width / 2.0 - cx, dy = d[i].y + d[i].height / 2.0 - cy;
        const double dist = dx * dx + dy * dy;
        if (best < 0 || dist < bd) { bd = dist; best = (int)i; }
    }
    return best;
}

/// Index of the display that holds a desktop point; -1 when none does.
inline int DisplayForPoint(const std::vector<DisplayInfo>& d, int px, int py) {
    for (size_t i = 0; i < d.size(); i++)
        if (px >= d[i].x && px < d[i].x + d[i].width && py >= d[i].y && py < d[i].y + d[i].height) return (int)i;
    return -1;
}

/// Move and shrink a window rectangle so it fits the work area of the display it is mostly on (a window saved on
/// a monitor that is gone must not open off screen). Size is capped to the work area; the position is pulled in
/// so at least the whole window shows when it fits. Returns false when there are no displays (rect unchanged).
inline bool FitToWorkArea(const std::vector<DisplayInfo>& d, int& x, int& y, int& w, int& h) {
    const int i = DisplayForRect(d, x, y, w, h);
    if (i < 0) return false;
    const DisplayInfo& m = d[(size_t)i];
    const int wx = m.workWidth > 0 ? m.workX : m.x, wy = m.workHeight > 0 ? m.workY : m.y;
    const int ww = m.workWidth > 0 ? m.workWidth : m.width, wh = m.workHeight > 0 ? m.workHeight : m.height;
    if (w > ww) w = ww;
    if (h > wh) h = wh;
    if (x + w > wx + ww) x = wx + ww - w;
    if (y + h > wy + wh) y = wy + wh - h;
    if (x < wx) x = wx;
    if (y < wy) y = wy;
    return true;
}

/// A window keeps its logical size when it moves between monitors of different scale only if the app asks; this
/// is the size to ask the OS for in device pixels after the scale changed from `oldScale` to `newScale`, so the
/// window stays the same logical size (Windows: WM_DPICHANGED's suggested rect does this).
inline int RescaleWindowExtent(int physicalExtent, float oldScale, float newScale) {
    if (oldScale <= 0.0f || newScale <= 0.0f) return physicalExtent;
    return RoundToInt((double)physicalExtent * newScale / oldScale);
}

} // namespace okay::platform::scale
