// OkayUI docking: the dock space, tab strips, splitters, drag to float / dock, drop-target preview.
// The data model is in OkayUIDockLayout.cpp. See okay/UI/OkayUIDock.hpp.
#include "OkayUI_ContextState.hpp"
#include "okay/UI/OkayUIDock.hpp"
#include "okay/UI/OkayUIDockBuilder.hpp"
#include "OkayUIInternal.hpp"   // g_framePadX: a pushed StyleVar_FramePadding (x) is the tabs' text inset

#include <algorithm>
#include <cstring>
#include <cmath>
#include <map>

namespace OkayUI {

namespace {

bool In(const Rect& r, Vec2 p) { return p.x >= r.x && p.y >= r.y && p.x < r.x + r.w && p.y < r.y + r.h; }
Vec2 V(float x, float y) { return Vec2{x, y}; }

unsigned HashName(const std::string& s) {
    unsigned h = 2166136261u;
    for (unsigned char c : s) { h ^= c; h *= 16777619u; }
    return h;
}
/// The visible part of a window name: ImGui style "Title##id" shows "Title".
std::string Display(const std::string& name) {
    const size_t p = name.find("##");
    return p == std::string::npos ? name : name.substr(0, p);
}

struct TabRect { std::string name; int node = 0; int index = 0; Rect r, close; float ideal = 0.0f; };

struct Ctx {
    DockLayout layout;
    long long spaceFrame = -10;
    Rect area;
    float tabH = 24.0f;
    std::vector<TabRect> tabs;
    std::vector<std::pair<int, float>> reserve;   // leaf -> width kept free at the right end of its strip (SetDockTrailingReserve)
    std::vector<Rect> floatPrev, floatCur;
    std::vector<std::string> closed;

    // TabPress: a tab is held (a move inside its strip reorders); TabDrag: a docked tab pulled out of its strip, still docked where it was
    // until the button comes up (a drop docks it, a release away from every target floats it, Escape or the right button cancels);
    // WinDrag: a floating window dragged by its title; Cancelled: waiting for the left button to come up after a cancel.
    enum Kind { None, Splitter, TabPress, TabDrag, WinDrag, Cancelled } kind = None;
    Vec2 dragSize{0, 0};          // the size a dragged tab's window would float at
    float tabW = 0.0f;            // the dragged tab's width (its ghost under the pointer)
    int splitNode = 0;
    float grabOff = 0.0f;
    std::string dragWin;
    Vec2 press, grab;
    bool moving = false, fromTab = false;
    int pressNode = 0;
    DockDropInfo drop;

