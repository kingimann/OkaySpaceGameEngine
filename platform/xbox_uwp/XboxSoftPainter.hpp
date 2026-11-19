#pragma once
// okay::platform::Painter for the Xbox UWP app: draws the engine's 2D / UI layer (RenderSceneUI) with XboxSoftRaster.hpp into a premultiplied RGBA
// buffer. It replaces SDL_Renderer (not available in UWP) and okayui's software painter (which would pull in the whole okayui library). The app
// uploads the buffer as a Direct3D 11 texture and composites it over the 3D frame (XboxOverlay.hpp). Written from engine/include/okay/Platform/Painter.hpp
// and UI/UIPainter.hpp (the interfaces it implements) and the semantics of the SDL calls those interfaces mirror (colour state, clip, scale, flips,
// vertex colour times texture), from the C++ standard. LoadTexture / FontAtlas / UploadAtlas come from the engine's shared Painter.cpp.
// Syntax-checked on Linux; the raster core under it is tested there. UNVERIFIED with the real UI pass on a console.
#include "XboxSoftRaster.hpp"
#include "okay/Platform/Painter.hpp"
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace okay::xbox {

class XboxSoftPainter : public okay::platform::Painter {
public:
    explicit XboxSoftPainter(int w = 1, int h = 1) : surf_(w, h) {}
    ~XboxSoftPainter() override { ReleaseCachedTextures(); for (SoftTexture* t : textures_) delete t; }

    SoftSurface& Surface() { return surf_; }
    void Resize(int w, int h) { surf_.Resize(w, h); }

    // ---- okay::platform::Painter ----
    okay::platform::PainterKind Kind() const override { return okay::platform::PainterKind::Software; }
    void* NativeRenderer() override { return nullptr; }
    void OutputSize(int* w, int* h) override { if (w) *w = surf_.Width(); if (h) *h = surf_.Height(); }
    void Clear() override { surf_.FillRect(IRect{0, 0, surf_.Width(), surf_.Height()}, cR_, cG_, cB_, cA_, false, IRect{0, 0, surf_.Width(), surf_.Height()}); surf_.MarkAll(); }
    void SetBlendMode(okay::platform::BlendMode m) override { blend_ = m; }
    okay::platform::BlendMode GetBlendMode() override { return blend_; }
    void FillRectF(const okay::ui2d::FRect* r) override {
        if (!r || r->w <= 0 || r->h <= 0) return;
        const int x0 = (int)std::floor(r->x * sx_), y0 = (int)std::floor(r->y * sy_), x1 = (int)std::ceil((r->x + r->w) * sx_), y1 = (int)std::ceil((r->y + r->h) * sy_);
        surf_.FillRect(IRect{x0, y0, x1 - x0, y1 - y0}, cR_, cG_, cB_, cA_, Blend(), ClipDev());
    }
    void DrawLineF(float x0, float y0, float x1, float y1) override { DrawLine((int)std::lround(x0), (int)std::lround(y0), (int)std::lround(x1), (int)std::lround(y1)); }
    okay::ui2d::Tex CreateTexture(const okay::platform::TextureDesc& d, const std::uint8_t* rgba) override {
        if (d.width < 1 || d.height < 1) return nullptr;
        SoftTexture* t = new SoftTexture(); t->w = d.width; t->h = d.height; t->px.assign((std::size_t)d.width * (std::size_t)d.height * 4, 0);
        if (rgba) { std::memcpy(t->px.data(), rgba, t->px.size()); PremultiplyRgba(t->px.data(), (std::size_t)d.width * (std::size_t)d.height); }
        t->blend = d.blend != okay::platform::BlendMode::None; t->linear = d.scale == okay::platform::ScaleMode::Linear;
        textures_.insert(t); return t;
    }
    bool UpdateTexture(okay::ui2d::Tex tex, const okay::ui2d::Rect* r, const void* rgba, int pitch) override {
        SoftTexture* t = Get(tex); if (!t || !rgba) return false;
        const okay::ui2d::Rect whole{0, 0, t->w, t->h}; okay::ui2d::Rect q = whole;
        if (r) { if (!okay::ui2d::IntersectRect(r, &whole, &q) || q.w != r->w || q.h != r->h) return false; }
        const int rowBytes = q.w * 4; const int p = pitch > 0 ? pitch : rowBytes;
        for (int y = 0; y < q.h; ++y) {
            std::uint8_t* d = &t->px[((std::size_t)(q.y + y) * (std::size_t)t->w + (std::size_t)q.x) * 4];
            std::memcpy(d, static_cast<const std::uint8_t*>(rgba) + (std::size_t)y * (std::size_t)p, (std::size_t)rowBytes);
            PremultiplyRgba(d, (std::size_t)q.w);
        }
        return true;
    }
    void DestroyTexture(okay::ui2d::Tex tex) override { SoftTexture* t = Get(tex); if (t) { textures_.erase(t); delete t; } }
    void SetTextureBlendMode(okay::ui2d::Tex tex, okay::platform::BlendMode m) override { if (SoftTexture* t = Get(tex)) t->blend = m != okay::platform::BlendMode::None; }
    void SetTextureScaleMode(okay::ui2d::Tex tex, okay::platform::ScaleMode m) override { if (SoftTexture* t = Get(tex)) t->linear = m == okay::platform::ScaleMode::Linear; }
    void Present() override {}                 // the app uploads Surface() to the GPU (XboxOverlay.hpp)
    bool SetVSync(bool) override { return false; }
    bool ReadPixels(std::vector<std::uint8_t>& rgba, int& w, int& h) override {   // straight (un-premultiplied) RGBA, for screenshots and tests
        w = surf_.Width(); h = surf_.Height(); rgba.assign(surf_.Data(), surf_.Data() + surf_.Bytes());
        for (std::size_t i = 0; i < rgba.size(); i += 4) { const unsigned a = rgba[i + 3]; if (a > 0 && a < 255) for (int c = 0; c < 3; ++c) rgba[i + (std::size_t)c] = (std::uint8_t)std::min(255u, (rgba[i + (std::size_t)c] * 255u + a / 2) / a); }
        return true;
    }

    // ---- okay::ui2d::Painter ----
    void SetDrawColor(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) override { cR_ = r; cG_ = g; cB_ = b; cA_ = a; }
    void FillRect(const okay::ui2d::Rect* r) override {
        if (!r) { surf_.FillRect(IRect{0, 0, surf_.Width(), surf_.Height()}, cR_, cG_, cB_, cA_, Blend(), ClipDev()); return; }
        surf_.FillRect(DevRect(*r), cR_, cG_, cB_, cA_, Blend(), ClipDev());
    }
    void DrawRect(const okay::ui2d::Rect* r) override {
        if (!r || r->w <= 0 || r->h <= 0) return;
        const int x1 = r->x + r->w - 1, y1 = r->y + r->h - 1;
        DrawLine(r->x, r->y, x1, r->y); DrawLine(x1, r->y, x1, y1); DrawLine(x1, y1, r->x, y1); DrawLine(r->x, y1, r->x, r->y);
    }
    void DrawLine(int x0, int y0, int x1, int y1) override {
        surf_.Line((int)std::lround(x0 * sx_), (int)std::lround(y0 * sy_), (int)std::lround(x1 * sx_), (int)std::lround(y1 * sy_), cR_, cG_, cB_, cA_, Blend(), ClipDev());
    }
    void DrawPoint(int x, int y) override { surf_.Point((int)std::lround(x * sx_), (int)std::lround(y * sy_), cR_, cG_, cB_, cA_, Blend(), ClipDev()); }
    void SetClipRect(const okay::ui2d::Rect* r) override { if (r) { clip_ = *r; clipOn_ = true; } else clipOn_ = false; }
    void GetClipRect(okay::ui2d::Rect* r) override { if (r) *r = clipOn_ ? clip_ : okay::ui2d::Rect{0, 0, (int)std::ceil(surf_.Width() / sx_), (int)std::ceil(surf_.Height() / sy_)}; }
    bool IsClipEnabled() override { return clipOn_; }
    void SetScale(float sx, float sy) override { sx_ = sx > 0 ? sx : 1.0f; sy_ = sy > 0 ? sy : 1.0f; }
    okay::ui2d::Tex LoadTexture(const std::string& path) override { return okay::platform::Painter::LoadTexture(path); }
    okay::ui2d::Tex FontAtlas(okay::TtfFont* font) override { return okay::platform::Painter::FontAtlas(font); }
    void QueryTexture(okay::ui2d::Tex tex, int* w, int* h) override { SoftTexture* t = Get(tex); if (w) *w = t ? t->w : 0; if (h) *h = t ? t->h : 0; }
    void SetTextureColorMod(okay::ui2d::Tex tex, std::uint8_t r, std::uint8_t g, std::uint8_t b) override { if (SoftTexture* t = Get(tex)) { t->modR = r; t->modG = g; t->modB = b; } }
    void SetTextureAlphaMod(okay::ui2d::Tex tex, std::uint8_t a) override { if (SoftTexture* t = Get(tex)) t->modA = a; }
    void Copy(okay::ui2d::Tex tex, const okay::ui2d::Rect* src, const okay::ui2d::Rect* dst) override {
        okay::ui2d::FRect f{0, 0, (float)surf_.Width() / sx_, (float)surf_.Height() / sy_};
        if (dst) f = {(float)dst->x, (float)dst->y, (float)dst->w, (float)dst->h};
        CopyExF(tex, src, &f, 0.0, nullptr, 0);
    }
    void CopyF(okay::ui2d::Tex tex, const okay::ui2d::Rect* src, const okay::ui2d::FRect* dst) override { CopyExF(tex, src, dst, 0.0, nullptr, 0); }
    void CopyEx(okay::ui2d::Tex tex, const okay::ui2d::Rect* src, const okay::ui2d::Rect* dst, double angle, const okay::ui2d::Point* center, int flip) override {
        okay::ui2d::FRect f{0, 0, (float)surf_.Width() / sx_, (float)surf_.Height() / sy_}; okay::ui2d::FPoint c{0, 0};
        if (dst) f = {(float)dst->x, (float)dst->y, (float)dst->w, (float)dst->h};
        if (center) c = {(float)center->x, (float)center->y};
        CopyExF(tex, src, &f, angle, center ? &c : nullptr, flip);
    }
    void CopyExF(okay::ui2d::Tex tex, const okay::ui2d::Rect* src, const okay::ui2d::FRect* dst, double angle, const okay::ui2d::FPoint* center, int flip) override {
        SoftTexture* t = Get(tex); if (!t || !dst) return;
        const float dx = dst->x * sx_, dy = dst->y * sy_, dw = dst->w * sx_, dh = dst->h * sy_;
        const float cx = center ? (dst->x + center->x) * sx_ : dx + dw * 0.5f, cy = center ? (dst->y + center->y) * sy_ : dy + dh * 0.5f;   // SDL: the centre is relative to the destination rectangle
        surf_.Blit(*t, src ? IRect{src->x, src->y, src->w, src->h} : IRect{}, dx, dy, dw, dh, angle, cx, cy, (flip & okay::ui2d::FlipH) != 0, (flip & okay::ui2d::FlipV) != 0, ClipDev());
    }
    void Geometry(okay::ui2d::Tex tex, const okay::ui2d::Vertex* verts, int n, const int* idx, int ni) override {
        if (!verts || n < 3) return;
        SoftTexture* t = tex ? Get(tex) : nullptr;
        std::vector<SoftSurface::Vtx> v((std::size_t)n);
        for (int i = 0; i < n; ++i) v[(std::size_t)i] = {verts[i].position.x * sx_, verts[i].position.y * sy_, verts[i].color.r, verts[i].color.g, verts[i].color.b, verts[i].color.a,
                                                          verts[i].tex_coord.x * (t ? (float)t->w : 0.0f), verts[i].tex_coord.y * (t ? (float)t->h : 0.0f)};   // SDL_Vertex uv is 0..1
        surf_.Triangles(t, v.data(), n, idx, idx ? ni : 0, ClipDev());
    }
    void SetImageFilter(bool linear) override { okay::platform::Painter::SetImageFilter(linear); }
    void SetTextureFilter(okay::ui2d::Tex tex, bool linear) override { if (SoftTexture* t = Get(tex)) t->linear = linear; }

private:
    SoftTexture* Get(okay::ui2d::Tex tex) { SoftTexture* t = static_cast<SoftTexture*>(tex); return t && textures_.count(t) ? t : nullptr; }
    bool Blend() const { return blend_ != okay::platform::BlendMode::None; }
    IRect DevRect(const okay::ui2d::Rect& r) const { return IRect{(int)std::lround(r.x * sx_), (int)std::lround(r.y * sy_), (int)std::lround(r.w * sx_), (int)std::lround(r.h * sy_)}; }
    IRect ClipDev() const { return clipOn_ ? DevRect(clip_) : IRect{0, 0, surf_.Width(), surf_.Height()}; }
    SoftSurface surf_;
    std::unordered_set<SoftTexture*> textures_;
    std::uint8_t cR_ = 255, cG_ = 255, cB_ = 255, cA_ = 255;
    okay::platform::BlendMode blend_ = okay::platform::BlendMode::Blend;
    okay::ui2d::Rect clip_{0, 0, 0, 0}; bool clipOn_ = false; float sx_ = 1, sy_ = 1;
};

} // namespace okay::xbox
