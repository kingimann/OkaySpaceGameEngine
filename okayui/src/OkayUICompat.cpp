#include "okay/UI/OkayUICompat.hpp"

#include <cmath>
#include <cstring>

namespace OkayUI {

namespace {
float Clamp01(float v) { return std::isfinite(v) ? (v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v)) : 0.0f; }
float Finite(float v) { return std::isfinite(v) ? v : 0.0f; }
unsigned Byte(float v) { return (unsigned)(Clamp01(v) * 255.0f + 0.5f); }
}

Viewport GetMainViewport() {
    Viewport vp;
    const IO& io = GetIO();
    vp.size = Vec2{ Finite(io.displaySize.x), Finite(io.displaySize.y) };
    return vp;
}

Vec4 ColorConvertU32ToFloat4(Color32 c) {
    const float s = 1.0f / 255.0f;
    return Vec4{ (float)(c & 0xffu) * s, (float)((c >> 8) & 0xffu) * s, (float)((c >> 16) & 0xffu) * s, (float)((c >> 24) & 0xffu) * s };
}

Color32 ColorConvertFloat4ToU32(Vec4 v) { return RGBA(Byte(v.x), Byte(v.y), Byte(v.z), Byte(v.w)); }

void ColorConvertRGBtoHSV(float r, float g, float b, float& h, float& s, float& v) {
    r = Clamp01(r); g = Clamp01(g); b = Clamp01(b);
    const float mx = std::fmax(r, std::fmax(g, b)), mn = std::fmin(r, std::fmin(g, b));
    const float d = mx - mn;
    v = mx;
    s = mx > 0.0f ? d / mx : 0.0f;
    if (d <= 0.0f) { h = 0.0f; return; }
    float hh;
    if (mx == r) hh = (g - b) / d + (g < b ? 6.0f : 0.0f);
    else if (mx == g) hh = (b - r) / d + 2.0f;
    else hh = (r - g) / d + 4.0f;
    h = hh / 6.0f;
}

void ColorConvertHSVtoRGB(float h, float s, float v, float& r, float& g, float& b) {
    h = Finite(h); s = Clamp01(s); v = Clamp01(v);
    h = h - std::floor(h);   // any hue wraps around the wheel
    const float x = h * 6.0f;
    const int i = (int)std::floor(x);
    const float f = x - (float)i;
    const float p = v * (1.0f - s), q = v * (1.0f - s * f), t = v * (1.0f - s * (1.0f - f));
    switch (i % 6) {
    default:
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    case 5: r = v; g = p; b = q; break;
    }
}

Vec4 GetStyleColorVec4(Col which) {
    const Theme& t = Style();
    const unsigned char* c = t.text;
    switch (which) {
    case Col_Bg: c = t.bg; break;
    case Col_BgHover: c = t.bgHover; break;
    case Col_BgDown: c = t.bgDown; break;
    case Col_Border: c = t.border; break;
    case Col_Text: c = t.text; break;
    case Col_Panel: c = t.panel; break;
    case Col_Track: c = t.track; break;
    case Col_Accent: c = t.accent; break;
    default: break;
    }
    return Vec4{ c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f, c[3] / 255.0f };
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

unsigned GetID(const char* str) {
    unsigned h = 2166136261u;
    for (const char* p = str ? str : ""; *p; ++p) { h ^= (unsigned char)*p; h *= 16777619u; }
    return h ? h : 1u;
}

void SetItemDefaultFocus() {}
void SetNextItemAllowOverlap() {}
void TableSetupScrollFreeze(int, int) {}

} // namespace OkayUI