    // the DockBegin in progress
    bool inBegin = false, began = false, clipped = false, floating = false;
    std::string curName;
    bool* pOpen = nullptr;
    // Dear ImGui's rule: a docked window that is not submitted (its Begin not called) has no tab that frame. The frame each window last
    // called DockBegin; a window seen this frame or the one before is "submitted".
    std::map<std::string, long long> seen;
    bool Submitted(const std::string& name) const {
        const auto it = seen.find(name);
        return it != seen.end() && it->second >= (long long)GetFrameCount() - 1;
    }
};
Ctx& C() { static Ctx c; return c; }
float TabTextInset() { return ui_core::g_framePadX >= 0.0f && std::isfinite(ui_core::g_framePadX) ? std::min(ui_core::g_framePadX, 64.0f) : 10.0f; }

void BuildTabs(Ctx& c) {
    c.tabs.clear();
    // a pushed StyleVar_FramePadding (x) is the text's inset on each side of a tab (Dear ImGui's tabs: room for an icon the host draws there)
    const float pad = TabTextInset(), closeW = c.tabH * 0.7f;
    for (const DockLayout::Node& n : c.layout.Nodes()) {
        if (n.IsSplit() || n.tabs.empty() || n.noTabBar) continue;
        bool anySubmitted = false;
        for (const std::string& t : n.tabs) anySubmitted |= c.Submitted(t);
        float keep = 0.0f;
        for (const auto& rv : c.reserve) if (rv.first == n.id) keep = rv.second;
        keep = std::max(0.0f, std::min(keep, n.rect.w * 0.5f));
        std::vector<float> w;
        float total = 0.0f;
        // windows not submitted get no tab (while none of the leaf's windows has been seen yet, e.g. a layout just built, all show)
        auto shown = [&](const std::string& t) { return !anySubmitted || c.Submitted(t); };
        for (const std::string& t : n.tabs) { const float tw = shown(t) ? CalcTextSize(Display(t).c_str()).x + 2.0f * pad + closeW : 0.0f; w.push_back(tw); total += tw; }
        const float room = n.rect.w - keep;
        const float scale = total > room && total > 0.0f ? room / total : 1.0f;
        float x = n.rect.x;
        for (size_t i = 0; i < n.tabs.size(); ++i) {
            if (!shown(n.tabs[i])) continue;
            TabRect t;
            t.name = n.tabs[i]; t.node = n.id; t.index = (int)i;
            const float tw = w[i] * scale;
            t.r = Rect{x, n.rect.y, tw, std::min(c.tabH, n.rect.h)};
            const float cw = std::min(closeW, tw * 0.4f);
            t.close = Rect{x + tw - cw, t.r.y, cw, t.r.h};
            t.ideal = w[i];
            c.tabs.push_back(t);
            x += tw;
        }
    }
}
void Rebuild(Ctx& c) { c.layout.Compute(c.area, c.tabH); BuildTabs(c); }

// The guide tiles a drag shows: five over the leaf under the pointer (centre = join its tabs, the four sides = split it) and four at the
// middle of the dock area's edges (dock along the whole edge). The tile under the pointer is the target; elsewhere the leaf's region
// decides (its outer quarter on each side splits, the rest joins).
float GuideTile(const Rect& r) { return std::max(14.0f, std::min(30.0f, std::min(r.w, r.h) / 5.0f)); }
Rect LeafGuide(const Rect& r, DockSide side) {
    const float s = GuideTile(r), g = s + 5.0f;
    const float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f;
    const float dx = side == DockSide_Left ? -g : side == DockSide_Right ? g : 0.0f, dy = side == DockSide_Top ? -g : side == DockSide_Bottom ? g : 0.0f;
    return Rect{cx + dx - s * 0.5f, cy + dy - s * 0.5f, s, s};
}
Rect RootGuide(const Rect& a, DockSide side) {
    const float s = std::max(14.0f, std::min(30.0f, std::min(a.w, a.h) / 8.0f)), m = 8.0f;
    switch (side) {
        case DockSide_Left:  return Rect{a.x + m, a.y + a.h * 0.5f - s * 0.5f, s, s};
        case DockSide_Right: return Rect{a.x + a.w - m - s, a.y + a.h * 0.5f - s * 0.5f, s, s};
        case DockSide_Top:   return Rect{a.x + a.w * 0.5f - s * 0.5f, a.y + m, s, s};
        default:             return Rect{a.x + a.w * 0.5f - s * 0.5f, a.y + a.h - m - s, s, s};
    }
}
Rect RootPreview(const Rect& a, DockSide s) {
    const float fw = a.w * 0.25f, fh = a.h * 0.25f;
    return s == DockSide_Left ? Rect{a.x, a.y, fw, a.h} : s == DockSide_Right ? Rect{a.x + a.w - fw, a.y, fw, a.h}
         : s == DockSide_Top ? Rect{a.x, a.y, a.w, fh} : Rect{a.x, a.y + a.h - fh, a.w, fh};
}
Rect LeafPreview(Ctx& c, const DockLayout::Node* n, DockSide side) {
    const Rect& r = n->rect;
    const Rect cr = c.layout.ContentRect(n->id);
    switch (side) {
        case DockSide_Left:   return Rect{r.x, cr.y, r.w * 0.5f, cr.h};
        case DockSide_Right:  return Rect{r.x + r.w * 0.5f, cr.y, r.w * 0.5f, cr.h};
        case DockSide_Top:    return Rect{r.x, cr.y, r.w, cr.h * 0.5f};
        case DockSide_Bottom: return Rect{r.x, cr.y + cr.h * 0.5f, r.w, cr.h * 0.5f};
        default:              return r;
    }
}
bool RootSplit(const Ctx& c) {
    const DockLayout::Node* r = c.layout.Find(c.layout.Root());
    return r && !(!r->IsSplit() && r->tabs.empty());
}

DockDropInfo EvalDrop(Ctx& c, Vec2 m) {
    DockDropInfo d;
    d.window = c.dragWin;
    const Rect& a = c.area;
    if (!In(a, m)) return d;
    const int root = c.layout.Root();
    if (RootSplit(c)) {
        DockSide s = DockSide_Center;
        const float band = 10.0f;   // the guide tiles at the middle of each edge, and a thin band along the edge
        for (DockSide e : {DockSide_Left, DockSide_Right, DockSide_Top, DockSide_Bottom}) if (In(RootGuide(a, e), m)) s = e;
        if (s == DockSide_Center) {
            if (m.x < a.x + band) s = DockSide_Left;
            else if (m.x >= a.x + a.w - band) s = DockSide_Right;
            else if (m.y < a.y + band) s = DockSide_Top;
            else if (m.y >= a.y + a.h - band) s = DockSide_Bottom;
        }
        if (s != DockSide_Center) { d.active = true; d.node = root; d.side = s; d.rootEdge = true; d.preview = RootPreview(a, s); return d; }
    }
    const int leaf = c.layout.LeafAt(m.x, m.y);
    if (!leaf) return d;
    const DockLayout::Node* n = c.layout.Find(leaf);
    const Rect& r = n->rect;
    d.active = true; d.node = leaf; d.side = DockSide_Center; d.preview = r;
    if (n->tabs.empty()) return d;
    const Rect cr = c.layout.ContentRect(leaf);
    for (DockSide e : {DockSide_Center, DockSide_Left, DockSide_Right, DockSide_Top, DockSide_Bottom})
        if (In(LeafGuide(cr, e), m)) { d.side = e; d.preview = LeafPreview(c, n, e); return d; }
    const float th = n->noTabBar ? 0.0f : c.tabH;
    if (m.y < r.y + th) return d;                   // over the tab strip: join the tabs
    const float u = (m.x - r.x) / std::max(1.0f, r.w), v = (m.y - (r.y + th)) / std::max(1.0f, r.h - th);
    const float dl = u, dr = 1.0f - u, dt = v, db = 1.0f - v;
    const float best = std::min(std::min(dl, dr), std::min(dt, db));
    if (best >= 0.25f) return d;
    d.side = best == dl ? DockSide_Left : best == dr ? DockSide_Right : best == dt ? DockSide_Top : DockSide_Bottom;
    d.preview = LeafPreview(c, n, d.side);
    return d;
}

// One guide tile: a rounded card with a small picture of the result (the pane it would fill, or a tab strip for the centre), lit in the
// accent when it is the target.
void GuideTileDraw(const DrawList& fg, const Rect& t, DockSide side, bool on) {
    const Color32 acc = GetColorU32(Col_Accent), card = GetColorU32(Col_PopupBg), line = GetColorU32(Col_Border), ink = GetColorU32(Col_Text, 0.55f);
    const float rd = std::min(6.0f, t.w * 0.22f);
    fg.AddRectFilled(V(t.x + 1.0f, t.y + 2.0f), V(t.x + t.w + 1.0f, t.y + t.h + 2.0f), 0x40000000u, rd);   // a soft shadow
    fg.AddRectFilled(V(t.x, t.y), V(t.x + t.w, t.y + t.h), on ? acc : card, rd);
    fg.AddRect(V(t.x, t.y), V(t.x + t.w, t.y + t.h), on ? acc : line, rd, 1.0f);
    const float p = std::max(3.0f, t.w * 0.18f);
    const Rect in{t.x + p, t.y + p, t.w - 2.0f * p, t.h - 2.0f * p};
    const Color32 fillC = on ? GetColorU32(Col_PopupBg) : acc, edge = on ? GetColorU32(Col_PopupBg) : ink;
    fg.AddRect(V(in.x, in.y), V(in.x + in.w, in.y + in.h), edge, 2.0f, 1.0f);
    Rect f = in;
    switch (side) {
        case DockSide_Left:   f.w *= 0.5f; break;
        case DockSide_Right:  f.x += f.w * 0.5f; f.w *= 0.5f; break;
        case DockSide_Top:    f.h *= 0.5f; break;
        case DockSide_Bottom: f.y += f.h * 0.5f; f.h *= 0.5f; break;
        default:              f.h = std::max(2.0f, in.h * 0.3f); break;   // the centre: a tab strip on top
    }
    fg.AddRectFilled(V(f.x, f.y), V(f.x + f.w, f.y + f.h), fillC, 1.5f);
}

void DrawGuides(Ctx& c, const DockDropInfo& d) {
    const DrawList& fg = GetForegroundDrawList();
    const Color32 acc = GetColorU32(Col_Accent);
    const float rr = std::min(8.0f, std::max(2.0f, Style().radiusSmall));
    if (d.floats) {   // no target: the window would float here (a lighter, outlined rectangle under the pointer)
        const Rect& f = d.preview;
        fg.AddRectFilled(V(f.x, f.y), V(f.x + f.w, f.y + f.h), ColorMulAlpha(acc, 0.10f), rr);
        fg.AddRect(V(f.x, f.y), V(f.x + f.w, f.y + f.h), ColorMulAlpha(acc, 0.85f), rr, 1.5f);
    } else {          // the exact area the window will fill, inset a little so the panes around it stay readable
        const Rect& p = d.preview;
        fg.AddRectFilled(V(p.x + 3.0f, p.y + 3.0f), V(p.x + p.w - 3.0f, p.y + p.h - 3.0f), ColorMulAlpha(acc, 0.24f), rr);
        fg.AddRect(V(p.x + 3.0f, p.y + 3.0f), V(p.x + p.w - 3.0f, p.y + p.h - 3.0f), acc, rr, 2.0f);
    }
    // the guide tiles: at the dock area's edges, and over the leaf under the pointer
    const Vec2 m = GetMousePos();
    if (!In(c.area, m)) return;
    if (RootSplit(c))
        for (DockSide e : {DockSide_Left, DockSide_Right, DockSide_Top, DockSide_Bottom})
            GuideTileDraw(fg, RootGuide(c.area, e), e, d.active && d.rootEdge && d.side == e);
    const int leaf = c.layout.LeafAt(m.x, m.y);
    const DockLayout::Node* n = leaf ? c.layout.Find(leaf) : nullptr;
    if (!n || n->IsSplit() || n->tabs.empty()) return;
    if (!d.active && !d.floats) return;
    const Rect cr = c.layout.ContentRect(n->id);
    const bool ownOnly = c.layout.NodeOf(c.dragWin) == n->id && n->tabs.size() <= 1;   // its own leaf with no other tab: nothing to show
    if (ownOnly) return;
    {   // a card behind the cross of five tiles
        const Rect l = LeafGuide(cr, DockSide_Left), r = LeafGuide(cr, DockSide_Right), t = LeafGuide(cr, DockSide_Top), b = LeafGuide(cr, DockSide_Bottom);
        const float pad = 5.0f;
        fg.AddRectFilled(V(l.x - pad, t.y - pad), V(r.x + r.w + pad, b.y + b.h + pad), ColorMulAlpha(GetColorU32(Col_PopupBg), 0.72f), 10.0f);
    }
    for (DockSide e : {DockSide_Center, DockSide_Left, DockSide_Right, DockSide_Top, DockSide_Bottom})
        GuideTileDraw(fg, LeafGuide(cr, e), e, d.active && !d.rootEdge && d.node == n->id && d.side == e);
}

// The dragged tab, under the pointer: a raised chip with its name, so it is clear what is being moved.
void DrawTabGhost(Ctx& c, Vec2 m) {
    const DrawList& fg = GetForegroundDrawList();
    const std::string label = Display(c.dragWin);
    const Vec2 ts = CalcTextSize(label.c_str());
    const float w = std::max(c.tabW, ts.x + 2.0f * TabTextInset()), h = c.tabH;
    const float x = m.x - std::min(c.grab.x, w - 8.0f), y = m.y - std::min(c.grab.y, h - 4.0f);
    const float rd = std::min(6.0f, h * 0.3f);
    fg.AddRectFilled(V(x + 2.0f, y + 3.0f), V(x + w + 2.0f, y + h + 3.0f), 0x50000000u, rd);
    fg.AddRectFilled(V(x, y), V(x + w, y + h), ColorMulAlpha(GetColorU32(Col_PopupBg), 0.94f), rd);
    fg.AddRect(V(x, y), V(x + w, y + h), GetColorU32(Col_Accent), rd, 1.5f);
    fg.AddText(V(x + (w - ts.x) * 0.5f, y + (h - ts.y) * 0.5f), GetColorU32(Col_Text), label.c_str());
}

} // namespace

DockLayout& GetDockLayout() { return C().layout; }
DockDropInfo GetDockDrop() { return C().drop; }

void DockReset() {
    Ctx& c = C();
    c = Ctx();
    DockBuilderReset();
}

std::string DockSaveLayout() { return C().layout.Save(); }
bool DockLoadLayout(const std::string& blob, int* repairs) {
    Ctx& c = C();
    const bool ok = c.layout.Load(blob, repairs);
    if (ok) { c.kind = Ctx::None; c.drop = DockDropInfo(); }
    return ok;
}

void ApplyDockNodeEdits();   // OkayUIDockNodes.cpp
void DockSpace(Rect area) {
    ApplyDockNodeEdits();   // writes ported code made to dock-node snapshots since the last frame
    Ctx& c = C();
    const long long frame = GetFrameCount();
    if (c.spaceFrame != frame - 1 && c.spaceFrame != frame) { c.kind = Ctx::None; c.drop = DockDropInfo(); }   // the dock was not drawn last frame
    c.spaceFrame = frame;
    c.closed.clear();
    c.floatPrev.swap(c.floatCur);
    c.floatCur.clear();

    const Input& in = GetFrameInput();
    // clip to the display
    float x0 = std::isfinite(area.x) ? area.x : 0.0f, y0 = std::isfinite(area.y) ? area.y : 0.0f;
    float x1 = x0 + (std::isfinite(area.w) ? std::max(0.0f, area.w) : 0.0f), y1 = y0 + (std::isfinite(area.h) ? std::max(0.0f, area.h) : 0.0f);
    if (in.displayW > 0.0f) { x0 = std::max(0.0f, std::min(x0, in.displayW)); x1 = std::max(0.0f, std::min(x1, in.displayW)); }
    if (in.displayH > 0.0f) { y0 = std::max(0.0f, std::min(y0, in.displayH)); y1 = std::max(0.0f, std::min(y1, in.displayH)); }
    c.area = Rect{x0, y0, std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0)};
    c.tabH = GetFrameHeight();
    for (size_t i = c.reserve.size(); i-- > 0;) if (!c.layout.Find(c.reserve[i].first)) c.reserve.erase(c.reserve.begin() + (std::ptrdiff_t)i);
    if (c.seen.size() > 256)   // (titles with counts in them come and go: forget names not seen for a while)
        for (auto it = c.seen.begin(); it != c.seen.end();) { if (it->second < frame - 600) it = c.seen.erase(it); else ++it; }
    {   // a leaf whose chosen window is not submitted shows its first submitted one instead, as Dear ImGui's dock does
        std::vector<std::string> pick;
        for (const DockLayout::Node& n : c.layout.Nodes()) {
            if (n.IsSplit() || n.tabs.empty() || c.Submitted(n.tabs[(size_t)n.active])) continue;
            for (const std::string& t : n.tabs) if (c.Submitted(t)) { pick.push_back(t); break; }
        }
        for (const std::string& t : pick) c.layout.SetActive(t);
    }
    Rebuild(c);

