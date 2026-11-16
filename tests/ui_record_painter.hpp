#pragma once
// A ui2d::Painter that draws nothing and remembers what the UI pass asked it to draw, so a headless test can measure where
// things really land: filled rectangles (the shapes, and the 8x8 font's pixels) with their colour, and the glyph quads of
// TrueType text with the colour they are tinted with. Boxes are looked up by colour (UI test scenes give every part its
// own colour). No SDL needed. Written for this engine's own tests.
#include "okay/UI/UIPainter.hpp"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <vector>

namespace uitest {

struct InkBox {
    int n = 0;                                  ///< how many rectangles went into it
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    bool Any() const { return n > 0; }
    float W() const { return x1 - x0; }
    float H() const { return y1 - y0; }
    float CX() const { return (x0 + x1) * 0.5f; }
    float CY() const { return (y0 + y1) * 0.5f; }
    /// Is this box inside `o`, give or take `tol` pixels on every side?
    bool InsideOf(const InkBox& o, float tol = 1.0f) const {
        return Any() && o.Any() && x0 >= o.x0 - tol && y0 >= o.y0 - tol && x1 <= o.x1 + tol && y1 <= o.y1 + tol;
    }
};

class RecordPainter : public okay::ui2d::Painter {
public:
    using Rect = okay::ui2d::Rect;
    using FRect = okay::ui2d::FRect;
    using Tex = okay::ui2d::Tex;

    struct Ink { float x, y, w, h; std::uint8_t r, g, b, a; bool glyph; };
    std::vector<Ink> inks;

    /// The union of every rectangle drawn in this colour (each channel within `tol`), alpha ignored.
    InkBox Of(int r, int g, int b, int tol = 2) const {
        InkBox bx;
        for (const Ink& k : inks) {
            if (std::abs((int)k.r - r) > tol || std::abs((int)k.g - g) > tol || std::abs((int)k.b - b) > tol) continue;
            if (k.w <= 0.0f || k.h <= 0.0f) continue;
            ++bx.n;
            bx.x0 = std::min(bx.x0, k.x); bx.y0 = std::min(bx.y0, k.y);
            bx.x1 = std::max(bx.x1, k.x + k.w); bx.y1 = std::max(bx.y1, k.y + k.h);
        }
        return bx;
    }

    void SetDrawColor(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) override { cr = r; cg = g; cb = b; ca = a; }
    void FillRect(const Rect* r) override { if (r) inks.push_back({(float)r->x, (float)r->y, (float)r->w, (float)r->h, cr, cg, cb, ca, false}); }
    void FillRectF(const FRect* r) override { if (r) inks.push_back({r->x, r->y, r->w, r->h, cr, cg, cb, ca, false}); }
    void DrawRect(const Rect*) override {}
    void DrawLine(int, int, int, int) override {}
    void DrawPoint(int, int) override {}
    void SetClipRect(const Rect* r) override { clip = r != nullptr; if (r) clipRect = *r; }
    void GetClipRect(Rect* r) override { *r = clipRect; }
    bool IsClipEnabled() override { return clip; }
    void SetScale(float, float) override {}
    Tex LoadTexture(const std::string&) override { return nullptr; }
    Tex FontAtlas(okay::TtfFont*) override { return &atlas; }   // a non-null handle: the TrueType text is "drawn" (recorded)
    void QueryTexture(Tex, int* w, int* h) override { *w = *h = 0; }
    void SetTextureColorMod(Tex, std::uint8_t r, std::uint8_t g, std::uint8_t b) override { tr = r; tg = g; tb = b; }
    void SetTextureAlphaMod(Tex, std::uint8_t a) override { ta = a; }
    void Copy(Tex, const Rect*, const Rect* d) override { if (d) inks.push_back({(float)d->x, (float)d->y, (float)d->w, (float)d->h, tr, tg, tb, ta, true}); }
    void CopyF(Tex, const Rect*, const FRect* d) override { if (d) inks.push_back({d->x, d->y, d->w, d->h, tr, tg, tb, ta, true}); }
    void CopyEx(Tex, const Rect*, const Rect* d, double, const okay::ui2d::Point*, int) override {
        if (d) inks.push_back({(float)d->x, (float)d->y, (float)d->w, (float)d->h, tr, tg, tb, ta, true});
    }
    void CopyExF(Tex, const Rect*, const FRect* d, double, const okay::ui2d::FPoint*, int) override {
        if (d) inks.push_back({d->x, d->y, d->w, d->h, tr, tg, tb, ta, true});
    }
    void Geometry(Tex, const okay::ui2d::Vertex* v, int n, const int*, int) override {
        if (!v || n < 1) return;
        float x0 = v[0].position.x, y0 = v[0].position.y, x1 = x0, y1 = y0;
        for (int i = 1; i < n; ++i) {
            x0 = std::min(x0, v[i].position.x); x1 = std::max(x1, v[i].position.x);
            y0 = std::min(y0, v[i].position.y); y1 = std::max(y1, v[i].position.y);
        }
        inks.push_back({x0, y0, x1 - x0, y1 - y0, tr, tg, tb, ta, true});
    }
    Tex UploadAtlas(const okay::Image&, int, Tex existing) override { return existing; }

private:
    std::uint8_t cr = 0, cg = 0, cb = 0, ca = 255, tr = 255, tg = 255, tb = 255, ta = 255;
    bool clip = false;
    Rect clipRect{0, 0, 0, 0};
    int atlas = 0;
};

} // namespace uitest
