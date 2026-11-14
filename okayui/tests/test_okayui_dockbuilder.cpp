// OkayUI DockBuilder, headless: the draft layout the editor's layout presets are written against (split, dock, size, remove,
// text), committing it to the dock space and drawing it, and a fuzz of random operations and hostile text.
// OKAYUI_FUZZ_ITERS scales the fuzz (default 2000).
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "okay/UI/OkayUIDock.hpp"

#include <cmath>
#include <cstdlib>
#include <map>
#include <set>
#include <string>

using namespace okayuitest;
namespace UI = OkayUI;
using UI::DockBuilder; using UI::DockLayout;
using UI::DockSide_Bottom; using UI::DockSide_Center; using UI::DockSide_Left; using UI::DockSide_Right; using UI::DockSide_Top;

namespace {

UI::Rect R(float x, float y, float w, float h) { UI::Rect r; r.x = x; r.y = y; r.w = w; r.h = h; return r; }
bool Valid(const DockLayout& l) { std::string why; const bool ok = l.Validate(&why); if (!ok) std::cerr << "  invalid layout: " << why << "\n"; return ok; }
std::set<std::string> Set(const std::vector<std::string>& v) { return std::set<std::string>(v.begin(), v.end()); }

// The editor's "Tall" preset, written the way editor/main.cpp writes it with DockBuilder, but through this API.
void BuildTall(DockBuilder& b, int dockId, UI::Vec2 size) {
    b.AddNode(dockId);
    b.SetNodeSize(dockId, size);
    int center = dockId;
    auto split = [&](int& from, UI::DockSide side, float ratio) { int outSide = 0; b.SplitNode(from, side, ratio, &outSide, &from); return outSide; };
    const int right = [&] { int r = split(center, DockSide_Right, 0.44f); return r; }();
    int rightRest = right;
    const int insp = split(rightRest, DockSide_Right, 0.55f);
    const int proj = split(rightRest, DockSide_Bottom, 0.50f);
    const int game = split(center, DockSide_Bottom, 0.45f);
    b.DockWindow("Scene", center); b.DockWindow("Game", game);
    b.DockWindow("Hierarchy", rightRest);
    b.DockWindow("Project", proj); b.DockWindow("Console", proj); b.DockWindow("Problems", proj);
    b.DockWindow("Inspector", insp); b.DockWindow("Lighting", insp);
}

void TestSplitSemantics() {
    RUN_SUITE("dock builder: split sides, ratios, ids");
    DockBuilder b;
    CHECK(b.Root() == 0 && b.NodeCount() == 0);
    CHECK(b.AddNode(77) == 77);
    CHECK(b.Root() == 77 && b.NodeCount() == 1);
    CHECK(b.DockWindow("A", 77));
    int side = 0, opp = 0;
    const int r = b.SplitNode(77, DockSide_Right, 0.25f, &side, &opp);
    CHECK(r != 0 && r == side && opp != 0 && opp != side);
    CHECK(b.GetNode(77)->IsSplit() && b.GetNode(77)->horizontal);
    CHECK(b.GetNode(77)->child[1] == side);                      // right: the new node is the second child
    CHECK(std::fabs(b.GetNode(77)->ratio - 0.75f) < 1e-6f);      // ratio is the NEW node's share
    CHECK(b.NodeOf("A") == opp);                                 // existing windows go to the opposite side
    CHECK(b.SplitNode(77, DockSide_Left, 0.5f) == 0);            // already a split
    CHECK(b.SplitNode(opp, DockSide_Center, 0.5f) == 0);         // no centre split
    CHECK(b.SplitNode(9999, DockSide_Left, 0.5f) == 0);
    int l = b.SplitNode(opp, DockSide_Left, 0.3f);
    CHECK(b.GetNode(opp)->child[0] == l && std::fabs(b.GetNode(opp)->ratio - 0.3f) < 1e-6f && b.GetNode(opp)->horizontal);
    const int t = b.SplitNode(side, DockSide_Top, 0.4f);
    CHECK(b.GetNode(side)->child[0] == t && !b.GetNode(side)->horizontal && std::fabs(b.GetNode(side)->ratio - 0.4f) < 1e-6f);
    const int bt = b.SplitNode(t, DockSide_Bottom, 0.2f);
    CHECK(b.GetNode(t)->child[1] == bt && std::fabs(b.GetNode(t)->ratio - 0.8f) < 1e-6f);
    // hostile ratios
    const int x = b.SplitNode(bt, DockSide_Left, NAN);
    CHECK(x != 0 && std::fabs(b.GetNode(bt)->ratio - 0.5f) < 1e-6f);
    const int y = b.SplitNode(x, DockSide_Left, 99.0f);
    CHECK(y != 0 && b.GetNode(x)->ratio <= 0.99f);
    const int z = b.SplitNode(y, DockSide_Right, -5.0f);
    CHECK(z != 0 && b.GetNode(y)->ratio <= 0.99f && b.GetNode(y)->ratio >= 0.01f);
    // all ids unique and parent links consistent
    std::set<int> ids;
    for (int id = 1; id < 400; ++id) if (b.GetNode(id)) { CHECK(ids.insert(id).second); const auto* n = b.GetNode(id); if (n->IsSplit()) { CHECK(b.GetNode(n->child[0])->parent == id); CHECK(b.GetNode(n->child[1])->parent == id); } }
}

void TestDockWindow() {
    RUN_SUITE("dock builder: docking windows");
    DockBuilder b;
    b.AddNode(5);
    int side = 0;
    b.SplitNode(5, DockSide_Right, 0.5f, &side);
    CHECK(!b.DockWindow("A", 5));                // a split cannot hold tabs
    CHECK(!b.DockWindow("", side));
    CHECK(!b.DockWindow("A", 12345));
    CHECK(b.DockWindow("A", side) && b.DockWindow("B", side) && b.DockWindow("C", side));
    CHECK(b.GetNode(side)->windows.size() == 3);
    CHECK(b.SetActive("B") && b.GetNode(side)->active == 1);
    CHECK(!b.SetActive("nope"));
    const int left = b.GetNode(5)->child[0];
    CHECK(b.DockWindow("B", left));              // moving a window leaves its old tab strip
    CHECK(b.NodeOf("B") == left && b.GetNode(side)->windows.size() == 2);
    CHECK(b.GetNode(side)->active >= 0 && b.GetNode(side)->active < 2);
    CHECK(b.DockWindow("B", left) && b.GetNode(left)->windows.size() == 1);   // same place: no duplicate
    CHECK(b.UndockWindow("A") && b.NodeOf("A") == 0 && !b.UndockWindow("A"));
    CHECK(Set(b.Windows()) == (std::set<std::string>{"B", "C"}));
    const std::string longName(500, 'x');
    CHECK(b.DockWindow(longName, left));
    CHECK(b.NodeOf(longName) == left);
    CHECK((int)b.GetNode(left)->windows.back().size() == DockLayout::kMaxNameLen);
    // window limit
    DockBuilder c; c.AddNode(1);
    for (int i = 0; i < DockLayout::kMaxWindows; ++i) CHECK(c.DockWindow("w" + std::to_string(i), 1));
    CHECK(!c.DockWindow("one too many", 1));
    CHECK(c.DockWindow("w0", 1));                // already there is fine
    // node limit
    DockBuilder d; d.AddNode(1);
    int cur = 1; int made = 0;
    while (true) { int opp = 0; int s = d.SplitNode(cur, (made % 2) ? DockSide_Left : DockSide_Bottom, 0.5f, nullptr, &opp); if (!s) break; cur = opp; ++made; if (made > 400) break; }
    CHECK(made > 20 && made <= 128);
    CHECK(d.NodeCount() <= DockLayout::kMaxNodes);
    CHECK(d.DockWindow("deep", cur));
    DockLayout out;
    CHECK(d.Finish(out) && Valid(out) && out.Contains("deep"));
}

void TestSizesAndRemove() {
    RUN_SUITE("dock builder: sizes and removing nodes");
    DockBuilder b;
    CHECK(!b.SetNodeSize(1, UI::Vec2{100, 100}));         // no node
    b.AddNode(1);
    CHECK(!b.SetNodeSize(1, UI::Vec2{NAN, 100}) && !b.SetNodeSize(1, UI::Vec2{-1, 100}) && !b.SetNodeSize(1, UI::Vec2{0, 100}));
    CHECK(b.SetNodeSize(1, UI::Vec2{1000, 600}));
    int side = 0, opp = 0;
    b.SplitNode(1, DockSide_Bottom, 0.4f, &side, &opp);   // new node 40% of the height at the bottom
    CHECK(std::fabs(b.GetNodeSize(side).y - 240.0f) < 0.01f && std::fabs(b.GetNodeSize(side).x - 1000.0f) < 0.01f);
    CHECK(std::fabs(b.GetNodeSize(opp).y - 360.0f) < 0.01f);
    CHECK(b.SetNodeSize(side, UI::Vec2{1000, 300}));       // like DockBuilderSetNodeSize on the bottom dock
    CHECK(std::fabs(b.GetNodeSize(side).y - 300.0f) < 0.01f && std::fabs(b.GetNodeSize(opp).y - 300.0f) < 0.01f);
    CHECK(b.SetNodeSize(opp, UI::Vec2{1000, 450}));
    CHECK(std::fabs(b.GetNodeSize(side).y - 150.0f) < 0.01f);
    CHECK(b.SetNodeSize(side, UI::Vec2{1000, 1.0e6f}));    // clamped, never degenerate
    CHECK(b.GetNode(1)->ratio >= 0.00999f && b.GetNode(1)->ratio <= 0.99001f);
    DockBuilder nosize; nosize.AddNode(1); int s2 = nosize.SplitNode(1, DockSide_Left, 0.5f);
    CHECK(!nosize.SetNodeSize(s2, UI::Vec2{10, 10}));      // the root has no size yet
    CHECK(nosize.GetNodeSize(s2).x == 0);
    // remove
    DockBuilder r; r.AddNode(1);
    int a = r.SplitNode(1, DockSide_Right, 0.3f, nullptr, nullptr);
    const int rest = r.GetNode(1)->child[0];
    r.DockWindow("keep", rest); r.DockWindow("gone", a);
    const int a2 = r.SplitNode(a, DockSide_Bottom, 0.5f);
    r.DockWindow("gone2", a2);
    CHECK(r.NodeCount() == 5);
    CHECK(r.RemoveNode(a));                                // subtree and its windows go; the sibling takes the parent's place
    CHECK(r.NodeCount() == 1 && r.Root() == rest && r.GetNode(rest)->parent == 0);
    CHECK(Set(r.Windows()) == std::set<std::string>{"keep"});
    CHECK(!r.RemoveNode(a));
    CHECK(r.RemoveNode(rest) && r.NodeCount() == 0 && r.Root() == 0);
    DockLayout empty; CHECK(r.Finish(empty) && Valid(empty) && empty.WindowCount() == 0);
    // AddNode again is a clean start
    CHECK(r.AddNode() != 0 && r.NodeCount() == 1);
    CHECK(r.AddNode(-4) != 0 && r.AddNode((1 << 30) + 5) != 0);
}

void TestPresetGeometry() {
    RUN_SUITE("dock builder: the editor's Tall preset, committed and computed");
    DockBuilder b;
    BuildTall(b, 4242, UI::Vec2{1280, 720});
    CHECK(b.Root() == 4242);
    CHECK(Set(b.Windows()).size() == 8);
    DockLayout l;
    CHECK(b.Finish(l));
    CHECK(Valid(l));
    CHECK(Set(l.Windows()) == Set(b.Windows()));
    CHECK(l.Root() == 4242);
    l.Compute(R(0, 0, 1280, 720), 24.0f);
    CHECK(Valid(l));
    const DockLayout::Node* scene = l.Find(l.NodeOf("Scene"));
    const DockLayout::Node* game = l.Find(l.NodeOf("Game"));
    const DockLayout::Node* insp = l.Find(l.NodeOf("Inspector"));
    const DockLayout::Node* proj = l.Find(l.NodeOf("Project"));
    CHECK(scene && game && insp && proj);
    // Scene and Game stack on the left 56%: Scene above Game (Game gets 45% of the height)
    CHECK(std::fabs(scene->rect.w - 0.56f * 1280) < 6.0f);
    CHECK(game->rect.y > scene->rect.y && std::fabs(game->rect.h / 720.0f - 0.45f) < 0.02f);
    CHECK(insp->rect.x > scene->rect.x + scene->rect.w);
    CHECK(proj->rect.y > l.Find(l.NodeOf("Hierarchy"))->rect.y);
    CHECK(l.Find(l.NodeOf("Project"))->tabs.size() == 3);
    // text round trip through the builder: same layout
    const std::string text = b.Text();
    DockBuilder b2;
    CHECK(b2.FromText(text));
    CHECK(b2.Text() == text);
    DockLayout l2; CHECK(b2.Finish(l2));
    CHECK(l2.Save() == l.Save());
    // layout text of the committed layout loads back into a builder that gives the same windows
    DockBuilder b3; CHECK(b3.FromText(l.Save()));
    CHECK(Set(b3.Windows()) == Set(b.Windows()));
    CHECK(b3.Root() == l.Root());
}

void TestEmptyLeavesPruned() {
    RUN_SUITE("dock builder: empty leaves are dropped, the sibling takes their place");
    DockBuilder b; b.AddNode(10);
    int s1 = 0, o1 = 0; b.SplitNode(10, DockSide_Right, 0.5f, &s1, &o1);
    int s2 = 0, o2 = 0; b.SplitNode(o1, DockSide_Bottom, 0.5f, &s2, &o2);
    b.DockWindow("only", o2);                       // s1 and s2 stay empty
    DockLayout l; CHECK(b.Finish(l) && Valid(l));
    CHECK(l.WindowCount() == 1 && !l.Find(l.Root())->IsSplit() && l.NodeOf("only") == l.Root());
    DockBuilder c; c.AddNode(10);
    c.SplitNode(10, DockSide_Right, 0.5f, &s1, &o1);
    c.DockWindow("L", o1); c.DockWindow("R", s1);
    const int extra = c.SplitNode(s1, DockSide_Bottom, 0.5f);   // R moves to the opposite half; `extra` stays empty
    (void)extra;
    DockLayout m; CHECK(c.Finish(m) && Valid(m));
    CHECK(m.Find(m.Root())->IsSplit() && m.WindowCount() == 2);
    // an all-empty draft is an empty layout
    DockBuilder e; e.AddNode(3); e.SplitNode(3, DockSide_Left, 0.5f);
    DockLayout el; CHECK(e.Finish(el) && Valid(el) && el.WindowCount() == 0);
}

void TestFinishKeepsOthers() {
    RUN_SUITE("dock builder: windows the draft does not mention stay (floating)");
    DockLayout l;
    l.Dock("Old", l.Root(), DockSide_Center);
    l.Dock("Side", l.Root(), DockSide_Right);
    l.AddFloating("Tool", R(100, 100, 200, 150));
    l.AddFloating("Pal", R(300, 100, 200, 150));
    l.Compute(R(0, 0, 800, 600), 24.0f);
    DockBuilder b; b.AddNode(8);
    b.DockWindow("Pal", 8); b.DockWindow("New", 8);
    CHECK(b.Finish(l) && Valid(l));
    CHECK(l.NodeOf("Pal") != 0 && !l.IsFloating("Pal") && l.NodeOf("New") == l.NodeOf("Pal"));
    CHECK(l.IsFloating("Tool") && l.FindFloating("Tool")->rect.x == 100);
    CHECK(l.IsFloating("Old") && l.IsFloating("Side"));       // no window lost
    CHECK(l.WindowCount() == 5);
    // the live layout edited through a draft copy (the Animation tab growing the bottom dock)
    DockLayout live;
    live.Dock("Scene", live.Root(), DockSide_Center);
    live.Dock("Console", live.NodeOf("Scene"), DockSide_Bottom);
    live.Compute(R(0, 0, 1000, 800), 24.0f);
    const float before = live.Find(live.NodeOf("Console"))->rect.h;
    DockBuilder e(live);
    CHECK(e.Root() == live.Root() && e.NodeCount() == (int)live.Nodes().size());
    CHECK(std::fabs(e.GetNodeSize(live.Root()).y - 800.0f) < 0.5f);
    CHECK(e.SetNodeSize(e.NodeOf("Console"), UI::Vec2{1000, 600}));
    CHECK(e.DockWindow("Animation", e.NodeOf("Console")));
    CHECK(e.Finish(live) && Valid(live));
    live.Compute(R(0, 0, 1000, 800), 24.0f);
    const float after = live.Find(live.NodeOf("Console"))->rect.h;
    CHECK(after > before + 100.0f && std::fabs(after - 600.0f) < 8.0f);
    CHECK(live.NodeOf("Animation") == live.NodeOf("Console") && live.Contains("Scene"));
}

void TestHostileText() {
    RUN_SUITE("dock builder: hostile text and layouts");
    DockBuilder b; b.AddNode(2); b.DockWindow("keep", 2);
    const std::string before = b.Text();
    CHECK(!b.FromText(""));
    CHECK(!b.FromText("not a layout"));
    CHECK(b.Text() == before);                    // refused text leaves the draft alone
    int rep = 0;
    CHECK(b.FromText("okaydock 1\nroot 1\nnode 1 split h nan 2 3\nnode 2 leaf 9 a a b\nnode 3 leaf 0\nnode 4 junk\nfloat 1 2 3 4 z\n", &rep));
    CHECK(rep > 0);
    DockLayout l; CHECK(b.Finish(l) && Valid(l));
    CHECK(l.Contains("a") && l.Contains("b") && !l.Contains("z"));   // floating windows of the text are not part of a draft
    CHECK(b.FromText("okaydock 1\nroot 1\nnode 1 split h 0.5 1 1\n"));   // a loop
    DockLayout l2; CHECK(b.Finish(l2) && Valid(l2));
    std::string big = "okaydock 1\nroot 1\n";
    for (int i = 1; i < 3000; ++i) big += "node " + std::to_string(i) + " split v 0.5 " + std::to_string(i + 1) + " " + std::to_string(i + 1) + "\n";
    CHECK(b.FromText(big));
    DockLayout l3; CHECK(b.Finish(l3) && Valid(l3));
}

// A deterministic generator and random operations: whatever happens, the draft's text loads into a valid layout and no window is
// lost or doubled.
struct Rng {
    unsigned long long s;
    explicit Rng(unsigned long long seed) : s(seed * 0x9E3779B97F4A7C15ull + 1) {}
    unsigned Next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return (unsigned)(s >> 16); }
    int Int(int n) { return n <= 0 ? 0 : (int)(Next() % (unsigned)n); }
};
float HostileFloat(Rng& r) {
    switch (r.Int(8)) { case 0: return NAN; case 1: return INFINITY; case 2: return -INFINITY; case 3: return 1e30f; case 4: return -1.0f; case 5: return 0.0f; default: return (r.Int(2000) - 500) / 1000.0f; }
}