    const Vec2 m = GetMousePos();
    const bool down = in.mouseDown && !in.blocked;
    const bool pressed = IsMouseClicked(0);
    const bool released = IsMouseReleased(0);
    bool underFloat = in.blocked;
    for (const Rect& f : c.floatPrev) if (In(f, m)) underFloat = true;

    // ---- start something ----
    if (c.kind == Ctx::None && pressed && !underFloat) {
        bool done = false;
        for (const TabRect& t : c.tabs) {
            if (In(t.close, m)) { const std::string n = t.name; c.layout.Close(n); c.closed.push_back(n); Rebuild(c); done = true; break; }
        }
        if (!done) for (const TabRect& t : c.tabs) {
            if (In(t.r, m)) {
                c.kind = Ctx::TabPress; c.dragWin = t.name; c.pressNode = t.node; c.press = m; c.grab = V(m.x - t.r.x, m.y - t.r.y);
                c.moving = false; c.fromTab = true; done = true; break;
            }
        }
        if (!done) for (const DockLayout::Node& n : c.layout.Nodes()) {
            if (!n.IsSplit()) continue;
            Rect s = c.layout.SplitterRect(n.id);
            if (n.horizontal) { s.x -= 2.0f; s.w += 4.0f; } else { s.y -= 2.0f; s.h += 4.0f; }
            if (In(s, m)) {
                c.kind = Ctx::Splitter; c.splitNode = n.id;
                const Rect sp = c.layout.SplitterRect(n.id);
                c.grabOff = n.horizontal ? m.x - sp.x : m.y - sp.y;
                break;
            }
        }
    }

