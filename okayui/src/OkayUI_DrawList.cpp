#include "OkayUI_ContextState.hpp"
#include <algorithm>
#include <cmath>
#include "OkayUIInternal.hpp"
#include "okay/UI/OkayUI.hpp"
#include "okay/UI/OkayUICompat.hpp"

namespace OkayUI {
using namespace ui_core;

namespace ui_core {

Vec2 g_path[kMaxPath];
int  g_pathN = 0;


void unpack(Color32 c, unsigned char out[4]) {
    out[0] = (unsigned char)(c & 0xffu);         out[1] = (unsigned char)((c >> 8) & 0xffu);
    out[2] = (unsigned char)((c >> 16) & 0xffu); out[3] = (unsigned char)((c >> 24) & 0xffu);
}

// A drawing call of the public draw list: dimming off for its duration, overlay batch for the foreground list.
struct ListScope {
    bool prevRaw, prevOv;
    explicit ListScope(bool overlay) : prevRaw(g_rawDraw), prevOv(g_toOverlay) { g_rawDraw = true; g_toOverlay = overlay || prevOv; }   // the window list draws where the widgets around it do: in a popup, that is the overlay
    ~ListScope() { g_rawDraw = prevRaw; g_toOverlay = prevOv; }
};

// cos(pi / n) for n = 3..128, worked out once: the mitre of a regular n-gon's stroke.
float cosHalfTurn(int n) {
    static float t[129] = {};
    if (t[n] == 0.0f) t[n] = SDL_cosf(3.14159265f / (float)n);
    return t[n];
}

int circleSegments(float r, int requested) {
    if (requested >= 3) return requested > 128 ? 128 : requested;
    int n = 10 + (int)(r * 0.9f);
    return n < 12 ? 12 : (n > 64 ? 64 : n);
}

// Stroke a polyline as one quad strip with mitred joins (so a translucent stroke never double-blends at a
// joint). Consecutive duplicate points are dropped; a join sharper than the mitre limit is clamped.
void strokePath(const Vec2* in, int n, bool closed, float th, const unsigned char c[4]) {
    if (n < 2 || !(th > 0.0f) || !finite1(th)) return;
    Vec2 p[kMaxPath]; int m = 0;
    for (int i = 0; i < n && m < kMaxPath; ++i) {
        if (m > 0) {
            const float dx = in[i].x - p[m - 1].x, dy = in[i].y - p[m - 1].y;
            if (dx * dx + dy * dy < 1.0e-6f) continue;
        }
        p[m++] = in[i];
    }
    if (closed && m > 2) { const float dx = p[0].x - p[m - 1].x, dy = p[0].y - p[m - 1].y; if (dx * dx + dy * dy < 1.0e-6f) --m; }
    if (m < 2) { if (m == 1) quad(p[0].x - th * 0.5f, p[0].y - th * 0.5f, th, th, c); return; }
    if (m == 2) closed = false;
    const float half = th * 0.5f;
    Vec2 off[kMaxPath];
    auto segNormal = [&](int a, int b) {
        float dx = p[b].x - p[a].x, dy = p[b].y - p[a].y;
        const float l = SDL_sqrtf(dx * dx + dy * dy);
        return Vec2{ -dy / l, dx / l };
    };
    for (int i = 0; i < m; ++i) {
        const bool hasPrev = closed || i > 0, hasNext = closed || i < m - 1;
        const Vec2 nb = hasNext ? segNormal(i, (i + 1) % m) : segNormal(i - 1, i);
        const Vec2 na = hasPrev ? segNormal((i + m - 1) % m, i) : nb;
        float mx = na.x + nb.x, my = na.y + nb.y;
        const float ml = SDL_sqrtf(mx * mx + my * my);
        if (ml < 1.0e-4f) { off[i] = Vec2{ na.x * half, na.y * half }; continue; }   // a U-turn: no mitre
        mx /= ml; my /= ml;
        float k = mx * na.x + my * na.y;                  // cos of half the turn angle
        if (k < 0.34f) k = 0.34f;                         // mitre limit about 3x
        off[i] = Vec2{ mx * half / k, my * half / k };
    }
    const int segs = closed ? m : m - 1;
    for (int i = 0; i < segs; ++i) {
        const int j = (i + 1) % m;
        quadPts(p[i].x + off[i].x, p[i].y + off[i].y, p[j].x + off[j].x, p[j].y + off[j].y,
                p[j].x - off[j].x, p[j].y - off[j].y, p[i].x - off[i].x, p[i].y - off[i].y, c);
    }
}

void fillConvex(const Vec2* p, int n, const unsigned char c[4]) {
    if (n < 3) return;
    for (int i = 1; i + 1 < n; ++i) tri(p[0].x, p[0].y, p[i].x, p[i].y, p[i + 1].x, p[i + 1].y, c);
}

// The rounded-rectangle outline as a closed path (corner radius `rad`, arcs of 6 segments).
int roundedRectPath(Vec2* out, float x0, float y0, float x1, float y1, float rad) {
    const float w = x1 - x0, h = y1 - y0;
    if (rad > w * 0.5f) rad = w * 0.5f;
    if (rad > h * 0.5f) rad = h * 0.5f;
    int n = 0;
    if (rad <= 0.5f) {
        out[n++] = Vec2{ x0, y0 }; out[n++] = Vec2{ x1, y0 }; out[n++] = Vec2{ x1, y1 }; out[n++] = Vec2{ x0, y1 };
        return n;
    }
    // The corners' unit points, worked out once (the same float angles as before: the path is unchanged), not 56 sines and cosines
    // per rectangle.
    struct Arcs {
        float c[4][7], s[4][7];
        Arcs() {
            const float PI = 3.14159265f;
            const float a0[4] = { PI, 1.5f * PI, 0.0f, 0.5f * PI };
            for (int k = 0; k < 4; ++k)
                for (int i = 0; i <= 6; ++i) { const float a = a0[k] + (0.5f * PI) * (float)i / 6.0f; c[k][i] = SDL_cosf(a); s[k][i] = SDL_sinf(a); }
        }
    };
    static const Arcs A;
    const float cx[4] = { x0 + rad, x1 - rad, x1 - rad, x0 + rad }, cy[4] = { y0 + rad, y0 + rad, y1 - rad, y1 - rad };
    for (int k = 0; k < 4; ++k)
        for (int i = 0; i <= 6; ++i) out[n++] = Vec2{ cx[k] + rad * A.c[k][i], cy[k] + rad * A.s[k][i] };
    return n;
}

} // namespace

Color32 GetColorU32(Col which, float alphaMul) {
    unsigned char tmp[4];
    const unsigned char* c = styleColor((int)which, tmp);
    float a = (float)c[3] * (alphaMul != alphaMul ? 0.0f : (alphaMul < 0.0f ? 0.0f : (alphaMul > 1.0f ? 1.0f : alphaMul)));
    return RGBA(c[0], c[1], c[2], (unsigned char)(a + 0.5f));
}

Color32 ColorMulAlpha(Color32 c, float alphaMul) {
    if (alphaMul != alphaMul) alphaMul = 0.0f;
    if (alphaMul < 0.0f) alphaMul = 0.0f;
    if (alphaMul > 1.0f) alphaMul = 1.0f;
    const unsigned a = (unsigned)((float)(c >> 24) * alphaMul + 0.5f);
    return (c & 0x00ffffffu) | (a << 24);
}

// ---- DrawList ---------------------------------------------------------------------------------
void DrawList::AddLine(Vec2 a, Vec2 b, Color32 col, float th) const {
    if (!finite2(a) || !finite2(b) || !finite1(th) || th <= 0.0f) return;
    ListScope s(overlay); unsigned char c[4]; unpack(col, c);
    line(a.x, a.y, b.x, b.y, th, c);
}

void DrawList::AddRectFilled(Vec2 mn, Vec2 mx, Color32 col, float rounding) const {
    if (!finite2(mn) || !finite2(mx) || mx.x <= mn.x || mx.y <= mn.y) return;
    ListScope s(overlay); unsigned char c[4]; unpack(col, c);
    rrect(mn.x, mn.y, mx.x - mn.x, mx.y - mn.y, finite1(rounding) ? rounding : 0.0f, c);
}

void DrawList::AddRect(Vec2 mn, Vec2 mx, Color32 col, float rounding, float th) const {
    if (!finite2(mn) || !finite2(mx) || mx.x <= mn.x || mx.y <= mn.y || !finite1(th) || th <= 0.0f) return;
    ListScope s(overlay); unsigned char c[4]; unpack(col, c);
    const float w = mx.x - mn.x, h = mx.y - mn.y;
    if (th * 2.0f >= w || th * 2.0f >= h) { quad(mn.x, mn.y, w, h, c); return; }   // the stroke fills the box
    if (!finite1(rounding) || rounding <= 0.5f) {   // four bars, drawn inside the rectangle, no overlap
        quad(mn.x, mn.y, w, th, c);
        quad(mn.x, mx.y - th, w, th, c);
        quad(mn.x, mn.y + th, th, h - 2.0f * th, c);
        quad(mx.x - th, mn.y + th, th, h - 2.0f * th, c);
        return;
    }
    Vec2 pts[32];
    const int n = roundedRectPath(pts, mn.x + th * 0.5f, mn.y + th * 0.5f, mx.x - th * 0.5f, mx.y - th * 0.5f,
                                  rounding - th * 0.5f > 0.0f ? rounding - th * 0.5f : 0.0f);
    strokePath(pts, n, true, th, c);
}

void DrawList::AddRectFilledMultiColor(Vec2 mn, Vec2 mx, Color32 tl, Color32 tr, Color32 br, Color32 bl) const {
    if (!finite2(mn) || !finite2(mx) || mx.x <= mn.x || mx.y <= mn.y) return;
    ListScope s(overlay); unsigned char a[4], b[4], c[4], d[4];
    unpack(tl, a); unpack(tr, b); unpack(br, c); unpack(bl, d);
    gradQuad(mn.x, mn.y, mx.x - mn.x, mx.y - mn.y, a, b, c, d);
}

void DrawList::AddCircleFilled(Vec2 center, float radius, Color32 col, int segments) const {
    if (!finite2(center) || !finite1(radius) || radius <= 0.0f) return;
    ListScope s(overlay); unsigned char c[4]; unpack(col, c);
    fillCircle(center.x, center.y, radius, c, circleSegments(radius, segments));
}

void DrawList::AddCircle(Vec2 center, float radius, Color32 col, int segments, float th) const {
    if (!finite2(center) || !finite1(radius) || radius <= 0.0f || !finite1(th) || th <= 0.0f) return;
    ListScope s(overlay); unsigned char c[4]; unpack(col, c);
    const int n = circleSegments(radius, segments);
    float r = radius - th * 0.5f; if (r < 0.0f) r = 0.0f;   // the stroke sits inside `radius`
    if (th >= radius) { fillCircle(center.x, center.y, radius, c, n); return; }
    // A regular polygon's stroke: every mitre is radial, half the thickness over cos(pi / n) long (what strokePath works out per
    // corner), so the ring is drawn straight from the cached unit points: no sines, cosines or square roots per call.
    const float* u = unitCircle(n);
    const float k = cosHalfTurn(n), ro = r + th * 0.5f / k, ri = r - th * 0.5f / k;
    float px = u[(n - 1) * 2], py = u[(n - 1) * 2 + 1];   // angle 2 pi: the first corner
    for (int i = 0; i < n; ++i) {
        const float qx = u[i * 2], qy = u[i * 2 + 1];
        quadPts(center.x + px * ro, center.y + py * ro, center.x + qx * ro, center.y + qy * ro,
                center.x + qx * ri, center.y + qy * ri, center.x + px * ri, center.y + py * ri, c);
        px = qx; py = qy;
    }
}

void DrawList::AddTriangleFilled(Vec2 a, Vec2 b, Vec2 c3, Color32 col) const {
    if (!finite2(a) || !finite2(b) || !finite2(c3)) return;
    ListScope s(overlay); unsigned char c[4]; unpack(col, c);
    tri(a.x, a.y, b.x, b.y, c3.x, c3.y, c);
}

void DrawList::AddTriangle(Vec2 a, Vec2 b, Vec2 c3, Color32 col, float th) const {
    if (!finite2(a) || !finite2(b) || !finite2(c3)) return;
    const Vec2 p[3] = { a, b, c3 };
    AddPolyline(p, 3, col, true, th);
}

void DrawList::AddQuadFilled(Vec2 a, Vec2 b, Vec2 c3, Vec2 d, Color32 col) const {
    if (!finite2(a) || !finite2(b) || !finite2(c3) || !finite2(d)) return;
    const Vec2 p[4] = { a, b, c3, d };
    AddConvexPolyFilled(p, 4, col);
}

void DrawList::AddQuad(Vec2 a, Vec2 b, Vec2 c3, Vec2 d, Color32 col, float th) const {
    const Vec2 p[4] = { a, b, c3, d };
    AddPolyline(p, 4, col, true, th);
}

void DrawList::AddPolyline(const Vec2* pts, int count, Color32 col, bool closed, float th) const {
    if (!pts || count < 2 || count > kMaxPath || !finite1(th) || th <= 0.0f) return;
    for (int i = 0; i < count; ++i) if (!finite2(pts[i])) return;
    ListScope s(overlay); unsigned char c[4]; unpack(col, c);
    strokePath(pts, count, closed, th, c);
}

void DrawList::AddPolyline(const Vec2* pts, int count, Color32 col, int flags, float th) const { AddPolyline(pts, count, col, (flags & DrawFlags_Closed) != 0, th); }

void DrawList::AddConcavePolyFilled(const Vec2* pts, int count, Color32 col) const {
    if (!pts || count < 3 || count > kMaxPath) return;
    for (int i = 0; i < count; ++i) if (!finite2(pts[i])) return;
    int idx[kMaxPath]; for (int i = 0; i < count; ++i) idx[i] = i;
    float area = 0.0f; for (int i = 0; i < count; ++i) { const Vec2 a = pts[i], b = pts[(i + 1) % count]; area += a.x * b.y - b.x * a.y; }
    const float sgn = area < 0.0f ? -1.0f : 1.0f;
    int n = count, guard = 0;
    auto cross = [&](Vec2 a, Vec2 b, Vec2 c) { return ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)) * sgn; };
    while (n > 3 && guard++ < count * count) {
        bool clipped = false;
        for (int i = 0; i < n && !clipped; ++i) {
            const int ia = idx[(i + n - 1) % n], ib = idx[i], ic = idx[(i + 1) % n];
            const Vec2 a = pts[ia], b = pts[ib], c = pts[ic];
            if (cross(a, b, c) <= 0.0f) continue;   // a reflex or flat corner is not an ear
            bool inside = false;
            for (int j = 0; j < n && !inside; ++j) {
                const int ip = idx[j]; if (ip == ia || ip == ib || ip == ic) continue;
                const Vec2 p = pts[ip];
                if (cross(a, b, p) >= 0.0f && cross(b, c, p) >= 0.0f && cross(c, a, p) >= 0.0f) inside = true;
            }
            if (inside) continue;
            const Vec2 tri[3] = {a, b, c};
            AddConvexPolyFilled(tri, 3, col);
            for (int j = i; j + 1 < n; ++j) idx[j] = idx[j + 1];
            --n; clipped = true;
        }
        if (!clipped) break;   // degenerate outline: stop rather than loop
    }
    if (n == 3) { const Vec2 tri[3] = {pts[idx[0]], pts[idx[1]], pts[idx[2]]}; AddConvexPolyFilled(tri, 3, col); }
}

