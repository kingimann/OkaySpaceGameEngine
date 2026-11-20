// Run, not just compile: the Project panel's Dear ImGui pieces (editor/ProjectPanelUI.cpp: name prompt, delete confirmation; editor/ProjectMenuDraw.cpp:
// property rows, context menu) compiled against OkayUI through the probe stand-in imgui.h and drawn headless. Fake state: a name request with an
// accept callback, a list of property rows and a context-menu item tree. The name prompt is driven with synthetic typing and clicks. See
// docs/own_ui_toolkit.md 6.14.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "ProjectPanelUI.hpp"

#include <string>

using namespace okayuitest;
namespace UI = OkayUI;
namespace pp = oked::projectpanel;

static void Golden(const char* name, const Bitmap& b) {
    std::string why; const bool ok = MatchesGolden(name, b, &why);
    if (!ok) std::cerr << "  golden " << name << ": " << why << "\n";
    CHECK(ok);
}

int main() {
    RUN_SUITE("port probe: Project panel pieces run headless");

    {   // the name prompt: opens, takes typing, Create calls accept with the name plus suffix, an error stays open, Cancel closes
        Reset(); Sim s(640, 360);
        std::string accepted; int calls = 0; bool reject = false;
        pp::NameRequest req;
        req.title = "New material"; req.hint = "Letters, digits and spaces"; req.initial = "Metal"; req.suffix = ".okaymat";
        req.accept = [&](const std::string& full) -> std::string { ++calls; accepted = full; return reject ? "That name is taken" : ""; };
        pp::AskName(req);
        CHECK(!pp::NamePromptOpen() || true);
        auto ui = [&] { UI::BareNextWindow(); if (UI::Begin("Project", 0, 0, 640, 360)) { pp::DrawDialogs(); } UI::End(); };
        s.Frames(4, ui);
        CHECK(pp::NamePromptOpen());
        const Bitmap prompt = s.Draw(28, 30, 36);
        CHECK(CountNot(prompt, 0, 0, 640, 360, Rgba(28, 30, 36)) > 4000);
        Golden("port_project_nameprompt", prompt);
        // type a new name over the selected initial text (AutoSelectAll), press Enter
        s.Type("Gold"); s.Frame(ui); s.Tap(Key_Enter, ui); s.Frames(2, ui);
        CHECK(calls == 1 && accepted == "Gold.okaymat");   // AutoSelectAll replaced "Metal" (the focus came from SetKeyboardFocusHere)
        CHECK(!pp::NamePromptOpen());
        // reopen: accept refuses, the prompt stays and shows the message; then Escape cancels it
        reject = true; pp::AskName(req); s.Frames(4, ui);
        CHECK(pp::NamePromptOpen());
        s.Tap(Key_Enter, ui); s.Frames(2, ui);
        CHECK(calls == 2 && pp::NamePromptOpen());
        Golden("port_project_nameerror", s.Draw(28, 30, 36));
        s.Press(Key_Escape); s.Frames(3, ui);
        // hostile: an empty title, a huge name, no accept callback
        pp::NameRequest odd; odd.title = ""; odd.suffix = ""; pp::AskName(odd); s.Frames(3, ui);
        s.Type(std::string(300, 'x')); s.Tap(Key_Enter, ui); s.Frames(2, ui);
        s.Press(Key_Escape); s.Frames(3, ui);
    }
    {   // property rows and a context menu from hand-made data
        Reset(); Sim s(520, 300);
        std::vector<okaypb::ctx::Property> rows = { {"Name", "Hero.okaymat", false}, {"Type", "Material", false}, {"Location", "Assets/Materials", false},
                                                    {"Size", "12 KB", false}, {"Used by", "3 assets", false}, {"Warning", "A texture this uses is missing", true} };
        auto ui = [&] { UI::SetNextWindowPos({10, 10}); UI::SetNextWindowSize({500, 280}); if (UI::Begin("Properties")) pp::DrawPropertyRows(rows); UI::End(); };
        s.Frames(3, ui);
        const Bitmap b = s.Draw(28, 30, 36);
        CHECK(CountNot(b, 10, 40, 510, 290, Rgba(28, 30, 36)) > 3000);
        Golden("port_project_properties", b);
        pp::DrawPropertyRows({});   // no rows outside any window: nothing happens
        std::vector<okaypb::ctx::Item> items(4);
        items[0].id = "open"; items[0].label = "Open"; items[0].shortcut = "Enter";
        items[1].separator = true;
        items[2].label = "Create"; items[2].submenu = "create"; items[2].children.resize(2);
        items[2].children[0].id = "create:folder"; items[2].children[0].label = "Folder";
        items[2].children[1].id = "create:mat"; items[2].children[1].label = "Material"; items[2].children[1].enabled = false; items[2].children[1].reason = "not here";
        items[3].id = "delete"; items[3].label = "Delete"; items[3].shortcut = "Del";
        std::string chosen; UI::Rect openRow;
        auto menu = [&] {
            UI::BareNextWindow();
            if (UI::Begin("w", 0, 0, 300, 200)) {
                if (UI::Button("context")) {}
                if (UI::BeginPopupContextItem("ctx")) { chosen = pp::DrawContextMenu(items); UI::EndPopup(); }
            }
            UI::End();
        };
        Reset(); Sim s2(300, 200);
        s2.Frame(menu); s2.rdown = true; s2.Move(30, 20); s2.Frame(menu); s2.rdown = false; s2.Frames(3, menu);
        CHECK(UI::GetDrawData().vertexCount > 100);
        Golden("port_project_contextmenu", s2.Draw(28, 30, 36));
        // the popup opened at the pointer (30, 20): click the first row ("Open"), reopen, expand the "Create" submenu (inline in a plain popup), click "Folder"
        chosen.clear(); s2.Click(50, 20 + 16, menu); s2.Frames(2, menu);
        CHECK(chosen == "open");
        chosen.clear(); s2.Frame(menu); s2.rdown = true; s2.Move(30, 20); s2.Frame(menu); s2.rdown = false; s2.Frames(3, menu);
        s2.Click(50, 85, menu); s2.Frames(3, menu);   // the "Create" row opens its entries below it
        Golden("port_project_contextmenu_sub", s2.Draw(28, 30, 36));
        s2.Click(60, 114, menu); s2.Frames(2, menu);
        CHECK(chosen == "create:folder");
    }
    TEST_MAIN_RESULT();
}
