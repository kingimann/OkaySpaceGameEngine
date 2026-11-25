// The in-game UI pass proper: RenderSceneUIImpl (every widget kind, in draw order), with the per-scene pass cache
// and the turned-frame painter it uses. Moved verbatim out of UIRenderer.cpp (docs/engine/modularization_plan.md, batch U1).
#include "UIRendererInternal.hpp"
#include "okay/UI/UIRenderer.hpp"
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

using ui_pass::g_uiDeviceScale;
using ui_pass::g_uiSmoothEdges;
using ui_pass::g_uiQuality;
using ui_pass::g_uiDefaultFont;
using ui_pass::UITextScale;
using ui_pass::RichLinkTable;
using ui_pass::DrawRichRuns;

// In-world UI (a WorldSpaceUI object, or a widget under a world-space Canvas): the origin the layout helpers return is already
// projected through the camera, but sizes and style metrics (corner radius, border, shadow) are still design pixels. This is
// the screen pixels per design pixel at the widget, so a panel or bar can be drawn at the size its origin and its children
// were placed with. 1 for every widget on screen (nothing changes there); 0 when the widget is behind the camera.
static float WorldWidgetScale(GameObject* g, Canvas* cv, int w, int h) {
    const bool world = (cv && cv->worldSpace) || WorldUIRoot(g) != nullptr;
    return world ? UIScaleFor(g, (float)w, (float)h) : 1.0f;
}

namespace {
// What the UI pass keeps between frames for one scene, so a steady frame neither
// walks every object nor allocates: the draw items of the objects that carry UI and
// the hierarchy pre-order, rebuilt only when the scene's structure (objects created,
// destroyed, reparented or reordered) or components change; plus per-frame scratch
// for each UI object's canvas and opacity. Items keep last frame's order, so the
// per-frame re-sort (uiDrawOrder / Canvas sortOrder can change any frame) is an
// insertion sort over an almost-sorted list.
struct UISlot {
    std::size_t index = 0;    // into scene.Objects()
    Canvas* canvas = nullptr; // owning canvas this frame
    float opacity = 1.0f;     // canvas + UI-animation fade this frame
    int overlay = 0;          // open-order of the UIModal it is shown in this frame (0 = none)
};
struct UIItem {
    int slot = 0, kind = 0, layer = 0;
    int canvasOrder = 0, eff = 0;   // per-frame keys
    std::size_t pre = 0;            // hierarchy pre-order (cached)
};
struct UIPassCache {
    std::uint64_t uid = 0, structure = 0, components = 0;
    std::size_t objectCount = 0;
    std::vector<UISlot> slots;
    std::vector<UIItem> items;
};

UIPassCache& PassCacheFor(const Scene& scene) {
    static UIPassCache caches[4];   // the editor can show a scene in a few views at once
    static int next = 0;
    for (UIPassCache& c : caches) if (c.uid == scene.Uid()) return c;
    UIPassCache& c = caches[next];
    next = (next + 1) % 4;
    c.uid = scene.Uid(); c.structure = 0;   // force a rebuild
    return c;
}

bool ItemBefore(const UIItem& a, const UIItem& b) {
    if (a.canvasOrder != b.canvasOrder) return a.canvasOrder < b.canvasOrder;
    if (a.eff != b.eff) return a.eff < b.eff;
    if (a.pre != b.pre) return a.pre < b.pre;
    return a.layer < b.layer;
}

void RebuildPassCache(UIPassCache& c, const Scene& scene) {
    const auto& objs = scene.Objects();
    c.structure = scene.StructureVersion();
    c.components = scene.ComponentVersion();
    c.objectCount = objs.size();
    c.slots.clear(); c.items.clear();
    std::vector<std::size_t> pre = BuildUIPreOrder(objs);
    std::vector<UIDrawItem> queued;
    for (std::size_t i = 0; i < objs.size(); ++i) {
        queued.clear();
        UIQueueDrawItems(objs[i].get(), i, queued);
        bool any = false;
        for (const UIDrawItem& q : queued) {
            if (q.kind == uilayers::K_FocusRing) continue;   // queued for every UI object below
            if (!any) { UISlot sl; sl.index = i; c.slots.push_back(sl); any = true; }
            UIItem it; it.slot = (int)c.slots.size() - 1; it.kind = q.kind; it.layer = q.layer; it.pre = pre[i];
            c.items.push_back(it);
        }
        // Focus moves without the structure changing, so every UI object carries a
        // focus-ring item; its block draws only while the object is focused.
        if (any) {
            UIItem it; it.slot = (int)c.slots.size() - 1; it.kind = uilayers::K_FocusRing;
            it.layer = uilayers::K_FocusRing; it.pre = pre[i];
            c.items.push_back(it);
        }
    }
}

// Refresh the per-frame keys and canvases, then restore the draw order.
void PreparePassFrame(UIPassCache& c, const Scene& scene) {
    const auto& objs = scene.Objects();
    for (UISlot& sl : c.slots) {
        GameObject* go = objs[sl.index].get();
        sl.canvas = OwningCanvas(go);
        sl.opacity = UIOpacity(go);
        sl.overlay = 0;
        for (Transform* t = go->transform; t && sl.overlay == 0; t = t->Parent())
            if (t->gameObject)
                if (const UIModal* m = t->gameObject->GetComponent<UIModal>(); m && m->IsOpen())
                    sl.overlay = (int)(m->OpenOrder() & 0xFFFFF);
    }
    // Overlays: an open modal's subtree draws above all other UI, newest modal on
    // top, its backdrop first; an open context menu draws above everything.
    constexpr int kModalBase = 1 << 24;
    for (UIItem& it : c.items) {
        const UISlot& sl = c.slots[it.slot];
        it.canvasOrder = sl.canvas ? sl.canvas->sortOrder : 0;
        const int o = objs[sl.index]->uiDrawOrder;
        it.eff = o != 0 ? o : it.layer;
        if (it.kind == uilayers::K_ContextMenu) it.canvasOrder = std::numeric_limits<int>::max();
        else if (sl.overlay != 0) {
            it.canvasOrder = kModalBase + sl.overlay;
            if (it.kind == uilayers::K_ModalDim) it.eff = std::numeric_limits<int>::min();
        }
    }
    // Stable insertion sort: same order as SortUIDrawItems, no allocation, O(n)
    // when nothing moved since last frame.
    for (std::size_t i = 1; i < c.items.size(); ++i) {
        UIItem v = c.items[i];
        std::size_t j = i;
        while (j > 0 && ItemBefore(v, c.items[j - 1])) { c.items[j] = c.items[j - 1]; --j; }
        c.items[j] = v;
    }
}
} // namespace

// Draws a turned or scaled frame by mapping the layout-space calls the rest of
// this pass already makes. With no turn and a scale of 1, 1 every call is handed
// on unchanged, so an old scene's pixels stay the same.
class FrameXformPainter final : public Painter {
public:
    explicit FrameXformPainter(Painter& in) : m_in(in) {}

    void Begin(GameObject* go, float sw, float sh) {
        m_on = false;
        m_clip = false;
        if (!go || !UIFrameTransformed(go)) return;
        UIFrameAffine xf;
        if (!UIFrameLayoutToScreen(go, sw, sh, xf)) return;
        m_xf = xf;
        m_on = true;
        m_in.SetClipRect(nullptr);
    }

    void SetDrawColor(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) override {
        m_r = r; m_g = g; m_b = b; m_a = a;
        m_in.SetDrawColor(r, g, b, a);
    }
    // A float rect (the one-device-pixel runs of every anti-aliased shape) goes straight through when no UI Frame transform is active. Without this the
    // base class turned it into a whole-pixel rect and every soft edge became a block as wide as the canvas scale.
    void FillRectF(const FRect* r) override {
        if (!m_on) { m_in.FillRectF(r); return; }
        Painter::FillRectF(r);
    }
    bool SupportsBulkRects() override { return !m_on && m_in.SupportsBulkRects(); }
    void FillRectsF(const ColoredRect* q, int n) override { m_in.FillRectsF(q, n); }
    void FillRect(const Rect* r) override {
        if (!m_on || !r) { m_in.FillRect(r); return; }
        float x = (float)r->x, y = (float)r->y, w = (float)r->w, h = (float)r->h;
        if (!ClipRect(x, y, w, h)) return;
        Quad(nullptr, x, y, w, h, 0, 0, 1, 1, false);
    }
    void DrawRect(const Rect* r) override {
        if (!m_on || !r) { m_in.DrawRect(r); return; }
        const float x = (float)r->x, y = (float)r->y, w = (float)r->w, h = (float)r->h;
        DrawLine((int)x, (int)y, (int)(x + w), (int)y);
        DrawLine((int)(x + w), (int)y, (int)(x + w), (int)(y + h));
        DrawLine((int)(x + w), (int)(y + h), (int)x, (int)(y + h));
        DrawLine((int)x, (int)(y + h), (int)x, (int)y);
    }
    void DrawLine(int x0, int y0, int x1, int y1) override {
        if (!m_on) { m_in.DrawLine(x0, y0, x1, y1); return; }
        float ax = (float)x0, ay = (float)y0, bx = (float)x1, by = (float)y1;
        if (m_clip && !ClipSeg(ax, ay, bx, by)) return;
        const Vec2 p0 = m_xf.Apply({ax, ay}), p1 = m_xf.Apply({bx, by});
        m_in.DrawLine((int)std::lround(p0.x), (int)std::lround(p0.y), (int)std::lround(p1.x), (int)std::lround(p1.y));
    }
    void DrawPoint(int x, int y) override {
        if (!m_on) { m_in.DrawPoint(x, y); return; }
        if (m_clip && ((float)x < m_cx || (float)y < m_cy || (float)x >= m_cx + m_cw || (float)y >= m_cy + m_ch)) return;
        const Vec2 p = m_xf.Apply({(float)x, (float)y});
        m_in.DrawPoint((int)std::lround(p.x), (int)std::lround(p.y));
    }
    void SetClipRect(const Rect* r) override {
        if (!m_on) { m_in.SetClipRect(r); return; }
        if (!r) { m_clip = false; m_in.SetClipRect(nullptr); return; }
        m_clip = true;
        m_cx = (float)r->x; m_cy = (float)r->y; m_cw = (float)r->w; m_ch = (float)r->h;
        m_in.SetClipRect(nullptr);
    }
    void GetClipRect(Rect* r) override {
        if (!m_on) { m_in.GetClipRect(r); return; }
        if (r) *r = m_clip ? Rect{(int)m_cx, (int)m_cy, (int)m_cw, (int)m_ch} : Rect{0, 0, 0, 0};
    }
    bool IsClipEnabled() override { return m_on ? m_clip : m_in.IsClipEnabled(); }
    void SetScale(float sx, float sy) override { m_in.SetScale(sx, sy); }
    Tex LoadTexture(const std::string& path) override { return m_in.LoadTexture(path); }
    Tex LoadTextureSized(const std::string& path, int w, int h) override { return m_in.LoadTextureSized(path, w, h); }
    Tex FontAtlas(TtfFont* font) override { return m_in.FontAtlas(font); }
    void QueryTexture(Tex t, int* w, int* h) override { m_in.QueryTexture(t, w, h); }
    void SetTextureColorMod(Tex t, std::uint8_t r, std::uint8_t g, std::uint8_t b) override { m_in.SetTextureColorMod(t, r, g, b); }
    void SetTextureAlphaMod(Tex t, std::uint8_t a) override { m_in.SetTextureAlphaMod(t, a); }
    void Copy(Tex t, const Rect* s, const Rect* d) override {
        if (!m_on || !d) { m_in.Copy(t, s, d); return; }
        Blit(t, s, (float)d->x, (float)d->y, (float)d->w, (float)d->h, 0.0, d->w * 0.5f, d->h * 0.5f, 0);
    }
    void CopyF(Tex t, const Rect* s, const FRect* d) override {
        if (!m_on || !d) { m_in.CopyF(t, s, d); return; }
        Blit(t, s, d->x, d->y, d->w, d->h, 0.0, d->w * 0.5f, d->h * 0.5f, 0);
    }
    void CopyEx(Tex t, const Rect* s, const Rect* d, double ang, const Point* c, int flip) override {
        if (!m_on || !d) { m_in.CopyEx(t, s, d, ang, c, flip); return; }
        const float cx = c ? (float)c->x : d->w * 0.5f, cy = c ? (float)c->y : d->h * 0.5f;
        Blit(t, s, (float)d->x, (float)d->y, (float)d->w, (float)d->h, ang, cx, cy, flip);
    }
    void CopyExF(Tex t, const Rect* s, const FRect* d, double ang, const FPoint* c, int flip) override {
        if (!m_on || !d) { m_in.CopyExF(t, s, d, ang, c, flip); return; }
        const float cx = c ? c->x : d->w * 0.5f, cy = c ? c->y : d->h * 0.5f;
        Blit(t, s, d->x, d->y, d->w, d->h, ang, cx, cy, flip);
    }
    void Geometry(Tex t, const Vertex* v, int nv, const int* idx, int ni) override {
        if (!m_on || !v) { m_in.Geometry(t, v, nv, idx, ni); return; }
        m_verts.assign(v, v + nv);
        for (Vertex& q : m_verts) {
            const Vec2 p = m_xf.Apply({q.position.x, q.position.y});
            q.position = {p.x, p.y};
        }
        m_in.Geometry(t, m_verts.data(), nv, idx, ni);
    }
    void MissingTexture(const std::string& path, const Rect& where) override { m_in.MissingTexture(path, where); }
    void SetImageFilter(bool linear) override { m_in.SetImageFilter(linear); }
    void SetTextureFilter(Tex t, bool linear) override { m_in.SetTextureFilter(t, linear); }
    Tex UploadAtlas(const Image& page, int pageIndex, Tex existing) override { return m_in.UploadAtlas(page, pageIndex, existing); }

private:
    bool ClipRect(float& x, float& y, float& w, float& h) const {
        if (w == 0.0f || h == 0.0f) return false;
        if (!m_clip) return w > 0.0f && h > 0.0f;
        if (m_cw <= 0.0f || m_ch <= 0.0f) return false;
        const float x1 = std::max(x, m_cx), y1 = std::max(y, m_cy);
        const float x2 = std::min(x + w, m_cx + m_cw), y2 = std::min(y + h, m_cy + m_ch);
        x = x1; y = y1; w = x2 - x1; h = y2 - y1;
        return w > 0.0f && h > 0.0f;
    }
    bool ClipSeg(float& x0, float& y0, float& x1, float& y1) const {
        auto code = [&](float x, float y) {
            int bits = 0;
            if (x < m_cx) bits |= 1;
            if (x > m_cx + m_cw) bits |= 2;
            if (y < m_cy) bits |= 4;
            if (y > m_cy + m_ch) bits |= 8;
            return bits;
        };
        for (int i = 0; i < 8; ++i) {
            const int c0 = code(x0, y0), c1 = code(x1, y1);
            if ((c0 | c1) == 0) return true;
            if (c0 & c1) return false;
            const int out = c0 ? c0 : c1;
            float x = x0, y = y0;
            const float dx = x1 - x0, dy = y1 - y0;
            if (out & 8) { if (dy == 0.0f) return false; x = x0 + dx * ((m_cy + m_ch) - y0) / dy; y = m_cy + m_ch; }
            else if (out & 4) { if (dy == 0.0f) return false; x = x0 + dx * (m_cy - y0) / dy; y = m_cy; }
            else if (out & 2) { if (dx == 0.0f) return false; y = y0 + dy * ((m_cx + m_cw) - x0) / dx; x = m_cx + m_cw; }
            else { if (dx == 0.0f) return false; y = y0 + dy * (m_cx - x0) / dx; x = m_cx; }
            if (c0) { x0 = x; y0 = y; } else { x1 = x; y1 = y; }
        }
        return false;
    }
    void Quad(Tex tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1, bool white) {
        Vec2 p[4] = {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}};
        Vertex v[4];
        const RGBA col = white ? RGBA{255, 255, 255, 255} : RGBA{m_r, m_g, m_b, m_a};
        const float us[4] = {u0, u1, u1, u0}, vs[4] = {v0, v0, v1, v1};
        for (int i = 0; i < 4; ++i) {
            const Vec2 s = m_xf.Apply(p[i]);
            v[i] = Vertex{FPoint{s.x, s.y}, col, FPoint{us[i], vs[i]}};
        }
        const int idx[6] = {0, 1, 2, 0, 2, 3};
        m_in.Geometry(tex, v, 4, idx, 6);
    }
    void Blit(Tex t, const Rect* src, float x, float y, float w, float h, double ang, float cx, float cy, int flip) {
        if (!t || w == 0.0f || h == 0.0f) return;
        if (ang == 0.0 && !ClipRect(x, y, w, h)) return;
        int tw = 0, th = 0;
        m_in.QueryTexture(t, &tw, &th);
        if (tw <= 0 || th <= 0) return;
        float u0 = src ? (float)src->x / (float)tw : 0.0f;
        float v0 = src ? (float)src->y / (float)th : 0.0f;
        float u1 = src ? (float)(src->x + src->w) / (float)tw : 1.0f;
        float v1 = src ? (float)(src->y + src->h) / (float)th : 1.0f;
        if (flip & FlipH) std::swap(u0, u1);
        if (flip & FlipV) std::swap(v0, v1);
        Vec2 p[4] = {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}};
        if (ang != 0.0) {
            constexpr float kDeg = 0.017453292519943295f;
            const float rad = (float)ang * kDeg, co = std::cos(rad), si = std::sin(rad);
            const float ox = x + cx, oy = y + cy;
            for (Vec2& q : p) {
                const float dx = q.x - ox, dy = q.y - oy;
                q = {ox + co * dx - si * dy, oy + si * dx + co * dy};
            }
        }
        Vertex v[4];
        const float us[4] = {u0, u1, u1, u0}, vs[4] = {v0, v0, v1, v1};
        for (int i = 0; i < 4; ++i) {
            const Vec2 s = m_xf.Apply(p[i]);
            v[i] = Vertex{FPoint{s.x, s.y}, RGBA{255, 255, 255, 255}, FPoint{us[i], vs[i]}};
        }
        const int idx[6] = {0, 1, 2, 0, 2, 3};
        m_in.Geometry(t, v, 4, idx, 6);
    }

    Painter& m_in;
    UIFrameAffine m_xf{};
    bool m_on = false;
    bool m_clip = false;
    float m_cx = 0, m_cy = 0, m_cw = 0, m_ch = 0;
    std::uint8_t m_r = 255, m_g = 255, m_b = 255, m_a = 255;
    std::vector<Vertex> m_verts;
};