void DrawList::AddConvexPolyFilled(const Vec2* pts, int count, Color32 col) const {
    if (!pts || count < 3 || count > kMaxPath) return;
    for (int i = 0; i < count; ++i) if (!finite2(pts[i])) return;
    ListScope s(overlay); unsigned char c[4]; unpack(col, c);
    fillConvex(pts, count, c);
}

void DrawList::AddBezierCubic(Vec2 p1, Vec2 p2, Vec2 p3, Vec2 p4, Color32 col, float th, int segments) const {
    if (!finite2(p1) || !finite2(p2) || !finite2(p3) || !finite2(p4)) return;
    int n = segments > 0 ? segments : 24;
    if (n > kMaxPath - 1) n = kMaxPath - 1;
    Vec2 pts[kMaxPath];
    for (int i = 0; i <= n; ++i) {
        const float t = (float)i / (float)n, u = 1.0f - t;
        const float a = u * u * u, b = 3.0f * u * u * t, cc = 3.0f * u * t * t, d = t * t * t;
        pts[i] = Vec2{ a * p1.x + b * p2.x + cc * p3.x + d * p4.x, a * p1.y + b * p2.y + cc * p3.y + d * p4.y };
    }
    AddPolyline(pts, n + 1, col, false, th);
}

void DrawList::AddText(Vec2 pos, Color32 col, const char* text, float scale) const {
    if (!text || !*text || !finite2(pos)) return;
    if (scale == 0.0f) scale = g_theme.textScale;
    else if (!(scale > 0.0f) || !finite1(scale)) return;
    ListScope s(overlay); unsigned char c[4]; unpack(col, c);
    drawText(pos.x, pos.y, text, scale, c);
}

