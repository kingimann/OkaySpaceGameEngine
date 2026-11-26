// OkaySpace Browser: the browser program (system "Browser", docs/feature_map/17-launcher-platform.md 17.18; plan:
// docs/platform/browser_plan.md). A window of its own with the Browser's page engine (okay/Browser/), the OkayUI view
// with an address field (okayui/screens/BrowserView.hpp) and the shared tab host (shared/BrowserHost.hpp): pages, links,
// back / forward, and OkaySpace web games on a page run natively by the player (step 2), each site in its own folder.
// Round 1 of "a regular browser" (owner, 2026-11-25): tabs, a history of every visit, bookmarks (a bar and a page), downloads
// (asked first, saved to the Downloads folder, a list), an address bar that also searches (Google by default, owner
// 2026-11-26; a setting), zoom per site, find on the page, a context menu, a home / new-tab page, Stop, and the shortcuts of
// common browsers. What lasts is kept in the person's settings folder (okay/Browser/Profile.hpp: profile.json).
//
//   okay-browser [ADDRESS ...] [--play]
//
// ADDRESS: pages to open, one tab each (http or https; "example.com" is read as https://example.com, words are a search).
// --play: once the first page is loaded, start its first game (a desktop shortcut to a game's page). Settings >
// Experimental > "Browser" (OKAY_BROWSER=0) switches our own browser off; the window then says so and loads nothing.
// The window, events and drawing go through okay::platform (window mode of OkayUI: shared/OkayUIFrameLoop.hpp); no Dear ImGui.
// The browser itself (tabs, toolbar, menus, find, downloads, games, the profile) is shared/BrowserApp.hpp, the same one the
// launcher's Browser page draws (sidebar > Browser); this file is only the program's window around it.
#define SDL_MAIN_HANDLED
#define OKAYUI_HOST_PLATFORM 1
#include "OkayUIFrameLoop.hpp"
#include "BrowserApp.hpp"
#include "Process.hpp"   // tools/okay-cli: SelfPath (where this program is, to find the player beside it)
#include "okay/Platform/Painter.hpp"
#include "okay/Platform/Platform.hpp"
#include "okay/UI/OkayUI.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace br = okay::browser;
namespace UI = OkayUI;
namespace fs = std::filesystem;

namespace {

// The player that runs games: beside this program, or in its Tools folder (the release layout, docs/release/packaging.md).
std::string FindPlayer() {
    const fs::path self = fs::u8path(okaycli::SelfPath());
    if (self.empty()) return std::string();
    std::error_code ec;
    for (const char* dir : {"", "Tools"})
        for (const char* name : {"OkaySpacePlayer.exe", "okay-player"}) {
            const fs::path p = dir[0] ? self.parent_path() / dir / name : self.parent_path() / name;
            if (fs::is_regular_file(p, ec)) return p.u8string();
        }
    return std::string();
}

bool SwitchedOff() {
    const char* e = std::getenv("OKAY_BROWSER");   // Settings > Experimental > "Browser" (shared/OwnSwitches.hpp); unset = on
    return e && e[0] == '0';
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> starts;
    bool autoplay = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--help" || a == "-h") {
            std::printf("Usage: %s [ADDRESS ...] [--play]\n  ADDRESS  pages to open, one tab each (an address or words to search)\n"
                        "  --play   start the first page's first game once it is loaded\n", argv[0]);
            return 0;
        }
        if (a == "--play") autoplay = true;
        else if (!a.empty() && a[0] != '-') starts.push_back(a);
    }

    std::unique_ptr<br::BrowserApp> app;
    okayuihost::platform::ProgramDesc desc;
    desc.tag = "browser";
    desc.window.title = "OkaySpace Browser";
    desc.window.width = 1180;
    desc.window.height = 800;
    desc.window.resizable = true;
    okayuihost::platform::ProgramHooks hooks;
    hooks.start = [&](okay::platform::Backend& be, okay::platform::Painter& painter) {
        br::BrowserApp::Config cfg;
        okay::platform::Painter* p = &painter;
        cfg.textures.make = [p](const std::uint8_t* rgba, int w, int h) -> void* {
            okay::platform::TextureDesc d;
            d.width = w; d.height = h; d.scale = okay::platform::ScaleMode::Linear;
            return p->CreateTexture(d, rgba);
        };
        cfg.textures.destroy = [p](void* t) { p->DestroyTexture(t); };
        cfg.runner.dataRoot = be.PrefPath("OkaySpace", "Browser");   // the launcher's Browser page uses the same folder and profile
        cfg.runner.playerExe = FindPlayer();
        cfg.profilePath = cfg.runner.dataRoot.empty() ? std::string() : cfg.runner.dataRoot + "profile.json";
        cfg.switchedOff = SwitchedOff();
        cfg.quitItem = true;
        std::fprintf(stderr, "[browser] data %s, player %s\n", cfg.runner.dataRoot.c_str(), cfg.runner.playerExe.empty() ? "(not found: games cannot run)" : cfg.runner.playerExe.c_str());
        app.reset(new br::BrowserApp(cfg));
        for (const std::string& s : starts)
            if (app->OpenTyped(s, app->TabCount() == 0) < 0) app->OpenUrl("about:newtab", app->TabCount() == 0);
        if (app->TabCount() == 0) app->OpenTyped(std::string());
        app->SetAutoplay(autoplay);
        UI::UseLauncherLook(true);
        return true;
    };
    hooks.frame = [&](float w, float h) { app->Frame(0, 0, w, h); };
    hooks.wantsQuit = [&] { return app->WantsQuit(); };
    hooks.title = [&] { return app->WindowTitle(); };   // the window's title: the page's title
    hooks.stop = [&] { app.reset(); };   // saves the profile, stops running games and frees the textures while the painter still exists
    return okayuihost::platform::RunProgram(desc, hooks);
}
