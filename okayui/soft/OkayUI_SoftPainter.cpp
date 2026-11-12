// The software okay::platform::Painter. See OkayUI_SoftPainter.hpp.
#include "OkayUI_SoftPainter.hpp"
#include "OkayUI_SoftRaster.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>
#include <unordered_set>

namespace OkayUI {
namespace soft {
namespace {

using okay::platform::BlendMode;
using okay::platform::PainterKind;
using okay::platform::ScaleMode;
using okay::platform::TextureDesc;
namespace ui2d = okay::ui2d;

constexpr int kMaxDim = 16384;

struct STex {
    int w = 0, h = 0;
    std::vector<std::uint8_t> px;
    BlendMode blend = BlendMode::Blend;
    ScaleMode scale = ScaleMode::Nearest;
    std::uint8_t mr = 255, mg = 255, mb = 255, ma = 255;
    raster::Tex View() const {
        raster::Tex t;
        t.w = w; t.h = h; t.rgba = px.data(); t.linear = scale == ScaleMode::Linear;
        t.modR = mr; t.modG = mg; t.modB = mb; t.modA = ma;
        return t;
    }
};

// Float coordinate to a pixel index; absurd and NaN values are pinned so no int conversion overflows.
inline int Pin(double v) { return !(v == v) ? 0 : (int)std::max(-1e8, std::min(1e8, v)); }

class SoftPainter final : public okay::platform::Painter {
public:
    SoftPainter(okay::platform::Window* win, int w, int h, bool vsync) : m_win(win), m_vsync(vsync) { Resize(w, h); }
    ~SoftPainter() override {
        ReleaseCachedTextures();
        for (STex* t : m_texs) delete t;
    }

    PainterKind Kind() const override { return PainterKind::Software; }
    void* NativeRenderer() override { return nullptr; }

    // ---- target ---------------------------------------------------------------------------
    void OutputSize(int* w, int* h) override { SyncSize(); if (w) *w = m_w; if (h) *h = m_h; }
    void Clear() override {
        SyncSize();
        for (std::size_t i = 0; i + 3 < m_px.size(); i += 4) { m_px[i] = m_r; m_px[i + 1] = m_g; m_px[i + 2] = m_b; m_px[i + 3] = m_a; }
    }
    void SetBlendMode(BlendMode m) override { m_blend = m; }
    BlendMode GetBlendMode() override { return m_blend; }
    void SetDrawColor(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) override { m_r = r; m_g = g; m_b = b; m_a = a; }

    // ---- clip and scale ---------------------------------------------------------------------
    void SetClipRect(const ui2d::Rect* r) override { if (r) { m_clipOn = true; m_clip = *r; } else m_clipOn = false; }
    void GetClipRect(ui2d::Rect* r) override { if (r) *r = m_clipOn ? m_clip : ui2d::Rect{0, 0, 0, 0}; }
    bool IsClipEnabled() override { return m_clipOn; }
    void SetScale(float sx, float sy) override { if (sx > 0 && sy > 0 && std::isfinite(sx) && std::isfinite(sy)) { m_sx = sx; m_sy = sy; } }

