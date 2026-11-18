// Run, not just compile: the Project panel pieces that need an asset database (editor/ProjectPanelUI.cpp) over a REAL okaypb::Browser on a temporary
// project folder: the delete confirmation (it lists what uses the assets), the references window ("used by" / "uses"), the broken-references list, the
// "move to" folder picker, the Properties window and the import settings editor. The Browser and the asset database are the real ones (hand-linked
// engine sources); the files are written by the test. Buttons are found by their colour in the rendered picture and clicked with synthetic input.
// See docs/own_ui_toolkit.md 6.17.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "ProjectPanelUI.hpp"
#include "okay/Core/ImageWrite.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

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
    {   // a real picture and a real (tiny) WAV file, so the import settings editor has a texture and an audio asset to edit
        fs::create_directories(assets / "Textures", ec);
        std::vector<std::uint8_t> px(16 * 16 * 4);
        for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) { std::uint8_t* q = &px[(size_t)(y * 16 + x) * 4]; q[0] = (std::uint8_t)(x * 16); q[1] = (std::uint8_t)(y * 16); q[2] = 128; q[3] = 255; }
        okay::ImageWrite::WritePNG((assets / "Textures" / "Hero.png").string(), 16, 16, 4, px.data());
        std::string wav = "RIFF"; auto le32 = [&](unsigned v) { for (int i = 0; i < 4; ++i) wav.push_back((char)((v >> (8 * i)) & 255)); }; auto le16 = [&](unsigned v) { wav.push_back((char)(v & 255)); wav.push_back((char)((v >> 8) & 255)); };
        le32(36 + 200); wav += "WAVEfmt "; le32(16); le16(1); le16(1); le32(8000); le32(16000); le16(2); le16(16); wav += "data"; le32(200);
        for (int i = 0; i < 100; ++i) le16((unsigned)(i * 300) & 0xFFFF);
        Write(assets / "Audio" / "Beep.wav", wav);
    }
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
        // not a golden: the window shows modification times relative to now
        s.Press(Key_Escape); s.Frames(3, ui);
        CHECK(!pp::HasImportSettings(browser, hero));
    }
    {   // the import settings editor on a real texture (its combo is drawn; a golden) and a real audio file (a checkbox is found by clicking, then Apply saves it)
        const std::string tex = (assets / "Textures" / "Hero.png").generic_string(), wavp = (assets / "Audio" / "Beep.wav").generic_string();
        CHECK(browser.Sync());
        CHECK(pp::HasImportSettings(browser, tex) && pp::HasImportSettings(browser, wavp));
        Reset(); Sim s3(520, 380);
        bool applied = false;
        auto imp = [&](const std::string& path) { return [&, path] { UI::BareNextWindow(); if (UI::Begin("Import", 0, 0, 520, 380)) { if (pp::DrawImportSettings(browser, path)) applied = true; } UI::End(); }; };
        auto uiTex = imp(tex);
        s3.Frames(4, uiTex);
        const Bitmap b = s3.Draw(26, 28, 34);
        CHECK(CountNot(b, 0, 0, 520, 380, Rgba(26, 28, 34)) > 2000);
        Golden("port_project_import_texture", b);
        // audio: click the left column row by row until the picture changes (the "Mix down" checkbox toggled, which enables Apply and Revert)
        auto uiWav = imp(wavp);
        s3.Frames(4, uiWav);
        okay::ImportSettings before; CHECK(browser.GetImportSettings(wavp, before));
        const Bitmap clean = s3.Draw(26, 28, 34);
        Golden("port_project_import_audio", clean);
        bool toggled = false;
        for (float y = 130; y < 175 && !toggled; y += 4) {   // the checkbox row is below the Loading combo (a click on the combo would open its list)
            s3.Move(12, y); s3.Frame(uiWav); s3.Down(); s3.Frame(uiWav); s3.Up(); s3.Frames(2, uiWav);
            toggled = s3.Draw(26, 28, 34).rgba != clean.rgba;
        }
        CHECK(toggled);
        const Bitmap dirty = s3.Draw(26, 28, 34);
        Golden("port_project_import_audio_edited", dirty);
        // Apply: try the row of buttons under the checkbox until the settings are saved (DrawImportSettings returns true on the frame it applied)
        for (float y = 250; y < 375 && !applied; y += 6) for (float x = 24; x < 120 && !applied; x += 24) { s3.Move(x, y); s3.Frame(uiWav); s3.Down(); s3.Frame(uiWav); s3.Up(); s3.Frames(2, uiWav); }
        okay::ImportSettings after; CHECK(browser.GetImportSettings(wavp, after));
        CHECK(applied && after.audio.forceMono != before.audio.forceMono);
        Sim s4(520, 300); s4.Frames(2, [&] { UI::BareNextWindow(); if (UI::Begin("Import", 0, 0, 520, 300)) { pp::DrawImportSettings(browser, hero); } UI::End(); });   // a material has none: nothing is drawn
    }
    {   // hostile input: a path that is not in the project, no paths, an empty picker
        pp::AskDelete(browser, {}, [] {}); s.Frames(3, ui); s.Press(Key_Escape); s.Frames(3, ui);
        pp::ShowReferences(browser, "/nowhere/x.png", [](const std::string&) {}); s.Frames(3, ui); s.Press(Key_Escape); s.Frames(3, ui);
        pp::AskFolder(browser, {}, "", [](const std::string&) {}); s.Frames(3, ui); s.Press(Key_Escape); s.Frames(3, ui);
        pp::ShowProperties(browser, "/nowhere/x.png", pp::PropertyExtras{}); s.Frames(3, ui); s.Press(Key_Escape); s.Frames(3, ui);
        (void)pp::HasImportSettings(browser, "/nowhere/x.png");   // answers true for an unindexed .png path (it only looks at the extension); not asserted
        s.blocked = true; s.Frame(ui); s.blocked = false;
    }
    fs::remove_all(proj, ec);
    TEST_MAIN_RESULT();
}
