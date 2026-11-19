#pragma once
// The geometry half of the Direct3D 11 painter (D3D11Painter.cpp), with no Direct3D in it so it is tested on any machine
// (tests/test_d3d11_batch.cpp): every Painter drawing call becomes triangles in device pixels (top-left origin, y down), the way a GPU
// painter must draw them. Written from the Painter interface's documented behaviour (scale, clip in the scaled space, source / destination
// rectangles, rotation about a centre in degrees, flips) and plain 2D geometry; not from SDL or any other renderer's code.
#include "okay/UI/UIPainter.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace okay::platform::d3d {

/// One vertex as the shader reads it: pixel position, colour (R, G, B, A bytes) and texture coordinate (0..1).
struct Vtx {
    float x = 0, y = 0;
    std::uint8_t r = 255, g = 255, b = 255, a = 255;
    float u = 0, v = 0;
};

inline int RoundI(double v) {   // half up, pinned so no int conversion overflows
    if (!(v == v)) return 0;
    v = std::floor(v + 0.5);
    return (int)std::max(-1e8, std::min(1e8, v));
}

/// A device rectangle [x0, x1) x [y0, y1).
struct DevRect { int x0 = 0, y0 = 0, x1 = 0, y1 = 0; bool Empty() const { return x1 <= x0 || y1 <= y0; } };

/// The logical rectangle scaled to device pixels and rounded (what FillRect covers).
inline DevRect ScaleRect(const okay::ui2d::Rect& r, float sx, float sy) {
    return DevRect{RoundI(r.x * (double)sx), RoundI(r.y * (double)sy), RoundI((r.x + (double)r.w) * sx), RoundI((r.y + (double)r.h) * sy)};
}
/// A float rectangle: a pixel is covered when its centre is inside (the first pixel whose centre is at or past the edge).
inline DevRect ScaleRectF(const okay::ui2d::FRect& r, float sx, float sy) {
    auto c = [](double v) { return (int)std::max(-1e8, std::min(1e8, std::ceil(v - 0.5))); };
    return DevRect{c(r.x * (double)sx), c(r.y * (double)sy), c((r.x + (double)r.w) * sx), c((r.y + (double)r.h) * sy)};
}

/// The scissor rectangle for a clip: the scaled clip rect cut to the target, or the whole target when clipping is off. An empty clip rect clips
/// everything away (an empty result), which is what a widget scrolled fully out of its view needs.
inline DevRect Scissor(bool clipOn, const okay::ui2d::Rect& clip, float sx, float sy, int targetW, int targetH) {
    DevRect t{0, 0, targetW, targetH};
    if (!clipOn) return t;
    if (clip.w <= 0 || clip.h <= 0) return DevRect{0, 0, 0, 0};
    const DevRect c = ScaleRect(clip, sx, sy);
    return DevRect{std::max(t.x0, c.x0), std::max(t.y0, c.y0), std::min(t.x1, c.x1), std::min(t.y1, c.y1)};
}

/// Two triangles (six vertices) for a device rectangle, one colour, texture coordinates (u0, v0)-(u1, v1).
inline void Quad(std::vector<Vtx>& out, float x0, float y0, float x1, float y1, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a,
                 float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1) {
    const Vtx tl{x0, y0, r, g, b, a, u0, v0}, tr{x1, y0, r, g, b, a, u1, v0}, br{x1, y1, r, g, b, a, u1, v1}, bl{x0, y1, r, g, b, a, u0, v1};
    out.push_back(tl); out.push_back(tr); out.push_back(br);
    out.push_back(tl); out.push_back(br); out.push_back(bl);
}

/// A solid rectangle.
inline void FillDev(std::vector<Vtx>& out, const DevRect& d, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    if (d.Empty()) return;
    Quad(out, (float)d.x0, (float)d.y0, (float)d.x1, (float)d.y1, r, g, b, a);
}

/// A rectangle outline one device pixel thick (DrawRect): top, bottom, then the two sides between them, so no corner is drawn twice.
inline void OutlineDev(std::vector<Vtx>& out, const DevRect& d, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    if (d.Empty()) return;
    const int x1 = std::max(d.x0 + 1, d.x1), y1 = std::max(d.y0 + 1, d.y1);
    FillDev(out, DevRect{d.x0, d.y0, x1, d.y0 + 1}, r, g, b, a);
    if (y1 - 1 > d.y0) FillDev(out, DevRect{d.x0, y1 - 1, x1, y1}, r, g, b, a);
    if (y1 - d.y0 > 2) {
        FillDev(out, DevRect{d.x0, d.y0 + 1, d.x0 + 1, y1 - 1}, r, g, b, a);
        if (x1 - 1 > d.x0) FillDev(out, DevRect{x1 - 1, d.y0 + 1, x1, y1 - 1}, r, g, b, a);
    }
}

/// A line from the centre of pixel (x0, y0) to the centre of pixel (x1, y1), `width` device pixels wide (a quad around the segment).
/// A GPU draws it as triangles, so this is an approximation of a one-pixel Bresenham line (the software painter's exact rule).
inline void LineQuad(std::vector<Vtx>& out, double x0, double y0, double x1, double y1, double width, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    double dx = x1 - x0, dy = y1 - y0;
    const double len = std::sqrt(dx * dx + dy * dy);
    if (!(len == len) || width <= 0) return;
    double nx, ny;
    if (len < 1e-9) { nx = 0; ny = 1; dx = 1; dy = 0; } else { nx = -dy / len; ny = dx / len; dx /= len; dy /= len; }
    const double h = width * 0.5;
    // extend by half a pixel along the line so both end pixels are covered
    const double ex = dx * 0.5, ey = dy * 0.5;
    const Vtx p0{(float)(x0 - ex + nx * h), (float)(y0 - ey + ny * h), r, g, b, a, 0, 0}, p1{(float)(x1 + ex + nx * h), (float)(y1 + ey + ny * h), r, g, b, a, 0, 0};
    const Vtx p2{(float)(x1 + ex - nx * h), (float)(y1 + ey - ny * h), r, g, b, a, 0, 0}, p3{(float)(x0 - ex - nx * h), (float)(y0 - ey - ny * h), r, g, b, a, 0, 0};
    out.push_back(p0); out.push_back(p1); out.push_back(p2);
    out.push_back(p0); out.push_back(p2); out.push_back(p3);
}

