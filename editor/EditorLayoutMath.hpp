#pragma once
// Editor layout and placement maths with no UI library in sight, so it is unit tested
// (tests/test_editor_layout.cpp). The drawing code in main.cpp only measures text, asks
// these functions where things go, and draws the answer.
//   - Overlay anchoring: the Scene view's camera readout sits in a corner and steps clear
//     of the top-left stats line (PlaceOverlay)
//   - Label placement: the selected object's name, camera / light names and gizmo hints
//     are placed so they do not sit on each other, and fade when the view is crowded
//   - Density metrics: Compact / Comfortable spacing for the Inspector, and the width the
//     value column gets at the Inspector's narrow widths
//   - Toolbar planning: icons only or icons + text, and what moves into the "..." menu
//   - Maximize: Shift+Space keeps the layout to come back to
//   - The view preferences those features save in okay_settings.txt
// No <mutex> / <thread> here and no names that clash with the Windows headers.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace okay::edlayout {

struct Pt { float x = 0, y = 0; };
struct Rc {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    float W() const { return x1 - x0; }
    float H() const { return y1 - y0; }
    float Area() const { return std::max(0.0f, W()) * std::max(0.0f, H()); }
};
inline Rc MakeRc(float x, float y, float w, float h) { return Rc{x, y, x + w, y + h}; }
inline bool Overlaps(const Rc& a, const Rc& b) { return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1; }
inline float OverlapArea(const Rc& a, const Rc& b) {
    const float w = std::min(a.x1, b.x1) - std::max(a.x0, b.x0);
    const float h = std::min(a.y1, b.y1) - std::max(a.y0, b.y0);
    return (w > 0 && h > 0) ? w * h : 0.0f;
}
inline Rc Moved(Rc r, float dx, float dy) { r.x0 += dx; r.x1 += dx; r.y0 += dy; r.y1 += dy; return r; }
inline bool Inside(const Rc& outer, const Rc& r) { return r.x0 >= outer.x0 && r.y0 >= outer.y0 && r.x1 <= outer.x1 && r.y1 <= outer.y1; }
/// Shift `r` the smallest amount that puts it inside `outer` (if it is bigger, its top-left wins).
inline Rc ClampInto(Rc r, const Rc& outer) {
    if (r.x1 > outer.x1) r = Moved(r, outer.x1 - r.x1, 0);
    if (r.y1 > outer.y1) r = Moved(r, 0, outer.y1 - r.y1);
    if (r.x0 < outer.x0) r = Moved(r, outer.x0 - r.x0, 0);
    if (r.y0 < outer.y0) r = Moved(r, 0, outer.y0 - r.y0);
    return r;
}

// ---- Overlay anchoring ----------------------------------------------------------------------------

enum class Corner { TopLeft = 0, TopRight = 1, BottomLeft = 2, BottomRight = 3 };
inline constexpr int kCornerCount = 4;
inline const char* CornerName(int c) {
    static const char* k[kCornerCount] = {"Top left", "Top right", "Bottom left", "Bottom right"};
    return (c >= 0 && c < kCornerCount) ? k[c] : "";
}
inline int ClampCorner(int c) { return c < 0 ? 0 : (c >= kCornerCount ? kCornerCount - 1 : c); }

struct OverlayPlacement {
    Rc rect;               // where to draw it
    bool visible = true;   // false: the canvas is too small to show it without covering `avoid`
    bool moved = false;    // true: it had to step clear of something
};

