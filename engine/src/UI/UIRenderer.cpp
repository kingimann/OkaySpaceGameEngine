// The in-game UI pass and its drawing helpers, moved verbatim from the player's main
// loop (player/main.cpp) onto the ui2d::Painter interface. See UIRenderer.hpp.
#include "okay/UI/UIRenderer.hpp"
#include "UIRendererInternal.hpp"
#include "okay/UI/UIIconDraw.hpp"   // "icon:name" pictures: our own vector icons
#include "okay/UI/UIWidgetsMoreDraw.hpp"
#include "okay/UI/UIFontPolicy.hpp"
#include "okay/UI/UIImageScale.hpp"
#include "okay/UI/UIFrameTransform.hpp"
#include "okay/UI/TooltipPlace.hpp"
#include <Okay.hpp>
#include "okay/Core/Utf8.hpp"
#include "okay/Core/AccessibilitySettings.hpp"
#include "okay/Components/CaptionOverlay.hpp"
#include "okay/UI/UIFocus.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <unordered_map>
#include <utility>
#include <string>
#include <vector>

namespace okay {
namespace ui2d {

float ui_pass::g_uiDeviceScale = 1.0f;
bool ui_pass::g_uiSmoothEdges = true;
UIQuality ui_pass::g_uiQuality = UIQuality::Good;   // set per pass from the scene (Scene::uiQuality) and the environment
TtfFont* ui_pass::g_uiDefaultFont = nullptr;
using ui_pass::g_uiDeviceScale;
using ui_pass::g_uiSmoothEdges;
using ui_pass::g_uiQuality;
using ui_pass::g_uiDefaultFont;
using ui_pass::UITextScale;
using ui_pass::RichLinkTable;
using ui_pass::DrawRichRuns;
using ui_pass::RenderSceneUIImpl;

namespace {
// The player's text size (Accessibility), for the widgets whose text can grow without a fixed slot.

// ---- Accessibility: colour vision filter and high-contrast text ----------------------------------
// The most recent large solid fill, as drawn (before any colour filter): what high-contrast text is
// measured against. Thin pieces (borders, text pixels) don't count.
Color g_backdrop{0.06f, 0.06f, 0.08f, 1.0f};
bool g_inText = false;
// Canvas pixels per logical pixel for the widget currently being drawn, and whether
// edges are anti-aliased. "pixel" scenes keep the hard bitmap look.

// Wraps the frame's painter so every flat colour and vertex colour the UI pass sets is shifted for the
// player's colour vision (Accessibility::FilterColor). Images are drawn as they are.
class A11yPainter final : public Painter {
public:
    explicit A11yPainter(Painter& in) : m_in(in) {}
    void SetDrawColor(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) override {
        m_c = Color::FromBytes(r, g, b, 255); m_a = a;
        std::uint8_t fr, fg, fb; Filter(r, g, b, fr, fg, fb);
        m_in.SetDrawColor(fr, fg, fb, a);
    }
    void FillRect(const Rect* r) override {
        if (r && !g_inText && m_a >= 128 && std::min(r->w, r->h) >= 8) g_backdrop = m_c;
        m_in.FillRect(r);
    }
    void FillRectF(const FRect* r) override {
        if (r && !g_inText && m_a >= 128 && std::min(r->w, r->h) >= 8.0f) g_backdrop = m_c;
        m_in.FillRectF(r);
    }
    void DrawRect(const Rect* r) override { m_in.DrawRect(r); }
    void DrawLine(int x0, int y0, int x1, int y1) override { m_in.DrawLine(x0, y0, x1, y1); }
    void DrawPoint(int x, int y) override { m_in.DrawPoint(x, y); }
    void SetClipRect(const Rect* r) override { m_in.SetClipRect(r); }
    void GetClipRect(Rect* r) override { m_in.GetClipRect(r); }
    bool IsClipEnabled() override { return m_in.IsClipEnabled(); }
    void SetScale(float sx, float sy) override { m_in.SetScale(sx, sy); }
    Tex LoadTexture(const std::string& path) override { return m_in.LoadTexture(path); }
    Tex LoadTextureSized(const std::string& path, int w, int h) override { return m_in.LoadTextureSized(path, w, h); }
    Tex FontAtlas(TtfFont* font) override { return m_in.FontAtlas(font); }
    void QueryTexture(Tex t, int* w, int* h) override { m_in.QueryTexture(t, w, h); }
    void SetTextureColorMod(Tex t, std::uint8_t r, std::uint8_t g, std::uint8_t b) override {
        std::uint8_t fr, fg, fb; Filter(r, g, b, fr, fg, fb);
        m_in.SetTextureColorMod(t, fr, fg, fb);
    }
    void SetTextureAlphaMod(Tex t, std::uint8_t a) override { m_in.SetTextureAlphaMod(t, a); }
    void Copy(Tex t, const Rect* s, const Rect* d) override { m_in.Copy(t, s, d); }
    void CopyF(Tex t, const Rect* s, const FRect* d) override { m_in.CopyF(t, s, d); }
    void CopyEx(Tex t, const Rect* s, const Rect* d, double a, const Point* c, int f) override { m_in.CopyEx(t, s, d, a, c, f); }
    void CopyExF(Tex t, const Rect* s, const FRect* d, double a, const FPoint* c, int f) override { m_in.CopyExF(t, s, d, a, c, f); }
    void Geometry(Tex t, const Vertex* v, int nv, const int* idx, int ni) override {
        m_verts.assign(v, v + nv);
        for (Vertex& x : m_verts) Filter(x.color.r, x.color.g, x.color.b, x.color.r, x.color.g, x.color.b);
        m_in.Geometry(t, m_verts.data(), nv, idx, ni);
    }
    void MissingTexture(const std::string& path, const Rect& where) override { m_in.MissingTexture(path, where); }
    void SetImageFilter(bool linear) override { m_in.SetImageFilter(linear); }
    void SetTextureFilter(Tex t, bool linear) override { m_in.SetTextureFilter(t, linear); }
    Tex UploadAtlas(const Image& page, int pageIndex, Tex existing) override { return m_in.UploadAtlas(page, pageIndex, existing); }

private:
    void Filter(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t& orr, std::uint8_t& og, std::uint8_t& ob) {
        const std::uint32_t key = ((std::uint32_t)r << 16) | ((std::uint32_t)g << 8) | b;
        if (!m_have || key != m_key) {   // the same colour is set many times in a row
            const Color f = Accessibility::FilterColor(Color::FromBytes(r, g, b, 255));
            m_key = key; m_have = true;
            m_out[0] = (std::uint8_t)(f.r * 255.0f + 0.5f); m_out[1] = (std::uint8_t)(f.g * 255.0f + 0.5f); m_out[2] = (std::uint8_t)(f.b * 255.0f + 0.5f);
        }
        orr = m_out[0]; og = m_out[1]; ob = m_out[2];
    }
    Painter& m_in;
    Color m_c{0, 0, 0, 1};
    std::uint8_t m_a = 255;
    std::uint32_t m_key = 0;
    bool m_have = false;
    std::uint8_t m_out[3] = {0, 0, 0};
    std::vector<Vertex> m_verts;
};

// Collects one-pixel-tall spans and draws runs of identical rows (same x, width and
// colour) as one rect: a rounded panel is a handful of rects instead of one per row.
struct RowRuns {
    Painter& ren;
    Rect run{0, 0, 0, 0};
    std::uint8_t rgba[4] = {0, 0, 0, 0};
    void Add(int x, int y, int w, const std::uint8_t c[4]) {
        if (run.h > 0 && x == run.x && w == run.w && y == run.y + run.h &&
            c[0] == rgba[0] && c[1] == rgba[1] && c[2] == rgba[2] && c[3] == rgba[3]) {
            ++run.h;
            return;
        }
        Flush();
        run = Rect{x, y, w, 1};
        for (int i = 0; i < 4; ++i) rgba[i] = c[i];
    }
    void Flush() {
        if (run.h <= 0) return;
        ren.SetDrawColor(rgba[0], rgba[1], rgba[2], rgba[3]);
        ren.FillRect(&run);
        run.h = 0;
    }
};
} // namespace

namespace {

// How much of pixel [px, px+1) lies inside the span [a, b).
float SpanOverlap(float a, float b, int px) {
    const float x0 = (float)px, x1 = x0 + 1.0f;
    const float lo = a > x0 ? a : x0;
    const float hi = b < x1 ? b : x1;
    return hi > lo ? hi - lo : 0.0f;
}

// One rectangle of a recorded shape (relative to the shape's origin, alpha 255 = the shape's own colour).
struct RecRect { float x, y, w, h; std::uint8_t a; };

struct FloatRun {
    Painter* ren = nullptr;
    std::vector<RecRect>* rec = nullptr;   // when set, rectangles are recorded instead of drawn
    float x = 0, y = 0, w = 0, h = 0;
    std::uint8_t c[4] = {};
    bool on = false;
    void Flush() {
        if (rec && on && w > 0.0f && h > 0.0f && c[3]) { rec->push_back(RecRect{x, y, w, h, c[3]}); on = false; return; }
        if (!on || !ren || w <= 0.0f || h <= 0.0f || c[3] == 0) { on = false; return; }
        ren->SetDrawColor(c[0], c[1], c[2], c[3]);
        const bool whole = std::fabs(x - std::round(x)) < 0.01f && std::fabs(y - std::round(y)) < 0.01f &&
                           std::fabs(w - std::round(w)) < 0.01f && std::fabs(h - std::round(h)) < 0.01f;
        if (whole) {
            Rect q{(int)std::lround(x), (int)std::lround(y), (int)std::lround(w), (int)std::lround(h)};
            if (q.w > 0 && q.h > 0) ren->FillRect(&q);
        } else {
            // A hundredth of a device pixel of bias, as in FillCoverage: a software renderer truncates float rects, and x / S * S can land just below x,
            // which left a one-pixel gap between neighbouring strips (hairlines across gradient bars and panels at 3x).
            const float S = g_uiDeviceScale > 0.05f ? g_uiDeviceScale : 1.0f;
            FRect q{x + 0.01f / S, y + 0.01f / S, w + 0.01f / S, h + 0.01f / S};
            ren->FillRectF(&q);
        }
        on = false;
    }
    void Add(float nx, float ny, float nw, float nh, const std::uint8_t col[4]) {
        if (nw <= 0.0f || nh <= 0.0f || col[3] == 0) return;
        if (on && std::fabs(nx - x) < 0.02f && std::fabs(nw - w) < 0.02f && std::fabs(ny - (y + h)) < 0.02f &&
            col[0] == c[0] && col[1] == c[1] && col[2] == c[2] && col[3] == c[3]) {
            h += nh;
            return;
        }
        Flush();
        x = nx; y = ny; w = nw; h = nh;
        for (int i = 0; i < 4; ++i) c[i] = col[i];
        on = true;
    }
};

void PackCov(const Color& c, float op, float cov, std::uint8_t out[4]) {
    float a = c.a * op * cov;
    if (a < 0.0f) a = 0.0f;
    if (a > 1.0f) a = 1.0f;
    out[0] = (std::uint8_t)(c.r * 255.0f);
    out[1] = (std::uint8_t)(c.g * 255.0f);
    out[2] = (std::uint8_t)(c.b * 255.0f);
    out[3] = (std::uint8_t)(a * 255.0f + 0.5f);
}

// Like PackCov, but each colour channel is rounded with a 4x4 ordered (Bayer) threshold at device pixel (x, y), so a slow gradient
// shows fine noise instead of wide bands (a smooth ramp over 20 levels is 20 flat strips otherwise).
void PackCovDither(const Color& c, float op, float cov, int x, int y, std::uint8_t out[4]) {
    static const std::uint8_t kBayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    const float thr = ((float)kBayer[(y & 3) * 4 + (x & 3)] + 0.5f) / 16.0f;
    auto q = [&](float v) {
        float f = std::floor(v * 255.0f + thr);
        return (std::uint8_t)(f < 0.0f ? 0.0f : (f > 255.0f ? 255.0f : f));
    };
    out[0] = q(c.r); out[1] = q(c.g); out[2] = q(c.b);
    float a = c.a * op * cov;
    a = a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a);
    out[3] = (std::uint8_t)(a * 255.0f + 0.5f);
}

Color LerpC(const Color& a, const Color& b, float t) {
    return Color{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

// Anti-aliased fill in device pixels. `r` is in logical pixels; the canvas scale
// turns each logical pixel into g_uiDeviceScale device pixels, so the curve is
// resolved at the screen's resolution and the fringe is one device pixel.
// Left fringe, solid middle, right fringe of span [a, b). At most three rects.
void EmitSpan(FloatRun& run, float ox, float ly, float lh, float S, float a, float b,
              const std::uint8_t solid[4], const Color& tint, float op, bool flat) {
    if (!(b > a)) return;
    const int i0 = (int)std::floor(a);
    const int i1 = (int)std::ceil(b - 1e-4f);   // exclusive
    if (i1 <= i0) return;
    auto put = [&](int x, float cov, float wpx) {
        if (cov <= 0.004f || wpx <= 0.0f) return;
        if (flat && cov > 0.996f) {
            run.Add(ox + (float)x / S, ly, wpx / S, lh, solid);
            return;
        }
        std::uint8_t col[4];
        PackCov(tint, op, cov > 1.0f ? 1.0f : cov, col);
        run.Add(ox + (float)x / S, ly, wpx / S, lh, col);
    };
    if (!flat) {
        for (int x = i0; x < i1; ++x) put(x, SpanOverlap(a, b, x), 1.0f);
        return;
    }
    if (i1 == i0 + 1) { put(i0, SpanOverlap(a, b, i0), 1.0f); return; }
    const float c0 = SpanOverlap(a, b, i0);
    const float c1 = SpanOverlap(a, b, i1 - 1);
    // A pixel that is mostly inside joins the solid run. Only a genuinely split
    // pixel gets its own translucent rect, so a curve stays a handful of quads.
    const int s0 = c0 >= 0.5f ? i0 : i0 + 1;
    const int s1 = c1 >= 0.5f ? i1 : i1 - 1;   // exclusive end of the solid run
    if (s0 == i0 + 1 && c0 >= 0.2f) put(i0, c0, 1.0f);
    if (s1 > s0) put(s0, 1.0f, (float)(s1 - s0));
    if (s1 == i1 - 1 && c1 >= 0.2f) put(i1 - 1, c1, 1.0f);
}

// One horizontal sub-scanline of a shape: up to two spans (a ring has a left and a right piece) at the sample's own height.
struct SubSpans { float a[2], b[2]; int n = 0; };

// Exact-coverage version of EmitSpan for several sub-scanlines per device row. Each device pixel gets the mean, over the samples, of
// the part of it that lies inside that sample's spans, so an edge that is nearly horizontal (the top and bottom of a circle, a pill's
// ends) is anti-aliased vertically as well as sideways, and no pixel is rounded up to solid or down to nothing. Runs of pixels that
// every sample covers completely are emitted as one rect.
void EmitMulti(FloatRun& run, float gx0, float ly, float lh, float S, const SubSpans* smp, int K,
               const std::uint8_t solid[4], const Color& tint, float op) {
    float lo = 1e30f, hi = -1e30f;
    for (int k = 0; k < K; ++k)
        for (int i = 0; i < smp[k].n; ++i) { lo = std::min(lo, smp[k].a[i]); hi = std::max(hi, smp[k].b[i]); }
    if (!(hi > lo)) return;
    const int xe = (int)std::ceil(hi - 1e-4f);
    int x = (int)std::floor(lo);
    const float inv = 1.0f / (float)K;
    float cov, endFull, nextStart;
    int full;
    auto eval = [&](int px) {
        cov = 0.0f; endFull = 1e30f; nextStart = 1e30f; full = 0;
        for (int k = 0; k < K; ++k) {
            bool fullHere = false;
            for (int i = 0; i < smp[k].n; ++i) {
                const float a = smp[k].a[i], b = smp[k].b[i];
                const float ov = SpanOverlap(a, b, px);
                cov += ov;
                if (ov >= 0.999f) { fullHere = true; endFull = std::min(endFull, std::floor(b + 1e-4f)); }
                if (a > (float)px + 1.0f) nextStart = std::min(nextStart, std::floor(a));
            }
            if (fullHere) ++full;
        }
        cov *= inv;
    };
    while (x < xe) {
        eval(x);
        if (full == K && endFull > (float)x) {   // every sample covers this pixel whole: solid up to the first end
            const int ex = std::min((int)endFull, xe);
            run.Add(gx0 + (float)x / S, ly, (float)(ex - x) / S, lh, solid);
            x = std::max(ex, x + 1);
            continue;
        }
        // Pixels under 3% covered are left out and pixels over 97% covered count as solid (an error of at most 8 of 255 levels on a few
        // edge pixels); neighbouring solid pixels merge into one rect. That is what keeps a screen of rounded widgets cheap.
        if (cov < 0.03f) { x = (nextStart < 1e29f) ? std::max(x + 1, (int)nextStart) : x + 1; continue; }
        if (cov >= 0.97f) {
            int x2 = x + 1;
            while (x2 < xe) { eval(x2); if (cov >= 0.97f) ++x2; else break; }
            run.Add(gx0 + (float)x / S, ly, (float)(x2 - x) / S, lh, solid);
            x = x2;
            continue;
        }
        // Alpha in tenths (5% off at most): rows of a gentle arc then share a value and merge into one rect when the shape is recorded.
        cov = std::floor(cov * 10.0f + 0.5f) * 0.1f;
        std::uint8_t col[4];
        PackCov(tint, op, cov, col);
        run.Add(gx0 + (float)x / S, ly, 1.0f / S, lh, col);
        ++x;
    }
}

// Where a logical rect lands on the device pixel grid: its edges rounded to whole device pixels (so at a canvas scale of 1.5 a
// 15 px wide card is 22 or 23 device pixels, with hard, even borders, instead of starting half way through a pixel). Returns the
// origin in logical units and the size in device pixels. The Old look keeps the unsnapped origin and the exact size.
// Recorded rectangles of flat shapes and borders, keyed by their geometry (not their colour): a screen that does not change draws the
// same shapes every frame, and replaying the list skips the span maths and the merging. Cleared when it grows past 512 shapes.
struct ShapeKey {
    int kind, shape, dw, dh, rad, mask, K, S, inset;
    bool operator==(const ShapeKey& o) const {
        return kind == o.kind && shape == o.shape && dw == o.dw && dh == o.dh && rad == o.rad && mask == o.mask && K == o.K && S == o.S && inset == o.inset;
    }
};
struct ShapeKeyHash {
    std::size_t operator()(const ShapeKey& k) const {
        std::size_t h = 1469598103u;
        for (int v : {k.kind, k.shape, k.dw, k.dh, k.rad, k.mask, k.K, k.S, k.inset}) h = (h ^ (std::size_t)(unsigned)v) * 1099511628211ull;
        return h;
    }
};
std::unordered_map<ShapeKey, std::vector<RecRect>, ShapeKeyHash>& ShapeCache() {
    static thread_local std::unordered_map<ShapeKey, std::vector<RecRect>, ShapeKeyHash> c;
    return c;
}
// Draw one recorded rectangle (the same whole-pixel / fractional split as FloatRun::Flush) in `col`.
void PutRect(Painter& ren, float x, float y, float w, float h, const std::uint8_t c[4]) {
    FloatRun r;
    r.ren = &ren; r.x = x; r.y = y; r.w = w; r.h = h; r.on = true;
    for (int i = 0; i < 4; ++i) r.c[i] = c[i];
    r.Flush();
}
void Replay(Painter& ren, const std::vector<RecRect>& rects, float ox, float oy, const std::uint8_t solid[4]) {
    if (solid[3] == 0) return;
    if (ren.SupportsBulkRects()) {   // the batcher: one call for the whole list (same rectangles, same half-hundredth bias as FloatRun::Flush)
        static thread_local std::vector<ColoredRect> buf;
        buf.clear();
        const float S = g_uiDeviceScale > 0.05f ? g_uiDeviceScale : 1.0f;
        for (const RecRect& q : rects) {
            const std::uint8_t a = q.a == 255 ? solid[3] : (std::uint8_t)((float)q.a * (float)solid[3] / 255.0f + 0.5f);
            if (!a) continue;
            const float x = ox + q.x, y = oy + q.y;
            const bool whole = std::fabs(x - std::round(x)) < 0.01f && std::fabs(y - std::round(y)) < 0.01f &&
                               std::fabs(q.w - std::round(q.w)) < 0.01f && std::fabs(q.h - std::round(q.h)) < 0.01f;
            FRect r = whole ? FRect{std::round(x), std::round(y), std::round(q.w), std::round(q.h)}
                            : FRect{x + 0.01f / S, y + 0.01f / S, q.w + 0.01f / S, q.h + 0.01f / S};
            buf.push_back(ColoredRect{r, RGBA{solid[0], solid[1], solid[2], a}});
        }
        if (!buf.empty()) ren.FillRectsF(buf.data(), (int)buf.size());
        return;
    }
    for (const RecRect& q : rects) {
        std::uint8_t col[4] = {solid[0], solid[1], solid[2], q.a == 255 ? solid[3] : (std::uint8_t)((float)q.a * (float)solid[3] / 255.0f + 0.5f)};
        if (col[3]) PutRect(ren, ox + q.x, oy + q.y, q.w, q.h, col);
    }
}

// Sub-scanlines per device row for shape edges: 1 keeps the coarse one-sample fill (Old look, Fast).
int SubSamples() {
    switch (g_uiQuality) {
        case UIQuality::Good: return 4;
        case UIQuality::Best: return 8;
        default: return 1;
    }
}

void SnapToDevice(const Rect& r, float S, float& ox, float& oy, float& dw, float& dh) {
    ox = (float)r.x; oy = (float)r.y;
    dw = std::max(1.0f, (float)r.w * S); dh = std::max(1.0f, (float)r.h * S);
    if (g_uiQuality == UIQuality::Old || std::fabs(S - 1.0f) < 1e-4f) return;
    const float x0 = std::floor((float)r.x * S + 0.5f), y0 = std::floor((float)r.y * S + 0.5f);
    const float x1 = std::floor((float)(r.x + r.w) * S + 0.5f), y1 = std::floor((float)(r.y + r.h) * S + 0.5f);
    ox = x0 / S; oy = y0 / S;
    dw = std::max(1.0f, x1 - x0); dh = std::max(1.0f, y1 - y0);
}

void FillUIShapeSmooth(Painter& ren, const Rect& r, UIShape shape, float radius,
                       const Color& top, const Color& bottom, bool gradient, bool horizontal,
                       float op, int diag, int cornerMask) {
    const float S = g_uiDeviceScale > 0.05f ? g_uiDeviceScale : 1.0f;
    float ox0, oy0, dw, dh;
    SnapToDevice(r, S, ox0, oy0, dw, dh);
    const float rad = (shape == UIShape::Pill) ? std::min(dw, dh) * 0.5f : std::max(0.0f, radius * S);
    const int rows = std::max(1, (int)std::ceil(dh - 1e-3f));
    const bool varyX = gradient && (horizontal || diag != 0);
    // Best: a gradient on a modest area is dithered per pixel (an ordered threshold on the 8-bit rounding). Bigger areas keep the
    // one-rect-per-row fill: a pixel per rect would cost too much, and a big gradient hides its bands less than a small card.
    const bool dither = gradient && g_uiQuality == UIQuality::Best && dw * dh <= 30000.0f;
    const bool perPixel = varyX || dither;
    const bool sideRun = shape == UIShape::Rounded || shape == UIShape::Pill || shape == UIShape::TabTop;
    const int cap = sideRun ? (int)std::ceil(rad) : rows;
    const bool flat = !gradient;
    const int K = SubSamples();
    std::uint8_t solidDraw[4];
    PackCov(top, op, 1.0f, solidDraw);
    std::uint8_t solid[4];
    for (int i = 0; i < 4; ++i) solid[i] = solidDraw[i];
    const bool cacheable = K > 1 && flat && !perPixel;
    ShapeKey key{0, (int)shape, (int)std::lround(dw * 16.0f), (int)std::lround(dh * 16.0f), (int)std::lround(rad * 16.0f), cornerMask, K, (int)std::lround(S * 64.0f), 0};
    if (cacheable) {
        auto& cache = ShapeCache();
        auto it = cache.find(key);
        if (it != cache.end()) { Replay(ren, it->second, ox0, oy0, solidDraw); return; }
    }
    std::vector<RecRect> rec;
    FloatRun run;
    run.ren = &ren;
    Color topC = top; float opC = op;
    std::uint8_t solidW[4] = {255, 255, 255, 255};
    std::uint8_t* solidP = solid;
    if (cacheable) {   // record in a neutral colour, then draw (and keep) the list
        run.rec = &rec; topC = Color{1, 1, 1, 1}; opC = 1.0f; solidP = solidW;
    }
    {
    const Color& top = topC; const float op = opC; std::uint8_t* solid = solidP;
    for (int row = 0; row < rows; ++row) {
        const float ly = oy0 + (float)row / S;
        const float lh = 1.0f / S;
        if (sideRun && flat && row >= cap && (shape == UIShape::TabTop || row < rows - cap)) {
            run.Add(ox0, ly, dw / S, lh, solid);
            continue;
        }
        if (K > 1 && !perPixel) {   // exact coverage, several sub-scanlines per device row
            SubSpans smp[8];
            int present = 0;
            for (int k = 0; k < K; ++k) {
                SubSpans& q = smp[k];
                q.n = 0;
                if (UIShapeSpanAt(shape, dw, dh, shape == UIShape::Pill ? 0.0f : rad, (float)row + ((float)k + 0.5f) / (float)K, q.a[0], q.b[0], cornerMask)) { q.n = 1; ++present; }
            }
            if (!present) continue;
            Color tint = top;
            std::uint8_t solidRow[4];
            if (gradient) {
                const float fy = dh > 1.0f ? ((float)row + 0.5f) / (dh - 1.0f) : 0.0f;
                tint = LerpC(top, bottom, fy < 0.0f ? 0.0f : (fy > 1.0f ? 1.0f : fy));
                PackCov(tint, op, 1.0f, solidRow);
            } else { for (int i = 0; i < 4; ++i) solidRow[i] = solid[i]; }
            EmitMulti(run, ox0, ly, lh, S, smp, K, solidRow, tint, op);
            continue;
        }
        float a, b;
        if (!UIShapeSpanAt(shape, dw, dh, shape == UIShape::Pill ? 0.0f : rad, (float)row + 0.5f, a, b, cornerMask))
            continue;
        Color tint = top;
        if (gradient && !perPixel) {
            const float fy = dh > 1.0f ? ((float)row + 0.5f) / (dh - 1.0f) : 0.0f;
            tint = LerpC(top, bottom, fy < 0.0f ? 0.0f : (fy > 1.0f ? 1.0f : fy));
            PackCov(tint, op, 1.0f, solid);
        }
        if (!perPixel) {
            EmitSpan(run, ox0, ly, lh, S, a, b, solid, tint, op, true);
            continue;
        }
        const int i0 = (int)std::floor(a), i1 = (int)std::ceil(b - 1e-4f);
        for (int x = i0; x < i1; ++x) {
            const float cov = SpanOverlap(a, b, x);
            if (cov <= 0.004f) continue;
            const float fy = dh > 1.0f ? ((float)row + 0.5f) / (dh - 1.0f) : 0.0f;
            const float fx = dw > 1.0f ? ((float)x + 0.5f) / (dw - 1.0f) : 0.0f;
            const float t = !varyX ? fy : (diag == 1 ? (fx + fy) * 0.5f : diag == 2 ? (fx + (1.0f - fy)) * 0.5f : fx);
            const Color c = LerpC(top, bottom, t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t));
            std::uint8_t col[4];
            if (dither) PackCovDither(c, op, cov, x, row, col); else PackCov(c, op, cov, col);
            run.Add(ox0 + (float)x / S, ly, 1.0f / S, lh, col);
        }
    }
    run.Flush();
    }
    if (cacheable) {
        auto& cache = ShapeCache();
        if (cache.size() > 512) cache.clear();
        for (RecRect& q : rec) { q.x -= ox0; q.y -= oy0; }   // keep them relative to the shape's origin
        const auto& stored = cache.emplace(key, std::move(rec)).first->second;
        Replay(ren, stored, ox0, oy0, solidDraw);
    }
}

void FillUIRingSmooth(Painter& ren, const Rect& r, UIShape shape, float radius,
                      const Color& c, int width, float op, int cornerMask) {
    const float S = g_uiDeviceScale > 0.05f ? g_uiDeviceScale : 1.0f;
    float gx0, gy0, dw, dh;
    SnapToDevice(r, S, gx0, gy0, dw, dh);
    const float rad = (shape == UIShape::Pill) ? std::min(dw, dh) * 0.5f : std::max(0.0f, radius * S);
    const float inset = g_uiQuality == UIQuality::Old ? std::max(1.0f, (float)width * S) : std::max(1.0f, std::floor((float)width * S + 0.5f));
    const float iw = dw - 2.0f * inset, ih = dh - 2.0f * inset;
    const float irad = std::max(0.0f, rad - inset);
    const int rows = std::max(1, (int)std::ceil(dh - 1e-3f));
    const bool sideRun = shape == UIShape::Rounded || shape == UIShape::Pill || shape == UIShape::TabTop;
    const int cap = sideRun ? (int)std::ceil(std::max(rad, inset + 1.0f)) : rows;
    const int K = SubSamples();
    std::uint8_t solidDraw[4];
    PackCov(c, op, 1.0f, solidDraw);
    const bool cacheable = K > 1;
    ShapeKey key{1, (int)shape, (int)std::lround(dw * 16.0f), (int)std::lround(dh * 16.0f), (int)std::lround(rad * 16.0f), cornerMask, K, (int)std::lround(S * 64.0f), (int)std::lround(inset * 16.0f)};
    if (cacheable) {
        auto& cache = ShapeCache();
        auto it = cache.find(key);
        if (it != cache.end()) { Replay(ren, it->second, gx0, gy0, solidDraw); return; }
    }
    std::vector<RecRect> rec;
    FloatRun run;
    run.ren = &ren;
    std::uint8_t solidW[4] = {255, 255, 255, 255};
    const std::uint8_t* solidP = solidDraw;
    Color cC = c; float opC = op;
    if (cacheable) { run.rec = &rec; solidP = solidW; cC = Color{1, 1, 1, 1}; opC = 1.0f; }
    {
    const Color& c = cC; const float op = opC; const std::uint8_t* solid = solidP;
    for (int row = 0; row < rows; ++row) {
        const float ly = gy0 + (float)row / S;
        const float lh = 1.0f / S;
        // Straight sides of a rounded ring: a strip on the left and the right.
        if (sideRun && row >= cap && (shape == UIShape::TabTop || row < rows - cap)) {
            const float band = inset / S;
            run.Add(gx0, ly, band, lh, solid);
            run.Add(gx0 + dw / S - band, ly, band, lh, solid);
            continue;
        }
        if (K > 1) {   // exact coverage, several sub-scanlines per device row (the ring is the outer shape minus the inner one)
            SubSpans smp[8];
            int present = 0;
            for (int k = 0; k < K; ++k) {
                SubSpans& q = smp[k];
                q.n = 0;
                const float sy = (float)row + ((float)k + 0.5f) / (float)K;
                float o0, o1;
                if (!UIShapeSpanAt(shape, dw, dh, shape == UIShape::Pill ? 0.0f : rad, sy, o0, o1, cornerMask)) continue;
                float i0 = 0.0f, i1 = -1.0f;
                const bool in = iw > 0.5f && ih > 0.5f && UIShapeSpanAt(shape, iw, ih, irad, sy - inset, i0, i1, cornerMask);
                if (!in) { q.a[0] = o0; q.b[0] = o1; q.n = 1; }
                else {
                    if (i0 + inset > o0) { q.a[q.n] = o0; q.b[q.n] = i0 + inset; ++q.n; }
                    if (o1 > i1 + inset) { q.a[q.n] = i1 + inset; q.b[q.n] = o1; ++q.n; }
                }
                if (q.n) ++present;
            }
            if (present) EmitMulti(run, gx0, ly, lh, S, smp, K, solid, c, op);
            continue;
        }
        float ox0, ox1;
        if (!UIShapeSpanAt(shape, dw, dh, shape == UIShape::Pill ? 0.0f : rad, (float)row + 0.5f, ox0, ox1, cornerMask))
            continue;
        float ix0 = 0.0f, ix1 = -1.0f;
        const bool inner = iw > 0.5f && ih > 0.5f &&
                           UIShapeSpanAt(shape, iw, ih, irad, (float)row + 0.5f - inset, ix0, ix1, cornerMask);
        if (!inner) {
            EmitSpan(run, gx0, ly, lh, S, ox0, ox1, solid, c, op, true);
            continue;
        }
        EmitSpan(run, gx0, ly, lh, S, ox0, ix0 + inset, solid, c, op, true);
        EmitSpan(run, gx0, ly, lh, S, ix1 + inset, ox1, solid, c, op, true);
    }
    run.Flush();
    }
    if (cacheable) {
        auto& cache = ShapeCache();
        if (cache.size() > 512) cache.clear();
        for (RecRect& q : rec) { q.x -= gx0; q.y -= gy0; }
        const auto& stored = cache.emplace(key, std::move(rec)).first->second;
        Replay(ren, stored, gx0, gy0, solidDraw);
    }
}



} // namespace

// Fill a UI shape (rectangle / rounded / circle / pill) into screen rect `r`,
// scanline by scanline so any silhouette uses one code path (identical rows merge
// into one rect). Supports a linear gradient (top->bottom, or left->right when
// `horizontal`); pass equal colors for a flat fill. `op` is the canvas master opacity.
// `diag`: 0 = axis-aligned (obey `horizontal`); 1 = diagonal top-left->bottom-right;
// 2 = diagonal bottom-left->top-right. Diagonals blend `top`->`bottom` across the
// corner and, like the horizontal case, step per pixel.
void FillUIShape(Painter& ren, const Rect& r, UIShape shape, float radius,
                 const Color& top, const Color& bottom, bool gradient, bool horizontal,
                 float op, int diag, int cornerMask) {
    if (r.w <= 0 || r.h <= 0) return;
    // Rectangles already sit on the pixel grid. Curves get a one-pixel coverage fringe
    // unless the scene asked for the pixel-art look.
    if (g_uiSmoothEdges && (shape != UIShape::Rectangle || (gradient && g_uiQuality == UIQuality::Best))) {
        FillUIShapeSmooth(ren, r, shape, radius, top, bottom, gradient, horizontal, op, diag, cornerMask);
        return;
    }
    auto lerp = [](const Color& a, const Color& b, float t) {
        return Color{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
                     a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
    };
    auto bytes = [op](const Color& c, std::uint8_t out[4]) {
        out[0] = (std::uint8_t)(c.r * 255); out[1] = (std::uint8_t)(c.g * 255);
        out[2] = (std::uint8_t)(c.b * 255); out[3] = (std::uint8_t)(c.a * 255 * op);
    };
    RowRuns runs{ren};
    std::uint8_t cb[4];
    bytes(top, cb);
    for (int row = 0; row < r.h; ++row) {
        float x0, x1;
        if (!UIShapeRowSpan(shape, (float)r.w, (float)r.h, radius, row, x0, x1, cornerMask)) continue;
        if (!gradient) {
            runs.Add(r.x + (int)x0, r.y + row, (int)(x1 - x0) + 1, cb);
        } else if (!horizontal && diag == 0) {
            float t = r.h > 1 ? (float)row / (r.h - 1) : 0.0f;
            bytes(lerp(top, bottom, t), cb);
            runs.Add(r.x + (int)x0, r.y + row, (int)(x1 - x0) + 1, cb);
        } else {
            // Horizontal or diagonal gradient: step across the span pixel by pixel.
            runs.Flush();
            int ix0 = (int)x0, ix1 = (int)x1;
            float fy = r.h > 1 ? (float)row / (r.h - 1) : 0.0f;
            for (int x = ix0; x <= ix1; ++x) {
                float fx = r.w > 1 ? (float)x / (r.w - 1) : 0.0f;
                float t = diag == 1 ? (fx + fy) * 0.5f
                        : diag == 2 ? (fx + (1.0f - fy)) * 0.5f
                        : fx;                                   // plain horizontal
                Color c = lerp(top, bottom, t);
                ren.SetDrawColor((std::uint8_t)(c.r * 255), (std::uint8_t)(c.g * 255),
                                       (std::uint8_t)(c.b * 255), (std::uint8_t)(c.a * 255 * op));
                Rect px{r.x + x, r.y + row, 1, 1};
                ren.FillRect(&px);
            }
        }
    }
    runs.Flush();
}

// A border ring: the outer shape minus the shape inset by `width`. Drawn over the
// fill, so a clear or see-through widget (an outline button, a glass panel) shows
// what's behind it instead of a solid slab of border colour.
void FillUIRing(Painter& ren, const Rect& r, UIShape shape, float radius,
                const Color& c, int width, float op, int cornerMask) {
    if (r.w <= 0 || r.h <= 0 || width <= 0) return;
    if (g_uiSmoothEdges && shape != UIShape::Rectangle) {
        FillUIRingSmooth(ren, r, shape, radius, c, width, op, cornerMask);
        return;
    }
    const std::uint8_t cb[4] = {(std::uint8_t)(c.r * 255), (std::uint8_t)(c.g * 255), (std::uint8_t)(c.b * 255),
                                (std::uint8_t)(c.a * 255 * op)};
    const int iw = r.w - 2 * width, ih = r.h - 2 * width;
    const float innerR = radius - width > 0.0f ? radius - width : 0.0f;
    RowRuns left{ren}, right{ren};   // a row is one span or a left and a right piece
    for (int row = 0; row < r.h; ++row) {
        float x0, x1;
        if (!UIShapeRowSpan(shape, (float)r.w, (float)r.h, radius, row, x0, x1, cornerMask)) continue;
        float ix0 = 0, ix1 = -1;
        const int ir = row - width;
        const bool inner = iw > 0 && ih > 0 && ir >= 0 && ir < ih &&
                           UIShapeRowSpan(shape, (float)iw, (float)ih, innerR, ir, ix0, ix1, cornerMask);
        if (!inner) {
            right.Flush();
            left.Add(r.x + (int)x0, r.y + row, (int)(x1 - x0) + 1, cb);
            continue;
        }
        const int l0 = (int)x0, l1 = width + (int)ix0;           // left piece  [l0, l1)
        const int r0 = width + (int)ix1 + 1, r1 = (int)x1 + 1;   // right piece [r0, r1)
        if (l1 > l0) left.Add(r.x + l0, r.y + row, l1 - l0, cb); else left.Flush();
        if (r1 > r0) right.Add(r.x + r0, r.y + row, r1 - r0, cb); else right.Flush();
    }
    left.Flush();
    right.Flush();
}

// Map a panel's gradient direction to FillUIShape's (horizontal, diag) params.
void GradientParams(UIPanel::GradientDir d, bool& horizontal, int& diag) {
    horizontal = (d == UIPanel::GradientDir::Horizontal);
    diag = (d == UIPanel::GradientDir::DiagonalDown) ? 1
         : (d == UIPanel::GradientDir::DiagonalUp)   ? 2 : 0;
}

// Draw a drop shadow for a UI shape. softness == 0 is a crisp shadow; softness > 0
// fakes a blur by stacking a few expanding, fading copies into a soft penumbra.
// A soft shadow as one Gouraud mesh: rings of the shape's outline moved by d (from inside the edge to well outside it), each ring
// carrying the alpha a Gaussian-blurred edge has at that distance (0.5 * erfc(d / (sigma * sqrt 2))); the graphics card (or the
// software renderer) blends between the rings, so the penumbra is smooth at any zoom instead of a stack of hard steps.
static void FillUIShadowSoft(Painter& ren, const Rect& r, UIShape shape, float radius,
                             const Color& color, float softness, float op, int cornerMask) {
    const float S = g_uiDeviceScale > 0.05f ? g_uiDeviceScale : 1.0f;
    const float W = (float)r.w, H = (float)r.h;
    const float sigma = std::max(0.25f, softness * 0.4f);
    const float minHalf = std::min(W, H) * 0.5f;
    // Corner radii in the order TR, BR, BL, TL; the straight corners of a Rectangle/Tab shape have none.
    float rad = shape == UIShape::Pill ? minHalf : (shape == UIShape::Circle ? minHalf : (shape == UIShape::Rectangle ? 0.0f : std::max(0.0f, radius)));
    rad = std::min(rad, minHalf);
    const int bits[4] = {UICornerTR, UICornerBR, UICornerBL, UICornerTL};
    float cr[4];
    for (int c = 0; c < 4; ++c) cr[c] = (shape == UIShape::Circle || (cornerMask & bits[c])) ? rad : 0.0f;
    if (shape == UIShape::TabTop) { cr[1] = cr[2] = 0.0f; }
    const bool ellipse = shape == UIShape::Circle && std::fabs(W - H) > 0.5f;   // a circle of a non-square rect is an ellipse
    // Steps per corner: the arc stays within a tenth of a device pixel of the true curve at the widest ring.
    const float reach = 2.6f * sigma;
    auto steps = [&](float rl) {
        const float rd = std::max(1.0f, rl * S);
        const float st = 2.0f * std::acos(std::max(0.0f, 1.0f - 0.1f / rd));
        return std::clamp((int)std::ceil(1.5707963f / std::max(st, 0.01f)), 3, 48);
    };
    const int n = steps(rad + reach);
    // Ring offsets, inner to outer. Alpha at each from the blurred-edge profile; the outermost is 0.
    constexpr int kRings = 10;
    float ds[kRings], al[kRings];
    for (int i = 0; i < kRings; ++i) {
        float t = (float)i / (float)(kRings - 1);
        float d = -2.0f * sigma + t * (2.0f * sigma + reach);
        if (d < -minHalf + 0.01f) d = -minHalf + 0.01f;
        ds[i] = d;
        al[i] = (i == kRings - 1) ? 0.0f : 0.5f * std::erfc(d / (sigma * 1.41421356f));
    }
    auto ring = [&](float d, std::vector<FPoint>& out) {
        const float x0 = (float)r.x - d, y0 = (float)r.y - d, x1 = (float)(r.x + r.w) + d, y1 = (float)(r.y + r.h) + d;
        if (shape == UIShape::Circle) {
            const float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f, rx = std::max(0.01f, (x1 - x0) * 0.5f), ry = std::max(0.01f, (y1 - y0) * 0.5f);
            const int m = 4 * n;
            for (int k = 0; k < m; ++k) {
                const float a = (float)k / (float)m * 6.2831853f;
                out.push_back({cx + std::cos(a) * rx, cy + std::sin(a) * ry});
            }
            return;
        }
        const float ccx[4] = {x1, x1, x0, x0}, ccy[4] = {y0, y1, y1, y0};
        for (int c = 0; c < 4; ++c) {
            const float rc = std::max(0.0f, std::min(cr[c] + d, std::min(x1 - x0, y1 - y0) * 0.5f));
            const float sx = (c == 0 || c == 1) ? -1.0f : 1.0f, sy = (c == 1 || c == 2) ? -1.0f : 1.0f;
            const float ox = ccx[c] + sx * rc, oy = ccy[c] + sy * rc;   // the arc's centre
            for (int k = 0; k <= n; ++k) {
                const float a = (-90.0f + c * 90.0f + (float)k * (90.0f / (float)n)) * 0.01745329f;
                out.push_back({ox + std::cos(a) * rc, oy + std::sin(a) * rc});
            }
        }
    };
    (void)ellipse;
    // The buffers are kept between calls (a screen full of shadows would otherwise allocate every frame).
    static thread_local std::vector<std::vector<FPoint>> rings;
    rings.resize(kRings);
    for (int i = 0; i < kRings; ++i) { rings[i].clear(); ring(ds[i], rings[i]); }
    const int m = (int)rings[0].size();
    const std::uint8_t cr8 = (std::uint8_t)(color.r * 255.0f + 0.5f), cg8 = (std::uint8_t)(color.g * 255.0f + 0.5f), cb8 = (std::uint8_t)(color.b * 255.0f + 0.5f);
    const float baseA = std::clamp(color.a * op, 0.0f, 1.0f);
    auto col = [&](float a) { return RGBA{cr8, cg8, cb8, (std::uint8_t)(std::clamp(a * baseA, 0.0f, 1.0f) * 255.0f + 0.5f)}; };
    static thread_local std::vector<Vertex> vx;
    static thread_local std::vector<int> ix;
    vx.clear(); ix.clear();
    float cx = 0.0f, cy = 0.0f;
    for (const FPoint& q : rings[0]) { cx += q.x; cy += q.y; }
    cx /= (float)m; cy /= (float)m;
    vx.push_back({{cx, cy}, col(al[0]), {0, 0}});
    for (int i = 0; i < kRings; ++i)
        for (const FPoint& q : rings[i]) vx.push_back({q, col(al[i]), {0, 0}});
    for (int k = 0; k < m; ++k) {   // the fan inside the innermost ring
        const int k1 = (k + 1) % m;
        ix.push_back(0); ix.push_back(1 + k); ix.push_back(1 + k1);
    }
    for (int i = 0; i + 1 < kRings; ++i)
        for (int k = 0; k < m; ++k) {
            const int k1 = (k + 1) % m;
            const int a = 1 + i * m + k, b = 1 + i * m + k1, c = 1 + (i + 1) * m + k, d = 1 + (i + 1) * m + k1;
            ix.push_back(a); ix.push_back(c); ix.push_back(b);
            ix.push_back(b); ix.push_back(c); ix.push_back(d);
        }
    ren.Geometry(nullptr, vx.data(), (int)vx.size(), ix.data(), (int)ix.size());
}

void FillUIShadow(Painter& ren, const Rect& r, UIShape shape, float radius,
                  const Color& color, float softness, float op, int cornerMask) {
    if (softness <= 0.0f) {
        FillUIShape(ren, r, shape, radius, color, color, false, false, op, 0, cornerMask);
        return;
    }
    if (g_uiSmoothEdges && g_uiQuality != UIQuality::Old && g_uiQuality != UIQuality::Fast && r.w > 0 && r.h > 0) {
        FillUIShadowSoft(ren, r, shape, radius, color, softness, op, cornerMask);
        return;
    }
    const int layers = 5;
    for (int k = layers; k >= 1; --k) {
        float grow = softness * (float)k / layers;
        Rect r2{r.x - (int)grow, r.y - (int)grow, r.w + (int)(2 * grow), r.h + (int)(2 * grow)};
        Color c = color; c.a = color.a * (0.6f / layers);     // accumulate toward the edge
        FillUIShape(ren, r2, shape, radius + grow, c, c, false, false, op, 0, cornerMask);
    }
    Color core = color; core.a *= 0.55f;   // a lighter core: soft shadows read as depth, not a dark slab
    FillUIShape(ren, r, shape, radius, core, core, false, false, op, 0, cornerMask);
}


// --- Small raster helpers for the minimap (filled circle / ring / triangle). ----
static void MMFillCircleHard(Painter& r, int cx, int cy, int rad, RGBA col) {
    if (rad < 1) rad = 1;
    r.SetDrawColor(col.r, col.g, col.b, col.a);
    for (int dy = -rad; dy <= rad; ++dy) {
        int dx = (int)std::sqrt((double)(rad * rad - dy * dy));
        r.DrawLine(cx - dx, cy + dy, cx + dx, cy + dy);
    }
}
static void MMDrawRingHard(Painter& r, int cx, int cy, int rad, int width, RGBA col) {
    if (rad < 1) rad = 1;
    if (width < 1) width = 1;
    r.SetDrawColor(col.r, col.g, col.b, col.a);
    const int seg = 64;
    int prevx = cx + rad, prevy = cy;
    for (int i = 1; i <= seg; ++i) {
        float a = (float)i / seg * 6.2831853f;
        int x = cx + (int)(std::cos(a) * rad), y = cy + (int)(std::sin(a) * rad);
        for (int w2 = 0; w2 < width; ++w2) r.DrawLine(prevx, prevy + w2, x, y + w2);
        prevx = x; prevy = y;
    }
}
static void MMFillTriangleHard(Painter& r, Point a, Point b, Point c, RGBA col) {
    r.SetDrawColor(col.r, col.g, col.b, col.a);
    int minY = std::min({a.y, b.y, c.y}), maxY = std::max({a.y, b.y, c.y});
    auto edgeX = [](Point p, Point q, int y, float& x) -> bool {
        if (p.y == q.y) return false;
        if (y < std::min(p.y, q.y) || y > std::max(p.y, q.y)) return false;
        float t = (float)(y - p.y) / (float)(q.y - p.y);
        x = p.x + t * (q.x - p.x); return true;
    };
    for (int y = minY; y <= maxY; ++y) {
        float xs[3]; int n = 0; float xv;
        if (edgeX(a, b, y, xv)) xs[n++] = xv;
        if (edgeX(b, c, y, xv)) xs[n++] = xv;
        if (edgeX(c, a, y, xv)) xs[n++] = xv;
        if (n >= 2) {
            int x0 = (int)std::min(xs[0], xs[1]), x1 = (int)std::max(xs[0], xs[1]);
            r.DrawLine(x0, y, x1, y);
        }
    }
}

// --- Resolution-independent fills for the small shapes (filled circle / ring / triangle /
// thick line / convex polygon: minimap, joystick, check marks). They are described in logical
// pixels but rasterised on the DEVICE pixel grid (logical size x the canvas scale), with
// analytic coverage, so a magnified canvas shows a smooth edge instead of logical-pixel blocks.
namespace {
struct CovRun { int x, w; std::uint8_t a; };
inline bool SameRuns(const std::vector<CovRun>& a, const std::vector<CovRun>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].x != b[i].x || a[i].w != b[i].w || a[i].a != b[i].a) return false;
    return true;
}
float EdgeCov(float d) { const float c = 0.5f - d; return c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c); }

// Fill the logical bounds [lx0,lx1]x[ly0,ly1] with `col` scaled by cov(devX, devY) (pixel centre,
// device pixels, 0..1). Runs of the same alpha become one rect; identical rows are merged.
template <class Cov>
void FillCoverage(Painter& r, float lx0, float ly0, float lx1, float ly1, RGBA col, Cov cov) {
    const float S = g_uiDeviceScale > 0.05f ? g_uiDeviceScale : 1.0f;
    if (!(lx1 > lx0) || !(ly1 > ly0) || col.a == 0) return;
    float fx0 = lx0 * S - 1.0f, fy0 = ly0 * S - 1.0f, fx1 = lx1 * S + 1.0f, fy1 = ly1 * S + 1.0f;
    if (r.IsClipEnabled()) {
        Rect c; r.GetClipRect(&c);
        fx0 = std::max(fx0, (float)c.x * S); fy0 = std::max(fy0, (float)c.y * S);
        fx1 = std::min(fx1, (float)(c.x + c.w) * S); fy1 = std::min(fy1, (float)(c.y + c.h) * S);
    }
    constexpr float kLim = 16384.0f;
    fx0 = std::max(fx0, -kLim); fy0 = std::max(fy0, -kLim); fx1 = std::min(fx1, kLim); fy1 = std::min(fy1, kLim);
    const int X0 = (int)std::floor(fx0), Y0 = (int)std::floor(fy0), X1 = (int)std::ceil(fx1), Y1 = (int)std::ceil(fy1);
    if (X1 <= X0 || Y1 <= Y0) return;
    const bool unit = std::fabs(S - 1.0f) < 1e-4f;
    auto flush = [&](const std::vector<CovRun>& runs, int y, int h) {
        for (const CovRun& u : runs) {
            if (u.a == 0) continue;
            r.SetDrawColor(col.r, col.g, col.b, u.a);
            if (unit) { Rect q{u.x, y, u.w, h}; r.FillRect(&q); }
            else {
                // A hundredth of a device pixel of bias keeps a software renderer that truncates float
                // rects from losing a whole pixel to rounding error (x / S * S can land just below x).
                FRect q{((float)u.x + 0.01f) / S, ((float)y + 0.01f) / S, ((float)u.w + 0.01f) / S, ((float)h + 0.01f) / S};
                r.FillRectF(&q);
            }
        }
    };
    std::vector<CovRun> prev, cur;
    int prevY = Y0, prevH = 0;
    for (int y = Y0; y < Y1; ++y) {
        cur.clear();
        for (int x = X0; x < X1; ++x) {
            const float cv = cov((float)x + 0.5f, (float)y + 0.5f);
            const int al = (int)(cv * (float)col.a + 0.5f);
            const std::uint8_t a8 = (std::uint8_t)(al < 0 ? 0 : (al > 255 ? 255 : al));
            if (!cur.empty() && cur.back().a == a8 && cur.back().x + cur.back().w == x) ++cur.back().w;
            else cur.push_back(CovRun{x, 1, a8});
        }
        if (prevH > 0 && SameRuns(prev, cur)) { ++prevH; continue; }
        if (prevH > 0) flush(prev, prevY, prevH);
        prev.swap(cur); prevY = y; prevH = 1;
    }
    if (prevH > 0) flush(prev, prevY, prevH);
}

// Convex polygon coverage (vertices in device pixels): the product-free "1 - sum of what each edge
// cuts off" estimate, exact for straight edges and for thin strips.
struct ConvexCov {
    std::vector<float> ax, ay, nx, ny;   // edge start and outward unit normal
    explicit ConvexCov(const std::vector<std::pair<float, float>>& v) {
        float area2 = 0.0f;
        for (std::size_t i = 0; i < v.size(); ++i) {
            const auto& p = v[i]; const auto& q = v[(i + 1) % v.size()];
            area2 += p.first * q.second - q.first * p.second;
        }
        const float sgn = area2 >= 0.0f ? 1.0f : -1.0f;   // outward normal side for either winding
        for (std::size_t i = 0; i < v.size(); ++i) {
            const auto& p = v[i]; const auto& q = v[(i + 1) % v.size()];
            const float ex = q.first - p.first, ey = q.second - p.second;
            const float len = std::sqrt(ex * ex + ey * ey);
            if (len < 1e-5f) continue;
            ax.push_back(p.first); ay.push_back(p.second);
            nx.push_back(sgn * ey / len); ny.push_back(-sgn * ex / len);
        }
    }
    float operator()(float x, float y) const {
        float cut = 0.0f;
        for (std::size_t i = 0; i < ax.size(); ++i) {
            const float d = (x - ax[i]) * nx[i] + (y - ay[i]) * ny[i];
            cut += 1.0f - EdgeCov(d);
            if (cut >= 1.0f) return 0.0f;
        }
        return 1.0f - cut;
    }
};

void FillConvexSmooth(Painter& r, const std::vector<std::pair<float, float>>& logical, RGBA col) {
    if (logical.size() < 3) return;
    const float S = g_uiDeviceScale > 0.05f ? g_uiDeviceScale : 1.0f;
    std::vector<std::pair<float, float>> dev;
    float minx = logical[0].first, maxx = minx, miny = logical[0].second, maxy = miny;
    for (const auto& q : logical) {
        dev.push_back({q.first * S, q.second * S});
        minx = std::min(minx, q.first); maxx = std::max(maxx, q.first);
        miny = std::min(miny, q.second); maxy = std::max(maxy, q.second);
    }
    FillCoverage(r, minx, miny, maxx, maxy, col, ConvexCov(dev));
}
} // namespace

