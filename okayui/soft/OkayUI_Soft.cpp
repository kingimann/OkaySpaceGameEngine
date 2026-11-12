// OkayUI software backend. See OkayUI_Soft.hpp.
#include "OkayUI_Soft.hpp"

#include <algorithm>
#include <cmath>

namespace OkayUI {
namespace soft {

void Bitmap::Clear(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) { rgba[i] = r; rgba[i + 1] = g; rgba[i + 2] = b; rgba[i + 3] = a; }
}

namespace {

struct Tex { int w = 0, h = 0; const std::uint8_t* rgba = nullptr; };
struct Clip { std::int64_t x0, y0, x1, y1; };   // pixels, [x0, x1) x [y0, y1)

bool Ok(float f) { return std::isfinite(f) && std::fabs(f) < 1e6f; }
inline int Div255(int v) { return (v + 127) / 255; }
inline bool SameColor(const DrawVertex& a, const DrawVertex& b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }

// Edge function; `tl` = the edge is a top or left edge (inclusive) under the top-left fill rule.
struct Edge { double a, b, c; bool tl; };
Edge MakeEdge(const DrawVertex& p, const DrawVertex& q) {
    Edge e; e.a = (double)p.y - q.y; e.b = (double)q.x - p.x; e.c = (double)p.x * q.y - (double)p.y * q.x;
    e.tl = (e.a > 0) || (e.a == 0 && e.b < 0);
    return e;
}

void Triangle(Bitmap& bm, const Clip& clip, const Tex& tex, const DrawVertex& v0, const DrawVertex& v1, const DrawVertex& v2) {
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
            if (tex.w > 0) {
                double u = l0 * a->u + l1 * b->u + l2 * c->u, v = l0 * a->v + l1 * b->v + l2 * c->v;
                // Bug fix 2: a NaN or infinite texture coordinate is clamped before the int conversion.
                if (!(u == u)) u = 0;
                if (!(v == v)) v = 0;
                u = std::max(-1e4, std::min(1e4, u)); v = std::max(-1e4, std::min(1e4, v));
                int tx = (int)std::floor(u * tex.w), ty = (int)std::floor(v * tex.h);
                tx = std::max(0, std::min(tex.w - 1, tx)); ty = std::max(0, std::min(tex.h - 1, ty));
                const std::uint8_t* t = tex.rgba + ((std::size_t)ty * (std::size_t)tex.w + (std::size_t)tx) * 4;
                r = Div255(r * t[0]); g = Div255(g * t[1]); bl = Div255(bl * t[2]); al = Div255(al * t[3]);
            }
            r = std::max(0, std::min(255, r)); g = std::max(0, std::min(255, g)); bl = std::max(0, std::min(255, bl)); al = std::max(0, std::min(255, al));
            if (al == 0) continue;
            std::uint8_t* d = &bm.rgba[((std::size_t)y * (std::size_t)bm.w + (std::size_t)x) * 4];
            if (al == 255) { d[0] = (std::uint8_t)r; d[1] = (std::uint8_t)g; d[2] = (std::uint8_t)bl; d[3] = 255; }
            else {
                d[0] = (std::uint8_t)Div255(r * al + d[0] * (255 - al));
                d[1] = (std::uint8_t)Div255(g * al + d[1] * (255 - al));
                d[2] = (std::uint8_t)Div255(bl * al + d[2] * (255 - al));
                d[3] = (std::uint8_t)std::min(255, al + Div255(d[3] * (255 - al)));
            }
        }
    }
}

} // namespace

void Render(const DrawData& data, Bitmap& bm) {
    if (bm.w <= 0 || bm.h <= 0 || bm.rgba.size() != (std::size_t)bm.w * (std::size_t)bm.h * 4) return;
    if (!data.vertices || !data.indices || data.vertexCount <= 0 || data.indexCount <= 0) return;
    if (data.vertexStride != (int)sizeof(DrawVertex)) return;   // not our vertex layout
    const DrawVertex* vtx = (const DrawVertex*)data.vertices;
    for (int ci = 0; ci < data.cmdCount && data.cmds; ++ci) {
        const DrawCmd& cmd = data.cmds[ci];
        Clip clip{ 0, 0, bm.w, bm.h };
        if (cmd.clipOn) {   // a garbage clip rectangle is clamped in 64-bit maths, never trusted
            clip.x0 = cmd.clipX; clip.y0 = cmd.clipY;
            clip.x1 = (std::int64_t)cmd.clipX + std::max(0, cmd.clipW); clip.y1 = (std::int64_t)cmd.clipY + std::max(0, cmd.clipH);
        }
        Tex tex;
        if (cmd.texture) {
            if (cmd.texture == data.atlasTexture && data.atlasRgba && data.atlasW > 0 && data.atlasH > 0) { tex.w = data.atlasW; tex.h = data.atlasH; tex.rgba = data.atlasRgba; }
            else continue;      // a GPU texture we cannot see
        }
        if (cmd.idxOffset < 0 || cmd.idxCount <= 0) continue;
        const std::int64_t end = std::min<std::int64_t>(data.indexCount, (std::int64_t)cmd.idxOffset + cmd.idxCount);
        for (std::int64_t i = cmd.idxOffset; i + 3 <= end; i += 3) {
            const int a = data.indices[i], b = data.indices[i + 1], c = data.indices[i + 2];
            if (a < 0 || b < 0 || c < 0 || a >= data.vertexCount || b >= data.vertexCount || c >= data.vertexCount) continue;
            Triangle(bm, clip, tex, vtx[a], vtx[b], vtx[c]);
        }
    }
}

} // namespace soft
} // namespace OkayUI
