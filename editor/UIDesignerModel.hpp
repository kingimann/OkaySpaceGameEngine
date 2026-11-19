#pragma once
// The UI designer's logic, with no ImGui and no scene, so the tests run it as it is (docs/ui_designer_upgrade.md).
// Written from our own design, from general ideas of vector design tools: a pan and zoom view, rulers, guides,
// snapping, resize handles, align and distribute, layer order and visibility, a 3x3 anchor picker and a preview at
// other screen sizes. Everything here works on plain rectangles in canvas pixels; the window files turn a drag or
// a click into one call and apply the answer to the selected widgets (as one undo step).
//
//   View          zoom about the cursor, pan, fit to window, canvas <-> screen
//   rulers        nice tick steps for the strips along the top and left
//   SnapMove      where a dragged rectangle jumps to: guides, other widgets, canvas edges and middle, grid
//   Resize        the new rectangle for each of the eight handles (Shift keeps the shape, Alt resizes from the middle)
//   Nudge         arrow keys: 1 pixel, Shift = grid step
//   Align / Distribute, Reorder (z-order), layers (hidden / locked / pick), UniqueName
//   anchor picker the 3x3 grid and three stretch buttons, and re-anchoring without moving the widget
//   responsive    common screen sizes, and where an anchored widget lands on each
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <utility>
#include <string>
#include <vector>
#include "okay/Components/UIAnchor.hpp"