void DrawList::AddImage(SDL_Texture* tex, Vec2 mn, Vec2 mx, Vec2 uv0, Vec2 uv1, Color32 tint) const {
    if (!tex || !finite2(mn) || !finite2(mx) || mx.x <= mn.x || mx.y <= mn.y) return;
    ListScope s(false); unsigned char c[4]; unpack(tint, c);   // textured geometry always goes to the main batch
    texQuad(mn.x, mn.y, mx.x - mn.x, mx.y - mn.y, tex, uv0.x, uv0.y, uv1.x, uv1.y, c);
}

// A textured convex polygon: a fan from point 0, each point with its own texture coordinate.
static void texPoly(SDL_Texture* tex, const Vec2* p, const Vec2* uv, int n, const unsigned char tint[4]) {
    if (g_cullRow || n < 3) return;
    setTexture(tex);
    if (g_nv + n > kMaxVerts || g_ni + (n - 2) * 3 > kMaxIdx) { noteDrop(); setTexture(g_defTex); return; }
    const SDL_Color sc = toColor(tint);
    const int base = g_nv;
    for (int i = 0; i < n; ++i) {
        SDL_Vertex v; v.color = sc; v.position.x = p[i].x; v.position.y = p[i].y; v.tex_coord.x = uv[i].x; v.tex_coord.y = uv[i].y;
        g_verts[g_nv++] = v;
    }
    for (int i = 1; i + 1 < n; ++i) { g_idx[g_ni++] = base; g_idx[g_ni++] = base + i; g_idx[g_ni++] = base + i + 1; }
    setTexture(g_defTex);
}

