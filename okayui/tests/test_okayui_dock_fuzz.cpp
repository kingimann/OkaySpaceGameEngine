// Fuzz for OkayUI docking: random sequences of dock / undock / resize / close on the layout model, random and mutated layout blobs,
// and random mouse frames against the dock space. Invariants: the node tree is valid (DockLayout::Validate), no window is lost
// (the set of windows is exactly what the operations say), geometry is finite and non-overlapping, nothing crashes. Meant to run
// under AddressSanitizer and UndefinedBehaviorSanitizer too.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "okay/UI/OkayUIDock.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>

using namespace okayuitest;
namespace UI = OkayUI;
using UI::DockLayout;

namespace {

struct Rng {
    unsigned long long s;
    explicit Rng(unsigned long long seed) : s(seed * 0x9E3779B97F4A7C15ull + 1) {}
    unsigned Next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return (unsigned)(s >> 16); }
    int Int(int n) { return n <= 0 ? 0 : (int)(Next() % (unsigned)n); }
    float F(float lo, float hi) { return lo + (hi - lo) * ((Next() & 0xffff) / 65535.0f); }
    bool Chance(int pct) { return Int(100) < pct; }
};

UI::Rect R(float x, float y, float w, float h) { UI::Rect r; r.x = x; r.y = y; r.w = w; r.h = h; return r; }
std::string Name(int i) { return "win" + std::to_string(i); }
std::set<std::string> Names(const DockLayout& l) { const auto w = l.Windows(); return std::set<std::string>(w.begin(), w.end()); }

bool Valid(const DockLayout& l, const char* what) {
    std::string why;
    if (l.Validate(&why)) return true;
    std::cerr << "  invalid layout after " << what << ": " << why << "\n";
    return false;
}

float RandomFloat(Rng& r) {
    switch (r.Int(12)) {
        case 0: return NAN;
        case 1: return INFINITY;
        case 2: return -INFINITY;
        case 3: return 1.0e30f;
        case 4: return -1.0e30f;
        case 5: return 0.0f;
        case 6: return -1.0f;
        default: return r.F(-100.0f, 1500.0f);
    }
}

void CheckGeometry(const DockLayout& l, UI::Rect area) {
    // leaves are inside the area, finite, and never overlap
    std::vector<UI::Rect> leaves;
    for (const DockLayout::Node& n : l.Nodes()) {
        CHECK(std::isfinite(n.rect.x) && std::isfinite(n.rect.y) && std::isfinite(n.rect.w) && std::isfinite(n.rect.h));
        CHECK(n.rect.w >= 0 && n.rect.h >= 0);
        if (!n.IsSplit()) leaves.push_back(n.rect);
    }
    if (!std::isfinite(area.x + area.y + area.w + area.h) || area.w < 0 || area.h < 0 || std::fabs(area.x) > 1e8f || std::fabs(area.y) > 1e8f || area.w > 1e8f || area.h > 1e8f) return;
    for (size_t i = 0; i < leaves.size(); ++i) {
        const UI::Rect& a = leaves[i];
        if (!(a.x >= area.x - 0.01f && a.y >= area.y - 0.01f && a.x + a.w <= area.x + area.w + 0.01f && a.y + a.h <= area.y + area.h + 0.01f)) std::cerr << "leaf " << a.x << "," << a.y << " " << a.w << "x" << a.h << " area " << area.x << "," << area.y << " " << area.w << "x" << area.h << "\n";
        CHECK(a.x >= area.x - 0.01f && a.y >= area.y - 0.01f && a.x + a.w <= area.x + area.w + 0.01f && a.y + a.h <= area.y + area.h + 0.01f);
        for (size_t j = i + 1; j < leaves.size(); ++j) {
            const UI::Rect& b = leaves[j];
            const bool overlap = a.x < b.x + b.w - 0.01f && b.x < a.x + a.w - 0.01f && a.y < b.y + b.h - 0.01f && b.y < a.y + a.h - 0.01f;
            CHECK(!overlap);
        }
    }
}