void MMFillCircle(Painter& r, int cx, int cy, int rad, RGBA col) {
    if (!g_uiSmoothEdges) { MMFillCircleHard(r, cx, cy, rad, col); return; }
    if (rad < 1) rad = 1;
    const float S = g_uiDeviceScale > 0.05f ? g_uiDeviceScale : 1.0f;
    const float mx = ((float)cx + 0.5f) * S, my = ((float)cy + 0.5f) * S, R = ((float)rad + 0.5f) * S;
    FillCoverage(r, (float)cx + 0.5f - (float)rad - 0.5f, (float)cy + 0.5f - (float)rad - 0.5f,
                 (float)cx + 0.5f + (float)rad + 0.5f, (float)cy + 0.5f + (float)rad + 0.5f, col,
                 [=](float x, float y) { return EdgeCov(std::sqrt((x - mx) * (x - mx) + (y - my) * (y - my)) - R); });
}
void MMDrawRing(Painter& r, int cx, int cy, int rad, int width, RGBA col) {
    if (!g_uiSmoothEdges) { MMDrawRingHard(r, cx, cy, rad, width, col); return; }
    if (rad < 1) rad = 1;
    if (width < 1) width = 1;
    const float S = g_uiDeviceScale > 0.05f ? g_uiDeviceScale : 1.0f;
    const float mx = ((float)cx + 0.5f) * S, my = ((float)cy + 0.5f) * S;
    const float mid = ((float)rad + 0.5f) * S, half = (float)width * 0.5f * S;
    const float ext = (float)rad + 0.5f + (float)width * 0.5f;
    FillCoverage(r, (float)cx + 0.5f - ext, (float)cy + 0.5f - ext, (float)cx + 0.5f + ext, (float)cy + 0.5f + ext, col,
                 [=](float x, float y) {
                     const float d = std::fabs(std::sqrt((x - mx) * (x - mx) + (y - my) * (y - my)) - mid) - half;
                     return EdgeCov(d);
                 });
}
void MMFillTriangle(Painter& r, Point a, Point b, Point c, RGBA col) {
    if (!g_uiSmoothEdges) { MMFillTriangleHard(r, a, b, c, col); return; }
    FillConvexSmooth(r, {{(float)a.x, (float)a.y}, {(float)b.x, (float)b.y}, {(float)c.x, (float)c.y}}, col);
}
// A line segment with thickness (butt ends), for minimap roads/walls and check marks.
void MMThickLine(Painter& r, float x0, float y0, float x1, float y1, float width, RGBA col) {
    float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx*dx + dy*dy);
    if (len < 1e-3f) return;
    float nx = -dy / len * width * 0.5f, ny = dx / len * width * 0.5f;
    if (!g_uiSmoothEdges) {
        Point a{(int)(x0+nx),(int)(y0+ny)}, b{(int)(x1+nx),(int)(y1+ny)},
              c{(int)(x1-nx),(int)(y1-ny)}, d{(int)(x0-nx),(int)(y0-ny)};
        MMFillTriangleHard(r, a, b, c, col); MMFillTriangleHard(r, a, c, d, col);
        return;
    }
    FillConvexSmooth(r, {{x0 + nx, y0 + ny}, {x1 + nx, y1 + ny}, {x1 - nx, y1 - ny}, {x0 - nx, y0 - ny}}, col);
}
// Fill a simple (convex) polygon: one coverage pass, so no seams between fan triangles.
void MMFillPoly(Painter& r, const std::vector<Point>& pts, RGBA col) {
    if (!g_uiSmoothEdges) {
        for (std::size_t i = 1; i + 1 < pts.size(); ++i) MMFillTriangleHard(r, pts[0], pts[i], pts[i+1], col);
        return;
    }
    std::vector<std::pair<float, float>> v;
    for (const Point& q : pts) v.push_back({(float)q.x, (float)q.y});
    FillConvexSmooth(r, v, col);
}