Vec2 Font::CalcTextSizeA(float size, float, float, const char* text, const char** remaining) const {
    if (remaining) *remaining = text ? text + std::strlen(text) : nullptr;
    if (!text || !*text || !(size > 0.0f) || !std::isfinite(size)) return Vec2{0, 0};
    const float scale = size / (height > 0 ? (float)height : 8.0f), cw = (float)width * scale;
    float best = 0.0f; int lines = 1, cur = 0;
    for (const char* p = text; *p; ++p) {
        if (*p == '\n') { if ((float)cur > best) best = (float)cur; cur = 0; ++lines; }
        else if (((unsigned char)*p & 0xC0) != 0x80) ++cur;   // count characters, not UTF-8 continuation bytes
    }
    if ((float)cur > best) best = (float)cur;
    return Vec2{best * cw, (float)lines * size};
}

void DrawList::AddText(const Font* font, float fontSize, Vec2 pos, Color32 col, const char* text, const char* textEnd, float wrapWidth, const ImVec4* clip) const {
    if (!text || !finite1(fontSize) || fontSize <= 0.0f) return;
    char buf[1024]; std::size_t n = textEnd && textEnd >= text ? (std::size_t)(textEnd - text) : std::strlen(text);
    if (n > sizeof buf - 1) n = sizeof buf - 1;
    std::memcpy(buf, text, n); buf[n] = 0;
    if ((finite1(wrapWidth) && wrapWidth > 0.0f) || std::strchr(buf, '\n')) {   // Dear ImGui's wrapped text: one line under the other, fontSize apart
        float y = pos.y;
        const float wrapAt = finite1(wrapWidth) && wrapWidth > 0.0f ? wrapWidth : 1.0e9f;
        imguiWrap(buf, wrapAt, fontSize / 8.0f, [&](const char* l) { if (l[0]) AddText(font, fontSize, Vec2{pos.x, y}, col, l, nullptr, 0.0f, clip); y += fontSize; return wrapAt; });
        return;
    }
    const Font* f = font ? font : GetFont();
    if (f && f->ttf) {   // a user font: draw with it for this call at `fontSize` pixels
        const Font* prev = GetFont(); SetFont(f); const float saved = g_theme.textScale; g_theme.textScale = fontSize / 8.0f;
        AddText(pos, col, buf, fontSize / 8.0f);
        g_theme.textScale = saved; SetFont(prev);
        return;
    }
    const float h = f && f->height > 0 ? (float)f->height : 8.0f;
    AddText(pos, col, buf, fontSize / h);
}

