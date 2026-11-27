// The window table and the dock-node view (OkayUIInternals.hpp): what ported editor code reads as `ImGuiWindow*`, `ImGuiDockNode*`, `ImGuiTabBar*`
// and `g.Windows`, as plain values rebuilt from OkayUI's window slots and the dock layout. Own design; written from what the names mean.
// Rules (also in docs/ui/own_ui_toolkit.md 6.20):
//  * One table of up to 256 window entries and up to 256 node entries, allocated once, so a pointer keeps its address for the same window name
//    (nodes: for the same slot of the layout's node list) and a rebuild refreshes the fields in place.
//  * A rebuild happens when the frame number, the dock layout version or the dock geometry serial changed since the last one.
//  * Writes made to the snapshots are noticed at the start of the next rebuild (and by DockSpace) by comparing with what the rebuild handed out.
#include "OkayUI_ContextState.hpp"
#include "okay/UI/OkayUIInternals.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace OkayUI {

DockId DockNodeIdOf(int layoutNode);

namespace {

constexpr int kMaxWin = 256, kMaxNode = DockLayout::kMaxNodes;

unsigned HashStr(const char* s, size_t n, unsigned seed) {
    unsigned h = 2166136261u ^ seed;
    if (n == 0) { for (; s && *s; ++s) h = (h ^ (unsigned char)*s) * 16777619u; }
    else for (size_t i = 0; i < n; ++i) h = (h ^ (unsigned char)s[i]) * 16777619u;
    return h ? h : 1u;
}
unsigned TabIdOf(const std::string& name) { return HashStr(name.c_str(), 0, 0) | 0x20000000u; }   // the tab hash OkayUIDock.cpp uses for the same window

struct Seen { unsigned flagsApplied = 0; };   // WindowClass flags last applied, per window slot

struct Table {
    std::vector<WindowRef> wins;                 // capacity kMaxWin, never reallocated
    std::vector<int> winLastFlags;               // WindowClass.DockNodeFlagsOverrideSet as of the last rebuild
    std::vector<unsigned> winLastDockId;
    std::unordered_map<std::string, int> byName;
    std::vector<DockNodeRef> nodes;              // capacity kMaxNode, never reallocated
    std::vector<TabBarRef> bars;
    std::vector<Vec2> nodeSizeRefHanded;         // SizeRef as handed out, per node slot
    std::vector<unsigned> nodeFirstWinHanded;    // ID of Windows[0] as handed out, per node slot (0: none)
    std::vector<int> nodeLayoutId;               // the layout node each slot stood for
    int nodeCount = 0;
    WindowRef host;                              // "##DockSpaceHost"
    ImVector<WindowRef*> order;
    ImVector<StoragePairRef> pairs;
    bool built = false;
    int stampFrame = -1; unsigned stampVersion = ~0u;
    unsigned stampSerial = ~0u;
    // plan nodes handed out by DockBuilderGetNode when the id is not live (a small ring)
    DockNodeRef planNodes[4]; int planNext = 0;
    // an amended strip
    DockNodeRef* amend = nullptr; int amendLayout = 0; bool amendBegan = false;
    unsigned spaceOwner = 0; int spaceOwnerFrame = -1;   // the window DockSpace(id, ...) was called in (the editor's ##OkayDockHost), and when
    Table() { wins.resize(kMaxWin); winLastFlags.assign(kMaxWin, 0); winLastDockId.assign(kMaxWin, 0); nodes.resize(kMaxNode); bars.resize(kMaxNode); nodeSizeRefHanded.assign(kMaxNode, Vec2{}); nodeFirstWinHanded.assign(kMaxNode, 0u); nodeLayoutId.assign(kMaxNode, 0); }
};
Table& T() { static Table t; return t; }
int g_winCount = 0;
unsigned g_dockSerial = 0;   // bumped by DockSpace (through GetDockArea changes); see Sync

WindowRef* Slot(Table& t, const std::string& name) {
    auto it = t.byName.find(name);
    if (it != t.byName.end()) return &t.wins[(size_t)it->second];
    if (g_winCount >= kMaxWin) return nullptr;
    const int i = g_winCount++;
    t.byName[name] = i;
    WindowRef& w = t.wins[(size_t)i];
    w = WindowRef();
    std::snprintf(w.Name, sizeof w.Name, "%s", name.c_str());
    w.ID = WindowIdOfTitle(w.Name);
    w.RootWindow = &w;
    return &w;
}
int Index(const Table& t, const WindowRef* w) { return w ? (int)(w - t.wins.data()) : -1; }

void FillRect(ImRect& r, Vec2 mn, Vec2 sz) { r.Min = mn; r.Max = Vec2{mn.x + sz.x, mn.y + sz.y}; }

}   // namespace

