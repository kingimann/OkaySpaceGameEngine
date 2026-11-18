// Run, not just compile: the Project panel pieces that need an asset database (editor/ProjectPanelUI.cpp) over a REAL okaypb::Browser on a temporary
// project folder: the delete confirmation (it lists what uses the assets), the references window ("used by" / "uses"), the broken-references list, the
// "move to" folder picker, the Properties window and the import settings editor. The Browser and the asset database are the real ones (hand-linked
// engine sources); the files are written by the test. Buttons are found by their colour in the rendered picture and clicked with synthetic input.
// See docs/own_ui_toolkit.md 6.17.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "ProjectPanelUI.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>

namespace fs = std::filesystem;
using namespace okayuitest;
namespace UI = OkayUI;
namespace pp = oked::projectpanel;

static void Golden(const char* name, const Bitmap& b) {
    std::string why; const bool ok = MatchesGolden(name, b, &why);
    if (!ok) std::cerr << "  golden " << name << ": " << why << "\n";
    CHECK(ok);
}
// The centre of the first pixel run of exactly this colour (a button's fill), or (-1, -1).
static bool FindColour(const Bitmap& b, unsigned rgba, float& cx, float& cy) {
    int x0 = -1, y0 = -1, x1 = -1, y1 = -1;
    for (int y = 0; y < b.h; ++y) for (int x = 0; x < b.w; ++x) if (PixelAt(b, x, y) == rgba) { if (x0 < 0 || x < x0) x0 = x; if (x1 < x) x1 = x; if (y0 < 0) y0 = y; if (y1 < y) y1 = y; }
    if (x0 < 0) return false;
    cx = (x0 + x1) * 0.5f; cy = (y0 + y1) * 0.5f; return true;
}
static void Write(const fs::path& p, const std::string& text) { fs::create_directories(p.parent_path()); std::ofstream(p, std::ios::binary) << text; }