// A rounded rectangle as a closed convex path, clockwise from the top-left; `flags` 0 means all four corners, DrawFlags_None none.
static int rectPath(Vec2 mn, Vec2 mx, float rounding, int flags, Vec2* pts) {
    const float w = mx.x - mn.x, h = mx.y - mn.y, maxR = 0.5f * (w < h ? w : h);
    if (!std::isfinite(rounding) || rounding < 0.0f) rounding = 0.0f;
    if (rounding > maxR) rounding = maxR;
    if (flags == 0) flags = DrawFlags_RoundCornersAll;
    if (flags & DrawFlags_None) flags = 0;
    const int arc = circleSegments(rounding, 0) / 4 + 1 > 16 ? 16 : circleSegments(rounding, 0) / 4 + 1;
    struct C { float cx, cy, a0; int bit; float px, py; };
    const C cs[4] = { { mn.x + rounding, mn.y + rounding, 3.14159265f, DrawFlags_RoundCornersTopLeft, mn.x, mn.y },
                      { mx.x - rounding, mn.y + rounding, 4.71238898f, DrawFlags_RoundCornersTopRight, mx.x, mn.y },
                      { mx.x - rounding, mx.y - rounding, 0.0f, DrawFlags_RoundCornersBottomRight, mx.x, mx.y },
                      { mn.x + rounding, mx.y - rounding, 1.57079633f, DrawFlags_RoundCornersBottomLeft, mn.x, mx.y } };
    int n = 0;
    for (const C& c : cs) {
        if (rounding >= 0.5f && (flags & c.bit)) for (int i = 0; i <= arc; ++i) { const float a = c.a0 + 1.57079633f * (float)i / (float)arc; pts[n++] = Vec2{ c.cx + rounding * SDL_cosf(a), c.cy + rounding * SDL_sinf(a) }; }
        else pts[n++] = Vec2{c.px, c.py};
    }
    return n;
}
void DrawList::AddRectFilled(Vec2 mn, Vec2 mx, Color32 col, float rounding, int flags) const {
    if (!finite2(mn) || !finite2(mx) || mx.x <= mn.x || mx.y <= mn.y) return;
    if (flags == 0 || flags == DrawFlags_RoundCornersAll) { AddRectFilled(mn, mx, col, rounding); return; }
    Vec2 p[4 * 17]; const int n = rectPath(mn, mx, rounding, flags, p);
    AddConvexPolyFilled(p, n, col);
}
void DrawList::AddRect(Vec2 mn, Vec2 mx, Color32 col, float rounding, int flags, float th) const {
    if (!finite2(mn) || !finite2(mx) || mx.x <= mn.x || mx.y <= mn.y) return;
    if (flags == 0 || flags == DrawFlags_RoundCornersAll) { AddRect(mn, mx, col, rounding, th); return; }
    Vec2 p[4 * 17]; const int n = rectPath(mn, mx, rounding, flags, p);
    AddPolyline(p, n, col, true, th);
}

