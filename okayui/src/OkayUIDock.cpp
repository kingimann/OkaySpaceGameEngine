// OkayUI docking: the dock space, tab strips, splitters, drag to float / dock, drop-target preview.
// The data model is in OkayUIDockLayout.cpp. See okay/UI/OkayUIDock.hpp.
#include "okay/UI/OkayUIDock.hpp"

#include <algorithm>
#include <cmath>

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

struct TabRect { std::string name; int node = 0; int index = 0; Rect r, close; };

struct Ctx {
    DockLayout layout;
    long long spaceFrame = -10;
    Rect area;
    float tabH = 24.0f;
    std::vector<TabRect> tabs;
    std::vector<Rect> floatPrev, floatCur;
    std::vector<std::string> closed;

    enum Kind { None, Splitter, TabPress, WinDrag } kind = None;
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
};
Ctx& C() { static Ctx c; return c; }

void BuildTabs(Ctx& c) {
    c.tabs.clear();
    const float pad = 10.0f, closeW = c.tabH * 0.7f;
    for (const DockLayout::Node& n : c.layout.Nodes()) {
        if (n.IsSplit() || n.tabs.empty()) continue;
        std::vector<float> w;
        float total = 0.0f;
        for (const std::string& t : n.tabs) { const float tw = CalcTextSize(Display(t).c_str()).x + 2.0f * pad + closeW; w.push_back(tw); total += tw; }
        const float scale = total > n.rect.w && total > 0.0f ? n.rect.w / total : 1.0f;
        float x = n.rect.x;
        for (size_t i = 0; i < n.tabs.size(); ++i) {
            TabRect t;
            t.name = n.tabs[i]; t.node = n.id; t.index = (int)i;
            const float tw = w[i] * scale;
            t.r = Rect{x, n.rect.y, tw, std::min(c.tabH, n.rect.h)};
            const float cw = std::min(closeW, tw * 0.4f);
            t.close = Rect{x + tw - cw, t.r.y, cw, t.r.h};
            c.tabs.push_back(t);
            x += tw;
        }
    }
}
void Rebuild(Ctx& c) { c.layout.Compute(c.area, c.tabH); BuildTabs(c); }

DockDropInfo EvalDrop(Ctx& c, Vec2 m) {
    DockDropInfo d;
    d.window = c.dragWin;
    const Rect& a = c.area;
    if (!In(a, m)) return d;
    const float band = 10.0f;
    const int root = c.layout.Root();
    const bool rootEmpty = c.layout.Find(root) && !c.layout.Find(root)->IsSplit() && c.layout.Find(root)->tabs.empty();
    if (!rootEmpty) {
        DockSide s = DockSide_Center;
        if (m.x < a.x + band) s = DockSide_Left;
        else if (m.x >= a.x + a.w - band) s = DockSide_Right;
        else if (m.y < a.y + band) s = DockSide_Top;
        else if (m.y >= a.y + a.h - band) s = DockSide_Bottom;
        if (s != DockSide_Center) {
            d.active = true; d.node = root; d.side = s; d.rootEdge = true;
            const float fw = a.w * 0.25f, fh = a.h * 0.25f;
            d.preview = s == DockSide_Left ? Rect{a.x, a.y, fw, a.h} : s == DockSide_Right ? Rect{a.x + a.w - fw, a.y, fw, a.h}
                      : s == DockSide_Top ? Rect{a.x, a.y, a.w, fh} : Rect{a.x, a.y + a.h - fh, a.w, fh};
            return d;
        }
    }
    const int leaf = c.layout.LeafAt(m.x, m.y);
    if (!leaf) return d;
    const DockLayout::Node* n = c.layout.Find(leaf);
    const Rect& r = n->rect;
    d.active = true; d.node = leaf; d.side = DockSide_Center; d.preview = r;
    if (n->tabs.empty()) return d;
    if (m.y < r.y + c.tabH) return d;               // over the tab strip: join the tabs
    const float u = (m.x - r.x) / std::max(1.0f, r.w), v = (m.y - (r.y + c.tabH)) / std::max(1.0f, r.h - c.tabH);
    const float dl = u, dr = 1.0f - u, dt = v, db = 1.0f - v;
    const float best = std::min(std::min(dl, dr), std::min(dt, db));
    if (best >= 0.25f) return d;
    const Rect cr = c.layout.ContentRect(leaf);
    if (best == dl) { d.side = DockSide_Left;   d.preview = Rect{r.x, cr.y, r.w * 0.5f, cr.h}; }
    else if (best == dr) { d.side = DockSide_Right;  d.preview = Rect{r.x + r.w * 0.5f, cr.y, r.w * 0.5f, cr.h}; }
    else if (best == dt) { d.side = DockSide_Top;    d.preview = Rect{r.x, cr.y, r.w, cr.h * 0.5f}; }
    else { d.side = DockSide_Bottom; d.preview = Rect{r.x, cr.y + cr.h * 0.5f, r.w, cr.h * 0.5f}; }
    return d;
}

