// Dock-aware Begin / End (SetDockAwareBegin), headless: code written for Dear ImGui opens its window with Begin and closes it with End; when the title is a
// window of the dock layout it must sit in the dock like one written for DockBegin. Covers the chosen tab and the hidden one, closing a tab, windows
// the layout does not hold, the switch off, unbalanced End, popups inside, and a random run that keeps the layout valid.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "okay/UI/OkayUIInternals.hpp"

#include <cmath>
#include <string>
#include <vector>

using namespace okayuitest;
namespace UI = OkayUI;

namespace {
constexpr UI::DockId kSpace = 0x5151u;
void BuildTwo() {
    UI::DockBuilderRemoveNode(kSpace);
    UI::DockBuilderAddNode(kSpace, UI::DockNode_DockSpace);
    UI::DockBuilderSetNodeSize(kSpace, UI::Vec2{800, 500});
    UI::DockId a = kSpace, b = 0;
    UI::DockBuilderSplitNode(a, UI::DockSide_Right, 0.4f, &b, &a);
    UI::DockBuilderDockWindow("Alpha", a); UI::DockBuilderDockWindow("Beta", a); UI::DockBuilderDockWindow("Gamma###g", b);
    UI::DockBuilderFinish(kSpace);
}
}

int main() {
    RUN_SUITE("okayui dock-aware Begin");
    {
        Reset(); Sim s(800, 500); BuildTwo();
        bool openA = true, openB = true, openG = true; int aShown = 0, bShown = 0, gShown = 0, freeShown = 0;
        UI::SetDockAwareBegin(true);
        CHECK(UI::DockAwareBegin());
        auto frame = [&] {
            s.Frame([&] {
                UI::DockSpace(UI::Rect{0, 0, 800, 500});
                if (UI::Begin("Alpha", &openA, 0)) { ++aShown; UI::Text("alpha content"); } UI::End();
                if (UI::Begin("Beta", &openB, 0)) { ++bShown; UI::Text("beta"); } UI::End();
                if (UI::Begin("Gamma###g", &openG, 0)) { ++gShown; UI::Text("gamma"); } UI::End();
                if (UI::Begin("Free", nullptr, 0)) { ++freeShown; UI::Text("free"); } UI::End();   // not in the layout: an ordinary window
            });
        };
        frame(); frame(); frame();
        const UI::DockLayout& L = UI::GetDockLayout();
        CHECK(L.Contains("Alpha") && !L.IsFloating("Alpha") && !L.IsFloating("Beta") && !L.IsFloating("Gamma###g") && L.NodeOf("Alpha") == L.NodeOf("Beta") && L.NodeOf("Alpha") != L.NodeOf("Gamma###g"));
        CHECK(aShown >= 3 && gShown >= 3 && bShown == 0);                      // the chosen tab draws, the hidden one does not
        CHECK(freeShown >= 3 && !L.Contains("Free") && !UI::IsWindowDocked("Free"));
        const UI::WindowInfo wa = UI::FindWindowByName("Alpha");
        CHECK(wa.found && wa.docked && wa.size.x > 300.0f && wa.size.y > 400.0f);   // the dock content rectangle, not a floating default
        // choose the other tab: Beta shows, Alpha does not
        const int before = aShown; UI::GetDockLayout().SetActive("Beta");
        frame(); frame();
        CHECK(bShown >= 2 && aShown == before);
        // SetWindowFocus on a docked window whose tab is hidden chooses its tab (as the editor's Play does for the Game view)
        { const int a0 = aShown; UI::SetWindowFocus("Alpha"); frame(); frame(); CHECK(aShown >= a0 + 2); UI::GetDockLayout().SetActive("Beta"); frame(); }
        {   // SetNextWindowFocus before the Begin of a docked window whose tab is hidden (the editor's Play does this for Game)
            const int a0 = aShown; bool once = true;
            s.Frame([&] {
                UI::DockSpace(UI::Rect{0, 0, 800, 500});
                if (once) { UI::SetNextWindowFocus(); once = false; }
                if (UI::Begin("Alpha", &openA, 0)) ++aShown; UI::End();
                if (UI::Begin("Beta", &openB, 0)) ++bShown; UI::End();
                if (UI::Begin("Gamma###g", &openG, 0)) ++gShown; UI::End();
            });
            CHECK(aShown == a0 + 1);
            UI::GetDockLayout().SetActive("Beta"); frame();
        }
        // closing a tab through the dock: the window leaves the layout and *p_open goes false
        for (const UI::DockTabInfo& t : UI::GetDockTabs()) if (t.window == "Gamma###g") {
            const int cx = (int)(t.rect.x + t.rect.w - 6.0f), cy = (int)(t.rect.y + t.rect.h * 0.5f);
            s.Move((float)cx, (float)cy); frame(); s.Down(); frame(); s.Up(); frame(); frame();
        }
        CHECK(!openG && !UI::GetDockLayout().Contains("Gamma###g"));
        // an End with no Begin does nothing harmful; a second dock-aware window pair afterwards still works
        UI::End(); UI::End();
        frame(); frame();
        CHECK(UI::GetDockLayout().Contains("Alpha") && std::string() == std::string());
        std::string why; CHECK(UI::GetDockLayout().Validate(&why));
        // switch off: the same calls make plain windows (the layout is left alone)
        UI::SetDockAwareBegin(false); CHECK(!UI::DockAwareBegin());
        int plain = 0;
        s.Frame([&] { UI::DockSpace(UI::Rect{0, 0, 800, 500}); if (UI::Begin("Alpha2", nullptr, 0)) ++plain; UI::End(); });
        CHECK(plain == 1 && !UI::IsWindowDocked("Alpha2"));
        UI::ResetState(); CHECK(!UI::DockAwareBegin());   // ResetState switches it off
    }
    {   // Dear ImGui's rule: ConfigFlags_DockingEnable on the IO docks a laid-out window by its plain Begin, without SetDockAwareBegin
        Reset(); Sim s(800, 500); BuildTwo();
        CHECK(!UI::DockAwareBegin());
        UI::GetIO().ConfigFlags |= UI::ConfigFlags_DockingEnable;
        int aShown = 0, bShown = 0;
        for (int i = 0; i < 3; ++i) s.Frame([&] {
            UI::DockSpace(UI::Rect{0, 0, 800, 500});
            if (UI::Begin("Alpha", nullptr, 0)) { ++aShown; UI::Text("a"); } UI::End();
            if (UI::Begin("Beta", nullptr, 0)) { ++bShown; UI::Text("b"); } UI::End();
        });
        CHECK(aShown >= 3 && bShown == 0 && UI::IsWindowDocked("Alpha"));
        const UI::WindowInfo wa = UI::FindWindowByName("Alpha");
        CHECK(wa.found && wa.docked && wa.size.x > 300.0f);
        UI::GetIO().ConfigFlags &= ~UI::ConfigFlags_DockingEnable;   // and without it: a plain window again
        int plain = 0;
        s.Frame([&] { UI::DockSpace(UI::Rect{0, 0, 800, 500}); if (UI::Begin("Alpha", nullptr, 0)) ++plain; UI::End(); });
        CHECK(plain == 1);
    }
    {   // "###": the host docks "Console###Console"; the window's title then carries a count ("Console  [x 3]###Console"). Same window, still docked.
        Reset(); Sim s(800, 500);
        UI::DockBuilderRemoveNode(kSpace); UI::DockBuilderAddNode(kSpace, UI::DockNode_DockSpace); UI::DockBuilderSetNodeSize(kSpace, UI::Vec2{800, 500});
        UI::DockBuilderDockWindow("Console###Console", kSpace); UI::DockBuilderFinish(kSpace);
        UI::SetDockAwareBegin(true);
        std::string title = "Console###Console"; int shown = 0;
        auto f = [&] { s.Frame([&] { UI::DockSpace(UI::Rect{0, 0, 800, 500}); if (UI::Begin(title.c_str(), nullptr, 0)) ++shown; UI::End(); }); };
        f(); f();
        title = "Console  [x 3]###Console"; f(); f();
        CHECK(UI::GetDockLayout().Contains(title) && !UI::GetDockLayout().Contains("Console###Console"));
        CHECK(UI::IsWindowDocked(title.c_str()) && shown == 4);
        title = "Console###Console"; f();
        CHECK(UI::GetDockLayout().Contains(title) && UI::GetDockLayout().WindowCount() == 1 && shown == 5);
        CHECK(UI::GetID("Console  [x 3]###Console") == UI::GetID("Console###Console") && UI::GetID("a##x") != UI::GetID("b##x"));
        CHECK(!UI::DockAdoptTitle("Other###Other") && !UI::DockAdoptTitle(nullptr));
        std::string why; CHECK(UI::GetDockLayout().Validate(&why));
        UI::SetDockAwareBegin(false);
    }
    {   // a docked window that is not submitted (the editor's Asset Manager while it is switched off) has no tab, and its leaf shows another window
        Reset(); Sim s(800, 500); BuildTwo(); UI::SetDockAwareBegin(true);
        UI::GetDockLayout().SetActive("Beta");
        bool betaOn = false; int aShown = 0, bShown = 0;
        auto f = [&] { s.Frame([&] {
            UI::DockSpace(UI::Rect{0, 0, 800, 500});
            if (UI::Begin("Alpha", nullptr, 0)) ++aShown; UI::End();
            if (betaOn) { if (UI::Begin("Beta", nullptr, 0)) ++bShown; UI::End(); }
            if (UI::Begin("Gamma###g", nullptr, 0)) {} UI::End();
        }); };
        for (int i = 0; i < 4; ++i) f();
        bool betaTab = false; for (const UI::DockTabInfo& t : UI::GetDockTabs()) betaTab |= t.window == "Beta";
        CHECK(!betaTab && aShown >= 2 && bShown == 0);   // no tab for Beta; Alpha fills the leaf
        betaOn = true; f(); f();
        betaTab = false; for (const UI::DockTabInfo& t : UI::GetDockTabs()) betaTab |= t.window == "Beta";
        CHECK(betaTab);                                   // submitted again: its tab is back
        UI::SetDockAwareBegin(false);
    }
    {   // a pushed FramePadding (x) around DockSpace is the tabs' text inset (the editor makes room for its panel icons this way)
        Reset(); Sim s(800, 500); BuildTwo(); UI::SetDockAwareBegin(true);
        float plainW = 0.0f, paddedW = 0.0f;
        auto f = [&](bool pad) { s.Frame([&] {
            if (pad) UI::PushStyleVar(UI::StyleVar_FramePadding, UI::Vec2{40.0f, UI::GetStyle().FramePadding.y});   // (the same height)
            UI::DockSpace(UI::Rect{0, 0, 800, 500});
            if (pad) UI::PopStyleVar();
            if (UI::Begin("Alpha", nullptr, 0)) {} UI::End();
            if (UI::Begin("Beta", nullptr, 0)) {} UI::End();
            if (UI::Begin("Gamma###g", nullptr, 0)) {} UI::End();
        }); };
        f(false); f(false);
        for (const UI::DockTabInfo& t : UI::GetDockTabs()) if (t.window == "Alpha") plainW = t.idealWidth;
        f(true); f(true);
        for (const UI::DockTabInfo& t : UI::GetDockTabs()) if (t.window == "Alpha") paddedW = t.idealWidth;
        CHECK(std::fabs(paddedW - plainW - 60.0f) < 0.5f);   // 40 instead of 10 on each side
        UI::SetDockAwareBegin(false);
    }
    {   // popups and child regions inside a routed window; a window that asks to close itself
        Reset(); Sim s(800, 500); BuildTwo(); UI::SetDockAwareBegin(true);
        int popupSeen = 0; bool open = true;
        for (int i = 0; i < 6; ++i) s.Frame([&] {
            UI::DockSpace(UI::Rect{0, 0, 800, 500});
            if (UI::Begin("Alpha", &open, 0)) {
                if (UI::Button("menu")) UI::OpenPopup("pp");
                if (UI::BeginPopup("pp")) { ++popupSeen; UI::Text("in popup"); UI::EndPopup(); }
                if (UI::BeginChild("kid", UI::Vec2{100, 60}, true)) { UI::Text("child"); UI::EndChild(); }
            }
            UI::End();
        });
        CHECK(UI::GetDockLayout().Validate(nullptr) && !UI::IsWindowDocked("kid"));
    }
    {   // random: Begin / End in random order, with and without the switch, never breaks the layout
        Reset(); Sim s(800, 500); BuildTwo();
        unsigned seed = 5u; auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
        const char* names[] = {"Alpha", "Beta", "Gamma###g", "Free", ""};
        for (int i = 0; i < 300; ++i) {
            UI::SetDockAwareBegin((rnd() & 3u) != 0u);
            s.Frame([&] {
                UI::DockSpace(UI::Rect{0, 0, 800, 500});
                const int n = (int)(rnd() % 5u);
                for (int k = 0; k < n; ++k) { bool open = true; const char* nm = names[rnd() % 5u]; if (UI::Begin(nm, rnd() & 1u ? &open : nullptr, 0)) UI::Text("x"); if (rnd() % 7u) UI::End(); }
                if (rnd() % 5u == 0) { UI::End(); UI::End(); }
            });
            std::string why; if (!UI::GetDockLayout().Validate(&why)) { CHECK(false); break; }
        }
        CHECK(UI::GetDockLayout().Validate(nullptr));
        UI::ResetState();
    }
    {   // the editor's right column (owner's report 2026-11-28): Inspector, Stats and Lighting share one leaf, Stats and Lighting start closed.
        // A tab is shown for every open window of the leaf on every frame, whichever is chosen; closing the Inspector does not bring up tabs
        // for the closed Stats and Lighting (they had no content); the Inspector opened again goes back into its group as a tab.
        Reset(); Sim s(1000, 600);
        UI::DockBuilderRemoveNode(kSpace); UI::DockBuilderAddNode(kSpace, UI::DockNode_DockSpace); UI::DockBuilderSetNodeSize(kSpace, UI::Vec2{1000, 600});
        UI::DockId centre = kSpace, right = 0;
        UI::DockBuilderSplitNode(centre, UI::DockSide_Right, 0.4f, &right, &centre);
        UI::DockBuilderDockWindow("Scene", centre);
        UI::DockBuilderDockWindow("Inspector", right); UI::DockBuilderDockWindow("Stats", right); UI::DockBuilderDockWindow("Lighting", right);
        UI::DockBuilderFinish(kSpace);
        UI::SetDockAwareBegin(true);
        bool openI = true, openS = false, openL = false;
        auto f = [&] { s.Frame([&] {
            UI::DockSpace(UI::Rect{0, 0, 1000, 600});
            if (UI::Begin("Scene", nullptr, 0)) UI::Text("scene"); UI::End();
            if (openI) { if (UI::Begin("Inspector", &openI, 0)) UI::Text("inspector"); UI::End(); }   // the editor draws the Inspector early ...
            if (openS) { if (UI::Begin("Stats", &openS, 0)) UI::Text("stats"); UI::End(); }           // ... and Stats and Lighting near the end
            if (openL) { if (UI::Begin("Lighting", &openL, 0)) UI::Text("lighting"); UI::End(); }
        }); };
        auto tabsOf = [&](int leaf) { std::vector<std::string> v; for (const UI::DockTabInfo& t : UI::GetDockTabs()) if (t.node == leaf) v.push_back(t.window); return v; };
        auto has = [](const std::vector<std::string>& v, const char* w) { for (const std::string& x : v) if (x == w) return true; return false; };
        const UI::DockLayout& L = UI::GetDockLayout();
        UI::GetDockLayout().SetActive("Lighting");   // a saved layout whose chosen tab was Lighting, while Lighting is closed now (owner, at launch)
        f();
        {   // the very first frame (the editor's first frame at launch can stay on screen a while): no tab of a closed window
            bool ghost = false;
            for (const UI::DockTabInfo& t : UI::GetDockTabs()) ghost |= t.window == "Stats" || t.window == "Lighting";
            if (ghost) std::cerr << "  the first frame shows tabs for the closed Stats and Lighting\n";
            CHECK(!ghost);
        }
        f();
        const int leaf = L.NodeOf("Inspector");
        CHECK(leaf != 0 && L.NodeOf("Stats") == leaf && L.NodeOf("Lighting") == leaf);
        CHECK(tabsOf(leaf) == std::vector<std::string>{"Inspector"});   // Stats and Lighting are closed: no tabs for them
        {   // the node a ported host reads lists only the open window, also when the host hides the strip (the editor's side tabs, which
            // listed Inspector, Stats and Lighting at launch, and clicking Stats or Lighting showed nothing)
            UI::DockNodeRef* node = UI::DockBuilderGetNode(right);
            CHECK(node && node->Windows.Size == 1 && std::string(node->Windows[0]->Name) == "Inspector");
            UI::GetDockLayout().SetNoTabBar(leaf, true); f(); f();
            node = UI::DockBuilderGetNode(right);
            if (node && node->Windows.Size != 1) std::cerr << "  with its strip hidden, the node lists " << node->Windows.Size << " windows\n";
            CHECK(node && node->Windows.Size == 1);
            UI::GetDockLayout().SetNoTabBar(leaf, false); f();
        }
        // close the Inspector with its tab's cross
        UI::Rect cross{};
        for (const UI::DockTabInfo& t : UI::GetDockTabs()) if (t.window == "Inspector") cross = t.rect;
        s.Move(cross.x + cross.w - 5.0f, cross.y + cross.h * 0.5f); f(); s.Down(); f(); s.Up(); f(); f();
        CHECK(!openI);
        const std::vector<std::string> afterClose = tabsOf(L.NodeOf("Stats"));
        if (!afterClose.empty()) std::cerr << "  after closing the Inspector, " << afterClose.size() << " tab(s) of closed windows show, first: " << afterClose[0] << "\n";
        CHECK(afterClose.empty());                                          // no empty tabs for the closed Stats and Lighting
        // open the Inspector again (View > Panels > Inspector): it is a tab of its group again, not a floating window
        openI = true; f(); f();
        if (L.IsFloating("Inspector")) std::cerr << "  the Inspector opened again floats instead of going back to its group\n";
        CHECK(!L.IsFloating("Inspector") && L.NodeOf("Inspector") == L.NodeOf("Lighting") && L.NodeOf("Inspector") != 0);
        CHECK(UI::IsWindowDocked("Inspector"));
        // open Stats and Lighting: every frame from the first one, all three tabs show, whichever is chosen
        openS = openL = true;
        for (int frame = 0; frame < 12; ++frame) {
            if (frame % 3 == 0) UI::GetDockLayout().SetActive(frame % 9 == 0 ? "Inspector" : frame % 9 == 3 ? "Stats" : "Lighting");
            f();
            if (frame == 0) continue;   // (the frame they are first drawn: their Begin runs after the dock space)
            const std::vector<std::string> v = tabsOf(L.NodeOf("Inspector"));
            CHECK(v.size() == 3 && has(v, "Inspector") && has(v, "Stats") && has(v, "Lighting"));
        }
        // each tab is clickable and chooses its window
        for (const char* w : {"Stats", "Lighting", "Inspector"}) {
            UI::Rect r{};
            for (const UI::DockTabInfo& t : UI::GetDockTabs()) if (t.window == w) r = t.rect;
            s.Move(r.x + 6.0f, r.y + r.h * 0.5f); f(); s.Down(); f(); s.Up(); f();
            const UI::DockLayout::Node* n = L.Find(L.NodeOf(w));
            CHECK(n && n->tabs[(size_t)n->active] == w);
        }
        {   // the side tabs' highlight: with the strip hidden and the LAST tab chosen, the chosen window is the one marked DockTabIsVisible; the
            // node's first window is the first in tab order (the editor highlighted Windows[0], so another panel stayed lit while one was shown)
            const int leafNow = L.NodeOf("Stats");
            const std::string last = L.Find(leafNow)->tabs.back();
            UI::GetDockLayout().SetActive(last);
            UI::GetDockLayout().SetNoTabBar(leafNow, true); f(); f();
            UI::DockNodeRef* node = UI::DockBuilderGetNode(right);
            int lit = 0; std::string litName;
            if (node) for (int k = 0; k < node->Windows.Size; ++k) if (node->Windows[k]->DockTabIsVisible) { ++lit; litName = node->Windows[k]->Name; }
            CHECK(node && node->Windows.Size == 3 && std::string(node->Windows[0]->Name) != last && lit == 1 && litName == last);
            UI::GetDockLayout().SetNoTabBar(leafNow, false); f();
        }
        std::string why; CHECK(L.Validate(&why));
        UI::SetDockAwareBegin(false);
    }
    {   // a narrow strip with many tabs: every tab is still on the strip, wide enough to click, and a click on it chooses it
        Reset(); Sim s(800, 500);
        UI::DockBuilderRemoveNode(kSpace); UI::DockBuilderAddNode(kSpace, UI::DockNode_DockSpace); UI::DockBuilderSetNodeSize(kSpace, UI::Vec2{800, 500});
        UI::DockId centre = kSpace, right = 0;
        UI::DockBuilderSplitNode(centre, UI::DockSide_Right, 0.25f, &right, &centre);
        UI::DockBuilderDockWindow("Scene", centre);
        std::vector<std::string> names;
        for (int i = 0; i < 12; ++i) { names.push_back("Panel number " + std::to_string(i)); UI::DockBuilderDockWindow(names.back().c_str(), right); }
        UI::DockBuilderFinish(kSpace);
        UI::SetDockAwareBegin(true);
        auto f = [&] { s.Frame([&] {
            UI::DockSpace(UI::Rect{0, 0, 800, 500});
            if (UI::Begin("Scene", nullptr, 0)) {} UI::End();
            for (const std::string& n : names) { if (UI::Begin(n.c_str(), nullptr, 0)) UI::Text("x"); UI::End(); }
        }); };
        f(); f();
        const UI::DockLayout& L = UI::GetDockLayout();
        const int leaf = L.NodeOf(names[0]);
        const UI::Rect strip = UI::GetDockStripRect(leaf);
        CHECK(strip.w > 100.0f && strip.w < 260.0f);
        for (const std::string& n : names) {
            UI::Rect r{}; bool found = false;
            for (const UI::DockTabInfo& t : UI::GetDockTabs()) if (t.window == n) { r = t.rect; found = true; }
            CHECK(found && r.w >= 8.0f && r.x >= strip.x - 0.5f && r.x + r.w <= strip.x + strip.w + 0.5f);
            s.Move(r.x + std::min(4.0f, r.w * 0.3f), r.y + r.h * 0.5f); f(); s.Down(); f(); s.Up(); f();
            const UI::DockLayout::Node* node = L.Find(leaf);
            CHECK(node && node->tabs[(size_t)node->active] == n);
        }
        UI::SetDockAwareBegin(false);
    }
    TEST_MAIN_RESULT();
}
