#pragma once
// A small software rasteriser for the Xbox 2D / UI layer. PURE C++17 (no engine, no Windows): draws into a PREMULTIPLIED-alpha RGBA8 surface that
// the app uploads as a Direct3D 11 texture and composites over the 3D frame with One / InvSrcAlpha. Written from the C++ standard and the general
// rules of source-over alpha compositing, barycentric triangle rasterisation and bilinear sampling (no code from any other project).
// The surface starts transparent; everything is blended source-over. Tested on Linux. UNVERIFIED on a console.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace okay::xbox {

struct IRect { int x = 0, y = 0, w = 0, h = 0; bool Empty() const { return w <= 0 || h <= 0; } };

inline IRect Intersect(const IRect& a, const IRect& b) {
    const int x0 = std::max(a.x, b.x), y0 = std::max(a.y, b.y), x1 = std::min(a.x + a.w, b.x + b.w), y1 = std::min(a.y + a.h, b.y + b.h);
    return (x1 > x0 && y1 > y0) ? IRect{x0, y0, x1 - x0, y1 - y0} : IRect{};
}
inline IRect Union(const IRect& a, const IRect& b) {
    if (a.Empty()) return b;
    if (b.Empty()) return a;
    const int x0 = std::min(a.x, b.x), y0 = std::min(a.y, b.y), x1 = std::max(a.x + a.w, b.x + b.w), y1 = std::max(a.y + a.h, b.y + b.h);
    return {x0, y0, x1 - x0, y1 - y0};
}

/// Straight RGBA8 (R,G,B,A bytes, as image files give them) to premultiplied, in place. Rounds to nearest.
inline void PremultiplyRgba(std::uint8_t* px, std::size_t pixels) {
    for (std::size_t i = 0; i < pixels; ++i, px += 4) {
        const unsigned a = px[3];
        if (a == 255) continue;
        px[0] = (std::uint8_t)((px[0] * a + 127) / 255); px[1] = (std::uint8_t)((px[1] * a + 127) / 255); px[2] = (std::uint8_t)((px[2] * a + 127) / 255);
    }
}

/// A texture the painter can draw from: PREMULTIPLIED RGBA8.
struct SoftTexture {
    int w = 0, h = 0;
    std::vector<std::uint8_t> px;     // premultiplied
    std::uint8_t modR = 255, modG = 255, modB = 255, modA = 255;
    bool blend = true, linear = false;
};

struct Rgba { float r = 0, g = 0, b = 0, a = 0; };   // premultiplied, 0..1

/// Sample at texel-space (u, v) inside the sub-rectangle `sub` of the texture (clamped to it). Nearest or bilinear.
inline Rgba SampleTexture(const SoftTexture& t, const IRect& sub, float u, float v, bool linear) {
    auto at = [&](int x, int y) {
        x = std::clamp(x, sub.x, sub.x + sub.w - 1); y = std::clamp(y, sub.y, sub.y + sub.h - 1);
        const std::uint8_t* p = &t.px[((std::size_t)y * (std::size_t)t.w + (std::size_t)x) * 4];
        return Rgba{p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f, p[3] / 255.0f};
    };
    if (!linear) return at((int)std::floor(u), (int)std::floor(v));
    const float fu = u - 0.5f, fv = v - 0.5f; const int x0 = (int)std::floor(fu), y0 = (int)std::floor(fv);
    const float tx = fu - (float)x0, ty = fv - (float)y0;
    const Rgba a = at(x0, y0), b = at(x0 + 1, y0), c = at(x0, y0 + 1), d = at(x0 + 1, y0 + 1);
    auto mix = [&](float p, float q, float r, float s) { return (p * (1 - tx) + q * tx) * (1 - ty) + (r * (1 - tx) + s * tx) * ty; };
    return {mix(a.r, b.r, c.r, d.r), mix(a.g, b.g, c.g, d.g), mix(a.b, b.b, c.b, d.b), mix(a.a, b.a, c.a, d.a)};
}