    // ---- primitives -------------------------------------------------------------------------
    void FillRect(const ui2d::Rect* r) override {
        if (!r) { FillDev(0, 0, m_w, m_h); return; }
        FillDev(Round(r->x * (double)m_sx), Round(r->y * (double)m_sy),
                Round((r->x + (double)r->w) * m_sx), Round((r->y + (double)r->h) * m_sy));
    }
    void FillRectF(const ui2d::FRect* r) override {
        if (!r) { FillDev(0, 0, m_w, m_h); return; }
        // A pixel is covered when its centre is inside the rectangle.
        FillDev(CeilC(r->x * (double)m_sx), CeilC(r->y * (double)m_sy),
                CeilC((r->x + (double)r->w) * m_sx), CeilC((r->y + (double)r->h) * m_sy));
    }
    void DrawRect(const ui2d::Rect* r) override {
        ui2d::Rect full{0, 0, (int)std::lround(m_w / m_sx), (int)std::lround(m_h / m_sy)};
        const ui2d::Rect rc = r ? *r : full;
        if (rc.w <= 0 || rc.h <= 0) return;
        const int x0 = Round(rc.x * (double)m_sx), y0 = Round(rc.y * (double)m_sy);
        const int x1 = std::max(x0 + 1, Round((rc.x + (double)rc.w) * m_sx)), y1 = std::max(y0 + 1, Round((rc.y + (double)rc.h) * m_sy));
        FillDev(x0, y0, x1, y0 + 1);          // top
        if (y1 - 1 > y0) FillDev(x0, y1 - 1, x1, y1);   // bottom
        if (y1 - y0 > 2) { FillDev(x0, y0 + 1, x0 + 1, y1 - 1); if (x1 - 1 > x0) FillDev(x1 - 1, y0 + 1, x1, y1 - 1); }
    }
    void DrawPoint(int x, int y) override {
        const int dx = Round((x + 0.5) * m_sx - 0.5), dy = Round((y + 0.5) * m_sy - 0.5);
        FillDev(dx, dy, dx + std::max(1, Round(m_sx)), dy + std::max(1, Round(m_sy)));
    }
    void DrawLine(int x0, int y0, int x1, int y1) override { Line(Round(x0 * (double)m_sx), Round(y0 * (double)m_sy), Round(x1 * (double)m_sx), Round(y1 * (double)m_sy)); }
    void DrawLineF(float x0, float y0, float x1, float y1) override { Line(Pin(std::floor(x0 * (double)m_sx)), Pin(std::floor(y0 * (double)m_sy)), Pin(std::floor(x1 * (double)m_sx)), Pin(std::floor(y1 * (double)m_sy))); }

    // ---- textures ---------------------------------------------------------------------------
    ui2d::Tex CreateTexture(const TextureDesc& d, const std::uint8_t* rgba) override {
        if (d.width <= 0 || d.height <= 0 || d.width > kMaxDim || d.height > kMaxDim) return nullptr;
        STex* t = new STex();
        t->w = d.width; t->h = d.height; t->blend = d.blend; t->scale = d.scale;
        t->px.assign((std::size_t)d.width * (std::size_t)d.height * 4, 0);
        if (rgba) std::memcpy(t->px.data(), rgba, t->px.size());
        m_texs.insert(t);
        return t;
    }
    bool UpdateTexture(ui2d::Tex tex, const ui2d::Rect* r, const void* rgba, int pitch) override {
        STex* t = T(tex);
        if (!t || !rgba) return false;
        ui2d::Rect rc{0, 0, t->w, t->h};
        if (r) rc = *r;
        if (rc.x < 0 || rc.y < 0 || rc.w <= 0 || rc.h <= 0 || rc.x + (long long)rc.w > t->w || rc.y + (long long)rc.h > t->h) return false;
        if (pitch < rc.w * 4) return false;
        const std::uint8_t* src = (const std::uint8_t*)rgba;
        for (int y = 0; y < rc.h; ++y)
            std::memcpy(&t->px[((std::size_t)(rc.y + y) * (std::size_t)t->w + (std::size_t)rc.x) * 4], src + (std::size_t)y * (std::size_t)pitch, (std::size_t)rc.w * 4);
        return true;
    }
    void DestroyTexture(ui2d::Tex tex) override {
        STex* t = T(tex);
        if (!t) return;
        m_texs.erase(t);
        delete t;
    }
    void SetTextureBlendMode(ui2d::Tex t, BlendMode m) override { if (STex* s = T(t)) s->blend = m; }
    void SetTextureScaleMode(ui2d::Tex t, ScaleMode m) override { if (STex* s = T(t)) s->scale = m; }
    void QueryTexture(ui2d::Tex t, int* w, int* h) override {
        STex* s = T(t);
        if (w) *w = s ? s->w : 0;
        if (h) *h = s ? s->h : 0;
    }
    void SetTextureColorMod(ui2d::Tex t, std::uint8_t r, std::uint8_t g, std::uint8_t b) override { if (STex* s = T(t)) { s->mr = r; s->mg = g; s->mb = b; } }
    void SetTextureAlphaMod(ui2d::Tex t, std::uint8_t a) override { if (STex* s = T(t)) s->ma = a; }

