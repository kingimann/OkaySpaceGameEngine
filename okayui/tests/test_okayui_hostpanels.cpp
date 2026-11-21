// Window mode with the editor's real panels (editor/OkayUIHostPanels.cpp: ConsolePanel.cpp, EditorWidgets.cpp, SceneManagerPanel.cpp and the dialogs of
// ProjectPanelUI.cpp compiled against OkayUI; the Project window over the real okaypb::Browser), run headless through the real frame loop with a fake backend,
// a software painter and the stand-in editor state (docs/ui/own_ui_toolkit.md 6.17). Step 5 of the window-mode plan, 6.24.
#define OKAY_HAVE_OKAYUI 1
#define OKAYUI_HOST_PLATFORM 1
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "OkayUIFrameLoop.hpp"
#include "OkayUIHostPanels.hpp"
#include "FakePlatformBackend.hpp"
#include "EditorState.hpp"
#include "SceneManagerPanel.hpp"
#include "ProblemsPanel.hpp"
#include "okay/UI/OkayUIDock.hpp"
#include "okay/Debug/ConsoleModel.hpp"
#include "DialogService.hpp"

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>

extern "C" std::time_t time(std::time_t* t) { const std::time_t v = 1700000000; if (t) *t = v; return v; }   // fixed clock: the Console stamps its rows

namespace fs = std::filesystem;
namespace oked {   // what main.cpp provides to the panels in the editor; stubs for the test
bool g_showScenes = true;
bool g_showConsole = true, g_paused = false, g_consoleCollapse = false;
void SaveSettings() {}
void ActivateBoundConsoleSource(const okay::ConsoleSource&) {}
void OpenScriptFileInEditorAt(const std::string&, int) {}
static std::vector<SceneBuildEntry> g_build;
std::vector<SceneBuildEntry> SceneBuildSnapshot() { return g_build; }
void SceneBuildApply(okay::editor::EditorState&, const std::vector<SceneBuildEntry>& e) { g_build = e; }
SDL_Texture* GetSceneThumb(const std::string&) { return nullptr; }
void AddRecent(const std::string&) {}
void RunAfterSavePrompt(okay::editor::EditorState&, const std::string&, std::function<void()> action, std::function<void()>) { if (action) action(); }
void ShowErrorPopup(const std::string&, const std::string&) {}
// The editor's own Console copy (ConsolePanel.cpp) is not linked here: a stand-in with the same mirror contract (its test is in okayui_port_console).
static void (*g_mirror)(const std::string&, int) = nullptr;
void ConsoleSetMirror(void (*fn)(const std::string&, int)) { g_mirror = fn; }
void ConsoleLog(const std::string& msg, int level) { if (g_mirror) g_mirror(msg, level); }
}

namespace oked {   // the renamed calls the panel sources make (OkayUIHostPanels.cpp): they go to the current DialogService
void ShowErrorPopup_WM(const std::string& title, const std::string& text);
void RunAfterSavePrompt_WM(okay::editor::EditorState&, const std::string& doing, std::function<void()> action, std::function<void()> onCancel);
void OpenScriptFileInEditorAt_WM(const std::string& path, int line);
}
namespace UI = OkayUI;
namespace plat = okay::platform;
using fakeplat::FakeBackend; using fakeplat::Move; using fakeplat::Btn;

