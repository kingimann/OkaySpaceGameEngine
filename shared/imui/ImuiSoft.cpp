// okay::imui software backend. See ImuiSoft.hpp.
#include "imui/ImuiSoft.hpp"

#include <algorithm>
#include <cmath>

namespace okay {
namespace imui {

void Bitmap::Clear(Col c) {
    for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
        rgba[i] = (std::uint8_t)ColR(c); rgba[i + 1] = (std::uint8_t)ColG(c); rgba[i + 2] = (std::uint8_t)ColB(c); rgba[i + 3] = (std::uint8_t)ColA(c);
    }
}

namespace {

struct Tex { int w = 0, h = 0; const std::uint8_t* rgba = nullptr; const std::uint8_t* alpha = nullptr; };

bool Ok(float f) { return std::isfinite(f) && std::fabs(f) < 1e6f; }

inline int Div255(int v) { return (v + 127) / 255; }

// Edge function; `tl` = the edge is a top or left edge (inclusive) under the top-left fill rule.
struct Edge { double a, b, c; bool tl; };
Edge MakeEdge(Vec2 p, Vec2 q) {
    Edge e; e.a = (double)p.y - q.y; e.b = (double)q.x - p.x; e.c = (double)p.x * q.y - (double)p.y * q.x;
    e.tl = (e.a > 0) || (e.a == 0 && e.b < 0);       // for counter-clockwise (in y-up sense) after normalising
    return e;
}

void Triangle(Bitmap& bm, const Rect& clip, const Tex& tex, const Vertex& v0, const Vertex& v1, const Vertex& v2) {
    if (!Ok(v0.pos.x) || !Ok(v0.pos.y) || !Ok(v1.pos.x) || !Ok(v1.pos.y) || !Ok(v2.pos.x) || !Ok(v2.pos.y)) return;
    const double area = ((double)v1.pos.x - v0.pos.x) * ((double)v2.pos.y - v0.pos.y) - ((double)v2.pos.x - v0.pos.x) * ((double)v1.pos.y - v0.pos.y);
    if (area == 0) return;
    const Vertex* a = &v0; const Vertex* b = &v1; const Vertex* c = &v2;
    if (area < 0) std::swap(b, c);                    // make the winding consistent
    const double ar = std::fabs(area);
    // pixel bounding box (pixel centres at +0.5)
    float fx0 = std::min({a->pos.x, b->pos.x, c->pos.x}), fx1 = std::max({a->pos.x, b->pos.x, c->pos.x});
    float fy0 = std::min({a->pos.y, b->pos.y, c->pos.y}), fy1 = std::max({a->pos.y, b->pos.y, c->pos.y});
    int x0 = (int)std::floor(std::max(fx0 - 0.5f, std::max(clip.min.x, 0.0f)));
    int y0 = (int)std::floor(std::max(fy0 - 0.5f, std::max(clip.min.y, 0.0f)));
    int x1 = (int)std::ceil(std::min(fx1 + 0.5f, std::min(clip.max.x, (float)bm.w)));
    int y1 = (int)std::ceil(std::min(fy1 + 0.5f, std::min(clip.max.y, (float)bm.h)));
    x0 = std::max(x0, 0); y0 = std::max(y0, 0); x1 = std::min(x1, bm.w); y1 = std::min(y1, bm.h);
    if (x0 >= x1 || y0 >= y1) return;
    const Edge e0 = MakeEdge(b->pos, c->pos), e1 = MakeEdge(c->pos, a->pos), e2 = MakeEdge(a->pos, b->pos);
    const bool flat = a->col == b->col && b->col == c->col;
    for (int y = y0; y < y1; ++y) {
        const double py = y + 0.5;
        if (py < clip.min.y || py >= clip.max.y) continue;
        for (int x = x0; x < x1; ++x) {
            const double px = x + 0.5;
            if (px < clip.min.x || px >= clip.max.x) continue;
            const double w0 = e0.a * px + e0.b * py + e0.c, w1 = e1.a * px + e1.b * py + e1.c, w2 = e2.a * px + e2.b * py + e2.c;
            // inside when every weight is positive, or zero on a top/left edge
            if (!(w0 > 0 || (w0 == 0 && e0.tl)) || !(w1 > 0 || (w1 == 0 && e1.tl)) || !(w2 > 0 || (w2 == 0 && e2.tl))) continue;
            const double l0 = w0 / ar, l1 = w1 / ar, l2 = w2 / ar;
            int r, g, bl, al;
            if (flat) { r = ColR(a->col); g = ColG(a->col); bl = ColB(a->col); al = ColA(a->col); }
            else {
                r = (int)(l0 * ColR(a->col) + l1 * ColR(b->col) + l2 * ColR(c->col) + 0.5);
                g = (int)(l0 * ColG(a->col) + l1 * ColG(b->col) + l2 * ColG(c->col) + 0.5);
                bl = (int)(l0 * ColB(a->col) + l1 * ColB(b->col) + l2 * ColB(c->col) + 0.5);
                al = (int)(l0 * ColA(a->col) + l1 * ColA(b->col) + l2 * ColA(c->col) + 0.5);
            }
            if (tex.w > 0) {
                double u = l0 * a->uv.x + l1 * b->uv.x + l2 * c->uv.x, v = l0 * a->uv.y + l1 * b->uv.y + l2 * c->uv.y;
                if (!(u == u)) u = 0;
                if (!(v == v)) v = 0;
                u = std::max(-1e4, std::min(1e4, u)); v = std::max(-1e4, std::min(1e4, v));
                int tx = (int)std::floor(u * tex.w), ty = (int)std::floor(v * tex.h);
                tx = std::max(0, std::min(tex.w - 1, tx)); ty = std::max(0, std::min(tex.h - 1, ty));
                const std::size_t ti = (std::size_t)ty * tex.w + tx;
                if (tex.alpha) al = Div255(al * tex.alpha[ti]);
                else if (tex.rgba) {
                    const std::uint8_t* t = tex.rgba + ti * 4;
                    r = Div255(r * t[0]); g = Div255(g * t[1]); bl = Div255(bl * t[2]); al = Div255(al * t[3]);
                }
            }
            r = std::max(0, std::min(255, r)); g = std::max(0, std::min(255, g)); bl = std::max(0, std::min(255, bl)); al = std::max(0, std::min(255, al));
            if (al == 0) continue;
            std::uint8_t* d = &bm.rgba[((std::size_t)y * bm.w + x) * 4];
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

void SoftRender(const DrawData& data, const FontAtlas& atlas, Bitmap& bm, const std::vector<SoftTexture>* textures) {
    if (bm.w <= 0 || bm.h <= 0 || bm.rgba.size() != (std::size_t)bm.w * bm.h * 4) return;
    for (const DrawList* dl : data.lists) {
        if (!dl) continue;
        for (const DrawCmd& cmd0 : dl->cmds) {
            DrawCmd cmd = cmd0;
            {   // a garbage clip rectangle is clamped (infinite) or skipped (NaN), never trusted
                float* f[4] = {&cmd.clip.min.x, &cmd.clip.min.y, &cmd.clip.max.x, &cmd.clip.max.y};
                bool nan = false;
                for (float* p : f) { if (std::isnan(*p)) nan = true; else *p = std::max(-1e6f, std::min(1e6f, *p)); }
                if (nan) continue;
            }
            Tex tex;
            if (cmd.tex == 0) { tex.w = atlas.width; tex.h = atlas.height; tex.alpha = atlas.alpha.empty() ? nullptr : atlas.alpha.data(); }
            else if (textures && cmd.tex - 1 < textures->size()) {
                const SoftTexture& st = (*textures)[cmd.tex - 1];
                if (st.rgba && st.w > 0 && st.h > 0) { tex.w = st.w; tex.h = st.h; tex.rgba = st.rgba; }
            }
            if (tex.w == 0) continue;     // unknown texture (or an empty atlas): nothing to sample
            const std::size_t end = std::min<std::size_t>(dl->idx.size(), (std::size_t)cmd.idxOffset + cmd.idxCount);
            for (std::size_t i = cmd.idxOffset; i + 3 <= end; i += 3) {
                const std::uint32_t a = dl->idx[i], b = dl->idx[i + 1], c = dl->idx[i + 2];
                if (a >= dl->vtx.size() || b >= dl->vtx.size() || c >= dl->vtx.size()) continue;
                Triangle(bm, cmd.clip, tex, dl->vtx[a], dl->vtx[b], dl->vtx[c]);
            }
        }
    }
}

} // namespace imui
} // namespace okay