// Draw a string with a TTF font (a textured glyph quad per character), tinted by col.
void DrawTtfText(Painter& r, okay::TtfFont* f, const std::string& text,
                 float ox, float oy, float px, RGBA col, float letterSp, float lineSp,
                 bool italic, float boldOffset) {
    // Bake at the on-screen pixel size (logical font size × canvas scale) so the
    // atlas is not a fixed 64px face scaled up or down.
    const float logicalH = px * (float)okay::Font8x8::Height;
    const float deviceH = logicalH * (g_uiDeviceScale > 0.05f ? g_uiDeviceScale : 1.0f);
    const UIQuality quality = g_uiQuality;
    const float S = g_uiDeviceScale > 0.05f ? g_uiDeviceScale : 1.0f;
    okay::TtfFont* sized = quality == UIQuality::Old ? f->ForPixelHeight(deviceH)
                                                     : f->ForDeviceSize(deviceH, quality != UIQuality::Fast);
    if (!sized || !sized->Valid()) sized = f;
    sized->Prepare(text);   // bake new characters before the atlas is uploaded
    if (quality == UIQuality::Best && !italic) {   // the quarter-pixel variants too: they are baked on first use, and must be in the atlas that is uploaded below
        for (std::size_t i = 0; i < text.size();) {
            const std::uint32_t cp = okay::utf8::Next(text, i);
            if (cp > ' ') for (int ph = 1; ph < okay::TtfFont::kPhases; ++ph) sized->GlyphForPhase(cp, ph);
        }
    }
    Tex tex = r.FontAtlas(sized);
    if (!tex) return;
    r.SetTextureColorMod(tex, col.r, col.g, col.b);
    r.SetTextureAlphaMod(tex, col.a);
    int atlasW = 0, atlasH = 0;
    if (italic) r.QueryTexture(tex, &atlasW, &atlasH);
    const bool shear = italic && atlasW > 0 && atlasH > 0;
    const float s = logicalH / sized->BakeHeight();
    const float baseline = sized->Ascent(sized->BakeHeight()) * s;
    const float kSlant = 0.22f;                       // how far the top of a glyph leans, per unit of height
    const int taps = boldOffset > 0.0f ? 3 : 1;
    // One atlas pixel per screen pixel (the face was baked at the size it is drawn): place every glyph on the
    // device pixel grid so the bitmap is copied as it is, not blurred by a fractional-position resample. Best also
    // keeps the quarter-pixel fraction of the pen by picking a glyph rendered for that phase.
    const bool snap = quality != UIQuality::Old && std::fabs(s * S - 1.0f) < 0.02f;
    const bool phased = snap && quality == UIQuality::Best && !italic;
    const bool kerning = quality != UIQuality::Old;
    std::uint32_t prevCh = 0;
    float penX = ox, penY = oy;
    for (std::size_t i = 0; i < text.size();) {
        const std::uint32_t ch = okay::utf8::Next(text, i);
        if (ch == '\n') { penY += sized->LineHeight(sized->BakeHeight()) * s + lineSp * px; penX = ox; prevCh = 0; continue; }
        if (kerning && prevCh) penX += sized->Kerning(prevCh, ch, sized->BakeHeight()) * s;
        prevCh = ch;
        const okay::TtfFont::Glyph* g = sized->GlyphFor(ch);
        if (!g) { if (ch >= ' ') penX += px * 4.0f; continue; }
        int gw = g->x1 - g->x0, gh = g->y1 - g->y0;
        if (gw > 0 && gh > 0) {
            for (int t = 0; t < taps; ++t) {
                const float dx = taps > 1 ? boldOffset * 0.5f * (float)t : 0.0f;
                FRect dst{penX + dx + g->xoff * s, penY + baseline + g->yoff * s, gw * s, gh * s};
                const okay::TtfFont::Glyph* gt = g;
                if (snap) {
                    const float penDev = (penX + dx) * S;
                    float ip = std::floor(penDev);
                    int phase = 0;
                    if (phased) {
                        phase = (int)std::floor((penDev - ip) * 4.0f + 0.5f);
                        if (phase >= 4) { phase = 0; ip += 1.0f; }
                        if (phase > 0) gt = sized->GlyphForPhase(ch, phase);
                        if (!gt) gt = g;
                    } else {
                        ip = std::floor(penDev + 0.5f);
                    }
                    const float baseDev = std::floor((penY + baseline) * S + 0.5f);
                    const float bias = 0.01f;   // a software renderer truncates float rects: nudge off the grid line (see FloatRun)
                    dst = FRect{(ip + g->xoff + bias) / S, (baseDev + g->yoff + bias) / S,
                                (float)(gt->x1 - gt->x0) / S, (float)gh / S};
                }
                Rect src{gt->x0, gt->y0, gt->x1 - gt->x0, gh};
                if (!shear) { r.CopyF(tex, &src, &dst); continue; }
                // Shear about the baseline: points higher above it lean further right.
                const float base = penY + baseline;
                const float topLean = (base - dst.y) * kSlant, botLean = (base - (dst.y + dst.h)) * kSlant;
                const float u0 = (float)g->x0 / (float)atlasW, u1 = (float)g->x1 / (float)atlasW;
                const float v0 = (float)g->y0 / (float)atlasH, v1 = (float)g->y1 / (float)atlasH;
                const RGBA white{255, 255, 255, 255};
                Vertex v[4] = {
                    {FPoint{dst.x + topLean, dst.y}, white, FPoint{u0, v0}},
                    {FPoint{dst.x + dst.w + topLean, dst.y}, white, FPoint{u1, v0}},
                    {FPoint{dst.x + dst.w + botLean, dst.y + dst.h}, white, FPoint{u1, v1}},
                    {FPoint{dst.x + botLean, dst.y + dst.h}, white, FPoint{u0, v1}}};
                const int idx[6] = {0, 1, 2, 0, 2, 3};
                r.Geometry(tex, v, 4, idx, 6);
            }
        }
        penX += g->xadvance * s + letterSp * px;
    }
}

