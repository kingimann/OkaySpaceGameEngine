#pragma once
// Internal to okay_ui_soft: the pixel routines both the draw-list renderer (OkayUI_Soft.cpp) and the
// software okay::platform::Painter (OkayUI_SoftPainter.cpp) draw with. One implementation of the triangle
// fill rule, texture sampling and source-over blending, so the two cannot drift apart.
#include "okay/UI/OkayUI.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

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

/// A box in texture coordinates whose samples are all one colour (Dear ImGui's solid fills sample the atlas's white pixel at
/// every pixel). Inside it the sample is known without reading texels; outside it Sample() is called as usual. Exact: bilinear
/// filtering of texels that are all equal gives that texel back unchanged (top = p, bottom = p, (int)(p + 0.5) = p), and the box
/// covers every texel Sample() can touch for a coordinate inside it.
struct UniformUv { bool on = false; double u0 = 0, u1 = 0, v0 = 0, v1 = 0; int rgba[4] = {0, 0, 0, 0}; };
inline UniformUv FindUniformUv(const Tex& t, const DrawVertex& a, const DrawVertex& b, const DrawVertex& c) {
    UniformUv r;
    if (t.w <= 0 || t.h <= 0 || !t.rgba) return r;
    const double us[3] = {a.u, b.u, c.u}, vs[3] = {a.v, b.v, c.v};
    for (int i = 0; i < 3; ++i) if (!std::isfinite(us[i]) || !std::isfinite(vs[i]) || std::fabs(us[i]) > 1e3 || std::fabs(vs[i]) > 1e3) return r;
    // a margin for the rounding of the interpolated coordinate; a pixel whose coordinate falls outside the box is sampled normally
    const double mu = 1e-7 * (1.0 + std::max(std::fabs(*std::min_element(us, us + 3)), std::fabs(*std::max_element(us, us + 3))));
    const double mv = 1e-7 * (1.0 + std::max(std::fabs(*std::min_element(vs, vs + 3)), std::fabs(*std::max_element(vs, vs + 3))));
    r.u0 = *std::min_element(us, us + 3) - mu; r.u1 = *std::max_element(us, us + 3) + mu;
    r.v0 = *std::min_element(vs, vs + 3) - mv; r.v1 = *std::max_element(vs, vs + 3) + mv;
    // the texels Sample() can read for a coordinate in the box (nearest: floor(u * w); bilinear: floor(u * w - 0.5) and the next one)
    const double off = t.linear ? 0.5 : 0.0;
    std::int64_t x0 = (std::int64_t)std::floor(r.u0 * t.w - off), x1 = (std::int64_t)std::floor(r.u1 * t.w - off) + (t.linear ? 1 : 0);
    std::int64_t y0 = (std::int64_t)std::floor(r.v0 * t.h - off), y1 = (std::int64_t)std::floor(r.v1 * t.h - off) + (t.linear ? 1 : 0);
    x0 = std::max<std::int64_t>(0, std::min<std::int64_t>(t.w - 1, x0)); x1 = std::max<std::int64_t>(0, std::min<std::int64_t>(t.w - 1, x1));
    y0 = std::max<std::int64_t>(0, std::min<std::int64_t>(t.h - 1, y0)); y1 = std::max<std::int64_t>(0, std::min<std::int64_t>(t.h - 1, y1));
    if ((x1 - x0 + 1) * (y1 - y0 + 1) > 64) return r;      // a large area: not worth checking, sample per pixel
    const std::uint8_t* first = Texel(t, (int)x0, (int)y0);
    for (std::int64_t y = y0; y <= y1; ++y)
        for (std::int64_t x = x0; x <= x1; ++x) {
            const std::uint8_t* p = Texel(t, (int)x, (int)y);
            if (p[0] != first[0] || p[1] != first[1] || p[2] != first[2] || p[3] != first[3]) return r;
        }
    for (int k = 0; k < 4; ++k) r.rgba[k] = first[k];
    r.on = true;
    return r;
}