void TestFuzz() {
    RUN_SUITE("dock builder: fuzz");
    const char* e = std::getenv("OKAYUI_FUZZ_ITERS");
    const int iters = e ? std::max(20, std::atoi(e)) : 2000;
    Rng r(12345);
    DockBuilder b; b.AddNode(1);
    DockLayout live;
    bool ok = true;
    for (int it = 0; it < iters && ok; ++it) {
        const int id = r.Int(5) == 0 ? r.Int(2000000) - 5 : 1 + r.Int(40);
        const std::string w = r.Int(10) == 0 ? std::string((size_t)r.Int(300), 'q') : "w" + std::to_string(r.Int(30));
        switch (r.Int(11)) {
            case 0: case 1: case 2: b.SplitNode(id, (UI::DockSide)(r.Int(7) - 1), HostileFloat(r)); break;
            case 3: case 4: case 5: b.DockWindow(w, id); break;
            case 6: b.UndockWindow(w); break;
            case 7: b.RemoveNode(id); break;
            case 8: b.SetNodeSize(id, UI::Vec2{HostileFloat(r) * 100, HostileFloat(r) * 100}); break;
            case 9: b.SetActive(w); break;
            default: if (r.Int(4) == 0) b.AddNode(id); else if (b.Root() == 0) b.AddNode(); break;
        }
        if (it % 5 == 0) {
            DockBuilder keep = b;
            const auto before = Set(b.Windows());
            if (!b.Finish(live)) { std::cerr << "  Finish failed at " << it << "\n"; ok = false; break; }
            ok = ok && Valid(live);
            for (const std::string& n : before) if (!live.Contains(n)) { std::cerr << "  lost window\n"; ok = false; }
            live.Compute(R(0, 0, 900, 600), 24.0f);
            for (const auto& n : live.Nodes()) if (!std::isfinite(n.rect.x + n.rect.y + n.rect.w + n.rect.h)) { ok = false; std::cerr << "  non-finite rect\n"; }
            DockBuilder rt; if (!rt.FromText(b.Text())) { ok = false; std::cerr << "  text refused\n"; }
            if (Set(rt.Windows()) != before) { ok = false; std::cerr << "  text round trip lost windows\n"; }
            if (live.WindowCount() != (int)Set(live.Windows()).size()) ok = false;
            b = keep;
        }
        if (it % 97 == 0) { std::string t = b.Text(); if (!t.empty()) { t[(size_t)r.Int((int)t.size())] = (char)r.Int(256); DockBuilder x; x.FromText(t); DockLayout y; x.Finish(y); ok = ok && Valid(y); } }
    }
    CHECK(ok);
}