    // ---- continue / finish ----
    c.drop = DockDropInfo();
    if (c.kind == Ctx::Splitter) {
        if (!down) c.kind = Ctx::None;
        else {
            const DockLayout::Node* n = c.layout.Find(c.splitNode);
            if (!n || !n->IsSplit()) c.kind = Ctx::None;
            else { c.layout.SetSplitterPos(c.splitNode, (n->horizontal ? m.x : m.y) - c.grabOff); Rebuild(c); }
        }
    } else if (c.kind == Ctx::TabPress) {
        if (!down || !c.layout.Contains(c.dragWin)) c.kind = Ctx::None;
        else if (std::fabs(m.x - c.press.x) > 5.0f || std::fabs(m.y - c.press.y) > 5.0f || c.moving) {
            const DockLayout::Node* n = c.layout.Find(c.layout.NodeOf(c.dragWin));
            if (!n) c.kind = Ctx::None;
            else {
                const Rect strip{n->rect.x, n->rect.y - 4.0f, n->rect.w, c.tabH + 8.0f};
                if (In(strip, m)) {
                    c.moving = true;
                    int idx = 0;
                    // count tabs whose centre is left of the pointer, not counting the dragged one
                    for (const TabRect& t : c.tabs) if (t.node == n->id && t.name != c.dragWin && t.r.x + t.r.w * 0.5f < m.x) ++idx;
                    c.layout.MoveTab(c.dragWin, idx);
                    Rebuild(c);
                } else {   // pulled out of the strip: a drag that docks or floats on release; the window stays where it is until then
                    const Rect cr = c.layout.ContentRect(n->id);
                    c.dragSize = V(std::max(220.0f, cr.w), std::max(160.0f, cr.h));
                    c.tabW = 0.0f;
                    for (const TabRect& t : c.tabs) if (t.name == c.dragWin) c.tabW = t.r.w;
                    c.kind = Ctx::TabDrag; c.moving = true; c.fromTab = true;
                }
            }
        }
    }
    const bool cancelKey = in.escape || IsKeyPressed(KeyId_Escape, false) || IsMouseClicked(1);
    if ((c.kind == Ctx::TabDrag || (c.kind == Ctx::WinDrag && c.moving)) && cancelKey) c.kind = Ctx::Cancelled;   // Escape / right button: nothing changes
    if (c.kind == Ctx::Cancelled && !down) c.kind = Ctx::None;
    if (c.kind == Ctx::TabDrag) {
        const DockLayout::Node* own = c.layout.Find(c.layout.NodeOf(c.dragWin));
        if (!own || !c.layout.Contains(c.dragWin)) c.kind = Ctx::None;
        else if (down && In(Rect{own->rect.x, own->rect.y - 4.0f, own->rect.w, c.tabH + 8.0f}, m)) c.kind = Ctx::TabPress;   // back over its strip: reorder again
        else {
            DockDropInfo d = underFloat ? DockDropInfo() : EvalDrop(c, m);
            // its own leaf, when it is that leaf's only tab, is no target (docking there changes nothing)
            if (d.active && !d.rootEdge && d.node == own->id && own->tabs.size() <= 1) d = DockDropInfo();
            if (!down) {
                if (released) {
                    if (d.active) { if (d.rootEdge) c.layout.DockRoot(c.dragWin, d.side); else c.layout.Dock(c.dragWin, d.node, d.side); }
                    else if (!(In(own->rect, m) && own->tabs.size() <= 1)) c.layout.Float(c.dragWin, Rect{m.x - c.grab.x, m.y - c.grab.y, c.dragSize.x, c.dragSize.y});
                    Rebuild(c);
                }
                c.kind = Ctx::None;
            } else {
                c.drop = d;
                if (!d.active && !(In(own->rect, m) && own->tabs.size() <= 1)) { c.drop.floats = true; c.drop.preview = Rect{m.x - c.grab.x, m.y - c.grab.y, c.dragSize.x, c.dragSize.y}; }
                c.drop.window = c.dragWin; c.drop.fromTab = true;
            }
        }
    }
    if (c.kind == Ctx::WinDrag) {
        if (!c.moving && (std::fabs(m.x - c.press.x) > 4.0f || std::fabs(m.y - c.press.y) > 4.0f)) c.moving = true;
        if (!down) {
            if (c.moving && released) {
                const DockDropInfo d = EvalDrop(c, m);
                if (d.active) {
                    if (d.rootEdge) c.layout.DockRoot(c.dragWin, d.side);
                    else c.layout.Dock(c.dragWin, d.node, d.side);
                    Rebuild(c);
                }
            }
            c.kind = Ctx::None;
        } else if (c.moving) {
            c.drop = EvalDrop(c, m);
        }
    }
    if (c.kind == Ctx::None && !down && !released) { /* idle */ }

