#pragma once
// Software stand-in for a compressed GPU texture format (the texture import setting "compress").
// Our own block codec in the style of the common 4x4 formats: each 4x4 block keeps two 5:6:5 colour
// endpoints and a 2-bit index per pixel (4 colours on the line between them), and, when the picture has
// transparency, two 8-bit alpha endpoints and a 3-bit index per pixel (8 alpha levels). CompressRoundTrip
// encodes and decodes in memory, so the software renderer shows what a compressed texture looks like
// (banding, colour blocks) without a GPU. The memory use is unchanged; CompressedByteSize says what a
// real compressed upload would take. No GPU, no third-party code.
#include "okay/Graphics/Image.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstdint>

namespace okay {

/// Bytes a block-compressed copy of a w x h picture takes: 8 per 4x4 block, 16 with an alpha channel.
inline std::size_t CompressedByteSize(int w, int h, bool withAlpha) {
    if (w <= 0 || h <= 0) return 0;
    const std::size_t blocks = static_cast<std::size_t>((w + 3) / 4) * static_cast<std::size_t>((h + 3) / 4);
    return blocks * (withAlpha ? 16u : 8u);
}

/// True when any pixel is not fully opaque.
inline bool ImageHasAlpha(const Image& img) {
    const std::uint8_t* p = img.Data();
    const std::size_t n = static_cast<std::size_t>(img.Width()) * static_cast<std::size_t>(img.Height());
    for (std::size_t i = 0; i < n; ++i) if (p[i * 4 + 3] != 255) return true;
    return false;
}

namespace texcompress_detail {

struct Rgb { int r, g, b; };

inline std::uint16_t Pack565(const Rgb& c) {
    return static_cast<std::uint16_t>(((c.r * 31 + 127) / 255) << 11 | ((c.g * 63 + 127) / 255) << 5 | ((c.b * 31 + 127) / 255));
}
inline Rgb Unpack565(std::uint16_t v) {
    const int r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
    return {(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)};
}

// One block of 16 RGB pixels (row-major) in, the same pixels as the codec would show out.
inline void RoundTripColourBlock(const Rgb in[16], Rgb out[16]) {
    // Endpoints: the two most distant colours of the block (exact for a block of two colours).
    Rgb lo = in[0], hi = in[0];
    int far = -1;
    for (int i = 0; i < 16; ++i)
        for (int j = i + 1; j < 16; ++j) {
            const int dr = in[i].r - in[j].r, dg = in[i].g - in[j].g, db = in[i].b - in[j].b;
            const int d = dr * dr + dg * dg + db * db;
            if (d > far) { far = d; lo = in[i]; hi = in[j]; }
        }
    std::uint16_t c0 = Pack565(hi), c1 = Pack565(lo);
    if (c0 < c1) std::swap(c0, c1);
    const Rgb e0 = Unpack565(c0), e1 = Unpack565(c1);
    Rgb pal[4] = {e0, e1,
                  {(2 * e0.r + e1.r) / 3, (2 * e0.g + e1.g) / 3, (2 * e0.b + e1.b) / 3},
                  {(e0.r + 2 * e1.r) / 3, (e0.g + 2 * e1.g) / 3, (e0.b + 2 * e1.b) / 3}};
    for (int i = 0; i < 16; ++i) {
        int best = 0, bestD = 1 << 30;
        for (int k = 0; k < 4; ++k) {
            const int dr = in[i].r - pal[k].r, dg = in[i].g - pal[k].g, db = in[i].b - pal[k].b;
            const int d = dr * dr + dg * dg + db * db;
            if (d < bestD) { bestD = d; best = k; }
        }
        out[i] = pal[best];
    }
}

inline void RoundTripAlphaBlock(const int in[16], int out[16]) {
    int lo = in[0], hi = in[0];
    for (int i = 1; i < 16; ++i) { lo = std::min(lo, in[i]); hi = std::max(hi, in[i]); }
    int pal[8];
    for (int k = 0; k < 8; ++k) pal[k] = (lo * (7 - k) + hi * k + 3) / 7;   // 8 levels, both ends exact
    for (int i = 0; i < 16; ++i) {
        int best = 0, bestD = 1 << 30;
        for (int k = 0; k < 8; ++k) {
            const int d = std::abs(in[i] - pal[k]);
            if (d < bestD) { bestD = d; best = k; }
        }
        out[i] = pal[best];
    }
}

} // namespace texcompress_detail

/// Replace the pixels with what a block-compressed copy would show. The alpha channel is coded only when
/// the picture has some (an opaque picture keeps alpha 255). Pictures smaller than a block are handled by
/// repeating the edge pixels. No-op for an invalid image. Returns the bytes a real compressed copy takes.
inline std::size_t CompressRoundTrip(Image& img) {
    using namespace texcompress_detail;
    if (!img.Valid()) return 0;
    const int w = img.Width(), h = img.Height();
    const bool alpha = ImageHasAlpha(img);
    std::uint8_t* px = img.Data();
    for (int by = 0; by < h; by += 4) {
        for (int bx = 0; bx < w; bx += 4) {
            Rgb in[16], out[16];
            int a[16], ao[16];
            for (int i = 0; i < 16; ++i) {
                const int x = std::min(bx + (i & 3), w - 1), y = std::min(by + (i >> 2), h - 1);
                const std::uint8_t* p = px + (static_cast<std::size_t>(y) * w + x) * 4;
                in[i] = {p[0], p[1], p[2]};
                a[i] = p[3];
            }
            RoundTripColourBlock(in, out);
            if (alpha) RoundTripAlphaBlock(a, ao);
            for (int i = 0; i < 16; ++i) {
                const int x = bx + (i & 3), y = by + (i >> 2);
                if (x >= w || y >= h) continue;
                std::uint8_t* p = px + (static_cast<std::size_t>(y) * w + x) * 4;
                p[0] = static_cast<std::uint8_t>(out[i].r);
                p[1] = static_cast<std::uint8_t>(out[i].g);
                p[2] = static_cast<std::uint8_t>(out[i].b);
                p[3] = alpha ? static_cast<std::uint8_t>(ao[i]) : static_cast<std::uint8_t>(255);
            }
        }
    }
    return CompressedByteSize(w, h, alpha);
}

} // namespace okay
