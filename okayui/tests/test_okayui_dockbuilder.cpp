// DockBuilder subset and SetNextWindowDockID, headless. The main case builds the editor's "Default" layout preset (the call sequence of
// BuildLayoutPreset in editor/main.cpp: split left 0.20, right 0.42, bottom 0.22 off the centre, then the panels) through the builder
// and checks the real DockLayout and its geometry; the rest covers sizes, dropped empty nodes, re-finishing, removal, docking by id, bad
// arguments and a random-call run that the layout's own Validate must accept.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "okay/UI/OkayUIDockBuilder.hpp"

#include <cmath>
#include <iterator>
#include <string>
#include <vector>

using namespace okayuitest;
namespace UI = OkayUI;
using UI::DockId; using UI::DockLayout;

namespace {

bool Valid(const DockLayout& l) { std::string why; const bool ok = l.Validate(&why); if (!ok) std::cerr << "  invalid layout: " << why << "\n"; return ok; }
UI::Rect NodeRect(const std::string& w) { const DockLayout& l = UI::GetDockLayout(); const DockLayout::Node* n = l.Find(l.NodeOf(w)); return n ? n->rect : UI::Rect{}; }
std::vector<std::string> Tabs(const std::string& w) { const DockLayout& l = UI::GetDockLayout(); const DockLayout::Node* n = l.Find(l.NodeOf(w)); return n ? n->tabs : std::vector<std::string>{}; }

const char* const kAll[] = {"Hierarchy", "Inspector", "Console###Console", "Problems###Problems", "Project", "Services", "Script Editor", "Animation###AnimationDock",
                            "Asset Manager", "Stats", "Lighting", "Scene", "Game"};

// The editor's Default preset, call for call (the layout code in editor/main.cpp BuildLayoutPreset, `default:` branch).
struct DefaultIds { DockId left = 0, right = 0, down = 0, center = 0; };
DefaultIds BuildEditorDefault(DockId space, UI::Vec2 size) {
    UI::DockBuilderRemoveNode(space);
    UI::DockBuilderAddNode(space, UI::DockNode_DockSpace);
    UI::DockBuilderSetNodeSize(space, size);
    DockId center = space;
    auto split = [](DockId& from, UI::DockSide dir, float ratio) { DockId opp = 0; const DockId nw = UI::DockBuilderSplitNode(from, dir, ratio, nullptr, &opp); from = opp; return nw; };
    DefaultIds ids;
    ids.left = split(center, UI::DockSide_Left, 0.20f);
    ids.right = split(center, UI::DockSide_Right, 0.42f);
    ids.down = split(center, UI::DockSide_Bottom, 0.22f);
    ids.center = center;
    UI::DockBuilderDockWindow("Hierarchy", ids.left);
    UI::DockBuilderDockWindow("Inspector", ids.right);
    UI::DockBuilderDockWindow("Console###Console", ids.down);
    UI::DockBuilderDockWindow("Problems###Problems", ids.down);
    UI::DockBuilderDockWindow("Project", ids.down);
    UI::DockBuilderDockWindow("Services", ids.down);
    UI::DockBuilderDockWindow("Script Editor", ids.down);
    UI::DockBuilderDockWindow("Animation###AnimationDock", ids.down);
    UI::DockBuilderDockWindow("Asset Manager", ids.down);
    UI::DockBuilderDockWindow("Stats", ids.right);
    UI::DockBuilderDockWindow("Lighting", ids.right);
    UI::DockBuilderDockWindow("Scene", ids.center);
    UI::DockBuilderDockWindow("Game", ids.center);
    return ids;
}

void RunFrame(Sim& s, UI::Rect area, const std::vector<std::string>& names, std::vector<int>* shown = nullptr) {
    s.Frame([&] {
        UI::DockSpace(area);
        for (size_t i = 0; i < names.size(); ++i) {
            const bool vis = UI::DockBegin(names[i].c_str());
            if (vis) { UI::Text("content"); }
            if (shown) (*shown)[i] = vis ? 1 : 0;
            UI::DockEnd();
        }
    });
}

} // namespace