class SoftSurface {
public:
    SoftSurface() = default;
    SoftSurface(int w, int h) { Resize(w, h); }
    void Resize(int w, int h) { w_ = w > 0 ? w : 0; h_ = h > 0 ? h : 0; px_.assign((std::size_t)w_ * (std::size_t)h_ * 4, 0); dirty_ = IRect{}; }
    int Width() const { return w_; }
    int Height() const { return h_; }
    const std::uint8_t* Data() const { return px_.data(); }
    std::uint8_t* Data() { return px_.data(); }
    std::size_t Bytes() const { return px_.size(); }
    /// What changed since the last TakeDirty() (union of everything drawn).
    IRect TakeDirty() { const IRect d = dirty_; dirty_ = IRect{}; return d; }
    void MarkAll() { dirty_ = IRect{0, 0, w_, h_}; }
    void Clear() { std::fill(px_.begin(), px_.end(), (std::uint8_t)0); MarkAll(); }

    /// Source-over of one premultiplied source pixel (0..1 floats) onto (x, y). `blend` false replaces the pixel.
    void Put(int x, int y, const Rgba& s, bool blend = true) {
        if (x < 0 || y < 0 || x >= w_ || y >= h_) return;
        std::uint8_t* d = &px_[((std::size_t)y * (std::size_t)w_ + (std::size_t)x) * 4];
        auto q = [](float v) { return (std::uint8_t)std::clamp((int)std::lround(v * 255.0f), 0, 255); };
        if (!blend) { d[0] = q(s.r); d[1] = q(s.g); d[2] = q(s.b); d[3] = q(s.a); return; }
        const float k = 1.0f - s.a;
        d[0] = q(s.r + d[0] / 255.0f * k); d[1] = q(s.g + d[1] / 255.0f * k); d[2] = q(s.b + d[2] / 255.0f * k); d[3] = q(s.a + d[3] / 255.0f * k);
    }
    void Touch(const IRect& r) { dirty_ = Union(dirty_, Intersect(r, IRect{0, 0, w_, h_})); }