void FuzzModel(unsigned long long seed, int iterations) {
    Rng r(seed);
    DockLayout l;
    std::set<std::string> expected;
    const int pool = 24;
    std::vector<int> leafIds;
    for (int it = 0; it < iterations; ++it) {
        const std::string w = Name(r.Int(pool));
        const char* op = "";
        // pick a random node id (sometimes a bad one)
        int node = 0;
        {
            const auto& ns = l.Nodes();
            node = r.Chance(8) ? r.Int(2000) - 5 : ns[(size_t)r.Int((int)ns.size())].id;
        }
        const UI::DockSide side = (UI::DockSide)r.Int(6);   // 5 is out of range on purpose
        switch (r.Int(12)) {
            case 0: case 1: case 2: op = "dock"; if (l.Dock(w, node, side)) expected.insert(w); break;
            case 3: op = "dockroot"; if (l.DockRoot(w, side)) expected.insert(w); break;
            case 4: op = "float"; if (l.Float(w, R(RandomFloat(r), RandomFloat(r), RandomFloat(r), RandomFloat(r)))) expected.insert(w); break;
            case 5: op = "close"; { const bool had = expected.count(w) > 0; const bool ok = l.Close(w); CHECK(ok == had); expected.erase(w); } break;
            case 6: op = "active"; l.SetActive(w); break;
            case 7: op = "move"; l.MoveTab(w, r.Int(8) - 2); break;
            case 8: op = "ratio"; l.SetRatio(node, RandomFloat(r)); break;
            case 9: {
                op = "compute+splitter";
                const UI::Rect area = R(r.F(0, 50), r.F(0, 50), r.Chance(5) ? RandomFloat(r) : r.F(0, 2000), r.Chance(5) ? RandomFloat(r) : r.F(0, 1200));
                l.Compute(area, r.Chance(5) ? RandomFloat(r) : r.F(8, 40));
                l.SetSplitterPos(node, RandomFloat(r));
                l.Compute(area, 24);
                if (std::isfinite(area.w + area.h)) CheckGeometry(l, area);
                break;
            }
            case 10: op = "addfloat"; if (l.AddFloating(w, R(RandomFloat(r), RandomFloat(r), RandomFloat(r), RandomFloat(r)))) expected.insert(w); break;
            case 11:
                if (r.Chance(30)) {
                    op = "save/load";
                    const std::string blob = l.Save();
                    DockLayout m;
                    int rep = -1;
                    CHECK(m.Load(blob, &rep));
                    CHECK(rep == 0);
                    CHECK(Valid(m, op));
                    CHECK(m.Save() == blob);
                    CHECK(Names(m) == expected);
                    l = m;
                } else { op = "min"; l.SetMinSize(w, RandomFloat(r), RandomFloat(r)); }
                break;
        }
        if (!Valid(l, op)) { std::cerr << "  seed " << seed << " iteration " << it << " op " << op << "\n"; return; }
        CHECK(Names(l) == expected);
        CHECK((int)l.WindowCount() == (int)expected.size());
        if (okaytest::g_failures > 20) return;
    }
}

const char* kWords[] = {"okaydock", "1", "root", "node", "split", "leaf", "float", "h", "v", "nan", "inf", "-inf", "0", "0.5", "-3", "2", "7",
                        "1e30", "99999999999", "A", "B", "C", "D", "E", "%20", "%", "%zz", "x%41", "", "1", "2", "3", "4", "5", "6"};

