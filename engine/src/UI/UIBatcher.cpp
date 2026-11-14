#include "okay/UI/UIBatcher.hpp"
#include "okay/Graphics/TtfFont.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace okay {
namespace ui2d {

namespace {
constexpr int kFirstPageSize = 1024;
constexpr int kMaxPageSize = 4096;
constexpr int kMaxAtlasImage = 512;   // larger images keep their own texture
constexpr int kPad = 1;               // transparent gutter around each atlas entry

RGBA Modulate(RGBA a, RGBA b) {
    auto m = [](std::uint8_t x, std::uint8_t y) { return (std::uint8_t)((x * y + 127) / 255); };
    return RGBA{m(a.r, b.r), m(a.g, b.g), m(a.b, b.b), m(a.a, b.a)};
}
} // namespace

BatchPainter::BatchPainter(Painter& backend, ImageLoader loader)
    : m_b(backend), m_load(std::move(loader)) {
    if (!m_load) m_load = [](const std::string& path, Image& out) { return out.Load(path); };
    m_v.reserve(4096);
    m_i.reserve(6144);
}

// ---- Atlas pages ------------------------------------------------------------------

bool BatchPainter::CreatePage(int page) {
    if (m_passthrough) return false;
    Page& pg = m_pages[page];
    pg.img = Image(kFirstPageSize, kFirstPageSize);   // transparent
    // A white block for untextured shapes (sampled at its centre texel).
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x) pg.img.SetPixel(kPad + x, kPad + y, Color::White);
    pg.whiteX = kPad + 1; pg.whiteY = kPad + 1;
    pg.shelfX = kPad * 2 + 3; pg.shelfY = 0; pg.shelfH = kPad * 2 + 3;
    // Probe the backend: no atlas support means pass everything straight through.
    pg.tex = m_b.UploadAtlas(pg.img, page, nullptr);
    pg.dirty = false;
    if (!pg.tex) { m_passthrough = true; pg.img = Image(); return false; }
    return true;
}

bool BatchPainter::Place(int page, int w, int h, int& x, int& y) {
    Page& pg = m_pages[page];
    const int pw = w + kPad * 2, ph = h + kPad * 2;
    for (;;) {
        const int W = pg.img.Width(), H = pg.img.Height();
        if (pw > W) return false;
        if (pg.shelfX + pw > W) { pg.shelfY += pg.shelfH; pg.shelfX = 0; pg.shelfH = 0; }
        if (pg.shelfY + ph <= H) {
            x = pg.shelfX + kPad; y = pg.shelfY + kPad;
            pg.shelfX += pw; pg.shelfH = std::max(pg.shelfH, ph);
            return true;
        }
        // Out of room: double the page, keeping every entry where it is (UVs are
        // computed from pixel positions at draw time). Pending triangles of this page
        // carry the old UVs, so submit them first.
        if (W * 2 > kMaxPageSize) return false;
        if (m_batchPage == page) Flush();
        Image bigger(W * 2, H * 2);
        for (int row = 0; row < H; ++row)
            std::memcpy(bigger.Data() + (std::size_t)row * W * 2 * 4, pg.img.Data() + (std::size_t)row * W * 4,
                        (std::size_t)W * 4);
        pg.img = std::move(bigger);
        pg.dirty = true;
    }
}

void BatchPainter::Blit(Page& pg, const Image& src, int x, int y) {
    const int W = pg.img.Width();
    for (int row = 0; row < src.Height(); ++row)
        std::memcpy(pg.img.Data() + ((std::size_t)(y + row) * W + x) * 4, src.Data() + (std::size_t)row * src.Width() * 4,
                    (std::size_t)src.Width() * 4);
    pg.dirty = true;
}

Tex BatchPainter::LoadTexture(const std::string& path) {
    if (path.empty()) return nullptr;
    if (m_passthrough) return m_b.LoadTexture(path);
    auto it = m_byPath.find(path);
    if (it != m_byPath.end()) return it->second;
    Entry* e = nullptr;
    Image img;
    if (m_load(path, img) && img.Width() > 0 && img.Height() > 0) {
        int x = 0, y = 0;
        if (img.Width() <= kMaxAtlasImage && img.Height() <= kMaxAtlasImage && EnsurePage(1) &&
            Place(1, img.Width(), img.Height(), x, y)) {
            m_entries.push_back(Entry{});
            e = &m_entries.back();
            e->page = 1; e->x = x; e->y = y; e->w = img.Width(); e->h = img.Height();
            Blit(m_pages[1], img, x, y);
        } else if (Tex own = m_b.LoadTexture(path)) {   // too big: the backend's own texture
            m_entries.push_back(Entry{});
            e = &m_entries.back();
            e->own = own; e->w = img.Width(); e->h = img.Height();
        }
    }
    if (m_passthrough) return m_b.LoadTexture(path);
    m_byPath.emplace(path, e);   // remember misses too, so a bad path isn't re-read every frame
    return e;
}

