// Run, not just compile: the editor's Scene Manager panel (editor/SceneManagerPanel.cpp) compiled against OkayUI through the probe stand-in imgui.h,
// with a stand-in EditorState (okayui/tests/shadow/EditorState.hpp: project folder, open path, Load / Save / NewScene recorded in memory) and a
// temporary project folder with three scene files. The functions main.cpp provides are stubbed. Drawn headless; the panel's buttons are pressed
// with synthetic clicks. See docs/ui/own_ui_toolkit.md 6.17.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "SceneManagerPanel.hpp"
#include "EditorState.hpp"   // the stand-in (the macro below makes editor/EditorState.hpp include it)
#include "ConsolePanel.hpp"

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>

namespace fs = std::filesystem;
namespace oked {   // what main.cpp provides to the panel
bool g_showScenes = true;
bool g_showConsole = true, g_paused = false, g_consoleCollapse = false, g_clearConsoleOnPlay = false;   // (the Console options menu toggles a real bool now)
void SaveSettings() {}
void ActivateBoundConsoleSource(const okay::ConsoleSource&) {}
void OpenScriptFileInEditorAt(const std::string&, int) {}
static std::vector<SceneBuildEntry> g_build;
std::vector<SceneBuildEntry> SceneBuildSnapshot() { return g_build; }
void SceneBuildApply(okay::editor::EditorState&, const std::vector<SceneBuildEntry>& e) { g_build = e; }
OkayUI::Tex GetSceneThumb(const std::string&) { return nullptr; }
void AddRecent(const std::string&) {}
void RunAfterSavePrompt(okay::editor::EditorState&, const std::string&, std::function<void()> action, std::function<void()>) { if (action) action(); }
void ShowErrorPopup(const std::string&, const std::string&) {}
}

using namespace okayuitest;
namespace UI = OkayUI;

static void Golden(const char* name, const Bitmap& b) {
    std::string why; const bool ok = MatchesGolden(name, b, &why);
    if (!ok) std::cerr << "  golden " << name << ": " << why << "\n";
    CHECK(ok);
}

int main() {
    RUN_SUITE("port probe: Scene Manager panel run headless");
    const fs::path proj = fs::temp_directory_path() / "okayui_port_scene_test";
    std::error_code ec; fs::remove_all(proj, ec);
    fs::create_directories(proj / "Assets" / "Scenes", ec);
    for (const char* n : {"Main", "Level2", "Menu"}) std::ofstream(proj / "Assets" / "Scenes" / (std::string(n) + ".okayscene")) << "{}";
    okay::editor::EditorState ed; ed.SetProjectDir(proj.string());

    Reset(); Sim s(720, 520);
    auto ui = [&] { UI::SetNextWindowPos({10, 10}, UI::Cond_Always); UI::SetNextWindowSize({700, 500}, UI::Cond_Always); oked::DrawSceneManager(ed); };
    s.Frames(5, ui);
    CHECK(UI::GetDrawData().vertexCount > 300 && UI::GetDrawData().dropped == 0);
    const Bitmap first = s.Draw(26, 28, 34);
    CHECK(CountNot(first, 0, 0, 720, 520, Rgba(26, 28, 34)) > 8000);
    Golden("port_scenemanager_list", first);
    // press around the panel: rows, tick boxes, buttons, the Create dialog's fields. Nothing may crash; files stay where they are.
    for (float y = 70; y < 480; y += 23) for (float x = 30; x < 690; x += 71) { s.Move(x, y); s.Frame(ui); s.Down(); s.Frame(ui); s.Up(); s.Frame(ui); }
    s.Type("abc"); s.Frame(ui); s.Tap(Key_Escape, ui); s.wheel = -3; s.Frame(ui);
    CHECK(UI::GetDrawData().vertexCount > 100 && UI::GetDrawData().dropped == 0);
    // playing disables the editing buttons; no project: the page says so
    ed.SetPlaying(true); s.Frames(2, ui); CHECK(UI::GetDrawData().vertexCount > 100);
    ed.SetPlaying(false); ed.SetProjectDir(""); s.Frames(2, ui); CHECK(UI::GetDrawData().vertexCount > 100);
    s.blocked = true; s.Frame(ui); s.blocked = false;
    Sim tiny(50, 30); tiny.Frame(ui);
    fs::remove_all(proj, ec);
    TEST_MAIN_RESULT();
}