/// A textured quad: the source rectangle `src` (texels; null = the whole texture) drawn into the logical destination rectangle (dx, dy, dw, dh),
/// scaled by (sx, sy), optionally rotated `angleDeg` clockwise about `centre` (relative to the destination's top-left, logical units; null = its middle)
/// and flipped (ui2d::FlipH, FlipV). The colour multiplies the texture. False when nothing can be drawn (a bad rectangle).
inline bool TexQuad(std::vector<Vtx>& out, int texW, int texH, const okay::ui2d::Rect* src, double dx, double dy, double dw, double dh, float sx, float sy,
                    double angleDeg, const okay::ui2d::FPoint* centre, int flip, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    if (texW <= 0 || texH <= 0 || !(dw > 0) || !(dh > 0) || !std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(dw) || !std::isfinite(dh)) return false;
    okay::ui2d::Rect sr{0, 0, texW, texH};
    if (src) sr = *src;
    if (sr.x < 0 || sr.y < 0 || sr.w <= 0 || sr.h <= 0 || sr.x + (long long)sr.w > texW || sr.y + (long long)sr.h > texH) return false;
    float u0 = (float)sr.x / (float)texW, u1 = (float)(sr.x + sr.w) / (float)texW;
    float v0 = (float)sr.y / (float)texH, v1 = (float)(sr.y + sr.h) / (float)texH;
    if (flip & okay::ui2d::FlipH) std::swap(u0, u1);
    if (flip & okay::ui2d::FlipV) std::swap(v0, v1);
    const double X = dx * sx, Y = dy * sy, W = dw * sx, H = dh * sy;
    double px[4] = {X, X + W, X + W, X}, py[4] = {Y, Y, Y + H, Y + H};
    const bool rotated = angleDeg != 0.0 && std::isfinite(angleDeg) && std::fmod(angleDeg, 360.0) != 0.0;
    if (rotated) {
        const double cx = X + (centre ? centre->x * (double)sx : W * 0.5), cy = Y + (centre ? centre->y * (double)sy : H * 0.5);
        const double rad = angleDeg * 3.14159265358979323846 / 180.0, cs = std::cos(rad), sn = std::sin(rad);
        for (int i = 0; i < 4; ++i) {
            const double rx = cx + (px[i] - cx) * cs - (py[i] - cy) * sn, ry = cy + (px[i] - cx) * sn + (py[i] - cy) * cs;
            px[i] = rx; py[i] = ry;
        }
    }
    const Vtx tl{(float)px[0], (float)py[0], r, g, b, a, u0, v0}, tr{(float)px[1], (float)py[1], r, g, b, a, u1, v0};
    const Vtx br{(float)px[2], (float)py[2], r, g, b, a, u1, v1}, bl{(float)px[3], (float)py[3], r, g, b, a, u0, v1};
    out.push_back(tl); out.push_back(tr); out.push_back(br);
    out.push_back(tl); out.push_back(br); out.push_back(bl);
    return true;
}

/// Triangles from a vertex list (and optional indices), positions scaled, colours multiplied by the texture's colour / alpha modulation. Bad indices
/// and a count that is not a multiple of three are skipped, like the other painters.
inline void GeometryVerts(std::vector<Vtx>& out, const okay::ui2d::Vertex* v, int nv, const int* idx, int ni, float sx, float sy,
                          std::uint8_t mr, std::uint8_t mg, std::uint8_t mb, std::uint8_t ma) {
    if (!v || nv <= 0) return;
    const int n = idx ? ni : nv;
    if (n < 3) return;
    auto mul = [](std::uint8_t c, std::uint8_t m) { return (std::uint8_t)((c * (int)m + 127) / 255); };
    auto make = [&](int i) {
        Vtx o;
        o.x = v[i].position.x * sx; o.y = v[i].position.y * sy;
        o.r = mul(v[i].color.r, mr); o.g = mul(v[i].color.g, mg); o.b = mul(v[i].color.b, mb); o.a = mul(v[i].color.a, ma);
        o.u = v[i].tex_coord.x; o.v = v[i].tex_coord.y;
        return o;
    };
    for (int i = 0; i + 3 <= n; i += 3) {
        const int a = idx ? idx[i] : i, b = idx ? idx[i + 1] : i + 1, c = idx ? idx[i + 2] : i + 2;
        if (a < 0 || b < 0 || c < 0 || a >= nv || b >= nv || c >= nv) continue;
        out.push_back(make(a)); out.push_back(make(b)); out.push_back(make(c));
    }
}

/// Pixel position to clip space for a target of w x h pixels (what the vertex shader does): x -> 2x/w - 1, y -> 1 - 2y/h.
inline void ToClip(float x, float y, int w, int h, float& cx, float& cy) { cx = 2.0f * x / (float)w - 1.0f; cy = 1.0f - 2.0f * y / (float)h; }

} // namespace okay::platform::d3d