// Scene-wide default UI font (from Scene::uiFont): DrawUIText falls back to it when no
// explicit font is given. Set around the screen-UI pass and cleared after, so world
// text without its own font keeps the bitmap font (matching the editor preview).
void SetUIDefaultFont(TtfFont* f) { g_uiDefaultFont = f; }
TtfFont* UIDefaultFont() { return g_uiDefaultFont; }
// Width of `text` in screen pixels at font-pixel size `px`, in the font DrawUIText will
// use (the scene's UI font during the UI pass, else the 8x8 bitmap font).
float UIMeasureText(TtfFont* font, const std::string& text, float px) {
    TtfFont* f = FontForText(font ? font : g_uiDefaultFont, text);
    if (px < 0.15f) px = 0.15f;
    if (!(f && f->Valid())) {
        if (px < 1.0f) px = 1.0f;
        return (float)okay::utf8::Length(text) * (Font8x8::Width + 1) * px;
    }
    const float logicalH = px * (float)Font8x8::Height;
    const float deviceH = logicalH * (g_uiDeviceScale > 0.05f ? g_uiDeviceScale : 1.0f);
    TtfFont* sized = f->ForPixelHeight(deviceH);
    if (!sized || !sized->Valid()) sized = f;
    return sized->Measure(text.c_str(), sized->BakeHeight(), g_uiQuality != UIQuality::Old) * (logicalH / sized->BakeHeight());
}
float UITextW(const std::string& text, float px) {
    return UIMeasureText(g_uiDefaultFont, text, px);
}