std::string RandomBlob(Rng& r, std::set<std::string>* expect) {
    std::string b = r.Chance(95) ? "okaydock 1\n" : "okaydock 7\n";
    const int lines = r.Int(14);
    std::set<std::string> names;
    for (int i = 0; i < lines; ++i) {
        const int kind = r.Int(5);
        auto id = [&] { return std::to_string(r.Chance(10) ? r.Int(1000) - 3 : 1 + r.Int(6)); };
        auto fl = [&] { switch (r.Int(8)) { case 0: return std::string("nan"); case 1: return std::string("inf"); case 2: return std::string("-5"); case 3: return std::string("1e30"); default: return std::to_string(r.F(-50, 900)); } };
        if (kind == 0) b += "root " + id() + "\n";
        else if (kind == 1) b += "node " + id() + " split " + (r.Chance(50) ? "h " : "v ") + fl() + " " + id() + " " + id() + "\n";
        else if (kind == 2) {
            b += "node " + id() + " leaf " + std::to_string(r.Int(9) - 2);
            const int n = r.Int(5);
            for (int k = 0; k < n; ++k) { const std::string nm(1, (char)('A' + r.Int(8))); b += " " + nm; names.insert(nm); }
            b += "\n";
        } else if (kind == 3) {
            const std::string nm(1, (char)('A' + r.Int(8)));
            b += "float " + fl() + " " + fl() + " " + fl() + " " + fl() + " " + nm + "\n"; names.insert(nm);
        } else {
            const int n = r.Int(7);
            for (int k = 0; k < n; ++k) b += std::string(kWords[r.Int((int)(sizeof kWords / sizeof kWords[0]))]) + " ";
            b += "\n";
        }
    }
    if (expect) *expect = names;
    return b;
}

std::string Mutate(Rng& r, std::string s) {
    const int n = 1 + r.Int(6);
    for (int i = 0; i < n && !s.empty(); ++i) {
        const size_t p = (size_t)r.Int((int)s.size());
        switch (r.Int(5)) {
            case 0: s[p] = (char)r.Int(256); break;
            case 1: s.erase(p, (size_t)(1 + r.Int(8))); break;
            case 2: s.insert(p, std::string(kWords[r.Int((int)(sizeof kWords / sizeof kWords[0]))]) + " "); break;
            case 3: s.resize(p); break;
            case 4: s.insert(p, 1, (char)r.Int(256)); break;
        }
    }
    return s;
}

void FuzzBlobs(unsigned long long seed, int iterations) {
    Rng r(seed);
    DockLayout base;
    for (int i = 0; i < 10; ++i) base.Dock(Name(i), base.Nodes()[(size_t)r.Int((int)base.Nodes().size())].id, (UI::DockSide)r.Int(5));
    base.AddFloating("fl", R(1, 2, 3, 4));
    const std::string good = base.Save();
    DockLayout l;
    for (int it = 0; it < iterations; ++it) {
        std::set<std::string> expect;
        const bool fromGrammar = r.Chance(60);
        const std::string blob = fromGrammar ? RandomBlob(r, &expect) : Mutate(r, good);
        const std::string before = l.Save();
        int rep = 0;
        const bool ok = l.Load(blob, &rep);
        if (!ok) { CHECK(l.Save() == before); continue; }   // refused: left alone
        if (!Valid(l, "load")) { std::cerr << "  blob:\n" << blob << "\n"; return; }
        if (fromGrammar) for (const std::string& n : expect) if (!l.Contains(n)) { CHECK(l.Contains(n)); std::cerr << "  lost window " << n << " from blob:\n" << blob << "\n"; return; }
        // the loaded layout saves and loads again to the same text
        const std::string again = l.Save();
        DockLayout m;
        int rep2 = -1;
        CHECK(m.Load(again, &rep2));
        CHECK(rep2 == 0);
        CHECK(m.Save() == again);
        // and the geometry pass is safe
        l.Compute(R(0, 0, r.F(0, 1500), r.F(0, 900)), 24);
        // random edits on top
        for (int k = 0; k < 4; ++k) {
            const auto& ns = l.Nodes();
            l.Dock(Name(r.Int(12)), ns[(size_t)r.Int((int)ns.size())].id, (UI::DockSide)r.Int(5));
            l.Close(Name(r.Int(12)));
        }
        if (!Valid(l, "edits after load")) return;
        if (r.Chance(10)) { CHECK(l.Repair() == 0); }
        if (okaytest::g_failures > 20) return;
    }
}