namespace oked::uid {

struct Rect { float x = 0, y = 0, w = 0, h = 0; float R() const { return x + w; } float B() const { return y + h; } };

// ---- view -------------------------------------------------------------------------------------------------------
constexpr float kMinZoom = 0.1f, kMaxZoom = 8.0f;
struct View {
    float zoom = 1.0f, panX = 0.0f, panY = 0.0f;   // screen = pan + canvas * zoom
    void ToScreen(float x, float y, float& sx, float& sy) const { sx = panX + x * zoom; sy = panY + y * zoom; }
    void ToCanvas(float sx, float sy, float& x, float& y) const { x = (sx - panX) / zoom; y = (sy - panY) / zoom; }
    void Pan(float dx, float dy) { panX += dx; panY += dy; }
    /// Zoom by a factor; the canvas point under (cx, cy) stays under it.
    void ZoomAt(float cx, float cy, float factor) {
        const float nz = std::min(kMaxZoom, std::max(kMinZoom, zoom * factor));
        const float k = nz / zoom;
        panX = cx - (cx - panX) * k; panY = cy - (cy - panY) * k; zoom = nz;
    }
    /// Show the whole canvas in a view of viewW x viewH, centred, with a margin all round.
    void Fit(float viewW, float viewH, float canvasW, float canvasH, float margin) {
        const float z = std::min((viewW - 2 * margin) / canvasW, (viewH - 2 * margin) / canvasH);
        zoom = std::min(kMaxZoom, std::max(kMinZoom, z));
        panX = (viewW - canvasW * zoom) * 0.5f; panY = (viewH - canvasH * zoom) * 0.5f;
    }
};

// ---- rulers -----------------------------------------------------------------------------------------------------
/// The smallest 1 / 2 / 5 x 10^n step that is at least 40 screen pixels apart at this zoom.
inline float RulerStep(float zoom) {
    const float want = 40.0f / std::max(zoom, 1e-4f);
    float p = std::pow(10.0f, std::floor(std::log10(want)));
    for (float f : {1.0f, 2.0f, 5.0f, 10.0f}) if (f * p * zoom >= 40.0f - 0.001f) return f * p;
    return 10.0f * p;
}
struct Tick { float pos; bool major; };
/// Ticks from `from` to `to` (canvas pixels): a major one every step and a minor one halfway.
inline std::vector<Tick> RulerTicks(float from, float to, float zoom) {
    std::vector<Tick> out; const float step = RulerStep(zoom), half = step >= 2.0f ? step * 0.5f : step;
    for (long long i = (long long)std::ceil(from / half); (float)i * half <= to; ++i) {
        const float pos = (float)i * half;
        const bool major = std::fabs(std::fmod(pos, step)) < half * 0.01f || half == step;
        out.push_back({pos, major});
    }
    return out;
}

// ---- guides and snapping ----------------------------------------------------------------------------------------
struct Guide { bool vertical = true; float pos = 0; };   // vertical: a line at x = pos
struct SnapOptions { bool enabled = true; float threshold = 6.0f; float gridStep = 0.0f; };
struct SnapResult { float dx = 0, dy = 0; std::vector<Guide> lines; };   // lines to draw while snapped

namespace detail {
inline void Best(const float mine[3], const std::vector<float>& targets, float& delta, float& line, bool& found, float thr) {
    float bestAbs = found ? std::fabs(delta) : thr + 1;
    for (float t : targets) for (int i = 0; i < 3; ++i) {
        const float d = t - mine[i];
        if (std::fabs(d) <= thr && std::fabs(d) < bestAbs) { bestAbs = std::fabs(d); delta = d; line = t; found = true; }
    }
}
}  // namespace detail

/// Where the moving rectangle jumps to. Its left / middle / right (and top / middle / bottom) are matched to the same
/// of every other widget, every guide, the canvas and, last, the grid. The nearest one within the threshold wins.
inline SnapResult SnapMove(const Rect& m, const std::vector<Rect>& others, const std::vector<Guide>& guides,
                           const Rect& canvas, const SnapOptions& o) {
    SnapResult r; if (!o.enabled) return r;
    std::vector<float> tx, ty;
    auto add3 = [](std::vector<float>& v, float a, float b) { v.push_back(a); v.push_back((a + b) * 0.5f); v.push_back(b); };
    for (const Rect& s : others) { add3(tx, s.x, s.R()); add3(ty, s.y, s.B()); }
    for (const Guide& g : guides) (g.vertical ? tx : ty).push_back(g.pos);
    add3(tx, canvas.x, canvas.R()); add3(ty, canvas.y, canvas.B());
    const float mx[3] = {m.x, m.x + m.w * 0.5f, m.R()}, my[3] = {m.y, m.y + m.h * 0.5f, m.B()};
    float dx = 0, dy = 0, lx = 0, ly = 0; bool fx = false, fy = false;
    detail::Best(mx, tx, dx, lx, fx, o.threshold); detail::Best(my, ty, dy, ly, fy, o.threshold);
    if (o.gridStep > 0) {
        std::vector<float> gx, gy;   // the nearest grid line to each of the three
        for (int i = 0; i < 3; ++i) { gx.push_back(std::round(mx[i] / o.gridStep) * o.gridStep); gy.push_back(std::round(my[i] / o.gridStep) * o.gridStep); }
        detail::Best(mx, gx, dx, lx, fx, o.threshold); detail::Best(my, gy, dy, ly, fy, o.threshold);
    }
    if (fx) { r.dx = dx; r.lines.push_back({true, lx}); }
    if (fy) { r.dy = dy; r.lines.push_back({false, ly}); }
    return r;
}

// ---- resize handles ---------------------------------------------------------------------------------------------
enum class Handle { NW, N, NE, E, SE, S, SW, W };
constexpr float kMinSize = 4.0f;
/// The rectangle after dragging `h` by (dx, dy). keepAspect (corners) keeps width : height; fromCenter resizes both
/// sides at once. It never gets smaller than kMinSize, and never flips over.
inline Rect Resize(const Rect& b, Handle h, float dx, float dy, bool keepAspect, bool fromCenter) {
    const bool west = h == Handle::NW || h == Handle::W || h == Handle::SW;
    const bool east = h == Handle::NE || h == Handle::E || h == Handle::SE;
    const bool north = h == Handle::NW || h == Handle::N || h == Handle::NE;
    const bool south = h == Handle::SW || h == Handle::S || h == Handle::SE;
    float l = b.x, t = b.y, r = b.R(), bt = b.B();
    if (east) { r += dx; if (fromCenter) l -= dx; }
    if (west) { l += dx; if (fromCenter) r -= dx; }
    if (south) { bt += dy; if (fromCenter) t -= dy; }
    if (north) { t += dy; if (fromCenter) bt -= dy; }
    const bool corner = (west || east) && (north || south);
    if (keepAspect && corner && b.h > 0) {
        const float aspect = b.w / b.h;
        float w = std::max(kMinSize, r - l), hh = w / aspect;
        if (fromCenter) { const float cx = b.x + b.w * 0.5f, cy = b.y + b.h * 0.5f; l = cx - w * 0.5f; r = cx + w * 0.5f; t = cy - hh * 0.5f; bt = cy + hh * 0.5f; }
        else { if (east) r = l + w; else l = r - w; if (south) bt = t + hh; else t = bt - hh; }
    }
    if (r - l < kMinSize) { if (west && !east) l = r - kMinSize; else r = l + kMinSize; }
    if (bt - t < kMinSize) { if (north && !south) t = bt - kMinSize; else bt = t + kMinSize; }
    return {l, t, r - l, bt - t};
}

// ---- the editor's drag maths (moved here from main.cpp unchanged, so a test can pin it) --------------------------
/// Nearest candidate to one dragged edge within `thr`: returns it (else v) and sets guide / hit.
inline float SnapEdge(float v, const std::vector<float>& cs, float thr, float& guide, bool& hit) {
    float best = thr, res = v;
    for (float c : cs) { float d = v > c ? v - c : c - v; if (d < best) { best = d; res = c; guide = c; hit = true; } }
    return res;
}
/// The shift that lines the nearest of lo / mid / hi up with a candidate (0 when none is within `thr`).
inline float BestSnap(float lo, float mid, float hi, const std::vector<float>& cands, float thr, float& guide) {
    float best = thr, adj = 0.0f; bool found = false;
    for (float m : {lo, mid, hi})
        for (float c : cands) { float d = m > c ? m - c : c - m; if (d < best) { best = d; adj = c - m; guide = c; found = true; } }
    return found ? adj : 0.0f;
}
/// An edge clinging to its parent's two edges (the nearer one within `thr`).
inline float ParentEdgeSnap(float v, float lo, float hi, float thr, float& guide, bool& hit) {
    float dl = v > lo ? v - lo : lo - v, dh = v > hi ? v - hi : hi - v;
    if (dl < thr && dl <= dh) { guide = lo; hit = true; return lo; }
    if (dh < thr)             { guide = hi; hit = true; return hi; }
    return v;
}
/// Shift keeps the original shape, Alt mirrors each dragged edge about the original middle. Edges in/out.
inline void ConstrainResize(float& l, float& t, float& rr, float& bb, float ox, float oy, float sw, float sh,
                            bool left, bool right, bool top, bool bottom, bool shift, bool alt) {
    const float cx0 = ox + sw * 0.5f, cy0 = oy + sh * 0.5f;
    if (shift && sw > 1e-3f && sh > 1e-3f) {
        float aspect0 = sw / sh;
        if ((left || right) && (top || bottom)) { float newH = (rr - l) / aspect0; if (top) t = bb - newH; else bb = t + newH; }
        else if (left || right) { float newH = (rr - l) / aspect0; t = cy0 - newH * 0.5f; bb = cy0 + newH * 0.5f; }
        else if (top || bottom) { float newW = (bb - t) * aspect0; l = cx0 - newW * 0.5f; rr = cx0 + newW * 0.5f; }
    }
    if (alt) {
        if (left)   rr = 2.0f * cx0 - l;
        if (right)  l  = 2.0f * cx0 - rr;
        if (top)    bb = 2.0f * cy0 - t;
        if (bottom) t  = 2.0f * cy0 - bb;
    }
}

// ---- nudge ------------------------------------------------------------------------------------------------------
/// Arrow keys: (ax, ay) is -1, 0 or 1 per axis. One pixel; with Shift the grid step (10 when there is no grid).
inline bool Nudge(int ax, int ay, bool shift, float gridStep, float& dx, float& dy) {
    if (!ax && !ay) return false;
    const float s = shift ? (gridStep > 0 ? gridStep : 10.0f) : 1.0f;
    dx = ax * s; dy = ay * s; return true;
}

// ---- align and distribute ---------------------------------------------------------------------------------------
enum class AlignMode { Left, CenterH, Right, Top, CenterV, Bottom };
inline Rect Bounds(const std::vector<Rect>& v) {
    if (v.empty()) return {};
    float l = v[0].x, t = v[0].y, r = v[0].R(), b = v[0].B();
    for (const Rect& q : v) { l = std::min(l, q.x); t = std::min(t, q.y); r = std::max(r, q.R()); b = std::max(b, q.B()); }
    return {l, t, r - l, b - t};
}
/// Line every rectangle up against `to` (the selection's bounds, or the canvas for one widget).
inline std::vector<Rect> AlignTo(std::vector<Rect> v, AlignMode m, const Rect& to) {
    for (Rect& q : v) switch (m) {
        case AlignMode::Left: q.x = to.x; break;
        case AlignMode::CenterH: q.x = to.x + (to.w - q.w) * 0.5f; break;
        case AlignMode::Right: q.x = to.R() - q.w; break;
        case AlignMode::Top: q.y = to.y; break;
        case AlignMode::CenterV: q.y = to.y + (to.h - q.h) * 0.5f; break;
        case AlignMode::Bottom: q.y = to.B() - q.h; break;
    }
    return v;
}
inline std::vector<Rect> Align(const std::vector<Rect>& v, AlignMode m) { return AlignTo(v, m, Bounds(v)); }
/// Equal gaps between three or more rectangles; the two outer ones stay. The result is in the input order.
inline std::vector<Rect> Distribute(std::vector<Rect> v, bool horizontal) {
    const size_t n = v.size(); if (n < 3) return v;
    std::vector<size_t> idx(n); for (size_t i = 0; i < n; ++i) idx[i] = i;
    std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return horizontal ? v[a].x < v[b].x : v[a].y < v[b].y; });
    const Rect& first = v[idx.front()]; const Rect& last = v[idx.back()];
    float used = 0; for (const Rect& q : v) used += horizontal ? q.w : q.h;
    const float span = horizontal ? last.R() - first.x : last.B() - first.y;
    const float gap = (span - used) / float(n - 1);
    float cur = horizontal ? first.x + first.w + gap : first.y + first.h + gap;
    for (size_t k = 1; k + 1 < n; ++k) {
        Rect& q = v[idx[k]];
        if (horizontal) { q.x = cur; cur += q.w + gap; } else { q.y = cur; cur += q.h + gap; }
    }
    return v;
}