// Draw a string with the built-in 8x8 font as filled rects, top-left at (ox, oy)
// in screen pixels, each font pixel `px` screen pixels wide. When `ttf` is loaded,
// the TTF atlas is used instead (same sizing space).
static void DrawUITextImpl(Painter& r, const std::string& text, float ox, float oy,
                           float px, RGBA col, float letterSp, float lineSp,
                           bool italic, bool gradient, RGBA col2, TtfFont* ttf, bool bold) {
    if (!ttf) ttf = g_uiDefaultFont;   // scene-wide default UI font (set during the UI pass)
    ttf = FontForText(ttf, text);      // a font with the text's characters (other languages)
    if (px < 0.15f) px = 0.15f;   // a smooth font scales below one font pixel; the 8x8 font can't
    if (ttf && ttf->Valid()) { DrawTtfText(r, ttf, text, ox, oy, px, col, letterSp, lineSp, italic, bold ? (px * 8.0f >= 30.0f ? 1.5f : 1.0f) : 0.0f); return; }
    if (px < 1.0f) px = 1.0f;
    if (bold) {   // the 8x8 font has one weight: draw it again one font pixel to the right
        DrawUITextImpl(r, text, ox + px, oy, px, col, letterSp, lineSp, italic, gradient, col2, nullptr, false);
    }
    if (!gradient) r.SetDrawColor(col.r, col.g, col.b, col.a);
    const float slant = italic ? 0.30f : 0.0f;   // px shift per row up from the baseline
    float cx = ox;
    for (std::size_t i = 0; i < text.size();) {
        const std::uint32_t cp = okay::utf8::Next(text, i);
        const char ch = cp < 0x80 ? (char)cp : '\0';   // the 8x8 font has ASCII only
        if (ch == '\n') { oy += (Font8x8::Height + 1 + lineSp) * px; cx = ox; continue; }
        for (int y = 0; y < Font8x8::Height; ++y) {
            if (gradient) {   // top row uses col, bottom row uses col2
                float t = (float)y / (float)(Font8x8::Height - 1);
                r.SetDrawColor((std::uint8_t)(col.r + (col2.r - col.r) * t),
                                       (std::uint8_t)(col.g + (col2.g - col.g) * t),
                                       (std::uint8_t)(col.b + (col2.b - col.b) * t),
                                       (std::uint8_t)(col.a + (col2.a - col.a) * t));
            }
            float sx = (Font8x8::Height - 1 - y) * slant * px;   // italic: higher rows lean right
            for (int x = 0; x < Font8x8::Width; ++x)
                if (Font8x8::Pixel(ch, x, y)) {
                    Rect cell{(int)(cx + x * px + sx), (int)(oy + y * px),
                                  (int)px + 1, (int)px + 1};
                    r.FillRect(&cell);
                }
        }
        cx += (Font8x8::Width + 1 + letterSp) * px; // inter-glyph gap + letter spacing
    }
}

