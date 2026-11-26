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
#define SDL_MAIN_HANDLED
#define OKAYUI_HOST_PLATFORM 1
#include "OkayUIFrameLoop.hpp"
#include "BrowserHost.hpp"
#include "BrowserView.hpp"
#include "Process.hpp"   // tools/okay-cli: SelfPath (where this program is, to find the player beside it)
#include "okay/Browser/Profile.hpp"
#include "okay/Browser/Url.hpp"
#include "okay/Core/StringUtil.hpp"
#include "okay/Platform/Painter.hpp"
#include "okay/Platform/Platform.hpp"
#include "okay/UI/OkayUI.hpp"
#include "okay/UI/OkayUICompat.hpp"   // AddFontTtf: web fonts as OkayUI TrueType fonts

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace br = okay::browser;
namespace S = okayuiscreens;
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

// The person's Downloads folder (Windows: %USERPROFILE%\Downloads; elsewhere ~/Downloads), else the program's own one.
std::string DefaultDownloadDir(const std::string& dataRoot) {
    for (const char* var : {"USERPROFILE", "HOME"}) {
        const char* h = std::getenv(var);
        if (!h || !h[0]) continue;
        std::error_code ec;
        const fs::path d = fs::u8path(h) / "Downloads";
        if (fs::is_directory(d, ec)) return d.u8string();
    }
    return dataRoot + "Downloads";
}

bool PathTaken(const std::string& p, void*) { std::error_code ec; return fs::exists(fs::u8path(p), ec); }

UI::Color32 Rgba(int r, int g, int b, int a = 255) { return (UI::Color32)r | ((UI::Color32)g << 8) | ((UI::Color32)b << 16) | ((UI::Color32)a << 24); }

// Text cut to `maxW` px with "..." at the end.
std::string Fit(const std::string& text, float maxW) {
    if (UI::CalcTextWidth(text.c_str()) <= maxW) return text;
    std::string t = text;
    while (!t.empty() && UI::CalcTextWidth((t + "...").c_str()) > maxW) {
        t.pop_back();
        while (!t.empty() && ((unsigned char)t.back() & 0xC0) == 0x80) t.pop_back();
    }
    return t + "...";
}

// One tab: its page host and its address field, and find-on-page results for it.
struct TabView {
    std::unique_ptr<br::TabHost> host;
    S::AddressField field;
    bool focusAddress = false;
    float pageW = 800.0f;                     // the page area's width last frame
    std::vector<br::FindHit> hits;
    std::string hitsKey;                      // what `hits` were found for
    S::ControlEdit edit;                      // the page's form field being typed in
};

// A small menu drawn over everything (the main menu, the context menu, a bookmark's menu).
struct MenuItem {
    std::string label, shortcut;
    int action = 0;          // 0 = a dividing line
    bool enabled = true;
};

enum Action {
    A_None = 0, A_NewTab, A_CloseTab, A_ReopenTab, A_History, A_Bookmarks, A_Downloads, A_ZoomIn, A_ZoomOut, A_ZoomReset, A_Find,
    A_Settings, A_ToggleBar, A_OpenLinkNewTab, A_CopyLink, A_CopyText, A_CopyPage, A_Back, A_Forward, A_Reload, A_Bookmark,
    A_RemoveBookmark, A_OpenBookmarkNewTab, A_Quit
};

