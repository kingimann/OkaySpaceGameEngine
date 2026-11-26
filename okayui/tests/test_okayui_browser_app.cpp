// Browser: the whole browser (shared/BrowserApp.hpp), the one the browser program draws over its window and the launcher's
// Browser page (sidebar > Browser) draws in its page area. Driven headless at an offset rectangle, as the launcher places it:
// three tabs with pages delivered by the test (no network), the golden picture of the launcher's page area with them, a click
// on another tab, Ctrl+T / Ctrl+W, Ctrl+D (a bookmark) and Ctrl+= (zoom) kept in the profile, a second browser on the same
// profile file seeing them (the launcher and the program share one profile.json), an answer that is a file opening the "Save
// this file?" prompt (Esc closes it), and the switch off (nothing is loaded, a line says why). Round 2 of the look: the address
// bar's suggestions (Down + Enter switches to an open tab), key scrolling that glides (End, Home, Space, Shift+Space), Tab focus
// with a ring and Enter on the focused link, the error page, dragging a tab; goldens of each. OKAY_SHOT_DIR=<folder> also
// writes pictures for a person to look at (never committed).
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "BrowserApp.hpp"
#include "BrowserView.hpp"

#include <cstdlib>
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

        auto tap = [&](UI::KeyId k, bool shift = false) {
            sim.shift = shift; sim.Hold(k, true); sim.Frame(ui); sim.Hold(k, false); sim.Frame(ui); sim.shift = false;
        };
        auto golden = [&](const char* name) {
            std::string why;
            const bool same = MatchesGolden(name, sim.Draw(18, 20, 26), &why);
            if (!same) std::cerr << "  golden " << name << ": " << why << "\n";
            CHECK(same);
        };
        sim.Move(5, 5);   // the pointer away from the browser (no hover, no tooltip)

        // ---- round 2: the address bar suggests while typing (the open tab "Example Domain" for "exam"); Down picks it, Enter
        // switches to that tab instead of searching ----
        chord(UI::KeyId_L);
        sim.Frame(ui);
        sim.Type("exam");
        sim.Frames(3, ui);
        golden("browser_app_suggest");
        tap(UI::KeyId_Down);
        sim.Press(Key_Enter);
        sim.Frames(2, ui);
        CHECK(app.ActiveId() == b);
        CHECK(app.TabById(a)->Url() == "https://notes.okayspace.test/notes.html");   // nothing was searched in the tab typed in

        // ---- round 2: the keyboard scrolls the page (End, Home, Space glide there) and Tab moves between the links; Enter
        // follows the focused one ----
        const int d = app.OpenUrl("https://long.example.test/");
        std::string longPage = "<html><head><title>Long</title></head><body style='margin:0'><p><a href='/one'>one</a> <a href='/two'>two</a></p>";
        for (int i = 0; i < 80; ++i) longPage += "<p>Line " + std::to_string(i) + "</p>";
        Deliver(app, d, longPage + "</body></html>");
        sim.Frames(3, ui);
        br::BrowserTab& lt = *app.TabById(d);
        CHECK(lt.GetState() == br::BrowserTab::State::Ready && lt.ScrollY() == 0.0f);
        tap(UI::KeyId_End);
        sim.Frames(30, ui);
        const float bottom = lt.ScrollY();
        CHECK(bottom > 1000.0f);
        tap(UI::KeyId_Home);
        sim.Frames(30, ui);
        CHECK(lt.ScrollY() == 0.0f);
        tap(UI::KeyId_Space);
        sim.Frame(ui);
        const float gliding = lt.ScrollY();
        sim.Frames(30, ui);
        CHECK(lt.ScrollY() > 200.0f && lt.ScrollY() < bottom && gliding > 0.0f && gliding < lt.ScrollY());   // it glides there
        tap(UI::KeyId_Space, true);   // Shift+Space: back up
        sim.Frames(30, ui);
        CHECK(lt.ScrollY() == 0.0f);
        tap(UI::KeyId_Tab);
        tap(UI::KeyId_Tab);
        golden("browser_app_focus");
        sim.Hold(UI::KeyId_Enter, true); sim.Frame(ui); sim.Hold(UI::KeyId_Enter, false);
        CHECK(lt.Url() == "https://long.example.test/two");
        for (const std::string& u : lt.TakeFetches()) lt.DeliverPage(u, true, 200, "<p>two</p>");   // answered here: no network
        sim.Frame(ui);
        tap(UI::KeyId_Tab, true);   // Shift+Tab on the next page (no links yet: nothing happens, nothing breaks)

        // ---- round 2: a site that cannot be reached shows a friendly page that says so, with Try again ----
        const int e = app.OpenUrl("https://down.example.test/");
        if (br::BrowserTab* et = app.TabById(e))
            for (const std::string& u : et->TakeFetches()) et->DeliverPage(u, false, 0, std::string(), "Could not reach the server. Check your connection and try again.");
        sim.Frames(3, ui);
        CHECK(app.ActiveTab().GetState() == br::BrowserTab::State::Error);
        golden("browser_app_error");

        // ---- round 2: dragging the last tab to the front of the strip reorders the tabs ----
        {
            const std::vector<int> before = app.TabIds();
            const float n = (float)before.size(), tw = std::max(48.0f, std::min(240.0f, (W - 60.0f) / n));
            sim.Move(X + 8 + tw * (n - 1.0f) + tw * 0.3f, Y + 22);
            sim.Frame(ui);
            sim.Down();
            sim.Frame(ui);
            for (float mx = X + 8 + tw * (n - 1.0f); mx > X + 10; mx -= 25.0f) { sim.Move(mx, Y + 22); sim.Frame(ui); }
            sim.Up();
            sim.Frame(ui);
            CHECK(app.TabIds().size() == before.size() && app.TabIds().front() == before.back());
            CHECK(app.ActiveId() == before.back());
        }
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
    // OKAY_SHOT_DIR set: pictures of the browser for a person to look at (software render, not a real window; never committed)
    if (const char* shots = std::getenv("OKAY_SHOT_DIR"); shots && *shots) {
        Reset();
        UI::UseLauncherLook(true);
        UI::Style().caretBlink = false;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        const float SW = 1100, SH = 680;
        Sim ss(SW, SH);
        auto shot = [&](const std::string& name) {
            const std::string path = std::string(shots) + "/" + name + ".png";
            std::cerr << "  picture: " << path << (WritePng(path, ss.Draw(18, 20, 26)) ? "" : " (not written)") << "\n";
        };
        br::BrowserApp app(cfg);
        auto ui = [&] { app.Frame(0, 0, SW, SH); };
        const int a = app.OpenUrl("https://notes.okayspace.test/notes.html");
        const int b = app.OpenUrl("https://example.test/", false);
        const int c = app.OpenUrl("http://plain.example.test/", false);
        Deliver(app, a, kNotes);
        Deliver(app, b, "<html><head><title>Example Domain</title></head><body><h1>Example Domain</h1><p>For examples.</p></body></html>");
        if (br::BrowserTab* t = app.TabById(c)) for (const std::string& u : t->TakeFetches()) t->DeliverPage(u, false, 0, std::string(), "Could not reach the server. Check your connection and try again.");
        ss.Frames(4, ui);
        shot("1_page");
        {   // the pointer over the link "Read more": the address bubble at the bottom-left
            const unsigned before = PixelAt(ss.Draw(18, 20, 26), 6, (int)SH - 8);
            const br::LayoutResult& lay = app.ActiveTab().Layout(SW - 16, okayuiscreens::OkayUIMeasure());
            const float lx = lay.areas.empty() ? 200.0f : lay.areas[0].x + lay.areas[0].w * 0.5f;
            for (float y = 40; y < 300; y += 3) {
                ss.Move(lx, y); ss.Frame(ui);
                if (PixelAt(ss.Draw(18, 20, 26), 6, (int)SH - 8) != before) break;
            }
        }
        shot("2_hover");
        ss.ctrl = true; ss.Hold(UI::KeyId_T, true); ss.Frame(ui); ss.Hold(UI::KeyId_T, false); ss.Frame(ui); ss.ctrl = false;
        ss.Frames(3, ui);
        shot("4_newtab");
        ss.Type("exa"); ss.Frames(2, ui);
        shot("5_typing");
        ss.Hold(UI::KeyId_Escape, true); ss.Frame(ui); ss.Hold(UI::KeyId_Escape, false); ss.Frame(ui);
        ss.ctrl = true; ss.Hold(UI::KeyId_3, true); ss.Frame(ui); ss.Hold(UI::KeyId_3, false); ss.Frame(ui); ss.ctrl = false;
        ss.Frames(2, ui);
        shot("6_error");
        ss.Move(500, 20); ss.Frames(40, ui);
        shot("7_tab_hover");
        app.CloseAll();
    }
    fs::remove_all(dir, ec);
    UI::UseLauncherLook(false);
    TEST_MAIN_RESULT();
}