/// Put a `size`-sized overlay in `corner` of `canvas`, `margin` in from the edges. Anything in `avoid`
/// (the stats line, the camera preview, ...) it would cover pushes it away from the top / bottom
/// edge it is anchored to, past that rectangle. If it no longer fits, `visible` is false.
inline OverlayPlacement PlaceOverlay(const Rc& canvas, Pt size, Corner corner, float margin,
                                     const std::vector<Rc>& avoid, float gap = 4.0f) {
    OverlayPlacement out;
    const bool right = corner == Corner::TopRight || corner == Corner::BottomRight;
    const bool bottom = corner == Corner::BottomLeft || corner == Corner::BottomRight;
    Rc r;
    r.x0 = right ? canvas.x1 - margin - size.x : canvas.x0 + margin;
    r.x1 = r.x0 + size.x;
    r.y0 = bottom ? canvas.y1 - margin - size.y : canvas.y0 + margin;
    r.y1 = r.y0 + size.y;
    // Several rectangles can chain (a stats line, then the preview below it): repeat until clear.
    for (int pass = 0; pass < (int)avoid.size() + 1; ++pass) {
        bool hit = false;
        for (const Rc& a : avoid) {
            if (!Overlaps(r, a)) continue;
            hit = true; out.moved = true;
            r = bottom ? Moved(r, 0, (a.y0 - gap) - r.y1) : Moved(r, 0, (a.y1 + gap) - r.y0);
        }
        if (!hit) break;
    }
    out.visible = size.x <= canvas.W() && size.y <= canvas.H() && Inside(canvas, r);
    for (const Rc& a : avoid) if (Overlaps(r, a)) out.visible = false;
    out.rect = r;
    return out;
}

// ---- Label placement -----------------------------------------------------------------------------

struct LabelReq {
    Pt anchor;                 // the screen point the label belongs to (object origin, gizmo centre)
    Pt size;                   // text size in px
    float keepOut = 0;         // radius round the anchor to stay out of (a gizmo's handles)
    int priority = 0;          // higher gets its pick of spots first (selected name > hint > camera names)
};
struct LabelPlaced {
    Rc rect;                   // where the text box goes
    float alpha = 1.0f;        // 1 = normal; lower when it had to overlap something
    float scale = 1.0f;        // 1 = normal; smaller when the view is crowded
    bool visible = true;       // false: no spot was clear enough, drawing it would only be noise
    bool leader = false;       // true: it ended up far from the anchor, so draw a thin line back to it
};

/// Eight spots round the anchor, nearest first: right, left, above, below, then the diagonals. Each clears
/// the `keepOut` radius.
inline std::vector<Rc> LabelCandidates(const LabelReq& q, float scale, float pad) {
    const float w = q.size.x * scale + pad * 2, h = q.size.y * scale + pad * 2;
    const float d = q.keepOut + pad;
    const float ax = q.anchor.x, ay = q.anchor.y;
    std::vector<Rc> c;
    c.push_back(MakeRc(ax + d,        ay - h * 0.5f, w, h));   // right
    c.push_back(MakeRc(ax - d - w,    ay - h * 0.5f, w, h));   // left
    c.push_back(MakeRc(ax - w * 0.5f, ay - d - h,    w, h));   // above
    c.push_back(MakeRc(ax - w * 0.5f, ay + d,        w, h));   // below
    c.push_back(MakeRc(ax + d,        ay - d - h,    w, h));   // upper right
    c.push_back(MakeRc(ax - d - w,    ay - d - h,    w, h));   // upper left
    c.push_back(MakeRc(ax + d,        ay + d,        w, h));   // lower right
    c.push_back(MakeRc(ax - d - w,    ay + d,        w, h));   // lower left
    return c;
}

