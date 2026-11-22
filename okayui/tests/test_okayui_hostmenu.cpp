// Window mode: the menu bar (editor/OkayUIHostMenu.cpp: the real MenuBarModel entries drawn through OkayUI, actions over the REAL EditorState) and the keyboard
// shortcuts, run through the real frame loop with a fake backend and a software painter. Docs 6.24, round 12.
#define OKAY_HAVE_OKAYUI 1
#define OKAYUI_HOST_PLATFORM 1
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "OkayUIFrameLoop.hpp"
#include "OkayUIHostMenu.hpp"
#include "FakePlatformBackend.hpp"
#include "EditorState.hpp"
#include "MenuBarModel.hpp"
#include "okay/UI/OkayUIDock.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace oked { static std::vector<std::string> g_log; void ConsoleLog(const std::string& m, int) { g_log.push_back(m); } }

namespace UI = OkayUI;
namespace plat = okay::platform;
using fakeplat::FakeBackend; using fakeplat::Move; using fakeplat::Btn;

int main() {
    RUN_SUITE("okayui window mode: menu bar and shortcuts");
    okay::editor::EditorState ed; ed.NewScene3D();
    okayuitest::Reset();
    auto painter = UI::soft::CreateBitmapPainter(1000, 650);
    FakeBackend be; okayuihost::platform::FrameLoop loop(be, *painter);
    oked::HostModeState st;
    oked::wm::AddMenuBar(st.panels, &st, &ed);
    CHECK(st.panels.menu);
    auto frame = [&] { be.now += 16; return loop.Frame([&] { oked::DrawHostModeUi(st); }); };
    auto key = [&](plat::Key k, int mods) {
        plat::Event e; e.type = plat::EventType::KeyDown; e.key = k; e.mods = mods; be.queue.push_back(e);
        plat::Event u; u.type = plat::EventType::KeyUp; u.key = k; u.mods = mods; be.queue.push_back(u);
        frame(); frame();
    };
    for (int i = 0; i < 4; ++i) CHECK(frame());
    const oked::wm::MenuProbe& mp = oked::wm::GetMenuProbe();
    // the bar: six top menus along the top strip, the dock below it
    CHECK(mp.topMenu[0].w > 20.0f && mp.topMenu[1].x > mp.topMenu[0].x && mp.topMenu[5].x > mp.topMenu[3].x && mp.topMenu[0].y < oked::wm::MenuBarHeight());
    CHECK(UI::GetDrawData().dropped == 0);
    // opening Edit draws its entries (the model's: Undo, Redo, Cut, ... Deselect, themes, Project Settings)
    {
        const int cx = (int)(mp.topMenu[1].x + mp.topMenu[1].w / 2), cy = (int)(mp.topMenu[1].y + mp.topMenu[1].h / 2);
        be.queue.push_back(Move(cx, cy)); frame(); be.queue.push_back(Btn(true, cx, cy)); frame(); be.queue.push_back(Btn(false, cx, cy)); frame(); frame();
        CHECK(mp.itemsDrawn >= 10);
        if (const char* shot = std::getenv("OKAYUI_SHOT")) { std::vector<std::uint8_t> px; int w = 0, h = 0; if (painter->ReadPixels(px, w, h)) okay::ImageWrite::WritePNG(shot, w, h, 4, px.data()); }
        be.queue.push_back(Move(500, 400)); frame(); be.queue.push_back(Btn(true, 500, 400)); frame(); be.queue.push_back(Btn(false, 500, 400)); frame(); frame();
    }
    // actions on the real editor state
    const std::size_t n0 = ed.scene().Objects().size();
    CHECK(oked::wm::RunMenuAction(ed, &st, "go.empty") && ed.scene().Objects().size() == n0 + 1 && ed.selected() != nullptr);
    CHECK(oked::wm::RunMenuAction(ed, &st, "create.cube") && ed.scene().Objects().size() == n0 + 2);
    okay::GameObject* cube = ed.selected();
    CHECK(oked::wm::RunMenuAction(ed, &st, "go.emptychild") && ed.selected()->transform->Parent() == cube->transform);
    ed.Select(cube);
    {
        std::size_t n = ed.scene().Objects().size();
        CHECK(oked::wm::RunMenuAction(ed, &st, "edit.duplicate") && ed.scene().Objects().size() > n);   // the cube and its child
        n = ed.scene().Objects().size();
        CHECK(oked::wm::RunMenuAction(ed, &st, "edit.copy") && oked::wm::RunMenuAction(ed, &st, "edit.paste") && ed.scene().Objects().size() > n);
        n = ed.scene().Objects().size();
        CHECK(oked::wm::RunMenuAction(ed, &st, "edit.delete") && ed.scene().Objects().size() < n);
        const std::size_t afterDelete = ed.scene().Objects().size();
        CHECK(oked::wm::RunMenuAction(ed, &st, "edit.undo") && ed.scene().Objects().size() > afterDelete);
        CHECK(oked::wm::RunMenuAction(ed, &st, "edit.redo") && ed.scene().Objects().size() == afterDelete);
    }
    CHECK(oked::wm::RunMenuAction(ed, &st, "edit.selectall") && ed.MultiSelection().size() == ed.scene().Objects().size());
    CHECK(oked::wm::RunMenuAction(ed, &st, "edit.deselect") && ed.selected() == nullptr);
    // View > Panels flips the host's open flag; a panel window mode lacks is refused and logged
    CHECK(st.IsOpen(oked::kPanelConsole));
    CHECK(oked::wm::RunMenuAction(ed, &st, "panel.console") && !st.IsOpen(oked::kPanelConsole));
    CHECK(oked::wm::RunMenuAction(ed, &st, "panel.console") && st.IsOpen(oked::kPanelConsole));
    oked::g_log.clear();
    CHECK(!oked::wm::RunMenuAction(ed, &st, "file.build") && !oked::g_log.empty() && oked::g_log.back().find("window mode") != std::string::npos);
    CHECK(!oked::wm::RunMenuAction(ed, &st, "panel.profiler"));
    // File > Exit asks the loop to stop
    CHECK(!st.quit && oked::wm::RunMenuAction(ed, &st, "file.exit") && st.quit); st.quit = false;
    // File > Save writes the scene to the project folder
    {
        const std::string dir = (std::filesystem::temp_directory_path() / "okayui_hostmenu_proj").string();
        std::error_code ec; std::filesystem::remove_all(dir, ec);
        ed.setProjectDir(dir);
        CHECK(oked::wm::RunMenuAction(ed, &st, "file.save") && !ed.path().empty() && std::filesystem::exists(ed.path()));
        std::filesystem::remove_all(dir, ec);
    }
    // the shortcuts, through real key events
    ed.Select(nullptr);
    const std::size_t n1 = ed.scene().Objects().size();
    ed.Select(ed.CreateEmpty()); frame();
    CHECK(ed.scene().Objects().size() == n1 + 1);
    key(plat::Key::Z, plat::ModCtrl);
    CHECK(ed.scene().Objects().size() == n1 && mp.lastAction == "edit.undo");
    key(plat::Key::Y, plat::ModCtrl);
    CHECK(ed.scene().Objects().size() == n1 + 1 && mp.lastAction == "edit.redo");
    key(plat::Key::D, plat::ModCtrl);
    CHECK(ed.scene().Objects().size() == n1 + 2 && mp.lastAction == "edit.duplicate");
    key(plat::Key::Delete, 0);
    CHECK(ed.scene().Objects().size() == n1 + 1 && mp.lastAction == "edit.delete");
    key(plat::Key::A, plat::ModCtrl);
    CHECK(mp.lastAction == "edit.selectall" && ed.MultiSelection().size() == ed.scene().Objects().size());
    // a plain Z (no Ctrl) does nothing
    oked::wm::RunMenuAction(ed, &st, "edit.deselect"); const std::string before = mp.lastAction;
    key(plat::Key::Z, 0);
    CHECK(mp.lastAction == before);
    TEST_MAIN_RESULT();
}
