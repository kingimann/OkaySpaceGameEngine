// okay::imui - hashing, UTF-8, font atlas and draw list. See Imui.hpp.
#include "imui/Imui.hpp"

#include <algorithm>
#include <cmath>

namespace okay {
namespace imui {

namespace {
const float kPi = 3.14159265358979f;
bool Finite(float f) { return std::isfinite(f) && std::fabs(f) < 1e7f; }
bool Finite(Vec2 v) { return Finite(v.x) && Finite(v.y); }
}

Rect Rect::Intersect(const Rect& o) const {
    Rect r(std::max(min.x, o.min.x), std::max(min.y, o.min.y), std::min(max.x, o.max.x), std::min(max.y, o.max.y));
    if (r.max.x < r.min.x) r.max.x = r.min.x;
    if (r.max.y < r.min.y) r.max.y = r.min.y;
    return r;
}

// ---- hashing ---------------------------------------------------------------------------------
std::uint32_t HashBytes(const void* data, std::size_t size, std::uint32_t seed) {
    std::uint32_t h = seed ? seed : 2166136261u;
    const unsigned char* p = (const unsigned char*)data;
    for (std::size_t i = 0; i < size; ++i) { h ^= p[i]; h *= 16777619u; }
    h ^= h >> 15;           // final mix so that nearby seeds spread
    h *= 2246822519u;
    h ^= h >> 13;
    return h ? h : 1u;
}
std::uint32_t HashString(const char* s, std::uint32_t seed) {
    std::size_t n = 0;
    if (s) while (s[n]) ++n;
    return HashBytes(s, n, seed);
}

// ---- UTF-8 -----------------------------------------------------------------------------------
int Utf8Decode(const char* b, const char* e, std::uint32_t* cp) {
    if (b >= e) { *cp = 0; return 0; }
    const unsigned char c = (unsigned char)b[0];
    int n = 1; std::uint32_t v = 0xFFFD, minv = 0;
    if (c < 0x80) { *cp = c; return 1; }
    else if ((c & 0xE0) == 0xC0) { n = 2; v = c & 0x1F; minv = 0x80; }
    else if ((c & 0xF0) == 0xE0) { n = 3; v = c & 0x0F; minv = 0x800; }
    else if ((c & 0xF8) == 0xF0) { n = 4; v = c & 0x07; minv = 0x10000; }
    else { *cp = 0xFFFD; return 1; }
    if (e - b < n) { *cp = 0xFFFD; return 1; }
    for (int i = 1; i < n; ++i) {
        const unsigned char d = (unsigned char)b[i];
        if ((d & 0xC0) != 0x80) { *cp = 0xFFFD; return 1; }
        v = (v << 6) | (d & 0x3F);
    }
    if (v < minv || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) { *cp = 0xFFFD; return 1; }
    *cp = v;
    return n;
}
void Utf8Append(std::string& out, std::uint32_t cp) {
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
    if (cp < 0x80) out += (char)cp;
    else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
    else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
}
bool Utf8Valid(const std::string& s) {
    const char* p = s.data(); const char* e = p + s.size();
    while (p < e) {
        std::uint32_t cp; int n = Utf8Decode(p, e, &cp);
        if (cp == 0xFFFD && !(n == 3 && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBF && (unsigned char)p[2] == 0xBD)) return false;
        p += n;
    }
    return true;
}

// ---- font atlas ------------------------------------------------------------------------------
void FontAtlas::BuildFrom8x8(GlyphSource rows, int scale) {
    if (scale < 1) scale = 1;
    if (scale > 8) scale = 8;
    const int cell = 8 * scale, cols = 16, nrows = 6;
    width = cols * cell; height = nrows * cell;
    alpha.assign((std::size_t)width * height, 0);
    for (int g = 0; g < 96; ++g) {
        const int cx = (g % cols) * cell, cy = (g / cols) * cell;
        const unsigned char* r = (g < 95 && rows) ? rows((char)(32 + g)) : nullptr;
        for (int y = 0; y < cell; ++y)
            for (int x = 0; x < cell; ++x) {
                bool on = (g == 95) || (r && (r[y / scale] & (1u << (x / scale))));
                alpha[(std::size_t)(cy + y) * width + cx + x] = on ? 255 : 0;
            }
        Glyph& gl = glyphs_[g];
        gl.u0 = (float)cx / width; gl.v0 = (float)cy / height;
        gl.u1 = (float)(cx + cell) / width; gl.v1 = (float)(cy + cell) / height;
        gl.w = gl.h = (float)cell; gl.advance = (float)cell;
    }
    whiteUV = Vec2(((15 * cell) + cell * 0.5f) / width, ((5 * cell) + cell * 0.5f) / height);
    lineHeight = fontSize = (float)cell;
}
const Glyph& FontAtlas::GetGlyph(std::uint32_t cp) const {
    if (cp < 32 || cp > 126) cp = '?';
    return glyphs_[cp - 32];
}
Vec2 FontAtlas::CalcTextSize(const char* b, const char* e) const {
    if (!b) return Vec2(0, lineHeight);
    if (!e) { e = b; while (*e) ++e; }
    float w = 0, maxw = 0, h = lineHeight;
    while (b < e) {
        std::uint32_t cp; int n = Utf8Decode(b, e, &cp);
        if (n <= 0) break;
        b += n;
        if (cp == '\n') { maxw = std::max(maxw, w); w = 0; h += lineHeight; continue; }
        if (cp == '\r') continue;
        w += GetGlyph(cp).advance;
    }
    return Vec2(std::max(maxw, w), h);
}

// ---- draw list -------------------------------------------------------------------------------
void DrawList::Init(const FontAtlas* a, Rect full) { atlas_ = a; fullClip_ = full; Clear(); }
void DrawList::Clear() { vtx.clear(); idx.clear(); cmds.clear(); clipStack_.clear(); }

void DrawList::PushClipRect(const Rect& r, bool intersect) {
    Rect n = r;
    if (!Finite(n.min) || !Finite(n.max)) n = Rect(0, 0, 0, 0);
    if (intersect) n = n.Intersect(ClipRect());
    clipStack_.push_back(n);
}
void DrawList::PopClipRect() { if (!clipStack_.empty()) clipStack_.pop_back(); }

DrawCmd& DrawList::Cmd(std::uint32_t tex) {
    const Rect clip = ClipRect();
    if (cmds.empty() || cmds.back().tex != tex || !(cmds.back().clip.min == clip.min) || !(cmds.back().clip.max == clip.max)) {
        DrawCmd c; c.clip = clip; c.idxOffset = (std::uint32_t)idx.size(); c.idxCount = 0; c.tex = tex;
        cmds.push_back(c);
    }
    return cmds.back();
}

void DrawList::AddQuadUV(std::uint32_t tex, Vec2 a, Vec2 b, Vec2 uv0, Vec2 uv1, Col c) {
    if (!Finite(a) || !Finite(b) || ColA(c) == 0) return;
    DrawCmd& cmd = Cmd(tex);
    const std::uint32_t base = (std::uint32_t)vtx.size();
    vtx.push_back({Vec2(a.x, a.y), Vec2(uv0.x, uv0.y), c});
    vtx.push_back({Vec2(b.x, a.y), Vec2(uv1.x, uv0.y), c});
    vtx.push_back({Vec2(b.x, b.y), Vec2(uv1.x, uv1.y), c});
    vtx.push_back({Vec2(a.x, b.y), Vec2(uv0.x, uv1.y), c});
    const std::uint32_t q[6] = {0, 1, 2, 0, 2, 3};
    for (std::uint32_t i : q) idx.push_back(base + i);
    cmd.idxCount += 6;
}

void DrawList::AddConvexFill(const std::vector<Vec2>& p, Col c) {
    if (p.size() < 3 || ColA(c) == 0) return;
    for (const Vec2& v : p) if (!Finite(v)) return;
    DrawCmd& cmd = Cmd(0);
    const Vec2 uv = atlas_ ? atlas_->WhiteUV() : Vec2(0, 0);
    const std::uint32_t base = (std::uint32_t)vtx.size();
    for (const Vec2& v : p) vtx.push_back({v, uv, c});
    for (std::uint32_t i = 2; i < p.size(); ++i) { idx.push_back(base); idx.push_back(base + i - 1); idx.push_back(base + i); }
    cmd.idxCount += (std::uint32_t)(3 * (p.size() - 2));
}

void DrawList::RoundedPath(std::vector<Vec2>& out, Vec2 a, Vec2 b, float r) const {
    r = std::min(r, std::min((b.x - a.x) * 0.5f, (b.y - a.y) * 0.5f));
    if (!(r > 0.5f)) { out = {a, Vec2(b.x, a.y), b, Vec2(a.x, b.y)}; return; }
    const int seg = std::max(2, std::min(8, (int)(r * 0.5f) + 1));
    const Vec2 centers[4] = {Vec2(a.x + r, a.y + r), Vec2(b.x - r, a.y + r), Vec2(b.x - r, b.y - r), Vec2(a.x + r, b.y - r)};
    for (int corner = 0; corner < 4; ++corner) {
        const float a0 = kPi * (1.0f + 0.5f * corner);
        for (int i = 0; i <= seg; ++i) {
            const float t = a0 + (kPi * 0.5f) * i / seg;
            out.push_back(Vec2(centers[corner].x + std::cos(t) * r, centers[corner].y + std::sin(t) * r));
        }
    }
}

void DrawList::AddRectFilled(Vec2 a, Vec2 b, Col c, float rounding) {
    if (!Finite(a) || !Finite(b)) return;
    if (!(rounding > 0.5f)) { AddQuadUV(0, a, b, atlas_ ? atlas_->WhiteUV() : Vec2(), atlas_ ? atlas_->WhiteUV() : Vec2(), c); return; }
    std::vector<Vec2> p; RoundedPath(p, a, b, rounding);
    AddConvexFill(p, c);
}

void DrawList::AddRect(Vec2 a, Vec2 b, Col c, float rounding, float t) {
    if (!Finite(a) || !Finite(b) || !Finite(t) || t <= 0) return;
    if (!(rounding > 0.5f)) {   // four crisp edge rectangles
        AddRectFilled(Vec2(a.x, a.y), Vec2(b.x, a.y + t), c);
        AddRectFilled(Vec2(a.x, b.y - t), Vec2(b.x, b.y), c);
        AddRectFilled(Vec2(a.x, a.y + t), Vec2(a.x + t, b.y - t), c);
        AddRectFilled(Vec2(b.x - t, a.y + t), Vec2(b.x, b.y - t), c);
        return;
    }
    std::vector<Vec2> p; RoundedPath(p, a, b, rounding);
    AddPolyline(p, true, c, t);
}

void DrawList::AddPolyline(const std::vector<Vec2>& p, bool closed, Col c, float t) {
    const std::size_t n = p.size();
    for (std::size_t i = 0; i + (closed ? 0 : 1) < n; ++i) AddLine(p[i], p[(i + 1) % n], c, t);
}

void DrawList::AddLine(Vec2 a, Vec2 b, Col c, float t) {
    if (!Finite(a) || !Finite(b) || !Finite(t) || ColA(c) == 0) return;
    const float dx = b.x - a.x, dy = b.y - a.y, len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-6f) return;
    const float nx = -dy / len * t * 0.5f, ny = dx / len * t * 0.5f;
    DrawCmd& cmd = Cmd(0);
    const Vec2 uv = atlas_ ? atlas_->WhiteUV() : Vec2(0, 0);
    const std::uint32_t base = (std::uint32_t)vtx.size();
    vtx.push_back({Vec2(a.x + nx, a.y + ny), uv, c});
    vtx.push_back({Vec2(b.x + nx, b.y + ny), uv, c});
    vtx.push_back({Vec2(b.x - nx, b.y - ny), uv, c});
    vtx.push_back({Vec2(a.x - nx, a.y - ny), uv, c});
    const std::uint32_t q[6] = {0, 1, 2, 0, 2, 3};
    for (std::uint32_t i : q) idx.push_back(base + i);
    cmd.idxCount += 6;
}

void DrawList::AddTriangleFilled(Vec2 a, Vec2 b, Vec2 c, Col col) { AddConvexFill({a, b, c}, col); }

void DrawList::AddCircleFilled(Vec2 ctr, float r, Col c, int seg) {
    if (!Finite(ctr) || !Finite(r) || r <= 0) return;
    if (seg < 3) seg = std::max(8, std::min(48, (int)(r * 1.5f)));
    std::vector<Vec2> p;
    for (int i = 0; i < seg; ++i) { const float t = 2 * kPi * i / seg; p.push_back(Vec2(ctr.x + std::cos(t) * r, ctr.y + std::sin(t) * r)); }
    AddConvexFill(p, c);
}

void DrawList::AddText(Vec2 pos, Col c, const char* b, const char* e) {
    if (!atlas_ || !b || !Finite(pos) || ColA(c) == 0) return;
    if (!e) { e = b; while (*e) ++e; }
    const float x0 = std::floor(pos.x + 0.5f);
    float x = x0, y = std::floor(pos.y + 0.5f);
    const Rect clip = ClipRect();
    while (b < e) {
        std::uint32_t cp; int n = Utf8Decode(b, e, &cp);
        if (n <= 0) break;
        b += n;
        if (cp == '\n') { x = x0; y += atlas_->lineHeight; continue; }
        if (cp == '\r') continue;
        const Glyph& g = atlas_->GetGlyph(cp);
        if (cp != ' ' && x + g.w > clip.min.x && x < clip.max.x && y + g.h > clip.min.y && y < clip.max.y)
            AddQuadUV(0, Vec2(x, y), Vec2(x + g.w, y + g.h), Vec2(g.u0, g.v0), Vec2(g.u1, g.v1), c);
        x += g.advance;
    }
}

void DrawList::AddImage(std::uint32_t tex, Vec2 a, Vec2 b, Vec2 uv0, Vec2 uv1, Col tint) {
    AddQuadUV(tex, a, b, uv0, uv1, tint);
}

} // namespace imui
} // namespace okay