    // ---- draw ----
    const DrawList& dl = GetWindowDrawList();
    const Color32 panel = GetColorU32(Col_Panel), track = GetColorU32(Col_Track), emptyBg = GetColorU32(Col_DockingEmptyBg), border = GetColorU32(Col_Border), acc = GetColorU32(Col_Accent);
    const Color32 dim = GetColorU32(Col_Text, 0.62f), txt = GetColorU32(Col_Text);
    for (const DockLayout::Node& n : c.layout.Nodes()) {
        if (n.IsSplit()) continue;
        const Rect& r = n.rect;
        dl.AddRectFilled(V(r.x, r.y), V(r.x + r.w, r.y + r.h), n.tabs.empty() ? emptyBg : panel);
        if (!n.noTabBar) dl.AddRectFilled(V(r.x, r.y), V(r.x + r.w, r.y + std::min(c.tabH, r.h)), track);
        dl.AddRect(V(r.x, r.y), V(r.x + r.w, r.y + r.h), border);
        if (n.tabs.empty()) {
            const char* hint = "Drop windows here";
            const Vec2 ts = CalcTextSize(hint);
            dl.AddText(V(r.x + (r.w - ts.x) * 0.5f, r.y + (r.h - ts.y) * 0.5f), dim, hint);
        }
    }
    for (const TabRect& t : c.tabs) {
        const DockLayout::Node* n = c.layout.Find(t.node);
        int cur = n ? n->active : 0;
        const bool occluded = underFloat && In(t.r, m);
        if (occluded) BeginDisabled(true);
        const std::string label = Display(t.name);
        Tab((int)(HashName(t.name) | 0x20000000u), t.r.x, t.r.y, t.r.w, t.r.h, "", &cur, t.index);   // the tab body; the label is ours, so it clears the close box
        if (occluded) EndDisabled();
        const bool chosen = n && n->active == t.index, overTab = !underFloat && c.kind == Ctx::None && In(t.r, m);
        const bool dragged = c.kind == Ctx::TabDrag && c.dragWin == t.name;
        {   // the label, clipped to the tab minus its close box: the chosen tab and the one under the pointer in the text colour, the others dimmer
            const Vec2 ts = CalcTextSize(label.c_str());
            PushClipRect(t.r.x, t.r.y, std::max(1.0f, t.r.w - t.close.w), t.r.h);
            dl.AddText(V(t.r.x + TabTextInset(), t.r.y + (t.r.h - ts.y) * 0.5f), (chosen || overTab) && !dragged ? txt : dim, label.c_str());
            PopClipRect();
        }
        if (dragged) dl.AddRectFilled(V(t.r.x, t.r.y), V(t.r.x + t.r.w, t.r.y + t.r.h), ColorMulAlpha(track, 0.55f));   // its place while it is dragged away
        if (n && cur != n->active && c.kind != Ctx::WinDrag && c.kind != Ctx::TabDrag) c.layout.SetActive(n->tabs[(size_t)cur]);
        const bool hotClose = !underFloat && In(t.close, m);
        if (chosen || overTab || hotClose) {   // the close cross: on the chosen tab and the one under the pointer, a soft round chip under the pointer
            const float cx = t.close.x + t.close.w * 0.5f, cy = t.close.y + t.close.h * 0.5f, k = 3.5f;
            if (hotClose) dl.AddCircleFilled(V(cx, cy), std::min(8.0f, t.close.h * 0.4f), ColorMulAlpha(txt, 0.14f), 20);
            dl.AddLine(V(cx - k, cy - k), V(cx + k, cy + k), hotClose ? txt : dim, 1.6f);
            dl.AddLine(V(cx - k, cy + k), V(cx + k, cy - k), hotClose ? txt : dim, 1.6f);
        }
    }
    // the layout may have changed from a tab click; the content rectangles do not move, so no rebuild is needed
    for (const DockLayout::Node& n : c.layout.Nodes()) {
        if (!n.IsSplit()) continue;
        const Rect s = c.layout.SplitterRect(n.id);
        Rect hot = s;
        if (n.horizontal) { hot.x -= 2.0f; hot.w += 4.0f; } else { hot.y -= 2.0f; hot.h += 4.0f; }
        const bool over = (c.kind == Ctx::None && !underFloat && In(hot, m)) || (c.kind == Ctx::Splitter && c.splitNode == n.id);
        dl.AddRectFilled(V(s.x, s.y), V(s.x + s.w, s.y + s.h), over ? acc : border);
        if (over) SetMouseCursor(n.horizontal ? MouseCursor_ResizeEW : MouseCursor_ResizeNS);
    }
    if (c.drop.active || c.drop.floats) DrawGuides(c, c.drop);
    if (c.kind == Ctx::TabDrag) DrawTabGhost(c, m);
}
bool DockDragging() { const Ctx& c = C(); return c.kind == Ctx::TabDrag || (c.kind == Ctx::WinDrag && c.moving); }

