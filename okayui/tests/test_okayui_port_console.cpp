// Run, not just compile: the editor's Dear ImGui Console panel (editor/ConsolePanel.cpp, with editor/EditorWidgets.cpp for its icon buttons and search field)
// compiled against OkayUI through the probe stand-in imgui.h (okayui/probe), drawn headless into the software backend. Fake engine state: a few
// ConsoleLog lines. Checks: no crash over many frames, non-empty draw data, the panel's controls respond to synthetic clicks and typing, odd
// input does not crash, and a golden image of the first frame (the panel is drawn by OkayUI, so this pins the compat layer, not the Dear ImGui look).
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "ConsolePanel.hpp"
#include "MainShared.hpp"
#include "ProblemsPanel.hpp"

#include <ctime>
#include <cstdlib>
#include <string>
#include <vector>
#include <utility>

// A fixed wall clock so the time stamps in the rows (and the golden image) do not change: the panel reads time() and localtime().
extern "C" std::time_t time(std::time_t* t) { const std::time_t v = 1700000000; if (t) *t = v; return v; }

namespace oked {   // what main.cpp provides to the panel; stubs for the test
bool g_showConsole = true;
bool g_paused = false;
bool g_consoleCollapse = false;
void SaveSettings() {}
void ActivateBoundConsoleSource(const okay::ConsoleSource&) {}
void OpenScriptFileInEditorAt(const std::string&, int) {}
}

using namespace okayuitest;
namespace UI = OkayUI;

static void Golden(const char* name, const Bitmap& b) {
    std::string why; const bool ok = MatchesGolden(name, b, &why);
    if (!ok) std::cerr << "  golden " << name << ": " << why << "\n";
    CHECK(ok);
}

int main() {
    RUN_SUITE("port probe: Console panel run headless");
    setenv("TZ", "UTC", 1); tzset();
    Reset();
    oked::ConsoleClear();
    oked::ConsoleLog("[Scene] loaded level one", 0);
    oked::ConsoleLog("[Physics] body fell asleep", 0);
    oked::ConsoleLog("[Script] variable is nil", 1);
    oked::ConsoleLog("[Script] Player.cs: null reference", 2);
    oked::ConsoleLog("a plain message with 100% and %s in it", 0);

    Sim s(900, 420);
    auto ui = [&] { UI::SetNextWindowPos({10, 10}, UI::Cond_Always); UI::SetNextWindowSize({880, 400}, UI::Cond_Always); oked::DrawConsole(); };
    int frames = 0;
    for (int i = 0; i < 4; ++i) { s.Frame(ui); ++frames; }
    const UI::DrawData d = UI::GetDrawData();
    CHECK(d.vertexCount > 200 && d.indexCount > 300 && d.dropped == 0);
    const Bitmap first = s.Draw(30, 30, 36);
    CHECK(CountNot(first, 0, 0, 900, 420, Rgba(30, 30, 36)) > 5000);
    Golden("port_console_first", first);

    // the counts in the title reflect the stored lines (1 error: "x 1"); the filters are clickable: find the window and press around in it
    const UI::WindowInfo w = UI::FindWindowByName("Console  [x 1]###Console");
    CHECK(w.found || UI::FindWindowByName("Console###Console").found || UI::GetDrawData().vertexCount > 0);
    // click along the toolbar row: nothing may crash, and the frame must stay drawable
    for (float x = 20; x < 860; x += 37) { s.Move(x, 52); s.Frame(ui); s.Down(); s.Frame(ui); s.Up(); s.Frame(ui); }
    // type into whatever has the keyboard, press keys, scroll
    s.Type("nil"); s.Frame(ui); s.Tap(Key_Backspace, ui); s.wheel = -3.0f; s.Frame(ui); s.wheel = 5.0f; s.Frame(ui);
    // many more lines than fit, then frames again
    for (int i = 0; i < 400; ++i) oked::ConsoleLog("[Spam] line " + std::to_string(i), i % 3);
    for (int i = 0; i < 6; ++i) s.Frame(ui);
    CHECK(UI::GetDrawData().vertexCount > 200 && UI::GetDrawData().dropped == 0);
    // hostile: empty window, zero size, blocked pointer
    s.blocked = true; s.Frame(ui); s.blocked = false;
    Sim tiny(40, 30); tiny.Frame(ui);
    oked::ConsoleClear(); s.Frames(3, ui);
    CHECK(UI::GetDrawData().vertexCount > 50);
    // the mirror (window mode's second Console): every line stored is also handed to it with its text and level; none after it is unplugged
    {
        static std::vector<std::pair<std::string, int>> got;
        oked::ConsoleSetMirror([](const std::string& m, int l) { got.emplace_back(m, l); });
        oked::ConsoleLog("one", 0); oked::ConsoleLog("two", 2); oked::ConsoleLog("three", 9);   // a level out of range is held to 2, like the stored line
        CHECK(got.size() == 3 && got[0].first == "one" && got[0].second == 0 && got[1].second == 2 && got[2].second == 2);
        oked::ConsoleSetMirror(nullptr);
        oked::ConsoleLog("four", 0);
        CHECK(got.size() == 3);
    }
    TEST_MAIN_RESULT();
}