Tex BatchPainter::FontAtlas(TtfFont* font) {
    if (!font || !font->Valid()) return nullptr;
    if (m_passthrough) return m_b.FontAtlas(font);
    const Image& a = font->Atlas();
    auto it = m_byFont.find(font);
    if (it != m_byFont.end()) {
        Entry* e = it->second;
        std::uint32_t& gen = m_fontGen[font];
        if (!e || gen == font->Generation()) return e;
        gen = font->Generation();   // the font baked new characters: copy its atlas again
        int x = 0, y = 0;
        if (e->page == 0 && e->w == a.Width() && e->h == a.Height()) Blit(m_pages[0], a, e->x, e->y);
        else if (e->page == 0 && Place(0, a.Width(), a.Height(), x, y)) {   // it grew: a new spot
            e->x = x; e->y = y; e->w = a.Width(); e->h = a.Height();
            Blit(m_pages[0], a, x, y);
        } else {   // too big for the page now: the backend's own texture
            e->page = -1; e->own = m_b.FontAtlas(font); e->w = a.Width(); e->h = a.Height();
        }
        return e;
    }
    m_fontGen[font] = font->Generation();
    Entry* e = nullptr;
    int x = 0, y = 0;
    if (EnsurePage(0) && Place(0, a.Width(), a.Height(), x, y)) {
        m_entries.push_back(Entry{});
        e = &m_entries.back();
        e->page = 0; e->x = x; e->y = y; e->w = a.Width(); e->h = a.Height();
        Blit(m_pages[0], a, x, y);
    } else if (!m_passthrough) {
        if (Tex own = m_b.FontAtlas(font)) {
            m_entries.push_back(Entry{});
            e = &m_entries.back();
            e->own = own; e->w = a.Width(); e->h = a.Height();
        }
    }
    if (m_passthrough) return m_b.FontAtlas(font);
    m_byFont.emplace(font, e);
    return e;
}

void BatchPainter::QueryTexture(Tex t, int* w, int* h) {
    if (m_passthrough) { m_b.QueryTexture(t, w, h); return; }
    const Entry* e = static_cast<const Entry*>(t);
    if (w) *w = e ? e->w : 0;
    if (h) *h = e ? e->h : 0;
}

void BatchPainter::SetTextureColorMod(Tex t, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    if (m_passthrough) { m_b.SetTextureColorMod(t, r, g, b); return; }
    if (Entry* e = static_cast<Entry*>(t)) { e->mod.r = r; e->mod.g = g; e->mod.b = b; }
}

void BatchPainter::SetTextureAlphaMod(Tex t, std::uint8_t a) {
    if (m_passthrough) { m_b.SetTextureAlphaMod(t, a); return; }
    if (Entry* e = static_cast<Entry*>(t)) e->mod.a = a;
}

// ---- Batch building ---------------------------------------------------------------

void BatchPainter::UseTexture(int page) {
    if (m_batchPage != page && !m_i.empty()) Flush();
    m_batchPage = page;
}

void BatchPainter::Flush() {
    if (m_i.empty() || m_batchPage < 0) {   // nothing pending: still hand over a cleared clip
        m_batchPage = -1;
        if (!m_passthrough) ApplyBackendClip();
        return;
    }
    Page& pg = m_pages[m_batchPage];
    if (pg.dirty) { pg.tex = m_b.UploadAtlas(pg.img, m_batchPage, pg.tex); pg.dirty = false; }
    ApplyBackendClip();
    m_b.Geometry(pg.tex, m_v.data(), (int)m_v.size(), m_i.data(), (int)m_i.size());
    m_v.clear(); m_i.clear();
    m_batchPage = -1;
}