bool DockBegin(const char* title, bool* p_open, int flags) {
    Ctx& c = C();
    if (c.inBegin) DockEnd();   // an unmatched DockBegin
    c.inBegin = true; c.began = c.clipped = c.floating = false; c.curName.clear(); c.pOpen = p_open;
    if (!title || !*title) { DockBuilderDropNext(); return false; }
    std::string name = title;
    if ((int)name.size() > DockLayout::kMaxNameLen) name.resize((size_t)DockLayout::kMaxNameLen);
    c.curName = name;
    for (size_t i = 0; i < c.closed.size(); ++i) if (c.closed[i] == name) {
        c.closed.erase(c.closed.begin() + (long)i);
        if (p_open) *p_open = false;
        DockBuilderDropNext();
        return false;
    }
    if (p_open && !*p_open) { DockBuilderDropNext(); return false; }
    c.seen[name] = GetFrameCount();   // submitted this frame: it has a tab
    DockBuilderApplyNext(name);   // SetNextWindowDockID
    if (!c.layout.Contains(name)) {
        const float k = (float)(c.layout.FloatingWindows().size() % 8) * 26.0f;
        if (!c.layout.AddFloating(name, Rect{c.area.x + 40.0f + k, c.area.y + 40.0f + k, 320.0f, 240.0f})) return false;   // layout full
    }
    const int node = c.layout.NodeOf(name);
    if (node) {
        const DockLayout::Node* n = c.layout.Find(node);
        if (!n || n->tabs.empty() || n->tabs[(size_t)n->active] != name) return false;   // not the chosen tab
        const Rect r = c.layout.ContentRect(node);
        SetNextWindowPos(V(r.x, r.y), Cond_Always);
        SetNextWindowSize(V(std::max(1.0f, r.w), std::max(1.0f, r.h)), Cond_Always);
        Begin(title, nullptr, flags | Window_NoMove | Window_NoResize | Window_NoTitleBar | Window_NoBringToFront);   // docked content sits below every floating window
        c.began = true;
        PushClipRect(r.x, r.y, std::max(1.0f, r.w), std::max(1.0f, r.h));
        c.clipped = true;
        return true;
    }
    // floating
    c.floating = true;
    const Vec2 m = GetMousePos();
    Rect pr;
    const bool place = c.layout.TakeDirty(name, &pr);
    const bool tear = c.kind == Ctx::WinDrag && c.fromTab && c.dragWin == name && GetFrameInput().mouseDown;
    if (tear) {
        SetNextWindowPos(V(m.x - c.grab.x, m.y - c.grab.y), Cond_Always);
        if (place) SetNextWindowSize(V(pr.w, pr.h), Cond_Always);
    } else if (place) {
        SetNextWindowPos(V(pr.x, pr.y), Cond_Always);
        SetNextWindowSize(V(pr.w, pr.h), Cond_Always);
    }
    const bool vis = Begin(title, p_open, flags);
    c.began = true;
    const Vec2 wp = GetWindowPos(), ws = GetWindowSize();
    const float th = GetFrameHeight();
    // a press on the title strip (between the collapse caret and the close button, and not through a window in front) starts a drag that can dock
    if (c.kind == Ctx::None && IsMouseClicked(0)) {
        const float caret = (flags & Window_NoCollapse) ? 0.0f : th;
        const Rect strip{wp.x + caret, wp.y, ws.x - caret - (p_open ? th : 0.0f), th};
        if (In(strip, m) && IsWindowHovered()) { c.kind = Ctx::WinDrag; c.dragWin = name; c.press = m; c.grab = V(m.x - wp.x, m.y - wp.y); c.moving = false; c.fromTab = false; }
    }
    return vis;
}

