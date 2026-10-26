// The in-game UI pass and its drawing helpers, moved verbatim from the player's main
// loop (player/main.cpp) onto the ui2d::Painter interface. See UIRenderer.hpp.
#include "okay/UI/UIRenderer.hpp"
#include <Okay.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace okay {
namespace ui2d {

namespace {
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
void FillUIShadow(Painter& ren, const Rect& r, UIShape shape, float radius,
                  const Color& color, float softness, float op, int cornerMask) {
    if (softness <= 0.0f) {
        FillUIShape(ren, r, shape, radius, color, color, false, false, op, 0, cornerMask);
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
void MMFillCircle(Painter& r, int cx, int cy, int rad, RGBA col) {
    if (rad < 1) rad = 1;
    r.SetDrawColor(col.r, col.g, col.b, col.a);
    for (int dy = -rad; dy <= rad; ++dy) {
        int dx = (int)std::sqrt((double)(rad * rad - dy * dy));
        r.DrawLine(cx - dx, cy + dy, cx + dx, cy + dy);
    }
}
void MMDrawRing(Painter& r, int cx, int cy, int rad, int width, RGBA col) {
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
void MMFillTriangle(Painter& r, Point a, Point b, Point c, RGBA col) {
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
// A line segment with thickness, drawn as a filled quad (for minimap roads/walls).
void MMThickLine(Painter& r, float x0, float y0, float x1, float y1, float width, RGBA col) {
    float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx*dx + dy*dy);
    if (len < 1e-3f) return;
    float nx = -dy / len * width * 0.5f, ny = dx / len * width * 0.5f;
    Point a{(int)(x0+nx),(int)(y0+ny)}, b{(int)(x1+nx),(int)(y1+ny)},
              c{(int)(x1-nx),(int)(y1-ny)}, d{(int)(x0-nx),(int)(y0-ny)};
    MMFillTriangle(r, a, b, c, col); MMFillTriangle(r, a, c, d, col);
}
// Fill a simple (convex) polygon by fanning triangles from the first vertex.
void MMFillPoly(Painter& r, const std::vector<Point>& pts, RGBA col) {
    for (std::size_t i = 1; i + 1 < pts.size(); ++i)
        MMFillTriangle(r, pts[0], pts[i], pts[i+1], col);
}


// Draw a string with a TTF font (a textured glyph quad per character), tinted by col.
void DrawTtfText(Painter& r, okay::TtfFont* f, const std::string& text,
                 float ox, float oy, float px, RGBA col, float letterSp, float lineSp) {
    Tex tex = r.FontAtlas(f);
    if (!tex) return;
    r.SetTextureColorMod(tex, col.r, col.g, col.b);
    r.SetTextureAlphaMod(tex, col.a);
    const float s = px * (float)okay::Font8x8::Height / f->BakeHeight();
    const float baseline = f->Ascent((float)okay::Font8x8::Height) * px;
    float penX = ox, penY = oy;
    for (char ch : text) {
        if (ch == '\n') { penY += f->LineHeight((float)okay::Font8x8::Height) * px + lineSp * px; penX = ox; continue; }
        const okay::TtfFont::Glyph* g = f->Get(ch);
        if (!g) { penX += px * 4.0f; continue; }
        int gw = g->x1 - g->x0, gh = g->y1 - g->y0;
        if (gw > 0 && gh > 0) {
            Rect src{g->x0, g->y0, gw, gh};
            FRect dst{penX + g->xoff * s, penY + baseline + g->yoff * s, gw * s, gh * s};
            r.CopyF(tex, &src, &dst);
        }
        penX += g->xadvance * s + letterSp * px;
    }
}

// Scene-wide default UI font (from Scene::uiFont): DrawUIText falls back to it when no
// explicit font is given. Set around the screen-UI pass and cleared after, so world
// text without its own font keeps the bitmap font (matching the editor preview).
static TtfFont* g_uiDefaultFont = nullptr;
void SetUIDefaultFont(TtfFont* f) { g_uiDefaultFont = f; }
TtfFont* UIDefaultFont() { return g_uiDefaultFont; }
// Width of `text` in screen pixels at font-pixel size `px`, in the font DrawUIText will
// use (the scene's UI font during the UI pass, else the 8x8 bitmap font).
float UITextW(const std::string& text, float px) {
    if (g_uiDefaultFont && g_uiDefaultFont->Valid()) return g_uiDefaultFont->Measure(text.c_str(), (float)Font8x8::Height) * px;
    return (float)text.size() * (Font8x8::Width + 1) * px;
}

// Draw a string with the built-in 8x8 font as filled rects, top-left at (ox, oy)
// in screen pixels, each font pixel `px` screen pixels wide. When `ttf` is loaded,
// the TTF atlas is used instead (same sizing space).
void DrawUIText(Painter& r, const std::string& text, float ox, float oy,
                float px, RGBA col, float letterSp, float lineSp,
                bool italic, bool gradient, RGBA col2, TtfFont* ttf) {
    if (!ttf) ttf = g_uiDefaultFont;   // scene-wide default UI font (set during the UI pass)
    if (px < 0.15f) px = 0.15f;   // a smooth font scales below one font pixel; the 8x8 font can't
    if (ttf && ttf->Valid()) { DrawTtfText(r, ttf, text, ox, oy, px, col, letterSp, lineSp); return; }
    if (px < 1.0f) px = 1.0f;
    if (!gradient) r.SetDrawColor(col.r, col.g, col.b, col.a);
    const float slant = italic ? 0.30f : 0.0f;   // px shift per row up from the baseline
    float cx = ox;
    for (char ch : text) {
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
    }
    for (UIItem& it : c.items) {
        const UISlot& sl = c.slots[it.slot];
        it.canvasOrder = sl.canvas ? sl.canvas->sortOrder : 0;
        const int o = objs[sl.index]->uiDrawOrder;
        it.eff = o != 0 ? o : it.layer;
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

void RenderSceneUI(Scene& scene, Painter& p, int w, int h, const UIRenderOptions& opt) {
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
        if (std::fabs(sc - uiScaleCur) < 1e-4f) return;
        uiScaleCur = sc;
        p.SetClipRect(nullptr);
        p.SetScale(sc, sc);
        w = (int)std::lround(uiFullW / sc); h = (int)std::lround(uiFullH / sc);
    };
    auto uiMouse = [&]() { Vec2 m = Input::MousePosition(); return Vec2{m.x / uiScaleCur, m.y / uiScaleCur}; };
    for (const UIItem& _it : cache.items) {
        const UISlot& _slot = cache.slots[_it.slot];
        const auto& up = scene.Objects()[_slot.index];
        // Every UI object carries a focus-ring item; skip it outright unless focused.
        if (_it.kind == K_FocusRing && (!up->active || !IsUIFocused(up.get()))) continue;
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
                // Rounded / circle / pill pictures (avatars, cards): the texture is
                // drawn on the shape's outline as a triangle fan, so the corners are cut.
                const bool rounded = (im->shape == UIShape::Rounded && im->cornerRadius > 0.5f) ||
                                     im->shape == UIShape::Circle || im->shape == UIShape::Pill;
                if (rounded && dst.w > 2 && dst.h > 2) {
                    std::vector<FPoint> pts;
                    const float x0 = (float)dst.x, y0 = (float)dst.y, ww = (float)dst.w, hh = (float)dst.h;
                    if (im->shape == UIShape::Circle) {
                        for (int k = 0; k < 48; ++k) {
                            const float a = k / 48.0f * 6.2831853f;
                            pts.push_back({x0 + ww * 0.5f + std::cos(a) * ww * 0.5f, y0 + hh * 0.5f + std::sin(a) * hh * 0.5f});
                        }
                    } else {
                        float rad = im->shape == UIShape::Pill ? std::min(ww, hh) * 0.5f : std::min(im->cornerRadius, std::min(ww, hh) * 0.5f);
                        const float cx[4] = {x0 + ww - rad, x0 + ww - rad, x0 + rad, x0 + rad};
                        const float cy[4] = {y0 + rad, y0 + hh - rad, y0 + hh - rad, y0 + rad};
                        const int bits[4] = {UICornerTR, UICornerBR, UICornerBL, UICornerTL};
                        for (int c = 0; c < 4; ++c) {
                            const float r0 = (im->cornerMask & bits[c]) ? rad : 0.0f;
                            const float ccx = r0 > 0 ? cx[c] : (c == 0 || c == 1 ? x0 + ww : x0);
                            const float ccy = r0 > 0 ? cy[c] : (c == 1 || c == 2 ? y0 + hh : y0);
                            for (int k = 0; k <= 8; ++k) {
                                const float a = (-90.0f + c * 90.0f + k * 11.25f) * 0.01745329f;
                                pts.push_back({ccx + std::cos(a) * r0, ccy + std::sin(a) * r0});
                            }
                        }
                    }
                    const RGBA vc{255, 255, 255, 255};
                    std::vector<Vertex> vx;
                    vx.push_back({{x0 + ww * 0.5f, y0 + hh * 0.5f}, vc, {0.5f, 0.5f}});
                    for (const auto& q : pts) {
                        float u = (q.x - x0) / ww, v = (q.y - y0) / hh;
                        if (im->flipX) u = 1.0f - u;
                        if (im->flipY) v = 1.0f - v;
                        vx.push_back({q, vc, {u, v}});
                    }
                    std::vector<int> ix;
                    const int n = (int)pts.size();
                    for (int k = 0; k < n; ++k) { ix.push_back(0); ix.push_back(1 + k); ix.push_back(1 + (k + 1) % n); }
                    p.Geometry(tex, vx.data(), (int)vx.size(), ix.data(), (int)ix.size());
                } else {
                    p.CopyEx(tex, nullptr, &dst, 0.0, nullptr, flip);
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
        Vec2 o = UIResolveOrigin(up.get(), (float)w, (float)h);
        Vec2 psz = UIResolveSize(up.get(), (float)w, (float)h);   // honors stretch anchors
        enterScroll(up.get(), o);
        Rect r{(int)o.x, (int)o.y, (int)psz.x, (int)psz.y};
        // A backdrop far larger than the screen (e.g. a pause-menu dim) covers the
        // whole window — clamp it so it tiles exactly the screen, not huge coords.
        if (psz.x > w * 1.5f && psz.y > h * 1.5f) r = Rect{0, 0, w, h};
        bool gh; int gdiag; GradientParams(pn->gradientDir, gh, gdiag);
        int cm = pn->cornerMask;                        // per-corner rounding
        if (pn->shadow) {                               // drop shadow behind (same shape)
            Rect sh{r.x + (int)pn->shadowOffset.x, r.y + (int)pn->shadowOffset.y, r.w, r.h};
            FillUIShadow(p, sh, pn->shape, pn->cornerRadius,
                         pn->shadowColor, pn->shadowSoftness, op, cm);
        }
        if (pn->outlineWidth > 0.0f) {                  // outer keyline/glow ring
            int ow = (int)pn->outlineWidth;
            Rect outer{r.x - ow, r.y - ow, r.w + 2 * ow, r.h + 2 * ow};
            FillUIShape(p, outer, pn->shape, pn->cornerRadius + ow,
                        pn->outlineColor, pn->outlineColor, false, false, op, 0, cm);
        }
        if (pn->borderWidth > 0.0f) {                   // fill, then the border ring over it
            FillUIShape(p, r, pn->shape, pn->cornerRadius,
                        pn->color, pn->colorBottom, pn->useGradient, gh, op, gdiag, cm);
            if (pn->topHighlight) {                     // inner glass sheen (top-down fade)
                Color hb = pn->highlightColor; hb.a = 0.0f;
                FillUIShape(p, r, pn->shape, pn->cornerRadius,
                            pn->highlightColor, hb, true, false, op, 0, cm);
            }
            FillUIRing(p, r, pn->shape, pn->cornerRadius, pn->borderColor,
                       std::max(1, (int)(pn->borderWidth + 0.5f)), op, cm);
        } else {
            FillUIShape(p, r, pn->shape, pn->cornerRadius,
                        pn->color, pn->colorBottom, pn->useGradient, gh, op, gdiag, cm);
            if (pn->topHighlight) {                     // inner glass sheen (top-down fade)
                Color hb = pn->highlightColor; hb.a = 0.0f;
                FillUIShape(p, r, pn->shape, pn->cornerRadius,
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
        if (sv) {
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
        Vec2 o = UIResolveOrigin(up.get(), (float)w, (float)h);
        enterScroll(up.get(), o);
        Rect bg{(int)o.x, (int)o.y, (int)pb->size.x, (int)pb->size.y};
        FillUIShape(p, bg, pb->shape, pb->cornerRadius,
                    pb->background, pb->background, false, false, op);
        float pfox, pfoy, pfw, pfh; pb->FillRect(pb->size.x, pb->size.y, pfox, pfoy, pfw, pfh);
        Rect fl{(int)(o.x + pfox), (int)(o.y + pfoy), (int)pfw, (int)pfh};
        bool horiz = pb->fillDir == UIProgressBar::FillDir::LeftRight ||
                     pb->fillDir == UIProgressBar::FillDir::RightLeft;
        FillUIShape(p, fl, pb->shape, pb->cornerRadius,
                    pb->fill, pb->fillEnd, pb->gradientFill, horiz, op);
        if (pb->showPercent) {
            char pct[8]; std::snprintf(pct, sizeof(pct), "%d%%", (int)(pb->Fraction() * 100.0f + 0.5f));
            float px = 2.0f;
            float tw = UITextW(pct, px);
            RGBA tc{(std::uint8_t)(pb->textColor.r * 255), (std::uint8_t)(pb->textColor.g * 255),
                         (std::uint8_t)(pb->textColor.b * 255), (std::uint8_t)(pb->textColor.a * 255 * op)};
            DrawUIText(p, pct, o.x + (pb->size.x - tw) * 0.5f,
                     o.y + (pb->size.y - Font8x8::Height * px) * 0.5f, px, tc);
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
        Rect bg{(int)o.x, (int)o.y, (int)sl->size.x, (int)sl->size.y};
        FillUIShape(p, bg, sl->trackShape, sl->cornerRadius,
                    sl->background, sl->background, false, false, op);
        float f = sl->Fraction();
        Rect fl, kn;
        if (sl->vertical) {
            fl = {(int)o.x, (int)(o.y + sl->size.y * (1.0f - f)), (int)sl->size.x, (int)(sl->size.y * f)};
            int kh = (int)(sl->size.x * sl->knobSize);
            kn = {(int)o.x - 2, (int)(o.y + sl->size.y * (1.0f - f)) - kh / 2, (int)sl->size.x + 4, kh};
        } else {
            fl = {(int)o.x, (int)o.y, (int)(sl->size.x * f), (int)sl->size.y};
            int kw = (int)(sl->size.y * sl->knobSize);
            kn = {(int)(o.x + sl->size.x * f) - kw / 2, (int)o.y - 2, kw, (int)sl->size.y + 4};
        }
        FillUIShape(p, fl, sl->trackShape, sl->cornerRadius,
                    sl->fill, sl->fill, false, false, op);
        // The handle: a circle when roundKnob, else a rounded tab.
        FillUIShape(p, kn, sl->roundKnob ? UIShape::Circle : UIShape::Rounded,
                    sl->cornerRadius, sl->knob, sl->knob, false, false, op);
        if (sl->showValue) {
            char vbuf[16]; std::snprintf(vbuf, sizeof(vbuf), "%.2f", sl->value);
            float px = 2.0f;
            RGBA tc{(std::uint8_t)(sl->textColor.r * 255), (std::uint8_t)(sl->textColor.g * 255),
                         (std::uint8_t)(sl->textColor.b * 255), (std::uint8_t)(sl->textColor.a * 255 * op)};
            DrawUIText(p, vbuf, o.x + sl->size.x + 8.0f,
                     o.y + (sl->size.y - Font8x8::Height * px) * 0.5f, px, tc);
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
        float px = 2.0f;
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
        if (btn->hoverScale != 1.0f && (btn->IsHovered() || btn->IsFocused())) {
            int gx = (int)(bsz.x * (btn->hoverScale - 1.0f) * 0.5f);
            int gy = (int)(bsz.y * (btn->hoverScale - 1.0f) * 0.5f);
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
        if (isz > 0.0f) {
            Tex itex = p.LoadTexture(btn->icon);
            float ix = btn->iconRight ? (o.x + bsz.x - isz - 8.0f * bk) : (o.x + 8.0f * bk);
            Rect ir{(int)ix, (int)(o.y + (bsz.y - isz) * 0.5f + shift), (int)isz, (int)isz};
            if (itex) { p.SetTextureColorMod(itex, 255, 255, 255); p.SetTextureAlphaMod(itex, 255);
                        p.Copy(itex, nullptr, &ir); }
            else p.MissingTexture(btn->icon, ir);
        }
        // Center the label at the button's font scale, within the area beside
        // the icon. Skip the built-in label when a child Text object provides it
        // — that child draws itself in the text pass.
        if (!UIButtonTextChild(up.get())) {
            float px = btn->fontScale * bk;
            okay::TtfFont* fnt = btn->fontPath.empty() ? g_uiDefaultFont : okay::GetFont(btn->fontPath);
            float tw = fnt ? fnt->Measure(btn->label.c_str(), (float)Font8x8::Height) * px
                           : UITextW(btn->label, px);
            float left  = o.x + (isz > 0.0f && !btn->iconRight ? isz + 12.0f * bk : 0.0f);
            float right = o.x + bsz.x - (isz > 0.0f && btn->iconRight ? isz + 12.0f * bk : 0.0f);
            float tx = left + ((right - left) - tw) * 0.5f;
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
        if (tr->background) {                       // label background box
            Vec2 b = UIResolveOrigin(up.get(), (float)w, (float)h);   // parent-relative box
            Rect br{(int)b.x, (int)b.y, (int)(tr->size.x * tk), (int)(tr->size.y * tk)};
            p.SetDrawColor((std::uint8_t)(tr->backgroundColor.r * 255),
                                   (std::uint8_t)(tr->backgroundColor.g * 255), (std::uint8_t)(tr->backgroundColor.b * 255),
                                   (std::uint8_t)(tr->backgroundColor.a * 255 * op));
            p.FillRect(&br);
        }
        Vec2 o = tWorld ? UIResolveOrigin(up.get(), (float)w, (float)h)
                        : UITextDrawOrigin(up.get(), tr, (float)w, (float)h);   // parent-relative + align
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
        DrawUIText(p, disp, o.x, o.y, ps, col, ls, lp, it, tr->gradient, c2, fnt);
        if (tr->bold) DrawUIText(p, disp, o.x + fo, o.y, ps, col, ls, lp, it, tr->gradient, c2, fnt);
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
            for (int i = 0; i < (int)dd->options.size(); ++i) {
                Rect orow{(int)o.x, (int)(top + i * dd->size.y), (int)dd->size.x, (int)dd->size.y};
                const Color& rc = (i == dd->HoveredOption()) ? dd->hoverColor : dd->listColor;
                p.SetDrawColor((std::uint8_t)(rc.r * 255), (std::uint8_t)(rc.g * 255),
                                       (std::uint8_t)(rc.b * 255), (std::uint8_t)(rc.a * 255));
                p.FillRect(&orow);
                DrawUIText(p, dd->options[i], o.x + 8.0f,
                         orow.y + (dd->size.y - Font8x8::Height * px) * 0.5f, px, tc);
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
        std::string full = empty ? in->placeholder : in->DisplayText();
        const Color& txc = empty ? in->placeholderColor : in->textColor;
        // Horizontal scroll: show the tail that fits so the caret stays visible.
        // Horizontal scroll: drop leading characters until the tail fits.
        std::string shown = full;
        while (shown.size() > 1 && UITextW(shown, px) > in->size.x - pad * 2) shown.erase(0, 1);
        float ty = o.y + (in->size.y - Font8x8::Height * px) * 0.5f;
        RGBA tc{(std::uint8_t)(txc.r * 255), (std::uint8_t)(txc.g * 255), (std::uint8_t)(txc.b * 255), (std::uint8_t)(txc.a * 255)};
        DrawUIText(p, shown, o.x + pad, ty, px, tc);
        if (in->focused && in->CaretVisible()) {        // blinking caret after visible text
            int cx = (int)(o.x + pad + UITextW(shown, px));
            p.SetDrawColor(255, 255, 255, 220);
            p.DrawLine(cx, (int)ty, cx, (int)(ty + Font8x8::Height * px));
        }
    }
        else if (_it.kind == K_FocusRing) {   // keyboard/gamepad focus ring
        if (!up->active || !IsUIFocused(up.get())) continue;
        p.SetClipRect(nullptr);   // overlay — never scroll-clipped
        UIRect r = GetUIRect(up.get());
        if (!r.valid || !r.position) continue;
        Vec2 o, sz;   // where the widget actually is (parents, canvases, world space)
        if (!GetUIScreenRect(up.get(), (float)w, (float)h, o, sz)) { o = UIResolveOrigin(up.get(), (float)w, (float)h); sz = r.size; }
        const float x0 = o.x - 3.0f, y0 = o.y - 3.0f, x1 = o.x + sz.x + 3.0f, y1 = o.y + sz.y + 3.0f;
        const RGBA fc{120, 182, 255, 230};   // launcher accent, lightened
        MMThickLine(p, x0, y0, x1, y0, 2.0f, fc); MMThickLine(p, x1, y0, x1, y1, 2.0f, fc);
        MMThickLine(p, x1, y1, x0, y1, 2.0f, fc); MMThickLine(p, x0, y1, x0, y0, 2.0f, fc);
    }
        else if (_it.kind == K_Tooltip) {   // tooltips (hover hints)
        p.SetClipRect(nullptr);   // overlay — never scroll-clipped
        auto* tt = up->GetComponent<UITooltip>();
        if (!tt || !up->active || !tt->Ready()) continue;
        Vec2 m = uiMouse();
        float px = 2.0f;
        float tw = UITextW(tt->text, px);
        float th = Font8x8::Height * px;
        Rect box{(int)(m.x + 14), (int)(m.y + 14), (int)(tw + 12), (int)(th + 10)};
        p.SetDrawColor((std::uint8_t)(tt->background.r * 255), (std::uint8_t)(tt->background.g * 255),
                               (std::uint8_t)(tt->background.b * 255), (std::uint8_t)(tt->background.a * 255));
        p.FillRect(&box);
        p.SetDrawColor((std::uint8_t)(tt->borderColor.r * 255), (std::uint8_t)(tt->borderColor.g * 255),
                               (std::uint8_t)(tt->borderColor.b * 255), (std::uint8_t)(tt->borderColor.a * 255));
        p.DrawRect(&box);
        RGBA tc{(std::uint8_t)(tt->textColor.r * 255), (std::uint8_t)(tt->textColor.g * 255),
                     (std::uint8_t)(tt->textColor.b * 255), (std::uint8_t)(tt->textColor.a * 255)};
        DrawUIText(p, tt->text, m.x + 20, m.y + 19, px, tc);
        }
    }   // end single UI draw pass
    uiUseScale(1.0f);
    w = uiFullW; h = uiFullH;
    g_uiDefaultFont = nullptr;   // world text / FPS overlay keep the bitmap font
    p.SetClipRect(nullptr);   // end any scroll clipping
}

} // namespace ui2d
} // namespace okay