// ---- z-order ----------------------------------------------------------------------------------------------------
enum class ZOp { ToFront, ToBack, Forward, Backward };
/// `order` lists ids from back to front; the selected ids move as one group, keeping their own order.
inline std::vector<int> Reorder(std::vector<int> order, const std::vector<int>& sel, ZOp op) {
    auto isSel = [&](int id) { return std::find(sel.begin(), sel.end(), id) != sel.end(); };
    const int n = (int)order.size();
    if (op == ZOp::ToFront || op == ZOp::ToBack) {
        std::vector<int> a, b; for (int id : order) (isSel(id) ? a : b).push_back(id);
        std::vector<int> out = op == ZOp::ToFront ? b : a; const auto& tail = op == ZOp::ToFront ? a : b;
        out.insert(out.end(), tail.begin(), tail.end()); return out;
    }
    if (op == ZOp::Forward) { for (int i = n - 2; i >= 0; --i) if (isSel(order[i]) && !isSel(order[i + 1])) std::swap(order[i], order[i + 1]); }
    else { for (int i = 1; i < n; ++i) if (isSel(order[i]) && !isSel(order[i - 1])) std::swap(order[i], order[i - 1]); }
    return order;
}

// ---- layers -----------------------------------------------------------------------------------------------------
struct Layer { int id = 0; Rect r; bool hidden = false, locked = false; };
inline bool IsDrawn(const Layer& l) { return !l.hidden; }
/// The widget under a click: the topmost one that is shown and not locked. `layers` is back to front. -1 = none.
inline int PickTopmost(const std::vector<Layer>& layers, float x, float y) {
    for (int i = (int)layers.size() - 1; i >= 0; --i) {
        const Layer& l = layers[i];
        if (l.hidden || l.locked) continue;
        if (x >= l.r.x && y >= l.r.y && x <= l.r.R() && y <= l.r.B()) return l.id;
    }
    return -1;
}
/// Box select: every shown, unlocked widget that is fully inside the box.
inline std::vector<int> PickInRect(const std::vector<Layer>& layers, const Rect& box) {
    std::vector<int> out;
    for (const Layer& l : layers)
        if (!l.hidden && !l.locked && l.r.x >= box.x && l.r.y >= box.y && l.r.R() <= box.R() && l.r.B() <= box.B()) out.push_back(l.id);
    return out;
}
/// "Button", then "Button 2", "Button 3" ... (for duplicate, paste and group).
inline std::string UniqueName(const std::vector<std::string>& taken, const std::string& base) {
    auto has = [&](const std::string& s) { return std::find(taken.begin(), taken.end(), s) != taken.end(); };
    if (!has(base)) return base;
    for (int i = 2;; ++i) { std::string s = base + " " + std::to_string(i); if (!has(s)) return s; }
}