int main() {
    RUN_SUITE("port probe: Project panel dialogs over a real asset database");
    const fs::path proj = fs::temp_directory_path() / "okayui_port_project2_test";
    std::error_code ec; fs::remove_all(proj, ec);
    const fs::path assets = proj / "Assets";
    Write(assets / "Materials" / "Hero.okaymat", "name = Hero\n");
    Write(assets / "Scenes" / "Main.okayscene", "scene Main\nobject Hero material = \"Assets/Materials/Hero.okaymat\"\nobject Gone texture = \"Assets/Textures/missing.png\"\n");
    Write(assets / "Scenes" / "Other.okayscene", "scene Other\nobject Hero material = \"Assets/Materials/Hero.okaymat\"\n");
    okaypb::Browser browser(assets.generic_string());
    CHECK(browser.Sync());
    const std::string hero = (assets / "Materials" / "Hero.okaymat").generic_string();
    CHECK(browser.UsersOf(hero) == 2);

    Reset(); Sim s(720, 520);
    auto ui = [&] { UI::BareNextWindow(); if (UI::Begin("Project", 0, 0, 720, 520)) pp::DrawDialogs(); UI::End(); };

    {   // delete confirmation: lists the two scenes that use the asset; Cancel keeps; Delete calls the callback
        int confirmed = 0;
        pp::AskDelete(browser, {hero}, [&] { ++confirmed; });
        s.Frames(4, ui);
        const Bitmap b = s.Draw(26, 28, 34);
        CHECK(CountNot(b, 0, 0, 720, 520, Rgba(26, 28, 34)) > 4000);
        Golden("port_project_delete", b);
        s.Press(Key_Escape); s.Frames(3, ui);
        CHECK(confirmed == 0);
        pp::AskDelete(browser, {hero}, [&] { ++confirmed; });
        s.Frames(4, ui);
        float cx, cy;
        const bool found = FindColour(s.Draw(26, 28, 34), Rgba(166, 56, 56), cx, cy);   // the red Delete button (0.65, 0.22, 0.22)
        CHECK(found);
        if (found) { s.Click(cx, cy, ui); s.Frames(2, ui); }
        CHECK(confirmed == 1);
    }
    {   // references window: both lists; a click on a line calls onPick (a line is found as the first "link" row: click the middle of the list area)
        std::string picked;
        pp::ShowReferences(browser, hero, [&](const std::string& p) { picked = p; }, pp::RefMode::Both);
        s.Frames(4, ui);
        const Bitmap b = s.Draw(26, 28, 34);
        CHECK(CountNot(b, 0, 0, 720, 520, Rgba(26, 28, 34)) > 4000);
        Golden("port_project_references", b);
        for (float y = 150; y < 420; y += 18) { s.Move(260, y); s.Frame(ui); s.Down(); s.Frame(ui); s.Up(); s.Frame(ui); }
        s.Press(Key_Escape); s.Frames(3, ui);
        pp::ShowReferences(browser, (assets / "Scenes" / "Main.okayscene").generic_string(), [](const std::string&) {}, pp::RefMode::Uses); s.Frames(3, ui);
        s.Press(Key_Escape); s.Frames(3, ui);
    }
    {   // broken references: Main.okayscene points at a texture that is not there
        pp::ShowReferenceProblems(browser, [](const std::string&) {}, [](const okaypb::Result&) {});
        s.Frames(4, ui);
        const Bitmap b = s.Draw(26, 28, 34);
        CHECK(CountNot(b, 0, 0, 720, 520, Rgba(26, 28, 34)) > 3000);
        Golden("port_project_broken", b);
        s.Press(Key_Escape); s.Frames(3, ui);
    }
    {   // "move to" folder picker
        std::string dest;
        pp::AskFolder(browser, {hero}, "", [&](const std::string& d) { dest = d; });
        s.Frames(4, ui);
        const Bitmap b = s.Draw(26, 28, 34);
        CHECK(CountNot(b, 0, 0, 720, 520, Rgba(26, 28, 34)) > 3000);
        Golden("port_project_movepicker", b);
        s.Press(Key_Escape); s.Frames(3, ui);
        CHECK(dest.empty());
    }
    {   // Properties of the material, and the import settings editor (a material has none: nothing drawn)
        pp::PropertyExtras extras; extras.favourite = true;
        pp::ShowProperties(browser, hero, extras);
        s.Frames(4, ui);
        const Bitmap b = s.Draw(26, 28, 34);
        CHECK(CountNot(b, 0, 0, 720, 520, Rgba(26, 28, 34)) > 3000);
        Golden("port_project_propertieswindow", b);
        s.Press(Key_Escape); s.Frames(3, ui);
        CHECK(!pp::HasImportSettings(browser, hero));
        Sim s2(720, 520);
        s2.Frame([&] { UI::BareNextWindow(); if (UI::Begin("Import", 0, 0, 720, 520)) { pp::DrawImportSettings(browser, hero); } UI::End(); });
    }
    {   // hostile input: a path that is not in the project, no paths, an empty picker
        pp::AskDelete(browser, {}, [] {}); s.Frames(3, ui); s.Press(Key_Escape); s.Frames(3, ui);
        pp::ShowReferences(browser, "/nowhere/x.png", [](const std::string&) {}); s.Frames(3, ui); s.Press(Key_Escape); s.Frames(3, ui);
        pp::AskFolder(browser, {}, "", [](const std::string&) {}); s.Frames(3, ui); s.Press(Key_Escape); s.Frames(3, ui);
        pp::ShowProperties(browser, "/nowhere/x.png", pp::PropertyExtras{}); s.Frames(3, ui); s.Press(Key_Escape); s.Frames(3, ui);
        CHECK(!pp::HasImportSettings(browser, "/nowhere/x.png"));
        s.blocked = true; s.Frame(ui); s.blocked = false;
    }
    fs::remove_all(proj, ec);
    TEST_MAIN_RESULT();
}