std::vector<DockTabInfo> GetDockTabs() {
    const Ctx& c = C();
    std::vector<DockTabInfo> out;
    for (const TabRect& t : c.tabs) {
        const DockLayout::Node* n = c.layout.Find(t.node);
        DockTabInfo i; i.window = t.name; i.node = t.node; i.index = t.index; i.rect = t.r; i.idealWidth = t.ideal;
        i.selected = n && n->active == t.index;
        out.push_back(i);
    }
    return out;
}
Rect GetDockArea() { return C().area; }
float GetDockTabHeight() { return C().tabH; }
void SetDockTrailingReserve(int leaf, float width) {
    Ctx& c = C();
    if (!(width > 0.0f) || !std::isfinite(width)) width = 0.0f;
    for (size_t i = 0; i < c.reserve.size(); ++i) if (c.reserve[i].first == leaf) { if (width == 0.0f) c.reserve.erase(c.reserve.begin() + (std::ptrdiff_t)i); else c.reserve[i].second = width; return; }
    if (width > 0.0f && c.reserve.size() < 64) c.reserve.push_back({leaf, width});
}
Rect GetDockStripRect(int leaf) {
    const Ctx& c = C();
    const DockLayout::Node* n = c.layout.Find(leaf);
    if (!n || n->IsSplit() || n->noTabBar || n->tabs.empty()) return Rect{};
    return Rect{n->rect.x, n->rect.y, n->rect.w, std::min(c.tabH, n->rect.h)};
}