// ---- anchor picker ----------------------------------------------------------------------------------------------
inline okay::UIAnchor AnchorFromCell(int col, int row) { return (okay::UIAnchor)(std::min(2, std::max(0, row)) * 3 + std::min(2, std::max(0, col))); }
inline bool CellOfAnchor(okay::UIAnchor a, int& col, int& row) { const int i = (int)a; if (i < 0 || i > 8) return false; col = i % 3; row = i / 3; return true; }
inline okay::UIAnchor AnchorFromStretch(bool across, bool down) {
    return across && down ? okay::UIAnchor::StretchFull : across ? okay::UIAnchor::StretchHorizontal : okay::UIAnchor::StretchVertical;
}
inline const char* AnchorLabel(okay::UIAnchor a) {
    static const char* n[12] = {"Top left", "Top center", "Top right", "Middle left", "Center", "Middle right",
                                "Bottom left", "Bottom center", "Bottom right", "Stretch across", "Stretch down", "Stretch full"};
    const int i = (int)a; return i >= 0 && i < 12 ? n[i] : "";
}
/// The offset that keeps a widget exactly where it is on screen when its anchor changes (fixed anchors only; for a
/// stretch anchor the offset is returned unchanged, because its numbers are margins and need the window's say).
inline okay::Vec2 ReanchorKeepPlace(okay::UIAnchor from, okay::UIAnchor to, const okay::Vec2& pos, const okay::Vec2& size, float cw, float ch) {
    if (okay::AnchorIsStretch(from) || okay::AnchorIsStretch(to)) return pos;
    const okay::Vec2 o = okay::ResolveAnchor(from, pos, size, cw, ch);
    const okay::Vec2 base = okay::ResolveAnchor(to, {0, 0}, size, cw, ch);
    return {o.x - base.x, o.y - base.y};
}

