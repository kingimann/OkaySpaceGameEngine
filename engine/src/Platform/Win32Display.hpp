#pragma once
// Pure display and DPI maths for the own Windows backend (no Windows headers; tested on Linux by
// tests/test_platform_win32_pure.cpp). Written from the public Microsoft documentation: 96 DPI is
// 100 percent, WM_DPICHANGED carries the new DPI in the low word of wParam and the suggested window
// rectangle in lParam, GetDpiForMonitor reports effective DPI.
#include "okay/Platform/Platform.hpp"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace okay::platform::win32disp {

constexpr int kBaseDpi = 96;

/// DPI -> scale factor (96 = 1.0, 144 = 1.5). Anything below 48 or above 1000 is not a DPI: 1.0.
inline float ScaleFromDpi(int dpi) { return dpi >= 48 && dpi <= 1000 ? (float)dpi / (float)kBaseDpi : 1.0f; }
/// A length in device-independent pixels (at 96 DPI) -> device pixels at `dpi`, rounded to nearest.
inline int ScaleLength(int len, int dpi) {
    if (dpi < 48 || dpi > 1000) return len;
    const long long n = (long long)len * dpi;
    return (int)(n >= 0 ? (n + kBaseDpi / 2) / kBaseDpi : -((-n + kBaseDpi / 2) / kBaseDpi));
}
/// WM_DPICHANGED: the new horizontal DPI is the low word of wParam (the high word is the vertical one, equal on Windows).
inline int DpiFromWParam(uint64_t wp) { return (int)(wp & 0xFFFF); }

struct Rect { int x = 0, y = 0, w = 0, h = 0; };
inline bool Contains(const Rect& r, int px, int py) { return px >= r.x && py >= r.y && px < r.x + r.w && py < r.y + r.h; }

/// A rectangle given as left, top, right, bottom (a RECT) -> Rect, never with a negative size.
inline Rect FromLtrb(int l, int t, int r, int b) { return Rect{l, t, r > l ? r - l : 0, b > t ? b - t : 0}; }

/// Primary display first, then left to right, then top to bottom: the order `Backend::DisplayAt` lists them in.
inline void OrderDisplays(std::vector<DisplayInfo>& v) {
    std::stable_sort(v.begin(), v.end(), [](const DisplayInfo& a, const DisplayInfo& b) {
        if (a.primary != b.primary) return a.primary;
        if (a.x != b.x) return a.x < b.x;
        return a.y < b.y;
    });
}
/// Index of the display containing the point, or the nearest one by centre distance when none does; -1 for an empty list.
inline int DisplayForPoint(const std::vector<DisplayInfo>& v, int px, int py) {
    int best = -1;
    long long bestD = 0;
    for (size_t i = 0; i < v.size(); ++i) {
        if (Contains(Rect{v[i].x, v[i].y, v[i].w, v[i].h}, px, py)) return (int)i;
        const long long cx = v[i].x + v[i].w / 2, cy = v[i].y + v[i].h / 2;
        const long long d = (cx - px) * (cx - px) + (cy - py) * (cy - py);
        if (best < 0 || d < bestD) { best = (int)i; bestD = d; }
    }
    return best;
}
/// A refresh rate from EnumDisplaySettings: 0 and 1 mean "hardware default" and are reported as unknown (0).
inline int RefreshFromDevMode(int hz) { return hz > 1 && hz < 1000 ? hz : 0; }

} // namespace okay::platform::win32disp