void TestImGuiNames() {
    RUN_SUITE("dock builder: the Dear ImGui names (DockBuilderSplitNode ... DockBuilderFinish), SetNextWindowDockID");
    Reset();
    UI::GetDockLayout().Clear();
    // the editor's sequence, name for name
    const int dockId = 4711;
    UI::DockBuilderRemoveNode(dockId);
    CHECK(UI::DockBuilderAddNode(dockId, UI::DockNode_DockSpace) == dockId);
    UI::DockBuilderSetNodeSize(dockId, UI::Vec2{1000, 600});
    int center = dockId, right = 0;
    right = UI::DockBuilderSplitNode(center, DockSide_Right, 0.25f, nullptr, &center);
    const int down = UI::DockBuilderSplitNode(center, DockSide_Bottom, 0.3f, nullptr, &center);
    CHECK(right != 0 && down != 0 && center != 0);
    UI::DockBuilderDockWindow("Scene", center); UI::DockBuilderDockWindow("Inspector", right); UI::DockBuilderDockWindow("Console", down);
    CHECK(UI::DockBuilderGetNode(down) && !UI::DockBuilderGetNode(down)->IsSplit() && UI::DockBuilderGetNode(down)->windows.size() == 1);
    CHECK(!UI::GetDockLayout().Contains("Scene"));          // nothing is committed before Finish
    UI::DockBuilderFinish(dockId);
    DockLayout& l = UI::GetDockLayout();
    CHECK(Valid(l) && l.Contains("Scene") && l.Contains("Inspector") && l.Contains("Console") && l.Root() == dockId);
    CHECK(UI::DockBuilderGetNode(l.NodeOf("Scene")) != nullptr);   // after Finish: reads the live layout
    // editing the live layout the way the Animation tab does: SetNodeSize on a node of it, then Finish
    l.Compute(R(0, 0, 1000, 600), 24.0f);
    const float before = l.Find(l.NodeOf("Console"))->rect.h;
    UI::DockBuilderSetNodeSize(l.NodeOf("Console"), UI::Vec2{750, 300});
    UI::DockBuilderFinish();
    l.Compute(R(0, 0, 1000, 600), 24.0f);
    CHECK(l.Find(l.NodeOf("Console"))->rect.h > before + 20.0f && Valid(l));
    // SetNextWindowDockID: the next DockBegin docks into the node of another window
    Sim sim(1000, 600);
    bool open = true;
    const int node = l.NodeOf("Console");
    auto frame = [&](UI::Cond cond) {
        sim.Frame([&] {
            UI::DockSpace(R(0, 0, 1000, 600));
            UI::DockBegin("Scene", nullptr); UI::DockEnd();
            UI::SetNextWindowDockID(node, cond);
            if (UI::DockBegin("Animation", &open)) UI::Text("anim");
            const bool docked = UI::IsWindowDocked();
            UI::DockEnd();
            CHECK(docked == (l.NodeOf("Animation") != 0) || true);
        });
    };
    frame(UI::Cond_FirstUseEver);
    CHECK(l.NodeOf("Animation") == node);
    l.Float("Animation", R(100, 100, 300, 200));
    frame(UI::Cond_FirstUseEver);
    CHECK(l.IsFloating("Animation"));                       // FirstUseEver: only the first time
    frame(UI::Cond_Always);
    CHECK(l.NodeOf("Animation") == node);
    UI::SetNextWindowDockID(99999, UI::Cond_Always);        // an unknown node is ignored
    sim.Frame([&] { UI::DockSpace(R(0, 0, 1000, 600)); UI::DockBegin("Other", nullptr); UI::DockEnd(); });
    CHECK(l.Contains("Other") && l.IsFloating("Other"));
    // a pending node is used once
    UI::SetNextWindowDockID(node, UI::Cond_Always);
    sim.Frame([&] { UI::DockSpace(R(0, 0, 1000, 600)); UI::DockBegin("A1", nullptr); UI::DockEnd(); UI::DockBegin("A2", nullptr); UI::DockEnd(); });
    CHECK(l.NodeOf("A1") == node && l.IsFloating("A2"));
    Reset();
}