WindowDC::WidthRef& WindowDC::WidthRef::operator=(float f) { v = f; SetWindowItemWidthDefault(f); return *this; }

unsigned ImHashStr(const char* data, size_t size, unsigned seed) { return HashStr(data, size, seed); }

// ---- applying writes ---------------------------------------------------------------------------------------------------------------

void ApplyDockNodeEdits() {
    Table& t = T();
    if (!t.built) return;
    DockLayout& L = GetDockLayout();
    // 1. window class flags -> tab strips
    bool flagsChanged = false;
    for (int i = 0; i < g_winCount; ++i) {
        const int now = t.wins[(size_t)i].WindowClass.DockNodeFlagsOverrideSet;
        if (now != t.winLastFlags[(size_t)i]) { flagsChanged = true; t.winLastFlags[(size_t)i] = now; }
    }
    // (the strips are set from the flags of all windows below, every time: a rebuilt layout forgets them)
    for (const DockLayout::Node& n : L.Nodes()) {
        if (n.IsSplit()) continue;
        bool hide = false;
        for (const std::string& w : n.tabs) { auto it = t.byName.find(w); if (it != t.byName.end() && (t.wins[(size_t)it->second].WindowClass.DockNodeFlagsOverrideSet & DockNode_NoTabBar)) hide = true; }
        if (n.noTabBar != hide) L.SetNoTabBar(n.id, hide);
    }
    (void)flagsChanged;
    // 2. node edits
    for (int i = 0; i < t.nodeCount; ++i) {
        DockNodeRef& nd = t.nodes[(size_t)i];
        const int ln = t.nodeLayoutId[(size_t)i];
        const DockLayout::Node* node = L.Find(ln);
        if (!node) continue;
        const Vec2 was = t.nodeSizeRefHanded[(size_t)i];
        if (nd.ParentNode && (nd.SizeRef.x != was.x || nd.SizeRef.y != was.y)) {
            const DockNodeRef* par = nd.ParentNode;
            const int ax = par->SplitAxis == Axis_Y ? 1 : 0;
            Vec2 want = Vec2{nd.Size.x, nd.Size.y};
            want[ax] = nd.SizeRef[ax];
            SetLiveNodeSize(ln, want);
            t.nodeSizeRefHanded[(size_t)i] = nd.SizeRef;
        }
        if (!node->IsSplit() && nd.Windows.Size > 0) {
            const WindowRef* first = nd.Windows.Data[0];
            if (first && first->ID != t.nodeFirstWinHanded[(size_t)i]) { L.SetActive(first->Name); t.nodeFirstWinHanded[(size_t)i] = first->ID; }
        }
    }
}

// ---- the rebuild -------------------------------------------------------------------------------------------------------------------