void DrawList::AddText(Vec2 pos, Color32 col, const char* text, const char* textEnd) const {
    if (!text) return;
    char buf[1024]; std::size_t n = textEnd && textEnd >= text ? (std::size_t)(textEnd - text) : std::strlen(text);
    if (n > sizeof buf - 1) n = sizeof buf - 1;
    std::memcpy(buf, text, n); buf[n] = 0;
    AddText(pos, col, static_cast<const char*>(buf), 0.0f);
}

void DrawList::AddImageQuad(SDL_Texture* tex, Vec2 p1, Vec2 p2, Vec2 p3, Vec2 p4, Vec2 uv1, Vec2 uv2, Vec2 uv3, Vec2 uv4, Color32 tint) const {
    if (!tex || !finite2(p1) || !finite2(p2) || !finite2(p3) || !finite2(p4) || !finite2(uv1) || !finite2(uv2) || !finite2(uv3) || !finite2(uv4)) return;
    ListScope s(false); unsigned char c[4]; unpack(tint, c);
    const Vec2 p[4] = {p1, p2, p3, p4}, uv[4] = {uv1, uv2, uv3, uv4};
    texPoly(tex, p, uv, 4, c);
}

void DrawList::AddImageRounded(SDL_Texture* tex, Vec2 mn, Vec2 mx, Vec2 uv0, Vec2 uv1, Color32 tint, float rounding, int flags) const {
    if (!tex || !finite2(mn) || !finite2(mx) || !finite2(uv0) || !finite2(uv1) || mx.x <= mn.x || mx.y <= mn.y) return;
    if (!std::isfinite(rounding) || rounding < 0.0f) rounding = 0.0f;
    const float w = mx.x - mn.x, h = mx.y - mn.y;
    const float maxR = 0.5f * (w < h ? w : h);
    if (rounding > maxR) rounding = maxR;
    if (flags == 0) flags = DrawFlags_RoundCornersAll;
    if (flags & DrawFlags_None) flags = 0;
    if (rounding < 0.5f || (flags & DrawFlags_RoundCornersAll) == 0) { AddImage(tex, mn, mx, uv0, uv1, tint); return; }
    // Corners clockwise from the top-left; each rounded corner is an arc of `arc` steps, a square one is a single point.
    const int arc = circleSegments(rounding, 0) / 4 + 1 > 16 ? 16 : circleSegments(rounding, 0) / 4 + 1;
    Vec2 pts[4 * 17]; int n = 0;
    struct Corner { float cx, cy, a0; int bit; float px, py; };
    const Corner cs[4] = {
        { mn.x + rounding, mn.y + rounding, 3.14159265f,        DrawFlags_RoundCornersTopLeft,     mn.x, mn.y },
        { mx.x - rounding, mn.y + rounding, 4.71238898f,        DrawFlags_RoundCornersTopRight,    mx.x, mn.y },
        { mx.x - rounding, mx.y - rounding, 0.0f,               DrawFlags_RoundCornersBottomRight, mx.x, mx.y },
        { mn.x + rounding, mx.y - rounding, 1.57079633f,        DrawFlags_RoundCornersBottomLeft,  mn.x, mx.y },
    };
    for (const Corner& c : cs) {
        if (flags & c.bit) {
            for (int i = 0; i <= arc; ++i) {
                const float a = c.a0 + 1.57079633f * (float)i / (float)arc;
                pts[n++] = Vec2{ c.cx + rounding * SDL_cosf(a), c.cy + rounding * SDL_sinf(a) };
            }
        } else pts[n++] = Vec2{c.px, c.py};
    }
    Vec2 uv[4 * 17];
    for (int i = 0; i < n; ++i) {
        uv[i].x = uv0.x + (uv1.x - uv0.x) * (pts[i].x - mn.x) / w;
        uv[i].y = uv0.y + (uv1.y - uv0.y) * (pts[i].y - mn.y) / h;
    }
    ListScope s(false); unsigned char c[4]; unpack(tint, c);
    // A fan from the first point is convex-safe for a rounded rectangle, but the first point is on a corner arc: use the centre instead.
    Vec2 fan[4 * 17 + 2]; Vec2 fuv[4 * 17 + 2]; int m = 0;
    fan[m] = Vec2{ (mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f };
    fuv[m] = Vec2{ (uv0.x + uv1.x) * 0.5f, (uv0.y + uv1.y) * 0.5f }; ++m;
    for (int i = 0; i < n; ++i) { fan[m] = pts[i]; fuv[m] = uv[i]; ++m; }
    fan[m] = pts[0]; fuv[m] = uv[0]; ++m;
    texPoly(tex, fan, fuv, m, c);
}

void DrawList::AddNgon(Vec2 center, float radius, Color32 col, int n, float th) const {
    if (!finite2(center) || !finite1(radius) || radius < 0.0f || n < 3) return;
    if (n > 128) n = 128;
    Vec2 p[128];
    for (int i = 0; i < n; ++i) { const float a = 6.28318531f * (float)i / (float)n; p[i] = Vec2{ center.x + radius * SDL_cosf(a), center.y + radius * SDL_sinf(a) }; }
    AddPolyline(p, n, col, true, th);
}
static int ellipsePoints(Vec2 c, Vec2 r, float rot, int seg, Vec2* p) {
    if (seg <= 0) seg = 32;
    if (seg < 3) seg = 3;
    if (seg > 128) seg = 128;
    const float cr = SDL_cosf(rot), sr = SDL_sinf(rot);
    for (int i = 0; i < seg; ++i) {
        const float a = 6.28318531f * (float)i / (float)seg, ex = r.x * SDL_cosf(a), ey = r.y * SDL_sinf(a);
        p[i] = Vec2{ c.x + ex * cr - ey * sr, c.y + ex * sr + ey * cr };
    }
    return seg;
}
void DrawList::AddEllipse(Vec2 c, Vec2 r, Color32 col, float rot, int seg, float th) const {
    if (!finite2(c) || !finite2(r) || !finite1(rot) || r.x < 0.0f || r.y < 0.0f) return;
    Vec2 p[128]; const int n = ellipsePoints(c, r, rot, seg, p);
    AddPolyline(p, n, col, true, th);
}
void DrawList::AddEllipseFilled(Vec2 c, Vec2 r, Color32 col, float rot, int seg) const {
    if (!finite2(c) || !finite2(r) || !finite1(rot) || r.x < 0.0f || r.y < 0.0f) return;
    Vec2 p[128]; const int n = ellipsePoints(c, r, rot, seg, p);
    AddConvexPolyFilled(p, n, col);
}

void DrawList::AddNgonFilled(Vec2 center, float radius, Color32 col, int n) const {
    if (!finite2(center) || !finite1(radius) || radius < 0.0f || n < 3) return;
    if (n > 128) n = 128;
    Vec2 p[128];
    for (int i = 0; i < n; ++i) { const float a = 6.28318531f * (float)i / (float)n; p[i] = Vec2{ center.x + radius * SDL_cosf(a), center.y + radius * SDL_sinf(a) }; }
    AddConvexPolyFilled(p, n, col);
}

void DrawList::PushClipRect(Vec2 mn, Vec2 mx) const {
    OkayUI::PushClipRect(mn.x, mn.y, mx.x - mn.x, mx.y - mn.y);
}
void DrawList::PushClipRect(Vec2 mn, Vec2 mx, bool) const { PushClipRect(mn, mx); }
void DrawList::PopClipRect() const { OkayUI::PopClipRect(); }

static void syncPath(const DrawList& d) { d._Path.clear(); for (int i = 0; i < g_pathN; ++i) d._Path.push_back(g_path[i]); }
void DrawList::PathClear() const { g_pathN = 0; _Path.clear(); }
void DrawList::PathLineTo(Vec2 p) const { if (g_pathN < kMaxPath && finite2(p)) g_path[g_pathN++] = p; syncPath(*this); }
void DrawList::PathArcTo(Vec2 center, float radius, float a0, float a1, int segments) const {
    if (!finite2(center) || !finite1(radius) || !finite1(a0) || !finite1(a1) || radius < 0.0f) return;
    int n = segments > 0 ? segments : circleSegments(radius, 0) / 4 + 1;
    if (n > 128) n = 128;
    for (int i = 0; i <= n; ++i) {
        const float a = a0 + (a1 - a0) * (float)i / (float)n;
        PathLineTo(Vec2{ center.x + radius * SDL_cosf(a), center.y + radius * SDL_sinf(a) });
    }
}
void DrawList::PathRect(Vec2 mn, Vec2 mx, float rounding, int) const {
    if (!finite2(mn) || !finite2(mx)) return;
    if (mx.x < mn.x) std::swap(mn.x, mx.x);
    if (mx.y < mn.y) std::swap(mn.y, mx.y);
    Vec2 pts[kMaxPath];
    const int n = roundedRectPath(pts, mn.x, mn.y, mx.x, mx.y, rounding > 0.0f && std::isfinite(rounding) ? rounding : 0.0f);
    for (int i = 0; i < n && g_pathN < kMaxPath; ++i) g_path[g_pathN++] = pts[i];
    syncPath(*this);
}
void DrawList::PathStroke(Color32 col, bool closed, float th) const {
    if (g_pathN >= 2) AddPolyline(g_path, g_pathN, col, closed, th);
    g_pathN = 0; _Path.clear();
}
void DrawList::PathFillConvex(Color32 col) const {
    if (g_pathN >= 3) AddConvexPolyFilled(g_path, g_pathN, col);
    g_pathN = 0; _Path.clear();
}

const DrawListHandle ui_core::g_windowList(false);
const DrawListHandle ui_core::g_foregroundList(true);
DrawListHandle::operator DrawList*() const { if (isNull) return nullptr; return const_cast<DrawListHandle*>(overlay ? &g_foregroundList : &g_windowList); }
// ---- raw geometry ----
Vec2 GetFontTexUvWhitePixel() { return Vec2{g_whiteU, g_whiteV}; }
namespace { SDL_Texture* g_texStack[8]; int g_texTop = 0; }
void DrawList::PrimReserve(int idxCount, int vtxCount) const {
    (void)idxCount; (void)vtxCount;
    _VtxCurrentIdx = (unsigned)(overlay ? g_onv : g_nv);
}
void DrawList::PrimWriteVtx(Vec2 pos, Vec2 uv, Color32 col) const {
    ListScope s(overlay);
    SDL_Vertex* V = g_toOverlay ? g_ovVerts : g_verts;
    int& nv = g_toOverlay ? g_onv : g_nv;
    if ((g_cullRow && !g_toOverlay)) return;
    if (nv + 1 > kMaxVerts) { noteDrop(); return; }
    unsigned char c[4]; unpack(col, c);
    SDL_Vertex v; v.color = toColor(c);
    v.position.x = std::isfinite(pos.x) ? pos.x : 0.0f; v.position.y = std::isfinite(pos.y) ? pos.y : 0.0f;
    v.tex_coord.x = std::isfinite(uv.x) ? uv.x : 0.0f; v.tex_coord.y = std::isfinite(uv.y) ? uv.y : 0.0f;
    V[nv++] = v;
}
void DrawList::PrimWriteIdx(ImDrawIdx idx) const {
    ListScope s(overlay);
    int* I = g_toOverlay ? g_ovIdx : g_idx;
    int& ni = g_toOverlay ? g_oni : g_ni;
    const int nv = g_toOverlay ? g_onv : g_nv;
    if ((g_cullRow && !g_toOverlay)) return;
    if (ni + 1 > kMaxIdx) { noteDrop(); return; }
    I[ni++] = idx < (unsigned)nv ? (int)idx : 0;   // an index past the vertices written is clamped (it would read garbage)
}
void DrawList::PrimVtx(Vec2 pos, Vec2 uv, Color32 col) const {
    PrimWriteIdx((ImDrawIdx)(overlay ? g_onv : g_nv));
    PrimWriteVtx(pos, uv, col);
}
void DrawList::PushTexture(const ImTextureRef& tex) const {
    ListScope s(overlay);
    if (g_texTop < 8) g_texStack[g_texTop++] = tex._TexID;
    bindTexture(tex._TexID);
}
void DrawList::PopTexture() const {
    ListScope s(overlay);
    if (g_texTop <= 0) return;
    --g_texTop;
    bindTexture(g_texTop > 0 ? g_texStack[g_texTop - 1] : g_defTex);
}

} // namespace OkayUI

// ---- What belongs to one context (OkayUI_ContextState.hpp: each context keeps its own copy of these) ----
namespace OkayUI {
OKAYUI_CONTEXT_STATE(
    OKAYUI_CTX_VAR(ui_core::g_path),
    OKAYUI_CTX_VAR(ui_core::g_pathN),
    OKAYUI_CTX_VAR(g_texStack),
    OKAYUI_CTX_VAR(g_texTop))
} // namespace OkayUI