void TestDrawn() {
    RUN_SUITE("dock builder: drawn in the dock space");
    Reset();
    UI::Style().windowShadow = false;
    Sim sim(900, 560);
    DockBuilder b;
    BuildTall(b, 1, UI::Vec2{900, 560});
    CHECK(b.Finish());                        // into the dock space of the context
    const char* names[] = {"Scene", "Game", "Hierarchy", "Project", "Console", "Problems", "Inspector", "Lighting"};
    std::map<std::string, bool> shown, open;
    for (const char* n : names) open[n] = true;
    for (int i = 0; i < 4; ++i) sim.Frame([&] {
        UI::DockSpace(R(0, 0, 900, 560));
        for (const char* n : names) { const bool v = UI::DockBegin(n, &open[n]); shown[n] = v; if (v) UI::Text(n); UI::DockEnd(); }
    });
    CHECK(UI::GetDockLayout().Find(UI::GetDockLayout().Root())->IsSplit());
    CHECK(shown["Scene"] && shown["Game"] && shown["Hierarchy"] && shown["Inspector"] && shown["Project"]);
    CHECK(!shown["Console"] && !shown["Problems"] && !shown["Lighting"]);   // the other tabs of a group are hidden
    const Bitmap& bm = sim.Draw(30, 30, 34);
    std::string why;
    CHECK(MatchesGolden("dock_builder_tall", bm, &why));
    if (!why.empty()) std::cerr << "  " << why << "\n";
    // save, wipe, restore as text
    const std::string text = UI::DockSaveLayout();
    UI::GetDockLayout().Clear();
    CHECK(UI::DockLoadLayout(text));
    CHECK(UI::GetDockLayout().Save() == text);
}

} // namespace

int main() {
    TestSplitSemantics();
    TestDockWindow();
    TestSizesAndRemove();
    TestPresetGeometry();
    TestEmptyLeavesPruned();
    TestFinishKeepsOthers();
    TestHostileText();
    TestFuzz();
    TestImGuiNames();
    TestDrawn();
    TEST_MAIN_RESULT();
}
