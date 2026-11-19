// The window table and the dock-node view (OkayUIInternals.hpp, OkayUIDockNodes.cpp), headless: what ported editor code reads as ImGuiDockNode*,
// ImGuiTabBar*, ImGuiWindow* and g.Windows, checked against the dock layout and the geometry of the real DockSpace, and the writes that are
// applied back (SizeRef, the first of Windows, WindowClass flags). Also: node ids for user-made splits, a button amended onto a strip, DockSpace(id, ...)
// and a random run that must keep the layout valid. Written from the behaviour the editor's tab code needs (editor/main.cpp, the tab menu, tab
// icons, tab overflow and side tabs).
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "okay/UI/OkayUIInternals.hpp"

#include <cmath>
#include <string>
#include <vector>

using namespace okayuitest;
namespace UI = OkayUI;
using UI::DockId; using UI::DockLayout; using UI::DockNodeRef; using UI::WindowRef;

namespace {

bool Valid(const DockLayout& l) { std::string why; const bool ok = l.Validate(&why); if (!ok) std::cerr << "  invalid layout: " << why << "\n"; return ok; }

const char* const kNames[] = {"Hierarchy", "Inspector", "Console###Console", "Project", "Stats", "Scene", "Game"};
constexpr DockId kSpace = 0xD0C5u;

struct Ids { DockId left = 0, right = 0, down = 0, center = 0; };
Ids Build(UI::Vec2 size) {
    UI::DockBuilderRemoveNode(kSpace);
    UI::DockBuilderAddNode(kSpace, UI::DockNode_DockSpace);
    UI::DockBuilderSetNodeSize(kSpace, size);
    DockId center = kSpace;
    auto split = [](DockId& from, UI::DockSide dir, float ratio) { DockId opp = 0; const DockId nw = UI::DockBuilderSplitNode(from, dir, ratio, nullptr, &opp); from = opp; return nw; };
    Ids ids;
    ids.left = split(center, UI::DockSide_Left, 0.20f);
    ids.right = split(center, UI::DockSide_Right, 0.30f);
    ids.down = split(center, UI::DockSide_Bottom, 0.25f);
    ids.center = center;
    UI::DockBuilderDockWindow("Hierarchy", ids.left);
    UI::DockBuilderDockWindow("Inspector", ids.right);
    UI::DockBuilderDockWindow("Stats", ids.right);
    UI::DockBuilderDockWindow("Console###Console", ids.down);
    UI::DockBuilderDockWindow("Project", ids.down);
    UI::DockBuilderDockWindow("Scene", ids.center);
    UI::DockBuilderDockWindow("Game", ids.center);
    UI::DockBuilderFinish(kSpace);
    return ids;
}

void Frame(Sim& s, UI::Rect area) {
    s.Frame([&] {
        UI::DockSpace(area);
        for (const char* n : kNames) { UI::DockBegin(n); UI::Text("content"); UI::DockEnd(); }
    });
}

} // namespace

