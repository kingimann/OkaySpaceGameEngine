// Dock-aware Begin / End (SetDockAwareBegin), headless: code written for Dear ImGui opens its window with Begin and closes it with End; when the title is a
// window of the dock layout it must sit in the dock like one written for DockBegin. Covers the chosen tab and the hidden one, closing a tab, windows
// the layout does not hold, the switch off, unbalanced End, popups inside, and a random run that keeps the layout valid.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "okay/UI/OkayUIInternals.hpp"

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
    TEST_MAIN_RESULT();
}