namespace {

void Rebuild(Table& t) {
    const DockLayout& L = GetDockLayout();
    const int frame = GetFrameCount();
    const std::vector<DockTabInfo> tabs = GetDockTabs();
    const Rect area = GetDockArea();
    // ---- windows ----
    WindowSlotInfo slots[128];
    const int ns = ListWindowSlots(slots, 128);
    for (int i = 0; i < g_winCount; ++i) { WindowRef& w = t.wins[(size_t)i]; w.Active = w.WasActive = false; w.DockNode = nullptr; w.DockIsActive = w.DockTabIsVisible = false; w.DockOrder = -1; w.DockNode = nullptr; FillRect(w.DC.DockTabItemRect, Vec2{0, 0}, Vec2{0, 0}); w.TabId = TabIdOf(w.Name); }
    const float titleH = GetFrameHeight();
    for (int i = 0; i < ns; ++i) {
        WindowRef* w = Slot(t, slots[i].title);
        if (!w) continue;
        w->ID = slots[i].id; w->Pos = slots[i].pos; w->Size = slots[i].size;
        w->Active = slots[i].lastFrame == (unsigned long long)frame;
        w->WasActive = slots[i].lastFrame + 1 >= (unsigned long long)frame;
    }
    for (const std::string& name : L.Windows()) {   // windows the layout holds, drawn or not
        WindowRef* w = Slot(t, name);
        if (!w) continue;
        if (!w->Active && !w->WasActive && w->Size.x == 0.0f) {
            if (const DockLayout::Node* n = L.Find(L.NodeOf(name))) { const Rect r = L.ContentRect(n->id); w->Pos = Vec2{r.x, r.y}; w->Size = Vec2{r.w, r.h}; }
        }
    }
    for (int i = 0; i < g_winCount; ++i) {
        WindowRef& w = t.wins[(size_t)i];
        w.DrawList = const_cast<DrawListHandle*>(&GetWindowDrawList());
        const bool docked = L.Contains(w.Name) && !L.IsFloating(w.Name);
        FillRect(w.InnerClipRect, Vec2{w.Pos.x, w.Pos.y + (docked ? 0.0f : titleH)}, Vec2{w.Size.x, std::max(0.0f, w.Size.y - (docked ? 0.0f : titleH))});
        w.WindowBorderSize = docked ? 0.0f : 1.0f;
        w.WindowClass.DockNodeFlagsOverrideSet = t.winLastFlags[(size_t)i];   // what the host wrote last is kept; it is re-applied to the layout by ApplyDockNodeEdits
    }
    // the dock host
    {
        WindowRef& h = t.host;
        if (!h.Name[0]) { std::snprintf(h.Name, sizeof h.Name, "##DockSpaceHost"); h.ID = WindowIdOfTitle(h.Name); h.RootWindow = &h; h.WindowBorderSize = 0.0f; }
        h.Pos = Vec2{area.x, area.y}; h.Size = Vec2{area.w, area.h}; h.Active = h.WasActive = area.w > 0.0f;
        h.DrawList = const_cast<DrawListHandle*>(&GetDockHostDrawList());   // drawn under the floating windows whenever it is drawn
        FillRect(h.InnerClipRect, h.Pos, h.Size);
    }
    // ---- nodes ----
    const std::vector<DockLayout::Node>& ln = L.Nodes();
    t.nodeCount = std::min<int>((int)ln.size(), kMaxNode);
    std::unordered_map<int, int> slotOf;
    for (int i = 0; i < t.nodeCount; ++i) slotOf[ln[(size_t)i].id] = i;
    t.pairs.clear();
    for (int i = 0; i < t.nodeCount; ++i) {
        const DockLayout::Node& n = ln[(size_t)i];
        DockNodeRef& d = t.nodes[(size_t)i];
        TabBarRef& bar = t.bars[(size_t)i];
        d.ID = DockNodeIdOf(n.id);
        t.nodeLayoutId[(size_t)i] = n.id;
        d.ParentNode = nullptr; d.ChildNodes[0] = d.ChildNodes[1] = nullptr; d.Windows.clear(); d.TabBar = nullptr;
        d.HostWindow = &t.host;
        d.Pos = Vec2{n.rect.x, n.rect.y}; d.Size = Vec2{n.rect.w, n.rect.h}; d.SizeRef = d.Size;
        t.nodeSizeRefHanded[(size_t)i] = d.SizeRef;
        d.SplitAxis = n.IsSplit() ? (n.horizontal ? Axis_X : Axis_Y) : Axis_None;
        d.LocalFlags = (n.parent == 0 ? DockNode_DockSpace : 0) | (n.noTabBar ? DockNode_NoTabBar : 0);
        d.MergedFlags = d.LocalFlags;
        d.WantLockSizeOnce = false;
        d.LastFrameActive = frame;
        d.IsVisible = !n.IsSplit() && !n.tabs.empty() && n.rect.w > 0.0f && n.rect.h > 0.0f;
        if (n.parent) { auto it = slotOf.find(n.parent); if (it != slotOf.end()) d.ParentNode = &t.nodes[(size_t)it->second]; }
        for (int k = 0; k < 2; ++k) if (n.child[k]) { auto it = slotOf.find(n.child[k]); if (it != slotOf.end()) d.ChildNodes[k] = &t.nodes[(size_t)it->second]; }
        if (!n.IsSplit()) {
            for (size_t k = 0; k < n.tabs.size(); ++k) {
                WindowRef* w = Slot(t, n.tabs[k]);
                if (!w) continue;
                d.Windows.push_back(w);
                w->DockNode = &d; w->DockId = d.ID; w->DockIsActive = true; w->DockOrder = (int)k; w->DockTabIsVisible = (int)k == n.active;
                t.winLastDockId[(size_t)Index(t, w)] = d.ID;
            }
            t.nodeFirstWinHanded[(size_t)i] = d.Windows.Size ? d.Windows.Data[0]->ID : 0u;
            if (!n.noTabBar && !n.tabs.empty() && n.rect.w > 0.0f) {
                bar = TabBarRef();
                bar.ID = d.ID ^ 0x7ab0u;
                const Rect strip = GetDockStripRect(n.id);
                FillRect(bar.BarRect, Vec2{strip.x, strip.y}, Vec2{strip.w, strip.h});
                float ideal = 0.0f;
                for (const DockTabInfo& ti : tabs) {
                    if (ti.node != n.id) continue;
                    TabItemRef item; item.ID = TabIdOf(ti.window); item.Window = Slot(t, ti.window);
                    item.Offset = ti.rect.x - strip.x; item.Width = ti.rect.w; item.ContentWidth = ti.idealWidth;
                    bar.Tabs.push_back(item);
                    ideal += ti.idealWidth;
                    if (item.Window) { FillRect(item.Window->DC.DockTabItemRect, Vec2{ti.rect.x, ti.rect.y}, Vec2{ti.rect.w, ti.rect.h}); }
                    if (ti.selected) bar.VisibleTabId = bar.SelectedTabId = item.ID;
                }
                bar.WidthAllTabsIdeal = ideal;
                d.TabBar = &bar;
            }
        }
        StoragePairRef pr; pr.key = d.ID; pr.val_p = &d; t.pairs.push_back(pr);
    }
    // ---- the ordered list ----
    t.order.clear();
    struct Ent { unsigned long long z; int i; };
    std::vector<Ent> ents;
    for (int i = 0; i < g_winCount; ++i) {
        unsigned long long z = 0;
        for (int k = 0; k < ns; ++k) if (slots[k].id == t.wins[(size_t)i].ID) { z = slots[k].z; break; }
        ents.push_back(Ent{z, i});
    }
    std::stable_sort(ents.begin(), ents.end(), [](const Ent& a, const Ent& b) { return a.z < b.z; });
    for (const Ent& e : ents) t.order.push_back(&t.wins[(size_t)e.i]);
    t.built = true;
}

void Sync(bool force = false) {
    Table& t = T();
    const int frame = GetFrameCount();
    const unsigned ver = GetDockLayout().Version();
    const Rect a = GetDockArea();
    unsigned serial = (unsigned)(a.x * 7.0f + a.y * 13.0f + a.w * 31.0f + a.h * 57.0f) ^ (unsigned)GetDockTabs().size() ^ (DockNodeIdOf(GetDockLayout().Root()) * 2654435761u);
    if (!force && t.built && t.stampFrame == frame && t.stampVersion == ver && t.stampSerial == serial) return;
    ApplyDockNodeEdits();
    Rebuild(t);
    t.stampFrame = frame; t.stampVersion = GetDockLayout().Version(); t.stampSerial = serial;
}

}   // namespace

