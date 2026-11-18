// okay::tex common pieces. Written from the sRGB piecewise curve (IEC 61966-2-1) and plain arithmetic.
#include "okay/Texture/Texture.hpp"
#include <algorithm>
#include <cmath>

namespace okay::tex {

float SrgbToLinear(float s) {
    if (!(s > 0.0f)) return 0.0f;
    if (s >= 1.0f) return 1.0f;
    return s <= 0.04045f ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f);
}
float LinearToSrgb(float l) {
    if (!(l > 0.0f)) return 0.0f;
    if (l >= 1.0f) return 1.0f;
    return l <= 0.0031308f ? l * 12.92f : 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f;
}

Surface FromRGBA8(const std::uint8_t* rgba, int w, int h, bool srgb) {
    if (!rgba || w <= 0 || h <= 0) return Surface();
    Surface s(w, h);
    float lut[256];
    for (int i = 0; i < 256; ++i) lut[i] = srgb ? SrgbToLinear(i / 255.0f) : i / 255.0f;
    const std::size_t n = static_cast<std::size_t>(w) * h;
    for (std::size_t i = 0; i < n; ++i) {
        for (int c = 0; c < 3; ++c) s.px[i * 4 + c] = lut[rgba[i * 4 + c]];
        s.px[i * 4 + 3] = rgba[i * 4 + 3] / 255.0f;
    }
    return s;
}

std::vector<std::uint8_t> ToRGBA8(const Surface& s, bool srgb) {
    std::vector<std::uint8_t> out;
    if (!s.Valid()) return out;
    out.resize(s.px.size());
    const std::size_t n = static_cast<std::size_t>(s.w) * s.h;
    auto q = [](float v) { v = v < 0 ? 0 : (v > 1 ? 1 : v); return static_cast<std::uint8_t>(std::floor(v * 255.0f + 0.5f)); };
    for (std::size_t i = 0; i < n; ++i) {
        for (int c = 0; c < 3; ++c) {
            float v = s.px[i * 4 + c];
            out[i * 4 + c] = q(srgb ? LinearToSrgb(v) : v);
        }
        out[i * 4 + 3] = q(s.px[i * 4 + 3]);
    }
    return out;
}

int MipCount(int w, int h) {
    if (w < 1 || h < 1) return 0;
    int n = 1;
    while (w > 1 || h > 1) { w = std::max(1, w / 2); h = std::max(1, h / 2); ++n; }
    return n;
}

void MipSize(int w, int h, int level, int* outW, int* outH) {
    for (int i = 0; i < level; ++i) { w = std::max(1, w / 2); h = std::max(1, h / 2); }
    if (outW) *outW = w;
    if (outH) *outH = h;
}

double Psnr8(const std::uint8_t* a, const std::uint8_t* b, std::size_t count) {
    if (!a || !b || count == 0) return 0.0;
    double se = 0.0;
    for (std::size_t i = 0; i < count; ++i) { double d = double(a[i]) - double(b[i]); se += d * d; }
    if (se == 0.0) return 99.0;
    double mse = se / double(count);
    return 10.0 * std::log10(255.0 * 255.0 / mse);
}

} // namespace okay::tex