void BatchPainter::ApplyBackendClip() {
    Rect want{0, 0, 0, 0};
    if (m_clipOn) {   // the renderer's clip is in logical units; the backend draws in pixels
        want.x = (int)std::floor(m_clip.x * m_scale);
        want.y = (int)std::floor(m_clip.y * m_scale);
        want.w = (int)std::ceil((m_clip.x + m_clip.w) * m_scale) - want.x;
        want.h = (int)std::ceil((m_clip.y + m_clip.h) * m_scale) - want.y;
    }
    if (m_clipOn == m_bClipOn && (!m_clipOn || (want.x == m_bClip.x && want.y == m_bClip.y &&
                                                want.w == m_bClip.w && want.h == m_bClip.h)))
        return;
    m_bClipOn = m_clipOn; m_bClip = want;
    m_b.SetClipRect(m_clipOn ? &want : nullptr);
}

void BatchPainter::Quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, RGBA c) {
    const int base = (int)m_v.size();
    const float s = m_scale;
    m_v.resize(m_v.size() + 4);
    Vertex* q = m_v.data() + base;
    q[0] = Vertex{FPoint{x0 * s, y0 * s}, c, FPoint{u0, v0}};
    q[1] = Vertex{FPoint{x1 * s, y0 * s}, c, FPoint{u1, v0}};
    q[2] = Vertex{FPoint{x1 * s, y1 * s}, c, FPoint{u1, v1}};
    q[3] = Vertex{FPoint{x0 * s, y1 * s}, c, FPoint{u0, v1}};
    const std::size_t ib = m_i.size();
    m_i.resize(ib + 6);
    int* ix = m_i.data() + ib;
    ix[0] = base; ix[1] = base + 1; ix[2] = base + 2; ix[3] = base; ix[4] = base + 2; ix[5] = base + 3;
}

void BatchPainter::WhiteQuad(float x, float y, float w, float h) {
    if (w <= 0.0f || h <= 0.0f) return;
    // Untextured shapes join whatever page is pending (both have a white texel).
    const int page = m_batchPage >= 0 ? m_batchPage : 0;
    if (!EnsurePage(page)) return;
    UseTexture(page);
    const Page& pg = m_pages[page];
    const float u = U(page, pg.whiteX + 0.5f), v = V(page, pg.whiteY + 0.5f);
    Quad(x, y, x + w, y + h, u, v, u, v, m_color);
}

// ---- Untextured primitives ----------------------------------------------------------

void BatchPainter::SetDrawColor(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    m_color = RGBA{r, g, b, a};
    if (m_passthrough) m_b.SetDrawColor(r, g, b, a);
}

void BatchPainter::FillRect(const Rect* r) {
    if (!r) return;
    if (m_passthrough || !EnsurePage(m_batchPage >= 0 ? m_batchPage : 0)) { m_b.FillRect(r); return; }
    WhiteQuad((float)r->x, (float)r->y, (float)r->w, (float)r->h);
}

// SDL outlines the border pixels inside the rect, one pixel wide.
void BatchPainter::DrawRect(const Rect* r) {
    if (!r || r->w <= 0 || r->h <= 0) return;
    if (m_passthrough || !EnsurePage(m_batchPage >= 0 ? m_batchPage : 0)) { m_b.DrawRect(r); return; }
    const float x = (float)r->x, y = (float)r->y, w = (float)r->w, h = (float)r->h;
    WhiteQuad(x, y, w, 1.0f);
    if (h > 1.0f) WhiteQuad(x, y + h - 1.0f, w, 1.0f);
    if (h > 2.0f) { WhiteQuad(x, y + 1.0f, 1.0f, h - 2.0f); if (w > 1.0f) WhiteQuad(x + w - 1.0f, y + 1.0f, 1.0f, h - 2.0f); }
}