// ---- responsive preview -----------------------------------------------------------------------------------------
struct PreviewSize { const char* name; int w, h; };
inline const std::vector<PreviewSize>& PreviewSizes() {
    static const std::vector<PreviewSize> v = {
        {"Design size (1280 x 720)", 1280, 720}, {"Full HD (1920 x 1080)", 1920, 1080}, {"Small laptop (1366 x 768)", 1366, 768},
        {"4K (3840 x 2160)", 3840, 2160}, {"Ultrawide (2560 x 1080)", 2560, 1080}, {"Old screen 4:3 (1024 x 768)", 1024, 768},
        {"Phone, landscape (2400 x 1080)", 2400, 1080}, {"Phone, upright (1080 x 2400)", 1080, 2400}, {"Tablet (2048 x 1536)", 2048, 1536}};
    return v;
}
/// Where a widget with this anchor, offset and size lands on a screen of cw x ch.
inline Rect PlaceAt(okay::UIAnchor a, const okay::Vec2& pos, const okay::Vec2& size, float cw, float ch) {
    okay::Vec2 o, s; okay::ResolveAnchorRect(a, pos, size, cw, ch, o, s); return {o.x, o.y, s.x, s.y};
}
/// Is the rectangle completely off the screen (a warning in the preview)?
inline bool OutsideCanvas(const Rect& r, float cw, float ch) { return r.R() < 0 || r.B() < 0 || r.x > cw || r.y > ch; }