void DrawUICross(Painter& ren, float x, float y, float size, RGBA col) {
    if (!(size >= 2.0f)) return;
    const float S = g_uiDeviceScale > 0.05f ? g_uiDeviceScale : 1.0f;
    const int n = std::max(2, (int)std::lround(size * S));
    const float pad = (float)n * 0.18f;
    const float ax = pad, ay = pad, bx = (float)n - pad, by = (float)n - pad;
    const float thick = std::max(1.25f, (float)n * 0.14f);
    auto dist = [](float px, float py, float x0, float y0, float x1, float y1) {
        const float dx = x1 - x0, dy = y1 - y0;
        const float len2 = dx * dx + dy * dy;
        float t = len2 > 1e-6f ? ((px - x0) * dx + (py - y0) * dy) / len2 : 0.0f;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        const float qx = x0 + t * dx, qy = y0 + t * dy;
        const float ex = px - qx, ey = py - qy;
        return std::sqrt(ex * ex + ey * ey);
    };
    for (int py = 0; py < n; ++py) {
        for (int px = 0; px < n; ++px) {
            const float cx = (float)px + 0.5f, cy = (float)py + 0.5f;
            const float d = std::min(dist(cx, cy, ax, ay, bx, by), dist(cx, cy, ax, by, bx, ay));
            float cov = thick * 0.5f + 0.55f - d;
            if (cov <= 0.0f) continue;
            if (cov > 1.0f) cov = 1.0f;
            ren.SetDrawColor(col.r, col.g, col.b, (std::uint8_t)(col.a * cov + 0.5f));
            if (S <= 1.001f) {
                Rect q{(int)std::lround(x) + px, (int)std::lround(y) + py, 1, 1};
                ren.FillRect(&q);
            } else {
                FRect q{x + (float)px / S, y + (float)py / S, 1.0f / S, 1.0f / S};
                ren.FillRectF(&q);
            }
        }
    }
}