int main() {
    RUN_SUITE("okayui window mode: the real panels");
    setenv("TZ", "UTC", 1); tzset();
    const fs::path proj = fs::temp_directory_path() / "okayui_hostpanels_proj";
    std::error_code ec; fs::remove_all(proj, ec);
    fs::create_directories(proj / "Assets" / "Scenes", ec); fs::create_directories(proj / "Assets" / "Textures", ec);
    std::ofstream(proj / "Assets" / "Scenes" / "level1.okayscene") << "{}";
    std::ofstream(proj / "Assets" / "readme.txt") << "hello";

    okayuitest::Reset();
    auto painter = UI::soft::CreateBitmapPainter(1100, 700);
    FakeBackend be; okayuihost::platform::FrameLoop loop(be, *painter);
    okay::editor::EditorState ed; ed.SetProjectDir(proj.generic_string());
    oked::HostModeState st;
    st.panels = oked::wm::MakeHooks(&ed, proj.generic_string(), &st);
    CHECK(st.panels.console && st.panels.sceneManager && st.panels.project && st.panels.before && st.panels.after);
    auto frame = [&] { be.now += 16; return loop.Frame([&] { oked::DrawHostModeUi(st); }); };

    for (int i = 0; i < 6; ++i) CHECK(frame());
    const UI::DockLayout& L = UI::GetDockLayout();
    std::string why; CHECK(L.Validate(&why));
    for (const char* w : {oked::kPanelProject, oked::kPanelConsole, oked::kPanelSceneManager, oked::kPanelMixer, oked::kPanelStatus})
        CHECK(L.Contains(w) && !L.IsFloating(w));
    CHECK(L.NodeOf(oked::kPanelProject) != L.NodeOf(oked::kPanelConsole) && L.NodeOf(oked::kPanelStatus) == L.NodeOf(oked::kPanelSceneManager));
    // the Project window is on the left, the Console along the bottom
    const UI::DockLayout::Node* np = L.Find(L.NodeOf(oked::kPanelProject)); const UI::DockLayout::Node* nc = L.Find(L.NodeOf(oked::kPanelConsole));
    CHECK(np && nc && np->rect.x == 0.0f && np->rect.w < 400.0f && nc->rect.y > 400.0f && nc->rect.w > 600.0f);
    // the panels really drew (the Project window lists the folder, the Scene Manager its page)
    const UI::DrawData dd = UI::GetDrawData();
    CHECK(dd.vertexCount > 3000 && dd.dropped == 0);
    const oked::wm::ProjectProbe pr = oked::wm::GetProjectProbe();
    CHECK(pr.newFolder.w > 20.0f && pr.firstRow.w > 20.0f && fs::path(pr.current).filename() == "Assets");

    // the Console: a logged line makes the panel draw more
    const int before = UI::GetDrawData().vertexCount;
    for (int i = 0; i < 12; ++i) oked::wm::ConsoleLog("Loaded level " + std::to_string(i), i % 3);
    frame(); frame();
    CHECK(UI::GetDrawData().vertexCount > before + 200);

    // step 8: a line logged through the editor's own Console (the copy every tool writes to) reaches the window-mode Console too, with its level
    {
        int c0[3], c1[3]; oked::wm::ConsoleCounts(c0);
        oked::ConsoleLog("from the editor: info", 0); oked::ConsoleLog("from the editor: warning", 1); oked::ConsoleLog("from the editor: error", 2);
        oked::wm::ConsoleCounts(c1);
        CHECK(c1[0] == c0[0] + 1 && c1[1] == c0[1] + 1 && c1[2] == c0[2] + 1);
        oked::ConsoleSetMirror(nullptr);   // unplugged: nothing more arrives
        oked::ConsoleLog("not mirrored", 0);
        int c2[3]; oked::wm::ConsoleCounts(c2);
        CHECK(c2[0] == c1[0]);
        oked::wm::MakeHooks(nullptr, std::string(), nullptr);   // plugs it back in
        oked::ConsoleLog("mirrored again", 0);
        oked::wm::ConsoleCounts(c2);
        CHECK(c2[0] == c1[0] + 1);
    }

    // step 7: the panels' error / save-question / open-script calls reach the current DialogService; without one an error still lands in the Console
    {
        struct Fake : oked::DialogService {
            std::string err, doing, script; int line = 0, asked = 0;
            void ShowError(const std::string& t, const std::string&) override { err = t; }
            void RunAfterSavePrompt(const std::string& d, std::function<void()> a, std::function<void()>) override { doing = d; ++asked; if (a) a(); }
            void OpenScriptAt(const std::string& p, int l) override { script = p; line = l; }
        } f;
        oked::SetDialogs(&f);
        int ran = 0;
        oked::ShowErrorPopup_WM("Couldn't open", "why"); oked::RunAfterSavePrompt_WM(ed, "closing", [&] { ++ran; }, {}); oked::OpenScriptFileInEditorAt_WM("a.cs", 7);
        CHECK(f.err == "Couldn't open" && f.doing == "closing" && f.asked == 1 && ran == 1 && f.script == "a.cs" && f.line == 7);
        oked::SetDialogs(nullptr);
        int c0[3], c1[3]; oked::wm::ConsoleCounts(c0);
        oked::ShowErrorPopup_WM("No service", "still logged"); oked::RunAfterSavePrompt_WM(ed, "closing", [&] { ++ran; }, {});
        oked::wm::ConsoleCounts(c1);
        CHECK(c1[2] == c0[2] + 1 && ran == 2);
    }

    if (const char* shot = std::getenv("OKAYUI_SHOT")) {   // a picture of the window, for looking at (not a golden)
        std::vector<std::uint8_t> px; int w = 0, h = 0;
        for (int i = 0; i < 12; ++i) oked::wm::ConsoleLog("Loaded level " + std::to_string(i), i % 3);
        frame(); frame();
        if (painter->ReadPixels(px, w, h)) okay::ImageWrite::WritePNG(shot, w, h, 4, px.data());
    }
    // the Project window: New Folder opens the real name prompt; typing a name and Enter makes the folder
    const int bx = (int)(pr.newFolder.x + pr.newFolder.w * 0.5f), by = (int)(pr.newFolder.y + pr.newFolder.h * 0.5f);
    be.queue.push_back(Move(bx, by)); frame();
    be.queue.push_back(Btn(true, bx, by)); frame();
    be.queue.push_back(Btn(false, bx, by)); frame(); frame();
    CHECK(oked::wm::GetProjectProbe().namePromptOpen);
    // (the field starts with "New Folder"; select all and type the new name)
    { plat::Event e; e.type = plat::EventType::KeyDown; e.key = plat::Key::A; e.mods = plat::ModCtrl; be.queue.push_back(e); }
    { plat::Event e; e.type = plat::EventType::TextInput; e.text = "Maps"; be.queue.push_back(e); }
    frame(); frame();
    { plat::Event e; e.type = plat::EventType::KeyDown; e.key = plat::Key::Return; be.queue.push_back(e); }
    { plat::Event e; e.type = plat::EventType::KeyUp; e.key = plat::Key::Return; be.queue.push_back(e); }
    frame(); frame(); frame();
    CHECK(fs::is_directory(proj / "Assets" / "Maps") || fs::is_directory(proj / "Assets" / "New Folder"));

    // closing a panel's tab: its open flag goes false, it leaves the layout and is not drawn any more
    for (const UI::DockTabInfo& t : UI::GetDockTabs()) if (t.window == oked::kPanelSceneManager) {
        const int cx = (int)(t.rect.x + t.rect.w - 6.0f), cy = (int)(t.rect.y + t.rect.h * 0.5f);
        be.queue.push_back(Move(cx, cy)); frame(); be.queue.push_back(Btn(true, cx, cy)); frame(); be.queue.push_back(Btn(false, cx, cy)); frame(); frame();
    }
    CHECK(!st.IsOpen(oked::kPanelSceneManager) && !L.Contains(oked::kPanelSceneManager) && oked::g_showScenes == false);
    for (int i = 0; i < 4; ++i) frame();
    CHECK(!L.Contains(oked::kPanelSceneManager) && L.Contains(oked::kPanelConsole) && L.Validate(&why));   // it stays closed (no floating copy came back)
    // the layout, the open flags: saved and restored by the state module
    const okayuihost::PersistedState saved = oked::ToPersisted(st, okayuihost::WindowState());
    CHECK(saved.open.count(oked::kPanelSceneManager) && !saved.open.at(oked::kPanelSceneManager) && saved.layout.find("Console") != std::string::npos);

    // a window with none of the panels wired in still gets the two screens
    {
        okayuitest::Reset();
        okay::editor::EditorState dummy; (void)dummy;
        oked::HostModeState bare;
        auto p2 = UI::soft::CreateBitmapPainter(800, 500); FakeBackend be2; okayuihost::platform::FrameLoop l2(be2, *p2);
        for (int i = 0; i < 3; ++i) l2.Frame([&] { oked::DrawHostModeUi(bare); });
        CHECK(UI::GetDockLayout().Contains(oked::kPanelMixer) && !UI::GetDockLayout().Contains(oked::kPanelConsole));
    }
    fs::remove_all(proj, ec);
    TEST_MAIN_RESULT();
}