void DrawGuides(Ctx& c, const DockDropInfo& d) {
    const DrawList& fg = GetForegroundDrawList();
    const Color32 acc = GetColorU32(Col_Accent), accSoft = GetColorU32(Col_DockingPreview), border = GetColorU32(Col_Border), panel = GetColorU32(Col_Panel);
    fg.AddRectFilled(V(d.preview.x, d.preview.y), V(d.preview.x + d.preview.w, d.preview.y + d.preview.h), accSoft);
    fg.AddRect(V(d.preview.x, d.preview.y), V(d.preview.x + d.preview.w, d.preview.y + d.preview.h), acc, 0.0f, 2.0f);
    // the five guide boxes over the leaf under the pointer
    const DockLayout::Node* n = d.node ? c.layout.Find(d.node) : nullptr;
    if (d.rootEdge || !n || n->IsSplit() || n->id == 0) return;
    const Rect r = c.layout.ContentRect(n->id);
    const float s = std::max(8.0f, std::min(26.0f, std::min(r.w, r.h) / 6.0f)), g = s + 6.0f;
    const Vec2 ctr = V(r.x + r.w * 0.5f, r.y + r.h * 0.5f);
    struct G { DockSide side; float dx, dy; } gs[5] = {{DockSide_Center, 0, 0}, {DockSide_Left, -g, 0}, {DockSide_Right, g, 0}, {DockSide_Top, 0, -g}, {DockSide_Bottom, 0, g}};
    for (const G& q : gs) {
        const bool on = q.side == d.side && d.node == n->id;
        const Vec2 a = V(ctr.x + q.dx - s * 0.5f, ctr.y + q.dy - s * 0.5f), b = V(a.x + s, a.y + s);
        fg.AddRectFilled(a, b, on ? acc : panel);
        fg.AddRect(a, b, border);
        if (q.side != DockSide_Center) {   // a half-filled box tells which side
            const Vec2 h0 = q.side == DockSide_Right ? V(a.x + s * 0.5f, a.y) : a;
            const Vec2 h1 = q.side == DockSide_Left ? V(a.x + s * 0.5f, b.y) : (q.side == DockSide_Top ? V(b.x, a.y + s * 0.5f) : b);
            const Vec2 h2 = q.side == DockSide_Bottom ? V(a.x, a.y + s * 0.5f) : h0;
            fg.AddRectFilled(h2, h1, on ? panel : acc);
        }
    }
}

} // namespace

DockLayout& GetDockLayout() { return C().layout; }
DockDropInfo GetDockDrop() { return C().drop; }

namespace { int g_nextDockNode = 0; int g_nextDockCond = 0; }
void DockReset() {
    Ctx& c = C();
    c = Ctx();
    g_nextDockNode = 0;
    DockBuilderReset();
}

std::string DockSaveLayout() { return C().layout.Save(); }
bool DockLoadLayout(const std::string& blob, int* repairs) {
    Ctx& c = C();
    const bool ok = c.layout.Load(blob, repairs);
    if (ok) { c.kind = Ctx::None; c.drop = DockDropInfo(); }
    return ok;
}