void DrawUIText(Painter& r, const std::string& text, float ox, float oy,
                float px, RGBA col, float letterSp, float lineSp,
                bool italic, bool gradient, RGBA col2, TtfFont* ttf, bool bold) {
    if (Accessibility::Get().highContrast) {   // text pushed to 7:1 against what is behind it
        auto fix = [](RGBA c) {
            const Color f = Accessibility::TextColor(Color::FromBytes(c.r, c.g, c.b, 255), g_backdrop);
            return RGBA{(std::uint8_t)(f.r * 255.0f + 0.5f), (std::uint8_t)(f.g * 255.0f + 0.5f), (std::uint8_t)(f.b * 255.0f + 0.5f), c.a};
        };
        col = fix(col);
        if (gradient) col2 = fix(col2);
    }
    const bool was = g_inText;
    g_inText = true;
    DrawUITextImpl(r, text, ox, oy, px, col, letterSp, lineSp, italic, gradient, col2, ttf, bold);
    g_inText = was;
}

// Rich text: draw `disp` (already free of tags) in runs, each in the colour and weight its characters
// carry in `styles` (one per visible character, in order). Each run is placed by measuring the text before
// it on its line, so any font lines up. The lines advance like DrawUIText's, except that a line is as tall as the
// largest [size] on it (RichLineScales): the line below starts under the big run instead of the big run climbing over
// the line above, and a smaller run sits on its line's bottom. (oy is the top of the first line.)
// The pictures an inline [icon=name] draws (okay/UI/RichText.hpp): a path to an image, else an item from the item
// database (its own icon when it has one, else a tile in its rarity or a hash colour with its first letter).
void DrawRichIcon(Painter& r, const std::string& name, float x, float y, float cell, RGBA tint) {
    if (const std::string vname = uilib::IconNameOf(name); !vname.empty()) {   // [icon=icon:heart]: one of our vector icons
        if (DrawVectorIcon(r, vname, (int)x, (int)y, (int)cell, tint, g_uiDeviceScale)) return;
    }
    const bool isPath = name.find('.') != std::string::npos || name.find('/') != std::string::npos;
    const std::string id = isPath ? std::string() : okay::ItemDatabase::Get().Resolve(name);
    std::string path = isPath ? name : okay::ItemIconOf("", id).path;
    if (!path.empty()) {
        if (Tex t = r.LoadTexture(path)) {
            FRect dst{x, y, cell, cell};
            r.SetTextureAlphaMod(t, tint.a);
            r.CopyF(t, nullptr, &dst);
            return;
        }
    }
    const okay::ItemDefinition* d = id.empty() ? nullptr : okay::ItemDatabase::Get().Find(id);
    unsigned h = 2166136261u;
    for (unsigned char c : name) { h ^= c; h *= 16777619u; }
    std::uint8_t cr = (std::uint8_t)(70 + (h & 0x7F)), cg = (std::uint8_t)(70 + ((h >> 7) & 0x7F)), cb = (std::uint8_t)(70 + ((h >> 14) & 0x7F));
    if (d && d->hasRarityColor) { cr = (std::uint8_t)(d->rarityColor.r * 255); cg = (std::uint8_t)(d->rarityColor.g * 255); cb = (std::uint8_t)(d->rarityColor.b * 255); }
    const float in = cell > 6.0f ? 1.0f : 0.0f;
    FRect box{x + in, y + in, cell - in * 2.0f, cell - in * 2.0f};
    r.SetDrawColor(cr, cg, cb, tint.a);
    r.FillRectF(&box);
    std::string letter(1, '?');
    const std::string& src = (d && !d->name.empty()) ? d->name : name;
    for (char c : src) if (std::isalnum((unsigned char)c)) { letter.assign(1, (char)std::toupper((unsigned char)c)); break; }
    const float px = cell / (float)Font8x8::Height * 0.8f;
    DrawUIText(r, letter, x + (cell - 8.0f * px) * 0.5f, y + (cell - 8.0f * px) * 0.5f, px, RGBA{255, 255, 255, tint.a});
}