// ---- public lookups ----------------------------------------------------------------------------------------------------------------

WindowRef* FindWindowRefByName(const char* title) {
    if (!title || !*title) return nullptr;
    Sync();
    Table& t = T();
    auto it = t.byName.find(title);
    return it == t.byName.end() ? nullptr : &t.wins[(size_t)it->second];
}
WindowRef* FindWindowRefByID(unsigned id) {
    if (id == 0) return nullptr;
    Sync();
    Table& t = T();
    for (int i = 0; i < g_winCount; ++i) if (t.wins[(size_t)i].ID == id) return &t.wins[(size_t)i];
    return id == t.host.ID ? &t.host : nullptr;
}
CurrentWindow::operator WindowRef*() const { return valid ? FindWindowRefByName(title) : nullptr; }

DockNodeRef* GetWindowDockNode() {
    const CurrentWindow w = GetCurrentWindow();
    if (!w.valid) return nullptr;
    const WindowRef* r = FindWindowRefByName(w.title);
    return r ? r->DockNode : nullptr;
}
DockNodeRef* DockNodeGetRootNode(const DockNodeRef* node) {
    for (int guard = 0; node && node->ParentNode && guard < 128; ++guard) node = node->ParentNode;
    return const_cast<DockNodeRef*>(node);
}
DockNodeRef* DockBuilderGetNode(DockId id) {
    if (id == 0) return nullptr;
    Sync();
    Table& t = T();
    for (int i = 0; i < t.nodeCount; ++i) if (t.nodes[(size_t)i].ID == id) return &t.nodes[(size_t)i];
    const DockNodeInfo info = DockBuilderGetNodeInfo(id);
    if (!info.found) return nullptr;
    // a plan node that is not in the live layout: windows from the plan, geometry from the plan, no strip
    DockNodeRef& d = t.planNodes[t.planNext]; t.planNext = (t.planNext + 1) & 3;
    d = DockNodeRef();
    d.ID = id; d.HostWindow = &t.host; d.Size = d.SizeRef = info.size; d.SplitAxis = info.isSplit ? (info.horizontal ? Axis_X : Axis_Y) : Axis_None;
    d.LocalFlags = d.MergedFlags = info.isRoot ? DockNode_DockSpace : 0;
    for (const std::string& w : info.windows) if (WindowRef* r = FindWindowRefByName(w.c_str())) d.Windows.push_back(r);
    return &d;
}
bool DockNodeBeginAmendTabBar(DockNodeRef* node) {
    Table& t = T();
    if (!node || !node->TabBar || node->IsNoTabBar() || t.amend) return false;
    const DockLayout& L = GetDockLayout();
    int ln = 0;
    for (int i = 0; i < t.nodeCount; ++i) if (&t.nodes[(size_t)i] == node) ln = t.nodeLayoutId[(size_t)i];
    if (!ln || !L.Find(ln)) return false;
    const Rect strip = GetDockStripRect(ln);
    if (!(strip.w > 0.0f)) return false;
    t.amend = node; t.amendLayout = ln; t.amendBegan = false;
    if (!GetCurrentWindow().valid) {   // outside any window: a see-through one over the strip, at the back, to hold the button
        char name[48]; std::snprintf(name, sizeof name, "##tabamend%d", ln);
        SetNextWindowPos(Vec2{strip.x, strip.y}, Cond_Always);
        SetNextWindowSize(Vec2{strip.w, strip.h}, Cond_Always);
        SetNextWindowBgAlpha(0.0f);
        Begin(name, nullptr, Window_NoDecoration | Window_NoBringToFront | Window_NoMove | Window_NoSavedSettings);
        t.amendBegan = true;
    }
    SetTabAmend(true, strip);
    return true;
}
void DockNodeEndAmendTabBar() {
    Table& t = T();
    if (!t.amend) return;
    SetDockTrailingReserve(t.amendLayout, TabAmendUsed());   // the strip leaves this room from the next DockSpace on
    SetTabAmend(false, Rect{});
    if (t.amendBegan) End();
    t.amend = nullptr; t.amendBegan = false; t.amendLayout = 0;
}
TabItemRef* TabBarFindTabByID(TabBarRef* bar, unsigned tabId) {
    if (!bar) return nullptr;
    for (TabItemRef& it : bar->Tabs) if (it.ID == tabId) return &it;
    return nullptr;
}

