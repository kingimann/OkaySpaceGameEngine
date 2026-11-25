// Browser: the launcher's page for our own web pages (see BrowserPage.hpp). Fetching, images and the tab's state are the
// Browser's shared host (shared/BrowserHost.hpp, also used by the browser program); this file is only the launcher's rule for
// which links open here, its SDL textures and its toolbar actions.
#include "LauncherShared.hpp"
#include "BrowserPage.hpp"
#include "BrowserHost.hpp"
#include "PainterBridge.hpp"
#include "okay/Browser/Browser.hpp"
#include "okay/Platform/Account/AccountService.hpp"
#ifdef OKAY_HAVE_OKAYUI
#include "BrowserView.hpp"
#endif

#include <cstdlib>
#include <memory>
#include <string>

namespace okl {

namespace {

namespace br = okay::browser;

bool g_open = false;
bool g_hostAvailable = false;

bool SwitchOn() {
    const char* e = std::getenv("OKAY_BROWSER");   // Settings > Experimental > "Browser" sets it (shared/OwnSwitches.hpp)
    return e && e[0] == '1';
}

// The tab and its fetches; images become textures on the launcher's renderer. Made on first use (the renderer exists then).
br::TabHost& Host() {
    static std::unique_ptr<br::TabHost> host;
    if (!host) {
        br::TabHost::TextureFns fns;
        fns.make = [](const std::uint8_t* rgba, int w, int h) -> void* { return TextureFromRgba(g_renderer, rgba, w, h); };
        fns.destroy = [](void* t) { okay::pb::DestroyTexture(g_renderer, (SDL_Texture*)t); };
        host.reset(new br::TabHost(fns, "OkaySpace Browser"));
    }
    return *host;
}

} // namespace

bool OpenPageLink(const std::string& url) {
    const std::string server = g_presenceAccount ? g_presenceAccount->ServerUrl() : std::string();
    if (!SwitchOn() || !g_hostAvailable || !IsServerPage(url, server)) { OpenExternal(url); return false; }
    if (!Host().Open(url)) { OpenExternal(url); return false; }
    g_open = true;
    return true;
}

bool BrowserPageOpen() { return g_open; }

void CloseBrowserPage() {
    if (!g_open) return;
    g_open = false;
    Host().Reset();
}

void SetBrowserHostAvailable(bool okayFrame) {
    g_hostAvailable = okayFrame;
    if (!okayFrame && g_open) CloseBrowserPage();   // the OkayUI screens were switched off: nothing can draw the page
}

void DrawBrowserPage(float x, float y, float w, float h) {
#ifdef OKAY_HAVE_OKAYUI
    if (!g_open) return;
    br::TabHost& host = Host();
    br::BrowserTab& tab = host.Tab();
    host.Pump();
    const okayuiscreens::BrowserViewResult r = okayuiscreens::DrawBrowserView(tab, x, y, w, h, [&host](const std::string& src) {
        return (SDL_Texture*)host.ImageFor(src);
    });
    if (r.back) tab.GoBack();
    if (r.forward) tab.GoForward();
    if (r.reload) tab.Reload();
    if (r.openInSystemBrowser && !tab.Url().empty()) OpenExternal(tab.Url());
    if (r.linkClicked >= 0) {
        const br::LayoutResult& page = tab.Layout(r.page.w, okayuiscreens::OkayUIMeasure());
        if (r.linkClicked < (int)page.links.size()) {
            const std::string href = page.links[(std::size_t)r.linkClicked];
            const br::BrowserTab::Action a = tab.Activate(href);
            if (a.kind == br::BrowserTab::Action::External) OpenExternal(a.url);   // OpenExternal checks the address is shell-safe
            else if (a.kind == br::BrowserTab::Action::Refused) Toast("That link can't be opened", 2);
        }
    }
    if (r.close) { CloseBrowserPage(); return; }
    host.Pump();
#else
    (void)x; (void)y; (void)w; (void)h;
#endif
}

} // namespace okl