/// Place every label. Highest priority first; each takes the first candidate spot that is inside `canvas`, clear
/// of the earlier labels and of the `obstacles` (HUD boxes). When nothing is clear it shrinks to 85%, tries again,
/// and then settles for the least-overlapping spot at reduced alpha; if even that covers more than half of
/// itself it is hidden. The result is in the SAME ORDER as `reqs`.
inline std::vector<LabelPlaced> PlaceLabels(const std::vector<LabelReq>& reqs, const Rc& canvas,
                                            const std::vector<Rc>& obstacles, float pad = 2.0f) {
    std::vector<LabelPlaced> out(reqs.size());
    std::vector<std::size_t> order(reqs.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return reqs[a].priority > reqs[b].priority; });
    std::vector<Rc> taken = obstacles;
    for (std::size_t idx : order) {
        const LabelReq& q = reqs[idx];
        const Rc disc = MakeRc(q.anchor.x - q.keepOut, q.anchor.y - q.keepOut, q.keepOut * 2, q.keepOut * 2);
        LabelPlaced best; bool found = false; float bestOv = 1e30f;
        for (int pass = 0; pass < 2 && !found; ++pass) {
            const float scale = pass == 0 ? 1.0f : 0.85f;
            const std::vector<Rc> cand = LabelCandidates(q, scale, pad);
            for (std::size_t ci = 0; ci < cand.size(); ++ci) {
                Rc r = ClampInto(cand[ci], canvas);
                float ov = 0;
                for (const Rc& t : taken) ov += OverlapArea(r, t);
                // A clamp that dragged the box back over its own anchor's keep-out disc is not clear either.
                if (q.keepOut > 0) ov += OverlapArea(r, disc);
                const bool clear = ov <= 0.0f;
                if (clear || ov < bestOv) {
                    bestOv = ov;
                    best.rect = r; best.scale = scale;
                    best.alpha = clear ? (pass == 1 ? 0.85f : 1.0f) : 0.55f;
                    best.visible = true;
                    const float cx = (r.x0 + r.x1) * 0.5f - q.anchor.x, cy = (r.y0 + r.y1) * 0.5f - q.anchor.y;
                    best.leader = std::sqrt(cx * cx + cy * cy) > q.keepOut + std::max(q.size.x, q.size.y) * 1.2f;
                    if (clear) { found = true; break; }
                }
            }
        }
        if (!found) {
            const float a = best.rect.Area();
            if (a <= 0 || bestOv > a * 0.5f) best.visible = false;
        }
        out[idx] = best;
        if (best.visible) taken.push_back(best.rect);
    }
    return out;
}

/// How strongly names should show for `count` labels in `areaPx` pixels^2: 1 when the view has room, falling toward
/// 0.35 as the labels outnumber it (the "fade when crowded" rule). One label per 9000 px^2 or more is "roomy".
inline float CrowdFade(int count, float areaPx) {
    if (count <= 1 || areaPx <= 0) return 1.0f;
    const float perLabel = areaPx / (float)count;
    const float roomy = 9000.0f, tight = 2500.0f;
    if (perLabel >= roomy) return 1.0f;
    if (perLabel <= tight) return 0.35f;
    return 0.35f + 0.65f * (perLabel - tight) / (roomy - tight);
}

// ---- Density (Inspector spacing) ---------------------------------------------------------------

enum class Density { Compact = 0, Comfortable = 1 };
inline int ClampDensity(int d) { return d <= 0 ? 0 : 1; }
inline const char* DensityName(int d) { return d <= 0 ? "Compact" : "Comfortable"; }

struct InspectorMetrics {
    Pt itemSpacing;        // between rows and between controls on a row
    Pt framePadding;       // inside a field: sets the row height
    Pt innerSpacing;       // between a control and its label
    float sectionGap = 0;  // extra space above a component's header
    float rowHeight = 0;   // one row, text line + 2 x vertical padding
    float valueWidth = 0;  // width of the value column; the label goes right of it
    float labelWidth = 0;  // what is left for the label
    bool stacked = false;  // too narrow for a value column and a label side by side
};

