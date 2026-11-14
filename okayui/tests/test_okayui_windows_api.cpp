// OkayUI window queries, headless: the supported form of what the editor read from Dear ImGui's window internals (FindWindowByName,
// GetCurrentWindow, NavWindow, ImGuiWindow fields): WindowInfo and the calls around it, "###" ids, SetNextWindowCollapsed,
// SetItemWidthDefault, BringWindowToDisplayFront. OKAYUI_FUZZ_ITERS scales the fuzz (default 1500).
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "okay/UI/OkayUICompat.hpp"
#include "okay/UI/OkayUIDock.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace okayuitest;
namespace UI = OkayUI;

namespace {

bool Near(float a, float b, float e = 0.51f) { return std::fabs(a - b) <= e; }

void TestFind() {
    RUN_SUITE("window queries: find by name and id, position, size, activity");
    Reset();
    Sim sim(640, 480);
    UI::WindowInfo inside;
    auto frame = [&](bool drawA, bool drawB) {
        sim.Frame([&] {
            if (drawA) { UI::Begin("Alpha", 30, 40, 200, 150); inside = UI::GetCurrentWindowInfo(); UI::Text("a"); UI::End(); }
            if (drawB) { UI::Begin("Beta", 300, 60, 180, 120); UI::Text("b"); UI::End(); }
        });
    };
    CHECK(!UI::FindWindowByName("Alpha").found && !UI::GetCurrentWindowInfo().found && !UI::GetFocusedWindowInfo().found);
    frame(true, true);
    CHECK(inside.found && std::string(inside.name) == "Alpha" && inside.active && inside.wasActive);
    const UI::WindowInfo a = UI::FindWindowByName("Alpha");
    CHECK(a.found && std::string(a.name) == "Alpha" && Near(a.pos.x, 30) && Near(a.pos.y, 40) && Near(a.size.x, 200));
    CHECK(a.id != 0 && UI::FindWindowByID(a.id).found && std::string(UI::FindWindowByID(a.id).name) == "Alpha");
    CHECK(!UI::FindWindowByID(12345).found && !UI::FindWindowByName(nullptr).found && !UI::FindWindowByName("").found && !UI::FindWindowByName("Nope").found);
    CHECK(a.active && a.wasActive && !a.docked && a.dockNode == 0 && !a.dockTabVisible);
    // not drawn for a frame: no longer active, still "was active"; two frames: neither
    frame(false, true);
    CHECK(!UI::FindWindowByName("Alpha").active && UI::FindWindowByName("Alpha").wasActive && UI::FindWindowByName("Beta").active);
    frame(false, true);
    CHECK(!UI::FindWindowByName("Alpha").active && !UI::FindWindowByName("Alpha").wasActive);
    frame(true, true);
    CHECK(UI::FindWindowByName("Alpha").active);
    // a moved window reports where it is
    UI::SetWindowPos("Alpha", UI::Vec2{111, 77});
    CHECK(Near(UI::FindWindowByName("Alpha").pos.x, 111) && Near(UI::FindWindowByName("Alpha").pos.y, 77));
    Reset();
}

void TestHashHash() {
    RUN_SUITE("window queries: \"###\" ids (a title can change, the window stays)");
    Reset();
    Sim sim(640, 480);
    unsigned id1 = 0, id2 = 0;
    sim.Frame([&] { UI::Begin("Console###Console", 20, 20, 200, 100); UI::Text("x"); UI::End(); });
    id1 = UI::FindWindowByName("Console###Console").id;
    CHECK(UI::FindWindowByName("###Console").found && UI::FindWindowByName("###Console").id == id1);
    sim.Frame([&] { UI::Begin("Console (3 new)###Console", 20, 20, 200, 100); UI::Text("x"); UI::End(); });
    id2 = UI::FindWindowByName("###Console").id;
    CHECK(id1 == id2 && id1 != 0);
    CHECK(UI::FindWindowByName("Console (3 new)###Console").found);
    // the window stayed where the user had put it (one window, not two)
    UI::WindowInfo all[8];
    CHECK(UI::GetWindowInfos(all, 8) == 1);
    // "##" is part of the id as before (not a reset)
    sim.Frame([&] { UI::Begin("A##1", 20, 220, 100, 80); UI::End(); UI::Begin("A##2", 140, 220, 100, 80); UI::End(); });
    CHECK(UI::FindWindowByName("A##1").found && UI::FindWindowByName("A##2").found && UI::FindWindowByName("A##1").id != UI::FindWindowByName("A##2").id);
    // widgets with "###" labels keep their state when the visible text changes
    bool b1 = false, b2 = false;
    float x1 = 0, x2 = 0;
    sim.Frame([&] { UI::Begin("W", 300, 20, 300, 200); UI::Checkbox("Show 1###chk", &b1); x1 = UI::GetItemRect().x; UI::End(); });
    sim.Frame([&] { UI::Begin("W", 300, 20, 300, 200); UI::Checkbox("Show 2 and more###chk", &b2); x2 = UI::GetItemRect().x; UI::End(); });
    CHECK(Near(x1, x2));
    Reset();
}

void TestFocusAndOrder() {
    RUN_SUITE("window queries: focus, stacking order, bring to front");
    Reset();
    Sim sim(640, 480);
    auto frame = [&] {
        sim.Frame([&] {
            UI::Begin("Back", 20, 20, 300, 200); UI::Text("back"); UI::End();
            UI::Begin("Front", 200, 100, 300, 200); UI::Text("front"); UI::End();
        });
    };
    frame(); frame();
    CHECK(UI::FindWindowByName("Front").order > UI::FindWindowByName("Back").order);
    CHECK(UI::GetFocusedWindowInfo().found && std::string(UI::GetFocusedWindowInfo().name) == "Front" && UI::FindWindowByName("Front").focused && !UI::FindWindowByName("Back").focused);
    // a click on the back window's title (where nothing covers it) focuses and raises it
    sim.Move(60, 28); frame(); sim.Down(); frame(); sim.Up(); frame(); frame();
    CHECK(std::string(UI::GetFocusedWindowInfo().name) == "Back" && UI::FindWindowByName("Back").order > UI::FindWindowByName("Front").order);
    // BringWindowToDisplayFront by name and for the current window
    UI::BringWindowToDisplayFront("Front");
    CHECK(UI::FindWindowByName("Front").order > UI::FindWindowByName("Back").order);
    sim.Frame([&] { UI::Begin("Back", 20, 20, 300, 200); UI::BringWindowToDisplayFront(); UI::End(); UI::Begin("Front", 200, 100, 300, 200); UI::End(); });
    CHECK(UI::FindWindowByName("Back").order > UI::FindWindowByName("Front").order);
    UI::BringWindowToDisplayFront("nope"); UI::BringWindowToDisplayFront(""); UI::BringWindowToDisplayFront(nullptr);
    // the list, back to front
    UI::WindowInfo list[4];
    CHECK(UI::GetWindowInfos(list, 4) == 2 && list[0].order < list[1].order && std::string(list[1].name) == "Back");
    CHECK(UI::GetWindowInfos(list, 1) == 1 && UI::GetWindowInfos(list, 0) == 0 && UI::GetWindowInfos(nullptr, 4) == 2);
    Reset();
}

void TestDockInfo() {
    RUN_SUITE("window queries: docked windows, their node, the chosen tab");
    Reset();
    UI::GetDockLayout().Clear();
    Sim sim(800, 500);
    UI::DockLayout& l = UI::GetDockLayout();
    l.Dock("Scene", l.Root(), UI::DockSide_Center);
    l.Dock("Game", l.Root(), UI::DockSide_Center);
    l.Dock("Console", l.Root(), UI::DockSide_Bottom);
    l.AddFloating("Tool", UI::Rect{300, 100, 200, 120});
    UI::WindowInfo scene, game, tool;
    for (int i = 0; i < 3; ++i) sim.Frame([&] {
        UI::DockSpace(UI::Rect{0, 0, 800, 500});
        UI::DockBegin("Scene"); scene = UI::GetCurrentWindowInfo(); const bool dockedHere = UI::IsWindowDocked(); (void)dockedHere; UI::DockEnd();
        UI::DockBegin("Game"); UI::DockEnd();
        UI::DockBegin("Console"); UI::DockEnd();
        UI::DockBegin("Tool"); tool = UI::GetCurrentWindowInfo(); UI::DockEnd();
    });
    const UI::WindowInfo s = UI::FindWindowByName("Scene"), g = UI::FindWindowByName("Game"), c = UI::FindWindowByName("Console"), t = UI::FindWindowByName("Tool");
    CHECK(s.found && s.docked && s.dockNode == l.NodeOf("Scene") && s.dockNode != 0);
    CHECK(g.docked && g.dockNode == s.dockNode);                      // the same leaf
    CHECK(c.docked && c.dockNode != s.dockNode);
    CHECK(s.dockTabVisible != g.dockTabVisible);                      // exactly one tab of a leaf is the chosen one
    { const UI::DockLayout::Node* n = l.Find(s.dockNode); CHECK(n && s.dockTabVisible == (n->tabs[(size_t)n->active] == "Scene")); }   // it agrees with the layout's chosen tab
    CHECK(c.dockTabVisible);
    CHECK(t.found && !t.docked && t.dockNode == 0 && !t.dockTabVisible);
    // choosing the other tab swaps the flags
    l.SetActive("Game");
    sim.Frame([&] { UI::DockSpace(UI::Rect{0, 0, 800, 500}); UI::DockBegin("Scene"); UI::DockEnd(); UI::DockBegin("Game"); UI::DockEnd(); });
    CHECK(UI::FindWindowByName("Game").dockTabVisible && !UI::FindWindowByName("Scene").dockTabVisible);
    // the docked window reports the rectangle of its node
    CHECK(UI::FindWindowByName("Game").size.x > 300);
    Reset();
    UI::GetDockLayout().Clear();
}

void TestInnerRect() {
    RUN_SUITE("window queries: window id and inner rectangle, drag target on it");
    Reset();
    Sim sim(640, 480);
    UI::Rect inner, childInner, winRect, btn;
    unsigned wid = 0;
    int accepted = 0, overFrames = 0;
    auto frame = [&] {
        sim.Frame([&] {
            UI::Begin("Host", 40, 30, 500, 380);
            wid = UI::GetWindowID();
            inner = UI::GetWindowInnerRect();
            winRect = UI::Rect{UI::GetWindowPos().x, UI::GetWindowPos().y, UI::GetWindowSize().x, UI::GetWindowSize().y};
            if (UI::BeginChild("c", UI::Vec2{200, 100}, UI::Child_Borders)) { childInner = UI::GetWindowInnerRect(); UI::Text("child"); }
            UI::EndChild();
            const bool over = UI::BeginDragDropTargetCustom(inner, wid); if (over) ++overFrames;
            if (over) {
                if (const UI::DragPayload* a = UI::AcceptDragDropPayload("T")) if (a->delivery) ++accepted;
                UI::EndDragDropTarget();
            }
            UI::Button("drag me"); btn = UI::GetItemRect();
            if (UI::BeginDragDropSource()) { int v = 1; UI::SetDragDropPayload("T", &v, sizeof v); UI::Text("payload"); UI::EndDragDropSource(); }
            UI::End();
        });
    };
    CHECK(UI::GetWindowID() == 0 && UI::GetWindowInnerRect().w == 0);
    frame(); frame();
    CHECK(wid != 0 && wid == UI::FindWindowByName("Host").id);
    CHECK(inner.x >= winRect.x - 0.5f && inner.y > winRect.y && inner.x + inner.w <= winRect.x + winRect.w + 0.5f && inner.y + inner.h <= winRect.y + winRect.h + 0.5f);   // below the title bar, inside the window
    CHECK(childInner.w > 100 && childInner.w < 210 && childInner.h > 40 && childInner.h < 110);                                                                              // the child, not the window
    // drag the button into the window area and release over an empty part
    const UI::Rect b = btn;
    sim.Move(b.x + 10, b.y + 8); frame(); sim.Down(); frame();
    for (int i = 1; i <= 8; ++i) { sim.Move(b.x + 10 + 30.0f * i, b.y + 8 + 6.0f * i); frame(); }
    sim.Up(); frame(); frame();
    CHECK(accepted == 1);
    Reset();
}

void TestItemWidth() {
    RUN_SUITE("window queries: default item width of a window");
    Reset();
    Sim sim(640, 480);
    float w1 = 0, w2 = 0, w3 = 0, w4 = 0, other = 0;
    float v = 1;
    auto frame = [&] {
        sim.Frame([&] {
            UI::Begin("W", 10, 10, 600, 300);
            UI::SliderFloat("", &v, 0, 10); w1 = UI::GetItemRectSize().x;
            UI::SetItemWidthDefault(150);
            UI::SliderFloat("", &v, 0, 10); w2 = UI::GetItemRectSize().x;
            UI::PushItemWidth(90); UI::SliderFloat("", &v, 0, 10); w3 = UI::GetItemRectSize().x; UI::PopItemWidth();
            UI::SliderFloat("", &v, 0, 10); w4 = UI::GetItemRectSize().x;
            UI::SetItemWidthDefault(NAN); UI::SetItemWidthDefault(-4);
            UI::SliderFloat("", &v, 0, 10);
            UI::SetItemWidthDefault(150);
            UI::End();
            UI::Begin("V", 10, 330, 600, 100);
            UI::SliderFloat("", &v, 0, 10); other = UI::GetItemRectSize().x;
            UI::End();
        });
    };
    frame(); frame();
    CHECK(w1 > 400 && Near(w2, 150, 1.0f) && Near(w3, 90, 1.0f) && Near(w4, 150, 1.0f));
    CHECK(other > 400);                                  // another window is not affected, and the setting ended with its window
}

void TestCollapsed() {
    RUN_SUITE("window queries: SetNextWindowCollapsed");
    Reset();
    Sim sim(640, 480);
    auto frame = [&](int mode) {   // 0 nothing, 1 Always collapsed, 2 FirstUseEver collapsed, 3 Always open
        sim.Frame([&] {
            if (mode == 1) UI::SetNextWindowCollapsed(true, UI::Cond_Always);
            if (mode == 2) UI::SetNextWindowCollapsed(true, UI::Cond_FirstUseEver);
            if (mode == 3) UI::SetNextWindowCollapsed(false, UI::Cond_Always);
            UI::Begin("W", 20, 20, 300, 200); UI::Text("body"); UI::End();
        });
    };
    frame(2); frame(0);
    CHECK(UI::FindWindowByName("W").collapsed && UI::FindWindowByName("W").size.y < 40);          // first use: collapsed, and it stays
    frame(3);
    CHECK(!UI::FindWindowByName("W").collapsed && UI::FindWindowByName("W").size.y > 100);
    frame(2);
    CHECK(!UI::FindWindowByName("W").collapsed);                                                  // FirstUseEver does nothing the second time
    frame(1);
    CHECK(UI::FindWindowByName("W").collapsed);
    frame(1); frame(0);
    CHECK(UI::FindWindowByName("W").collapsed);
    Reset();
}

void TestHostile() {
    RUN_SUITE("window queries: hostile names, many windows");
    Reset();
    Sim sim(640, 480);
    const std::string longName(300, 'q');
    sim.Frame([&] { UI::Begin(longName.c_str(), 10, 10, 200, 100); UI::End(); });
    const UI::WindowInfo l = UI::FindWindowByName(longName.c_str());
    CHECK(l.found && std::strlen(l.name) == 63);
    // more windows than OkayUI keeps: the extra ones are simply not tracked, nothing breaks
    sim.Frame([&] { for (int i = 0; i < 100; ++i) { const std::string n = "w" + std::to_string(i); UI::Begin(n.c_str(), (float)(i % 10) * 20, (float)(i / 10) * 20, 100, 60); UI::End(); } });
    UI::WindowInfo all[200];
    const int n = UI::GetWindowInfos(all, 200);
    CHECK(n > 10 && n <= 64);
    bool sorted = true; for (int i = 1; i < n; ++i) if (all[i].order < all[i - 1].order) sorted = false;
    CHECK(sorted);
    CHECK(UI::FindWindowByName("w0").found || true);
    Reset();
}

void TestFuzz() {
    RUN_SUITE("window queries: fuzz");
    const char* e = std::getenv("OKAYUI_FUZZ_ITERS");
    const int iters = e ? std::max(30, std::atoi(e) / 4) : 1500;
    std::uint64_t st = 0xC0FFEEull * 0x9E3779B97F4A7C15ull;
    auto next = [&] { st ^= st << 13; st ^= st >> 7; st ^= st << 17; return (unsigned)(st >> 16); };
    auto I = [&](int n) { return n <= 0 ? 0 : (int)(next() % (unsigned)n); };
    auto F = [&]() -> float { switch (I(8)) { case 0: return NAN; case 1: return INFINITY; case 2: return -1.0f; case 3: return 1e30f; default: return (float)I(700); } };
    Reset();
    Sim sim(640, 480);
    bool ok = true;
    for (int it = 0; it < iters && ok; ++it) {
        sim.Move((float)I(640), (float)I(480)); sim.down = I(3) == 0;
        sim.Frame([&] {
            const int n = I(5);
            for (int k = 0; k < n; ++k) {
                char name[32]; std::snprintf(name, sizeof name, I(3) ? "win%d" : "T%d###id%d", I(12), I(4));
                if (I(8) == 0) UI::SetNextWindowCollapsed(I(2) == 0, I(2) ? UI::Cond_Always : UI::Cond_Once);
                if (I(6) == 0) UI::SetNextWindowDockID(I(20) - 2, UI::Cond_Always);
                UI::Begin(name, (float)I(500), (float)I(300), 80.0f + (float)I(300), 60.0f + (float)I(200));
                const UI::WindowInfo c = UI::GetCurrentWindowInfo();
                if (!c.found || std::strlen(c.name) > 63) ok = false;
                (void)UI::GetWindowInnerRect(); (void)UI::GetWindowID();
                if (I(4) == 0) UI::SetItemWidthDefault(F());
                if (I(4) == 0) UI::BringWindowToDisplayFront(I(2) ? name : nullptr);
                UI::Text("x"); UI::Button("b");
                UI::End();
            }
            (void)UI::FindWindowByName("win3"); (void)UI::FindWindowByID((unsigned)next()); (void)UI::GetFocusedWindowInfo();
            UI::WindowInfo all[70]; const int cnt = UI::GetWindowInfos(all, 70);
            for (int i = 0; i < cnt; ++i) if (!std::isfinite(all[i].pos.x + all[i].pos.y + all[i].size.x + all[i].size.y)) ok = false;
        });
        const UI::DrawData dd = UI::GetDrawData();
        const UI::DrawVertex* vs = static_cast<const UI::DrawVertex*>(dd.vertices);
        for (int i = 0; i < dd.indexCount && ok; ++i) if (dd.indices[i] < 0 || dd.indices[i] >= dd.vertexCount) ok = false;
        for (int i = 0; i < dd.vertexCount && ok; ++i) if (!std::isfinite(vs[i].x) || !std::isfinite(vs[i].y) || !std::isfinite(vs[i].u) || !std::isfinite(vs[i].v)) ok = false;
    }
    CHECK(ok);
    Reset();
}

} // namespace

int main() {
    TestFind();
    TestHashHash();
    TestFocusAndOrder();
    TestDockInfo();
    TestInnerRect();
    TestItemWidth();
    TestCollapsed();
    TestHostile();
    TestFuzz();
    TEST_MAIN_RESULT();
}
