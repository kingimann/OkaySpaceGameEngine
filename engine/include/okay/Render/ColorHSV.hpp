#pragma once
// Hue / saturation / value maths for colour pickers. Written from the plain definition of the HSV hexcone (hue 0..360 degrees,
// saturation and value 0..1): the six 60-degree sectors of the colour wheel.
#include "okay/Render/Color.hpp"
#include <cmath>
#include <cstdio>
#include <string>

namespace okay {
namespace colorhsv {

struct HSV { float h = 0.0f, s = 0.0f, v = 0.0f; };

/// RGB (0..1 each) to hue (degrees), saturation and value.
inline HSV FromRGB(float r, float g, float b) {
    const float mx = std::fmax(r, std::fmax(g, b)), mn = std::fmin(r, std::fmin(g, b));
    const float d = mx - mn;
    HSV o;
    o.v = mx;
    o.s = mx > 0.0f ? d / mx : 0.0f;
    if (d <= 1e-6f) { o.h = 0.0f; return o; }
    float h;
    if (mx == r)      h = std::fmod((g - b) / d, 6.0f);
    else if (mx == g) h = (b - r) / d + 2.0f;
    else              h = (r - g) / d + 4.0f;
    h *= 60.0f;
    if (h < 0.0f) h += 360.0f;
    o.h = h;
    return o;
}

/// Hue (degrees, wraps), saturation and value (0..1) to RGB with alpha `a`.
inline Color ToColor(float h, float s, float v, float a = 1.0f) {
    h = std::fmod(h, 360.0f); if (h < 0.0f) h += 360.0f;
    s = s < 0.0f ? 0.0f : (s > 1.0f ? 1.0f : s);
    v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    const float c = v * s;
    const float x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    const float m = v - c;
    float r = 0, g = 0, b = 0;
    const int sector = (int)(h / 60.0f) % 6;
    switch (sector) {
        case 0: r = c; g = x; break;
        case 1: r = x; g = c; break;
        case 2: g = c; b = x; break;
        case 3: g = x; b = c; break;
        case 4: r = x; b = c; break;
        default: r = c; b = x; break;
    }
    return Color(r + m, g + m, b + m, a);
}

/// "#RRGGBB", or "#RRGGBBAA" when the colour is not fully opaque.
inline std::string Hex(const Color& c) {
    auto q = [](float f) { int i = (int)std::lround(f * 255.0f); return i < 0 ? 0 : (i > 255 ? 255 : i); };
    char buf[16];
    if (c.a < 0.999f) std::snprintf(buf, sizeof(buf), "#%02X%02X%02X%02X", q(c.r), q(c.g), q(c.b), q(c.a));
    else              std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", q(c.r), q(c.g), q(c.b));
    return buf;
}

} // namespace colorhsv
} // namespace okay