/// Spacing and the value / label split for the Inspector. `textHeight` is one line of text (font size x scale),
/// `uiScale` the editor's UI scale (the metrics are for 100% and scale with it), `contentWidth` the room inside
/// the panel. At narrow widths (a 440 px panel) the label keeps a readable minimum and the value column gives way.
inline InspectorMetrics ComputeInspectorMetrics(Density d, float uiScale, float textHeight, float contentWidth) {
    InspectorMetrics m;
    const float s = uiScale > 0.25f ? uiScale : 1.0f;
    const bool comfy = d == Density::Comfortable;
    m.itemSpacing  = Pt{(comfy ? 10.0f : 7.0f) * s, (comfy ? 12.0f : 5.0f) * s};
    m.framePadding = Pt{(comfy ? 9.0f : 6.0f) * s,  (comfy ? 8.0f : 4.0f) * s};
    m.innerSpacing = Pt{(comfy ? 8.0f : 6.0f) * s,  (comfy ? 6.0f : 4.0f) * s};
    m.sectionGap   = (comfy ? 10.0f : 3.0f) * s;
    m.rowHeight    = textHeight + m.framePadding.y * 2;
    // Label column: wide enough for a typical name ("Rotation", "Angular Drag"), more room when there is more.
    const float minLabel = 96.0f * s, minValue = 120.0f * s;
    const float w = std::max(contentWidth, 1.0f);
    if (w < minLabel + minValue + m.innerSpacing.x) {
        // Too narrow for both side by side: the value takes the whole width and the caller leaves the label out
        // of the row (it shows in the field's tooltip).
        m.stacked = true;
        m.valueWidth = w;
        return m;
    }
    const float frac = comfy ? 0.58f : 0.62f;
    float value = std::floor(w * frac);
    value = std::min(value, w - minLabel - m.innerSpacing.x);
    value = std::max(value, minValue);
    m.valueWidth = value;
    m.labelWidth = w - value - m.innerSpacing.x;
    return m;
}

// ---- Toolbar --------------------------------------------------------------------------------------

struct ToolbarItem {
    float iconW = 0;     // width as a bare icon
    float labelW = 0;    // width with its text beside the icon
    int group = 0;       // items of one group sit together; a divider goes between groups
};
struct ToolbarPlan {
    int labelCount = 0;                   // the first this-many items carry their text beside the icon
    std::vector<bool> shown;              // per item: on the bar (false = in the "..." menu)
    std::vector<bool> dividerBefore;      // per item: a divider goes before it
    float used = 0;
    int overflowed = 0;
    bool labels() const { return labelCount > 0; }
};

/// Lay the toolbar out in `avail` px. `wantLabels` asks for icon + text on every tool; when that does not fit,
/// the labels are kept for as many of the leading tools as fit (the rest stay bare icons, their tooltips still
/// name them), then none. Only when even bare icons do not fit do the last tools move into the overflow menu.
/// `gap` is the spacing between items, `divider` the width of a group divider, `reserve` what the overflow
/// button and the things beside it need.
inline ToolbarPlan PlanToolbar(const std::vector<ToolbarItem>& items, float avail, bool wantLabels,
                               float gap, float divider, float reserve) {
    ToolbarPlan p;
    p.shown.assign(items.size(), true);
    p.dividerBefore.assign(items.size(), false);
    auto widthOf = [&](int labelCount, std::size_t upTo, std::vector<bool>* div) {
        float w = 0; int prevGroup = -1; bool first = true;
        for (std::size_t i = 0; i < upTo; ++i) {
            if (!first && items[i].group != prevGroup) { w += divider; if (div) (*div)[i] = true; }
            else if (!first) w += gap;
            w += (int)i < labelCount ? items[i].labelW : items[i].iconW;
            first = false; prevGroup = items[i].group;
        }
        return w;
    };
    const float room = avail - reserve;
    if (wantLabels)
        for (int k = (int)items.size(); k >= 1; --k)
            if (widthOf(k, items.size(), nullptr) <= room) { p.labelCount = k; break; }
    std::size_t n = items.size();
    while (n > 0 && widthOf(p.labelCount, n, nullptr) > room) --n;
    for (std::size_t i = n; i < items.size(); ++i) { p.shown[i] = false; ++p.overflowed; }
    p.used = widthOf(p.labelCount, n, &p.dividerBefore);
    return p;
}