    void Copy(ui2d::Tex t, const ui2d::Rect* s, const ui2d::Rect* d) override {
        if (d) Blit(t, s, d->x, d->y, d->w, d->h, 0.0, nullptr, 0);
        else Blit(t, s, 0, 0, m_w / m_sx, m_h / m_sy, 0.0, nullptr, 0);
    }
    void CopyF(ui2d::Tex t, const ui2d::Rect* s, const ui2d::FRect* d) override {
        if (d) Blit(t, s, d->x, d->y, d->w, d->h, 0.0, nullptr, 0);
        else Blit(t, s, 0, 0, m_w / m_sx, m_h / m_sy, 0.0, nullptr, 0);
    }
    void CopyEx(ui2d::Tex t, const ui2d::Rect* s, const ui2d::Rect* d, double angle, const ui2d::Point* c, int flip) override {
        ui2d::FPoint fc; if (c) fc = ui2d::FPoint{(float)c->x, (float)c->y};
        if (d) Blit(t, s, d->x, d->y, d->w, d->h, angle, c ? &fc : nullptr, flip);
        else Blit(t, s, 0, 0, m_w / m_sx, m_h / m_sy, angle, c ? &fc : nullptr, flip);
    }
    void CopyExF(ui2d::Tex t, const ui2d::Rect* s, const ui2d::FRect* d, double angle, const ui2d::FPoint* c, int flip) override {
        if (d) Blit(t, s, d->x, d->y, d->w, d->h, angle, c, flip);
        else Blit(t, s, 0, 0, m_w / m_sx, m_h / m_sy, angle, c, flip);
    }

    void Geometry(ui2d::Tex tex, const ui2d::Vertex* v, int nv, const int* idx, int ni) override {
        if (!v || nv <= 0) return;
        STex* st = tex ? T(tex) : nullptr;
        if (tex && !st) return;
        raster::Tex view; if (st) view = st->View();
        const bool blend = (st ? st->blend : m_blend) == BlendMode::Blend;
        const int n = idx ? ni : nv;
        if (n < 3) return;
        raster::Surface surf{m_px.data(), m_w, m_h};
        const raster::Clip clip = DevClip();
        auto V = [&](int i) {
            DrawVertex d;
            d.x = v[i].position.x * m_sx; d.y = v[i].position.y * m_sy;
            d.r = v[i].color.r; d.g = v[i].color.g; d.b = v[i].color.b; d.a = v[i].color.a;
            d.u = v[i].tex_coord.x; d.v = v[i].tex_coord.y;
            return d;
        };
        for (int i = 0; i + 3 <= n; i += 3) {
            const int a = idx ? idx[i] : i, b = idx ? idx[i + 1] : i + 1, c = idx ? idx[i + 2] : i + 2;
            if (a < 0 || b < 0 || c < 0 || a >= nv || b >= nv || c >= nv) continue;
            raster::Triangle(surf, clip, st ? &view : nullptr, blend, V(a), V(b), V(c));
        }
    }