/// The columns [xs, xe) of row centre `py` that can be inside edge `e` (a superset: every pixel in it is still tested exactly).
/// An edge that is nearly horizontal leaves the range as it is, since its crossing point cannot be placed reliably.
inline void EdgeSpan(const Edge& e, double py, std::int64_t& xs, std::int64_t& xe) {
    if (std::fabs(e.a) < 1e-3) return;
    const double cross = -(e.b * py + e.c) / e.a;           // where a * px + b * py + c = 0
    const double lo = (double)xs, hi = (double)xe;
    // 3 pixels plus a bound on the rounding of both this and the exact test, measured in pixels along the row
    const double margin = 3.0 + 1e-13 * (std::fabs(e.b * py) + std::fabs(e.c) + std::fabs(e.a) * (std::fabs(lo) + std::fabs(hi))) / std::fabs(e.a);
    if (e.a > 0) {                                           // inside to the right of the crossing
        const double s = std::floor(cross - 0.5 - margin);
        if (s > lo) xs = s >= hi ? xe : (std::int64_t)s;
    } else {                                                 // inside to the left of it
        const double t = std::ceil(cross - 0.5 + margin);
        if (t < hi) xe = t <= lo ? xs : (std::int64_t)t;
    }
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
    const bool textured = tex && tex->w > 0;
    const UniformUv uni = textured ? FindUniformUv(*tex, *a, *b, *c) : UniformUv{};
    // One colour for the whole triangle: a flat colour with no texture, or with a texture that is one colour wherever it is sampled
    // (the uniform box above). Worked out once here, the same way the per-pixel code below works it out.
    const bool constCol = flat && (!textured || uni.on);
    int cc[4] = {a->r, a->g, a->b, a->a};
    if (constCol && textured) {
        for (int k = 0; k < 4; ++k) cc[k] = Div255(cc[k] * uni.rgba[k]);
        if (modded) { cc[0] = Div255(cc[0] * tex->modR); cc[1] = Div255(cc[1] * tex->modG); cc[2] = Div255(cc[2] * tex->modB); cc[3] = Div255(cc[3] * tex->modA); }
    }
    for (int k = 0; k < 4; ++k) cc[k] = Clamp255(cc[k]);
    const double invAr = 1.0 / ar;
    const double uvMax = textured ? std::max({std::fabs((double)a->u), std::fabs((double)b->u), std::fabs((double)c->u),
                                              std::fabs((double)a->v), std::fabs((double)b->v), std::fabs((double)c->v)}) : 0.0;
    for (std::int64_t y = y0; y < y1; ++y) {
        const double py = (double)y + 0.5;
        // only the part of the row that can be inside all three edges is visited (the others fail the test below anyway)
        std::int64_t xs = x0, xe = x1;
        EdgeSpan(e0, py, xs, xe); EdgeSpan(e1, py, xs, xe); EdgeSpan(e2, py, xs, xe);
        if (constCol && xs < xe) {
            // Each computed edge value only grows (or only shrinks) along the row, since every rounding step is monotonic, so the
            // pixels inside all three edges are one run: find its two ends with the exact test, then fill between them.
            auto inside = [&](std::int64_t x) {
                const double px = (double)x + 0.5;
                const double w0 = e0.a * px + e0.b * py + e0.c, w1 = e1.a * px + e1.b * py + e1.c, w2 = e2.a * px + e2.b * py + e2.c;
                return (w0 > 0 || (w0 == 0 && e0.tl)) && (w1 > 0 || (w1 == 0 && e1.tl)) && (w2 > 0 || (w2 == 0 && e2.tl));
            };
            std::int64_t xa = xs;
            while (xa < xe && !inside(xa)) ++xa;
            if (xa >= xe) continue;
            std::int64_t xb = xe - 1;
            while (xb > xa && !inside(xb)) --xb;
            bool rowOne = true;
            if (textured) {
                // the texture coordinate is linear along the row: when the estimates at both ends are inside the uniform box by
                // twice the rounding bound, every pixel between is inside it by the bound (see the per-pixel check below)
                for (std::int64_t x : {xa, xb}) {
                    const double px = (double)x + 0.5;
                    const double w0 = e0.a * px + e0.b * py + e0.c, w1 = e1.a * px + e1.b * py + e1.c, w2 = e2.a * px + e2.b * py + e2.c;
                    const double sum = (w0 + w1 + w2) * invAr;
                    const double fu = (w0 * a->u + w1 * b->u + w2 * c->u) * invAr, fv = (w0 * a->v + w1 * b->v + w2 * c->v) * invAr;
                    const double d = 2e-12 * (1.0 + sum) * (1.0 + uvMax);
                    if (!(fu - d >= uni.u0 && fu + d <= uni.u1 && fv - d >= uni.v0 && fv + d <= uni.v1)) rowOne = false;
                }
            }
            if (rowOne) {
                if (cc[3] == 0 && blend) continue;
                std::uint8_t* d = &bm.px[((std::size_t)y * (std::size_t)bm.w + (std::size_t)xa) * 4];
                const std::uint8_t* const end = d + (std::size_t)(xb - xa + 1) * 4;
                if (!blend || cc[3] == 255) {                    // Put's replace case: the same four bytes in every pixel
                    const std::uint8_t px4[4] = {(std::uint8_t)cc[0], (std::uint8_t)cc[1], (std::uint8_t)cc[2], (std::uint8_t)cc[3]};
                    for (; d < end; d += 4) std::memcpy(d, px4, 4);
                } else {                                         // Put's source-over case, written out with its terms worked out once
                    const int ia = 255 - cc[3], sr = cc[0] * cc[3], sg = cc[1] * cc[3], sb = cc[2] * cc[3];
                    for (; d < end; d += 4) {
                        d[0] = (std::uint8_t)Div255(sr + d[0] * ia);
                        d[1] = (std::uint8_t)Div255(sg + d[1] * ia);
                        d[2] = (std::uint8_t)Div255(sb + d[2] * ia);
                        d[3] = (std::uint8_t)std::min(255, cc[3] + Div255(d[3] * ia));
                    }
                }
                continue;
            }
            xs = xa; xe = xb + 1;                                // otherwise pixel by pixel, over the run only
        }
        for (std::int64_t x = xs; x < xe; ++x) {
            const double px = (double)x + 0.5;
            const double w0 = e0.a * px + e0.b * py + e0.c, w1 = e1.a * px + e1.b * py + e1.c, w2 = e2.a * px + e2.b * py + e2.c;
            // inside when every weight is positive, or zero on a top/left edge
            if (!(w0 > 0 || (w0 == 0 && e0.tl)) || !(w1 > 0 || (w1 == 0 && e1.tl)) || !(w2 > 0 || (w2 == 0 && e2.tl))) continue;
            if (constCol) {
                // With a texture, the pixel takes the one colour only when its coordinate is surely inside the uniform box: a cheaper
                // estimate of it, kept a rounding bound `d` away from the box's edges (the exact one differs by far less than d).
                bool one = true;
                if (textured) {
                    const double sum = (w0 + w1 + w2) * invAr;
                    const double fu = (w0 * a->u + w1 * b->u + w2 * c->u) * invAr, fv = (w0 * a->v + w1 * b->v + w2 * c->v) * invAr;
                    const double d = 1e-12 * (1.0 + sum) * (1.0 + uvMax);
                    one = fu - d >= uni.u0 && fu + d <= uni.u1 && fv - d >= uni.v0 && fv + d <= uni.v1;
                }
                if (one) {
                    if (cc[3] == 0 && blend) continue;
                    Put(&bm.px[((std::size_t)y * (std::size_t)bm.w + (std::size_t)x) * 4], cc[0], cc[1], cc[2], cc[3], blend);
                    continue;
                }
            }
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
                const double su = l0 * a->u + l1 * b->u + l2 * c->u, sv = l0 * a->v + l1 * b->v + l2 * c->v;
                if (uni.on && su >= uni.u0 && su <= uni.u1 && sv >= uni.v0 && sv <= uni.v1) { t[0] = uni.rgba[0]; t[1] = uni.rgba[1]; t[2] = uni.rgba[2]; t[3] = uni.rgba[3]; }
                else Sample(*tex, su, sv, t);
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