    /// Straight-colour fill: r,g,b,a bytes (straight), converted to premultiplied here.
    void FillRect(const IRect& r, std::uint8_t cr, std::uint8_t cg, std::uint8_t cb, std::uint8_t ca, bool blend, const IRect& clip) {
        const IRect c = Intersect(Intersect(r, IRect{0, 0, w_, h_}), clip);
        if (c.Empty()) return;
        const float a = ca / 255.0f; const Rgba s{cr / 255.0f * a, cg / 255.0f * a, cb / 255.0f * a, a};
        for (int y = c.y; y < c.y + c.h; ++y) for (int x = c.x; x < c.x + c.w; ++x) Put(x, y, s, blend);
        Touch(c);
    }
    void Point(int x, int y, std::uint8_t cr, std::uint8_t cg, std::uint8_t cb, std::uint8_t ca, bool blend, const IRect& clip) { FillRect(IRect{x, y, 1, 1}, cr, cg, cb, ca, blend, clip); }
    void Line(int x0, int y0, int x1, int y1, std::uint8_t cr, std::uint8_t cg, std::uint8_t cb, std::uint8_t ca, bool blend, const IRect& clip) {   // Bresenham
        int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
        for (int guard = 0; guard < 100000; ++guard) {
            Point(x0, y0, cr, cg, cb, ca, blend, clip);
            if (x0 == x1 && y0 == y1) break;
            const int e2 = 2 * err; if (e2 >= dy) { err += dy; x0 += sx; } if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }
    /// Draw (a part of) a texture into the float rectangle (dx, dy, dw, dh) of the surface, rotated by `angleDeg` about (cx, cy) (surface space), with flips.
    void Blit(const SoftTexture& t, const IRect& src, float dx, float dy, float dw, float dh, double angleDeg, float cx, float cy, bool flipH, bool flipV, const IRect& clip) {
        IRect sub = src.Empty() ? IRect{0, 0, t.w, t.h} : Intersect(src, IRect{0, 0, t.w, t.h});
        if (sub.Empty() || dw <= 0 || dh <= 0 || t.px.size() != (std::size_t)t.w * (std::size_t)t.h * 4) return;
        const double rad = angleDeg * 3.14159265358979323846 / 180.0, cs = std::cos(rad), sn = std::sin(rad);
        // bounding box of the rotated rectangle
        float xs[4] = {dx, dx + dw, dx, dx + dw}, ys[4] = {dy, dy, dy + dh, dy + dh}, minx = 1e30f, miny = 1e30f, maxx = -1e30f, maxy = -1e30f;
        for (int i = 0; i < 4; ++i) { const double px = xs[i] - cx, py = ys[i] - cy; const float rx = (float)(px * cs - py * sn + cx), ry = (float)(px * sn + py * cs + cy); minx = std::min(minx, rx); maxx = std::max(maxx, rx); miny = std::min(miny, ry); maxy = std::max(maxy, ry); }
        const IRect box = Intersect(Intersect(IRect{(int)std::floor(minx), (int)std::floor(miny), (int)std::ceil(maxx) - (int)std::floor(minx), (int)std::ceil(maxy) - (int)std::floor(miny)}, IRect{0, 0, w_, h_}), clip);
        if (box.Empty()) return;
        const float mr = t.modR / 255.0f, mg = t.modG / 255.0f, mb = t.modB / 255.0f, ma = t.modA / 255.0f;
        for (int y = box.y; y < box.y + box.h; ++y) for (int x = box.x; x < box.x + box.w; ++x) {
            const double px = (x + 0.5) - cx, py = (y + 0.5) - cy;   // inverse rotation into the unrotated rectangle
            const float u = (float)(px * cs + py * sn + cx), v = (float)(-px * sn + py * cs + cy);
            if (u < dx || v < dy || u >= dx + dw || v >= dy + dh) continue;
            float fu = (u - dx) / dw, fv = (v - dy) / dh; if (flipH) fu = 1 - fu; if (flipV) fv = 1 - fv;
            Rgba s = SampleTexture(t, sub, sub.x + fu * (float)sub.w, sub.y + fv * (float)sub.h, t.linear);
            s.r *= mr * ma; s.g *= mg * ma; s.b *= mb * ma; s.a *= ma;   // colour mod, then alpha mod (premultiplied: alpha scales everything)
            Put(x, y, s, t.blend);
        }
        Touch(box);
    }
    struct Vtx { float x, y; std::uint8_t r, g, b, a; float u, v; };   // straight colour, texel-space uv
    /// Triangles with per-vertex colour (and an optional texture: texel = colour * sample).
    void Triangles(const SoftTexture* t, const Vtx* vs, int nv, const int* idx, int ni, const IRect& clip) {
        for (int i = 0; i + 2 < ni || (!idx && i + 2 < nv); i += 3) {
            const Vtx& a = vs[idx ? idx[i] : i]; const Vtx& b = vs[idx ? idx[i + 1] : i + 1]; const Vtx& c = vs[idx ? idx[i + 2] : i + 2];
            const float area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
            if (std::fabs(area) < 1e-6f) continue;
            const IRect box = Intersect(Intersect(IRect{(int)std::floor(std::min({a.x, b.x, c.x})), (int)std::floor(std::min({a.y, b.y, c.y})),
                (int)std::ceil(std::max({a.x, b.x, c.x})) - (int)std::floor(std::min({a.x, b.x, c.x})), (int)std::ceil(std::max({a.y, b.y, c.y})) - (int)std::floor(std::min({a.y, b.y, c.y}))}, IRect{0, 0, w_, h_}), clip);
            for (int y = box.y; y < box.y + box.h; ++y) for (int x = box.x; x < box.x + box.w; ++x) {
                const float px = x + 0.5f, py = y + 0.5f;
                const float w0 = ((b.x - px) * (c.y - py) - (c.x - px) * (b.y - py)) / area, w1 = ((c.x - px) * (a.y - py) - (a.x - px) * (c.y - py)) / area, w2 = 1.0f - w0 - w1;
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                const float cr = (a.r * w0 + b.r * w1 + c.r * w2) / 255.0f, cg = (a.g * w0 + b.g * w1 + c.g * w2) / 255.0f, cb = (a.b * w0 + b.b * w1 + c.b * w2) / 255.0f, ca = (a.a * w0 + b.a * w1 + c.a * w2) / 255.0f;
                Rgba s{cr * ca, cg * ca, cb * ca, ca};   // straight vertex colour to premultiplied
                if (t && t->w > 0) {
                    const Rgba tex = SampleTexture(*t, IRect{0, 0, t->w, t->h}, a.u * w0 + b.u * w1 + c.u * w2, a.v * w0 + b.v * w1 + c.v * w2, t->linear);
                    s = {s.r * tex.r, s.g * tex.g, s.b * tex.b, s.a * tex.a};
                }
                Put(x, y, s, true);
            }
            Touch(box);
        }
    }
private:
    int w_ = 0, h_ = 0; std::vector<std::uint8_t> px_; IRect dirty_;
};

} // namespace okay::xbox
