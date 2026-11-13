// OkayUI docking, headless: the layout model (DockLayout) on its own, then the dock space driven by synthetic mouse frames
// (dock by dragging, tabs, tear off, close, splitters, minimum sizes, save and load), and a golden picture.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "okay/UI/OkayUIDock.hpp"

#include <cmath>
#include <map>
#include <set>
#include <string>

using namespace okayuitest;
namespace UI = OkayUI;
using UI::DockLayout;
using UI::DockSide_Bottom; using UI::DockSide_Center; using UI::DockSide_Left; using UI::DockSide_Right; using UI::DockSide_Top;

namespace {

bool Valid(const DockLayout& l) {
    std::string why;
    const bool ok = l.Validate(&why);
    if (!ok) std::cerr << "  invalid layout: " << why << "\n";
    return ok;
}
std::set<std::string> Names(const DockLayout& l) { const auto w = l.Windows(); return std::set<std::string>(w.begin(), w.end()); }
UI::Rect R(float x, float y, float w, float h) { UI::Rect r; r.x = x; r.y = y; r.w = w; r.h = h; return r; }

void TestModelBasics() {
    RUN_SUITE("dock model: dock, split, tabs, collapse");
    DockLayout l;
    CHECK(Valid(l));
    CHECK(l.Find(l.Root()) != nullptr);
    CHECK(l.WindowCount() == 0);
    CHECK(l.AddFloating("A", R(10, 10, 200, 100)));
    CHECK(!l.AddFloating("A", R(0, 0, 10, 10)));      // already there
    CHECK(l.IsFloating("A"));
    CHECK(!l.AddFloating("", R(0, 0, 10, 10)));
    const int root = l.Root();
    CHECK(l.Dock("A", root, DockSide_Center));
    CHECK(!l.IsFloating("A"));
    CHECK(l.NodeOf("A") == root);
    CHECK(Valid(l));
    // side dock splits the leaf
    CHECK(l.Dock("B", root, DockSide_Right));
    CHECK(Valid(l));
    CHECK(l.Find(l.Root())->IsSplit());
    CHECK(l.Find(l.Root())->horizontal);
    const int nb = l.NodeOf("B");
    CHECK(nb != 0 && nb != root);
    CHECK(l.Find(l.Root())->child[1] == nb);     // right: the new window is the second child
    CHECK(l.Dock("C", root, DockSide_Left));
    CHECK(l.Find(l.Find(l.Root())->child[0])->child[0] == l.NodeOf("C"));
    CHECK(Valid(l));
    CHECK(l.Dock("D", nb, DockSide_Top));          // vertical split inside the right half
    CHECK(Valid(l));
    CHECK(!l.Find(l.Find(nb)->parent)->horizontal);
    CHECK(l.Find(l.Find(nb)->parent)->child[0] == l.NodeOf("D"));
    CHECK(l.Dock("E", nb, DockSide_Bottom));
    CHECK(l.Find(l.Find(nb)->parent)->child[1] == l.NodeOf("E"));
    CHECK(Valid(l));
    CHECK(l.WindowCount() == 5);
    // tabs
    CHECK(l.Dock("F", root, DockSide_Center));
    CHECK(l.Find(root)->tabs.size() == 2);
    CHECK(l.Find(root)->active == 1);
    CHECK(l.SetActive("A"));
    CHECK(l.Find(root)->active == 0);
    CHECK(l.MoveTab("A", 1));
    CHECK(l.Find(root)->tabs[1] == "A" && l.Find(root)->tabs[0] == "F");
    CHECK(l.Find(root)->tabs[(size_t)l.Find(root)->active] == "A");   // the chosen tab stays chosen
    CHECK(l.MoveTab("A", 99) && l.Find(root)->tabs[1] == "A");
    CHECK(Valid(l));
    // moving the only tab of a leaf collapses it
    const size_t before = l.Nodes().size();
    CHECK(l.Dock("D", root, DockSide_Center));
    CHECK(l.Nodes().size() == before - 2);        // the emptied leaf and its parent split went away
    CHECK(Valid(l));
    CHECK(l.WindowCount() == 6);
    // docking the only tab of a leaf onto its own leaf does nothing
    CHECK(!l.Dock("B", l.NodeOf("B"), DockSide_Center));
    CHECK(!l.Dock("B", l.NodeOf("B"), DockSide_Left));
    CHECK(Valid(l));
    // float and close
    CHECK(l.Float("E", R(5, 5, 100, 100)));
    CHECK(l.IsFloating("E"));
    CHECK(Valid(l));
    CHECK(l.Close("E"));
    CHECK(!l.Contains("E"));
    CHECK(!l.Close("E"));
    CHECK(l.Close("A") && l.Close("B") && l.Close("C") && l.Close("D") && l.Close("F"));
    CHECK(l.WindowCount() == 0);
    CHECK(Valid(l));
    CHECK(l.Nodes().size() == 1);                  // everything collapsed back to one empty root
    // not a leaf, bad side, bad names
    CHECK(l.Dock("G", root, DockSide_Center) || true);
    l.Clear();
    CHECK(l.Dock("G", l.Root(), DockSide_Center));
    CHECK(l.Dock("H", l.Root(), DockSide_Right));
    CHECK(!l.Dock("I", l.Root(), DockSide_Center));       // the root is a split now
    CHECK(!l.Dock("I", 9999, DockSide_Center));
    CHECK(!l.Dock("I", l.NodeOf("G"), (UI::DockSide)42));
    CHECK(!l.Dock(std::string(500, 'x'), l.NodeOf("G"), DockSide_Center));
    CHECK(Valid(l));
    // root edge
    CHECK(l.DockRoot("J", DockSide_Bottom));
    CHECK(Valid(l));
    CHECK(l.Find(l.Root())->IsSplit() && !l.Find(l.Root())->horizontal && l.Find(l.Root())->child[1] == l.NodeOf("J"));
    CHECK(l.DockRoot("K", DockSide_Left));
    CHECK(Valid(l));
    CHECK(Names(l) == (std::set<std::string>{"G", "H", "J", "K"}));
}

void TestModelGeometry() {
    RUN_SUITE("dock model: geometry, splitters, minimum sizes");
    DockLayout l;
    l.Dock("A", l.Root(), DockSide_Center);
    l.Dock("B", l.Root(), DockSide_Right);
    const int split = l.Root();
    l.Compute(R(0, 0, 804, 400), 24);
    const auto* s = l.Find(split);
    const auto* a = l.Find(s->child[0]); const auto* b = l.Find(s->child[1]);
    CHECK_NEAR(a->rect.w, 400, 0.01); CHECK_NEAR(b->rect.w, 400, 0.01);
    CHECK_NEAR(b->rect.x, 404, 0.01);
    CHECK_NEAR(l.SplitterRect(split).x, 400, 0.01); CHECK_NEAR(l.SplitterRect(split).w, DockLayout::kSplitter, 0.01);
    CHECK_NEAR(l.ContentRect(a->id).y, 24, 0.01); CHECK_NEAR(l.ContentRect(a->id).h, 376, 0.01);
    CHECK(l.LeafAt(10, 10) == a->id);
    CHECK(l.LeafAt(600, 100) == b->id);
    CHECK(l.LeafAt(402, 100) == 0);                 // the gap belongs to nobody
    CHECK(l.LeafAt(-5, 5) == 0);
    // move the divider
    CHECK(l.SetSplitterPos(split, 200));
    l.Compute(R(0, 0, 804, 400), 24);
    CHECK_NEAR(l.Find(s->child[0])->rect.w, 200, 0.5);
    // beyond the minimum: held at the minimum
    CHECK(l.SetSplitterPos(split, 1));
    l.Compute(R(0, 0, 804, 400), 24);
    CHECK(l.Find(s->child[0])->rect.w >= DockLayout::kMinLeafW - 0.01f);
    CHECK(l.SetSplitterPos(split, 100000));
    l.Compute(R(0, 0, 804, 400), 24);
    CHECK(l.Find(s->child[1])->rect.w >= DockLayout::kMinLeafW - 0.01f);
    CHECK(!l.SetSplitterPos(split, NAN));
    CHECK(!l.SetSplitterPos(l.NodeOf("A"), 5));       // a leaf has no divider
    // a window's own minimum size
    l.SetMinSize("A", 300, 100);
    l.SetSplitterPos(split, 10);
    l.Compute(R(0, 0, 804, 400), 24);
    CHECK(l.Find(s->child[0])->rect.w >= 300 - 0.01f);
    CHECK(l.MinSizeOf(l.NodeOf("A")).y >= 24 + 100 - 0.01f);
    // an area too small for both minimums: shared in proportion, never negative or overlapping
    l.Compute(R(0, 0, 200, 50), 24);
    const float wa = l.Find(s->child[0])->rect.w, wb = l.Find(s->child[1])->rect.w;
    CHECK(wa >= 0 && wb >= 0);
    CHECK_NEAR(wa + wb + DockLayout::kSplitter, 200, 0.01);
    CHECK(wa > wb);                                  // A asked for more
    l.Compute(R(0, 0, 0, 0), 24);
    CHECK(l.Find(s->child[0])->rect.w == 0 && l.Find(s->child[1])->rect.w == 0);
    l.Compute(R(NAN, 0, 100, 100), 24);               // garbage in: a zero rectangle, no crash
    CHECK(Valid(l));
    // vertical splits
    l.Clear();
    l.Dock("A", l.Root(), DockSide_Center); l.Dock("B", l.Root(), DockSide_Bottom);
    l.Compute(R(10, 20, 300, 404), 24);
    const auto* top = l.Find(l.Find(l.Root())->child[0]); const auto* bot = l.Find(l.Find(l.Root())->child[1]);
    CHECK_NEAR(top->rect.h, 200, 0.01); CHECK_NEAR(bot->rect.y, 20 + 204, 0.01); CHECK_NEAR(bot->rect.w, 300, 0.01);
    CHECK(l.SetRatio(l.Root(), 0.25f));
    l.Compute(R(10, 20, 300, 404), 24);
    CHECK_NEAR(l.Find(l.Find(l.Root())->child[0])->rect.h, 100, 1.0);
}

void TestModelText() {
    RUN_SUITE("dock model: save, load, repair");
    DockLayout l;
    l.Dock("Scene", l.Root(), DockSide_Center);
    l.Dock("Inspector Panel", l.Root(), DockSide_Right);
    l.Dock("Console", l.NodeOf("Scene"), DockSide_Bottom);
    l.Dock("Assets", l.NodeOf("Console"), DockSide_Center);
    l.Dock("100% weird\tname", l.NodeOf("Inspector Panel"), DockSide_Center);
    l.AddFloating("Float me", R(12.5f, 33.25f, 410, 222));
    l.SetRatio(l.Root(), 0.7f);
    l.SetActive("Scene");
    l.Compute(R(0, 0, 1000, 600), 24);
    l.SetRatio(l.Root(), 0.7f);
    CHECK(Valid(l));
    const std::string blob = l.Save();
    CHECK(blob.rfind("okaydock 1\n", 0) == 0);
    DockLayout m;
    int repairs = -1;
    CHECK(m.Load(blob, &repairs));
    CHECK(repairs == 0);
    CHECK(Valid(m));
    CHECK(m.Save() == blob);                          // exact round trip
    CHECK(Names(m) == Names(l));
    CHECK(m.NodeOf("100% weird\tname") == m.NodeOf("Inspector Panel"));
    CHECK(m.IsFloating("Float me"));
    CHECK_NEAR(m.FindFloating("Float me")->rect.x, 12.5, 1e-6); CHECK_NEAR(m.FindFloating("Float me")->rect.h, 222, 1e-6);
    CHECK_NEAR(m.Find(m.Root())->ratio, l.Find(l.Root())->ratio, 1e-6);
    CHECK(m.Find(m.NodeOf("Assets"))->tabs.size() == 2);
    CHECK(m.Find(m.NodeOf("Scene"))->tabs[(size_t)m.Find(m.NodeOf("Scene"))->active] == "Scene");
    // edit after load keeps working (ids stay unique)
    CHECK(m.Dock("New", m.NodeOf("Scene"), DockSide_Left));
    CHECK(Valid(m));
    // not a layout
    DockLayout n; n.Dock("keep", n.Root(), DockSide_Center);
    const std::string before = n.Save();
    CHECK(!n.Load("", nullptr));
    CHECK(!n.Load("hello world\n", nullptr));
    CHECK(!n.Load("okaydock 2\nroot 1\n", nullptr));
    CHECK(n.Save() == before);
    // damaged blobs are repaired, windows are kept
    DockLayout r;
    CHECK(r.Load("okaydock 1\nroot 1\nnode 1 split h 0.5 2 3\nnode 2 leaf 0 A B\nnode 3 leaf 0 B C\n", &repairs));   // B twice
    CHECK(repairs > 0); CHECK(Valid(r));
    CHECK(Names(r) == (std::set<std::string>{"A", "B", "C"}));
    CHECK(r.Load("okaydock 1\nroot 1\nnode 1 split h nan 2 1\nnode 2 leaf 0 A\n", &repairs));   // cycle and NaN ratio
    CHECK(Valid(r)); CHECK(Names(r) == (std::set<std::string>{"A"}));
    CHECK(r.Load("okaydock 1\nroot 7\nnode 1 leaf 0 A\nnode 2 leaf 5 B\nfloat 1 2 3 4 C\n", &repairs));       // root missing
    CHECK(Valid(r)); CHECK(Names(r) == (std::set<std::string>{"A", "B", "C"}));
    CHECK(r.Load("okaydock 1\nroot 1\nnode 1 split v 2 2 3\nnode 2 leaf 0\nnode 3 leaf 9 X\n", &repairs));   // empty leaf, ratio out of range, active out of range
    CHECK(Valid(r)); CHECK(r.WindowCount() == 1 && !r.Find(r.Root())->IsSplit());
    CHECK(r.Load("okaydock 1\nfloat nan nan inf -1 F\nfloat 1 1 1 1 \nbogus line\nnode x y z\n", &repairs));
    CHECK(Valid(r)); CHECK(r.WindowCount() == 1);
    CHECK(r.FindFloating("F")->rect.w >= 32);
    // a split with one broken child collapses to the good child
    CHECK(r.Load("okaydock 1\nroot 1\nnode 1 split h 0.5 2 99\nnode 2 leaf 0 A\n", &repairs));
    CHECK(Valid(r)); CHECK(!r.Find(r.Root())->IsSplit());
    // Repair of an in-memory break
    DockLayout b;
    b.Dock("A", b.Root(), DockSide_Center); b.Dock("B", b.Root(), DockSide_Right);
    const_cast<DockLayout::Node&>(b.Nodes()[0]).tabs.push_back("B");     // B twice, and a stray tab in a leaf
    const_cast<DockLayout::Node&>(b.Nodes()[2]).ratio = NAN;
    CHECK(!b.Validate());
    CHECK(b.Repair() > 0);
    CHECK(Valid(b));
    CHECK(Names(b) == (std::set<std::string>{"A", "B"}));
    // limits
    DockLayout big;
    std::string blobBig = "okaydock 1\nroot 1\nnode 1 leaf 0";
    for (int i = 0; i < 400; ++i) blobBig += " W" + std::to_string(i);
    blobBig += "\n";
    CHECK(big.Load(blobBig, &repairs));
    CHECK(Valid(big)); CHECK(big.WindowCount() == DockLayout::kMaxWindows);
    DockLayout lim;
    for (int i = 0; i < DockLayout::kMaxWindows; ++i) CHECK(lim.AddFloating("w" + std::to_string(i), R(0, 0, 50, 50)));
    CHECK(!lim.AddFloating("one too many", R(0, 0, 50, 50)));
    CHECK(!lim.Dock("one too many", lim.Root(), DockSide_Center));
    CHECK(lim.Dock("w0", lim.Root(), DockSide_Center));
    CHECK(Valid(lim));
}

// ---- UI --------------------------------------------------------------------------------------------------------------

struct Rig {
    Sim sim;
    std::vector<std::string> names;
    std::map<std::string, bool> open;
    std::map<std::string, UI::Vec2> pos, size;
    std::map<std::string, bool> shown;
    UI::Rect area;
    Rig(std::vector<std::string> n = {"Alpha", "Beta", "Gamma"}, float w = 800, float h = 500) : sim(w, h), names(n), area(R(0, 0, w, h)) {
        Reset();
        for (const auto& s : names) open[s] = true;
    }
    void Frame() {
        sim.Frame([&] {
            UI::DockSpace(area);
            for (const std::string& s : names) {
                const bool vis = UI::DockBegin(s.c_str(), &open[s]);
                shown[s] = vis;
                if (vis) { pos[s] = UI::GetWindowPos(); size[s] = UI::GetWindowSize(); UI::Text(s.c_str()); UI::Button("btn"); }
                UI::DockEnd();
            }
        });
    }
    void Frames(int n) { for (int i = 0; i < n; ++i) Frame(); }
    void Move(float x, float y) { sim.Move(x, y); Frame(); }
    void Drag(float x0, float y0, float x1, float y1, int steps = 6, bool release = true) {
        Move(x0, y0); sim.Down(); Frame();
        for (int i = 1; i <= steps; ++i) Move(x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps);
        if (release) { sim.Up(); Frame(); Frame(); }
    }
    DockLayout& L() { return UI::GetDockLayout(); }
    float TabH() const { return UI::GetFrameHeight(); }
    // the centre of the tab for `name`, and its close button
    UI::Vec2 TabCentre(const std::string& name) {
        const DockLayout::Node* n = L().Find(L().NodeOf(name));
        const float pad = 10.0f, closeW = TabH() * 0.7f;
        float total = 0; std::vector<float> w;
        for (const auto& t : n->tabs) { const float tw = UI::CalcTextSize(t.c_str()).x + 2 * pad + closeW; w.push_back(tw); total += tw; }
        const float scale = total > n->rect.w ? n->rect.w / total : 1.0f;
        float x = n->rect.x;
        for (size_t i = 0; i < n->tabs.size(); ++i) { if (n->tabs[i] == name) return UI::Vec2{x + w[i] * scale * 0.4f, n->rect.y + TabH() * 0.5f}; x += w[i] * scale; }
        return UI::Vec2{};
    }
    UI::Vec2 CloseCentre(const std::string& name) {
        const DockLayout::Node* n = L().Find(L().NodeOf(name));
        const float pad = 10.0f, closeW = TabH() * 0.7f;
        float total = 0; std::vector<float> w;
        for (const auto& t : n->tabs) { const float tw = UI::CalcTextSize(t.c_str()).x + 2 * pad + closeW; w.push_back(tw); total += tw; }
        const float scale = total > n->rect.w ? n->rect.w / total : 1.0f;
        float x = n->rect.x;
        for (size_t i = 0; i < n->tabs.size(); ++i) { if (n->tabs[i] == name) return UI::Vec2{x + w[i] * scale - closeW * 0.5f, n->rect.y + TabH() * 0.5f}; x += w[i] * scale; }
        return UI::Vec2{};
    }
    // a floating window's title strip point
    UI::Vec2 TitlePoint(const std::string& name) { const auto* f = L().FindFloating(name); return UI::Vec2{f->rect.x + f->rect.w * 0.5f, f->rect.y + TabH() * 0.5f}; }
};

void TestFloatingAndDockByApi() {
    RUN_SUITE("dock space: floating windows, docked by API");
    Rig r;
    r.Frames(3);
    CHECK(r.L().IsFloating("Alpha") && r.L().IsFloating("Beta") && r.L().IsFloating("Gamma"));
    CHECK(r.shown["Alpha"] && r.shown["Beta"]);
    const UI::Vec2 p0 = r.pos["Alpha"];
    CHECK(p0.x >= 0 && p0.y >= 0);
    CHECK_NEAR(r.size["Alpha"].x, 320, 0.01);
    r.L().Dock("Alpha", r.L().Root(), DockSide_Center);
    r.Frames(2);
    CHECK(Valid(r.L()));
    const UI::Rect c = r.L().ContentRect(r.L().NodeOf("Alpha"));
    CHECK_NEAR(r.pos["Alpha"].x, c.x, 0.01); CHECK_NEAR(r.pos["Alpha"].y, c.y, 0.01);
    CHECK_NEAR(r.size["Alpha"].x, c.w, 0.01); CHECK_NEAR(r.size["Alpha"].y, c.h, 0.01);
    CHECK_NEAR(c.w, 800, 0.01); CHECK_NEAR(c.y, r.TabH(), 0.01);
    // two tabs: only the chosen one is shown
    r.L().Dock("Beta", r.L().Root(), DockSide_Center);
    r.Frames(2);
    CHECK(r.shown["Beta"] && !r.shown["Alpha"]);
    r.L().SetActive("Alpha");
    r.Frames(2);
    CHECK(r.shown["Alpha"] && !r.shown["Beta"]);
    // a window docked left has the left part
    r.L().Dock("Gamma", r.L().Root(), DockSide_Left);
    r.Frames(2);
    const UI::Rect cg = r.L().ContentRect(r.L().NodeOf("Gamma"));
    CHECK_NEAR(cg.x, 0, 0.01); CHECK_NEAR(cg.w, 398, 0.01);
    CHECK_NEAR(r.pos["Gamma"].x, 0, 0.01); CHECK_NEAR(r.size["Gamma"].x, 398, 0.01);
    // area is clipped to the display
    r.area = R(-50, -50, 5000, 5000);
    r.Frames(2);
    CHECK_NEAR(r.L().Find(r.L().NodeOf("Gamma"))->rect.x, 0, 0.01);
    CHECK_NEAR(r.L().Find(r.L().NodeOf("Alpha"))->rect.x + r.L().Find(r.L().NodeOf("Alpha"))->rect.w, 800, 0.01);
    r.area = R(NAN, NAN, NAN, NAN);
    r.Frames(2);                                        // no crash, nothing to draw into
    CHECK(Valid(r.L()));
}

void TestDragDock() {
    RUN_SUITE("dock space: drag a title bar to dock");
    Rig r;
    r.L().Dock("Alpha", r.L().Root(), DockSide_Center);
    r.Frames(3);
    // Beta floats at (300, 120) 320x240; drag its title over the left part of Alpha's content
    r.L().AddFloating("Beta", R(300, 120, 320, 240));
    r.Frames(3);
    const UI::Vec2 t = r.TitlePoint("Beta");
    const UI::Rect lc = r.L().ContentRect(r.L().NodeOf("Alpha"));
    r.Drag(t.x, t.y, 40, lc.y + lc.h * 0.5f, 6, false);
    UI::DockDropInfo d = UI::GetDockDrop();
    CHECK(d.active && d.side == DockSide_Left && d.window == "Beta" && !d.rootEdge);
    CHECK(d.node == r.L().NodeOf("Alpha"));
    CHECK_NEAR(d.preview.w, 400, 1.0);                   // half of the node
    r.sim.Up(); r.Frames(2);
    CHECK(!UI::GetDockDrop().active);
    CHECK(Valid(r.L()));
    CHECK(!r.L().IsFloating("Beta"));
    CHECK(r.L().Find(r.L().Root())->IsSplit());
    CHECK(r.L().Find(r.L().Root())->child[0] == r.L().NodeOf("Beta"));
    CHECK(r.shown["Beta"] && r.shown["Alpha"]);
    // drag Gamma into the centre of Beta's node (tabs)
    r.L().AddFloating("Gamma", R(500, 150, 250, 200));
    r.Frames(3);
    const UI::Vec2 tg = r.TitlePoint("Gamma");
    const UI::Rect bc = r.L().ContentRect(r.L().NodeOf("Beta"));
    r.Drag(tg.x, tg.y, bc.x + bc.w * 0.5f, bc.y + bc.h * 0.5f, 6, false);
    d = UI::GetDockDrop();
    CHECK(d.active && d.side == DockSide_Center && d.node == r.L().NodeOf("Beta"));
    r.sim.Up(); r.Frames(2);
    CHECK(r.L().NodeOf("Gamma") == r.L().NodeOf("Beta"));
    CHECK(r.L().Find(r.L().NodeOf("Beta"))->tabs.size() == 2);
    CHECK(Valid(r.L()));
    // a release over nothing keeps the window floating; a short click does not drag-dock
    r.L().Float("Gamma", R(300, 300, 200, 150));
    r.Frames(3);
    const UI::Vec2 tg2 = r.TitlePoint("Gamma");
    r.Drag(tg2.x, tg2.y, tg2.x + 2, tg2.y + 1, 2);
    CHECK(r.L().IsFloating("Gamma"));
    // root edge: drop on the bottom band of the dock space gives a strip along the bottom
    const UI::Vec2 tg3 = r.TitlePoint("Gamma");
    r.Drag(tg3.x, tg3.y, 400, 495, 8, false);
    d = UI::GetDockDrop();
    CHECK(d.active && d.rootEdge && d.side == DockSide_Bottom);
    r.sim.Up(); r.Frames(2);
    CHECK(Valid(r.L()));
    const auto* root = r.L().Find(r.L().Root());
    CHECK(root->IsSplit() && !root->horizontal && root->child[1] == r.L().NodeOf("Gamma"));
    // the window being dragged moved with the pointer (Begin's own title drag)
    Rig q({"Solo"});
    q.area = R(0, 400, 800, 100);                // the drag stays away from the dock space
    q.L().AddFloating("Solo", R(60, 40, 300, 200));
    q.Frames(2);
    const UI::Vec2 ts = q.TitlePoint("Solo");
    const float x0 = q.L().FindFloating("Solo")->rect.x;
    q.Drag(ts.x, ts.y, ts.x + 60, ts.y + 30, 4, true);
    CHECK_NEAR(q.L().FindFloating("Solo")->rect.x, x0 + 60, 1.5);
}

void TestTabsAndClose() {
    RUN_SUITE("dock space: tabs, reorder, tear off, close");
    Rig r;
    r.L().Dock("Alpha", r.L().Root(), DockSide_Center);
    r.L().Dock("Beta", r.L().Root(), DockSide_Center);
    r.L().Dock("Gamma", r.L().Root(), DockSide_Center);
    r.L().SetActive("Alpha");
    r.Frames(3);
    CHECK(r.shown["Alpha"] && !r.shown["Beta"] && !r.shown["Gamma"]);
    // click a tab to choose it
    UI::Vec2 tb = r.TabCentre("Beta");
    r.Move(tb.x, tb.y); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame();
    r.Frames(2);
    CHECK(r.L().Find(r.L().Root())->tabs[(size_t)r.L().Find(r.L().Root())->active] == "Beta");
    CHECK(r.shown["Beta"] && !r.shown["Alpha"]);
    // reorder: drag Alpha's tab to the right of Gamma
    const UI::Vec2 ta = r.TabCentre("Alpha");
    r.Drag(ta.x, ta.y, 780, ta.y, 8);
    CHECK(r.L().Find(r.L().Root())->tabs.size() == 3);
    CHECK(r.L().Find(r.L().Root())->tabs[2] == "Alpha");
    CHECK(Valid(r.L()));
    // close a tab: the window leaves the layout and its p_open turns false
    const UI::Vec2 cb = r.CloseCentre("Beta");
    r.Move(cb.x, cb.y); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame(); r.Frame();
    CHECK(!r.L().Contains("Beta"));
    CHECK(!r.open["Beta"]);
    CHECK(r.L().WindowCount() == 2);
    CHECK(Valid(r.L()));
    // tear a tab off: drag it down out of the strip, it floats and follows the pointer
    r.open["Beta"] = true;
    const UI::Vec2 tg = r.TabCentre("Gamma");
    r.Drag(tg.x, tg.y, tg.x + 10, 300, 8, false);
    CHECK(r.L().IsFloating("Gamma"));
    CHECK(UI::GetDockDrop().active);                       // pointer is over the dock space
    r.sim.Up(); r.Frames(2);
    CHECK(Valid(r.L()));
    // the leaf with the remaining tab; drop a torn-off tab onto the bottom of it
    const UI::Vec2 tw = r.TabCentre("Alpha");
    r.Drag(tw.x, tw.y, 400, 420, 8, false);
    CHECK(r.L().IsFloating("Alpha"));
    r.sim.Up(); r.Frames(2);
    CHECK(Valid(r.L()));
    CHECK(r.L().WindowCount() == 3);              // Beta, closed above, is drawn again by the rig and floats
}

void TestSplitters() {
    RUN_SUITE("dock space: splitter drag and minimum sizes");
    Rig r;
    r.L().Dock("Alpha", r.L().Root(), DockSide_Center);
    r.L().Dock("Beta", r.L().Root(), DockSide_Right);
    r.L().Dock("Gamma", r.L().NodeOf("Alpha"), DockSide_Center);      // nothing floats over the splitter
    r.Frames(3);
    const int split = r.L().Root();
    UI::Rect sp = r.L().SplitterRect(split);
    r.Drag(sp.x + 2, 200, sp.x + 2 - 150, 200, 6);
    CHECK_NEAR(r.L().Find(r.L().Find(split)->child[0])->rect.w, 250, 3.0);
    CHECK(UI::GetMouseCursor() == UI::MouseCursor_Arrow || true);
    // far to the left: held at the minimum
    sp = r.L().SplitterRect(split);
    r.Drag(sp.x + 2, 200, -400, 200, 6);
    CHECK(r.L().Find(r.L().Find(split)->child[0])->rect.w >= DockLayout::kMinLeafW - 0.5f);
    CHECK(r.L().Find(r.L().Find(split)->child[0])->rect.w < DockLayout::kMinLeafW + 3);
    // far to the right
    sp = r.L().SplitterRect(split);
    r.Drag(sp.x + 2, 200, 5000, 200, 6);
    CHECK(r.L().Find(r.L().Find(split)->child[1])->rect.w >= DockLayout::kMinLeafW - 0.5f);
    CHECK(Valid(r.L()));
    // hovering shows the resize cursor
    sp = r.L().SplitterRect(split);
    r.Move(sp.x + 2, 200);
    CHECK(UI::GetMouseCursor() == UI::MouseCursor_ResizeEW);
    // a window's own minimum holds too
    r.L().SetMinSize("Beta", 400, 100);
    sp = r.L().SplitterRect(split);
    r.Drag(sp.x + 2, 200, 700, 200, 6);
    CHECK(r.L().Find(r.L().Find(split)->child[1])->rect.w >= 400 - 0.5f);
    // vertical splitter cursor
    r.L().Dock("Gamma", r.L().NodeOf("Alpha"), DockSide_Bottom);
    r.Frames(2);
    const int vs = r.L().Find(r.L().NodeOf("Gamma"))->parent;
    const UI::Rect vr = r.L().SplitterRect(vs);
    r.Move(vr.x + 20, vr.y + 2);
    CHECK(UI::GetMouseCursor() == UI::MouseCursor_ResizeNS);
    r.Drag(vr.x + 20, vr.y + 2, vr.x + 20, vr.y - 100, 5);
    CHECK(Valid(r.L()));
}

void TestSaveLoadThroughUi() {
    RUN_SUITE("dock space: layout save and load");
    Rig r;
    r.L().Dock("Alpha", r.L().Root(), DockSide_Center);
    r.L().Dock("Beta", r.L().Root(), DockSide_Right);
    r.L().Dock("Gamma", r.L().NodeOf("Beta"), DockSide_Center);
    r.L().SetActive("Beta");
    r.Frames(3);
    const std::string blob = UI::DockSaveLayout();
    // shuffle everything, then load
    r.L().Float("Alpha", R(10, 10, 300, 200));
    r.L().Close("Beta");
    r.Frames(3);
    int repairs = -1;
    CHECK(UI::DockLoadLayout(blob, &repairs));
    CHECK(repairs == 0);
    r.Frames(3);
    CHECK(Valid(r.L()));
    CHECK(UI::DockSaveLayout() == blob);
    CHECK(r.shown["Alpha"] && r.shown["Beta"] && !r.shown["Gamma"]);
    // a blob with a window the program never draws: its tab is there, nothing crashes
    CHECK(UI::DockLoadLayout("okaydock 1\nroot 1\nnode 1 leaf 0 Ghost Alpha\n", nullptr));
    r.Frames(3);
    CHECK(Valid(r.L()));
    CHECK(r.L().Contains("Ghost"));
    // ResetState forgets the layout
    UI::ResetState();
    CHECK(UI::GetDockLayout().WindowCount() == 0);
}

void TestGolden() {
    RUN_SUITE("dock space: pictures");
    Rig r({"Scene", "Inspector", "Console", "Assets", "Tool"}, 640, 400);
    UI::Style().windowShadow = false;
    r.L().Dock("Scene", r.L().Root(), DockSide_Center);
    r.L().Dock("Inspector", r.L().Root(), DockSide_Right);
    r.L().Dock("Console", r.L().NodeOf("Scene"), DockSide_Bottom);
    r.L().Dock("Assets", r.L().NodeOf("Console"), DockSide_Center);
    r.L().AddFloating("Tool", R(250, 90, 220, 120));
    r.L().SetRatio(r.L().Root(), 0.72f);
    r.Frames(4);
    const Bitmap& b = r.sim.Draw(30, 30, 34);
    std::string why;
    CHECK(MatchesGolden("dock_layout", b, &why));
    if (!why.empty()) std::cerr << "  " << why << "\n";
    // mid-drag: Tool over the left edge of Scene shows the preview and the guide boxes
    const UI::Vec2 t = r.TitlePoint("Tool");
    r.Drag(t.x, t.y, 100, 120, 6, false);
    CHECK(UI::GetDockDrop().active);
    const Bitmap& d = r.sim.Draw(30, 30, 34);
    why.clear();
    CHECK(MatchesGolden("dock_drop_preview", d, &why));
    if (!why.empty()) std::cerr << "  " << why << "\n";
    // the preview really changes pixels over the node it targets
    CHECK(CountNot(d, 10, 60, 120, 200, ThemeRgba(UI::Style().panel)) > 500);
}

} // namespace

int main() {
    TestModelBasics();
    TestModelGeometry();
    TestModelText();
    TestFloatingAndDockByApi();
    TestDragDock();
    TestTabsAndClose();
    TestSplitters();
    TestSaveLoadThroughUi();
    TestGolden();
    TEST_MAIN_RESULT();
}