// ---- the ruler bar: where each control sits, so nothing prints over anything else ---------------------------------
enum class RulerHit { None, ToggleLayers, Fit, NewGuideFromTop, NewGuideFromLeft };
constexpr float kBarH = 18.0f, kRulerW = 16.0f;
struct RulerLayout {
    Rect bar, layersBtn, fitBtn, zoomSlot;   // a strip along the top: L, Fit, then the zoom percentage
    Rect topRuler, leftRuler, corner;        // the rulers sit below the bar; the corner box stays empty
    Rect content;                            // what is left for the design screen (handles and labels stay inside)
    bool Contains(const Rect& r, float x, float y) const { return x >= r.x && y >= r.y && x <= r.R() && y <= r.B(); }
};
inline RulerLayout MakeRulerLayout(float x, float y, float w, float h) {
    RulerLayout L;
    L.bar = {x, y, w, kBarH};
    L.layersBtn = {x + 2, y + 1, 22, kBarH - 2};
    L.fitBtn = {x + 28, y + 1, 36, kBarH - 2};
    L.zoomSlot = {x + 70, y, 56, kBarH};
    L.corner = {x, y + kBarH, kRulerW, kRulerW};
    L.topRuler = {x + kRulerW, y + kBarH, w - kRulerW, kRulerW};
    L.leftRuler = {x, y + kBarH + kRulerW, kRulerW, h - kBarH - kRulerW};
    L.content = {x + kRulerW, y + kBarH + kRulerW, w - kRulerW, h - kBarH - kRulerW};
    return L;
}
/// What a mouse press at (mx, my) does. The buttons win over everything; a press on a ruler starts a guide.
inline RulerHit RulerPress(const RulerLayout& L, float mx, float my) {
    if (L.Contains(L.layersBtn, mx, my)) return RulerHit::ToggleLayers;
    if (L.Contains(L.fitBtn, mx, my)) return RulerHit::Fit;
    if (L.Contains(L.bar, mx, my) || L.Contains(L.corner, mx, my)) return RulerHit::None;   // empty parts of the bar swallow the press
    if (L.Contains(L.topRuler, mx, my)) return RulerHit::NewGuideFromTop;
    if (L.Contains(L.leftRuler, mx, my)) return RulerHit::NewGuideFromLeft;
    return RulerHit::None;
}
/// Fit: zoom 100% and no pan (the whole design screen is shown).
inline void FitReset(float& zoom, float& panX, float& panY) { zoom = 1.0f; panX = 0.0f; panY = 0.0f; }
/// A point pulled inside the rectangle (handles and labels at the edge of the view).
inline void ClampPoint(const Rect& area, float& x, float& y) {
    x = std::min(std::max(x, area.x), area.R()); y = std::min(std::max(y, area.y), area.B());
}
/// Move a w x h box so it lies inside `area` (as far as it fits).
inline Rect ClampBox(const Rect& area, Rect b) {
    if (b.R() > area.R()) b.x = area.R() - b.w;
    if (b.B() > area.B()) b.y = area.B() - b.h;
    if (b.x < area.x) b.x = area.x;
    if (b.y < area.y) b.y = area.y;
    return b;
}
/// A pixel value as the readout shows it: at most one decimal, none when it is whole ("25", "13.5", not "25.0001").
inline std::string FormatPx(float v) {
    const float r = std::round(v * 10.0f) / 10.0f;
    char b[32];
    if (std::fabs(r - std::round(r)) < 0.05f) std::snprintf(b, sizeof(b), "%d", (int)std::lround(r)); else std::snprintf(b, sizeof(b), "%.1f", r);
    return b;
}

