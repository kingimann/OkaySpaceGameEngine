// The dock layout builder (okayui/include/okay/UI/OkayUIDockBuilder.hpp): the pure tree, building it into a DockLayout, an editor-style
// preset written with the builder calls, the context calls driving a live dock space, and a fuzz of random operations.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "okay/UI/OkayUIDockBuilder.hpp"

#include <cmath>
#include <limits>
#include <set>
#include <string>

using namespace okayuitest;
namespace UI = OkayUI;

namespace {
std::set<std::string> Names(const UI::DockLayout& l) { const auto w = l.Windows(); return std::set<std::string>(w.begin(), w.end()); }
UI::Rect ContentOf(const UI::DockLayout& l, const std::string& w) { return l.ContentRect(l.NodeOf(w)); }
struct Rng { unsigned s; unsigned Next() { s = s * 1664525u + 1013904223u; return s >> 8; } float F() { return (Next() & 0xffff) / 65535.0f; } };
}

int main() {
    RUN_SUITE("okayui dock builder");
    {   // the tree: add, split, dock, remove
        UI::DockBuilderTree t;
        const unsigned root = t.AddNode(7);
        CHECK(root == 7 && t.GetNode(7) && !t.GetNode(7)->IsSplit());
        CHECK(t.DockWindow("Scene", 7));
        CHECK(!t.DockWindow("", 7) && !t.DockWindow("X", 99));
        unsigned side = 0, opp = 0;
        const unsigned s = t.SplitNode(7, UI::Dir_Right, 0.3f, &side, &opp);
        CHECK(s == side && side != 0 && opp != 0 && side != opp);
        CHECK(t.GetNode(7)->IsSplit() && t.GetNode(7)->windows.empty());
        CHECK(t.GetNode(opp)->windows.size() == 1 && t.GetNode(opp)->windows[0] == "Scene");   // what the node held moved to the opposite part
        CHECK(t.GetNode(side)->windows.empty());
        CHECK(!t.DockWindow("Y", 7));                                                          // a split holds no windows
        CHECK(t.DockWindow("Inspector", side));
        CHECK(t.DockWindow("Scene", side));                                                    // moves
        CHECK(t.NodeOfWindow("Scene") == side && t.GetNode(opp)->windows.empty());
        std::string why; CHECK(t.Validate(&why));
        // split a split: the subtree moves under the opposite id
        unsigned s2 = 0, o2 = 0;
        CHECK(t.SplitNode(7, UI::Dir_Down, 0.4f, &s2, &o2) != 0);
        CHECK(t.GetNode(o2)->IsSplit() && t.GetNode(o2)->child[0] == side && t.GetNode(side)->parent == o2);
        CHECK(t.Validate(&why));
        // remove a leaf: its sibling takes the parent's place
        t.RemoveNode(s2);
        CHECK(!t.GetNode(s2));
        CHECK(t.GetNode(7) && t.GetNode(7)->IsSplit() && t.Validate(&why));
        CHECK(t.NodeOfWindow("Scene") != 0);
        t.RemoveNode(12345);                                                                   // unknown: nothing happens
        t.RemoveNode(7);
        CHECK(t.NodeCount() == 0);
        // bad arguments
        CHECK(t.SplitNode(1, UI::Dir_Left, 0.5f) == 0);
        const unsigned r = t.AddNode();
        CHECK(r != 0);
        CHECK(t.SplitNode(r, 9, 0.5f) == 0 && t.SplitNode(r, UI::Dir_None, 0.5f) == 0);
        const float nan = std::numeric_limits<float>::quiet_NaN();
        unsigned sn = 0; CHECK(t.SplitNode(r, UI::Dir_Left, nan, &sn) != 0);
        CHECK(t.GetNode(r)->ratio == 0.5f);
        unsigned sx = 0; t.SplitNode(sn, UI::Dir_Left, 5.0f, &sx);
        CHECK(t.GetNode(sn)->ratio <= 0.95f);
        t.SplitNode(sx, UI::Dir_Left, -3.0f, &sx);
        CHECK(t.Validate(&why));
    }
    {   // building: sides, ratios, tabs, empty parts dropped
        UI::DockBuilderTree t;
        const unsigned root = t.AddNode(1);
        unsigned left = 0, rest = 0;
        t.SplitNode(root, UI::Dir_Left, 0.25f, &left, &rest);
        unsigned bottom = 0, mid = 0;
        t.SplitNode(rest, UI::Dir_Down, 0.3f, &bottom, &mid);
        unsigned ghost = 0, keep = 0;
        t.SplitNode(mid, UI::Dir_Right, 0.5f, &ghost, &keep);          // the side part gets no windows: dropped
        t.DockWindow("Hierarchy", left);
        t.DockWindow("Scene", keep); t.DockWindow("Game", keep); t.DockWindow("Script", keep);
        t.DockWindow("Console", bottom); t.DockWindow("Problems", bottom);
        UI::DockLayout l;
        CHECK(t.Build(root, l));
        std::string why; CHECK(l.Validate(&why));
        CHECK(Names(l) == (std::set<std::string>{"Hierarchy", "Scene", "Game", "Script", "Console", "Problems"}));
        CHECK(l.NodeOf("Scene") == l.NodeOf("Game") && l.NodeOf("Scene") == l.NodeOf("Script"));
        const UI::DockLayout::Node* sc = l.Find(l.NodeOf("Scene"));
        CHECK(sc->tabs.size() == 3 && sc->tabs[0] == "Scene" && sc->tabs[(size_t)sc->active] == "Scene");   // tab order, first chosen
        CHECK(l.Nodes().size() == 5);                                  // three leaves and two splits: the ghost part left no leaf
        l.Compute(UI::Rect{0, 0, 1000, 600}, 20);
        const UI::Rect h = ContentOf(l, "Hierarchy"), s = ContentOf(l, "Scene"), c = ContentOf(l, "Console");
        CHECK(h.x == 0 && h.w > 200 && h.w < 260);                     // the left column, about a quarter
        CHECK(s.x > h.x + h.w && s.y < c.y);                           // Scene right of it, above the Console
        CHECK(c.y > 300);                                              // the bottom part is below the middle
        CHECK(c.x > h.x + h.w);                                        // and also right of the left column (the down split was inside "rest")
        CHECK(std::fabs((c.h + 20) / 600.0f - 0.3f) < 0.06f);          // a 0.3 share (with its tab strip)
        // a tree with no windows builds an empty layout; an unknown root builds nothing
        UI::DockBuilderTree e; const unsigned er = e.AddNode(5);
        UI::DockLayout empty; empty.AddFloating("Z", UI::Rect{});
        CHECK(e.Build(er, empty) && empty.WindowCount() == 0 && empty.Validate());
        UI::DockLayout untouched; untouched.AddFloating("Q", UI::Rect{});
        CHECK(!e.Build(99, untouched) && untouched.WindowCount() == 1);
        // Right and Down sides: the side part is the second child, its ratio is its own share
        UI::DockBuilderTree t2; const unsigned r2 = t2.AddNode(1);
        unsigned sd = 0, op = 0; t2.SplitNode(r2, UI::Dir_Right, 0.3f, &sd, &op);
        t2.DockWindow("A", op); t2.DockWindow("B", sd);
        UI::DockLayout l2; CHECK(t2.Build(r2, l2));
        l2.Compute(UI::Rect{0, 0, 1000, 500}, 20);
        const UI::Rect a = ContentOf(l2, "A"), b = ContentOf(l2, "B");
        CHECK(a.x < b.x && std::fabs(b.w / 1000.0f - 0.3f) < 0.03f);
        // the layout survives Save and Load
        UI::DockLayout l3; int fixes = 0;
        CHECK(l3.Load(l.Save(), &fixes) && fixes == 0 && Names(l3) == Names(l));
    }
    {   // an editor-style preset written with the context calls (the shape of the editor's "Tall" layout)
        UI::ResetState();
        const unsigned dockId = UI::GetID("MainDockSpace");
        UI::DockBuilderRemoveNode(dockId);
        UI::DockBuilderAddNode(dockId);
        UI::DockBuilderSetNodeSize(dockId, UI::Vec2{1200, 700});
        unsigned center = dockId;
        auto split = [](unsigned& from, int dir, float ratio) {
            unsigned side = 0, opp = 0;
            UI::DockBuilderSplitNode(from, dir, ratio, &side, &opp);
            from = opp;
            return side;
        };
        const unsigned right = [&] { unsigned r = split(center, UI::Dir_Right, 0.44f); return r; }();
        unsigned rightCol = right;
        const unsigned insp = split(rightCol, UI::Dir_Right, 0.55f);
        const unsigned proj = split(rightCol, UI::Dir_Down, 0.50f);
        const unsigned game = split(center, UI::Dir_Down, 0.45f);
        UI::DockBuilderDockWindow("Scene", center); UI::DockBuilderDockWindow("Game", game);
        UI::DockBuilderDockWindow("Hierarchy", rightCol);
        UI::DockBuilderDockWindow("Project", proj); UI::DockBuilderDockWindow("Console", proj);
        UI::DockBuilderDockWindow("Inspector", insp);
        CHECK(UI::DockBuilderGetNode(dockId) != nullptr && UI::DockBuilderGetNode(dockId)->size.x == 1200);
        UI::DockBuilderFinish(dockId);
        UI::DockLayout& live = UI::GetDockLayout();
        std::string why; CHECK(live.Validate(&why));
        CHECK(live.WindowCount() == 6 && !live.IsFloating("Scene"));
        // draw it: every window gets its content area and only the chosen tab of a node answers true
        Sim sim(1200, 700);
        bool began[6] = {};
        const char* names[6] = {"Scene", "Game", "Hierarchy", "Project", "Console", "Inspector"};
        UI::Rect contents[6];
        auto ui = [&] {
            UI::DockSpace(UI::Rect{0, 0, 1200, 700});
            for (int i = 0; i < 6; ++i) {
                began[i] = UI::DockBegin(names[i]);
                if (began[i]) contents[i] = UI::GetDockLayout().ContentRect(UI::GetDockLayout().NodeOf(names[i]));
                UI::DockEnd();
            }
        };
        sim.Frames(3, ui);
        CHECK(began[0] && began[1] && began[2] && began[3] && !began[4] && began[5]);   // Console is the second tab of Project's node
        CHECK(contents[0].x < contents[2].x && contents[0].y < contents[1].y);          // Scene left of Hierarchy, above Game
        CHECK(contents[2].x < contents[5].x);                                           // Hierarchy left of Inspector
        CHECK(contents[2].y < contents[3].y);                                           // Hierarchy above Project
        {   // the picture of the preset (golden): every window shows its name
            std::string why;
            sim.Frames(2, [&] {
                UI::DockSpace(UI::Rect{0, 0, 1200, 700});
                for (int i = 0; i < 6; ++i) { if (UI::DockBegin(names[i])) { UI::Text(names[i]); } UI::DockEnd(); }
            });
            const bool ok = MatchesGolden("dockbuilder_tall_preset", sim.Draw(28, 30, 36), &why);
            if (!ok) std::cerr << "  golden dockbuilder_tall_preset: " << why << "\n";
            CHECK(ok);
        }
        // finishing again replaces the layout; a second preset swaps cleanly
        UI::DockBuilderRemoveNode(dockId);
        UI::DockBuilderAddNode(dockId);
        UI::DockBuilderDockWindow("Scene", dockId);
        UI::DockBuilderFinish(dockId);
        CHECK(UI::GetDockLayout().WindowCount() == 1 && UI::GetDockLayout().Validate());
        UI::DockBuilderDockWindow(nullptr, dockId);                                     // hostile: harmless
        UI::DockBuilderSplitNode(0, UI::Dir_Left, 0.5f); UI::DockBuilderSetNodeSize(77, UI::Vec2{});
    }
    {   // fuzz: random operations keep the tree valid, and what builds is a valid layout holding the windows
        for (unsigned seed = 1; seed <= 40; ++seed) {
            Rng r{seed * 2654435761u};
            UI::DockBuilderTree t;
            std::vector<unsigned> ids;
            ids.push_back(t.AddNode(1));
            std::set<std::string> docked;
            for (int step = 0; step < 120; ++step) {
                const unsigned pick = ids.empty() ? 0 : ids[r.Next() % ids.size()];
                switch (r.Next() % 6) {
                    case 0: case 1: { unsigned a = 0, b = 0; if (t.SplitNode(pick, (int)(r.Next() % 4), r.F(), &a, &b)) { ids.push_back(a); ids.push_back(b); } break; }
                    case 2: case 3: { char nm[8]; std::snprintf(nm, sizeof nm, "w%u", r.Next() % 12); if (t.DockWindow(nm, pick)) docked.insert(nm); break; }
                    case 4: t.RemoveNode(pick); break;
                    default: t.SetNodeSize(pick, UI::Vec2{r.F() * 500, r.F() * 500}); break;
                }
                std::string why;
                if (!t.Validate(&why)) { std::cerr << "seed " << seed << " step " << step << ": " << why << "\n"; CHECK(false); break; }
            }
            for (unsigned root : ids) {
                if (!t.GetNode(root) || t.GetNode(root)->parent) continue;
                UI::DockLayout l;
                const bool ok = t.Build(root, l);
                std::string why;
                if (!ok || !l.Validate(&why)) { std::cerr << "seed " << seed << ": build failed " << why << "\n"; CHECK(false); break; }
            }
        }
        CHECK(true);
    }
    TEST_MAIN_RESULT();
}