    // ---- the frame --------------------------------------------------------------------------
    void Present() override {
        if (m_win) m_win->PresentPixels(m_px.data(), m_w, m_h);
        if (!m_win || !m_vsync) return;
        // A software present has no display sync: wait for the next refresh period instead of spinning.
        using clock = std::chrono::steady_clock;
        int hz = m_win->RefreshRateHz();
        if (hz < 20 || hz > 360) hz = 60;
        const auto period = std::chrono::microseconds(1000000 / hz);
        const auto now = clock::now();
        auto target = m_lastPresent + period;
        if (m_lastPresent.time_since_epoch().count() != 0 && now < target) { std::this_thread::sleep_until(target); m_lastPresent = target; }
        else m_lastPresent = now;
    }
    bool SetVSync(bool on) override { m_vsync = on; return false; }   // paced, not display-synced
    bool ReadPixels(std::vector<std::uint8_t>& rgba, int& w, int& h) override {
        SyncSize();
        w = m_w; h = m_h; rgba = m_px;
        return !rgba.empty();
    }

private:
    STex* T(ui2d::Tex t) const {
        STex* s = (STex*)t;
        return s && m_texs.count(s) ? s : nullptr;
    }
    static int Round(double v) { return Pin(std::floor(v + 0.5)); }
    static int CeilC(double v) { return Pin(std::ceil(v - 0.5)); }   // first pixel whose centre is >= v