// The rectangles of the links the last draw of a text produced, per text object, in the space the text was drawn in.
std::unordered_map<const okay::GameObject*, std::vector<RichLinkRect>>& ui_pass::RichLinkTable() {
    static thread_local std::unordered_map<const okay::GameObject*, std::vector<RichLinkRect>> t;
    return t;
}
const std::vector<RichLinkRect>& RichLinksOf(const okay::GameObject* go) {
    static const std::vector<RichLinkRect> none;
    auto& t = RichLinkTable();
    auto it = t.find(go);
    return it == t.end() ? none : it->second;
}

void ui_pass::DrawRichRuns(Painter& r, const std::string& disp, const std::vector<okay::RichStyle>& styles,
                  float ox, float oy, float px, RGBA base, float letterSp, float lineSp, bool italic,
                  bool bold, float boldOff, TtfFont* ttf,
                  const std::vector<std::string>* links, const std::vector<std::string>* icons, std::vector<RichLinkRect>* linkRects) {
    TtfFont* f = FontForText(ttf ? ttf : g_uiDefaultFont, disp);
    const float logicalH = px * (float)Font8x8::Height;
    const float deviceH = logicalH * (g_uiDeviceScale > 0.05f ? g_uiDeviceScale : 1.0f);
    TtfFont* sized = (f && f->Valid()) ? f->ForPixelHeight(deviceH) : nullptr;
    const float adv = (sized && sized->Valid())
                          ? sized->LineHeight(sized->BakeHeight()) * (logicalH / sized->BakeHeight()) + lineSp * px
                          : (Font8x8::Height + 1 + lineSp) * px;
    (void)boldOff;   // bold is drawn by DrawUIText itself; kept so every caller states the weight offset it draws with
    const std::vector<float> lineScale = okay::RichLineScales(disp, styles);
    std::size_t glyph = 0, lineStart = 0, lineNo = 0;
    float y = oy;
    while (lineStart <= disp.size()) {
        const float S = lineNo < lineScale.size() ? lineScale[lineNo] : 1.0f;   // the largest size on this line
        ++lineNo;
        std::size_t lineEnd = disp.find('\n', lineStart);
        if (lineEnd == std::string::npos) lineEnd = disp.size();
        const std::string line = disp.substr(lineStart, lineEnd - lineStart);
        std::size_t runStart = 0;
        okay::RichStyle runStyle;
        bool haveRun = false;
        // Runs are placed one after the other: each starts where the one before ended at its own size ([size=N]).
        // With every size 1 this is the same as measuring the text before the run.
        float cx = ox;
        std::size_t cxAt = 0;
        auto flush = [&](std::size_t end) {
            if (!haveRun || end <= runStart) return;
            if (runStart > cxAt) {   // leading spaces before the first run, at the text's own size
                const std::string gap = line.substr(cxAt, runStart - cxAt);
                cx += UITextW(gap, px) + letterSp * px * (float)okay::utf8::Length(gap);
            }
            const std::string run = line.substr(runStart, end - runStart);
            const float s = runStyle.size > 0.0f ? runStyle.size : 1.0f;
            const float rpx = px * s;
            RGBA col = base;
            if (runStyle.hasColor) col = RGBA{runStyle.r, runStyle.g, runStyle.b, (std::uint8_t)(runStyle.a * base.a / 255)};
            const bool it = italic || runStyle.italic;
            // Every run sits on the bottom of its line: the line is as tall as its largest run (S), a smaller run starts lower.
            const float ry = y + (S - s) * (float)Font8x8::Height * px;
            float runW, inkW = 0.0f;
            if (runStyle.icon >= 0 && icons && (std::size_t)runStyle.icon < icons->size()) {   // [icon=...]: one glyph cell, drawn as a picture
                const float cell = (float)Font8x8::Height * rpx;
                runW = cell + rpx * 2.0f;                                   // a little air after the picture
                DrawRichIcon(r, (*icons)[(std::size_t)runStyle.icon], cx, ry, cell, col.a ? RGBA{255, 255, 255, col.a} : base);
            } else {
                DrawUIText(r, run, cx, ry, rpx, col, letterSp, lineSp, it, false, RGBA{0, 0, 0, 0}, ttf, bold || runStyle.bold);
                runW = UITextW(run, rpx) + letterSp * rpx * (float)okay::utf8::Length(run);
                if (runStyle.underline || runStyle.link >= 0) {
                    std::string shown = run;                                // the space that joins a run to the next is not underlined
                    while (!shown.empty() && shown.back() == ' ') shown.pop_back();
                    inkW = UITextW(shown, rpx) + letterSp * rpx * (float)okay::utf8::Length(shown);
                }
                if (runStyle.underline) {
                    const float uy = ry + (float)Font8x8::Height * rpx + 1.0f;
                    r.SetDrawColor(col.r, col.g, col.b, col.a);
                    FRect line{cx, uy, inkW, rpx > 1.5f ? 2.0f : 1.0f};
                    r.FillRectF(&line);
                }
            }
            if (runStyle.link >= 0 && links && linkRects && (std::size_t)runStyle.link < links->size())
                linkRects->push_back(RichLinkRect{cx, ry, inkW > 0.0f ? inkW : runW, (float)Font8x8::Height * rpx + 3.0f, (*links)[(std::size_t)runStyle.link]});
            cx += runW;
            cxAt = end;
        };
        for (std::size_t i = 0; i < line.size();) {
            const std::size_t at = i;
            const std::uint32_t cp = okay::utf8::Next(line, i);
            if (cp == ' ' || cp == '\t') continue;       // a space joins the run it sits in
            const okay::RichStyle st = glyph < styles.size() ? styles[glyph] : okay::RichStyle();
            ++glyph;
            if (!haveRun) { runStart = at; runStyle = st; haveRun = true; }
            else if (!(st == runStyle)) { flush(at); runStart = at; runStyle = st; }
        }
        flush(line.size());
        y += okay::RichLineAdvance(adv, lineSp * px, S);
        if (lineEnd >= disp.size()) break;
        lineStart = lineEnd + 1;
    }
}

void DrawWorldText(Painter& p, const TextRenderer& tr, float x, float y, float px, float opacity) {
    auto rgba = [&](const Color& c) {
        return RGBA{(std::uint8_t)(c.r * 255), (std::uint8_t)(c.g * 255), (std::uint8_t)(c.b * 255), (std::uint8_t)(c.a * 255 * opacity)};
    };
    TtfFont* fnt = tr.Font();
    const bool rich = tr.richText && RichTextHasTags(tr.text);
    const okay::RichText rt = rich ? okay::ParseRichText(tr.text) : okay::RichText();
    const std::string& shown = rich ? rt.plain : tr.text;
    const float fo = fnt ? 1.0f : px;   // TTF: about one screen pixel, not a whole font pixel
    const RGBA none{0, 0, 0, 0};
    if (tr.shadow) {
        if (rich) DrawRichRuns(p, shown, rt.styles, x + tr.shadowOffset.x * px, y + tr.shadowOffset.y * px, px, rgba(tr.shadowColor), 0, 0, tr.italic, tr.bold, fo, fnt);
        else DrawUIText(p, shown, x + tr.shadowOffset.x * px, y + tr.shadowOffset.y * px, px, rgba(tr.shadowColor), 0, 0, false, false, none, fnt);
    }
    if (tr.outline) {
        const float dx[4] = {-fo, fo, 0, 0}, dy[4] = {0, 0, -fo, fo};
        for (int k = 0; k < 4; ++k) {
            if (rich) {
                // The outline keeps the outline colour: strip the colours, keep sizes and weights.
                std::vector<okay::RichStyle> st = rt.styles;
                for (auto& s : st) s.hasColor = false;
                DrawRichRuns(p, shown, st, x + dx[k], y + dy[k], px, rgba(tr.outlineColor), 0, 0, tr.italic, tr.bold, fo, fnt);
            } else DrawUIText(p, shown, x + dx[k], y + dy[k], px, rgba(tr.outlineColor), 0, 0, false, false, none, fnt);
        }
    }
    if (rich) DrawRichRuns(p, shown, rt.styles, x, y, px, rgba(tr.color), 0, 0, tr.italic, tr.bold, fo, fnt);
    else {
        // Rich text off: exactly the look world text always had (no italic; bold is a second copy one step right).
        DrawUIText(p, shown, x, y, px, rgba(tr.color), 0, 0, false, false, none, fnt);
        if (tr.bold) DrawUIText(p, shown, x + fo, y, px, rgba(tr.color), 0, 0, false, false, none, fnt);
    }
}


void RenderSceneUI(Scene& scene, Painter& p, int w, int h, const UIRenderOptions& opt) {
    const AccessibilitySettings& a = Accessibility::Get();
    g_backdrop = Color{0.06f, 0.06f, 0.08f, 1.0f};
    if (a.colorblind == ColorblindMode::None && !a.highContrast) { RenderSceneUIImpl(scene, p, w, h, opt); return; }   // untouched
    A11yPainter filtered(p);
    RenderSceneUIImpl(scene, filtered, w, h, opt);
}

} // namespace ui2d
} // namespace okay