int main() {
    RUN_SUITE("okayui dock builder");

    // ---------------- the editor's default layout ----------------
    {
        Reset();
        const DockId space = 0xD0C5u;
        const DefaultIds ids = BuildEditorDefault(space, UI::Vec2{1280, 720});
        CHECK(ids.left != 0 && ids.right != 0 && ids.down != 0 && ids.center != 0 && ids.left != ids.right && ids.center != space);
        const UI::DockNodeInfo before = UI::DockBuilderGetNode(ids.down);
        CHECK(before.found && before.isLeaf && !before.live && before.windows.size() == 7);
        CHECK(UI::DockBuilderGetNode(space).isRoot && UI::DockBuilderGetNode(space).isSplit);
        CHECK(UI::DockBuilderFinish(space));
        const DockLayout& L = UI::GetDockLayout();
        CHECK(Valid(L));
        CHECK(L.WindowCount() == 13 && L.FloatingWindows().empty());
        // tabs: the five-way groups the preset describes
        CHECK(L.NodeOf("Scene") == L.NodeOf("Game") && L.NodeOf("Scene") != 0);
        const int dn = L.NodeOf("Console###Console");
        CHECK(dn != 0 && dn == L.NodeOf("Problems###Problems") && dn == L.NodeOf("Project") && dn == L.NodeOf("Services") && dn == L.NodeOf("Script Editor")
              && dn == L.NodeOf("Animation###AnimationDock") && dn == L.NodeOf("Asset Manager"));
        CHECK(Tabs("Console###Console").size() == 7 && Tabs("Console###Console")[0] == "Console###Console" && Tabs("Console###Console")[6] == "Asset Manager");
        const int rn = L.NodeOf("Inspector");
        CHECK(rn != 0 && rn == L.NodeOf("Stats") && rn == L.NodeOf("Lighting") && Tabs("Inspector").size() == 3);
        CHECK(L.NodeOf("Hierarchy") != 0 && Tabs("Hierarchy").size() == 1);
        // the five groups are five different leaves
        CHECK(L.NodeOf("Hierarchy") != rn && L.NodeOf("Hierarchy") != dn && L.NodeOf("Scene") != dn && L.NodeOf("Scene") != rn && rn != dn);
        // builder ids now read from the live layout
        const UI::DockNodeInfo live = UI::DockBuilderGetNode(ids.down);
        CHECK(live.live && live.windows.size() == 7 - 0 && live.windows[1] == "Problems###Problems");
        CHECK(UI::GetWindowDockID("Hierarchy") == ids.left && UI::GetWindowDockID("Scene") == ids.center && UI::GetWindowDockID("Stats") == ids.right && UI::GetWindowDockID("nope") == 0);

        // geometry: drawn through the dock space in a 1280 x 720 area
        std::vector<std::string> names(std::begin(kAll), std::end(kAll));
        Sim s(1280, 720);
        std::vector<int> shown(names.size(), 0);
        RunFrame(s, UI::Rect{0, 0, 1280, 720}, names, &shown); RunFrame(s, UI::Rect{0, 0, 1280, 720}, names, &shown);
        const float sp = DockLayout::kSplitter;
        const float leftW = 0.20f * (1280 - sp);                         // first split: the new left node's share of the whole width
        const float restW = (1280 - sp) - leftW;                         // what is left for the other two columns
        const float rightW = 0.42f * (restW - sp);
        const float centerW = restW - sp - rightW;
        const float downH = 0.22f * (720 - sp);
        const UI::Rect rl = NodeRect("Hierarchy"), rr = NodeRect("Inspector"), rd = NodeRect("Console###Console"), rc = NodeRect("Scene");
        CHECK_NEAR(rl.x, 0.0f, 1.0f); CHECK_NEAR(rl.w, leftW, 1.0f); CHECK_NEAR(rl.h, 720.0f, 1.0f);
        CHECK_NEAR(rr.w, rightW, 1.0f); CHECK_NEAR(rr.x + rr.w, 1280.0f, 1.0f); CHECK_NEAR(rr.h, 720.0f, 1.0f);
        CHECK_NEAR(rc.w, centerW, 1.0f); CHECK_NEAR(rc.x, leftW + sp, 1.0f);
        CHECK_NEAR(rd.h, downH, 1.0f); CHECK_NEAR(rd.y + rd.h, 720.0f, 1.0f); CHECK_NEAR(rd.w, centerW, 1.0f);
        CHECK_NEAR(rc.h, 720.0f - sp - downH, 1.0f);
        std::printf("  left %.1f centre %.1fx%.1f right %.1f bottom h %.1f (expected %.1f / %.1f / %.1f / %.1f)\n", rl.w, rc.w, rc.h, rr.w, rd.h, leftW, centerW, 720.0f - sp - downH, rightW, downH);
        // the chosen tab of each group draws, the others do not
        int visible = 0; for (int v : shown) visible += v;
        CHECK(visible == 4);                                              // Hierarchy, Inspector, Console, Scene
        CHECK(shown[0] == 1 && shown[1] == 1 && shown[2] == 1 && shown[11] == 1 && shown[12] == 0);
        // and it really paints something: the frame is not one flat colour
        const UI::soft::Bitmap& bmp = s.Draw(5, 5, 5);
        int lit = 0; for (size_t i = 0; i + 3 < bmp.rgba.size(); i += 4) if (bmp.rgba[i] != 5 || bmp.rgba[i + 1] != 5 || bmp.rgba[i + 2] != 5) ++lit;
        CHECK(lit > 100000);
        // the finished layout survives a save and a load, and Validate accepts both
        const std::string blob = UI::DockSaveLayout();
        CHECK(UI::DockLoadLayout(blob) && Valid(UI::GetDockLayout()) && UI::DockSaveLayout() == blob);

        // SetNextWindowDockID: a new window goes into the bottom group; FirstUseEver does not move a window the layout has; 0 floats it
        UI::SetNextWindowDockID(ids.down, UI::Cond_Always);
        RunFrame(s, UI::Rect{0, 0, 1280, 720}, {"Extra"});
        CHECK(UI::GetDockLayout().NodeOf("Extra") == UI::GetDockLayout().NodeOf("Console###Console"));
        UI::SetNextWindowDockID(ids.down, UI::Cond_FirstUseEver);
        RunFrame(s, UI::Rect{0, 0, 1280, 720}, {"Hierarchy"});
        CHECK(UI::GetDockLayout().NodeOf("Hierarchy") != UI::GetDockLayout().NodeOf("Console###Console"));
        UI::SetNextWindowDockID(ids.down, UI::Cond_Always);
        RunFrame(s, UI::Rect{0, 0, 1280, 720}, {"Hierarchy"});            // Always moves it
        CHECK(UI::GetDockLayout().NodeOf("Hierarchy") == UI::GetDockLayout().NodeOf("Console###Console") && Valid(UI::GetDockLayout()));
        UI::SetNextWindowDockID(0, UI::Cond_Always);
        RunFrame(s, UI::Rect{0, 0, 1280, 720}, {"Extra"});
        CHECK(UI::GetDockLayout().IsFloating("Extra"));
        // one request only, and a bad id does nothing
        UI::SetNextWindowDockID(ids.right, UI::Cond_Always);
        RunFrame(s, UI::Rect{0, 0, 1280, 720}, {"Fresh1", "Fresh2"});
        CHECK(UI::GetDockLayout().NodeOf("Fresh1") == UI::GetDockLayout().NodeOf("Inspector") && UI::GetDockLayout().IsFloating("Fresh2"));
        UI::SetNextWindowDockID(0xBAD1D, UI::Cond_Always);
        RunFrame(s, UI::Rect{0, 0, 1280, 720}, {"Fresh3"});
        CHECK(UI::GetDockLayout().IsFloating("Fresh3"));
        UI::SetNextWindowDockID(ids.down, UI::Cond_Always); s.Frame([&] { UI::DockSpace(UI::Rect{0, 0, 1280, 720}); UI::DockBegin(nullptr); UI::DockEnd(); });
        RunFrame(s, UI::Rect{0, 0, 1280, 720}, {"Fresh4"});                // the null-title DockBegin used the request up
        CHECK(UI::GetDockLayout().IsFloating("Fresh4"));
        CHECK(Valid(UI::GetDockLayout()));
    }

    // ---------------- sizes in pixels ----------------
    {
        Reset();
        UI::DockBuilderAddNode(7);
        UI::DockBuilderSetNodeSize(7, UI::Vec2{1000, 600});
        DockId opp = 0; const DockId nw = UI::DockBuilderSplitNode(7, UI::DockSide_Right, 0.5f, nullptr, &opp);
        UI::DockBuilderSetNodeSize(nw, UI::Vec2{300, 600});                  // the right node is 300 px wide
        UI::DockBuilderDockWindow("L", opp); UI::DockBuilderDockWindow("R", nw);
        CHECK(UI::DockBuilderFinish(7));
        const DockLayout& L = UI::GetDockLayout();
        CHECK(L.Find(L.Root())->IsSplit() && L.Find(L.Root())->horizontal);
        CHECK_NEAR(L.Find(L.Root())->ratio, 1.0f - 300.0f / (1000.0f - DockLayout::kSplitter), 0.001f);
        const UI::DockNodeInfo r = UI::DockBuilderGetNode(nw);
        CHECK_NEAR(r.size.x, 300.0f, 0.01f); CHECK_NEAR(r.size.y, 600.0f, 0.01f);
        // on the live layout: moves the divider now
        Sim s(1000, 600);
        RunFrame(s, UI::Rect{0, 0, 1000, 600}, {"L", "R"});
        UI::DockBuilderSetNodeSize(nw, UI::Vec2{400, 600});
        RunFrame(s, UI::Rect{0, 0, 1000, 600}, {"L", "R"});
        CHECK_NEAR(NodeRect("R").w, 400.0f, 1.5f);
        // a vertical split, size on the first half
        UI::DockBuilderAddNode(8); UI::DockBuilderSetNodeSize(8, UI::Vec2{500, 804});
        DockId o2 = 0; const DockId top = UI::DockBuilderSplitNode(8, UI::DockSide_Top, 0.5f, nullptr, &o2);
        UI::DockBuilderSetNodeSize(top, UI::Vec2{500, 100});
        UI::DockBuilderDockWindow("T", top); UI::DockBuilderDockWindow("B", o2);
        CHECK(UI::DockBuilderFinish(8));
        const DockLayout& L2 = UI::GetDockLayout();
        CHECK(!L2.Find(L2.Root())->horizontal); CHECK_NEAR(L2.Find(L2.Root())->ratio, 100.0f / 800.0f, 0.001f);
        // no root size: the ratio from the split stands, a child size is ignored
        UI::DockBuilderAddNode(9);
        DockId o3 = 0; const DockId a3 = UI::DockBuilderSplitNode(9, UI::DockSide_Left, 0.25f, nullptr, &o3);
        UI::DockBuilderSetNodeSize(a3, UI::Vec2{999, 999});
        UI::DockBuilderDockWindow("X", a3); UI::DockBuilderDockWindow("Y", o3);
        CHECK(UI::DockBuilderFinish(9));
        CHECK_NEAR(UI::GetDockLayout().Find(UI::GetDockLayout().Root())->ratio, 0.25f, 0.0001f);
    }

    // ---------------- empty nodes are dropped, windows move, plans re-finish ----------------
    {
        Reset();
        UI::DockBuilderAddNode(1);
        DockId opp = 0; const DockId side = UI::DockBuilderSplitNode(1, UI::DockSide_Left, 0.3f, nullptr, &opp);
        UI::DockBuilderDockWindow("Only", opp);                              // `side` stays empty
        CHECK(UI::DockBuilderFinish(1));
        const DockLayout& L = UI::GetDockLayout();
        CHECK(!L.Find(L.Root())->IsSplit() && L.WindowCount() == 1 && Valid(L));
        CHECK(!UI::DockBuilderGetNode(side).live && UI::DockBuilderGetNode(side).found);
        // docking into an empty (dropped) node while finished waits for the next Finish, then shows
        UI::DockBuilderDockWindow("Late", side);
        CHECK(!UI::GetDockLayout().Contains("Late"));
        CHECK(UI::DockBuilderFinish(side));                                  // any id of the plan finishes it
        CHECK(UI::GetDockLayout().Contains("Late") && UI::GetDockLayout().Find(UI::GetDockLayout().Root())->IsSplit());
        // a window is in one node only
        UI::DockBuilderDockWindow("Late", opp);
        CHECK(UI::DockBuilderGetNode(side).windows.empty() && UI::DockBuilderGetNode(opp).windows.size() == 2);
        CHECK(UI::GetDockLayout().NodeOf("Late") == UI::GetDockLayout().NodeOf("Only"));      // docked at once into the live node
        // a split's id docks into its first leaf
        UI::DockBuilderDockWindow("InSplit", 1);
        CHECK(UI::DockBuilderGetNode(side).windows.size() == 1);                  // in the plan's first leaf (the left one)
        CHECK(UI::DockBuilderFinish(1) && UI::GetDockLayout().Contains("InSplit") && Valid(UI::GetDockLayout()));
        // windows the plan does not mention stay, floating
        Reset();
        UI::GetDockLayout().AddFloating("Old", UI::Rect{10, 20, 300, 200}); UI::GetDockLayout().Dock("Docked", UI::GetDockLayout().Root(), UI::DockSide_Center);
        UI::DockBuilderAddNode(5); UI::DockBuilderDockWindow("New", 5);
        CHECK(UI::DockBuilderFinish(5));
        CHECK(UI::GetDockLayout().IsFloating("Old") && UI::GetDockLayout().IsFloating("Docked") && !UI::GetDockLayout().IsFloating("New"));
        const UI::DockLayout::Floating* f = UI::GetDockLayout().FindFloating("Old"); CHECK(f && f->rect.x == 10 && f->rect.w == 300);
        CHECK(Valid(UI::GetDockLayout()));
    }

    // ---------------- removing nodes ----------------
    {
        Reset();
        UI::DockBuilderAddNode(1);
        DockId opp = 0; const DockId nw = UI::DockBuilderSplitNode(1, UI::DockSide_Bottom, 0.3f, nullptr, &opp);
        UI::DockBuilderDockWindow("A", opp); UI::DockBuilderDockWindow("B", nw);
        UI::DockBuilderRemoveNode(nw);                                       // the sibling takes the parent's place
        CHECK(!UI::DockBuilderGetNode(nw).found && !UI::DockBuilderGetNode(1).found && UI::DockBuilderGetNode(opp).found && UI::DockBuilderGetNode(opp).isRoot);
        CHECK(UI::DockBuilderFinish(opp) && UI::GetDockLayout().WindowCount() == 1 && UI::GetDockLayout().Contains("A"));
        UI::DockBuilderRemoveNode(opp);                                      // a finished root: the live layout empties
        CHECK(UI::GetDockLayout().WindowCount() == 0 && !UI::DockBuilderGetNode(opp).found && Valid(UI::GetDockLayout()));
        UI::DockBuilderRemoveNode(12345); UI::DockBuilderRemoveNode(0);      // unknown: nothing
        // the editor's pattern: remove, add with the same id, rebuild
        const DefaultIds ids = BuildEditorDefault(0xD0C5u, UI::Vec2{800, 600}); (void)ids;
        CHECK(UI::DockBuilderFinish(0xD0C5u) && UI::GetDockLayout().WindowCount() == 13);
        BuildEditorDefault(0xD0C5u, UI::Vec2{800, 600});
        CHECK(UI::DockBuilderFinish(0xD0C5u) && UI::GetDockLayout().WindowCount() == 13 && Valid(UI::GetDockLayout()));
    }

    // ---------------- bad arguments ----------------
    {
        Reset();
        CHECK(UI::DockBuilderAddNode(3) == 3 && UI::DockBuilderAddNode(3) == 0);        // the id is taken
        const DockId made = UI::DockBuilderAddNode(); CHECK(made != 0 && made != 3);
        DockId a = 5, b = 5;
        CHECK(UI::DockBuilderSplitNode(3, UI::DockSide_Center, 0.5f, &a, &b) == 0 && a == 5 && b == 5);
        CHECK(UI::DockBuilderSplitNode(3, UI::DockSide_Left, std::nanf(""), nullptr, nullptr) == 0);
        CHECK(UI::DockBuilderSplitNode(3, UI::DockSide_Left, INFINITY, nullptr, nullptr) == 0);
        CHECK(UI::DockBuilderSplitNode(999, UI::DockSide_Left, 0.5f, nullptr, nullptr) == 0);
        CHECK(UI::DockBuilderSplitNode(0, UI::DockSide_Left, 0.5f, nullptr, nullptr) == 0);
        const DockId n1 = UI::DockBuilderSplitNode(3, UI::DockSide_Left, 5.0f, nullptr, nullptr);   // clamped, not refused
        CHECK(n1 != 0 && UI::DockBuilderGetNode(3).ratio == 0.98f);
        CHECK(UI::DockBuilderSplitNode(3, UI::DockSide_Left, 0.5f, nullptr, nullptr) == 0);          // already split
        UI::DockBuilderDockWindow(nullptr, 3); UI::DockBuilderDockWindow("", 3); UI::DockBuilderDockWindow("W", 777);
        CHECK(UI::DockBuilderGetNode(3).windows.empty());
        UI::DockBuilderSetNodeSize(3, UI::Vec2{std::nanf(""), 5}); UI::DockBuilderSetNodeSize(3, UI::Vec2{-1, 5}); UI::DockBuilderSetNodeSize(555, UI::Vec2{5, 5});
        CHECK(!UI::DockBuilderFinish(999) && !UI::DockBuilderFinish(0));
        CHECK(!UI::DockBuilderFinish(3));                                                           // a plan with no window at all
        CHECK(!UI::DockBuilderGetNode(0).found && !UI::DockBuilderGetNode(424242).found);
        UI::SetNextWindowDockID(5); UI::DockBuilderReset(); Sim s(100, 100); RunFrame(s, UI::Rect{0, 0, 100, 100}, {"Z"}); CHECK(UI::GetDockLayout().IsFloating("Z"));
        CHECK(UI::GetWindowDockID(nullptr) == 0 && UI::GetWindowDockID("") == 0);
        UI::ResetState(); CHECK(!UI::DockBuilderGetNode(3).found);                                   // ResetState forgets the plans too
    }

    // ---------------- a long window name is cut like the layout cuts it ----------------
    {
        Reset();
        UI::DockBuilderAddNode(1);
        std::string longName(200, 'n'); longName += " with spaces and %percent";
        UI::DockBuilderDockWindow(longName.c_str(), 1); UI::DockBuilderDockWindow("Name With Spaces", 1);
        CHECK(UI::DockBuilderFinish(1) && Valid(UI::GetDockLayout()) && UI::GetDockLayout().WindowCount() == 2);
        CHECK(UI::GetDockLayout().Contains("Name With Spaces") && UI::GetDockLayout().Contains(std::string(DockLayout::kMaxNameLen, 'n')));
    }

    // ---------------- random calls: the layout stays valid ----------------
    {
        Reset();
        unsigned seed = 12345u;
        auto rnd = [&](unsigned n) { seed = seed * 1664525u + 1013904223u; return (seed >> 8) % n; };
        std::vector<DockId> ids = {1};
        int finishes = 0;
        UI::DockBuilderAddNode(1);
        for (int i = 0; i < 4000; ++i) {
            const DockId id = rnd(10) == 0 ? (DockId)rnd(60) : ids[rnd((unsigned)ids.size())];
            switch (rnd(9)) {
            case 0: { DockId o = 0; const DockId nw = UI::DockBuilderSplitNode(id, (UI::DockSide)rnd(5), (float)rnd(120) / 100.0f - 0.1f, nullptr, &o); if (nw) { ids.push_back(nw); ids.push_back(o); } break; }
            case 1: case 2: case 3: { const std::string w = "w" + std::to_string(rnd(24)); UI::DockBuilderDockWindow(w.c_str(), id); break; }
            case 4: UI::DockBuilderSetNodeSize(id, UI::Vec2{(float)rnd(2000), (float)rnd(1200)}); break;
            case 5: if (UI::DockBuilderFinish(id)) { ++finishes; CHECK(Valid(UI::GetDockLayout())); } break;
            case 6: if (rnd(4) == 0) { UI::DockBuilderRemoveNode(id); } break;
            case 7: { const DockId nid = UI::DockBuilderAddNode(rnd(4) == 0 ? (DockId)rnd(60) : 0); if (nid) ids.push_back(nid); break; }
            default: { UI::SetNextWindowDockID(id, (UI::Cond)rnd(4)); const std::string w = "w" + std::to_string(rnd(30)); Sim s(300, 200); RunFrame(s, UI::Rect{0, 0, 300, 200}, {w}); CHECK(Valid(UI::GetDockLayout())); }
            }
        }
        CHECK(Valid(UI::GetDockLayout()));
        std::printf("  random run: %d finishes, %d windows\n", finishes, UI::GetDockLayout().WindowCount());
        CHECK(finishes > 20);
    }
    TEST_MAIN_RESULT();
}