struct Menu {
    bool open = false;
    UI::Vec2 at;
    std::vector<MenuItem> items;
    std::string link;        // the context menu's link (resolved), or the bookmark's address
    std::string text;        // the context menu's text under the pointer
    int openedFrame = 0;
};

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

    okay::platform::Backend* be = okay::platform::Default();
    if (!be || !be->Init(okay::platform::InitFlags())) {
        std::fprintf(stderr, "[browser] no platform backend: %s\n", be ? be->LastError().c_str() : "none");
        return 1;
    }
    okay::platform::WindowDesc wd;
    wd.title = "OkaySpace Browser";
    wd.width = 1180;
    wd.height = 800;
    wd.resizable = true;
    std::unique_ptr<okay::platform::Window> win;
    std::unique_ptr<okay::platform::Painter> painter;
    std::string err;
    if (!okayuihost::platform::OpenWindow(*be, wd, win, painter, err)) { std::fprintf(stderr, "[browser] %s\n", err.c_str()); be->Shutdown(); return 1; }

    br::TabHost::TextureFns tex;
    okay::platform::Painter* p = painter.get();
    tex.make = [p](const std::uint8_t* rgba, int w, int h) -> void* {
        okay::platform::TextureDesc d;
        d.width = w; d.height = h; d.scale = okay::platform::ScaleMode::Linear;
        return p->CreateTexture(d, rgba);
    };
    tex.destroy = [p](void* t) { p->DestroyTexture(t); };
    br::TabHost::GameRunner runner;
    runner.dataRoot = be->PrefPath("OkaySpace", "Browser");
    runner.playerExe = FindPlayer();
    std::fprintf(stderr, "[browser] data %s, player %s\n", runner.dataRoot.c_str(), runner.playerExe.empty() ? "(not found: games cannot run)" : runner.playerExe.c_str());

    // ---- what lasts: the profile in the person's own settings folder ----
    br::Profile profile;
    const std::string profilePath = runner.dataRoot + "profile.json";
    {
        std::string why;
        if (!br::LoadProfile(profile, profilePath, &why)) std::fprintf(stderr, "[browser] profile: %s (starting fresh)\n", why.c_str());
    }
    bool profileDirty = false;
    double profileDirtyAt = 0.0;
    auto markDirty = [&] { if (!profileDirty) profileDirtyAt = UI::GetTime(); profileDirty = true; };

    br::DownloadHost downloads("OkaySpace Browser");
    const bool off = SwitchedOff();
    std::string notice;   // one line at the bottom: why a link, a game or a download did not happen

    // ---- tabs ----
    br::TabOrder order;
    std::map<int, TabView> tabs;
    auto internalPages = [&profile, &downloads](const std::string& url) { return br::InternalPage(url, profile, downloads.Items()); };
    auto homeAddress = [&]() { return profile.settings.homePage.empty() ? std::string("about:newtab") : profile.settings.homePage; };
    // Open `url` in tab `tv` (a new visit when the tab is empty, else a new history entry).
    auto load = [&](TabView& tv, const std::string& url) -> bool {
        br::BrowserTab& t = tv.host->Tab();
        const bool ok = url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0 || br::IsInternalPage(url);
        if (!ok) { notice = "Only http and https addresses can be opened."; return false; }
        if (t.Url().empty() || t.GetState() == br::BrowserTab::State::Empty) return tv.host->Open(url);
        t.Navigate(url);
        return true;
    };
    auto newTab = [&](const std::string& url, bool fromActive, bool activate) -> int {
        const int id = order.Open(fromActive, activate);
        TabView& tv = tabs[id];
        tv.host.reset(new br::TabHost(tex, "OkaySpace Browser"));
        tv.host->Tab().SetAnyOrigin(true);
        tv.host->SetGameRunner(runner);
        tv.host->SetInternalPages(internalPages);
        tv.host->SetFontLoader([](const br::FontFace& face, const std::vector<std::uint8_t>& bytes) {
            // our own TrueType rasterizer through OkayUI's font store (it keeps a copy of the bytes; a few files at most)
            if (const UI::Font* f = UI::AddFontTtf(bytes.data(), bytes.size(), 16.0f, face.family.c_str()))
                S::RegisterWebFont(face.family, face.weight, face.italic, f);
        });
        const std::string u = url.empty() ? homeAddress() : url;
        if (!off) load(tv, u);
        tv.focusAddress = u == "about:newtab";
        return id;
    };
    auto closeTab = [&](int id) {
        auto it = tabs.find(id);
        if (it == tabs.end()) return;
        const std::string url = it->second.host->Tab().Url();
        order.Close(id, br::IsInternalPage(url) ? std::string() : url);
        tabs.erase(it);
        if (order.Count() == 0) newTab(std::string(), false, true);   // never an empty window: a new-tab page
    };
    auto active = [&]() -> TabView& { return tabs[order.Active()]; };
    auto zoomKey = [](const std::string& url) { return br::IsInternalPage(url) ? std::string("about:") : br::OriginOf(url); };
    auto typedAddress = [&](const std::string& typed) -> std::string {
        const br::AddressDecision d = br::DecideAddress(typed, profile.settings.searchUrl);
        if (d.kind == br::AddressDecision::NoSearch) { notice = "Type an address (no search address is set in Settings)."; return std::string(); }
        return d.url;
    };

    if (starts.empty()) newTab(std::string(), false, true);
    for (const std::string& s : starts) {
        const std::string u = typedAddress(s);
        newTab(u.empty() ? std::string("about:newtab") : u, false, order.Count() == 0);
    }
    if (off) notice = "Our own browser is switched off (Settings > Experimental > Browser). Nothing is loaded.";

    // ---- the menus, the find bar, the dialogs ----
    Menu menu;
    bool findOpen = false, findFocus = false;
    char findText[256] = {0};
    int findCurrent = 0;
    float findScrollTo = -1.0f;
    struct Offer { br::BrowserTab::DownloadOffer offer; bool open = false; } offer;
    bool settingsOpen = false;
    char setSearch[1024] = {0}, setHome[1024] = {0}, setDir[1024] = {0};
    bool setAsk = true, setBar = true;
    bool shelfOpen = false;
    int frame = 0;
    bool quit = false;

    auto startDownload = [&](const br::BrowserTab::DownloadOffer& o) {
        const std::string dir = profile.settings.downloadDir.empty() ? DefaultDownloadDir(runner.dataRoot) : profile.settings.downloadDir;
        const std::string path = br::UniqueDownloadPath(dir, o.fileName, PathTaken, nullptr);
        downloads.Start(o.url, path, o.contentType, o.size);
        shelfOpen = true;
        notice.clear();
    };
    auto openSettings = [&] {
        std::snprintf(setSearch, sizeof setSearch, "%s", profile.settings.searchUrl.c_str());
        std::snprintf(setHome, sizeof setHome, "%s", profile.settings.homePage.c_str());
        std::snprintf(setDir, sizeof setDir, "%s", profile.settings.downloadDir.c_str());
        setAsk = profile.settings.askBeforeDownload;
        setBar = profile.settings.showBookmarksBar;
        settingsOpen = true;
    };
    auto doAction = [&](int a) {
        TabView& tv = active();
        br::BrowserTab& t = tv.host->Tab();
        const std::string zk = zoomKey(t.Url());
        switch (a) {
        case A_NewTab: newTab(std::string(), false, true); break;
        case A_CloseTab: closeTab(order.Active()); break;
        case A_ReopenTab: { const std::string u = order.TakeClosed(); if (!u.empty()) newTab(u, false, true); break; }
        case A_History: newTab("about:history", true, true); break;
        case A_Bookmarks: newTab("about:bookmarks", true, true); break;
        case A_Downloads: newTab("about:downloads", true, true); break;
        case A_ZoomIn: profile.zoom.In(zk); markDirty(); break;
        case A_ZoomOut: profile.zoom.Out(zk); markDirty(); break;
        case A_ZoomReset: profile.zoom.Reset(zk); markDirty(); break;
        case A_Find: findOpen = true; findFocus = true; break;
        case A_Settings: openSettings(); break;
        case A_ToggleBar: profile.settings.showBookmarksBar = !profile.settings.showBookmarksBar; markDirty(); break;
        case A_OpenLinkNewTab: if (!menu.link.empty()) newTab(menu.link, true, false); break;
        case A_OpenBookmarkNewTab: if (!menu.link.empty()) newTab(menu.link, false, true); break;
        case A_CopyLink: UI::SetClipboardText(menu.link.c_str()); break;
        case A_CopyText: UI::SetClipboardText(menu.text.c_str()); break;
        case A_CopyPage: UI::SetClipboardText(t.Url().c_str()); break;
        case A_Back: t.GoBack(); break;
        case A_Forward: t.GoForward(); break;
        case A_Reload: t.Reload(); break;
        case A_Bookmark: if (!t.Url().empty()) { profile.bookmarks.Toggle(t.Url(), t.Title()); markDirty(); } break;
        case A_RemoveBookmark: profile.bookmarks.Remove(menu.link); markDirty(); break;
        case A_Quit: quit = true; break;
        default: break;
        }
    };
    auto openMenu = [&](UI::Vec2 at, std::vector<MenuItem> items) {
        menu.open = true;
        menu.at = at;
        menu.items = std::move(items);
        menu.openedFrame = frame;
    };

    OkayUI::UseLauncherLook(true);
    okayuihost::platform::FrameLoop loop(*be, *painter);
    bool autoplayDone = false;
    std::string shownTitle;
    int lastActive = -1;
    while (!quit && loop.Frame([&] {
        ++frame;
        const UI::Input& in = UI::GetFrameInput();
        const bool ctrl = in.ctrl, shift = in.shift, alt = UI::IsKeyDown(UI::KeyId_Alt);
        // Ctrl+<key> is a shortcut, never typing (a field would otherwise get the "1" of Ctrl+1)
        if (ctrl && !alt) UI::GetIO().InputQueueCharacters.resize(0);
        const bool modal = menu.open || offer.open || settingsOpen;

        // ---- every tab: fetches, visits for the history, download offers ----
        for (auto& kv : tabs) {
            br::TabHost& h = *kv.second.host;
            if (!off) h.Pump();
            br::BrowserTab& t = h.Tab();
            if (t.TakeVisited() && !br::IsInternalPage(t.Url())) {
                profile.history.Add(t.Url(), t.Title(), (std::int64_t)std::time(nullptr));
                markDirty();
            }
            br::BrowserTab::DownloadOffer o;
            if (t.TakeDownloadOffer(o)) {
                if (profile.settings.askBeforeDownload) { offer.offer = o; offer.open = true; }
                else startDownload(o);
            }
        }
        downloads.Pump();
        if (downloads.TakeChanged())
            for (auto& kv : tabs) if (kv.second.host->Tab().Url() == "about:downloads") kv.second.host->Tab().Reload();

        // ---- shortcuts, as in common browsers ----
        auto key = [](UI::KeyId k) { return UI::IsKeyPressed(k, false); };
        if (!modal && !off) {
            if (ctrl && key(UI::KeyId_T)) { if (shift) doAction(A_ReopenTab); else doAction(A_NewTab); }
            else if (ctrl && (key(UI::KeyId_W) || key(UI::KeyId_F4))) doAction(A_CloseTab);
            else if (ctrl && key(UI::KeyId_Tab)) { if (shift) order.Previous(); else order.Next(); }
            else if (ctrl && key(UI::KeyId_PageDown)) order.Next();
            else if (ctrl && key(UI::KeyId_PageUp)) order.Previous();
            else if ((ctrl && key(UI::KeyId_L)) || (alt && key(UI::KeyId_D)) || key(UI::KeyId_F6)) active().focusAddress = true;
            else if (key(UI::KeyId_F5) || (ctrl && key(UI::KeyId_R))) doAction(A_Reload);
            else if (alt && key(UI::KeyId_Left)) doAction(A_Back);
            else if (alt && key(UI::KeyId_Right)) doAction(A_Forward);
            else if (alt && key(UI::KeyId_Home)) load(active(), homeAddress());
            else if (ctrl && key(UI::KeyId_D)) doAction(A_Bookmark);
            else if (ctrl && key(UI::KeyId_H)) doAction(A_History);
            else if (ctrl && key(UI::KeyId_J)) doAction(A_Downloads);
            else if (ctrl && shift && key(UI::KeyId_O)) doAction(A_Bookmarks);
            else if (ctrl && shift && key(UI::KeyId_B)) doAction(A_ToggleBar);
            else if (ctrl && (key(UI::KeyId_Equal) || key(UI::KeyId_KeypadAdd))) doAction(A_ZoomIn);
            else if (ctrl && (key(UI::KeyId_Minus) || key(UI::KeyId_KeypadSubtract))) doAction(A_ZoomOut);
            else if (ctrl && (key(UI::KeyId_0) || key(UI::KeyId_Keypad0))) doAction(A_ZoomReset);
            else if (ctrl && key(UI::KeyId_F)) doAction(A_Find);
            else if (key(UI::KeyId_F3) && findOpen) findCurrent += shift ? -1 : 1, findScrollTo = 0.0f;
            else if (ctrl && key(UI::KeyId_G) && findOpen) findCurrent += shift ? -1 : 1, findScrollTo = 0.0f;
            else if (ctrl && key(UI::KeyId_Q)) doAction(A_Quit);
            else if (ctrl) {
                for (int n = 1; n <= 9; ++n) if (key((UI::KeyId)(UI::KeyId_1 + n - 1))) order.ActivateNumber(n);
            }
            if (key(UI::KeyId_Escape) && !UI::WantsTextInput()) {
                if (findOpen) findOpen = false;
                else if (active().host->Tab().GetState() == br::BrowserTab::State::Loading) active().host->Tab().Stop();
            }
        }
        if (tabs.find(order.Active()) == tabs.end()) newTab(std::string(), false, true);
        if (order.Active() != lastActive) {   // another tab: the keyboard leaves the old tab's fields (its address is shown)
            if (lastActive >= 0 && !active().focusAddress) UI::SetKeyboardFocusId(0);
            lastActive = order.Active();
        }

        const float W = (float)loop.GetHost().DisplayWidth(), H = (float)loop.GetHost().DisplayHeight();
        br::BrowserTab& tab = active().host->Tab();

        // ---- the window's title: the page's title ----
        const std::string title = (tab.Title().empty() ? (tab.Url().empty() ? std::string("New tab") : tab.Url()) : tab.Title()) + " - OkaySpace Browser";
        if (title != shownTitle) { win->SetTitle(title); shownTitle = title; }

        // ---- the tab bar ----
        const float tabBarH = 34.0f;
        UI::BareNextWindow();
        if (UI::Begin("##tabbar", 0, 0, W, tabBarH)) {
            const auto& dl = UI::GetWindowDrawList();
            dl->AddRectFilled(UI::Vec2(0, 0), UI::Vec2(W, tabBarH), Rgba(28, 31, 38));
            const std::vector<int> ids = order.Ids();
            const float plusW = 34.0f;
            const float tw = std::max(60.0f, std::min(220.0f, (W - plusW - 12.0f) / (float)std::max<std::size_t>(1, ids.size())));
            const UI::Vec2 m = UI::GetMousePos();
            float x = 6.0f;
            int toClose = -1;
            for (int id : ids) {
                br::BrowserTab& t = tabs[id].host->Tab();
                const bool act = id == order.Active();
                const bool loadingTab = t.GetState() == br::BrowserTab::State::Loading;
                const float y0 = 5.0f, y1 = tabBarH;
                const bool hover = !modal && m.x >= x && m.x < x + tw - 2 && m.y >= y0 && m.y < y1;
                dl->AddRectFilled(UI::Vec2(x, y0), UI::Vec2(x + tw - 2, y1), act ? Rgba(236, 239, 245) : hover ? Rgba(62, 67, 80) : Rgba(44, 48, 58), 6.0f);
                std::string label = t.Title();
                if (label.empty()) label = t.Url().empty() || t.Url() == "about:newtab" ? std::string("New tab") : t.Url();
                if (loadingTab && t.Title().empty()) label = "Loading...";
                float tx = x + 10.0f;
                if (loadingTab) {   // the loading indicator: a dot that turns around a small circle
                    const float a = (float)frame * 0.25f;
                    dl->AddCircle(UI::Vec2(tx + 6, y0 + 14), 6.0f, act ? Rgba(170, 180, 200) : Rgba(110, 118, 135), 0, 2.0f);
                    dl->AddCircleFilled(UI::Vec2(tx + 6 + std::cos(a) * 6.0f, y0 + 14 + std::sin(a) * 6.0f), 2.5f, Rgba(58, 123, 213));
                    tx += 18.0f;
                }
                const float closeX = x + tw - 24.0f;
                dl->AddText(UI::Vec2(tx, y0 + 7), act ? Rgba(25, 30, 40) : Rgba(220, 225, 235), Fit(label, closeX - tx - 4).c_str());
                const bool overClose = hover && m.x >= closeX && m.x < closeX + 18;
                if (overClose) dl->AddRectFilled(UI::Vec2(closeX, y0 + 5), UI::Vec2(closeX + 18, y0 + 23), act ? Rgba(200, 205, 215) : Rgba(80, 86, 100), 4.0f);
                dl->AddText(UI::Vec2(closeX + 5, y0 + 7), act ? Rgba(60, 66, 80) : Rgba(180, 186, 200), "x");
                if (hover && UI::IsMouseClicked(0)) { if (overClose) toClose = id; else order.Activate(id); }
                if (hover && UI::IsMouseClicked(2)) toClose = id;   // a middle click closes a tab
                x += tw;
            }
            const bool plusHover = !modal && m.x >= x && m.x < x + plusW - 4 && m.y >= 5 && m.y < tabBarH - 2;
            dl->AddRectFilled(UI::Vec2(x, 7), UI::Vec2(x + plusW - 4, tabBarH - 3), plusHover ? Rgba(62, 67, 80) : Rgba(36, 40, 48), 6.0f);
            dl->AddText(UI::Vec2(x + 11, 12), Rgba(220, 225, 235), "+");
            if (plusHover && UI::IsMouseClicked(0)) doAction(A_NewTab);
            if (toClose >= 0) closeTab(toClose);
        }
        UI::End();
        if (tabs.find(order.Active()) == tabs.end()) return;   // the tab bar closed it: draw again next frame
        TabView& cur = active();
        br::TabHost& h = *cur.host;
        br::BrowserTab& t = h.Tab();

        // ---- the bottom: find bar, download shelf, notice ----
        const float findH = findOpen ? 36.0f : 0.0f;
        const float shelfH = shelfOpen && !downloads.Items().empty() ? 30.0f : 0.0f;
        const float noticeH = notice.empty() ? 0.0f : 24.0f;
        const float viewTop = tabBarH, viewH = H - tabBarH - findH - shelfH - noticeH;

        // find hits for the page as it is laid out now
        const float zoom = profile.zoom.Get(zoomKey(t.Url()));
        if (findOpen && t.HasPage()) {
            const br::LayoutResult& lay = t.Layout(cur.pageW / zoom, S::OkayUIMeasure());
            const std::string keyNow = std::string(findText) + "\n" + t.Url() + "\n" + std::to_string(lay.cmds.size()) + "\n" + std::to_string((int)cur.pageW);
            if (keyNow != cur.hitsKey) {
                const bool sameQuery = cur.hitsKey.rfind(std::string(findText) + "\n", 0) == 0;
                cur.hits = br::FindInPage(lay, findText, S::OkayUIMeasure());
                cur.hitsKey = keyNow;
                if (!sameQuery) { findCurrent = 0; findScrollTo = 0.0f; }
            }
        } else if (!findOpen) { cur.hits.clear(); cur.hitsKey.clear(); }
        const int nHits = (int)cur.hits.size();
        if (nHits > 0) findCurrent = ((findCurrent % nHits) + nHits) % nHits;

        // ---- the toolbar and the page ----
        S::BrowserViewOptions opt;
        opt.address = &cur.field;
        opt.hostButtons = false;
        opt.programButtons = true;
        opt.bookmarked = profile.bookmarks.Has(t.Url());
        opt.placeholder = br::AddressPlaceholder(profile.settings.searchUrl);
        opt.focusAddress = cur.focusAddress && !modal;
        cur.focusAddress = false;
        opt.blockInput = modal;
        opt.zoom = zoom;
        opt.edit = &cur.edit;
        if (findOpen && nHits > 0) {
            opt.findHits = &cur.hits;
            opt.findCurrent = findCurrent;
            if (findScrollTo >= 0.0f) { opt.scrollToPageY = cur.hits[(std::size_t)findCurrent].y; findScrollTo = -1.0f; }
        }
        opt.embedState = [&h](const br::DrawCmd& c) {
            S::EmbedState st;
            const std::string url = h.GameUrlFor(c.tag, c.text);
            if (url.empty()) {
                if (c.tag == "okay-game") st.status = h.CanRunGames() ? "This game is on another site and cannot run here." : "The OkaySpace player is missing, so games cannot run.";
                return st;
            }
            st.playable = true;
            st.title = h.GameTitle();
            const br::TabHost::GameStatus g = h.Game(url);
            switch (g.phase) {
            case br::TabHost::GameStatus::None: st.status = "Runs natively with the OkaySpace player"; break;
            case br::TabHost::GameStatus::Downloading:
                st.busy = true;
                st.status = "Downloading... " + std::to_string((unsigned long long)(g.bytes >> 10)) + " KB";
                break;
            case br::TabHost::GameStatus::Unpacking: st.busy = true; st.status = "Checking and unpacking..."; break;
            case br::TabHost::GameStatus::Running: st.busy = true; st.status = "Running in its own window"; break;
            case br::TabHost::GameStatus::Finished: st.status = g.message; break;
            case br::TabHost::GameStatus::Failed: st.status = g.message; break;
            }
            return st;
        };
        // the bookmarks bar, between the toolbar and the page
        int barClicked = -1, barMiddle = -1, barRight = -1;
        UI::Vec2 barRightAt;
        if (profile.settings.showBookmarksBar) {
            opt.belowToolbarH = 26.0f;
            opt.belowToolbar = [&](float bx, float by, float bw) {
                const auto& dl = UI::GetWindowDrawList();
                const UI::Vec2 m = UI::GetMousePos();
                float x = bx;
                const auto& items = profile.bookmarks.Items();
                if (items.empty()) {
                    dl->AddText(UI::Vec2(x + 4, by + 5), Rgba(140, 148, 165), "Bookmarks appear here (Ctrl+D bookmarks this page)");
                    return;
                }
                for (std::size_t i = 0; i < items.size(); ++i) {
                    const std::string label = Fit(items[i].title.empty() ? items[i].url : items[i].title, 150.0f);
                    const float w = UI::CalcTextWidth(label.c_str()) + 16.0f;
                    if (x + w > bx + bw) break;
                    const bool hover = !modal && m.x >= x && m.x < x + w && m.y >= by && m.y < by + 24.0f;
                    if (hover) dl->AddRectFilled(UI::Vec2(x, by), UI::Vec2(x + w, by + 24.0f), Rgba(70, 76, 90), 5.0f);
                    dl->AddText(UI::Vec2(x + 8, by + 5), Rgba(225, 230, 240), label.c_str());
                    if (hover) {
                        UI::SetMouseCursor(UI::MouseCursor_Hand);
                        if (UI::IsMouseClicked(0)) { if (UI::GetFrameInput().ctrl) barMiddle = (int)i; else barClicked = (int)i; }
                        if (UI::IsMouseClicked(2)) barMiddle = (int)i;
                        if (UI::IsMouseClicked(1)) { barRight = (int)i; barRightAt = m; }
                    }
                    x += w + 2.0f;
                }
            };
        }
        const S::BrowserViewResult r = S::DrawBrowserView(t, 0, viewTop, W, viewH,
            [&h](const std::string& src) { return (SDL_Texture*)h.ImageFor(src); }, opt);
        cur.pageW = r.page.w;
        if (off) return;
        if (barClicked >= 0 && barClicked < (int)profile.bookmarks.Items().size()) load(cur, profile.bookmarks.Items()[(std::size_t)barClicked].url);
        if (barMiddle >= 0 && barMiddle < (int)profile.bookmarks.Items().size()) newTab(profile.bookmarks.Items()[(std::size_t)barMiddle].url, false, false);
        if (barRight >= 0 && barRight < (int)profile.bookmarks.Items().size()) {
            menu.link = profile.bookmarks.Items()[(std::size_t)barRight].url;
            openMenu(barRightAt, {{"Open", "", A_OpenBookmarkNewTab}, {"Copy address", "", A_CopyLink}, {"", "", 0}, {"Remove bookmark", "", A_RemoveBookmark}});
        }
        if (r.formSent) notice = r.formNote;
        else if (!r.formNote.empty()) notice = r.formNote;
        if (r.back) { t.GoBack(); notice.clear(); }
        if (r.forward) { t.GoForward(); notice.clear(); }
        if (r.reload) t.Reload();
        if (r.stop) t.Stop();
        if (r.home) load(cur, homeAddress());
        if (r.bookmark) doAction(A_Bookmark);
        if (r.menu) {
            const bool bar = profile.settings.showBookmarksBar;
            openMenu(UI::Vec2(std::max(0.0f, r.menuButton.x + r.menuButton.w - 260.0f), r.menuButton.y + r.menuButton.h + 4.0f),
                     {{"New tab", "Ctrl+T", A_NewTab}, {"Reopen closed tab", "Ctrl+Shift+T", A_ReopenTab}, {"", "", 0},
                      {"History", "Ctrl+H", A_History}, {"Bookmarks", "Ctrl+Shift+O", A_Bookmarks}, {"Downloads", "Ctrl+J", A_Downloads},
                      {bar ? "Hide bookmarks bar" : "Show bookmarks bar", "Ctrl+Shift+B", A_ToggleBar}, {"", "", 0},
                      {"Zoom in", "Ctrl +", A_ZoomIn}, {"Zoom out", "Ctrl -", A_ZoomOut},
                      {"Actual size (" + std::to_string((int)std::lround(zoom * 100)) + "%)", "Ctrl+0", A_ZoomReset}, {"", "", 0},
                      {"Find on page", "Ctrl+F", A_Find}, {"Settings", "", A_Settings}, {"", "", 0}, {"Quit", "Ctrl+Q", A_Quit}});
        }
        if (!r.navigate.empty()) {
            notice.clear();
            const std::string u = typedAddress(r.navigate);
            if (!u.empty()) load(cur, u);
        }
        if (r.linkClicked >= 0) {
            const br::LayoutResult& page = t.Layout(r.page.w / zoom, S::OkayUIMeasure());
            if (r.linkClicked < (int)page.links.size()) {
                const std::string& href = page.links[(std::size_t)r.linkClicked];
                if (r.linkInNewTab) {
                    const std::string target = t.ResolveImage(href);   // the link resolved against the page (and its <base href>)
                    if (target.rfind("http://", 0) == 0 || target.rfind("https://", 0) == 0) newTab(target, true, false);
                    else notice = "That link cannot be opened (only http and https).";
                } else {
                    const br::BrowserTab::Action a = t.Activate(href);
                    if (a.kind == br::BrowserTab::Action::Refused) notice = "That link cannot be opened (only http and https).";
                    else if (a.kind == br::BrowserTab::Action::Scrolled) { /* the view scrolls */ }
                    else notice.clear();
                }
            }
        }
        if (r.contextMenu) {
            const br::LayoutResult& page = t.Layout(r.page.w / zoom, S::OkayUIMeasure());
            std::vector<MenuItem> items;
            menu.link.clear();
            menu.text = r.contextText;
            if (r.contextLink >= 0 && r.contextLink < (int)page.links.size()) {
                menu.link = t.ResolveImage(page.links[(std::size_t)r.contextLink]);
                items.push_back({"Open link in new tab", "", A_OpenLinkNewTab});
                items.push_back({"Copy link address", "", A_CopyLink});
                items.push_back({"", "", 0});
            }
            items.push_back({"Back", "Alt+Left", A_Back, t.CanGoBack()});
            items.push_back({"Forward", "Alt+Right", A_Forward, t.CanGoForward()});
            items.push_back({"Reload", "Ctrl+R", A_Reload});
            items.push_back({"", "", 0});
            if (!r.contextText.empty()) items.push_back({"Copy text", "", A_CopyText});
            items.push_back({"Copy page address", "", A_CopyPage});
            items.push_back({opt.bookmarked ? "Remove bookmark" : "Bookmark this page", "Ctrl+D", A_Bookmark});
            items.push_back({"Find on page", "Ctrl+F", A_Find});
            openMenu(r.contextAt, items);
        }
        // games
        int play = r.playEmbed;
        if (autoplay && !autoplayDone && t.GetState() == br::BrowserTab::State::Ready) {
            autoplayDone = true;
            const br::LayoutResult& page = t.Layout(r.page.w / zoom, S::OkayUIMeasure());
            for (int ei : page.embeds)
                if (!h.GameUrlFor(page.cmds[(std::size_t)ei].tag, page.cmds[(std::size_t)ei].text).empty()) { play = ei; break; }
        }
        if (play >= 0) {
            const br::LayoutResult& page = t.Layout(r.page.w / zoom, S::OkayUIMeasure());
            if (play < (int)page.cmds.size()) {
                const br::DrawCmd& c = page.cmds[(std::size_t)play];
                std::string why;
                if (!h.Play(h.GameUrlFor(c.tag, c.text), why)) notice = why;
                else notice.clear();
            }
        }

        // ---- find bar ----
        float y = viewTop + viewH;
        if (findOpen) {
            UI::BareNextWindow();
            if (UI::Begin("##findbar", 0, y, W, findH)) {
                UI::GetWindowDrawList()->AddRectFilled(UI::Vec2(0, y), UI::Vec2(W, y + findH), Rgba(36, 40, 48));
                UI::SetCursorScreenPos(UI::Vec2(8, y + 5));
                UI::TextDisabled("Find");
                UI::SameLine();
                UI::SetNextItemWidth(260.0f);
                if (findFocus) { UI::SetKeyboardFocusHere(); findFocus = false; }
                const bool enter = UI::InputText("##find", findText, (int)sizeof findText, UI::InputText_EnterReturnsTrue);
                if (enter) { findCurrent += shift ? -1 : 1; findScrollTo = 0.0f; findFocus = true; }
                UI::SameLine();
                if (UI::Button("Previous")) { --findCurrent; findScrollTo = 0.0f; }
                UI::SameLine();
                if (UI::Button("Next")) { ++findCurrent; findScrollTo = 0.0f; }
                UI::SameLine();
                const std::string count = findText[0] == 0 ? std::string() : nHits == 0 ? std::string("No matches")
                                        : std::to_string(findCurrent + 1) + " of " + std::to_string(nHits);
                UI::TextDisabled(count.c_str());
                UI::SameLine();
                if (UI::Button("Close")) findOpen = false;
                if (UI::IsKeyPressed(UI::KeyId_Escape, false)) findOpen = false;
            }
            UI::End();
            y += findH;
        }
        // ---- download shelf: the newest download and its progress ----
        if (shelfH > 0.0f) {
            UI::BareNextWindow();
            if (UI::Begin("##shelf", 0, y, W, shelfH)) {
                UI::GetWindowDrawList()->AddRectFilled(UI::Vec2(0, y), UI::Vec2(W, y + shelfH), Rgba(44, 48, 58));
                const br::DownloadItem& d = downloads.Items().back();
                std::string line = d.fileName + "  -  ";
                switch (d.state) {
                case br::DownloadItem::Active:
                    line += br::SizeText(d.bytes) + (d.total > 0 ? " of " + br::SizeText(d.total) : std::string());
                    break;
                case br::DownloadItem::Done: line += "saved (" + br::SizeText(d.bytes) + ") to " + d.path; break;
                case br::DownloadItem::Failed: line += "failed: " + d.message; break;
                case br::DownloadItem::Cancelled: line += "cancelled"; break;
                }
                UI::SetCursorScreenPos(UI::Vec2(10, y + 3));
                UI::TextUnformatted(Fit(line, W - 300.0f).c_str());
                UI::SameLine();
                if (d.state == br::DownloadItem::Active) {
                    if (d.total > 0) {
                        const UI::Vec2 at = UI::GetCursorScreenPos();
                        const float f = std::min(1.0f, (float)d.bytes / (float)d.total);
                        UI::GetWindowDrawList()->AddRectFilled(UI::Vec2(at.x, at.y + 6), UI::Vec2(at.x + 80, at.y + 14), Rgba(26, 28, 34), 3.0f);
                        UI::GetWindowDrawList()->AddRectFilled(UI::Vec2(at.x, at.y + 6), UI::Vec2(at.x + 80 * f, at.y + 14), Rgba(58, 123, 213), 3.0f);
                        UI::Dummy(84, 18);
                        UI::SameLine();
                    }
                    if (UI::Button("Cancel")) downloads.Cancel((int)downloads.Items().size() - 1);
                    UI::SameLine();
                }
                if (UI::Button("Show all")) doAction(A_Downloads);
                UI::SameLine();
                if (UI::Button("Hide")) shelfOpen = false;
            }
            UI::End();
            y += shelfH;
        }
        if (!notice.empty()) {
            const auto& fg = UI::GetForegroundDrawList();
            fg->AddRectFilled(UI::Vec2(0, H - noticeH), UI::Vec2(W, H), 0xF0302A22u);
            fg->AddText(UI::Vec2(8, H - noticeH + 6), 0xFFB8E8F5u, notice.c_str());
        }

        // ---- the open menu, over everything ----
        if (menu.open) {
            UI::BareNextWindow();
            if (UI::Begin("##menu", 0, 0, W, H)) {
                const auto& dl = UI::GetWindowDrawList();
                const float rowH = 24.0f, sepH = 9.0f, mw = 260.0f;
                float mh = 8.0f;
                for (const MenuItem& it : menu.items) mh += it.action ? rowH : sepH;
                const float mx = std::min(menu.at.x, W - mw - 4.0f), my = std::min(menu.at.y, H - mh - 4.0f);
                dl->AddRectFilled(UI::Vec2(mx + 3, my + 4), UI::Vec2(mx + mw + 3, my + mh + 4), Rgba(0, 0, 0, 90), 6.0f);
                dl->AddRectFilled(UI::Vec2(mx, my), UI::Vec2(mx + mw, my + mh), Rgba(40, 44, 54), 6.0f);
                dl->AddRect(UI::Vec2(mx, my), UI::Vec2(mx + mw, my + mh), Rgba(80, 86, 100), 6.0f, 1.0f);
                const UI::Vec2 m = UI::GetMousePos();
                float iy = my + 4.0f;
                int chosen = 0;
                for (const MenuItem& it : menu.items) {
                    if (!it.action) {
                        dl->AddRectFilled(UI::Vec2(mx + 8, iy + 4), UI::Vec2(mx + mw - 8, iy + 5), Rgba(80, 86, 100));
                        iy += sepH;
                        continue;
                    }
                    const bool hover = it.enabled && m.x >= mx && m.x < mx + mw && m.y >= iy && m.y < iy + rowH;
                    if (hover) dl->AddRectFilled(UI::Vec2(mx + 4, iy), UI::Vec2(mx + mw - 4, iy + rowH), Rgba(58, 123, 213), 4.0f);
                    const UI::Color32 col = !it.enabled ? Rgba(120, 126, 140) : Rgba(230, 234, 242);
                    dl->AddText(UI::Vec2(mx + 14, iy + 4), col, it.label.c_str());
                    if (!it.shortcut.empty()) {
                        const float sw = UI::CalcTextWidth(it.shortcut.c_str());
                        dl->AddText(UI::Vec2(mx + mw - 14 - sw, iy + 4), hover ? Rgba(220, 230, 250) : Rgba(140, 148, 165), it.shortcut.c_str());
                    }
                    if (hover && UI::IsMouseClicked(0)) chosen = it.action;
                    iy += rowH;
                }
                const bool outside = !(m.x >= mx && m.x < mx + mw && m.y >= my && m.y < my + mh);
                if (chosen) { menu.open = false; doAction(chosen); }
                else if (frame != menu.openedFrame && ((outside && (UI::IsMouseClicked(0) || UI::IsMouseClicked(1))) || UI::IsKeyPressed(UI::KeyId_Escape, false)))
                    menu.open = false;
            }
            UI::End();
        }

        // ---- dialogs: save a download, settings ----
        auto dialog = [&](const char* id, float dw, float dh, const std::function<void(float x, float y)>& body) {
            UI::BareNextWindow();
            if (UI::Begin(id, 0, 0, W, H)) {
                const auto& dl = UI::GetWindowDrawList();
                dl->AddRectFilled(UI::Vec2(0, 0), UI::Vec2(W, H), Rgba(0, 0, 0, 110));
                const float dx = (W - dw) * 0.5f, dy = std::max(40.0f, (H - dh) * 0.35f);
                dl->AddRectFilled(UI::Vec2(dx, dy), UI::Vec2(dx + dw, dy + dh), Rgba(40, 44, 54), 8.0f);
                dl->AddRect(UI::Vec2(dx, dy), UI::Vec2(dx + dw, dy + dh), Rgba(90, 96, 112), 8.0f, 1.0f);
                body(dx + 18.0f, dy + 14.0f);
            }
            UI::End();
        };
        if (offer.open) {
            dialog("##download", 520.0f, 170.0f, [&](float dx, float dy) {
                const br::BrowserTab::DownloadOffer& o = offer.offer;
                UI::SetCursorScreenPos(UI::Vec2(dx, dy));
                UI::SetCursorScreenPos(UI::Vec2(dx, UI::GetCursorScreenPos().y));
                UI::Heading("Save this file?");
                UI::SetCursorScreenPos(UI::Vec2(dx, UI::GetCursorScreenPos().y));
                UI::TextUnformatted(Fit(o.fileName, 480).c_str());
                const std::string meta = (o.contentType.empty() ? std::string("unknown type") : o.contentType) + ", " + br::SizeText(o.size) +
                                         ", from " + br::HostOf(o.url);
                UI::SetCursorScreenPos(UI::Vec2(dx, UI::GetCursorScreenPos().y));
                UI::TextDisabled(Fit(meta, 480).c_str());
                UI::SetCursorScreenPos(UI::Vec2(dx, UI::GetCursorScreenPos().y));
                UI::Spacing(10.0f);
                UI::SetCursorScreenPos(UI::Vec2(dx, UI::GetCursorScreenPos().y));
                if (UI::Button("Save")) { startDownload(o); offer.open = false; }
                UI::SameLine();
                if (UI::Button("Cancel") || UI::IsKeyPressed(UI::KeyId_Escape, false)) offer.open = false;
            });
        }
        if (settingsOpen) {
            dialog("##settings", 620.0f, 330.0f, [&](float dx, float dy) {
                UI::SetCursorScreenPos(UI::Vec2(dx, dy));
                UI::SetCursorScreenPos(UI::Vec2(dx, UI::GetCursorScreenPos().y));
                UI::Heading("Settings");
                UI::SetCursorScreenPos(UI::Vec2(dx, UI::GetCursorScreenPos().y));
                UI::TextUnformatted("Search address (%s is replaced by the words; empty = no search)");
                UI::SetCursorScreenPos(UI::Vec2(dx, UI::GetCursorScreenPos().y));
                UI::SetNextItemWidth(580.0f);
                UI::InputText("##search", setSearch, (int)sizeof setSearch);
                UI::SetCursorScreenPos(UI::Vec2(dx, UI::GetCursorScreenPos().y));
                if (UI::Button("Use Google")) std::snprintf(setSearch, sizeof setSearch, "%s", br::kDefaultSearchUrl);
                UI::SetCursorScreenPos(UI::Vec2(dx, UI::GetCursorScreenPos().y));
                UI::TextUnformatted("Home page (empty = the new-tab page)");
                UI::SetCursorScreenPos(UI::Vec2(dx, UI::GetCursorScreenPos().y));
                UI::SetNextItemWidth(580.0f);
                UI::InputText("##home", setHome, (int)sizeof setHome);
                UI::SetCursorScreenPos(UI::Vec2(dx, UI::GetCursorScreenPos().y));
                UI::TextUnformatted("Download folder (empty = your Downloads folder)");
                UI::SetCursorScreenPos(UI::Vec2(dx, UI::GetCursorScreenPos().y));
                UI::SetNextItemWidth(580.0f);
                UI::InputText("##dir", setDir, (int)sizeof setDir);
                UI::SetCursorScreenPos(UI::Vec2(dx, UI::GetCursorScreenPos().y));
                UI::Checkbox("Ask before saving a download", &setAsk);
                UI::SetCursorScreenPos(UI::Vec2(dx, UI::GetCursorScreenPos().y));
                UI::Checkbox("Show the bookmarks bar", &setBar);
                UI::SetCursorScreenPos(UI::Vec2(dx, UI::GetCursorScreenPos().y));
                UI::Spacing(6.0f);
                UI::SetCursorScreenPos(UI::Vec2(dx, UI::GetCursorScreenPos().y));
                if (UI::Button("Save")) {
                    profile.settings.searchUrl = okay::str::Trim(setSearch);
                    profile.settings.homePage = okay::str::Trim(setHome);
                    profile.settings.downloadDir = okay::str::Trim(setDir);
                    profile.settings.askBeforeDownload = setAsk;
                    profile.settings.showBookmarksBar = setBar;
                    markDirty();
                    settingsOpen = false;
                }
                UI::SameLine();
                if (UI::Button("Cancel") || UI::IsKeyPressed(UI::KeyId_Escape, false)) settingsOpen = false;
            });
        }

        // ---- save the profile a little after it changed ----
        if (profileDirty && UI::GetTime() - profileDirtyAt > 2.0) {
            std::string why;
            if (!br::SaveProfile(profile, profilePath, &why)) std::fprintf(stderr, "[browser] %s\n", why.c_str());
            profileDirty = false;
        }
    })) {}
    if (profileDirty) br::SaveProfile(profile, profilePath);
    tabs.clear();   // stops running games and frees the textures while the painter still exists
    painter.reset();
    win.reset();
    be->Shutdown();
    return 0;
}