// ---- advanced tools: selection, key-object alignment, spacing, flip, units, search, commands ----------------------
struct Item { int id = 0; int type = 0; bool locked = false, hidden = false; };
inline std::vector<int> SelectAllIds(const std::vector<Item>& v) { std::vector<int> o; for (const Item& i : v) if (!i.locked && !i.hidden) o.push_back(i.id); return o; }
inline std::vector<int> InvertIds(const std::vector<Item>& v, const std::vector<int>& sel) {
    std::vector<int> o; for (const Item& i : v) if (!i.locked && !i.hidden && std::find(sel.begin(), sel.end(), i.id) == sel.end()) o.push_back(i.id); return o;
}
/// Everything of the same kind as anything already selected.
inline std::vector<int> SameTypeIds(const std::vector<Item>& v, const std::vector<int>& sel) {
    std::vector<int> types; for (const Item& i : v) if (std::find(sel.begin(), sel.end(), i.id) != sel.end()) types.push_back(i.type);
    std::vector<int> o; for (const Item& i : v) if (!i.locked && !i.hidden && std::find(types.begin(), types.end(), i.type) != types.end()) o.push_back(i.id); return o;
}
/// Align to one chosen "key" rectangle, which does not move.
inline std::vector<Rect> AlignToKey(const std::vector<Rect>& v, size_t key, AlignMode m) {
    if (key >= v.size()) return v;
    std::vector<Rect> o = AlignTo(v, m, v[key]); o[key] = v[key]; return o;
}
/// Equal gaps of exactly `gap` pixels, in order of position; the first one stays where it is. Result in input order.
inline std::vector<Rect> DistributeGap(std::vector<Rect> v, bool horizontal, float gap) {
    std::vector<size_t> idx(v.size()); for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
    std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return horizontal ? v[a].x < v[b].x : v[a].y < v[b].y; });
    for (size_t k = 1; k < idx.size(); ++k) {
        const Rect& p = v[idx[k - 1]]; Rect& q = v[idx[k]];
        if (horizontal) q.x = p.R() + gap; else q.y = p.B() + gap;
    }
    return v;
}
/// Give every rectangle the key's width and / or height (the key is unchanged).
inline std::vector<Rect> MatchToKey(std::vector<Rect> v, size_t key, bool w, bool h) {
    if (key >= v.size()) return v;
    const Rect k = v[key];
    for (Rect& q : v) { if (w) q.w = k.w; if (h) q.h = k.h; }
    return v;
}
/// Tidy up: lay the rectangles out in `cols` columns, in reading order (top row first), starting at the top-left of
/// their bounds, with `gap` between cells. Column widths and row heights follow the largest member.
inline std::vector<Rect> TidyGrid(std::vector<Rect> v, int cols, float gap) {
    if (v.empty() || cols < 1) return v;
    const Rect b = Bounds(v);
    std::vector<size_t> idx(v.size()); for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
    std::sort(idx.begin(), idx.end(), [&](size_t a, size_t c) { return v[a].y != v[c].y ? v[a].y < v[c].y : v[a].x < v[c].x; });
    const int rows = ((int)v.size() + cols - 1) / cols;
    std::vector<float> cw(cols, 0), rh(rows, 0);
    for (size_t k = 0; k < idx.size(); ++k) { cw[k % cols] = std::max(cw[k % cols], v[idx[k]].w); rh[k / cols] = std::max(rh[k / cols], v[idx[k]].h); }
    for (size_t k = 0; k < idx.size(); ++k) {
        float x = b.x, y = b.y; for (int c = 0; c < (int)(k % cols); ++c) x += cw[c] + gap; for (int r = 0; r < (int)(k / cols); ++r) y += rh[r] + gap;
        v[idx[k]].x = x; v[idx[k]].y = y;
    }
    return v;
}
/// Mirror the group about the middle of its bounds (flip left-right or up-down).
inline std::vector<Rect> Flip(std::vector<Rect> v, bool horizontal) {
    const Rect b = Bounds(v);
    for (Rect& q : v) { if (horizontal) q.x = b.x + b.R() - q.R(); else q.y = b.y + b.B() - q.B(); }
    return v;
}
/// Rotation from the pivot to the mouse, in degrees clockwise on screen with 0 = straight up; Shift snaps to 15.
inline float AngleFromPivot(float px, float py, float mx, float my, bool snap15) {
    float a = std::atan2(mx - px, -(my - py)) * 57.2957795f;
    if (a < 0) a += 360.0f;
    if (snap15) a = std::round(a / 15.0f) * 15.0f;
    return a >= 360.0f ? a - 360.0f : a;
}
enum class Unit { Px, Percent };
/// A typed value in px or % of the parent, as pixels (and back).
inline float ToPx(float v, Unit u, float parent) { return u == Unit::Percent ? v * 0.01f * parent : v; }
inline float FromPx(float px, Unit u, float parent) { return u == Unit::Percent ? (parent > 0 ? px * 100.0f / parent : 0.0f) : px; }
/// Scale a rectangle about a point, as "scale with children" does for each child.
inline Rect ScaleAbout(const Rect& r, float ox, float oy, float sx, float sy) { return {ox + (r.x - ox) * sx, oy + (r.y - oy) * sy, r.w * sx, r.h * sy}; }