    void Resize(int w, int h) {
        w = std::max(1, std::min(kMaxDim, w)); h = std::max(1, std::min(kMaxDim, h));
        m_w = w; m_h = h;
        m_px.assign((std::size_t)w * (std::size_t)h * 4, 0);
    }
    void SyncSize() {
        if (!m_win) return;
        int w = 0, h = 0;
        m_win->GetDrawableSize(w, h);
        if (w > 0 && h > 0 && (w != m_w || h != m_h)) Resize(w, h);
    }
    raster::Clip DevClip() const {
        raster::Clip c{0, 0, m_w, m_h};
        if (m_clipOn) {
            if (m_clip.w <= 0 || m_clip.h <= 0) return raster::Clip{0, 0, 0, 0};
            const std::int64_t x0 = Round(m_clip.x * (double)m_sx), y0 = Round(m_clip.y * (double)m_sy);
            const std::int64_t x1 = Round((m_clip.x + (double)m_clip.w) * m_sx), y1 = Round((m_clip.y + (double)m_clip.h) * m_sy);
            c.x0 = std::max<std::int64_t>(c.x0, x0); c.y0 = std::max<std::int64_t>(c.y0, y0);
            c.x1 = std::min<std::int64_t>(c.x1, x1); c.y1 = std::min<std::int64_t>(c.y1, y1);
        }
        return c;
    }
    // Fill the device rectangle [x0, x1) x [y0, y1) with the draw colour, clipped.
    void FillDev(int x0, int y0, int x1, int y1) {
        const raster::Clip c = DevClip();
        x0 = (int)std::max<std::int64_t>(x0, c.x0); y0 = (int)std::max<std::int64_t>(y0, c.y0);
        x1 = (int)std::min<std::int64_t>(x1, c.x1); y1 = (int)std::min<std::int64_t>(y1, c.y1);
        if (x0 >= x1 || y0 >= y1) return;
        const bool blend = m_blend == BlendMode::Blend;
        if (blend && m_a == 0) return;
        for (int y = y0; y < y1; ++y) {
            std::uint8_t* d = &m_px[((std::size_t)y * (std::size_t)m_w + (std::size_t)x0) * 4];
            for (int x = x0; x < x1; ++x, d += 4) raster::Put(d, m_r, m_g, m_b, m_a, blend);
        }
    }
    void Line(int x0, int y0, int x1, int y1) {   // device pixels, both ends included
        const raster::Clip c = DevClip();
        const bool blend = m_blend == BlendMode::Blend;
        if (blend && m_a == 0) return;
        const long long dx = std::llabs((long long)x1 - x0), dy = -std::llabs((long long)y1 - y0);
        if (dx > 100000 || -dy > 100000) {   // absurd: clip against the clip box by walking only the visible part is not worth it
            return;
        }
        const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
        long long err = dx + dy;
        for (;;) {
            if (x0 >= c.x0 && x0 < c.x1 && y0 >= c.y0 && y0 < c.y1)
                raster::Put(&m_px[((std::size_t)y0 * (std::size_t)m_w + (std::size_t)x0) * 4], m_r, m_g, m_b, m_a, blend);
            if (x0 == x1 && y0 == y1) break;
            const long long e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }

    // Copy the source rectangle of a texture into the destination rectangle (logical units), optionally rotated
    // about `centre` (relative to the destination's top-left; default its middle) and flipped. A pixel is drawn
    // when its centre is inside the (rotated) destination.
    void Blit(ui2d::Tex tex, const ui2d::Rect* src, double dx, double dy, double dw, double dh, double angle,
              const ui2d::FPoint* centre, int flip) {
        STex* st = T(tex);
        if (!st || !(dw > 0) || !(dh > 0) || !std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(dw) || !std::isfinite(dh)) return;
        ui2d::Rect sr{0, 0, st->w, st->h};
        if (src) sr = *src;
        if (sr.x < 0 || sr.y < 0 || sr.w <= 0 || sr.h <= 0 || sr.x + (long long)sr.w > st->w || sr.y + (long long)sr.h > st->h) return;
        const raster::Tex view = st->View();
        const bool blend = st->blend == BlendMode::Blend;
        const raster::Clip clip = DevClip();
        // device rectangle
        const double X = dx * m_sx, Y = dy * m_sy, W = dw * m_sx, H = dh * m_sy;
        const bool flipH = (flip & 1) != 0, flipV = (flip & 2) != 0;
        const bool modded = view.modR != 255 || view.modG != 255 || view.modB != 255 || view.modA != 255;
        const bool rotated = angle != 0.0 && std::isfinite(angle) && std::fmod(angle, 360.0) != 0.0;

        // The pixel box to visit.
        double bx0, by0, bx1, by1;
        double cx = 0, cy = 0, cs = 1, sn = 0;   // rotation about the centre: inverse applied to each pixel centre
        if (rotated) {
            cx = X + (centre ? centre->x * m_sx : W * 0.5); cy = Y + (centre ? centre->y * m_sy : H * 0.5);
            const double rad = angle * 3.14159265358979323846 / 180.0;
            cs = std::cos(rad); sn = std::sin(rad);
            const double px[4] = {X, X + W, X + W, X}, py[4] = {Y, Y, Y + H, Y + H};
            bx0 = by0 = 1e30; bx1 = by1 = -1e30;
            for (int i = 0; i < 4; ++i) {   // forward rotation of the corners
                const double rx = cx + (px[i] - cx) * cs - (py[i] - cy) * sn, ry = cy + (px[i] - cx) * sn + (py[i] - cy) * cs;
                bx0 = std::min(bx0, rx); bx1 = std::max(bx1, rx); by0 = std::min(by0, ry); by1 = std::max(by1, ry);
            }
        } else { bx0 = X; by0 = Y; bx1 = X + W; by1 = Y + H; }
        std::int64_t x0 = Pin(std::floor(bx0)), y0 = Pin(std::floor(by0)), x1 = Pin(std::ceil(bx1)), y1 = Pin(std::ceil(by1));
        x0 = std::max<std::int64_t>(x0, clip.x0); y0 = std::max<std::int64_t>(y0, clip.y0);
        x1 = std::min<std::int64_t>(x1, clip.x1); y1 = std::min<std::int64_t>(y1, clip.y1);
        if (x0 >= x1 || y0 >= y1) return;

        const double invW = 1.0 / W, invH = 1.0 / H;
        if (!rotated && !view.linear) {
            // The common case (sprites, the 3D frame, UI images): no rotation, nearest. The texel column of each
            // pixel is worked out once for the whole rectangle instead of once per pixel.
            m_cols.resize((std::size_t)(x1 - x0));
            for (std::int64_t x = x0; x < x1; ++x) {
                const double lx = (double)x + 0.5 - X;
                if (lx < 0 || lx >= W) { m_cols[(std::size_t)(x - x0)] = -1; continue; }
                double fu = lx * invW;
                if (flipH) fu = 1.0 - fu;
                m_cols[(std::size_t)(x - x0)] = std::max(0, std::min(st->w - 1, (int)std::floor(sr.x + fu * sr.w)));
            }
            for (std::int64_t y = y0; y < y1; ++y) {
                const double ly = (double)y + 0.5 - Y;
                if (ly < 0 || ly >= H) continue;
                double fv = ly * invH;
                if (flipV) fv = 1.0 - fv;
                const int ty = std::max(0, std::min(st->h - 1, (int)std::floor((sr.y + fv * sr.h))));
                const std::uint8_t* trow = &st->px[(std::size_t)ty * (std::size_t)st->w * 4];
                std::uint8_t* row = &m_px[((std::size_t)y * (std::size_t)m_w) * 4];
                for (std::int64_t x = x0; x < x1; ++x) {
                    const int tx = m_cols[(std::size_t)(x - x0)];
                    if (tx < 0) continue;
                    const std::uint8_t* t = trow + (std::size_t)tx * 4;
                    int r = t[0], g = t[1], b = t[2], a = t[3];
                    if (modded) { r = raster::Div255(r * view.modR); g = raster::Div255(g * view.modG); b = raster::Div255(b * view.modB); a = raster::Div255(a * view.modA); }
                    if (a == 0 && blend) continue;
                    raster::Put(row + (std::size_t)x * 4, r, g, b, a, blend);
                }
            }
            return;
        }
        for (std::int64_t y = y0; y < y1; ++y) {
            std::uint8_t* row = &m_px[((std::size_t)y * (std::size_t)m_w) * 4];
            for (std::int64_t x = x0; x < x1; ++x) {
                double lx = (double)x + 0.5, ly = (double)y + 0.5;
                if (rotated) {   // undo the rotation about the centre
                    const double ox = lx - cx, oy = ly - cy;
                    lx = cx + ox * cs + oy * sn; ly = cy - ox * sn + oy * cs;
                }
                lx -= X; ly -= Y;
                if (lx < 0 || ly < 0 || lx >= W || ly >= H) continue;
                double fu = lx * invW, fv = ly * invH;
                if (flipH) fu = 1.0 - fu;
                if (flipV) fv = 1.0 - fv;
                const double u = (sr.x + fu * sr.w) / st->w, v = (sr.y + fv * sr.h) / st->h;
                int t[4];
                raster::Sample(view, u, v, t);
                int r = t[0], g = t[1], b = t[2], a = t[3];
                if (modded) { r = raster::Div255(r * view.modR); g = raster::Div255(g * view.modG); b = raster::Div255(b * view.modB); a = raster::Div255(a * view.modA); }
                if (a == 0 && blend) continue;
                raster::Put(row + (std::size_t)x * 4, r, g, b, a, blend);
            }
        }
    }

    okay::platform::Window* m_win;
    bool m_vsync;
    int m_w = 0, m_h = 0;
    std::vector<std::uint8_t> m_px;
    std::uint8_t m_r = 0, m_g = 0, m_b = 0, m_a = 255;
    BlendMode m_blend = BlendMode::None;     // SDL's default draw blend mode
    bool m_clipOn = false;
    ui2d::Rect m_clip{0, 0, 0, 0};
    float m_sx = 1.0f, m_sy = 1.0f;
    std::unordered_set<STex*> m_texs;
    std::vector<int> m_cols;   // Blit scratch: the source column of each destination pixel
    std::chrono::steady_clock::time_point m_lastPresent{};
};

} // namespace

std::unique_ptr<okay::platform::Painter> CreateBitmapPainter(int w, int h) {
    if (w < 1 || h < 1 || w > kMaxDim || h > kMaxDim) return nullptr;
    return std::unique_ptr<okay::platform::Painter>(new SoftPainter(nullptr, w, h, false));
}

std::unique_ptr<okay::platform::Painter> CreateWindowPainter(okay::platform::Window& w, const okay::platform::PainterDesc& d, std::string* err) {
    if (w.Kind() == okay::platform::BackendKind::Sdl2) {
        if (err) *err = "an sdl2 window is drawn through SDL_Renderer; the software painter needs a window of the own backend";
        return nullptr;
    }
    int dw = 0, dh = 0;
    w.GetDrawableSize(dw, dh);
    if (dw < 1 || dh < 1) { if (err) *err = "the window has no drawable size"; return nullptr; }
    return std::unique_ptr<okay::platform::Painter>(new SoftPainter(&w, dw, dh, d.vsync));
}

// ---- OkayUI's frame through any painter ---------------------------------------------------------
FrameDrawer::~FrameDrawer() { if (m_atlas) m_p.DestroyTexture(m_atlas); }

void FrameDrawer::Draw(const DrawData& data) {
    static_assert(sizeof(DrawVertex) == sizeof(ui2d::Vertex), "DrawVertex must match ui2d::Vertex");
    if (!data.vertices || !data.indices || data.vertexCount <= 0 || data.indexCount <= 0 || data.cmdCount <= 0 || !data.cmds) return;
    if (data.vertexStride != (int)sizeof(DrawVertex)) return;
    // The launcher look's atlas: one painter texture, linear like the GPU backend, kept while the atlas stays the same.
    if (data.atlasTexture && data.atlasRgba && data.atlasW > 0 && data.atlasH > 0) {
        if (m_atlas && (m_atlasKey != data.atlasTexture || m_atlasW != data.atlasW || m_atlasH != data.atlasH)) { m_p.DestroyTexture(m_atlas); m_atlas = nullptr; }
        if (!m_atlas) {
            okay::platform::TextureDesc td; td.width = data.atlasW; td.height = data.atlasH; td.scale = okay::platform::ScaleMode::Linear;
            m_atlas = m_p.CreateTexture(td, data.atlasRgba);
            m_atlasKey = data.atlasTexture; m_atlasW = data.atlasW; m_atlasH = data.atlasH;
        }
    }
    const okay::platform::BlendMode savedBlend = m_p.GetBlendMode();
    const bool hadClip = m_p.IsClipEnabled();
    ui2d::Rect savedClip{0, 0, 0, 0};
    m_p.GetClipRect(&savedClip);
    m_p.SetBlendMode(okay::platform::BlendMode::Blend);
    const ui2d::Vertex* vtx = (const ui2d::Vertex*)data.vertices;
    for (int i = 0; i < data.cmdCount; ++i) {
        const DrawCmd& c = data.cmds[i];
        if (c.idxCount < 3 || c.idxOffset < 0 || (long long)c.idxOffset + c.idxCount > data.indexCount) continue;
        ui2d::Tex tex = nullptr;
        if (c.texture) {
            if (c.texture == data.atlasTexture) { tex = m_atlas; if (!tex) continue; }
            else if (okay::TtfFont* f = FallbackFontOf(c.texture)) { tex = m_p.FontAtlas(f); if (!tex) continue; }
            else tex = const_cast<void*>(c.texture);   // a texture the app made on this painter (OkayUI::Image)
        }
        if (c.clipOn) { const ui2d::Rect r{c.clipX, c.clipY, c.clipW, c.clipH}; m_p.SetClipRect(&r); }
        else m_p.SetClipRect(nullptr);
        m_p.Geometry(tex, vtx, data.vertexCount, data.indices + c.idxOffset, c.idxCount);
    }
    m_p.SetClipRect(hadClip ? &savedClip : nullptr);
    m_p.SetBlendMode(savedBlend);
}

} // namespace soft
} // namespace OkayUI