void DockSpace(Rect area) {
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
                } else {
                    const Rect cr = c.layout.ContentRect(n->id);
                    const Rect fr{m.x - c.grab.x, m.y - c.grab.y, std::max(220.0f, cr.w), std::max(160.0f, cr.h)};
                    c.layout.Float(c.dragWin, fr);
                    c.kind = Ctx::WinDrag; c.moving = true; c.fromTab = true;
                    Rebuild(c);
                }
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
    const Color32 dim = GetColorU32(Col_Text, 0.55f), txt = GetColorU32(Col_Text);
    for (const DockLayout::Node& n : c.layout.Nodes()) {
        if (n.IsSplit()) continue;
        const Rect& r = n.rect;
        dl.AddRectFilled(V(r.x, r.y), V(r.x + r.w, r.y + r.h), n.tabs.empty() ? emptyBg : panel);
        dl.AddRectFilled(V(r.x, r.y), V(r.x + r.w, r.y + std::min(c.tabH, r.h)), track);
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
        {   // the label, clipped to the tab minus its close box
            const Vec2 ts = CalcTextSize(label.c_str());
            PushClipRect(t.r.x, t.r.y, std::max(1.0f, t.r.w - t.close.w), t.r.h);
            dl.AddText(V(t.r.x + 10.0f, t.r.y + (t.r.h - ts.y) * 0.5f), txt, label.c_str());
            PopClipRect();
        }
        if (n && cur != n->active && c.kind != Ctx::WinDrag) c.layout.SetActive(n->tabs[(size_t)cur]);
        const bool hotClose = !underFloat && In(t.close, m);
        const Vec2 xs = CalcTextSize("x");
        if (hotClose) dl.AddRectFilled(V(t.close.x, t.close.y + 2.0f), V(t.close.x + t.close.w, t.close.y + t.close.h - 2.0f), ColorMulAlpha(acc, 0.35f));
        dl.AddText(V(t.close.x + (t.close.w - xs.x) * 0.5f, t.close.y + (t.close.h - xs.y) * 0.5f), hotClose ? txt : dim, "x");
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
    if (c.drop.active) DrawGuides(c, c.drop);
}

void SetNextWindowDockID(int node, Cond cond) { g_nextDockNode = node; g_nextDockCond = (int)cond; }
bool IsWindowDocked() { Ctx& c = C(); return c.inBegin && c.began && c.layout.NodeOf(c.curName) != 0; }

bool DockBegin(const char* title, bool* p_open, int flags) {
    Ctx& c = C();
    const int wantNode = g_nextDockNode; const int wantCond = g_nextDockCond;   // SetNextWindowDockID applies to this window only
    g_nextDockNode = 0;
    if (c.inBegin) DockEnd();   // an unmatched DockBegin
    c.inBegin = true; c.began = c.clipped = c.floating = false; c.curName.clear(); c.pOpen = p_open;
    if (!title || !*title) return false;
    std::string name = title;
    if ((int)name.size() > DockLayout::kMaxNameLen) name.resize((size_t)DockLayout::kMaxNameLen);
    c.curName = name;
    for (size_t i = 0; i < c.closed.size(); ++i) if (c.closed[i] == name) {
        c.closed.erase(c.closed.begin() + (long)i);
        if (p_open) *p_open = false;
        return false;
    }
    if (p_open && !*p_open) return false;
    bool isNew = false;
    if (!c.layout.Contains(name)) {
        const float k = (float)(c.layout.FloatingWindows().size() % 8) * 26.0f;
        if (!c.layout.AddFloating(name, Rect{c.area.x + 40.0f + k, c.area.y + 40.0f + k, 320.0f, 240.0f})) return false;   // layout full
        isNew = true;
    }
    if (wantNode && (wantCond == Cond_Always || isNew) && c.layout.Find(wantNode) && !c.layout.Find(wantNode)->IsSplit() && c.layout.NodeOf(name) != wantNode)
        c.layout.Dock(name, wantNode, DockSide_Center);
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