/// Case-insensitive: does the name contain every word of the query?
inline bool NameMatches(const std::string& name, const std::string& query) {
    auto low = [](std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; };
    const std::string n = low(name); std::string q = low(query), w;
    for (size_t i = 0; i <= q.size(); ++i) {
        if (i == q.size() || q[i] == ' ') { if (!w.empty() && n.find(w) == std::string::npos) return false; w.clear(); } else w += q[i];
    }
    return true;
}
/// Replace text; returns the new string and how many were replaced.
inline std::string ReplaceText(const std::string& s, const std::string& find, const std::string& repl, bool matchCase, int& count) {
    count = 0; if (find.empty()) return s;
    auto low = [&](std::string t) { if (!matchCase) for (char& c : t) c = (char)std::tolower((unsigned char)c); return t; };
    const std::string hs = low(s), nd = low(find); std::string out; size_t i = 0;
    while (i < s.size()) {
        if (hs.compare(i, nd.size(), nd) == 0) { out += repl; i += nd.size(); ++count; } else out += s[i++];
    }
    return out;
}

struct Command { std::string id, label, keys, words; };
/// The commands that match the typed words, best first (a label that starts with the words beats one that only contains them).
inline std::vector<int> RankCommands(const std::vector<Command>& cmds, const std::string& query) {
    std::vector<std::pair<int, int>> sc;
    for (int i = 0; i < (int)cmds.size(); ++i) {
        if (!NameMatches(cmds[i].label + " " + cmds[i].words, query)) continue;
        int score = 10; if (query.empty() || NameMatches(cmds[i].label.substr(0, query.size()), query)) score = 0;
        sc.push_back({score, i});
    }
    std::stable_sort(sc.begin(), sc.end(), [](auto& a, auto& b) { return a.first < b.first; });
    std::vector<int> o; for (auto& p : sc) o.push_back(p.second); return o;
}
struct Shortcut { const char* keys; const char* what; };
inline const std::vector<Shortcut>& Shortcuts() {
    static const std::vector<Shortcut> v = {
        {"Ctrl+P", "Find any action"}, {"F1 or ?", "This list"}, {"Arrow keys", "Nudge 1 pixel (Shift: grid step)"},
        {"Mouse wheel", "Zoom about the cursor"}, {"Middle drag or Space+drag", "Pan"}, {"Drag from a ruler", "New guide (drag it back to remove)"},
        {"Shift while resizing", "Keep the shape"}, {"Alt while resizing", "Resize from the middle"},
        {"Ctrl+A", "Select all widgets"}, {"Ctrl+Shift+I", "Invert the selection"}, {"Ctrl+Shift+A", "Select the same kind"},
        {"Ctrl+G", "Group (put on a card)"}, {"Ctrl+Shift+L", "Lock selected"}, {"Ctrl+Shift+H", "Hide selected"},
        {"Ctrl+Z / Ctrl+Y", "Undo / redo"}};
    return v;
}

}  // namespace oked::uid