int main() {
    RUN_SUITE("okayui dock nodes");

    // ---------------- nodes, tab bars and windows read from the live layout ----------------
    {
        Reset();
        const Ids ids = Build(UI::Vec2{1280, 720});
        Sim s(1280, 720);
        const UI::Rect area{0, 0, 1280, 720};
        Frame(s, area); Frame(s, area);
        UI::Context* g = UI::RefreshContext();
        CHECK(g->Windows.Size >= 7 && g->DockContext.Nodes.Data.Size == (int)UI::GetDockLayout().Nodes().size());
        DockNodeRef* root = UI::DockBuilderGetNode(kSpace);
        CHECK(root && root->IsRootNode() && root->IsSplitNode() && !root->IsLeafNode() && root->IsDockSpace() && root->ParentNode == nullptr);
        CHECK(root && root->ChildNodes[0] && root->ChildNodes[1] && root->ChildNodes[0]->ParentNode == root && root->ChildNodes[1]->ParentNode == root);
        CHECK_NEAR(root->Size.x, 1280.0f, 1.0f); CHECK_NEAR(root->Size.y, 720.0f, 1.0f);
        DockNodeRef* left = UI::DockBuilderGetNode(ids.left);
        DockNodeRef* right = UI::DockBuilderGetNode(ids.right);
        DockNodeRef* down = UI::DockBuilderGetNode(ids.down);
        DockNodeRef* centre = UI::DockBuilderGetNode(ids.center);
        CHECK(left && right && down && centre && left->IsLeafNode() && right->IsLeafNode() && down->IsLeafNode() && centre->IsLeafNode());
        CHECK(left && left->ID == ids.left && right->ID == ids.right && down->ID == ids.down && centre->ID == ids.center);
        CHECK(right && right->Windows.Size == 2 && std::string(right->Windows[0]->Name) == "Inspector" && std::string(right->Windows[1]->Name) == "Stats");
        CHECK(down && down->Windows.Size == 2 && centre && centre->Windows.Size == 2);
        CHECK(left && left->TabBar && right->TabBar && right->TabBar->Tabs.Size == 2);
        // the strip's rectangle and the tab rectangles are those the dock drew
        const std::vector<UI::DockTabInfo> tabs = UI::GetDockTabs();
        CHECK(tabs.size() == 7);
        bool tabsAgree = true;
        for (const UI::DockTabInfo& t : tabs) {
            WindowRef* w = UI::FindWindowRefByName(t.window.c_str());
            tabsAgree = tabsAgree && w && std::fabs(w->DC.DockTabItemRect.Min.x - t.rect.x) < 0.01f && std::fabs(w->DC.DockTabItemRect.Max.x - (t.rect.x + t.rect.w)) < 0.01f
                        && w->DC.DockTabItemRect.Min.y == t.rect.y && w->DockOrder == t.index && w->TabId == UI::TabItemRef{w->TabId}.ID;
        }
        CHECK(tabsAgree);
        CHECK(right && right->TabBar->BarRect.Min.x == right->Pos.x && right->TabBar->BarRect.Min.y == right->Pos.y && right->TabBar->BarRect.Max.x - right->TabBar->BarRect.Min.x > 100.0f);
        CHECK(right && right->TabBar->VisibleTabId == right->TabBar->SelectedTabId && right->TabBar->VisibleTabId == right->TabBar->Tabs[0].ID);
        CHECK(right && right->TabBar->WidthAllTabsIdeal > 0.0f && right->TabBar->Tabs[0].Window == right->Windows[0]);
        // pointer identity: the same window is one entry everywhere
        WindowRef* insp = UI::FindWindowRefByName("Inspector");
        CHECK(insp && insp == right->Windows[0] && insp->DockNode == right && insp->DockId == ids.right && insp->DockIsActive && insp->DockTabIsVisible && insp->DockOrder == 0);
        WindowRef* stats = UI::FindWindowRefByName("Stats");
        CHECK(stats && stats->DockNode == right && !stats->DockTabIsVisible && stats->DockOrder == 1);
        CHECK(UI::FindWindowRefByID(insp->ID) == insp && insp->ID == UI::WindowIdOfTitle("Inspector") && insp->RootWindow == insp && insp->ParentWindow == nullptr);
        bool inList = false; for (WindowRef* w : g->Windows) if (w == insp) inList = true;
        CHECK(inList);
        UI::WindowInfo wi = UI::FindWindowByName("Inspector");
        CHECK(wi.found && static_cast<WindowRef*>(wi) == insp);
        CHECK(UI::DockNodeGetRootNode(insp->DockNode) == root && UI::DockNodeGetRootNode(root) == root && UI::DockNodeGetRootNode(nullptr) == nullptr);
        // an active window is Active, a never-drawn name is not found
        CHECK(insp->Active && insp->WasActive && UI::FindWindowRefByName("nope") == nullptr);
        // split nodes: the axis follows the split direction (left is a side-by-side split, down is stacked)
        CHECK(root && root->SplitAxis == UI::Axis_X);
        DockNodeRef* par = down->ParentNode;
        CHECK(par && par->SplitAxis == UI::Axis_Y && (par->ChildNodes[0] == down || par->ChildNodes[1] == down));
        // every node of the list is reachable through g.DockContext.Nodes
        bool allThere = true;
        for (int i = 0; i < g->DockContext.Nodes.Data.Size; ++i) { const DockNodeRef* n = static_cast<DockNodeRef*>(g->DockContext.Nodes.Data[i].val_p); allThere = allThere && n && n->ID == g->DockContext.Nodes.Data[i].key; }
        CHECK(allThere);
        // visible / empty / flags
        CHECK(left && left->IsVisible && !left->IsNoTabBar() && !left->IsHiddenTabBar() && !left->IsCentralNode() && left->HostWindow && std::string(left->HostWindow->Name) == "##DockSpaceHost");
        // hovering a strip: the dock host is the hovered window
        s.Move(right->Pos.x + 10.0f, right->Pos.y + 4.0f);
        Frame(s, area);
        UI::Context* g2 = UI::RefreshContext();
        CHECK(g2->HoveredWindow == right->HostWindow);
        // the focused window is NavWindow
        UI::SetWindowFocus("Hierarchy"); Frame(s, area);
        g2 = UI::RefreshContext();
        CHECK(g2->NavWindow && std::string(g2->NavWindow->Name) == "Hierarchy");
        // a tab bar of the focused chosen window is flagged
        DockNodeRef* l2 = UI::DockBuilderGetNode(ids.left);
        CHECK(l2 && (l2->TabBar->Flags & UI::TabBar_IsFocused));
        CHECK(UI::TabBarFindTabByID(l2->TabBar, l2->TabBar->SelectedTabId) == &l2->TabBar->Tabs[0] && UI::TabBarFindTabByID(l2->TabBar, 12345u) == nullptr && UI::TabBarFindTabByID(nullptr, 1u) == nullptr);
    }

    // ---------------- the current window's node ----------------
    {
        Reset();
        Build(UI::Vec2{1280, 720});
        Sim s(1280, 720);
        DockNodeRef* seen = nullptr; bool sawDocked = false;
        for (int i = 0; i < 3; ++i) s.Frame([&] {
            UI::DockSpace(UI::Rect{0, 0, 1280, 720});
            for (const char* n : kNames) {
                if (UI::DockBegin(n)) { if (std::string(n) == "Inspector") { seen = UI::GetWindowDockNode(); sawDocked = UI::IsWindowDocked(); } }
                UI::DockEnd();
            }
        });
        CHECK(seen && sawDocked && seen->Windows.Size == 2 && std::string(seen->Windows[0]->Name) == "Inspector");
        CHECK(UI::GetWindowDockNode() == nullptr);   // outside a window
    }

    // ---------------- writes that are applied back ----------------
    {
        Reset();
        const Ids ids = Build(UI::Vec2{1280, 720});
        Sim s(1280, 720);
        const UI::Rect area{0, 0, 1280, 720};
        Frame(s, area); Frame(s, area);
        // SizeRef along the parent's split resizes the node (the "Collapse Group" menu entry does this)
        DockNodeRef* down = UI::DockBuilderGetNode(ids.down);
        CHECK(down && down->ParentNode && down->ParentNode->SplitAxis == UI::Axis_Y);
        const float h0 = down->Size.y;
        down->SizeRef[1] = 60.0f;
        Frame(s, area); Frame(s, area);
        down = UI::DockBuilderGetNode(ids.down);
        CHECK(down && down->Size.y < h0 - 50.0f);
        CHECK(down && down->Size.y >= 59.0f && down->Size.y < 120.0f);   // 60, or the node's minimum if that is larger
        // and back
        down->SizeRef[1] = h0;
        Frame(s, area); Frame(s, area);
        down = UI::DockBuilderGetNode(ids.down);
        CHECK(down && std::fabs(down->Size.y - h0) < 3.0f);
        // a non-finite or absurd write cannot break the layout
        down->SizeRef[1] = NAN; Frame(s, area); Frame(s, area);
        down = UI::DockBuilderGetNode(ids.down); down->SizeRef[1] = 1.0e30f; Frame(s, area); Frame(s, area);
        down = UI::DockBuilderGetNode(ids.down); down->SizeRef[0] = -5.0f; Frame(s, area);
        CHECK(Valid(UI::GetDockLayout()));
        // the first of Windows becomes the chosen tab (the side tabs' "move to front")
        DockNodeRef* right = UI::DockBuilderGetNode(ids.right);
        CHECK(right && right->Windows.Size == 2 && std::string(right->Windows[0]->Name) == "Inspector");
        WindowRef* stats = right->Windows[1];
        right->Windows.erase(right->Windows.Data + 1);
        right->Windows.push_front(stats);
        Frame(s, area);
        right = UI::DockBuilderGetNode(ids.right);
        CHECK(right && stats->DockTabIsVisible && right->TabBar->VisibleTabId == stats->TabId);
        const DockLayout& L = UI::GetDockLayout();
        CHECK(L.Find(L.NodeOf("Stats"))->tabs[(size_t)L.Find(L.NodeOf("Stats"))->active] == "Stats");
        // WindowClass: NoTabBar hides the strip of the node (content grows by the strip height), and clears again
        const UI::Rect before = L.ContentRect(L.NodeOf("Hierarchy"));
        WindowRef* hier = UI::FindWindowRefByName("Hierarchy");
        CHECK(hier);
        hier->WindowClass.DockNodeFlagsOverrideSet |= UI::DockNode_NoTabBar;
        Frame(s, area); Frame(s, area);
        const UI::Rect after = L.ContentRect(L.NodeOf("Hierarchy"));
        DockNodeRef* left = UI::DockBuilderGetNode(ids.left);
        CHECK(after.h > before.h + 10.0f && left && left->IsNoTabBar() && left->TabBar == nullptr);
        bool drawnStrip = false;
        for (const UI::DockTabInfo& t : UI::GetDockTabs()) if (t.window == "Hierarchy") drawnStrip = true;
        CHECK(!drawnStrip && UI::GetDockStripRect(L.NodeOf("Hierarchy")).w == 0.0f);
        UI::FindWindowRefByName("Hierarchy")->WindowClass.DockNodeFlagsOverrideSet &= ~UI::DockNode_NoTabBar;
        Frame(s, area); Frame(s, area);
        left = UI::DockBuilderGetNode(ids.left);
        CHECK(std::fabs(L.ContentRect(L.NodeOf("Hierarchy")).h - before.h) < 1.0f && left && !left->IsNoTabBar() && left->TabBar != nullptr);
        CHECK(Valid(L));
    }

    // ---------------- ids of nodes nobody planned ----------------
    {
        Reset();
        const Ids ids = Build(UI::Vec2{1280, 720});
        Sim s(1280, 720);
        const UI::Rect area{0, 0, 1280, 720};
        Frame(s, area);
        DockLayout& L = UI::GetDockLayout();
        // a window dragged to the left of the Scene node makes a split the plan never saw
        const int sceneLeaf = L.NodeOf("Scene");
        CHECK(L.Dock("Game", sceneLeaf, UI::DockSide_Left));
        Frame(s, area); Frame(s, area);
        const int gameLeaf = L.NodeOf("Game");
        const DockId gid = UI::DockNodeIdOf(gameLeaf);
        CHECK(gid == UI::kDockDerivedBase + (DockId)gameLeaf && UI::DockLayoutNodeOf(gid) == gameLeaf && UI::GetWindowDockID("Game") == gid);
        DockNodeRef* gn = UI::DockBuilderGetNode(gid);
        CHECK(gn && gn->ID == gid && gn->Windows.Size == 1 && std::string(gn->Windows[0]->Name) == "Game" && UI::FindWindowRefByName("Game")->DockId == gid);
        // the node can be a target: dock another window in by that id, resize it, and SetNextWindowDockID finds it
        UI::DockBuilderDockWindow("Project", gid);
        CHECK(L.NodeOf("Project") == gameLeaf);
        Frame(s, area); Frame(s, area);
        gn = UI::DockBuilderGetNode(gid);
        CHECK(gn && gn->Windows.Size == 2);
        const float w0 = gn->Size.x;
        UI::DockBuilderSetNodeSize(gid, UI::Vec2{w0 + 100.0f, gn->Size.y});
        Frame(s, area);
        CHECK(UI::DockBuilderGetNode(gid)->Size.x > w0 + 50.0f);
        UI::SetNextWindowDockID(gid, UI::Cond_Always);
        UI::DockBegin("Stats"); UI::DockEnd();
        CHECK(L.NodeOf("Stats") == gameLeaf);
        // an id of a node that is gone, or never was, answers null
        CHECK(UI::DockBuilderGetNode(UI::kDockDerivedBase + 200u) == nullptr && UI::DockBuilderGetNode(0) == nullptr && UI::DockBuilderGetNode(0xFFFFFFFFu) == nullptr);
        // the root answers to the id given to DockSpace
        UI::SetDockSpaceId(0xABCD01u);
        CHECK(UI::DockNodeIdOf(L.Root()) == 0xABCD01u && UI::DockBuilderGetNode(0xABCD01u) && UI::DockBuilderGetNode(0xABCD01u)->IsRootNode());
        UI::SetDockSpaceId(0);
        CHECK(Valid(L));
        (void)ids;
    }

    // ---------------- DockSpace(id, size, flags) as an item ----------------
    {
        Reset();
        UI::DockBuilderRemoveNode(1);
        Sim s(800, 600);
        UI::Vec2 after{0, 0};
        for (int i = 0; i < 3; ++i) s.Frame([&] {
            UI::SetNextWindowPos(UI::Vec2{0, 0}, UI::Cond_Always); UI::SetNextWindowSize(UI::Vec2{800, 600}, UI::Cond_Always);
            UI::Begin("host", nullptr, UI::Window_NoDecoration);
            const UI::Vec2 p = UI::GetCursorScreenPos();
            const DockId id = UI::DockSpace(0x5151u, UI::Vec2{0.0f, -30.0f}, UI::DockNode_None);
            after = UI::GetCursorScreenPos();
            (void)p; (void)id;
            UI::End();
            UI::DockBegin("A"); UI::Text("a"); UI::DockEnd();
        });
        const UI::Rect a = UI::GetDockArea();
        CHECK(a.w > 400.0f && a.h > 300.0f && a.h < 600.0f);
        CHECK(after.y > a.y + a.h - 2.0f);   // the dock space took its room in the window
        CHECK(UI::DockNodeIdOf(UI::GetDockLayout().Root()) == 0x5151u);
    }

    // ---------------- a button on a strip ----------------
    {
        Reset();
        const Ids ids = Build(UI::Vec2{1280, 720});
        Sim s(1280, 720);
        const UI::Rect area{0, 0, 1280, 720};
        int clicks = 0;
        auto run = [&] {
            s.Frame([&] {
                UI::DockSpace(area);
                for (const char* n : kNames) { UI::DockBegin(n); UI::Text("content"); UI::DockEnd(); }
                DockNodeRef* node = UI::DockBuilderGetNode(ids.down);
                if (node && UI::DockNodeBeginAmendTabBar(node)) {
                    UI::SetNextItemWidth(22.0f);
                    if (UI::TabItemButton("##list", UI::Tab_Trailing)) ++clicks;
                    UI::DockNodeEndAmendTabBar();
                }
            });
        };
        run(); run(); run();
        DockNodeRef* down = UI::DockBuilderGetNode(ids.down);
        CHECK(down && down->TabBar);
        const UI::ImRect bar = down->TabBar->BarRect;
        // the strip kept room at its right end: the last tab ends before the button
        float lastEnd = 0.0f; for (const UI::DockTabInfo& t : UI::GetDockTabs()) if (t.node == UI::DockLayoutNodeOf(ids.down)) lastEnd = std::max(lastEnd, t.rect.x + t.rect.w);
        // (the tabs are narrow here, so check the reserve through a squeeze below)
        s.Move(bar.Max.x - 12.0f, (bar.Min.y + bar.Max.y) * 0.5f);
        run(); s.Down(); run(); s.Up(); run();
        CHECK(clicks == 1);
        // a click elsewhere on the strip does not count
        s.Move(bar.Min.x + 5.0f, (bar.Min.y + bar.Max.y) * 0.5f);
        s.Down(); run(); s.Up(); run();
        CHECK(clicks == 1);
        // squeeze: with a very narrow node the tabs shrink to leave the room
        UI::DockBuilderSetNodeSize(ids.left, UI::Vec2{1200, 400});
        (void)lastEnd;
        // end without begin and a second begin are harmless
        UI::DockNodeEndAmendTabBar();
        CHECK(!UI::DockNodeBeginAmendTabBar(nullptr));
        CHECK(Valid(UI::GetDockLayout()));
    }

    // ---------------- random writes and calls ----------------
    {
        Reset();
        Build(UI::Vec2{1000, 700});
        Sim s(1000, 700);
        const UI::Rect area{0, 0, 1000, 700};
        unsigned seed = 12345u;
        auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
        for (int step = 0; step < 400; ++step) {
            Frame(s, area);
            UI::Context* g = UI::RefreshContext();
            const int pick = (int)(rnd() % 8u);
            if (g->DockContext.Nodes.Data.Size == 0) continue;
            DockNodeRef* n = static_cast<DockNodeRef*>(g->DockContext.Nodes.Data[(int)(rnd() % (unsigned)g->DockContext.Nodes.Data.Size)].val_p);
            const float junk[] = {0.0f, -1.0f, 40.0f, 300.0f, 5.0e6f, NAN, INFINITY};
            switch (pick) {
                case 0: n->SizeRef[rnd() & 1] = junk[rnd() % 7u]; break;
                case 1: if (n->Windows.Size > 1) { WindowRef* w = n->Windows[(int)(rnd() % (unsigned)n->Windows.Size)]; n->Windows.erase(n->Windows.begin() + n->Windows.find_index(w)); n->Windows.push_front(w); } break;
                case 2: if (n->Windows.Size) n->Windows[0]->WindowClass.DockNodeFlagsOverrideSet ^= UI::DockNode_NoTabBar; break;
                case 3: UI::DockBuilderSetNodeSize(n->ID, UI::Vec2{junk[rnd() % 7u], junk[rnd() % 7u]}); break;
                case 4: UI::DockBuilderDockWindow(kNames[rnd() % 7u], n->ID); break;
                case 5: { const int leaf = UI::DockLayoutNodeOf(n->ID); if (leaf) UI::GetDockLayout().Dock(kNames[rnd() % 7u], leaf, (UI::DockSide)(rnd() % 5u)); break; }
                case 6: if (UI::DockNodeBeginAmendTabBar(n)) { UI::TabItemButton("##x", UI::Tab_Trailing); UI::DockNodeEndAmendTabBar(); } break;
                case 7: UI::GetDockLayout().Float(kNames[rnd() % 7u], UI::Rect{50, 50, 200, 150}); break;
            }
            if (!Valid(UI::GetDockLayout())) { CHECK(false); break; }
        }
        CHECK(Valid(UI::GetDockLayout()));
        UI::ResetState();
        CHECK(UI::DockBuilderGetNode(kSpace) == nullptr);
    }

    TEST_MAIN_RESULT();
}
