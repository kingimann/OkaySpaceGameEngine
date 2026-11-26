// Browser: the whole browser (shared/BrowserApp.hpp), the one the browser program draws over its window and the launcher's
// Browser page (sidebar > Browser) draws in its page area. Driven headless at an offset rectangle, as the launcher places it:
// three tabs with pages delivered by the test (no network), the golden picture of the launcher's page area with them, a click
// on another tab, Ctrl+T / Ctrl+W, Ctrl+D (a bookmark) and Ctrl+= (zoom) kept in the profile, a second browser on the same
// profile file seeing them (the launcher and the program share one profile.json), an answer that is a file opening the "Save
// this file?" prompt (Esc closes it), and the switch off (nothing is loaded, a line says why).
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "BrowserApp.hpp"

#include <filesystem>
#include <string>

using namespace okayuitest;
namespace UI = OkayUI;
namespace br = okay::browser;
namespace fs = std::filesystem;

static const char* kNotes =
    "<!DOCTYPE html><html><head><title>Patch notes</title><style>"
    "body { margin: 16px; background: #f4f6fa; color: #1c2230; font-family: sans-serif }"
    "h1 { color: #3a7bd5; margin: 0 0 8px 0 } .card { border: 1px solid #c8d0de; background: #fff; padding: 10px; margin: 8px 0 }"
    ".row { display: flex; gap: 10px } .row div { flex: 1; background: #e3ebf8; padding: 8px }"
    "</style></head><body>"
    "<h1>OkaySpace 0.9 patch notes</h1>"
    "<p>The launcher has a <b>Browser</b> page now. <a href='/more.html'>Read more</a> about it.</p>"
    "<div class=row><div>Tabs</div><div>Bookmarks</div><div>Downloads</div></div>"
    "<div class=card>Games on a page run natively with the OkaySpace player.</div>"
    "<ul><li>Google search from the address bar</li><li>Find on the page, zoom per site</li></ul>"
    "</body></html>";

// The tab's waiting fetch answered by the test (the page engine's own entry point; no worker, no network).
static void Deliver(br::BrowserApp& app, int id, const std::string& html) {
    br::BrowserTab* t = app.TabById(id);
    if (!t) return;
    for (const std::string& u : t->TakeFetches()) t->DeliverPage(u, true, 200, html);
}

int main() {
    RUN_SUITE("okayui browser app (the launcher's Browser page and the browser program)");
    Reset();
    UI::UseLauncherLook(true);
    UI::Style().caretBlink = false;

    const fs::path dir = fs::temp_directory_path() / "okay_browser_app_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    br::BrowserApp::Config cfg;
    cfg.profilePath = (dir / "profile.json").u8string();
    cfg.runner.dataRoot = dir.u8string() + "/";
    cfg.idPrefix = "##test_browser";

    // the launcher's page area: right of a 180 px sidebar, under a 40 px title row
    const float X = 180, Y = 40, W = 720, H = 480;
    Sim sim(X + W, Y + H);
    {
        br::BrowserApp app(cfg);
        const int a = app.OpenUrl("https://notes.okayspace.test/notes.html");
        const int b = app.OpenUrl("https://example.test/", false);
        const int c = app.OpenUrl("https://games.example.test/index.html", false);
        CHECK(app.TabCount() == 3 && app.ActiveId() == a);
        Deliver(app, a, kNotes);
        Deliver(app, b, "<html><head><title>Example Domain</title></head><body><h1>Example Domain</h1><p>For examples.</p></body></html>");
        Deliver(app, c, "<html><head><title>Space Run - play</title></head><body><okay-game src='space.okaypack.gz' width=300 height=120></okay-game></body></html>");
        auto ui = [&] { app.Frame(X, Y, W, H); };
        sim.Frames(4, ui);
        CHECK(app.ActiveTab().Title() == "Patch notes");
        CHECK(app.WindowTitle().find("OkaySpace Browser") != std::string::npos);
        {
            std::string why;
            const bool same = MatchesGolden("browser_app", sim.Draw(18, 20, 26), &why);
            if (!same) std::cerr << "  golden browser_app: " << why << "\n";
            CHECK(same);
        }

        // a click on the second tab (the tab bar starts at X + 6, three tabs of 220 px)
        sim.Click(X + 6 + 220 + 60, Y + 18, ui);
        CHECK(app.ActiveId() == b);
        // Ctrl+T: a new tab (the new-tab page), Ctrl+W closes it
        auto chord = [&](UI::KeyId k, bool shift = false) {
            sim.ctrl = true; sim.shift = shift; sim.Hold(k, true); sim.Frame(ui);
            sim.Hold(k, false); sim.Frame(ui); sim.ctrl = false; sim.shift = false;
        };
        chord(UI::KeyId_T);
        CHECK(app.TabCount() == 4 && app.ActiveTab().Url() == "about:newtab");
        sim.Frames(2, ui);   // our own new-tab page is made by the host (no network)
        CHECK(app.ActiveTab().GetState() == br::BrowserTab::State::Ready);
        chord(UI::KeyId_W);
        CHECK(app.TabCount() == 3);
        // back to the notes: Ctrl+1; Ctrl+D bookmarks it, Ctrl+= zooms its site
        chord(UI::KeyId_1);
        CHECK(app.ActiveId() == a);
        chord(UI::KeyId_D);
        CHECK(app.GetProfile().bookmarks.Has("https://notes.okayspace.test/notes.html"));
        chord(UI::KeyId_Equal);
        CHECK(app.GetProfile().zoom.Get("https://notes.okayspace.test") > 1.0f);
        app.Save();

        // the other host (the browser program, or the launcher) on the same profile file sees both
        {
            br::BrowserApp other(cfg);
            CHECK(other.GetProfile().bookmarks.Has("https://notes.okayspace.test/notes.html"));
            CHECK(other.GetProfile().zoom.Get("https://notes.okayspace.test") > 1.0f);
        }

        // an answer that is a file: the "Save this file?" prompt, Esc closes it and nothing is downloaded
        br::BrowserTab& t = app.ActiveTab();
        t.Activate("/setup.zip");
        for (const std::string& u : t.TakeFetches()) t.DeliverDownload(u, "application/zip", "setup.zip", 2048);
        sim.Frames(2, ui);
        CHECK(app.DownloadPromptOpen());
        sim.Hold(UI::KeyId_Escape, true); sim.Frame(ui); sim.Hold(UI::KeyId_Escape, false); sim.Frame(ui);
        CHECK(!app.DownloadPromptOpen() && app.Downloads().empty());
        app.CloseAll();
    }

    // the switch off: a tab exists but nothing is fetched, and the line says why
    {
        br::BrowserApp::Config off = cfg;
        off.switchedOff = true;
        br::BrowserApp app(off);
        app.OpenUrl("https://example.test/");
        sim.Frames(2, [&] { app.Frame(X, Y, W, H); });
        CHECK(app.Notice().find("switched off") != std::string::npos);
        CHECK(app.ActiveTab().GetState() == br::BrowserTab::State::Empty);
    }
    fs::remove_all(dir, ec);
    UI::UseLauncherLook(false);
    TEST_MAIN_RESULT();
}