// SDL lines include both end pixels. Axis-aligned ones (the scanline fills of
// circles and triangles) are one rect; diagonals step like SDL's Bresenham, one
// rect per run of pixels on the same row (or column), so they rasterize alike.
void BatchPainter::DrawLine(int x0, int y0, int x1, int y1) {
    if (m_passthrough || !EnsurePage(m_batchPage >= 0 ? m_batchPage : 0)) { m_b.DrawLine(x0, y0, x1, y1); return; }
    if (y0 == y1) { WhiteQuad((float)std::min(x0, x1), (float)y0, (float)(std::abs(x1 - x0) + 1), 1.0f); return; }
    if (x0 == x1) { WhiteQuad((float)x0, (float)std::min(y0, y1), 1.0f, (float)(std::abs(y1 - y0) + 1)); return; }
    const int dx = std::abs(x1 - x0), dy = std::abs(y1 - y0);
    const bool xMajor = dx >= dy;
    const int n = (xMajor ? dx : dy) + 1;
    int d = xMajor ? 2 * dy - dx : 2 * dx - dy;
    const int dinc1 = xMajor ? dy * 2 : dx * 2, dinc2 = xMajor ? (dy - dx) * 2 : (dx - dy) * 2;
    const int sx = x0 > x1 ? -1 : 1, sy = y0 > y1 ? -1 : 1;
    int x = x0, y = y0;
    int runX = x, runY = y, runLen = 0;   // pixels along the major axis since the last minor step
    auto emit = [&]() {
        if (runLen <= 0) return;
        if (xMajor) WhiteQuad((float)(sx > 0 ? runX : runX - runLen + 1), (float)runY, (float)runLen, 1.0f);
        else        WhiteQuad((float)runX, (float)(sy > 0 ? runY : runY - runLen + 1), 1.0f, (float)runLen);
    };
    for (int i = 0; i < n; ++i) {
        if (runLen == 0) { runX = x; runY = y; }
        ++runLen;
        if (d < 0) {
            d += dinc1;
            if (xMajor) x += sx; else y += sy;
        } else {
            d += dinc2;
            x += sx; y += sy;
            emit();
            runLen = 0;
        }
    }
    emit();
}

void BatchPainter::DrawPoint(int x, int y) {
    if (m_passthrough || !EnsurePage(m_batchPage >= 0 ? m_batchPage : 0)) { m_b.DrawPoint(x, y); return; }
    WhiteQuad((float)x, (float)y, 1.0f, 1.0f);
}

// ---- Clip and scale -------------------------------------------------------------------

void BatchPainter::SetClipRect(const Rect* r) {
    if (m_passthrough) { m_clipOn = r != nullptr; if (r) m_clip = *r; m_b.SetClipRect(r); return; }
    const bool on = r != nullptr;
    if (on == m_clipOn && (!on || (r->x == m_clip.x && r->y == m_clip.y && r->w == m_clip.w && r->h == m_clip.h)))
        return;   // unchanged (the renderer resets the clip for every widget)
    Flush();      // the pending triangles were clipped by the old rect
    m_clipOn = on;
    if (on) m_clip = *r;
}

void BatchPainter::GetClipRect(Rect* r) {
    if (m_passthrough) { m_b.GetClipRect(r); return; }
    if (r) *r = m_clipOn ? m_clip : Rect{0, 0, 0, 0};
}

void BatchPainter::SetScale(float sx, float sy) {
    if (m_passthrough) { m_b.SetScale(sx, sy); return; }
    // Applied to the vertices here, so a scale change doesn't break the batch; the
    // backend keeps drawing at scale 1.
    if (m_clipOn && sx != m_scale) Flush();
    m_scale = sx > 0.0f ? sx : 1.0f;
}

// ---- Textured primitives ----------------------------------------------------------------

void BatchPainter::ForwardCopy(Entry& e, const Rect* src, float x, float y, float w, float h, double angle,
                               const FPoint* center, int flip) {
    Flush();
    ApplyBackendClip();
    m_b.SetTextureColorMod(e.own, e.mod.r, e.mod.g, e.mod.b);
    m_b.SetTextureAlphaMod(e.own, e.mod.a);
    FRect d{x * m_scale, y * m_scale, w * m_scale, h * m_scale};
    FPoint c{};
    if (center) c = FPoint{center->x * m_scale, center->y * m_scale};
    m_b.CopyExF(e.own, src, &d, angle, center ? &c : nullptr, flip);
}

void BatchPainter::Image4(Entry& e, const Rect* src, float x, float y, float w, float h, double angle,
                          const FPoint* center, int flip) {
    if (w <= 0.0f || h <= 0.0f) return;
    if (e.page < 0) { ForwardCopy(e, src, x, y, w, h, angle, center, flip); return; }
    UseTexture(e.page);
    const int sx = src ? src->x : 0, sy = src ? src->y : 0;
    const int sw = src ? src->w : e.w, sh = src ? src->h : e.h;
    float u0 = U(e.page, (float)(e.x + sx)), v0 = V(e.page, (float)(e.y + sy));
    float u1 = U(e.page, (float)(e.x + sx + sw)), v1 = V(e.page, (float)(e.y + sy + sh));
    if (flip & FlipH) std::swap(u0, u1);
    if (flip & FlipV) std::swap(v0, v1);
    if (angle == 0.0) { Quad(x, y, x + w, y + h, u0, v0, u1, v1, e.mod); return; }
    const float a = (float)(angle * 3.14159265358979 / 180.0), c = std::cos(a), sn = std::sin(a);
    const float cx = x + (center ? center->x : w * 0.5f), cy = y + (center ? center->y : h * 0.5f);
    auto rot = [&](float px, float py, float u, float v) {
        const float dx = px - cx, dy = py - cy;
        return Vertex{FPoint{(cx + dx * c - dy * sn) * m_scale, (cy + dx * sn + dy * c) * m_scale}, e.mod, FPoint{u, v}};
    };
    const int base = (int)m_v.size();
    m_v.push_back(rot(x, y, u0, v0));
    m_v.push_back(rot(x + w, y, u1, v0));
    m_v.push_back(rot(x + w, y + h, u1, v1));
    m_v.push_back(rot(x, y + h, u0, v1));
    const int ix[6] = {base, base + 1, base + 2, base, base + 2, base + 3};
    m_i.insert(m_i.end(), ix, ix + 6);
}