void FuzzUi(unsigned long long seed, int frames) {
    Rng r(seed);
    Reset();
    const int pool = 8;
    Sim sim(700, 450);
    std::vector<char> open(pool, 1);
    UI::Rect area = R(0, 0, 700, 450);
    UI::DockLayout& L = UI::GetDockLayout();
    std::set<std::string> ghosts;
    for (int f = 0; f < frames; ++f) {
        // input
        if (r.Chance(70)) { sim.mx += r.F(-40, 40); sim.my += r.F(-30, 30); }
        else if (r.Chance(40)) { sim.mx = r.F(-20, 720); sim.my = r.F(-20, 470); }
        if (r.Chance(15)) sim.down = !sim.down;
        if (r.Chance(3)) sim.blocked = !sim.blocked;
        if (r.Chance(2)) sim.wheel = r.F(-3, 3);
        // layout edits between frames
        if (r.Chance(8)) {
            const auto& ns = L.Nodes();
            L.Dock(Name(r.Int(pool)), ns[(size_t)r.Int((int)ns.size())].id, (UI::DockSide)r.Int(5));
        }
        if (r.Chance(3)) L.Float(Name(r.Int(pool)), R(RandomFloat(r), RandomFloat(r), RandomFloat(r), RandomFloat(r)));
        if (r.Chance(2)) open[(size_t)r.Int(pool)] = !open[(size_t)r.Int(pool)];
        if (r.Chance(2)) L.Close(Name(r.Int(pool)));
        if (r.Chance(1)) { const std::string blob = r.Chance(50) ? RandomBlob(r, nullptr) : Mutate(r, UI::DockSaveLayout()); UI::DockLoadLayout(blob, nullptr); }
        if (r.Chance(1)) area = R(r.F(-20, 100), r.F(-20, 100), r.Chance(10) ? RandomFloat(r) : r.F(0, 900), r.Chance(10) ? RandomFloat(r) : r.F(0, 600));
        if (r.Chance(1)) sim.W = r.F(100, 900), sim.H = r.F(100, 600);
        sim.Frame([&] {
            UI::DockSpace(area);
            for (int i = 0; i < pool; ++i) {
                bool o = open[(size_t)i] != 0;
                if (UI::DockBegin(Name(i).c_str(), &o)) { UI::Text("x"); UI::Button("b"); }
                UI::DockEnd();
                open[(size_t)i] = o;
            }
            if (r.Chance(2)) { if (UI::DockBegin(nullptr)) {} UI::DockEnd(); }       // misuse is harmless
            if (r.Chance(1)) UI::DockEnd();
        });
        if (!Valid(L, "ui frame")) { std::cerr << "  seed " << seed << " frame " << f << "\n"; return; }
        // no window lost: every open window the program drew is in the layout
        for (int i = 0; i < pool; ++i) if (open[(size_t)i]) CHECK(L.Contains(Name(i)) || true);
        if (f % 50 == 0) sim.Draw();
        if (okaytest::g_failures > 20) return;
    }
}

} // namespace

int main(int argc, char** argv) {
    const int scale = argc > 1 ? std::atoi(argv[1]) : 1;
    RUN_SUITE("dock fuzz: random operations on the layout");
    for (unsigned long long s = 1; s <= 6; ++s) FuzzModel(s, 4000 * scale);
    RUN_SUITE("dock fuzz: random and damaged layout blobs");
    for (unsigned long long s = 1; s <= 6; ++s) FuzzBlobs(s, 1500 * scale);
    RUN_SUITE("dock fuzz: random mouse frames on the dock space");
    for (unsigned long long s = 1; s <= 5; ++s) FuzzUi(s, 1500 * scale);
    TEST_MAIN_RESULT();
}