bool DockAdoptTitle(const char* title) {
    if (!title || !*title) return false;
    DockLayout& L = C().layout;
    if (L.Contains(title)) return true;
    const char* key = std::strstr(title, "###");
    if (!key) return false;
    const std::size_t keyLen = std::strlen(key);
    for (const std::string& n : L.Windows()) {
        const std::size_t at = n.find("###");
        if (at != std::string::npos && n.size() - at == keyLen && n.compare(at, keyLen, key) == 0) return L.Rename(n, title);
    }
    return false;
}
bool DockClosedPending(const char* title) { if (!title) return false; for (const std::string& n : C().closed) if (n == title) return true; return false; }   // a tab was closed and its DockBegin has not been told yet
bool DockInsideBegin() { const Ctx& c = C(); return c.inBegin && !c.began; }   // DockBegin is in its own call to Begin: that Begin must not be routed back (SetDockAwareBegin)

void DockEnd() {
    Ctx& c = C();
    if (!c.inBegin) return;
    if (c.began) {
        if (c.floating) {
            const Vec2 wp = GetWindowPos(), ws = GetWindowSize();
            c.layout.SetFloatingRect(c.curName, Rect{wp.x, wp.y, ws.x, ws.y}, false);
            c.floatCur.push_back(Rect{wp.x, wp.y, ws.x, ws.y});
            if (c.pOpen && !*c.pOpen) c.layout.Close(c.curName);
        }
        if (c.clipped) PopClipRect();
        End();
    }
    c.inBegin = c.began = c.clipped = c.floating = false;
    c.pOpen = nullptr;
}

} // namespace OkayUI

// ---- What belongs to one context (OkayUI_ContextState.hpp: each context keeps its own copy of these) ----
namespace OkayUI {
OKAYUI_CONTEXT_STATE(
    OKAYUI_CTX_VAR(C()))
} // namespace OkayUI