void BatchPainter::Copy(Tex t, const Rect* src, const Rect* dst) {
    if (m_passthrough) { m_b.Copy(t, src, dst); return; }
    if (t && dst) Image4(*static_cast<Entry*>(t), src, (float)dst->x, (float)dst->y, (float)dst->w, (float)dst->h, 0.0, nullptr, FlipNone);
}

void BatchPainter::CopyF(Tex t, const Rect* src, const FRect* dst) {
    if (m_passthrough) { m_b.CopyF(t, src, dst); return; }
    if (t && dst) Image4(*static_cast<Entry*>(t), src, dst->x, dst->y, dst->w, dst->h, 0.0, nullptr, FlipNone);
}

void BatchPainter::CopyEx(Tex t, const Rect* src, const Rect* dst, double angle, const Point* center, int flip) {
    if (m_passthrough) { m_b.CopyEx(t, src, dst, angle, center, flip); return; }
    if (!t || !dst) return;
    FPoint c{};
    if (center) c = FPoint{(float)center->x, (float)center->y};
    Image4(*static_cast<Entry*>(t), src, (float)dst->x, (float)dst->y, (float)dst->w, (float)dst->h, angle,
           center ? &c : nullptr, flip);
}

void BatchPainter::CopyExF(Tex t, const Rect* src, const FRect* dst, double angle, const FPoint* center, int flip) {
    if (m_passthrough) { m_b.CopyExF(t, src, dst, angle, center, flip); return; }
    if (t && dst) Image4(*static_cast<Entry*>(t), src, dst->x, dst->y, dst->w, dst->h, angle, center, flip);
}

void BatchPainter::Geometry(Tex t, const Vertex* v, int nv, const int* idx, int ni) {
    if (m_passthrough) { m_b.Geometry(t, v, nv, idx, ni); return; }
    if (!v || nv <= 0) return;
    Entry* e = static_cast<Entry*>(t);
    if (e && e->page < 0) {   // the backend's own texture: submit as is (scaled)
        Flush();
        ApplyBackendClip();
        m_b.SetTextureColorMod(e->own, e->mod.r, e->mod.g, e->mod.b);
        m_b.SetTextureAlphaMod(e->own, e->mod.a);
        m_scaled.assign(v, v + nv);
        for (Vertex& q : m_scaled) { q.position.x *= m_scale; q.position.y *= m_scale; }
        m_b.Geometry(e->own, m_scaled.data(), nv, idx, ni);
        return;
    }
    const int page = e ? e->page : (m_batchPage >= 0 ? m_batchPage : 0);
    if (!EnsurePage(page)) { m_b.Geometry(t, v, nv, idx, ni); return; }
    UseTexture(page);
    const Page& pg = m_pages[page];
    const int base = (int)m_v.size();
    for (int i = 0; i < nv; ++i) {
        Vertex q = v[i];
        q.position.x *= m_scale; q.position.y *= m_scale;
        if (e) {   // the entry's 0..1 UVs into the page
            q.tex_coord.x = U(page, e->x + q.tex_coord.x * e->w);
            q.tex_coord.y = V(page, e->y + q.tex_coord.y * e->h);
            q.color = Modulate(q.color, e->mod);   // like a texture's colour/alpha mod
        } else {
            q.tex_coord = FPoint{U(page, pg.whiteX + 0.5f), V(page, pg.whiteY + 0.5f)};
        }
        m_v.push_back(q);
    }
    if (idx) for (int i = 0; i < ni; ++i) m_i.push_back(base + idx[i]);
    else     for (int i = 0; i < nv; ++i) m_i.push_back(base + i);
}

} // namespace ui2d
} // namespace okay
