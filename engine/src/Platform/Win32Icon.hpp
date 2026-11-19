#pragma once
// Pure pixel preparation for a Windows window icon (tested on Linux by tests/test_platform_win32_pure.cpp).
// CreateIconIndirect takes a colour bitmap and a monochrome mask. With a 32-bit colour bitmap that carries
// alpha the mask is ignored by current Windows but must exist, so we still build it correctly (1 bit per pixel,
// rows padded to a 16-bit boundary, 1 = transparent). Written from the public Microsoft documentation of
// ICONINFO, BITMAP and SM_CXICON / SM_CXSMICON.
#include <algorithm>
#include <cstdint>
#include <vector>

namespace okay::platform::win32icon {

constexpr int kMaxIconEdge = 512;

inline bool ValidIcon(const uint8_t* rgba, int w, int h) { return rgba && w >= 1 && h >= 1 && w <= kMaxIconEdge && h <= kMaxIconEdge; }

/// RGBA8 (straight alpha) -> BGRA8 in the same order, for a 32-bit DIB section.
inline std::vector<uint8_t> ToBgra(const uint8_t* rgba, int w, int h) {
    std::vector<uint8_t> o((size_t)w * h * 4);
    for (size_t i = 0, n = (size_t)w * h; i < n; ++i) { o[i * 4] = rgba[i * 4 + 2]; o[i * 4 + 1] = rgba[i * 4 + 1]; o[i * 4 + 2] = rgba[i * 4]; o[i * 4 + 3] = rgba[i * 4 + 3]; }
    return o;
}
/// Bytes per mask row: width bits rounded up to a multiple of 16 bits.
inline int MaskStride(int w) { return ((w + 15) / 16) * 2; }
/// 1 bpp AND mask, top row first, most significant bit = leftmost pixel; a set bit is a transparent pixel (alpha < 128).
inline std::vector<uint8_t> AndMask(const uint8_t* rgba, int w, int h) {
    const int stride = MaskStride(w);
    std::vector<uint8_t> m((size_t)stride * h, 0);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (rgba[((size_t)y * w + x) * 4 + 3] < 128) m[(size_t)y * stride + x / 8] |= (uint8_t)(0x80 >> (x % 8));
    return m;
}
/// Box-filter resize (each target pixel averages the source pixels it covers; colour weighted by alpha so a
/// transparent border does not darken the edge). Used to make the 16 / 32 pixel icons from one big image.
inline std::vector<uint8_t> Resize(const uint8_t* rgba, int w, int h, int tw, int th) {
    std::vector<uint8_t> o((size_t)tw * th * 4, 0);
    for (int ty = 0; ty < th; ++ty) {
        const int y0 = (int)((long long)ty * h / th), y1 = std::max(y0 + 1, (int)((long long)(ty + 1) * h / th));
        for (int tx = 0; tx < tw; ++tx) {
            const int x0 = (int)((long long)tx * w / tw), x1 = std::max(x0 + 1, (int)((long long)(tx + 1) * w / tw));
            unsigned long long r = 0, g = 0, b = 0, a = 0, n = 0;
            for (int y = y0; y < y1 && y < h; ++y)
                for (int x = x0; x < x1 && x < w; ++x) {
                    const uint8_t* p = rgba + ((size_t)y * w + x) * 4;
                    r += (unsigned long long)p[0] * p[3]; g += (unsigned long long)p[1] * p[3]; b += (unsigned long long)p[2] * p[3]; a += p[3]; ++n;
                }
            uint8_t* q = &o[((size_t)ty * tw + tx) * 4];
            if (a) { q[0] = (uint8_t)((r + a / 2) / a); q[1] = (uint8_t)((g + a / 2) / a); q[2] = (uint8_t)((b + a / 2) / a); }
            q[3] = n ? (uint8_t)((a + n / 2) / n) : 0;
        }
    }
    return o;
}

} // namespace okay::platform::win32icon