// ---- Maximize the viewport ------------------------------------------------------------------------

/// Shift+Space: the Scene fills the editor; pressing it again brings the previous panels back. This holds what
/// to bring back. `layoutIni` is the saved panel arrangement, `open` the panels that were open.
struct MaximizeState {
    bool on = false;
    std::string layoutIni;
    std::vector<std::string> open;
    std::string layoutName;
};
/// Enter: remember the current layout. Returns false (and changes nothing) if already maximized.
inline bool EnterMaximize(MaximizeState& s, const std::string& ini, const std::vector<std::string>& open, const std::string& name) {
    if (s.on) return false;
    s.on = true; s.layoutIni = ini; s.open = open; s.layoutName = name;
    return true;
}
/// Leave: hands back what was remembered and clears it. Returns false if not maximized.
inline bool LeaveMaximize(MaximizeState& s, std::string& ini, std::vector<std::string>& open, std::string& name) {
    if (!s.on) return false;
    ini = std::move(s.layoutIni); open = std::move(s.open); name = std::move(s.layoutName);
    s = MaximizeState{};
    return true;
}

// ---- View preferences (okay_settings.txt) ------------------------------------------------------

struct ViewPrefs {
    bool camHud = false;            // the camera readout overlay (off until switched on in the Scene view menu)
    bool camHudCollapsed = false;   // shrunk to its title line
    int camHudCorner = (int)Corner::TopRight;
    int camHudOpacityPct = 60;      // 20..100
    bool statsLine = true;          // the "3D | 5 objects | 25 FPS" line at the top left
    bool showNames = true;          // names beside the selected object, cameras and lights
    bool toolbarLabels = false;     // Scene view toolbar: icon + text
    int density = (int)Density::Comfortable;
    int layoutPreset = 0;           // the built-in layout on screen when the editor closed (-1 = a saved or custom arrangement)

    /// One "key value" line of the settings file. False when the key is not one of these.
    bool Apply(const std::string& k, int v) {
        if (k == "showcamhud")         { camHud = v != 0; return true; }
        if (k == "camhudcollapsed")    { camHudCollapsed = v != 0; return true; }
        if (k == "camhudcorner")       { camHudCorner = ClampCorner(v); return true; }
        if (k == "camhudopacity")      { camHudOpacityPct = v < 20 ? 20 : (v > 100 ? 100 : v); return true; }
        if (k == "statsline")          { statsLine = v != 0; return true; }
        if (k == "shownames")          { showNames = v != 0; return true; }
        if (k == "toolbarlabels")      { toolbarLabels = v != 0; return true; }
        if (k == "density")            { density = ClampDensity(v); return true; }
        if (k == "layoutpreset")       { layoutPreset = v < -1 ? -1 : (v > 99 ? 99 : v); return true; }
        return false;
    }
    /// The settings lines for all of these.
    std::string Write() const {
        std::ostringstream o;
        o << "showcamhud " << (camHud ? 1 : 0) << "\n"
          << "camhudcollapsed " << (camHudCollapsed ? 1 : 0) << "\n"
          << "camhudcorner " << camHudCorner << "\n"
          << "camhudopacity " << camHudOpacityPct << "\n"
          << "statsline " << (statsLine ? 1 : 0) << "\n"
          << "shownames " << (showNames ? 1 : 0) << "\n"
          << "toolbarlabels " << (toolbarLabels ? 1 : 0) << "\n"
          << "density " << density << "\n"
          << "layoutpreset " << layoutPreset << "\n";
        return o.str();
    }
    /// Read a whole settings text, picking out the keys that are ours.
    static ViewPrefs Parse(const std::string& text) {
        ViewPrefs p; std::istringstream in(text); std::string k; int v;
        while (in >> k >> v) p.Apply(k, v);
        return p;
    }
};

} // namespace okay::edlayout
