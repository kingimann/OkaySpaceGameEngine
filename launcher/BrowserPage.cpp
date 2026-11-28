// Browser: the launcher's Browser page (see BrowserPage.hpp). The browser itself is shared/BrowserApp.hpp, the same one the
// browser program draws; this file is only the launcher's side: its textures (SDL renderer), where the profile and the games
// live (the browser program's own settings folder, so both use one profile.json), the player beside the launcher, and the rule
// for which links open here.
#include "LauncherShared.hpp"
#include "BrowserPage.hpp"
#include "PainterBridge.hpp"
#include "okay/Platform/Platform.hpp"
#include "BrowserApp.hpp"

#include <cstdlib>
#include <memory>
#include <string>

namespace okl {

namespace {

bool g_hostAvailable = false;
bool g_showRequest = false;

namespace br = okay::browser;

std::unique_ptr<br::BrowserApp> g_app;

// The browser, made on first use (the renderer exists then). Its data folder and profile are the browser program's
// (PrefPath("OkaySpace", "Browser")), so history, bookmarks, zoom and settings are the same in both.
br::BrowserApp& App() {
    if (!g_app) {
        br::BrowserApp::Config cfg;
        cfg.textures.make = [](const std::uint8_t* rgba, int w, int h) -> void* { return TextureFromRgba(g_renderer, rgba, w, h); };
        cfg.textures.destroy = [](void* t) { okay::pb::DestroyTexture(g_renderer, (okay::ui2d::Tex)t); };
        okay::platform::Backend* be = okay::platform::Default();
        cfg.runner.dataRoot = be ? be->PrefPath("OkaySpace", "Browser") : std::string();
        cfg.runner.playerExe = FindExe({"OkaySpacePlayer.exe", "okay-player.exe", "okay-player"});   // games run in the player, as in the program
        cfg.profilePath = cfg.runner.dataRoot.empty() ? std::string() : cfg.runner.dataRoot + "profile.json";
        cfg.idPrefix = "##launcher_browser";
        g_app.reset(new br::BrowserApp(cfg));
        if (const char* demo = std::getenv("OKAY_UI_DEMO_BROWSER")) {   // dev screenshot hook: addresses to open, one tab each
            std::string list = demo, one;
            for (std::size_t i = 0; i <= list.size(); ++i) {
                if (i < list.size() && list[i] != ' ') { one += list[i]; continue; }
                if (!one.empty()) g_app->OpenTyped(one, g_app->TabCount() == 0);
                one.clear();
            }
        }
    }
    return *g_app;
}

} // namespace

bool BrowserSwitchOn() { return BrowserSwitchValueOn(std::getenv("OKAY_BROWSER")); }

bool OpenPageLink(const std::string& url) {
    if (RouteLink(url, BrowserSwitchOn(), g_hostAvailable) == LinkTarget::BrowserPage) {
        if (App().OpenUrl(url, true) >= 0) { g_showRequest = true; return true; }
    }
    OpenExternal(url);   // OpenExternal checks the address is shell-safe
    return false;
}

bool TakeShowRequest() {
    const bool r = g_showRequest;
    g_showRequest = false;
    return r;
}

void SetBrowserHostAvailable(bool okayFrame) { g_hostAvailable = okayFrame; }

void PumpBrowser() {
    if (g_app) g_app->Pump();
}

void DrawBrowserPage(float x, float y, float w, float h) {
    if (!g_hostAvailable || !BrowserSwitchOn()) return;
    br::BrowserApp& app = App();
    if (app.TabCount() == 0) app.OpenTyped(std::string());   // the home page (the new-tab page unless the settings name one)
    app.Frame(x, y, w, h);
}

void ShutdownBrowser() {
    g_app.reset();   // saves the profile, stops running games and frees the textures while the renderer still exists
}

} // namespace okl
