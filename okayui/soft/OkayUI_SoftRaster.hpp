#pragma once
// Internal to okay_ui_soft: the pixel routines both the draw-list renderer (OkayUI_Soft.cpp) and the
// software okay::platform::Painter (OkayUI_SoftPainter.cpp) draw with. One implementation of the triangle
// fill rule, texture sampling and source-over blending, so the two cannot drift apart.
#include "okay/UI/OkayUI.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace OkayUI {
namespace soft {
namespace raster {

/// A writable RGBA8 image (top row first).
struct Surface { std::uint8_t* px = nullptr; int w = 0, h = 0; };
/// A readable RGBA8 image and how it is sampled and combined with what is under it.
struct Tex {
    int w = 0, h = 0;
    const std::uint8_t* rgba = nullptr;
    bool linear = false;                               // bilinear instead of nearest
    std::uint8_t modR = 255, modG = 255, modB = 255, modA = 255;   // colour / alpha modulation
};
struct Clip { std::int64_t x0, y0, x1, y1; };          // pixels, [x0, x1) x [y0, y1)

inline bool Ok(float f) { return std::isfinite(f) && std::fabs(f) < 1e6f; }
inline int Div255(int v) { return (v + 127) / 255; }
inline int Clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }
inline bool SameColor(const DrawVertex& a, const DrawVertex& b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }

/// Write one source pixel (already clamped to 0..255). `blend`: source-over; else replace, alpha included.
inline void Put(std::uint8_t* d, int r, int g, int b, int a, bool blend) {
    if (!blend || a == 255) { d[0] = (std::uint8_t)r; d[1] = (std::uint8_t)g; d[2] = (std::uint8_t)b; d[3] = (std::uint8_t)a; return; }
    if (a == 0) return;
    d[0] = (std::uint8_t)Div255(r * a + d[0] * (255 - a));
    d[1] = (std::uint8_t)Div255(g * a + d[1] * (255 - a));
    d[2] = (std::uint8_t)Div255(b * a + d[2] * (255 - a));
    d[3] = (std::uint8_t)std::min(255, a + Div255(d[3] * (255 - a)));
}

/// One texel, coordinates already clamped.
inline const std::uint8_t* Texel(const Tex& t, int x, int y) {
    x = std::max(0, std::min(t.w - 1, x)); y = std::max(0, std::min(t.h - 1, y));
    return t.rgba + ((std::size_t)y * (std::size_t)t.w + (std::size_t)x) * 4;
}

/// Sample at normalised coordinates (u, v), clamped to the edges; out = r, g, b, a.
inline void Sample(const Tex& t, double u, double v, int out[4]) {
    // A NaN or infinite coordinate is clamped before any int conversion.
    if (!(u == u)) u = 0;
    if (!(v == v)) v = 0;
    u = std::max(-1e4, std::min(1e4, u)); v = std::max(-1e4, std::min(1e4, v));
    if (!t.linear) {
        const std::uint8_t* p = Texel(t, (int)std::floor(u * t.w), (int)std::floor(v * t.h));
        out[0] = p[0]; out[1] = p[1]; out[2] = p[2]; out[3] = p[3];
        return;
    }
    const double fx = u * t.w - 0.5, fy = v * t.h - 0.5;
    const double flx = std::floor(fx), fly = std::floor(fy);
    const int x0 = (int)flx, y0 = (int)fly;
    const double ax = fx - flx, ay = fy - fly;
    const std::uint8_t* p00 = Texel(t, x0, y0); const std::uint8_t* p10 = Texel(t, x0 + 1, y0);
    const std::uint8_t* p01 = Texel(t, x0, y0 + 1); const std::uint8_t* p11 = Texel(t, x0 + 1, y0 + 1);
    for (int c = 0; c < 4; ++c) {
        const double top = p00[c] + (p10[c] - p00[c]) * ax, bot = p01[c] + (p11[c] - p01[c]) * ax;
        out[c] = (int)(top + (bot - top) * ay + 0.5);
    }
}

// Edge function; `tl` = the edge is a top or left edge (inclusive) under the top-left fill rule.
struct Edge { double a, b, c; bool tl; };
inline Edge MakeEdge(const DrawVertex& p, const DrawVertex& q) {
    Edge e; e.a = (double)p.y - q.y; e.b = (double)q.x - p.x; e.c = (double)p.x * q.y - (double)p.y * q.x;
    e.tl = (e.a > 0) || (e.a == 0 && e.b < 0);
    return e;
}

/// Fill one triangle. `tex` null = flat / vertex colour only. `blend` false = replace (SDL_BLENDMODE_NONE).
/// The vertex colour multiplies the texel, then the texture's colour / alpha modulation.
inline void Triangle(Surface& bm, const Clip& clip, const Tex* tex, bool blend,
                     const DrawVertex& v0, const DrawVertex& v1, const DrawVertex& v2) {
    // Bug fix 1 (kept from the fuzz test of the first rasteriser): a non-finite or absurd coordinate
    // must never reach the float to int conversions below.
    if (!Ok(v0.x) || !Ok(v0.y) || !Ok(v1.x) || !Ok(v1.y) || !Ok(v2.x) || !Ok(v2.y)) return;
    const double area = ((double)v1.x - v0.x) * ((double)v2.y - v0.y) - ((double)v2.x - v0.x) * ((double)v1.y - v0.y);
    if (area == 0) return;
    const DrawVertex* a = &v0; const DrawVertex* b = &v1; const DrawVertex* c = &v2;
    if (area < 0) std::swap(b, c);                    // make the winding consistent
    const double ar = std::fabs(area);
    const float fx0 = std::min({a->x, b->x, c->x}), fx1 = std::max({a->x, b->x, c->x});
    const float fy0 = std::min({a->y, b->y, c->y}), fy1 = std::max({a->y, b->y, c->y});
    // Pixel bounding box (pixel centres at +0.5), intersected with the scissor and the bitmap.
    std::int64_t x0 = (std::int64_t)std::floor(fx0 - 0.5f), y0 = (std::int64_t)std::floor(fy0 - 0.5f);
    std::int64_t x1 = (std::int64_t)std::ceil(fx1 + 0.5f), y1 = (std::int64_t)std::ceil(fy1 + 0.5f);
    x0 = std::max<std::int64_t>(x0, std::max<std::int64_t>(clip.x0, 0)); y0 = std::max<std::int64_t>(y0, std::max<std::int64_t>(clip.y0, 0));
    x1 = std::min<std::int64_t>(x1, std::min<std::int64_t>(clip.x1, bm.w)); y1 = std::min<std::int64_t>(y1, std::min<std::int64_t>(clip.y1, bm.h));
    if (x0 >= x1 || y0 >= y1) return;
    const Edge e0 = MakeEdge(*b, *c), e1 = MakeEdge(*c, *a), e2 = MakeEdge(*a, *b);
    const bool flat = SameColor(*a, *b) && SameColor(*b, *c);
    const bool modded = tex && (tex->modR != 255 || tex->modG != 255 || tex->modB != 255 || tex->modA != 255);
    for (std::int64_t y = y0; y < y1; ++y) {
        const double py = (double)y + 0.5;
        for (std::int64_t x = x0; x < x1; ++x) {
            const double px = (double)x + 0.5;
            const double w0 = e0.a * px + e0.b * py + e0.c, w1 = e1.a * px + e1.b * py + e1.c, w2 = e2.a * px + e2.b * py + e2.c;
            // inside when every weight is positive, or zero on a top/left edge
            if (!(w0 > 0 || (w0 == 0 && e0.tl)) || !(w1 > 0 || (w1 == 0 && e1.tl)) || !(w2 > 0 || (w2 == 0 && e2.tl))) continue;
            const double l0 = w0 / ar, l1 = w1 / ar, l2 = w2 / ar;
            int r, g, bl, al;
            if (flat) { r = a->r; g = a->g; bl = a->b; al = a->a; }
            else {
                r = (int)(l0 * a->r + l1 * b->r + l2 * c->r + 0.5);
                g = (int)(l0 * a->g + l1 * b->g + l2 * c->g + 0.5);
                bl = (int)(l0 * a->b + l1 * b->b + l2 * c->b + 0.5);
                al = (int)(l0 * a->a + l1 * b->a + l2 * c->a + 0.5);
            }
            if (tex && tex->w > 0) {
                int t[4];
                Sample(*tex, l0 * a->u + l1 * b->u + l2 * c->u, l0 * a->v + l1 * b->v + l2 * c->v, t);
                r = Div255(r * t[0]); g = Div255(g * t[1]); bl = Div255(bl * t[2]); al = Div255(al * t[3]);
                if (modded) { r = Div255(r * tex->modR); g = Div255(g * tex->modG); bl = Div255(bl * tex->modB); al = Div255(al * tex->modA); }
            }
            r = Clamp255(r); g = Clamp255(g); bl = Clamp255(bl); al = Clamp255(al);
            if (al == 0 && blend) continue;
            Put(&bm.px[((std::size_t)y * (std::size_t)bm.w + (std::size_t)x) * 4], r, g, bl, al, blend);
        }
    }
}

} // namespace raster
} // namespace soft
} // namespace OkayUI