DockId DockSpace(DockId id, Vec2 size, int flags) {
    const Vec2 p = GetCursorScreenPos(), av = GetContentRegionAvail();
    float w = size.x > 0.0f ? size.x : av.x + size.x, h = size.y > 0.0f ? size.y : av.y + size.y;
    w = std::isfinite(w) ? std::max(1.0f, w) : 1.0f; h = std::isfinite(h) ? std::max(1.0f, h) : 1.0f;
    SetDockSpaceId(id);
    {   // the window the dock space sits in: over the dock area it is not the hovered window, the dock host is (FillContextFromWindowTable)
        const CurrentWindow cw = GetCurrentWindow();
        Table& t = T();
        t.spaceOwner = cw.valid ? cw.id : 0u; t.spaceOwnerFrame = GetFrameCount();
    }
    Dummy(Vec2{w, h});
    if (flags & DockNode_NoTabBar) {   // the strips of the whole tree
        DockLayout& L = GetDockLayout();
        std::vector<int> leaves;
        for (const DockLayout::Node& n : L.Nodes()) if (!n.IsSplit()) leaves.push_back(n.id);
        for (int l : leaves) L.SetNoTabBar(l, true);
    }
    DockSpace(Rect{p.x, p.y, w, h});
    return id;
}

void FillContextFromWindowTable(Context& c, unsigned hoveredId, unsigned activeId, unsigned currentId, unsigned focusedId) {
    Sync(true);
    Table& t = T();
    auto byId = [&](unsigned id) -> WindowRef* { if (!id) return nullptr; for (int i = 0; i < g_winCount; ++i) if (t.wins[(size_t)i].ID == id) return &t.wins[(size_t)i]; return nullptr; };
    c.HoveredWindow = byId(hoveredId);
    // The window the dock space was drawn in (a full-window host, as the editor's) is the topmost window under the strips and the empty
    // nodes: there the dock host is hovered, as in Dear ImGui (the editor's tab menu checks g.HoveredWindow == node->HostWindow).
    if (c.HoveredWindow && t.spaceOwner != 0 && c.HoveredWindow->ID == t.spaceOwner && t.spaceOwnerFrame + 1 >= GetFrameCount() && t.host.Size.x > 0.0f) {
        const Vec2 m = GetMousePos();
        if (m.x >= t.host.Pos.x && m.y >= t.host.Pos.y && m.x < t.host.Pos.x + t.host.Size.x && m.y < t.host.Pos.y + t.host.Size.y) c.HoveredWindow = nullptr;
    }
    if (!c.HoveredWindow && t.host.Size.x > 0.0f && !GetFrameInput().blocked) {   // over the dock space (its strips and empty nodes): the host
        const Vec2 m = GetMousePos();
        if (m.x >= t.host.Pos.x && m.y >= t.host.Pos.y && m.x < t.host.Pos.x + t.host.Size.x && m.y < t.host.Pos.y + t.host.Size.y) c.HoveredWindow = &t.host;
    }
    c.ActiveIdWindow = byId(activeId);
    c.CurrentWindow = byId(currentId);
    c.NavWindow = byId(focusedId);
    c.Windows = t.order;
    c.Style = GetStyle();
    c.DockContext.Nodes.Data = t.pairs;
    {   // open popups as window entries
        static WindowRef popRefs[8];
        PopupSlotInfo pops[8];
        const int np = ListOpenPopups(pops, 8);
        c.OpenPopupStack.clear();
        for (int i = 0; i < np; ++i) {
            WindowRef& w = popRefs[i];
            w = WindowRef(); w.ID = pops[i].id; w.Pos = pops[i].pos; w.Size = pops[i].size; w.Active = w.WasActive = true; w.RootWindow = &w;
            w.Flags = pops[i].modal ? Window_Modal : 0;
            PopupDataRef pr; pr.Window = &w; pr.PopupId = pops[i].id;
            c.OpenPopupStack.push_back(pr);
        }
    }
    // the strip of the chosen window being the focused one
    for (int i = 0; i < t.nodeCount; ++i) {
        DockNodeRef& d = t.nodes[(size_t)i];
        if (!d.TabBar) continue;
        bool focused = false;
        for (WindowRef* w : d.Windows) if (w->DockTabIsVisible && w->ID == focusedId) focused = true;
        d.TabBar->Flags = focused ? TabBar_IsFocused : 0;
    }
}

}   // namespace OkayUI

// ---- What belongs to one context (OkayUI_ContextState.hpp: each context keeps its own copy of these) ----
namespace OkayUI {
OKAYUI_CONTEXT_STATE(
    OKAYUI_CTX_VAR(g_dockSerial),
    OKAYUI_CTX_VAR(g_winCount),
    OKAYUI_CTX_VAR(T()))
} // namespace OkayUI