void ui_pass::RenderSceneUIImpl(Scene& scene, Painter& target, int w, int h, const UIRenderOptions& opt) {
    FrameXformPainter xp(target);
    Painter& p = xp;
    // Scroll / mask membership: offset a widget's origin by the scroll views it
    // sits in and clip drawing to every enclosing viewport and mask, so
    // scrollable and masked content shows and hides correctly in the built game.
    // Call once per UI widget right after resolving its origin; unclipped
    // widgets reset the clip to none.
    auto enterScroll = [&](GameObject* g, Vec2& o) {
        o = o - UIScrollTotal(g);
        Vec2 co, cs;
        if (UIClipRectLogical(g, (float)w, (float)h, co, cs)) {
            Rect clip{(int)co.x, (int)co.y, (int)cs.x, (int)cs.y};
            p.SetClipRect(&clip);
        } else {
            p.SetClipRect(nullptr);
        }
    };
    // The widgets' draw blocks (from the shared layer table), cached per scene and
    // put in draw order for this frame.
    using namespace okay::uilayers;
    UIPassCache& cache = PassCacheFor(scene);
    if (cache.structure != scene.StructureVersion() || cache.components != scene.ComponentVersion() ||
        cache.objectCount != scene.Objects().size())
        RebuildPassCache(cache, scene);
    PreparePassFrame(cache, scene);
    g_uiDefaultFont = okay::SceneUIFont(scene.uiFont);   // scene-wide default for this pass ("" = the launcher font)
    okay::SetActiveUIFont(g_uiDefaultFont);              // screen-space text measures in it too
    // A "Scale With Screen Size" canvas draws at its scale factor, like the editor's
    // Game view and like hit-testing (GetUIScreenRect): the renderer is scaled and
    // w/h become the canvas's logical size, so every widget kind (whether it lays out
    // with UIResolveOrigin or GetUIScreenRect) lands where clicks land.
    const int uiFullW = w, uiFullH = h;
    float uiScaleCur = 1.0f;
    auto uiUseScale = [&](float sc) {
        if (sc < 0.05f) sc = 1.0f;
        g_uiDeviceScale = sc;
        if (std::fabs(sc - uiScaleCur) < 1e-4f) return;
        uiScaleCur = sc;
        p.SetClipRect(nullptr);
        p.SetScale(sc, sc);
        w = (int)std::lround(uiFullW / sc); h = (int)std::lround(uiFullH / sc);
    };
    g_uiSmoothEdges = UISmoothFilteringFor(scene.uiFont);   // smooth edges + linear images, unless pixel-art
    g_uiQuality = UIQualityFor(scene.uiQuality);
    p.SetImageFilter(g_uiSmoothEdges);
    g_uiDeviceScale = 1.0f;
    auto uiMouse = [&]() { Vec2 m = Input::MousePosition(); return Vec2{m.x / uiScaleCur, m.y / uiScaleCur}; };
    for (const UIItem& _it : cache.items) {
        const UISlot& _slot = cache.slots[_it.slot];
        const auto& up = scene.Objects()[_slot.index];
        // Every UI object carries a focus-ring item; skip it outright unless focused.
        if (_it.kind == K_FocusRing && !UIShowsFocusRing(up.get())) continue;
        if (_it.kind == K_Caption) {   // subtitle and sound-caption lines
            auto* cap = up->GetComponent<CaptionOverlay>();
            if (!cap || !up->active || UIHidden(up.get())) continue;
            std::vector<CaptionOverlay::Mark> marks;
            cap->Layout((float)w, (float)h, marks);
            const float op = _slot.opacity;
            for (const CaptionOverlay::Mark& m : marks) {
                const float a = m.color.a * op;
                if (a < 0.01f) continue;
                if (m.box) {
                    if (m.w < 1.0f || m.h < 1.0f) continue;
                    p.SetDrawColor((std::uint8_t)(m.color.r * 255), (std::uint8_t)(m.color.g * 255),
                                   (std::uint8_t)(m.color.b * 255), (std::uint8_t)(a * 255));
                    Rect box{(int)m.x, (int)m.y, (int)m.w, (int)m.h};
                    if (box.w > 0 && box.h > 0) p.FillRect(&box);
                } else if (!m.text.empty()) {
                    RGBA col{(std::uint8_t)(m.color.r * 255), (std::uint8_t)(m.color.g * 255),
                             (std::uint8_t)(m.color.b * 255), (std::uint8_t)(a * 255)};
                    DrawUIText(p, m.text, m.x, m.y, m.pixel, col, 0.0f, 0.0f, m.italic);
                }
            }
            continue;
        }
        // A mask with its graphic hidden only clips: its own widget doesn't draw.
        if (const UIMask* mk = up->GetComponent<UIMask>(); mk && !mk->showGraphic && _it.kind != K_FocusRing) continue;
        {
            float want = 1.0f;
            Canvas* ucv = _slot.canvas;
            if (ucv && !ucv->worldSpace && !WorldUIRoot(up.get()) &&
                ucv->scaleMode == Canvas::ScaleMode::ScaleWithScreenSize)
                want = ucv->ScaleFactor((float)uiFullW, (float)uiFullH);
            uiUseScale(want > 0.05f ? want : 1.0f);
        }
        xp.Begin(up.get(), (float)w, (float)h);
        if (_it.kind == K_DropBg) {   // drop-target slot backgrounds (behind items)
        auto* dt = up->GetComponent<UIDropTarget>();
        if (!dt || !up->active || !dt->drawBackground || UIHidden(up.get())) continue;
        Vec2 o, sz; if (!GetUIScreenRect(up.get(), (float)w, (float)h, o, sz)) continue;
        Rect r{(int)o.x, (int)o.y, (int)sz.x, (int)sz.y};
        p.SetDrawColor((std::uint8_t)(dt->background.r*255), (std::uint8_t)(dt->background.g*255),
                               (std::uint8_t)(dt->background.b*255), (std::uint8_t)(dt->background.a*255));
        p.FillRect(&r);
        for (int b = 0; b < (int)dt->borderWidth; ++b) {   // outline
            Rect br{r.x+b, r.y+b, r.w-2*b, r.h-2*b};
            p.SetDrawColor((std::uint8_t)(dt->borderColor.r*255), (std::uint8_t)(dt->borderColor.g*255),
                                   (std::uint8_t)(dt->borderColor.b*255), (std::uint8_t)(dt->borderColor.a*255));
            p.DrawRect(&br);
        }
    }
        else if (_it.kind == K_Image) {   // images (logos/icons) first
        auto* im = up->GetComponent<UIImage>();
        if (auto* ri = up->GetComponent<UIRawImage>(); ri && !im) {   // Raw Image: texture through a UV rect
            if (!ri->enabled || !up->active || UIHidden(up.get())) continue;
            float op = _slot.opacity;
            Vec2 o = UIResolveOrigin(up.get(), (float)w, (float)h);
            Vec2 isz = UIResolveSize(up.get(), (float)w, (float)h);
            enterScroll(up.get(), o);
            Tex tex = p.LoadTexture(ri->texture);
            if (!tex) {
                Rect rr{(int)o.x, (int)o.y, (int)isz.x, (int)isz.y};
                FillUIShape(p, rr, UIShape::Rectangle, 0.0f, ri->color, ri->color, false, false, op);
                if (!ri->texture.empty()) p.MissingTexture(ri->texture, rr);
                continue;
            }
            int tw = 0, th = 0; p.QueryTexture(tex, &tw, &th);
            p.SetTextureColorMod(tex, (std::uint8_t)(ri->color.r * 255), (std::uint8_t)(ri->color.g * 255), (std::uint8_t)(ri->color.b * 255));
            p.SetTextureAlphaMod(tex, (std::uint8_t)(ri->color.a * 255 * op));
            for (const auto& pc : ri->Pieces(isz.x, isz.y)) {
                const int flip = (pc.u0 > pc.u1 ? FlipH : 0) | (pc.v0 > pc.v1 ? FlipV : 0);
                const float ua = std::min(pc.u0, pc.u1), ub = std::max(pc.u0, pc.u1);
                const float va = std::min(pc.v0, pc.v1), vb = std::max(pc.v0, pc.v1);
                Rect src{(int)(ua * tw), (int)(va * th), std::max(1, (int)std::lround((ub - ua) * tw)), std::max(1, (int)std::lround((vb - va) * th))};
                FRect dst{o.x + pc.x, o.y + pc.y, pc.w, pc.h};
                p.CopyExF(tex, &src, &dst, 0.0, nullptr, (int)flip);
            }
            continue;
        }
        if (!im || !up->active || UIHidden(up.get())) continue;
        float op = _slot.opacity;   // canvas master fade
        Vec2 o = UIResolveOrigin(up.get(), (float)w, (float)h);
        Vec2 isz = UIResolveSize(up.get(), (float)w, (float)h);   // honors stretch anchors
        enterScroll(up.get(), o);
        Rect r{(int)o.x, (int)o.y, (int)isz.x, (int)isz.y};
        // Radial/linear fill: shrink the drawn rect to fillAmount along an axis.
        float fox, foy, fw, fh;
        im->FilledRect(isz.x, isz.y, fox, foy, fw, fh);
        Rect fr{(int)(o.x + fox), (int)(o.y + foy), (int)fw, (int)fh};
        bool filled = im->fillMode != UIImage::FillMode::None;
        int flip = (int)((im->flipX ? FlipH : 0) |
                                                   (im->flipY ? FlipV   : 0));
        if (im->shadow) {   // drop shadow behind the image (parity with panels)
            Rect sh{r.x + (int)im->shadowOffset.x, r.y + (int)im->shadowOffset.y, r.w, r.h};
            FillUIShadow(p, sh, im->shape, im->cornerRadius,
                         im->shadowColor, im->shadowSoftness, op, im->cornerMask);
        }
        if (const std::string vname = uilib::IconNameOf(im->texture); !vname.empty()) {   // a vector icon: drawn square, centred, tinted
            const int side = std::min(r.w, r.h);
            auto b8 = [](float v) { int i = (int)(v * 255.0f + 0.5f); return (std::uint8_t)(i < 0 ? 0 : i > 255 ? 255 : i); };
            DrawVectorIcon(p, vname, r.x + (r.w - side) / 2, r.y + (r.h - side) / 2, side, RGBA{b8(im->color.r), b8(im->color.g), b8(im->color.b), b8(im->color.a * op)}, g_uiDeviceScale);
            continue;
        }
        Tex tex = p.LoadTexture(im->texture);
        if (tex) {
            p.SetTextureColorMod(tex, (std::uint8_t)(im->color.r * 255), (std::uint8_t)(im->color.g * 255),
                                   (std::uint8_t)(im->color.b * 255));
            p.SetTextureAlphaMod(tex, (std::uint8_t)(im->color.a * 255 * op));
            if (im->nineSlice && im->border > 0.0f) {
                int tw = 0, th = 0; p.QueryTexture(tex, &tw, &th);
                int sb = (int)im->border;                       // source border
                int dbx = (int)im->border, dby = (int)im->border;  // dest border (clamped)
                if (dbx * 2 > r.w) dbx = r.w / 2;
                if (dby * 2 > r.h) dby = r.h / 2;
                // Column x's (src and dst) and row y's: left | middle | right.
                int sx[4] = {0, sb, tw - sb, tw};
                int sy[4] = {0, sb, th - sb, th};
                int dx[4] = {r.x, r.x + dbx, r.x + r.w - dbx, r.x + r.w};
                int dy[4] = {r.y, r.y + dby, r.y + r.h - dby, r.y + r.h};
                for (int cy = 0; cy < 3; ++cy)
                    for (int cx = 0; cx < 3; ++cx) {
                        Rect s{sx[cx], sy[cy], sx[cx + 1] - sx[cx], sy[cy + 1] - sy[cy]};
                        Rect d{dx[cx], dy[cy], dx[cx + 1] - dx[cx], dy[cy + 1] - dy[cy]};
                        if (s.w > 0 && s.h > 0 && d.w > 0 && d.h > 0)
                            p.CopyEx(tex, &s, &d, 0.0, nullptr, flip);
                    }
            } else if (filled) {                        // reveal a proportional slice
                int tw = 0, th = 0; p.QueryTexture(tex, &tw, &th);
                Rect src{
                    (int)(isz.x > 0 ? fox / isz.x * tw : 0),
                    (int)(isz.y > 0 ? foy / isz.y * th : 0),
                    (int)(isz.x > 0 ? fw  / isz.x * tw : tw),
                    (int)(isz.y > 0 ? fh  / isz.y * th : th)};
                p.CopyEx(tex, &src, &fr, 0.0, nullptr, flip);
            } else {
                Rect dst = r;
                if (im->preserveAspect) {               // letterbox to keep source aspect
                    int tw = 0, th = 0; p.QueryTexture(tex, &tw, &th);
                    float aox, aoy, afw, afh;
                    im->AspectRect((float)r.w, (float)r.h, (float)tw, (float)th, aox, aoy, afw, afh);
                    dst = Rect{r.x + (int)aox, r.y + (int)aoy, (int)afw, (int)afh};
                }
                // Shrunk by 25% or more: draw a copy filtered down to the drawn size (area average in linear light) instead of
                // letting the linear sampler skip texels. Pixel-art pictures and the Old look sample as they always did.
                Tex dtex = tex;
                if (g_uiSmoothEdges && g_uiQuality != UIQuality::Old) {
                    int tw = 0, th = 0; p.QueryTexture(tex, &tw, &th);
                    const float Sd = g_uiDeviceScale > 0.05f ? g_uiDeviceScale : 1.0f;
                    int mw = 0, mh = 0;
                    if (UIImageMipSize(tw, th, (int)std::lround(dst.w * Sd), (int)std::lround(dst.h * Sd), mw, mh)) {
                        if (Tex small = p.LoadTextureSized(im->texture, mw, mh)) {
                            if (small != tex) {
                                p.SetTextureColorMod(small, (std::uint8_t)(im->color.r * 255), (std::uint8_t)(im->color.g * 255), (std::uint8_t)(im->color.b * 255));
                                p.SetTextureAlphaMod(small, (std::uint8_t)(im->color.a * 255 * op));
                                dtex = small;
                            }
                        }
                    }
                }
                // Rounded / circle / pill pictures (avatars, cards): the texture is
                // drawn on the shape's outline as a triangle fan, so the corners are cut.
                const bool rounded = (im->shape == UIShape::Rounded && im->cornerRadius > 0.5f) ||
                                     im->shape == UIShape::Circle || im->shape == UIShape::Pill;
                if (rounded && dst.w > 2 && dst.h > 2) {
                    std::vector<FPoint> pts;
                    std::vector<FPoint> nrm;   // outward direction at each point (for the soft edge)
                    const float S = g_uiDeviceScale > 0.05f ? g_uiDeviceScale : 1.0f;
                    // Enough segments that a curve stays within a tenth of a device pixel of the true
                    // shape at any zoom (the old fixed 48 / 9 per corner showed facets when magnified).
                    auto arcSteps = [&](float radLogical) {
                        const float rd = std::max(1.0f, radLogical * S);
                        const float step = 2.0f * std::acos(std::max(0.0f, 1.0f - 0.1f / rd));
                        return std::clamp((int)std::ceil(1.5707963f / std::max(step, 0.01f)), 4, 64);
                    };
                    const float x0 = (float)dst.x, y0 = (float)dst.y, ww = (float)dst.w, hh = (float)dst.h;
                    if (im->shape == UIShape::Circle) {
                        const int n = g_uiSmoothEdges ? 4 * arcSteps(std::max(ww, hh) * 0.5f) : 48;
                        for (int k = 0; k < n; ++k) {
                            const float a = k / (float)n * 6.2831853f;
                            pts.push_back({x0 + ww * 0.5f + std::cos(a) * ww * 0.5f, y0 + hh * 0.5f + std::sin(a) * hh * 0.5f});
                            nrm.push_back({std::cos(a), std::sin(a)});
                        }
                    } else {
                        float rad = im->shape == UIShape::Pill ? std::min(ww, hh) * 0.5f : std::min(im->cornerRadius, std::min(ww, hh) * 0.5f);
                        const int steps = g_uiSmoothEdges ? arcSteps(rad) : 8;
                        const float cx[4] = {x0 + ww - rad, x0 + ww - rad, x0 + rad, x0 + rad};
                        const float cy[4] = {y0 + rad, y0 + hh - rad, y0 + hh - rad, y0 + rad};
                        const int bits[4] = {UICornerTR, UICornerBR, UICornerBL, UICornerTL};
                        for (int c = 0; c < 4; ++c) {
                            const float r0 = (im->cornerMask & bits[c]) ? rad : 0.0f;
                            const float ccx = r0 > 0 ? cx[c] : (c == 0 || c == 1 ? x0 + ww : x0);
                            const float ccy = r0 > 0 ? cy[c] : (c == 1 || c == 2 ? y0 + hh : y0);
                            for (int k = 0; k <= steps; ++k) {
                                const float a = (-90.0f + c * 90.0f + k * (90.0f / (float)steps)) * 0.01745329f;
                                pts.push_back({ccx + std::cos(a) * r0, ccy + std::sin(a) * r0});
                                nrm.push_back({std::cos(a), std::sin(a)});
                            }
                        }
                    }
                    const RGBA vc{255, 255, 255, 255};
                    std::vector<Vertex> vx;
                    // The software renderer reads the texel at floor(u * width), so a coordinate of exactly 1.0 reads one texel past the last row or
                    // column (heap garbage: a few grey pixels at the bottom/right tip of a round picture that changed from render to render). Keep
                    // coordinates a quarter texel inside the picture.
                    int texW = 0, texH = 0;
                    p.QueryTexture(dtex, &texW, &texH);
                    const float uMax = texW > 0 ? 1.0f - 0.25f / (float)texW : 1.0f, vMax = texH > 0 ? 1.0f - 0.25f / (float)texH : 1.0f;
                    auto uvOf = [&](float qx, float qy) {
                        float u = std::clamp((qx - x0) / ww, 0.0f, 1.0f), v = std::clamp((qy - y0) / hh, 0.0f, 1.0f);   // the soft edge sits just outside the picture: clamp, never wrap
                        if (im->flipX) u = 1.0f - u;
                        if (im->flipY) v = 1.0f - v;
                        return FPoint{std::min(u, uMax), std::min(v, vMax)};
                    };
                    vx.push_back({{x0 + ww * 0.5f, y0 + hh * 0.5f}, vc, {0.5f, 0.5f}});
                    for (const auto& q : pts) vx.push_back({q, vc, uvOf(q.x, q.y)});
                    std::vector<int> ix;
                    const int n = (int)pts.size();
                    auto same = [&](int a, int b) { return std::fabs(pts[a].x - pts[b].x) < 1e-3f && std::fabs(pts[a].y - pts[b].y) < 1e-3f; };   // a corner with no radius repeats a point: no zero-area triangles
                    for (int k = 0; k < n; ++k) { if (same(k, (k + 1) % n)) continue; ix.push_back(0); ix.push_back(1 + k); ix.push_back(1 + (k + 1) % n); }
                    // The picture is drawn first, then its soft edge as a second call: the software renderer draws nothing when a fan with
                    // see-through ring vertices is sent in one call.
                    p.Geometry(dtex, vx.data(), (int)vx.size(), ix.data(), (int)ix.size());
                    ix.clear();
                    if (g_uiSmoothEdges) {   // a one-device-pixel soft edge: the picture fades out instead of stair-stepping
                        const RGBA vz{255, 255, 255, 0};
                        for (int k = 0; k < n; ++k) {
                            const float qx = pts[k].x + nrm[k].x / S, qy = pts[k].y + nrm[k].y / S;
                            vx.push_back({{qx, qy}, vz, uvOf(qx, qy)});
                        }
                        for (int k = 0; k < n; ++k) {
                            const int k1 = (k + 1) % n;
                            if (same(k, k1)) continue;
                            // A straight edge (the same outward direction at both ends) lies on the pixel grid and needs no soft edge; only the curves get one.
                            if (std::fabs(nrm[k].x - nrm[k1].x) < 1e-4f && std::fabs(nrm[k].y - nrm[k1].y) < 1e-4f) continue;
                            ix.push_back(1 + k); ix.push_back(1 + n + k); ix.push_back(1 + k1);
                            ix.push_back(1 + k1); ix.push_back(1 + n + k); ix.push_back(1 + n + k1);
                        }
                    }
                    if (!ix.empty()) p.Geometry(dtex, vx.data(), (int)vx.size(), ix.data(), (int)ix.size());
                } else {
                    p.CopyEx(dtex, nullptr, &dst, 0.0, nullptr, flip);
                }
            }
        } else {                                        // no image -> colored shape fill
            const Rect& rr = filled ? fr : r;
            FillUIShape(p, rr, im->shape, im->cornerRadius,
                        im->color, im->color, false, false, op, 0, im->cornerMask);
            if (!im->texture.empty()) p.MissingTexture(im->texture, r);
        }
        if (im->borderWidth > 0.0f) {                   // optional frame/matte around the image
            int b = (int)im->borderWidth;
            p.SetDrawColor((std::uint8_t)(im->borderColor.r * 255),
                                   (std::uint8_t)(im->borderColor.g * 255), (std::uint8_t)(im->borderColor.b * 255),
                                   (std::uint8_t)(im->borderColor.a * 255 * op));
            for (int e = 0; e < b; ++e) {               // nested rects = a b-px stroke
                Rect frm{r.x + e, r.y + e, r.w - 2 * e, r.h - 2 * e};
                if (frm.w > 0 && frm.h > 0) p.DrawRect(&frm);
            }
        }
    }
        else if (_it.kind == K_Panel) {   // panels (backgrounds) first
        auto* pn = up->GetComponent<UIPanel>();
        if (!pn || !up->active || UIHidden(up.get())) continue;
        float op = _slot.opacity;   // canvas master fade
        // In the world the origin is projected and the size, radius, border, shadow and outline are scaled to match
        // (screen pixels per design pixel; 1 on screen, so nothing changes there).
        const float wk = WorldWidgetScale(up.get(), _slot.canvas, w, h);
        if (wk <= 0.0f) continue;   // behind the camera
        Vec2 o = UIResolveOrigin(up.get(), (float)w, (float)h);
        Vec2 psz = UIResolveSize(up.get(), (float)w, (float)h) * wk;   // honors stretch anchors
        enterScroll(up.get(), o);
        Rect r{(int)o.x, (int)o.y, (int)psz.x, (int)psz.y};
        // A backdrop far larger than the screen (e.g. a pause-menu dim) covers the
        // whole window — clamp it so it tiles exactly the screen, not huge coords.
        if (psz.x > w * 1.5f && psz.y > h * 1.5f) r = Rect{0, 0, w, h};
        bool gh; int gdiag; GradientParams(pn->gradientDir, gh, gdiag);
        int cm = pn->cornerMask;                        // per-corner rounding
        const float radius = pn->cornerRadius * wk;
        if (pn->shadow) {                               // drop shadow behind (same shape)
            Rect sh{r.x + (int)(pn->shadowOffset.x * wk), r.y + (int)(pn->shadowOffset.y * wk), r.w, r.h};
            FillUIShadow(p, sh, pn->shape, radius,
                         pn->shadowColor, pn->shadowSoftness * wk, op, cm);
        }
        if (pn->outlineWidth > 0.0f) {                  // outer keyline/glow ring
            int ow = (int)(pn->outlineWidth * wk);
            Rect outer{r.x - ow, r.y - ow, r.w + 2 * ow, r.h + 2 * ow};
            FillUIShape(p, outer, pn->shape, radius + ow,
                        pn->outlineColor, pn->outlineColor, false, false, op, 0, cm);
        }
        if (pn->borderWidth > 0.0f) {                   // fill, then the border ring over it
            FillUIShape(p, r, pn->shape, radius,
                        pn->color, pn->colorBottom, pn->useGradient, gh, op, gdiag, cm);
            if (pn->topHighlight) {                     // inner glass sheen (top-down fade)
                Color hb = pn->highlightColor; hb.a = 0.0f;
                FillUIShape(p, r, pn->shape, radius,
                            pn->highlightColor, hb, true, false, op, 0, cm);
            }
            FillUIRing(p, r, pn->shape, radius, pn->borderColor,
                       std::max(1, (int)(pn->borderWidth * wk + 0.5f)), op, cm);
        } else {
            FillUIShape(p, r, pn->shape, radius,
                        pn->color, pn->colorBottom, pn->useGradient, gh, op, gdiag, cm);
            if (pn->topHighlight) {                     // inner glass sheen (top-down fade)
                Color hb = pn->highlightColor; hb.a = 0.0f;
                FillUIShape(p, r, pn->shape, radius,
                            pn->highlightColor, hb, true, false, op, 0, cm);
            }
        }
    }
        else if (_it.kind == K_DropHi) {   // drop-target highlight (drag feedback)
        auto* dt = up->GetComponent<UIDropTarget>();
        if (!dt || !up->active || !dt->showHighlight || !dt->IsHovered()) continue;
        Vec2 o, sz;
        if (!GetUIScreenRect(up.get(), (float)w, (float)h, o, sz)) continue;
        Rect hr{(int)o.x, (int)o.y, (int)sz.x, (int)sz.y};
        const Color& hc = dt->HasValid() ? dt->highlight : dt->rejectHighlight;  // green-ish vs reject
        p.SetDrawColor((std::uint8_t)(hc.r * 255), (std::uint8_t)(hc.g * 255),
                               (std::uint8_t)(hc.b * 255), (std::uint8_t)(hc.a * 255));
        p.FillRect(&hr);
    }
        else if (_it.kind == K_Scroll) {   // scroll-view / list-view backgrounds + scrollbars
        auto* sv = up->GetComponent<UIScrollView>();
        auto* lv = sv ? nullptr : up->GetComponent<UIListView>();
        if ((!sv && !lv) || !up->active || UIHidden(up.get())) continue;
        float op = _slot.opacity;
        Vec2 o = UIResolveOrigin(up.get(), (float)w, (float)h);
        enterScroll(up.get(), o);
        const Vec2 vs = sv ? sv->size : lv->size;
        const Color& bg = sv ? sv->background : lv->background;
        const Color& bar = sv ? sv->barColor : lv->barColor;
        Rect box{(int)o.x, (int)o.y, (int)vs.x, (int)vs.y};
        p.SetDrawColor((std::uint8_t)(bg.r * 255), (std::uint8_t)(bg.g * 255),
                               (std::uint8_t)(bg.b * 255), (std::uint8_t)(bg.a * 255 * op));
        p.FillRect(&box);
        // A scrollbar: track + thumb along `len` px, the thumb `frac` of it, `t` along.
        auto drawBar = [&](bool vertical, float frac, float t) {
            const int barW = 6;
            Rect track = vertical ? Rect{box.x + box.w - barW - 2, box.y + 2, barW, box.h - 4}
                                  : Rect{box.x + 2, box.y + box.h - barW - 2, box.w - 4, barW};
            p.SetDrawColor(255, 255, 255, 30);
            p.FillRect(&track);
            const int len = vertical ? track.h : track.w;
            const int thumbL = (int)(len * (frac < 1.0f ? frac : 1.0f));
            const int at = (int)((len - thumbL) * t);
            Rect thumb = vertical ? Rect{track.x, track.y + at, barW, thumbL} : Rect{track.x + at, track.y, thumbL, barW};
            p.SetDrawColor((std::uint8_t)(bar.r * 255), (std::uint8_t)(bar.g * 255),
                                   (std::uint8_t)(bar.b * 255), (std::uint8_t)(bar.a * 255 * op));
            p.FillRect(&thumb);
        };
        if (sv && sv->barStyle != 0) {
            // grok/ui-controls: thin, or hidden until the view is scrolling.
            if (sv->BarVisible()) {
                auto drawThin = [&](bool vertical, float frac, float t) {
                    const int barW = sv->barStyle == 1 ? 3 : 4;
                    Rect track = vertical ? Rect{box.x + box.w - barW - 2, box.y + 2, barW, box.h - 4}
                                          : Rect{box.x + 2, box.y + box.h - barW - 2, box.w - 4, barW};
                    p.SetDrawColor(255, 255, 255, 22);
                    p.FillRect(&track);
                    const int len = vertical ? track.h : track.w;
                    const int thumbL = (int)(len * (frac < 1.0f ? frac : 1.0f));
                    const int at = (int)((len - thumbL) * Mathf::Clamp01(t));
                    Rect thumb = vertical ? Rect{track.x, track.y + at, barW, thumbL} : Rect{track.x + at, track.y, thumbL, barW};
                    p.SetDrawColor((std::uint8_t)(bar.r * 255), (std::uint8_t)(bar.g * 255),
                                           (std::uint8_t)(bar.b * 255), (std::uint8_t)(bar.a * 255 * op));
                    p.FillRect(&thumb);
                };
                if (sv->ScrollMax() > 0.0f)
                    drawThin(true, sv->size.y / (sv->contentHeight > 1.0f ? sv->contentHeight : 1.0f), sv->Fraction());
                if (sv->ScrollMaxX() > 0.0f)
                    drawThin(false, sv->size.x / (sv->contentWidth > 1.0f ? sv->contentWidth : 1.0f), sv->FractionX());
            }
        } else if (sv) {
            if (sv->ScrollMax() > 0.0f)
                drawBar(true, sv->size.y / (sv->contentHeight > 1.0f ? sv->contentHeight : 1.0f), sv->Fraction());
            if (sv->ScrollMaxX() > 0.0f)
                drawBar(false, sv->size.x / (sv->contentWidth > 1.0f ? sv->contentWidth : 1.0f), sv->FractionX());
        } else if (lv->ScrollMax() > 0.0f) {
            const float ch = lv->ContentHeight();
            drawBar(true, lv->size.y / (ch > 1.0f ? ch : 1.0f), lv->scroll / lv->ScrollMax());
        }
    }
        else if (_it.kind == K_Progress) {   // progress bars
        auto* pb = up->GetComponent<UIProgressBar>();
        if (!pb || !up->active || UIHidden(up.get())) continue;
        float op = _slot.opacity;
        // In the world (a WorldSpaceUI object or a world-space Canvas) the origin is already projected, so the size and the
        // style metrics are scaled here: screen pixels per design pixel. 1 on screen, so nothing changes there.
        const float wk = WorldWidgetScale(up.get(), _slot.canvas, w, h);
        if (wk <= 0.0f) continue;   // behind the camera
        Vec2 o = UIResolveOrigin(up.get(), (float)w, (float)h);
        enterScroll(up.get(), o);
        const Vec2 pbs{pb->size.x * wk, pb->size.y * wk};
        const float pbRadius = pb->cornerRadius * wk;
        Rect bg{(int)o.x, (int)o.y, (int)pbs.x, (int)pbs.y};
        FillUIShape(p, bg, pb->shape, pbRadius,
                    pb->background, pb->background, false, false, op);
        bool horiz = pb->fillDir == UIProgressBar::FillDir::LeftRight ||
                     pb->fillDir == UIProgressBar::FillDir::RightLeft;
        // grok/ui-options: segments, a delayed damage trail, and a color that follows the value.
        // All three off is the historical single fill (pixels unchanged).
        auto paintFill = [&](float frac, const Color& c0, const Color& c1, bool grad) {
            if (pb->segments < 2) {
                float pfox, pfoy, pfw, pfh;
                pb->FillRectAt(frac, pbs.x, pbs.y, pfox, pfoy, pfw, pfh);
                Rect fl{(int)(o.x + pfox), (int)(o.y + pfoy), (int)pfw, (int)pfh};
                FillUIShape(p, fl, pb->shape, pbRadius, c0, c1, grad, horiz, op);
                return;
            }
            int n = pb->segments; if (n > 40) n = 40;
            float gap = (pb->segmentGap < 0.0f ? 0.0f : pb->segmentGap) * wk;
            float span = horiz ? pbs.x : pbs.y;
            float cell = (span - gap * (float)(n - 1)) / (float)n;
            if (cell < 1.0f) cell = 1.0f;
            const bool fromEnd = pb->fillDir == UIProgressBar::FillDir::RightLeft ||
                                 pb->fillDir == UIProgressBar::FillDir::TopBottom;
            for (int i = 0; i < n; ++i) {
                float a = (float)i / (float)n, b = (float)(i + 1) / (float)n;
                float sf = frac <= a ? 0.0f : (frac >= b ? 1.0f : (frac - a) / (b - a));
                if (sf <= 0.0f) continue;
                int slot = fromEnd ? (n - 1 - i) : i;
                float along = slot * (cell + gap);
                Rect fl;
                if (horiz) {
                    float fw = cell * sf;
                    float x = pb->fillDir == UIProgressBar::FillDir::RightLeft ? (along + cell - fw) : along;
                    fl = {(int)(o.x + x), (int)o.y, (int)fw, (int)pbs.y};
                } else if (pb->fillDir == UIProgressBar::FillDir::BottomTop) {
                    float fh = cell * sf;
                    float base = pbs.y - along - cell;
                    fl = {(int)o.x, (int)(o.y + base + cell - fh), (int)pbs.x, (int)fh};
                } else {
                    float fh = cell * sf;
                    fl = {(int)o.x, (int)(o.y + along), (int)pbs.x, (int)fh};
                }
                if (fl.w > 0 && fl.h > 0)
                    FillUIShape(p, fl, pb->shape, pbRadius, c0, c1, grad, horiz, op);
            }
        };
        if (pb->segments < 2 && pb->trailDelay <= 0.0f && !pb->colorByValue) {
            float pfox, pfoy, pfw, pfh; pb->FillRect(pbs.x, pbs.y, pfox, pfoy, pfw, pfh);
            Rect fl{(int)(o.x + pfox), (int)(o.y + pfoy), (int)pfw, (int)pfh};
            FillUIShape(p, fl, pb->shape, pbRadius,
                        pb->fill, pb->fillEnd, pb->gradientFill, horiz, op);
        } else {
            if (pb->trailDelay > 0.0f && pb->TrailFraction() > pb->Fraction() + 0.001f)
                paintFill(pb->TrailFraction(), pb->trailColor, pb->trailColor, false);
            Color c0 = pb->colorByValue ? pb->ValueColor() : pb->fill;
            Color c1 = (pb->gradientFill && !pb->colorByValue) ? pb->fillEnd : c0;
            paintFill(pb->Fraction(), c0, c1, pb->gradientFill && !pb->colorByValue);
        }
        if (pb->showPercent || !pb->textFormat.empty()) {
            char pct[48];
            if (pb->textFormat.empty())
                std::snprintf(pct, sizeof(pct), "%d%%", (int)(pb->Fraction() * 100.0f + 0.5f));
            else {
                std::string shown = pb->FormatText();
                std::snprintf(pct, sizeof(pct), "%s", shown.c_str());
            }
            float px = 2.0f * wk;
            float tw = UITextW(pct, px);
            RGBA tc{(std::uint8_t)(pb->textColor.r * 255), (std::uint8_t)(pb->textColor.g * 255),
                         (std::uint8_t)(pb->textColor.b * 255), (std::uint8_t)(pb->textColor.a * 255 * op)};
            DrawUIText(p, pct, o.x + (pbs.x - tw) * 0.5f,
                     o.y + (pbs.y - Font8x8::Height * px) * 0.5f, px, tc);
        }
    }
        else if (_it.kind == K_Radial) {   // radial / ring progress
        auto* rp = up->GetComponent<UIRadialProgress>();
        if (!rp || !up->active || UIHidden(up.get())) continue;
        float op = _slot.opacity;
        Vec2 o = UIResolveOrigin(up.get(), (float)w, (float)h);
        enterScroll(up.get(), o);
        int bw = (int)rp->size.x, bh = (int)rp->size.y;
        for (int y = 0; y < bh; ++y)
            for (int x = 0; x < bw; ++x) {
                int reg = UIRadialProgress::Sample((float)bw, (float)bh, rp->thickness,
                                                   rp->EffectiveStart(), rp->clockwise, rp->value,
                                                   x + 0.5f, y + 0.5f);
                if (reg == UIRadialProgress::Outside) continue;
                const Color& c = (reg == UIRadialProgress::Fill) ? rp->fill : rp->background;
                p.SetDrawColor((std::uint8_t)(c.r * 255), (std::uint8_t)(c.g * 255),
                                       (std::uint8_t)(c.b * 255), (std::uint8_t)(c.a * 255 * op));
                Rect px1{(int)o.x + x, (int)o.y + y, 1, 1};
                p.FillRect(&px1);
            }
        if (rp->showPercent) {
            char pct[8]; std::snprintf(pct, sizeof(pct), "%d%%", (int)(rp->Fraction() * 100.0f + 0.5f));
            float ps = 2.0f;
            float tw = UITextW(pct, ps);
            RGBA tc{(std::uint8_t)(rp->textColor.r * 255), (std::uint8_t)(rp->textColor.g * 255),
                         (std::uint8_t)(rp->textColor.b * 255), (std::uint8_t)(rp->textColor.a * 255 * op)};
            DrawUIText(p, pct, o.x + (rp->size.x - tw) * 0.5f,
                     o.y + (rp->size.y - Font8x8::Height * ps) * 0.5f, ps, tc);
        }
    }
        else if (_it.kind == K_Minimap) {   // top-down minimap panel
        auto* mm = up->GetComponent<Minimap>();
        if (!mm || !up->active || UIHidden(up.get())) continue;
        float op = _slot.opacity;
        Vec2 o, sz; if (!GetUIScreenRect(up.get(), (float)w, (float)h, o, sz)) continue;
        // GTA-style fullscreen pause map: re-centre + enlarge, and zoom out.
        float wpp = mm->worldPerPixel;
        if (mm->fullscreen) {
            float frac = mm->fullscreenFrac > 0.1f ? mm->fullscreenFrac : 0.82f;
            float fw = w * frac, fh = h * frac;
            o = Vec2{(w - fw) * 0.5f, (h - fh) * 0.5f}; sz = Vec2{fw, fh};
            wpp = mm->worldPerPixel * (mm->fullscreenZoom > 0.01f ? mm->fullscreenZoom : 1.0f);
        }
        Rect box{(int)o.x, (int)o.y, (int)sz.x, (int)sz.y};
        int cx = (int)(o.x + sz.x * 0.5f), cy = (int)(o.y + sz.y * 0.5f);
        int radius = (int)((sz.x < sz.y ? sz.x : sz.y) * 0.5f);
        RGBA bgCol{(std::uint8_t)(mm->background.r*255), (std::uint8_t)(mm->background.g*255),
                        (std::uint8_t)(mm->background.b*255), (std::uint8_t)(mm->background.a*255*op)};
        RGBA bdCol{(std::uint8_t)(mm->border.r*255), (std::uint8_t)(mm->border.g*255),
                        (std::uint8_t)(mm->border.b*255), (std::uint8_t)(mm->border.a*255*op)};
        int bw = (int)mm->borderWidth; if (bw < 1) bw = 1;
        // Clip everything (grid + blips) to the map rect so nothing bleeds out.
        Rect prevClip; p.GetClipRect(&prevClip);
        bool hadClip = p.IsClipEnabled();
        if (mm->circular) {
            MMFillCircle(p, cx, cy, radius, bgCol);
        } else {
            p.SetDrawColor(bgCol.r, bgCol.g, bgCol.b, bgCol.a);
            p.FillRect(&box);
        }
        p.SetClipRect(&box);
        // Center world position + heading of the target.
        Vec3 center{0,0,0};
        float targetHeading = 0.0f;
        if (!mm->target.empty()) {
            if (GameObject* tg = scene.Find(mm->target); tg && tg->transform) {
                center = tg->transform->Position();
                targetHeading = Minimap::HeadingOf(tg->transform->Forward(), mm->useXZ);
            }
        }
        // A custom map renders north-up (GTA pause-map style); a plain radar can
        // spin with the target when rotateWithTarget is on.
        float mapHeading = (!mm->HasMap() && mm->rotateWithTarget) ? targetHeading : 0.0f;
        // GTA-style world-map image: pan/zoom a sub-rectangle under the blips.
        if (mm->HasMap()) {
            if (Tex mtex = p.LoadTexture(mm->mapTexture)) {
                int tw = 0, th = 0; p.QueryTexture(mtex, &tw, &th);
                float u0, v0, u1, v1;
                Minimap::MapSrcUV(*mm, center, sz.x, sz.y, wpp, u0, v0, u1, v1);
                Rect src{(int)(u0*tw), (int)(v0*th), (int)((u1-u0)*tw), (int)((v1-v0)*th)};
                p.SetTextureAlphaMod(mtex, (std::uint8_t)(255*op));
                p.Copy(mtex, &src, &box);
            }
        }
        // Reference grid (rectangular maps only).
        if (mm->showGrid && !mm->circular && mm->gridSpacing > 1.0f) {
            p.SetDrawColor((std::uint8_t)(mm->gridColor.r*255), (std::uint8_t)(mm->gridColor.g*255),
                                   (std::uint8_t)(mm->gridColor.b*255), (std::uint8_t)(mm->gridColor.a*255*op));
            for (float gx = sz.x*0.5f; gx < sz.x; gx += mm->gridSpacing) {
                p.DrawLine((int)(o.x+gx), box.y, (int)(o.x+gx), box.y+box.h);
                p.DrawLine((int)(o.x+sz.x-gx), box.y, (int)(o.x+sz.x-gx), box.y+box.h);
            }
            for (float gy = sz.y*0.5f; gy < sz.y; gy += mm->gridSpacing) {
                p.DrawLine(box.x, (int)(o.y+gy), box.x+box.w, (int)(o.y+gy));
                p.DrawLine(box.x, (int)(o.y+sz.y-gy), box.x+box.w, (int)(o.y+sz.y-gy));
            }
        }
        float selfAng = targetHeading - mapHeading;   // facing of the player on-map
        // Click the open (fullscreen) map to drop a waypoint; right-click clears it.
        if (opt.interactive && mm->fullscreen && mm->mapClickWaypoint) {
            okay::Vec2 cur = uiMouse();   // logical (canvas-scaled) coordinates
            bool inBox = cur.x >= o.x && cur.x < o.x + sz.x && cur.y >= o.y && cur.y < o.y + sz.y;
            if (inBox && Input::GetMouseButtonDown(0)) {
                mm->userWaypoint = Minimap::MapToWorldPlane(*mm, center, sz.x, sz.y, mapHeading,
                                                           cur.x - o.x, cur.y - o.y, wpp);
                mm->hasUserWaypoint = true;
            } else if (inBox && Input::GetMouseButtonDown(1)) {
                mm->hasUserWaypoint = false;
            }
        }
        // Authorable vector map: roads (lines), houses (rects), zones (polys).
        auto projPlane = [&](const Vec2& en, float& ox, float& oy) {
            float pmx, pmy;
            Minimap::WorldToMapR(*mm, center, mm->PlaneToWorld(en), sz.x, sz.y, mapHeading, pmx, pmy, wpp);
            ox = o.x + pmx; oy = o.y + pmy;
        };
        for (const auto& shp : mm->mapShapes) {
            if (shp.points.empty()) continue;
            RGBA col{(std::uint8_t)(shp.color.r*255), (std::uint8_t)(shp.color.g*255),
                          (std::uint8_t)(shp.color.b*255), (std::uint8_t)(shp.color.a*255*op)};
            std::vector<Point> pts;
            if (shp.kind == Minimap::MapShape::Kind::Rect && shp.points.size() >= 2) {
                Vec2 p0 = shp.points[0], p1 = shp.points[1];
                Vec2 corners[4] = {{p0.x,p0.y},{p1.x,p0.y},{p1.x,p1.y},{p0.x,p1.y}};
                for (auto& c : corners) { float x,y; projPlane(c,x,y); pts.push_back({(int)x,(int)y}); }
            } else {
                for (const auto& p : shp.points) { float x,y; projPlane(p,x,y); pts.push_back({(int)x,(int)y}); }
            }
            bool fill = shp.filled && shp.kind != Minimap::MapShape::Kind::Line;
            if (fill && pts.size() >= 3) {
                MMFillPoly(p, pts, col);
            } else if (shp.kind == Minimap::MapShape::Kind::Line) {
                for (std::size_t i = 0; i + 1 < pts.size(); ++i)
                    MMThickLine(p, pts[i].x, pts[i].y, pts[i+1].x, pts[i+1].y, shp.thickness, col);
            } else {   // closed outline for Rect/Poly
                p.SetDrawColor(col.r, col.g, col.b, col.a);
                for (std::size_t i = 0; i < pts.size(); ++i) {
                    const Point& a2 = pts[i]; const Point& b2 = pts[(i+1)%pts.size()];
                    MMThickLine(p, a2.x, a2.y, b2.x, b2.y, shp.thickness, col);
                }
            }
        }
        // Range rings (concentric distance circles).
        if (mm->rangeRings > 0) {
            RGBA rc{(std::uint8_t)(mm->ringColor.r*255), (std::uint8_t)(mm->ringColor.g*255),
                         (std::uint8_t)(mm->ringColor.b*255), (std::uint8_t)(mm->ringColor.a*255*op)};
            for (int k = 1; k <= mm->rangeRings; ++k)
                MMDrawRing(p, cx, cy, radius * k / (mm->rangeRings + 1), 1, rc);
        }
        // View cone (FOV wedge from the centre along the player's facing).
        if (mm->viewCone) {
            RGBA vc{(std::uint8_t)(mm->viewConeColor.r*255), (std::uint8_t)(mm->viewConeColor.g*255),
                         (std::uint8_t)(mm->viewConeColor.b*255), (std::uint8_t)(mm->viewConeColor.a*255*op)};
            float half = mm->viewConeAngle * 0.5f * 0.0174532925f, L = mm->viewConeLength;
            Point apex{cx, cy};
            Point lft{(int)(cx + std::sin(selfAng-half)*L), (int)(cy - std::cos(selfAng-half)*L)};
            Point rgt{(int)(cx + std::sin(selfAng+half)*L), (int)(cy - std::cos(selfAng+half)*L)};
            MMFillTriangle(p, apex, lft, rgt, vc);
        }
        // Draw a marker (square/dot/triangle/arrow) at (px,py).
        auto drawBlip = [&](float px, float py, int half, RGBA col,
                            MinimapBlip::Shape shp, float ang) {
            switch (shp) {
                case MinimapBlip::Shape::Dot:
                    MMFillCircle(p, (int)px, (int)py, half, col); break;
                case MinimapBlip::Shape::Triangle:
                case MinimapBlip::Shape::Arrow: {
                    Point tip{(int)(px + std::sin(ang)*half), (int)(py - std::cos(ang)*half)};
                    float ba = 2.4f;
                    Point l{(int)(px + std::sin(ang-ba)*half), (int)(py - std::cos(ang-ba)*half)};
                    Point r{(int)(px + std::sin(ang+ba)*half), (int)(py - std::cos(ang+ba)*half)};
                    if (shp == MinimapBlip::Shape::Arrow) {
                        Point tail{(int)(px - std::sin(ang)*half*0.4f), (int)(py + std::cos(ang)*half*0.4f)};
                        MMFillTriangle(p, tip, l, tail, col);
                        MMFillTriangle(p, tip, r, tail, col);
                    } else {
                        MMFillTriangle(p, tip, l, r, col);
                    }
                } break;
                default: {
                    Rect br{(int)px - half, (int)py - half, half*2, half*2};
                    p.SetDrawColor(col.r, col.g, col.b, col.a);
                    p.FillRect(&br);
                }
            }
        };
        // Plot every MinimapBlip.
        for (const auto& bp : scene.Objects()) {
            if (!bp || !bp->active) continue;
            auto* bl = bp->GetComponent<MinimapBlip>();
            if (!bl || !bp->transform) continue;
            if (!Minimap::WithinRange(*mm, center, bp->transform->Position())) continue;   // radar range
            float mx, my;
            bool inside = Minimap::WorldToMapR(*mm, center, bp->transform->Position(), sz.x, sz.y, mapHeading, mx, my, wpp);
            if (!inside) {
                if (!mm->clampBlips) continue;
                float dx = mx - sz.x*0.5f, dy = my - sz.y*0.5f;
                float len = std::sqrt(dx*dx + dy*dy); if (len < 1e-3f) continue;
                float rr = (mm->circular ? radius : (sz.x < sz.y ? sz.x : sz.y) * 0.5f) - mm->blipSize;
                mx = sz.x*0.5f + dx/len*rr; my = sz.y*0.5f + dy/len*rr;
            }
            int half = (int)(bl->size > 0 ? bl->size : mm->blipSize);
            RGBA col{(std::uint8_t)(bl->color.r*255), (std::uint8_t)(bl->color.g*255),
                          (std::uint8_t)(bl->color.b*255), (std::uint8_t)(bl->color.a*255*op)};
            Tex bicon = bl->icon.empty() ? nullptr : p.LoadTexture(bl->icon);
            if (bicon) {
                p.SetTextureAlphaMod(bicon, (std::uint8_t)(255*op));
                Rect dr{(int)(o.x+mx)-half, (int)(o.y+my)-half, half*2, half*2};
                p.Copy(bicon, nullptr, &dr);
            } else {
                float ang = bl->rotateWithObject ? (Minimap::HeadingOf(bp->transform->Forward(), mm->useXZ) - mapHeading) : 0.0f;
                drawBlip(o.x + mx, o.y + my, half, col, bl->shape, ang);
            }
            if (mm->showLabels && !bl->label.empty()) {
                RGBA lc{(std::uint8_t)(mm->labelColor.r*255), (std::uint8_t)(mm->labelColor.g*255),
                             (std::uint8_t)(mm->labelColor.b*255), (std::uint8_t)(mm->labelColor.a*255*op)};
                DrawUIText(p, bl->label, o.x+mx+half+2, o.y+my-half, mm->labelSize, lc);
            }
        }
        // Route lines (GTA pink line from the player centre to a chosen marker
        // and/or the user-placed waypoint).
        auto drawRoute = [&](const Vec2& worldEN) {
            float rmx, rmy;
            Minimap::WorldToMapR(*mm, center, mm->PlaneToWorld(worldEN), sz.x, sz.y, mapHeading, rmx, rmy, wpp);
            p.SetDrawColor((std::uint8_t)(mm->routeColor.r*255), (std::uint8_t)(mm->routeColor.g*255),
                                   (std::uint8_t)(mm->routeColor.b*255), (std::uint8_t)(mm->routeColor.a*255*op));
            int rw = (int)mm->routeWidth; if (rw < 1) rw = 1;
            if (g_uiSmoothEdges) {
                MMThickLine(p, (float)cx, (float)cy, o.x + rmx, o.y + rmy, (float)rw,
                            RGBA{(std::uint8_t)(mm->routeColor.r*255), (std::uint8_t)(mm->routeColor.g*255),
                                 (std::uint8_t)(mm->routeColor.b*255), (std::uint8_t)(mm->routeColor.a*255*op)});
                return;
            }
            for (int t = -(rw/2); t <= rw/2; ++t)
                p.DrawLine(cx, cy+t, (int)(o.x+rmx), (int)(o.y+rmy)+t);
        };
        if (mm->routeMarker >= 0 && mm->routeMarker < (int)mm->markers.size())
            drawRoute(mm->markers[mm->routeMarker].world);
        if (mm->hasUserWaypoint) drawRoute(mm->userWaypoint);
        // Waypoint markers (GTA POIs): fixed world points drawn as diamonds.
        for (const auto& mk : mm->markers) {
            float mx, my;
            bool inside = Minimap::WorldToMapR(*mm, center, mm->PlaneToWorld(mk.world), sz.x, sz.y, mapHeading, mx, my, wpp);
            if (!inside) {
                if (!mm->clampBlips) continue;
                float dx = mx - sz.x*0.5f, dy = my - sz.y*0.5f;
                float len = std::sqrt(dx*dx + dy*dy); if (len < 1e-3f) continue;
                float rr = (mm->circular ? radius : (sz.x < sz.y ? sz.x : sz.y) * 0.5f) - mk.size;
                mx = sz.x*0.5f + dx/len*rr; my = sz.y*0.5f + dy/len*rr;
            }
            RGBA col{(std::uint8_t)(mk.color.r*255), (std::uint8_t)(mk.color.g*255),
                          (std::uint8_t)(mk.color.b*255), (std::uint8_t)(mk.color.a*255*op)};
            float px = o.x + mx, py = o.y + my; int h = (int)mk.size;
            Point top{(int)px,(int)(py-h)}, rgt{(int)(px+h),(int)py},
                      bot{(int)px,(int)(py+h)}, lft{(int)(px-h),(int)py};
            MMFillTriangle(p, top, rgt, bot, col);
            MMFillTriangle(p, top, lft, bot, col);
            if (mm->showLabels && !mk.label.empty()) {
                RGBA lc{(std::uint8_t)(mm->labelColor.r*255), (std::uint8_t)(mm->labelColor.g*255),
                             (std::uint8_t)(mm->labelColor.b*255), (std::uint8_t)(mm->labelColor.a*255*op)};
                DrawUIText(p, mk.label, px+h+2, py-h, mm->labelSize, lc);
            }
        }
        // User-placed waypoint (click-to-set): a bright diamond on the map.
        if (mm->hasUserWaypoint) {
            float wmx, wmy;
            Minimap::WorldToMapR(*mm, center, mm->PlaneToWorld(mm->userWaypoint), sz.x, sz.y, mapHeading, wmx, wmy, wpp);
            RGBA col{(std::uint8_t)(mm->userWaypointColor.r*255), (std::uint8_t)(mm->userWaypointColor.g*255),
                          (std::uint8_t)(mm->userWaypointColor.b*255), (std::uint8_t)(mm->userWaypointColor.a*255*op)};
            float px = o.x + wmx, py = o.y + wmy; int h = (int)(mm->blipSize * 1.3f);
            Point top{(int)px,(int)(py-h)}, rgt{(int)(px+h),(int)py},
                      bot{(int)px,(int)(py+h)}, lft{(int)(px-h),(int)py};
            MMFillTriangle(p, top, rgt, bot, col);
            MMFillTriangle(p, top, lft, bot, col);
        }
        // The target marker at the map center (the arrow shows facing; a north-up
        // map spins the arrow GTA-style).
        if (mm->showSelf) {
            int half = (int)(mm->blipSize);
            RGBA col{(std::uint8_t)(mm->targetColor.r*255), (std::uint8_t)(mm->targetColor.g*255),
                          (std::uint8_t)(mm->targetColor.b*255), (std::uint8_t)(mm->targetColor.a*255*op)};
            if (mm->playerArrow)
                drawBlip((float)cx, (float)cy, (int)(half*1.4f), col, MinimapBlip::Shape::Arrow, selfAng);
            else
                drawBlip((float)cx, (float)cy, half, col, MinimapBlip::Shape::Square, 0.0f);
        }
        // North indicator: a small tick at the edge in the world-north direction.
        if (mm->showNorth) {
            float northAng = -mapHeading;   // north is "up" rotated by the map heading
            float edge = (mm->circular ? radius : (sz.x < sz.y ? sz.x : sz.y) * 0.5f) - 4.0f;
            float nx = cx + std::sin(northAng) * edge, ny = cy - std::cos(northAng) * edge;
            RGBA nc{(std::uint8_t)(mm->northColor.r*255), (std::uint8_t)(mm->northColor.g*255),
                         (std::uint8_t)(mm->northColor.b*255), (std::uint8_t)(mm->northColor.a*255*op)};
            Point tip{(int)(cx + std::sin(northAng)*(edge+5)), (int)(cy - std::cos(northAng)*(edge+5))};
            Point l{(int)(nx + std::sin(northAng-2.4f)*5), (int)(ny - std::cos(northAng-2.4f)*5)};
            Point r{(int)(nx + std::sin(northAng+2.4f)*5), (int)(ny - std::cos(northAng+2.4f)*5)};
            MMFillTriangle(p, tip, l, r, nc);
        }
        // Restore clip and draw the border on top (rect rings or circle ring).
        if (hadClip) p.SetClipRect(&prevClip);
        else p.SetClipRect(nullptr);
        if (mm->circular) {
            MMDrawRing(p, cx, cy, radius, bw, bdCol);
        } else {
            p.SetDrawColor(bdCol.r, bdCol.g, bdCol.b, bdCol.a);
            for (int i = 0; i < bw; ++i) {
                Rect e{box.x + i, box.y + i, box.w - 2*i, box.h - 2*i};
                if (e.w <= 0 || e.h <= 0) break;
                p.DrawRect(&e);
            }
        }
    }
        else if (_it.kind == K_Crosshair) {   // aim reticle at screen center
        auto* cr = up->GetComponent<Crosshair>();
        if (!cr || !up->active || UIHidden(up.get())) continue;
        float op = _slot.opacity;
        Vec2 o, sz; if (!GetUIScreenRect(up.get(), (float)w, (float)h, o, sz)) continue;
        float cx = o.x + sz.x * 0.5f, cy = o.y + sz.y * 0.5f;
        int th = (int)(cr->thickness > 1 ? cr->thickness : 1);
        // A line drawn as a filled rect, with an optional 1px dark outline under it.
        auto drawLine = [&](int rx, int ry, int rw, int rh) {
            if (cr->outline) {
                p.SetDrawColor((std::uint8_t)(cr->outlineColor.r*255), (std::uint8_t)(cr->outlineColor.g*255),
                                       (std::uint8_t)(cr->outlineColor.b*255), (std::uint8_t)(cr->outlineColor.a*255*op));
                Rect ol{rx-1, ry-1, rw+2, rh+2};
                p.FillRect(&ol);
            }
            p.SetDrawColor((std::uint8_t)(cr->color.r*255), (std::uint8_t)(cr->color.g*255),
                                   (std::uint8_t)(cr->color.b*255), (std::uint8_t)(cr->color.a*255*op));
            Rect ln{rx, ry, rw, rh};
            p.FillRect(&ln);
        };
        if (cr->showLines) {
            int g0 = (int)(cr->gap + cr->spread), g1 = (int)(cr->gap + cr->spread + cr->length);
            if (cr->lineUp)    drawLine((int)cx - th/2, (int)cy - g1, th, g1 - g0);   // up
            if (cr->lineDown)  drawLine((int)cx - th/2, (int)cy + g0, th, g1 - g0);   // down
            if (cr->lineLeft)  drawLine((int)cx - g1, (int)cy - th/2, g1 - g0, th);   // left
            if (cr->lineRight) drawLine((int)cx + g0, (int)cy - th/2, g1 - g0, th);   // right
        }
        if (cr->circle) {
            int segs = 48; float rr = cr->circleRadius, ct = cr->circleThickness < 1 ? 1 : cr->circleThickness;
            p.SetDrawColor((std::uint8_t)(cr->circleColor.r*255), (std::uint8_t)(cr->circleColor.g*255),
                                   (std::uint8_t)(cr->circleColor.b*255), (std::uint8_t)(cr->circleColor.a*255*op));
            if (g_uiSmoothEdges) {   // one anti-aliased ring at the device resolution
                RGBA cc{(std::uint8_t)(cr->circleColor.r*255), (std::uint8_t)(cr->circleColor.g*255),
                        (std::uint8_t)(cr->circleColor.b*255), (std::uint8_t)(cr->circleColor.a*255*op)};
                MMDrawRing(p, (int)cx, (int)cy, (int)(rr + ct * 0.5f), (int)ct, cc);
                ct = 0;
            }
            for (float t = 0; t < ct; t += 1.0f) {
                float ring = rr + t;
                for (int i = 0; i < segs; ++i) {
                    float a0 = (float)i / segs * 6.2831853f;
                    p.DrawPoint((int)(cx + std::cos(a0) * ring), (int)(cy + std::sin(a0) * ring));
                }
            }
        }
        if (cr->dot) {
            int half = (int)(cr->dotSize * 0.5f); if (half < 1) half = 1;
            if (cr->outline) {
                p.SetDrawColor((std::uint8_t)(cr->outlineColor.r*255), (std::uint8_t)(cr->outlineColor.g*255),
                                       (std::uint8_t)(cr->outlineColor.b*255), (std::uint8_t)(cr->outlineColor.a*255*op));
                Rect od{(int)cx - half - 1, (int)cy - half - 1, half*2 + 2, half*2 + 2};
                p.FillRect(&od);
            }
            p.SetDrawColor((std::uint8_t)(cr->dotColor.r*255), (std::uint8_t)(cr->dotColor.g*255),
                                   (std::uint8_t)(cr->dotColor.b*255), (std::uint8_t)(cr->dotColor.a*255*op));
            Rect dr{(int)cx - half, (int)cy - half, half*2, half*2};
            p.FillRect(&dr);
        }
    }
        else if (_it.kind == K_Slider) {   // sliders
        auto* sl = up->GetComponent<UISlider>();
        if (!sl || !up->active || UIHidden(up.get())) continue;
        float op = _slot.opacity;
        Vec2 o = UIResolveOrigin(up.get(), (float)w, (float)h);
        enterScroll(up.get(), o);
        const float trackW = sl->TrackWidth();   // the value readout has its own space to the right of the track
        Rect bg{(int)o.x, (int)o.y, (int)trackW, (int)sl->size.y};
        FillUIShape(p, bg, sl->trackShape, sl->cornerRadius,
                    sl->background, sl->background, false, false, op);
        float f = sl->Fraction();
        Rect fl, kn;
        if (sl->vertical) {
            fl = {(int)o.x, (int)(o.y + sl->size.y * (1.0f - f)), (int)sl->size.x, (int)(sl->size.y * f)};
            int kh = (int)(sl->size.x * sl->knobSize);
            kn = {(int)o.x - 2, (int)(o.y + sl->size.y * (1.0f - f)) - kh / 2, (int)sl->size.x + 4, kh};
        } else {
            fl = {(int)o.x, (int)o.y, (int)(trackW * f), (int)sl->size.y};
            int kw = (int)(sl->size.y * sl->knobSize);
            kn = {(int)(o.x + trackW * f) - kw / 2, (int)o.y - 2, kw, (int)sl->size.y + 4};
        }
        // grok/ui-options: a fill gradient only when asked; otherwise the old solid fill.
        FillUIShape(p, fl, sl->trackShape, sl->cornerRadius,
                    sl->fill, sl->fillGradient ? sl->fillEnd : sl->fill,
                    sl->fillGradient, sl->fillGradient && !sl->vertical, op);
        // The handle: a circle when roundKnob, else a rounded tab. knobShape overrides that.
        UIShape ks = sl->roundKnob ? UIShape::Circle : UIShape::Rounded;
        if (sl->knobShape >= 0 && sl->knobShape < kUIShapeCount) ks = (UIShape)sl->knobShape;
        FillUIShape(p, kn, ks,
                    sl->cornerRadius, sl->knob, sl->knob, false, false, op);
        if (sl->showValue) {
            char vbuf[32];
            if (sl->valueFormat.empty()) std::snprintf(vbuf, sizeof(vbuf), "%.2f", sl->value);
            else std::snprintf(vbuf, sizeof(vbuf), "%s", sl->FormatValue().c_str());
            float px = 2.0f;
            RGBA tc{(std::uint8_t)(sl->textColor.r * 255), (std::uint8_t)(sl->textColor.g * 255),
                         (std::uint8_t)(sl->textColor.b * 255), (std::uint8_t)(sl->textColor.a * 255 * op)};
            // In its own space at the right end of the box, never over the track (a vertical slider has no room, so it keeps the old place).
            const float tw = UITextW(vbuf, px);
            float tx = o.x + sl->size.x - tw - 4.0f;
            if (!sl->vertical) { const float space = sl->size.x - trackW; tx = o.x + trackW + (space - tw) * 0.5f; if (tx < o.x + trackW + 2.0f) tx = o.x + trackW + 2.0f; }
            else if (tx < o.x + 2.0f) tx = o.x + 2.0f;
            DrawUIText(p, vbuf, tx, o.y + (sl->size.y - Font8x8::Height * px) * 0.5f, px, tc);
        }
        if (!sl->interactable) { Rect dr{(int)o.x, (int)o.y, (int)sl->size.x, (int)sl->size.y};
            p.SetDrawColor(30, 30, 35, 150); p.FillRect(&dr); }
    }
        else if (_it.kind == K_Stepper) {   // numeric steppers
        auto* st = up->GetComponent<UIStepper>();
        if (!st || !up->active || UIHidden(up.get())) continue;
        float op = _slot.opacity;
        Vec2 o = UIResolveOrigin(up.get(), (float)w, (float)h);
        enterScroll(up.get(), o);
        Rect bg{(int)o.x, (int)o.y, (int)st->size.x, (int)st->size.y};
        FillUIShape(p, bg, st->shape, st->cornerRadius, st->background, st->background, false, false, op);
        float bw = st->ButtonWidth();
        Rect minusR{(int)o.x, (int)o.y, (int)bw, (int)st->size.y};
        Rect plusR{(int)(o.x + st->size.x - bw), (int)o.y, (int)bw, (int)st->size.y};
        FillUIShape(p, minusR, st->shape, st->cornerRadius, st->button, st->button, false, false, op);
        FillUIShape(p, plusR, st->shape, st->cornerRadius, st->button, st->button, false, false, op);
        RGBA tc{(std::uint8_t)(st->textColor.r * 255), (std::uint8_t)(st->textColor.g * 255),
                     (std::uint8_t)(st->textColor.b * 255), (std::uint8_t)(st->textColor.a * 255 * op)};
        float px = 2.0f;
        // "-" and "+" glyphs centred on the end buttons.
        DrawUIText(p, "-", o.x + bw * 0.5f - Font8x8::Width * px * 0.5f,
                 o.y + (st->size.y - Font8x8::Height * px) * 0.5f, px, tc);
        DrawUIText(p, "+", o.x + st->size.x - bw * 0.5f - Font8x8::Width * px * 0.5f,
                 o.y + (st->size.y - Font8x8::Height * px) * 0.5f, px, tc);
        char vb[24];
        if (st->wholeNumbers) std::snprintf(vb, sizeof(vb), "%d", (int)st->value);
        else                  std::snprintf(vb, sizeof(vb), "%.2f", st->value);
        float tw = UITextW(vb, px);
        DrawUIText(p, vb, o.x + (st->size.x - tw) * 0.5f,
                 o.y + (st->size.y - Font8x8::Height * px) * 0.5f, px, tc);
        if (!st->interactable) { p.SetDrawColor(30, 30, 35, 150); p.FillRect(&bg); }
    }
        else if (_it.kind == K_Rating) {   // star ratings
        auto* rt = up->GetComponent<UIRating>();
        if (!rt || !up->active || UIHidden(up.get()) || rt->count <= 0) continue;
        float op = _slot.opacity;
        Vec2 o = UIResolveOrigin(up.get(), (float)w, (float)h);
        enterScroll(up.get(), o);
        float cw = rt->CellWidth();
        float d = Mathf::Min(cw, rt->size.y);          // star size (square cell)
        for (int i = 0; i < rt->count; ++i) {
            float cx = o.x + i * cw + (cw - d) * 0.5f;
            float cy = o.y + (rt->size.y - d) * 0.5f;
            Rect star{(int)cx, (int)cy, (int)d, (int)d};
            float f = rt->StarFill(i);              // 0, 0.5 or 1 (uses hover preview)
            FillUIShape(p, star, UIShape::Diamond, 0.0f, rt->off, rt->off, false, false, op);
            if (f > 0.0f) {
                // Reveal the filled color over the left fraction of the star so a
                // half rating shows a real half-star. Clip to the existing clip so
                // ratings inside a scroll view stay clipped.
                bool wasClip = p.IsClipEnabled();
                Rect prev; p.GetClipRect(&prev);
                Rect want{star.x, star.y, (int)(star.w * f + 0.5f), star.h}, use = want;
                if (wasClip) IntersectRect(&want, &prev, &use);
                p.SetClipRect(&use);
                FillUIShape(p, star, UIShape::Diamond, 0.0f, rt->on, rt->on, false, false, op);
                if (wasClip) p.SetClipRect(&prev);
                else         p.SetClipRect(nullptr);
            }
        }
    }
        else if (_it.kind == K_Toggle) {   // toggles (checkboxes)
        auto* tg = up->GetComponent<UIToggle>();
        if (!tg || !up->active || UIHidden(up.get())) continue;
        float op = _slot.opacity;
        Vec2 o = UIResolveOrigin(up.get(), (float)w, (float)h);
        enterScroll(up.get(), o);
        Rect box{(int)o.x, (int)o.y, (int)tg->size.x, (int)tg->size.y};
        float t = opt.interactive ? tg->AnimT()         // 0=off..1=on (smoothed)
                                  : (tg->on ? 1.0f : 0.0f);
        if (tg->style == UIToggle::Style::Switch) {     // pill track + sliding knob
            auto mix = [t](const Color& a, const Color& b) {
                return Color{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
                             a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
            };
            Color trk = mix(tg->boxColor, tg->checkColor);   // cross-fade the track
            FillUIShape(p, box, UIShape::Pill, 0.0f, trk, trk, false, false, op);
            int kd = box.h - 4;
            int kx = box.x + 2 + (int)((box.w - kd - 4) * t);   // glide the knob
            Rect knob{kx, box.y + 2, kd, kd};
            FillUIShape(p, knob, UIShape::Circle, 0.0f,
                        tg->knobColor, tg->knobColor, false, false, op);
        } else if (tg->style == UIToggle::Style::Radio) {   // round button, dot when picked
            const int d = std::min(box.w, box.h);
            Rect rb{box.x, box.y, d, d};
            FillUIShape(p, rb, UIShape::Circle, 0.0f, tg->boxColor, tg->boxColor, false, false, op);
            Color ring = tg->checkColor; ring.a *= (0.35f + 0.65f * t);
            const int rw = std::max(2, d / 12);
            Rect inner{rb.x + rw, rb.y + rw, d - 2 * rw, d - 2 * rw};
            FillUIShape(p, rb, UIShape::Circle, 0.0f, ring, ring, false, false, op);
            FillUIShape(p, inner, UIShape::Circle, 0.0f, tg->boxColor, tg->boxColor, false, false, op);
            if (t > 0.01f) {
                const int dd = std::max(2, (int)(d * 0.5f * t));
                Rect dot{rb.x + (d - dd) / 2, rb.y + (d - dd) / 2, dd, dd};
                FillUIShape(p, dot, UIShape::Circle, 0.0f, tg->checkColor, tg->checkColor, false, false, op);
            }
        } else {
            FillUIShape(p, box, UIShape::Rounded, tg->cornerRadius,
                        tg->boxColor, tg->boxColor, false, false, op);
            if (t > 0.01f) {                            // filled box + white tick (fades in)
                Color cc = tg->checkColor; cc.a *= t;
                FillUIShape(p, box, UIShape::Rounded, tg->cornerRadius,
                            cc, cc, false, false, op);
                const float bw = (float)box.w, bh = (float)box.h, lw = std::max(2.0f, bw * 0.11f);
                RGBA wc{255, 255, 255, (std::uint8_t)(255 * t * op)};
                MMThickLine(p, box.x + bw * 0.24f, box.y + bh * 0.52f, box.x + bw * 0.43f, box.y + bh * 0.70f, lw, wc);
                MMThickLine(p, box.x + bw * 0.43f, box.y + bh * 0.70f, box.x + bw * 0.77f, box.y + bh * 0.32f, lw, wc);
            }
        }
        float px = 2.0f * UITextScale();
        float tx = o.x + tg->size.x + 8.0f;
        float ty = o.y + (tg->size.y - Font8x8::Height * px) * 0.5f;
        RGBA tc{(std::uint8_t)(tg->textColor.r * 255), (std::uint8_t)(tg->textColor.g * 255),
                     (std::uint8_t)(tg->textColor.b * 255), (std::uint8_t)(tg->textColor.a * 255 * op)};
        DrawUIText(p, tg->label, tx, ty, px, tc);
        if (!tg->interactable) { Rect dr{box.x, box.y, box.w, box.h};
            p.SetDrawColor(30, 30, 35, 150); p.FillRect(&dr); }
    }
        else if (_it.kind == K_Tabs) {   // tab bars (segmented / underline / folder / pills / vertical)
        auto* tb = up->GetComponent<UITabs>();
        if (!tb || !up->active || UIHidden(up.get()) || tb->Count() <= 0) continue;
        float op = _slot.opacity;
        // Where the bar really is (inside its UI parent, stretch anchors resolved,
        // world-space projected) and its scale: the same rect its clicks are tested
        // against, so the tab under the pointer is the one that gets selected.
        Vec2 o, tsz; float tk = 1.0f;
        if (!GetUIScreenRect(up.get(), (float)w, (float)h, o, tsz, &tk)) continue;
        { Vec2 clipOnly = o; enterScroll(up.get(), clipOnly); }   // scroll-view clip (o already scrolled)
        Rect outerClip{0, 0, 0, 0};
        const bool hadClip = p.IsClipEnabled() == true;
        if (hadClip) p.GetClipRect(&outerClip);
        for (const UITabsDraw& d : tb->Paint(tsz.x, tsz.y, tk, UITextW)) {
            const float dx = o.x + d.x, dy = o.y + d.y;
            switch (d.kind) {
                case UITabsDraw::Kind::Fill: {
                    Rect r{(int)dx, (int)dy, (int)d.w, (int)d.h};
                    FillUIShape(p, r, d.shape, d.radius, d.color, d.color, false, false, op, 0, d.cornerMask);
                    break;
                }
                case UITabsDraw::Kind::Text: {
                    RGBA tc{(std::uint8_t)(d.color.r * 255), (std::uint8_t)(d.color.g * 255), (std::uint8_t)(d.color.b * 255),
                                 (std::uint8_t)(d.color.a * 255 * op)};
                    DrawUIText(p, d.text, dx, dy, d.px, tc);
                    break;
                }
                case UITabsDraw::Kind::Icon:
                    if (Tex itex = p.LoadTexture(d.text)) {
                        Rect ir{(int)dx, (int)dy, (int)d.w, (int)d.h};
                        p.SetTextureColorMod(itex, 255, 255, 255);
                        p.SetTextureAlphaMod(itex, (std::uint8_t)(255 * d.color.a * op));
                        p.Copy(itex, nullptr, &ir);
                        p.SetTextureAlphaMod(itex, 255);
                    } else if (!d.text.empty()) {
                        p.MissingTexture(d.text, Rect{(int)dx, (int)dy, (int)d.w, (int)d.h});
                    }
                    break;
                case UITabsDraw::Kind::Clip: {   // the scrolling strip, inside any scroll-view clip
                    Rect want{(int)dx, (int)dy, (int)d.w, (int)d.h}, use = want;
                    if (hadClip) IntersectRect(&want, &outerClip, &use);
                    p.SetClipRect(&use);
                    break;
                }
                case UITabsDraw::Kind::Unclip:
                    p.SetClipRect(hadClip ? &outerClip : nullptr);
                    break;
            }
        }
        p.SetClipRect(hadClip ? &outerClip : nullptr);
    }
        else if (_it.kind == K_Button) {   // buttons (box, icon, label)
        auto* btn = up->GetComponent<UIButton>();
        if (!btn || !up->active || UIHidden(up.get())) continue;
        float op = _slot.opacity;
        Color bg = btn->DisplayColor();
        // World-aware rect + scale: a 3D button (own object / world canvas)
        // projects through the camera, so its box and label shrink with
        // distance. A screen-space button keeps its design size and k=1,
        // unchanged from before.
        float bk = 1.0f;
        Vec2 o, bsz;
        if (btn->IsWorldSpaceUI()) {
            GetUIScreenRect(up.get(), (float)w, (float)h, o, bsz, &bk);
        } else {
            o = UIResolveOrigin(up.get(), (float)w, (float)h);
            bsz = btn->size;
        }
        enterScroll(up.get(), o);
        Rect r{(int)o.x, (int)o.y, (int)bsz.x, (int)bsz.y};
        float sc = btn->DisplayScale();
        if (sc != 1.0f) {
            int gx = (int)(bsz.x * (sc - 1.0f) * 0.5f);
            int gy = (int)(bsz.y * (sc - 1.0f) * 0.5f);
            r.x -= gx; r.y -= gy; r.w += 2 * gx; r.h += 2 * gy;
        }
        if (btn->shadow) {                              // drop shadow behind (same shape)
            Rect sh{r.x + (int)btn->shadowOffset.x, r.y + (int)btn->shadowOffset.y, r.w, r.h};
            FillUIShadow(p, sh, btn->shape, btn->cornerRadius,
                         btn->shadowColor, btn->shadowSoftness, op);
        }
        if (btn->borderWidth > 0.0f) {                  // fill, then the border ring over it
            FillUIShape(p, r, btn->shape, btn->cornerRadius, bg, bg, false, false, op);
            FillUIRing(p, r, btn->shape, btn->cornerRadius, btn->borderColor,
                       std::max(1, (int)(btn->borderWidth + 0.5f)), op);
        } else {
            FillUIShape(p, r, btn->shape, btn->cornerRadius, bg, bg, false, false, op);
        }
        // Optional icon (left by default, right when iconRight); the label
        // takes the remaining space. Press shifts content down slightly.
        float shift = btn->PressShift() * bk;
        float isz = (!btn->icon.empty() && btn->iconSize > 0.0f) ? btn->iconSize * bk : 0.0f;
        // Icon and label are centred as one group (not the icon at the edge and the label centred in what is left) when both fit.
        float groupIconX = -1.0f, groupLabelX = -1.0f;
        if (isz > 0.0f && !btn->label.empty() && !UIButtonTextChild(up.get())) {
            const float gpx = btn->fontScale * bk * UITextScale();
            okay::TtfFont* gf = okay::FontForText(btn->fontPath.empty() ? g_uiDefaultFont : okay::GetFont(btn->fontPath), btn->label);
            const float gtw = UIMeasureText(gf, btn->label, gpx), gap = 8.0f * bk, total = isz + gap + gtw;
            if (total <= bsz.x - 8.0f * bk) {
                const float sx = o.x + (bsz.x - total) * 0.5f;
                groupIconX = btn->iconRight ? sx + gtw + gap : sx;
                groupLabelX = btn->iconRight ? sx : sx + isz + gap;
            }
        }
        if (isz > 0.0f) {
            const std::string vname = uilib::IconNameOf(btn->icon);
            Tex itex = vname.empty() ? p.LoadTexture(btn->icon) : nullptr;
            float ix = groupIconX >= 0.0f ? groupIconX : btn->iconRight ? (o.x + bsz.x - isz - 8.0f * bk) : (o.x + 8.0f * bk);
            Rect ir{(int)ix, (int)(o.y + (bsz.y - isz) * 0.5f + shift), (int)isz, (int)isz};
            if (!vname.empty()) {   // a vector icon in the label colour
                auto b8 = [](float v) { int i = (int)(v * 255.0f + 0.5f); return (std::uint8_t)(i < 0 ? 0 : i > 255 ? 255 : i); };
                DrawVectorIcon(p, vname, ir.x, ir.y, ir.w, RGBA{b8(btn->textColor.r), b8(btn->textColor.g), b8(btn->textColor.b), b8(btn->textColor.a * op)}, g_uiDeviceScale);
            } else if (itex) { p.SetTextureColorMod(itex, 255, 255, 255); p.SetTextureAlphaMod(itex, 255);
                        p.Copy(itex, nullptr, &ir); }
            else p.MissingTexture(btn->icon, ir);
        }
        // Center the label at the button's font scale, within the area beside
        // the icon. Skip the built-in label when a child Text object provides it
        // — that child draws itself in the text pass.
        if (!UIButtonTextChild(up.get())) {
            float px = btn->fontScale * bk * UITextScale();   // accessibility text size
            okay::TtfFont* fnt = okay::FontForText(btn->fontPath.empty() ? g_uiDefaultFont : okay::GetFont(btn->fontPath), btn->label);
            float tw = UIMeasureText(fnt, btn->label, px);
            float left  = o.x + (isz > 0.0f && !btn->iconRight ? isz + 12.0f * bk : 0.0f);
            float right = o.x + bsz.x - (isz > 0.0f && btn->iconRight ? isz + 12.0f * bk : 0.0f);
            float tx = groupLabelX >= 0.0f ? groupLabelX : left + ((right - left) - tw) * 0.5f;
            float ty = o.y + (bsz.y - Font8x8::Height * px) * 0.5f + shift;
            Color tcc = btn->CurrentTextColor();
            RGBA tc{(std::uint8_t)(tcc.r * 255), (std::uint8_t)(tcc.g * 255), (std::uint8_t)(tcc.b * 255), (std::uint8_t)(tcc.a * 255 * op)};
            DrawUIText(p, btn->label, tx, ty, px, tc, 0, 0, false, false, RGBA{0,0,0,0}, fnt);
        }
    }
        else if (_it.kind == K_Text) {   // screen-space text — on top of panels/controls
        auto* tr = up->GetComponent<TextRenderer>();
        if (!tr || !up->active || !tr->screenSpace || UIHidden(up.get())) continue;
        float op = _slot.opacity;   // canvas master fade
        RGBA col{(std::uint8_t)(tr->color.r * 255), (std::uint8_t)(tr->color.g * 255),
                      (std::uint8_t)(tr->color.b * 255), (std::uint8_t)(tr->color.a * 255 * op)};
        RGBA sh{(std::uint8_t)(tr->shadowColor.r * 255), (std::uint8_t)(tr->shadowColor.g * 255),
                     (std::uint8_t)(tr->shadowColor.b * 255), (std::uint8_t)(tr->shadowColor.a * 255 * op)};
        RGBA ol{(std::uint8_t)(tr->outlineColor.r * 255), (std::uint8_t)(tr->outlineColor.g * 255),
                     (std::uint8_t)(tr->outlineColor.b * 255), (std::uint8_t)(tr->outlineColor.a * 255 * op)};
        // In-world text projects through the camera (so it sits in 3D like the
        // other widgets) instead of anchoring to the screen — either under a
        // world-space Canvas, or as a standalone WorldSpaceUI object (its own 3D
        // text). UIResolveOrigin/UIScaleFor are world-aware; pixelSize scales by k.
        Canvas* tcv = _slot.canvas;
        bool tWorld = (tcv && tcv->worldSpace) || WorldUIRoot(up.get()) != nullptr;
        float ps = tr->EffectivePixelSize(), ls = tr->letterSpacing, lp = tr->lineSpacing;
        float tk = 1.0f;
        if (tWorld) {
            tk = UIScaleFor(up.get(), (float)w, (float)h);
            if (tk <= 0.0f) continue;   // canvas behind the camera
            ps *= tk;
        }
        // Text inside a Scroll View / List View / Mask scrolls with it and is clipped to it, like every other widget.
        { Vec2 clipOnly{0.0f, 0.0f}; if (!tWorld) enterScroll(up.get(), clipOnly); }
        if (tr->background) {                       // label background box
            Vec2 b = UIResolveOrigin(up.get(), (float)w, (float)h);   // parent-relative box
            if (!tWorld) b = b - UIScrollTotal(up.get());
            Rect br{(int)b.x, (int)b.y, (int)(tr->size.x * tk), (int)(tr->size.y * tk)};
            p.SetDrawColor((std::uint8_t)(tr->backgroundColor.r * 255),
                                   (std::uint8_t)(tr->backgroundColor.g * 255), (std::uint8_t)(tr->backgroundColor.b * 255),
                                   (std::uint8_t)(tr->backgroundColor.a * 255 * op));
            p.FillRect(&br);
        }
        Vec2 o = tWorld ? UIResolveOrigin(up.get(), (float)w, (float)h)
                        : UITextDrawOrigin(up.get(), tr, (float)w, (float)h);   // parent-relative + align
        if (!tWorld) o = o - UIScrollTotal(up.get());
        if (tWorld) {
            // The text's own alignment inside its projected box, as the same text has on screen: left / centre / right
            // and top / middle / bottom. (In the world it used to sit at the box's top-left whatever these said, so a
            // centred badge label hugged the left edge of its pill and a middle-aligned name sat on the top of its box.)
            Vec2 bo, bsz;
            if (GetUIScreenRect(up.get(), (float)w, (float)h, bo, bsz)) {
                const float tw = (float)tr->PixelWidth() * ps, th = (float)tr->PixelHeight() * ps;
                if (tr->align == 1)      o.x += (bsz.x - tw) * 0.5f;
                else if (tr->align == 2) o.x += bsz.x - tw;
                o.y += tr->alignBottom ? (bsz.y - th) : (tr->vcenter ? (bsz.y - th) * 0.5f : 0.0f);
            }
        }
        thread_local std::string dispScratch;
        const std::string& disp = tr->DisplayText(dispScratch);
        bool it = tr->italic;
        RGBA c2{(std::uint8_t)(tr->colorBottom.r * 255), (std::uint8_t)(tr->colorBottom.g * 255),
                     (std::uint8_t)(tr->colorBottom.b * 255), (std::uint8_t)(tr->colorBottom.a * 255 * op)};
        okay::TtfFont* fnt = tr->Font();
        if (tr->shadow)
            DrawUIText(p, disp, o.x + tr->shadowOffset.x * ps,
                     o.y + tr->shadowOffset.y * ps, ps, sh, ls, lp, it, false, RGBA{0,0,0,0}, fnt);
        // Bold / outline offsets: one font pixel for the 8x8 font; for smooth TTF text
        // about one screen pixel (a font pixel is 5 px at title size: a double image).
        const float fo = fnt ? (ps * 8.0f >= 30.0f ? 1.5f : 1.0f) : ps;
        if (tr->outline) {
            DrawUIText(p, disp, o.x - fo, o.y, ps, ol, ls, lp, it, false, RGBA{0,0,0,0}, fnt);
            DrawUIText(p, disp, o.x + fo, o.y, ps, ol, ls, lp, it, false, RGBA{0,0,0,0}, fnt);
            DrawUIText(p, disp, o.x, o.y - fo, ps, ol, ls, lp, it, false, RGBA{0,0,0,0}, fnt);
            DrawUIText(p, disp, o.x, o.y + fo, ps, ol, ls, lp, it, false, RGBA{0,0,0,0}, fnt);
        }
        if (tr->richText && tr->screenSpace && RichTextHasTags(tr->text)) {
            const okay::RichText rt = okay::ParseRichText(tr->text);
            std::vector<RichLinkRect> rects;
            DrawRichRuns(p, disp, rt.styles, o.x, o.y, ps, col, ls, lp, it, tr->bold, fo, fnt, &rt.links, &rt.icons, &rects);
            if (rects.empty()) RichLinkTable().erase(up.get()); else RichLinkTable()[up.get()] = std::move(rects);
        } else {
            DrawUIText(p, disp, o.x, o.y, ps, col, ls, lp, it, tr->gradient, c2, fnt, tr->bold);
        }
    }
        else if (_it.kind == K_WorldUI) {   // in-world UI labels/markers (3D -> screen)
        auto* wu = up->GetComponent<WorldUI>();
        if (!wu || !up->active || UIHidden(up.get()) || !up->transform) continue;
        Vec3 wp = up->transform->Position() + wu->worldOffset;
        Vec2 sp; float depth = 0.0f;
        if (opt.worldLabelsViaUIWorld) {   // the caller's view camera (editor views)
            if (!UIWorld().active || !UIWorld().project || !UIWorld().project(wp, sp, depth)) continue;
        } else {
            Camera* mc = scene.mainCamera;
            if (!mc) continue;
            if (!mc->WorldToScreen(wp, (float)w, (float)h, sp, &depth)) continue;   // behind camera
        }
        if (wu->maxDistance > 0.0f && depth > wu->maxDistance) continue;
        float scale = wu->pixelSize;
        if (wu->scaleWithDistance && depth > 0.001f)
            scale = Mathf::Clamp(wu->pixelSize * (wu->refDistance / depth),
                                 wu->pixelSize * wu->minScale, wu->pixelSize * wu->maxScale);
        float op = _slot.opacity;
        float tw = UITextW(wu->text, scale);
        float th = Font8x8::Height * scale;
        float tx = sp.x - tw * 0.5f, ty = sp.y - th * 0.5f;
        if (wu->background.a > 0.001f) {
            Rect bg{(int)(tx - 5), (int)(ty - 4), (int)(tw + 10), (int)(th + 8)};
            FillUIShape(p, bg, UIShape::Rounded, 4.0f, wu->background, wu->background, false, false, op);
        }
        RGBA tc{(std::uint8_t)(wu->color.r * 255), (std::uint8_t)(wu->color.g * 255),
                     (std::uint8_t)(wu->color.b * 255), (std::uint8_t)(wu->color.a * 255 * op)};
        DrawUIText(p, wu->text, tx, ty, scale, tc);
        if (wu->bar >= 0.0f) {                          // optional health/progress bar under the text
            float bw = tw > 36.0f ? tw : 36.0f;
            float bx = sp.x - bw * 0.5f, by = ty + th + 3.0f, bh = 4.0f * scale;
            Rect bgb{(int)bx, (int)by, (int)bw, (int)bh};
            FillUIShape(p, bgb, UIShape::Rounded, 2.0f, wu->barBackground, wu->barBackground, false, false, op);
            Rect fb{(int)bx, (int)by, (int)(bw * Mathf::Clamp01(wu->bar)), (int)bh};
            FillUIShape(p, fb, UIShape::Rounded, 2.0f, wu->barColor, wu->barColor, false, false, op);
        }
    }
        else if (_it.kind == K_Dropdown) {   // dropdowns (header + open list)
        auto* dd = up->GetComponent<UIDropdown>();
        if (!dd || !up->active || UIHidden(up.get())) continue;
        Vec2 o = UIResolveOrigin(up.get(), (float)w, (float)h);
        enterScroll(up.get(), o);
        Rect hdr{(int)o.x, (int)o.y, (int)dd->size.x, (int)dd->size.y};
        FillUIShape(p, hdr, dd->shape, dd->cornerRadius,
                    dd->color, dd->color, false, false, 1.0f);
        float px = 2.0f;
        float ty = o.y + (dd->size.y - Font8x8::Height * px) * 0.5f;
        RGBA tc{(std::uint8_t)(dd->textColor.r * 255), (std::uint8_t)(dd->textColor.g * 255),
                     (std::uint8_t)(dd->textColor.b * 255), (std::uint8_t)(dd->textColor.a * 255)};
        RGBA htc = dd->HasSelection() ? tc : RGBA{150, 152, 158, 255};
        DrawUIText(p, dd->HeaderText(), o.x + 8.0f, ty, px, htc);
        if (dd->open) {
            float top = o.y + dd->size.y;
            if (!dd->ListExtras()) {
                for (int i = 0; i < (int)dd->options.size(); ++i) {
                    Rect orow{(int)o.x, (int)(top + i * dd->size.y), (int)dd->size.x, (int)dd->size.y};
                    const Color& rc = (i == dd->HoveredOption()) ? dd->hoverColor : dd->listColor;
                    p.SetDrawColor((std::uint8_t)(rc.r * 255), (std::uint8_t)(rc.g * 255),
                                           (std::uint8_t)(rc.b * 255), (std::uint8_t)(rc.a * 255));
                    p.FillRect(&orow);
                    DrawUIText(p, dd->options[i], o.x + 8.0f,
                             orow.y + (dd->size.y - Font8x8::Height * px) * 0.5f, px, tc);
                }
            } else {
                // grok/ui-controls: optional search row, a cap on visible rows, and a glyph per option.
                int row = 0;
                auto rowBox = [&](int r) {
                    return Rect{(int)o.x, (int)(top + r * dd->size.y), (int)dd->size.x, (int)dd->size.y};
                };
                if (dd->searchable) {
                    Rect orow = rowBox(row++);
                    const Color& rc = (dd->HoveredOption() == UIDropdown::kSearchRow) ? dd->hoverColor : dd->listColor;
                    p.SetDrawColor((std::uint8_t)(rc.r * 255), (std::uint8_t)(rc.g * 255),
                                           (std::uint8_t)(rc.b * 255), (std::uint8_t)(rc.a * 255));
                    p.FillRect(&orow);
                    const std::string& q = dd->Filter();
                    RGBA sc = q.empty() ? RGBA{150, 152, 158, 255} : tc;
                    DrawUIText(p, q.empty() ? std::string("Search...") : q, o.x + 8.0f,
                             orow.y + (dd->size.y - Font8x8::Height * px) * 0.5f, px, sc);
                }
                const int firstRow = row;
                for (int i = 0; i < (int)dd->options.size(); ++i) {
                    if (dd->ShownSlot(i) < 0) continue;
                    Rect orow = rowBox(row++);
                    const Color& rc = (i == dd->HoveredOption()) ? dd->hoverColor : dd->listColor;
                    p.SetDrawColor((std::uint8_t)(rc.r * 255), (std::uint8_t)(rc.g * 255),
                                           (std::uint8_t)(rc.b * 255), (std::uint8_t)(rc.a * 255));
                    p.FillRect(&orow);
                    const std::string& glyph = dd->IconOf(i);
                    float tx = o.x + 8.0f;
                    float ty = orow.y + (dd->size.y - Font8x8::Height * px) * 0.5f;
                    if (!glyph.empty()) {
                        DrawUIText(p, glyph, tx, ty, px, tc);
                        tx += UITextW(glyph, px) + 6.0f;
                    }
                    DrawUIText(p, dd->options[i], tx, ty, px, tc);
                }
                if (dd->Scrolls()) {   // a scrollbar on the right edge: the thumb shows where the visible rows sit
                    const int matches = dd->MatchCount(), vis = dd->maxVisible;
                    const int trackY = (int)(top + firstRow * dd->size.y), trackH = (int)(vis * dd->size.y);
                    const int barW = 6, barX = (int)(o.x + dd->size.x) - barW - 3;
                    Rect track{barX, trackY + 2, barW, trackH - 4};
                    p.SetDrawColor(255, 255, 255, 38); p.FillRect(&track);
                    const int thumbH = std::max(14, (int)((float)track.h * (float)vis / (float)matches));
                    const int span = track.h - thumbH, steps = matches - vis;
                    Rect thumb{barX, track.y + (steps > 0 ? span * dd->ScrollOffset() / steps : 0), barW, thumbH};
                    p.SetDrawColor(tc.r, tc.g, tc.b, 190); p.FillRect(&thumb);
                }
            }
        }
        if (!dd->interactable) { Rect dr{(int)o.x, (int)o.y, (int)dd->size.x, (int)dd->size.y};
            p.SetDrawColor(30, 30, 35, 150); p.FillRect(&dr); }
    }
        else if (_it.kind == K_Input) {   // input fields (box + text + caret)
        auto* in = up->GetComponent<UIInputField>();
        if (!in || !up->active || UIHidden(up.get())) continue;
        Vec2 o = UIResolveOrigin(up.get(), (float)w, (float)h);
        enterScroll(up.get(), o);
        Rect box{(int)o.x, (int)o.y, (int)in->size.x, (int)in->size.y};
        Color bg = in->CurrentColor();
        // Focus ring: draw the border shape behind, then the field inset over it.
        if (in->focused && in->borderWidth > 0.0f) {
            FillUIShape(p, box, in->shape, in->cornerRadius, bg, bg, false, false, 1.0f);
            FillUIRing(p, box, in->shape, in->cornerRadius, in->borderColor,
                       std::max(1, (int)(in->borderWidth + 0.5f)), 1.0f);
        } else {
            FillUIShape(p, box, in->shape, in->cornerRadius, bg, bg, false, false, 1.0f);
        }
        float px = 2.0f, pad = 6.0f;
        bool empty = in->text.empty();
        const Color& txc = empty ? in->placeholderColor : in->textColor;
        RGBA tc{(std::uint8_t)(txc.r * 255), (std::uint8_t)(txc.g * 255), (std::uint8_t)(txc.b * 255), (std::uint8_t)(txc.a * 255)};
        if (!in->multiline) {
            std::string full = empty ? in->placeholder : in->DisplayText();
            // Horizontal scroll: drop leading characters until the text before the caret
            // fits, so the caret stays visible; then drop trailing characters that don't fit.
            const float avail = in->size.x - pad * 2;
            const std::size_t caretAt = empty ? 0 : std::min(in->DisplayTextBeforeCaret().size(), full.size());
            std::size_t start = 0;
            while (start < caretAt && UITextW(full.substr(start, caretAt - start), px) > avail) ++start;
            std::string shown = full.substr(start);
            while (shown.size() > 1 && UITextW(shown, px) > avail) shown.pop_back();
            float ty = o.y + (in->size.y - Font8x8::Height * px) * 0.5f;
            DrawUIText(p, shown, o.x + pad, ty, px, tc);
            if (in->focused && in->CaretVisible()) {        // blinking caret at the caret position
                // Measured on the drawn (shaped) text, so the caret lands where it is in right-to-left text too.
                int cx = (int)(o.x + pad + UICaretX(nullptr, shown, caretAt - start, px));
                p.SetDrawColor(255, 255, 255, 220);
                p.DrawLine(cx, (int)ty, cx, (int)(ty + Font8x8::Height * px));
            }
        } else {
            // grok/ui-controls: stack lines. Enter inserted the newlines.
            const std::string src = empty ? in->placeholder : in->DisplayText();
            const float lineH = Font8x8::Height * px;
            float y = o.y + pad;
            std::string line;
            // The caret's line and the text before it on that line.
            const std::string before = empty ? std::string() : in->DisplayTextBeforeCaret();
            const std::size_t nl = before.rfind('\n');
            const int caretLine = (int)std::count(before.begin(), before.end(), '\n');
            const std::size_t caretCol = nl == std::string::npos ? before.size() : before.size() - nl - 1;
            int lineNo = 0;
            float caretX = 0.0f, caretY = y;
            auto emit = [&]() {
                std::string shown = line;
                while (shown.size() > 1 && UITextW(shown, px) > in->size.x - pad * 2) shown.erase(0, 1);
                if (y + lineH <= o.y + in->size.y + 1.0f)
                    DrawUIText(p, shown, o.x + pad, y, px, tc);
                if (lineNo == caretLine) {   // the caret, measured from the first character drawn
                    const std::size_t dropped = line.size() - shown.size();
                    const std::size_t col = std::min(caretCol, line.size());
                    caretX = UICaretX(nullptr, shown, col > dropped ? col - dropped : 0, px);
                    caretY = y;
                }
                ++lineNo;
                y += lineH;
            };
            for (char c : src) {
                if (c == '\n') { emit(); line.clear(); }
                else line.push_back(c);
            }
            emit();
            if (in->focused && in->CaretVisible()) {
                int cx = (int)(o.x + pad + caretX);
                p.SetDrawColor(255, 255, 255, 220);
                p.DrawLine(cx, (int)caretY, cx, (int)(caretY + lineH));
            }
        }
    }
        else if (_it.kind == K_TouchStick) {   // virtual joystick
        auto* js = up->GetComponent<VirtualJoystick>();
        if (!js || !up->active || UIHidden(up.get()) || !js->Shows()) continue;
        float op = _slot.opacity * ((js->tracking || !js->fadeWhenIdle) ? 1.0f : 0.45f);
        Vec2 o = UIResolveOrigin(up.get(), (float)w, (float)h);
        if (js->floating && js->tracking) o = js->floatCenter - js->size * 0.5f;
        enterScroll(up.get(), o);
        const int cx = (int)(o.x + js->size.x * 0.5f), cy = (int)(o.y + js->size.y * 0.5f);
        int rad = (int)(std::min(js->size.x, js->size.y) * 0.5f); if (rad < 4) rad = 4;
        auto bytes = [op](const Color& c) {
            return RGBA{(std::uint8_t)(c.r * 255), (std::uint8_t)(c.g * 255),
                        (std::uint8_t)(c.b * 255), (std::uint8_t)(c.a * 255 * op)};
        };
        MMFillCircle(p, cx, cy, rad, bytes(js->baseColor));
        int kr = (int)(rad * js->knobScale); if (kr < 6) kr = 6;
        MMFillCircle(p, cx + (int)js->knob.x, cy + (int)js->knob.y, kr, bytes(js->knobColor));
        MMDrawRing(p, cx, cy, rad - 1, 2, bytes(js->knobColor));
    }
        else if (_it.kind == K_TouchPad) {   // directional pad
        auto* dp = up->GetComponent<TouchDPad>();
        if (!dp || !up->active || UIHidden(up.get()) || !dp->Shows()) continue;
        float op = _slot.opacity;
        Vec2 o = UIResolveOrigin(up.get(), (float)w, (float)h);
        enterScroll(up.get(), o);
        Rect box{(int)o.x, (int)o.y, (int)dp->size.x, (int)dp->size.y};
        FillUIShape(p, box, UIShape::Rounded, 18.0f, dp->color, dp->color, false, false, op);
        const int arm = std::max(8, (int)(std::min(dp->size.x, dp->size.y) * 0.28f));
        const int cx = (int)(o.x + dp->size.x * 0.5f), cy = (int)(o.y + dp->size.y * 0.5f);
        auto armRect = [&](int dir) {
            // 0 up, 1 down, 2 left, 3 right
            if (dir == 0) return Rect{cx - arm / 2, (int)o.y + 6, arm, (int)(dp->size.y * 0.5f) - arm / 2};
            if (dir == 1) return Rect{cx - arm / 2, cy, arm, (int)(dp->size.y * 0.5f) - 6};
            if (dir == 2) return Rect{(int)o.x + 6, cy - arm / 2, (int)(dp->size.x * 0.5f) - arm / 2, arm};
            return Rect{cx, cy - arm / 2, (int)(dp->size.x * 0.5f) - 6, arm};
        };
        const bool on[4] = {dp->value.y > 0.5f, dp->value.y < -0.5f, dp->value.x < -0.5f, dp->value.x > 0.5f};
        for (int dir = 0; dir < 4; ++dir) {
            const Color& c = on[dir] ? dp->pressedColor : dp->color;
            Color brighter{std::min(1.0f, c.r + 0.25f), std::min(1.0f, c.g + 0.25f), std::min(1.0f, c.b + 0.25f), c.a};
            FillUIShape(p, armRect(dir), UIShape::Rounded, 8.0f, on[dir] ? dp->pressedColor : brighter,
                        on[dir] ? dp->pressedColor : brighter, false, false, op);
        }
    }
        else if (_it.kind == K_TouchButton) {   // touch action button
        auto* tb = up->GetComponent<TouchButton>();
        if (!tb || !up->active || UIHidden(up.get()) || !tb->Shows()) continue;
        float op = _slot.opacity;
        Vec2 o = UIResolveOrigin(up.get(), (float)w, (float)h);
        enterScroll(up.get(), o);
        const Color& fill = tb->pressed ? tb->pressedColor : tb->color;
        Rect box{(int)o.x, (int)o.y, (int)tb->size.x, (int)tb->size.y};
        FillUIShape(p, box, UIShape::Circle, 0.0f, fill, fill, false, false, op);
        if (!tb->label.empty()) {
            const float px = 2.0f;
            const float tw = UITextW(tb->label, px);
            RGBA tc{(std::uint8_t)(tb->textColor.r * 255), (std::uint8_t)(tb->textColor.g * 255),
                    (std::uint8_t)(tb->textColor.b * 255), (std::uint8_t)(tb->textColor.a * 255 * op)};
            DrawUIText(p, tb->label, o.x + (tb->size.x - tw) * 0.5f,
                       o.y + (tb->size.y - 8.0f * px) * 0.5f, px, tc);
        }
    }
        else if (_it.kind == K_FocusRing) {   // keyboard/gamepad focus ring (hidden after a click)
        if (!UIShowsFocusRing(up.get())) continue;
        p.SetClipRect(nullptr);   // overlay — never scroll-clipped
        UIRect r = GetUIRect(up.get());
        if (!r.valid || !r.position) continue;
        Vec2 o, sz;   // where the widget actually is (parents, canvases, world space)
        if (!GetUIScreenRect(up.get(), (float)w, (float)h, o, sz)) { o = UIResolveOrigin(up.get(), (float)w, (float)h); sz = r.size; }
        const UIFocusStyle st = ResolveUIFocusStyle(up.get());
        const float fade = UIFocusPulseAlpha(st);
        auto toRgba = [fade](const Color& c) {
            return RGBA{(std::uint8_t)(c.r * 255.0f + 0.5f), (std::uint8_t)(c.g * 255.0f + 0.5f),
                        (std::uint8_t)(c.b * 255.0f + 0.5f),
                        (std::uint8_t)std::min(255.0f, c.a * fade * 255.0f + 0.5f)};
        };
        auto stroke = [&](float pad, float width, RGBA col) {
            if (width < 0.5f) return;
            const float x0 = o.x - pad, y0 = o.y - pad, x1 = o.x + sz.x + pad, y1 = o.y + sz.y + pad;
            MMThickLine(p, x0, y0, x1, y0, width, col);
            MMThickLine(p, x1, y0, x1, y1, width, col);
            MMThickLine(p, x1, y1, x0, y1, width, col);
            MMThickLine(p, x0, y1, x0, y0, width, col);
        };
        if (st.highVisibility)
            stroke(st.padding + st.thickness * 0.5f + 2.0f, std::max(3.0f, st.thickness * 0.55f), RGBA{0, 0, 0, 230});
        stroke(st.padding, st.thickness, toRgba(st.color));
    }
        else if (_it.kind == K_ModalDim) {   // an open modal's backdrop: dims the whole screen
        auto* md = up->GetComponent<UIModal>();
        if (!md || !md->IsOpen() || !md->dimBackground) continue;
        uiUseScale(1.0f);
        p.SetClipRect(nullptr);
        Rect all{0, 0, uiFullW, uiFullH};
        p.SetDrawColor((std::uint8_t)(md->dimColor.r * 255), (std::uint8_t)(md->dimColor.g * 255),
                       (std::uint8_t)(md->dimColor.b * 255), (std::uint8_t)(md->dimColor.a * 255 * _slot.opacity));
        p.FillRect(&all);
    }
        else if (_it.kind == K_ContextMenu) {   // an open right-click menu, in screen pixels
        auto* cm = up->GetComponent<UIContextMenu>();
        if (!cm || !cm->IsOpen() || cm->items.empty()) continue;
        uiUseScale(1.0f);
        p.SetClipRect(nullptr);
        const Vec2 mo = cm->Origin(), ms = cm->Size();
        Rect box{(int)mo.x, (int)mo.y, (int)ms.x, (int)ms.y};
        auto col = [](const Color& c) { return RGBA{(std::uint8_t)(c.r * 255), (std::uint8_t)(c.g * 255),
                                                    (std::uint8_t)(c.b * 255), (std::uint8_t)(c.a * 255)}; };
        const RGBA bg = col(cm->background), hv = col(cm->hoverColor), tx = col(cm->textColor), bd = col(cm->borderColor);
        p.SetDrawColor(bg.r, bg.g, bg.b, bg.a);
        p.FillRect(&box);
        for (int i = 0; i < (int)cm->items.size(); ++i) {
            const int iy = (int)(mo.y + i * cm->itemHeight);
            if (cm->items[i] == "-") {   // separator
                Rect sep{box.x + 6, iy + (int)(cm->itemHeight * 0.5f), box.w - 12, 1};
                p.SetDrawColor(bd.r, bd.g, bd.b, bd.a);
                p.FillRect(&sep);
                continue;
            }
            if (i == cm->Hover()) {
                Rect row{box.x + 2, iy + 1, box.w - 4, (int)cm->itemHeight - 2};
                p.SetDrawColor(hv.r, hv.g, hv.b, hv.a);
                p.FillRect(&row);
            }
            DrawUIText(p, cm->items[i], mo.x + 10.0f, iy + (cm->itemHeight - Font8x8::Height * cm->textSize * UITextScale()) * 0.5f,
                       cm->textSize * UITextScale(), tx);
        }
        p.SetDrawColor(bd.r, bd.g, bd.b, bd.a);
        p.DrawRect(&box);
    }
        else if (_it.kind == K_Keybind) {   // keybind fields: action name + bound key
        auto* kb = up->GetComponent<UIKeybindField>();
        if (!kb || !up->active || UIHidden(up.get())) continue;
        const float op = _slot.opacity;
        Vec2 o = UIResolveOrigin(up.get(), (float)w, (float)h);
        enterScroll(up.get(), o);
        auto col = [op](const Color& c, float k = 1.0f) {
            return RGBA{(std::uint8_t)(c.r * 255 * k), (std::uint8_t)(c.g * 255 * k), (std::uint8_t)(c.b * 255 * k),
                        (std::uint8_t)(c.a * 255 * op)};
        };
        const float shade = !kb->interactable ? 0.6f : (kb->IsHovered() ? 1.15f : 1.0f);
        Color bgc = kb->background; bgc.r = std::min(1.0f, bgc.r * shade); bgc.g = std::min(1.0f, bgc.g * shade); bgc.b = std::min(1.0f, bgc.b * shade);
        Rect box{(int)o.x, (int)o.y, (int)kb->size.x, (int)kb->size.y};
        const RGBA b = col(bgc);
        p.SetDrawColor(b.r, b.g, b.b, b.a);
        p.FillRect(&box);
        const float px = kb->textSize * UITextScale(), ty = o.y + (kb->size.y - Font8x8::Height * px) * 0.5f;
        DrawUIText(p, kb->action, o.x + 8.0f, ty, px, col(kb->textColor));
        std::string keyText = kb->IsListening() ? std::string(kb->ListenPrompt()) : "[" + kb->KeyLabel() + "]";
        if (kb->IsListening()) {   // the prompt takes the place of the key on the right, and shortens until it no longer touches the action name
            const float room = kb->size.x - 24.0f - UITextW(kb->action, px);
            if (UITextW(keyText, px) > room) keyText = UITextW("Press key", px) <= room ? "Press key" : (UITextW("...", px) <= room ? "..." : "");
        }
        const RGBA kc = col(kb->IsListening() ? kb->listenColor : kb->keyColor);
        DrawUIText(p, keyText, o.x + kb->size.x - 8.0f - UITextW(keyText, px), ty, px, kc);
        if (kb->IsListening()) {
            p.SetDrawColor(kc.r, kc.g, kc.b, kc.a);
            p.DrawRect(&box);
        }
    }
        else if (_it.kind == K_Tooltip) {   // tooltips (hover hints)
        p.SetClipRect(nullptr);   // overlay — never scroll-clipped
        auto* tt = up->GetComponent<UITooltip>();
        if (!tt || !up->active || !tt->Ready()) continue;
        Vec2 m = uiMouse();
        float px = 2.0f * UITextScale();
        const float ox = tt->offset.x, oy = tt->offset.y;
        auto plain = [&](const std::string& s) {
            std::string out;
            for (std::size_t i = 0; i < s.size();) {
                if (s.compare(i, 3, "<b>") == 0) { i += 3; continue; }
                if (s.compare(i, 4, "</b>") == 0) { i += 4; continue; }
                if (s[i] == '#' && i + 7 <= s.size()) {
                    bool hex = true;
                    for (int k = 1; k <= 6; ++k) {
                        char c = s[i + k];
                        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) { hex = false; break; }
                    }
                    if (hex) { i += 7; continue; }
                }
                out.push_back(s[i++]);
            }
            return out;
        };
        const std::string visible = tt->richText ? plain(tt->text) : tt->text;
        float tw = UITextW(visible, px);
        float th = Font8x8::Height * px;
        const TooltipBox placed = PlaceTooltip(m.x, m.y, tw + 12, th + 10, 0.0f, 0.0f, (float)w, (float)h);
        float bw = placed.w, bh = placed.h, bx = placed.x + ox, by = placed.y + oy;
        if (bx < 4) bx = 4;
        if (by < 4) by = 4;
        if (bx + bw > w - 4) bx = std::max(4.0f, (float)w - 4 - bw);
        if (by + bh > h - 4) by = std::max(4.0f, (float)h - 4 - bh);
        Rect box{(int)bx, (int)by, (int)bw, (int)bh};
        p.SetDrawColor((std::uint8_t)(tt->background.r * 255), (std::uint8_t)(tt->background.g * 255),
                               (std::uint8_t)(tt->background.b * 255), (std::uint8_t)(tt->background.a * 255));
        p.FillRect(&box);
        p.SetDrawColor((std::uint8_t)(tt->borderColor.r * 255), (std::uint8_t)(tt->borderColor.g * 255),
                               (std::uint8_t)(tt->borderColor.b * 255), (std::uint8_t)(tt->borderColor.a * 255));
        p.DrawRect(&box);
        RGBA tc{(std::uint8_t)(tt->textColor.r * 255), (std::uint8_t)(tt->textColor.g * 255),
                     (std::uint8_t)(tt->textColor.b * 255), (std::uint8_t)(tt->textColor.a * 255)};
        if (!tt->richText) {
            DrawUIText(p, tt->text, bx + 6.0f, by + 5.0f, px, tc);
        } else {
            auto nib = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            float x = bx + 6.0f;
            const float y = by + 5.0f;
            RGBA cur = tc;
            bool bold = false;
            const std::string& s = tt->text;
            for (std::size_t i = 0; i < s.size();) {
                if (s.compare(i, 3, "<b>") == 0) { bold = true; i += 3; continue; }
                if (s.compare(i, 4, "</b>") == 0) { bold = false; i += 4; continue; }
                if (s[i] == '#' && i + 7 <= s.size()) {
                    int h[6]; bool hex = true;
                    for (int k = 0; k < 6; ++k) { h[k] = nib(s[i + 1 + k]); if (h[k] < 0) hex = false; }
                    if (hex) {
                        cur = RGBA{(std::uint8_t)(h[0] * 16 + h[1]), (std::uint8_t)(h[2] * 16 + h[3]),
                                   (std::uint8_t)(h[4] * 16 + h[5]), tc.a};
                        i += 7; continue;
                    }
                }
                std::size_t j = i;
                while (j < s.size() && s.compare(j, 3, "<b>") != 0 && s.compare(j, 4, "</b>") != 0 && s[j] != '#') ++j;
                if (j == i) ++j;   // a '#' that is not a colour is ordinary text
                std::string run = s.substr(i, j - i);
                DrawUIText(p, run, x, y, px, cur);
                if (bold) DrawUIText(p, run, x + 1.0f, y, px, cur);
                x += UITextW(run, px);
                i = j;
            }
        }
        }
        else if (DrawMoreUIWidget(_it.kind, up.get(), p, (float)w, (float)h, _slot.opacity)) {
        }
    }   // end single UI draw pass
    uiUseScale(1.0f);
    w = uiFullW; h = uiFullH;
    g_uiDefaultFont = nullptr;   // world text / FPS overlay keep the bitmap font
    g_uiSmoothEdges = true;
    g_uiQuality = UIQuality::Good;
    g_uiDeviceScale = 1.0f;
    p.SetClipRect(nullptr);   // end any scroll clipping
}

} // namespace ui2d
} // namespace okay
