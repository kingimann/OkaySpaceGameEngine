// Browser: the whole browser in a rectangle (see BrowserApp.hpp). Moved here from browser/main.cpp (round 1 of "a regular
// browser") so the launcher's Browser page and the browser program draw the same browser; nothing of it is copied.
#include "BrowserApp.hpp"

#include "BrowserView.hpp"
#include "okay/Browser/Url.hpp"
#include "okay/Core/StringUtil.hpp"
#include "okay/UI/OkayUI.hpp"
#include "okay/UI/OkayUICompat.hpp"   // AddFontTtf: web fonts as OkayUI TrueType fonts

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <filesystem>
#include <map>
#include <utility>

namespace okay::browser {

namespace S = okayuiscreens;
namespace UI = OkayUI;
namespace fs = std::filesystem;

namespace {

// The person's Downloads folder (Windows: %USERPROFILE%\Downloads; elsewhere ~/Downloads), else the browser's own one.
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
    std::unique_ptr<TabHost> host;
    S::AddressField field;
    bool focusAddress = false;
    float pageW = 800.0f;                     // the page area's width last frame
    std::vector<FindHit> hits;
    std::string hitsKey;                      // what `hits` were found for
    S::ControlEdit edit;                      // the page's form field being typed in
    S::PageKeys keys;                         // key scrolling and the Tab focus on the page
    // the address bar's suggestions while the person types (round 2)
    std::vector<Suggestion> sugg;
    std::string suggFor;                      // the text they were made for
    int suggSel = 0;                          // the highlighted row (Enter opens it)
    bool suggHidden = false;                  // Esc closed them (until the text changes)
    UI::Rect suggRect;                        // where they were drawn last frame (clicks there are not the page's)
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
    A_RemoveBookmark, A_OpenBookmarkNewTab, A_Quit, A_Console
};

struct Menu {
    bool open = false;
    UI::Vec2 at;
    std::vector<MenuItem> items;
    std::string link;        // the context menu's link (resolved), or the bookmark's address
    std::string text;        // the context menu's text under the pointer
    int openedFrame = 0;
};

constexpr double kSaveDelay = 2.0;      // seconds after a change before the profile is written
constexpr double kRefreshEvery = 1.0;   // seconds between looks at the profile file (the other program may have written it)

} // namespace

struct BrowserApp::Impl {
    Config cfg;
    ProfileStore store;
    DownloadHost downloads;
    TabOrder order;
    std::map<int, TabView> tabs;
    std::string notice;
    double dirtyAt = 0.0, lastRefresh = 0.0;

    Menu menu;
    bool findOpen = false, findFocus = false;
    bool consoleOpen = false;   // the page's JavaScript console (F12): its newest lines at the bottom
    char findText[256] = {0};
    int findCurrent = 0;
    float findScrollTo = -1.0f;
    struct Offer { BrowserTab::DownloadOffer offer; bool open = false; } offer;
    bool settingsOpen = false;
    char setSearch[1024] = {0}, setHome[1024] = {0}, setDir[1024] = {0};
    bool setAsk = true, setBar = true;
    bool shelfOpen = false;
    int frame = 0;
    bool quit = false;
    bool autoplay = false, autoplayDone = false;
    int lastActive = -1;
    int dragTab = -1;          // the tab held down in the tab strip (dragged to reorder once the pointer moves)
    float dragFromX = 0.0f;
    bool dragging = false;

    explicit Impl(Config c) : cfg(std::move(c)), store(cfg.profilePath), downloads(cfg.userAgent) {
        std::string why;
        if (!cfg.profilePath.empty() && !store.Load(&why)) std::fprintf(stderr, "[browser] profile: %s (starting fresh)\n", why.c_str());
        if (cfg.switchedOff) notice = "Our own browser is switched off (Settings > Experimental > Browser). Nothing is loaded.";
    }

    const Profile& profile() const { return store.Get(); }
    void change(ProfileStore::ChangeFn f) {
        if (!store.Dirty()) dirtyAt = UI::GetTime();
        store.Change(std::move(f));
    }
    std::string homeAddress() const {
        return profile().settings.homePage.empty() ? std::string("about:newtab") : profile().settings.homePage;
    }
    static std::string zoomKey(const std::string& url) { return IsInternalPage(url) ? std::string("about:") : OriginOf(url); }
    TabView& active() { return tabs[order.Active()]; }

    // Open `url` in tab `tv` (a new visit when the tab is empty, else a new history entry).
    bool load(TabView& tv, const std::string& url) {
        BrowserTab& t = tv.host->Tab();
        const bool ok = url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0 || IsInternalPage(url);
        if (!ok) { notice = "Only http and https addresses can be opened."; return false; }
        if (t.Url().empty() || t.GetState() == BrowserTab::State::Empty) return tv.host->Open(url);
        t.Navigate(url);
        return true;
    }

    std::string typedAddress(const std::string& typed) {
        const AddressDecision d = DecideAddress(typed, profile().settings.searchUrl);
        if (d.kind == AddressDecision::NoSearch) { notice = "Type an address (no search address is set in Settings)."; return std::string(); }
        return d.url;
    }

    int newTab(const std::string& url, bool fromActive, bool activate) {
        const int id = order.Open(fromActive, activate);
        TabView& tv = tabs[id];
        tv.host.reset(new TabHost(cfg.textures, cfg.userAgent));
        tv.host->Tab().SetAnyOrigin(true);
        tv.host->SetGameRunner(cfg.runner);
        tv.host->SetInternalPages([this](const std::string& u) { return InternalPage(u, profile(), downloads.Items()); });
        tv.host->SetFontLoader([](const FontFace& face, const std::vector<std::uint8_t>& in) {
            // our own TrueType rasterizer through OkayUI's font store (a few files at most). The store tells files apart by the
            // address of their bytes, so each file's bytes are kept for the program's life.
            static std::deque<std::vector<std::uint8_t>> kept;
            if (kept.size() >= 8) return;
            kept.push_back(in);
            const std::vector<std::uint8_t>& bytes = kept.back();
            const std::string name = face.family + " " + std::to_string(face.weight) + (face.italic ? " italic" : "");   // one name per face
            if (const UI::Font* f = UI::AddFontTtf(bytes.data(), bytes.size(), 16.0f, name.c_str()))
                S::RegisterWebFont(face.family, face.weight, face.italic, f);
            else std::fprintf(stderr, "[browser] web font '%s' (weight %d) not loaded: OkayUI's font store is full or the file is not a font\n",
                              face.family.c_str(), face.weight);
        });
        const std::string u = url.empty() ? homeAddress() : url;
        if (!cfg.switchedOff) load(tv, u);
        tv.focusAddress = u == "about:newtab";
        return id;
    }

    void closeTab(int id) {
        auto it = tabs.find(id);
        if (it == tabs.end()) return;
        const std::string url = it->second.host->Tab().Url();
        order.Close(id, IsInternalPage(url) ? std::string() : url);
        tabs.erase(it);
        if (order.Count() == 0) newTab(std::string(), false, true);   // never an empty browser: a new-tab page
    }

    void startDownload(const BrowserTab::DownloadOffer& o) {
        const std::string dir = profile().settings.downloadDir.empty() ? DefaultDownloadDir(cfg.runner.dataRoot) : profile().settings.downloadDir;
        const std::string path = UniqueDownloadPath(dir, o.fileName, PathTaken, nullptr);
        downloads.Start(o.url, path, o.contentType, o.size);
        shelfOpen = true;
        notice.clear();
    }

    void openSettings() {
        const BrowserSettings& s = profile().settings;
        std::snprintf(setSearch, sizeof setSearch, "%s", s.searchUrl.c_str());
        std::snprintf(setHome, sizeof setHome, "%s", s.homePage.c_str());
        std::snprintf(setDir, sizeof setDir, "%s", s.downloadDir.c_str());
        setAsk = s.askBeforeDownload;
        setBar = s.showBookmarksBar;
        settingsOpen = true;
    }

    void doAction(int a) {
        TabView& tv = active();
        BrowserTab& t = tv.host->Tab();
        const std::string zk = zoomKey(t.Url());
        switch (a) {
        case A_NewTab: newTab(std::string(), false, true); break;
        case A_CloseTab: closeTab(order.Active()); break;
        case A_ReopenTab: { const std::string u = order.TakeClosed(); if (!u.empty()) newTab(u, false, true); break; }
        case A_History: newTab("about:history", true, true); break;
        case A_Bookmarks: newTab("about:bookmarks", true, true); break;
        case A_Downloads: newTab("about:downloads", true, true); break;
        // zoom and the bookmark star are kept as the value they lead to, so they apply the same on a profile re-read from the file
        case A_ZoomIn: { SiteZoom z = profile().zoom; const float v = z.In(zk); change([zk, v](Profile& p) { p.zoom.Set(zk, v); }); break; }
        case A_ZoomOut: { SiteZoom z = profile().zoom; const float v = z.Out(zk); change([zk, v](Profile& p) { p.zoom.Set(zk, v); }); break; }
        case A_ZoomReset: change([zk](Profile& p) { p.zoom.Reset(zk); }); break;
        case A_Find: findOpen = true; findFocus = true; break;
        case A_Console: consoleOpen = !consoleOpen; break;
        case A_Settings: openSettings(); break;
        case A_ToggleBar: { const bool on = !profile().settings.showBookmarksBar; change([on](Profile& p) { p.settings.showBookmarksBar = on; }); break; }
        case A_OpenLinkNewTab: if (!menu.link.empty()) newTab(menu.link, true, false); break;
        case A_OpenBookmarkNewTab: if (!menu.link.empty()) newTab(menu.link, false, true); break;
        case A_CopyLink: UI::SetClipboardText(menu.link.c_str()); break;
        case A_CopyText: UI::SetClipboardText(menu.text.c_str()); break;
        case A_CopyPage: UI::SetClipboardText(t.Url().c_str()); break;
        case A_Back: t.GoBack(); break;
        case A_Forward: t.GoForward(); break;
        case A_Reload: t.Reload(); break;
        case A_Bookmark:
            if (!t.Url().empty()) {
                const std::string url = t.Url(), title = t.Title();
                if (profile().bookmarks.Has(url)) change([url](Profile& p) { p.bookmarks.Remove(url); });
                else change([url, title](Profile& p) { p.bookmarks.Add(url, title); });
            }
            break;
        case A_RemoveBookmark: { const std::string url = menu.link; change([url](Profile& p) { p.bookmarks.Remove(url); }); break; }
        case A_Quit: if (cfg.quitItem) quit = true; break;
        default: break;
        }
    }

    void openMenu(UI::Vec2 at, std::vector<MenuItem> items) {
        menu.open = true;
        menu.at = at;
        menu.items = std::move(items);
        menu.openedFrame = frame;
    }

    void pump() {
        // every tab: fetches, visits for the history, download offers
        for (auto& kv : tabs) {
            TabHost& h = *kv.second.host;
            if (!cfg.switchedOff) h.Pump();
            BrowserTab& t = h.Tab();
            if (t.TakeVisited() && !IsInternalPage(t.Url())) {
                const std::string url = t.Url(), title = t.Title();
                const std::int64_t when = (std::int64_t)std::time(nullptr);
                change([url, title, when](Profile& p) { p.history.Add(url, title, when); });
            }
            BrowserTab::DownloadOffer o;
            if (t.TakeDownloadOffer(o)) {
                if (profile().settings.askBeforeDownload) { offer.offer = o; offer.open = true; }
                else startDownload(o);
            }
        }
        downloads.Pump();
        if (downloads.TakeChanged())
            for (auto& kv : tabs) if (kv.second.host->Tab().Url() == "about:downloads") kv.second.host->Tab().Reload();
        // the profile: written a little after it changed; read again when the other program (the launcher or the browser
        // program) replaced the file
        const double now = UI::GetTime();
        if (store.Dirty() && now - dirtyAt > kSaveDelay) save();
        else if (now - lastRefresh > kRefreshEvery) { lastRefresh = now; store.Refresh(); }
    }

    void save() {
        std::string why;
        if (!store.Save(&why)) std::fprintf(stderr, "[browser] %s\n", why.c_str());
    }

    void frameIn(float X, float Y, float W, float H);
};

BrowserApp::BrowserApp(Config config) : m(new Impl(std::move(config))) {}

BrowserApp::~BrowserApp() {
    if (m->store.Dirty()) m->save();
    m->tabs.clear();   // stops running games and frees the textures
}

int BrowserApp::OpenTyped(const std::string& typed, bool activate) {
    if (typed.empty()) return m->newTab(std::string(), false, activate || m->order.Count() == 0);
    const std::string u = m->typedAddress(typed);
    if (u.empty()) return -1;
    return m->newTab(u, false, activate || m->order.Count() == 0);
}

int BrowserApp::OpenUrl(const std::string& url, bool activate, bool fromActive) {
    return m->newTab(url, fromActive && m->order.Count() > 0, activate || m->order.Count() == 0);
}

void BrowserApp::Pump() { m->pump(); }
void BrowserApp::Frame(float x, float y, float w, float h) { m->frameIn(x, y, w, h); }
bool BrowserApp::WantsQuit() const { return m->quit; }
void BrowserApp::SetAutoplay(bool on) { m->autoplay = on; }
void BrowserApp::Save() { if (m->store.Dirty()) m->save(); }
void BrowserApp::CloseAll() { m->tabs.clear(); m->order = TabOrder(); }
std::size_t BrowserApp::TabCount() const { return m->order.Count(); }
int BrowserApp::ActiveId() const { return m->order.Active(); }
const std::vector<int>& BrowserApp::TabIds() const { return m->order.Ids(); }
BrowserTab* BrowserApp::TabById(int id) { auto it = m->tabs.find(id); return it == m->tabs.end() ? nullptr : &it->second.host->Tab(); }
BrowserTab& BrowserApp::ActiveTab() { if (m->tabs.find(m->order.Active()) == m->tabs.end()) m->newTab(std::string(), false, true); return m->active().host->Tab(); }
const Profile& BrowserApp::GetProfile() const { return m->profile(); }
const std::string& BrowserApp::Notice() const { return m->notice; }
bool BrowserApp::DownloadPromptOpen() const { return m->offer.open; }
const std::vector<DownloadItem>& BrowserApp::Downloads() const { return m->downloads.Items(); }

std::string BrowserApp::WindowTitle() const {
    auto it = m->tabs.find(m->order.Active());
    if (it == m->tabs.end()) return "OkaySpace Browser";
    const BrowserTab& tab = it->second.host->Tab();
    return (tab.Title().empty() ? (tab.Url().empty() ? std::string("New tab") : tab.Url()) : tab.Title()) + " - OkaySpace Browser";
}

// One frame of the browser inside (X, Y, W, H): what browser/main.cpp drew over its whole window.
void BrowserApp::Impl::frameIn(float X, float Y, float W, float H) {
    ++frame;
    const bool off = cfg.switchedOff;
    const std::string& pre = cfg.idPrefix;
    const UI::Input& in = UI::GetFrameInput();
    const bool ctrl = in.ctrl, shift = in.shift, alt = UI::IsKeyDown(UI::KeyId_Alt);
    // Ctrl+<key> is a shortcut, never typing (a field would otherwise get the "1" of Ctrl+1)
    if (ctrl && !alt) UI::GetIO().InputQueueCharacters.resize(0);
    const bool modal = menu.open || offer.open || settingsOpen;

    if (order.Count() == 0) newTab(std::string(), false, true);
    pump();

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
        else if (key(UI::KeyId_F12) || (ctrl && shift && key(UI::KeyId_J))) doAction(A_Console);
        else if (key(UI::KeyId_F3) && findOpen) findCurrent += shift ? -1 : 1, findScrollTo = 0.0f;
        else if (ctrl && key(UI::KeyId_G) && findOpen) findCurrent += shift ? -1 : 1, findScrollTo = 0.0f;
        else if (ctrl && key(UI::KeyId_Q) && cfg.quitItem) doAction(A_Quit);
        else if (ctrl) {
            for (int n = 1; n <= 9; ++n) if (key((UI::KeyId)(UI::KeyId_1 + n - 1))) order.ActivateNumber(n);
        }
        if (key(UI::KeyId_Escape) && !UI::WantsTextInput()) {
            if (findOpen) findOpen = false;
            else if (active().host->Tab().GetState() == BrowserTab::State::Loading) active().host->Tab().Stop();
        }
    }
    if (tabs.find(order.Active()) == tabs.end()) newTab(std::string(), false, true);
    if (order.Active() != lastActive) {   // another tab: the keyboard leaves the old tab's fields (its address is shown)
        if (lastActive >= 0 && !active().focusAddress) UI::SetKeyboardFocusId(0);
        lastActive = order.Active();
    }

    // ---- the tab strip (round 2): rounded tabs joined to the toolbar, site badges, close buttons on hover, a + button, tabs
    // that shrink as more open, a tooltip with the whole title, dragging a tab to reorder ----
    const float tabBarH = 40.0f;
    UI::BareNextWindow();
    if (UI::Begin((pre + "_tabbar").c_str(), X, Y, W, tabBarH)) {
        namespace lk = S::browserlook;
        const auto& dl = UI::GetWindowDrawList();
        dl->AddRectFilled(UI::Vec2(X, Y), UI::Vec2(X + W, Y + tabBarH), lk::kStrip);
        const std::vector<int> ids = order.Ids();
        const float plusW = 36.0f, left = X + 8.0f;
        const float tw = std::max(48.0f, std::min(240.0f, (W - plusW - 24.0f) / (float)std::max<std::size_t>(1, ids.size())));
        const UI::Vec2 m = UI::GetMousePos();
        const float y0 = Y + 7.0f, y1 = Y + tabBarH, r = 8.0f;
        int toClose = -1, hoverId = -1;
        // dragging: once the held tab moved a few pixels it follows the pointer's slot
        if (dragTab >= 0) {
            if (!UI::IsMouseDown(0) || modal) { dragTab = -1; dragging = false; }
            else {
                if (std::fabs(m.x - dragFromX) > 6.0f) dragging = true;
                if (dragging) {
                    const int slot = std::max(0, std::min((int)ids.size() - 1, (int)std::floor((m.x - left) / tw)));
                    if (slot != order.IndexOf(dragTab)) order.Move(dragTab, slot);
                }
            }
        }
        const std::vector<int> shownIds = order.Ids();
        for (std::size_t i = 0; i < shownIds.size(); ++i) {
            const float x = left + tw * (float)i;
            if (!modal && !dragging && m.x >= x && m.x < x + tw && m.y >= y0 && m.y < y1) hoverId = shownIds[i];
        }
        for (std::size_t i = 0; i < shownIds.size(); ++i) {
            const int id = shownIds[i];
            const float x = left + tw * (float)i;
            BrowserTab& t = tabs[id].host->Tab();
            const bool act = id == order.Active();
            const bool hover = id == hoverId;
            const bool loadingTab = t.GetState() == BrowserTab::State::Loading;
            if (act) {   // the active tab: one surface with the toolbar under it
                dl->AddRectFilled(UI::Vec2(x, y0), UI::Vec2(x + tw, y1), lk::kToolbar, r, UI::DrawFlags_RoundCornersTop);
            } else if (hover) {
                dl->AddRectFilled(UI::Vec2(x + 2, y0 + 2), UI::Vec2(x + tw - 2, y1 - 4), lk::kTabHover, r);
            }
            // a thin line between two tabs that are neither shown nor under the pointer
            const int nextId = i + 1 < shownIds.size() ? shownIds[i + 1] : -1;
            if (!act && !hover && nextId >= 0 && nextId != order.Active() && nextId != hoverId)
                dl->AddRectFilled(UI::Vec2(x + tw - 0.5f, y0 + 9), UI::Vec2(x + tw + 0.5f, y1 - 11), lk::kDivider);
            std::string label = t.Title();
            if (label.empty()) label = t.Url().empty() || t.Url() == "about:newtab" ? std::string("New tab") : t.Url();
            if (loadingTab && t.Title().empty()) label = "Loading...";
            // the site badge: a spinner while loading, a sign for an error, our own pages' sign, else the site's letter
            const float ix = x + 12.0f, iy = (y0 + y1) * 0.5f, ir = 8.0f;
            const bool narrow = tw < 72.0f;
            const float badgeX = narrow ? x + tw * 0.5f - ir : ix;
            if (loadingTab) {
                const float a0 = (float)frame * 0.2f;
                dl->PathClear();
                dl->PathArcTo(UI::Vec2(badgeX + ir, iy), ir - 1.0f, a0, a0 + 4.2f, 16);
                dl->PathStroke(lk::kAccent, false, 2.0f);
            } else if (t.GetState() == BrowserTab::State::Error) {
                S::DrawBrowserIcon(S::BrowserIcon::Warning, badgeX + ir, iy, 15.0f, lk::Rgb(240, 180, 90));
            } else if (IsInternalPage(t.Url()) || t.Url().empty()) {
                S::DrawBrowserIcon(S::BrowserIcon::Globe, badgeX + ir, iy, 15.0f, act ? lk::kText : lk::kTextDim);
            } else {
                static const UI::Color32 kBadge[8] = {lk::Rgb(58, 123, 213), lk::Rgb(213, 87, 58), lk::Rgb(47, 158, 110), lk::Rgb(138, 91, 214),
                                                      lk::Rgb(201, 138, 30), lk::Rgb(30, 155, 176), lk::Rgb(194, 71, 126), lk::Rgb(93, 111, 138)};
                const SiteMark mark = SiteMarkOf(t.Url());   // the same colour and letter as on the new-tab page
                dl->AddCircleFilled(UI::Vec2(badgeX + ir, iy), ir, kBadge[mark.color]);
                const char letter[2] = {mark.letter, 0};
                const float ls = 1.5f, lw = UI::CalcTextWidth(letter) * ls / UI::Style().textScale;
                dl->AddText(UI::Vec2(badgeX + ir - lw * 0.5f, iy - 6.0f), lk::Rgb(255, 255, 255), letter, ls);
            }
            // the close button: on the shown tab, the tab under the pointer, and every tab that is wide enough
            const bool showClose = act || hover || tw >= 140.0f;
            const float cs = 20.0f, closeX = x + tw - 8.0f - cs, closeY = iy - cs * 0.5f;
            const bool overClose = showClose && hover && m.x >= closeX && m.x < closeX + cs && m.y >= closeY && m.y < closeY + cs;
            if (!narrow) {
                const float tx = ix + 2.0f * ir + 8.0f;
                const float room = (showClose ? closeX : x + tw - 8.0f) - tx - 4.0f;
                if (room > 8.0f) {
                    UI::PushClipRect(tx, y0, room, y1 - y0);
                    dl->AddText(UI::Vec2(tx, iy - UI::GetTextLineHeight() * 0.5f), act ? lk::kText : lk::Rgb(196, 202, 214), Fit(label, room).c_str());
                    UI::PopClipRect();
                }
            }
            if (showClose && (!narrow || act)) {
                if (overClose) dl->AddCircleFilled(UI::Vec2(closeX + cs * 0.5f, iy), cs * 0.5f, UI::IsMouseDown(0) ? lk::kPressed : lk::kHover);
                S::DrawBrowserIcon(S::BrowserIcon::Close, closeX + cs * 0.5f, iy, 11.0f, act || hover ? lk::kText : lk::kTextDim);
            }
            if (hover) {
                if (UI::IsMouseClicked(0)) {
                    if (overClose) toClose = id;
                    else { order.Activate(id); dragTab = id; dragFromX = m.x; dragging = false; }
                }
                if (UI::IsMouseClicked(2)) toClose = id;   // a middle click closes a tab
                const std::string host = HostOf(t.Url());
                S::BrowserDelayedTooltip((unsigned)id * 2654435761u, host.empty() || IsInternalPage(t.Url()) ? label : label + "\n" + host);
            }
        }
        const float px = left + tw * (float)shownIds.size() + 6.0f, pcy = (y0 + y1) * 0.5f - 1.0f;
        const bool plusHover = !modal && !dragging && (m.x - (px + 14.0f)) * (m.x - (px + 14.0f)) + (m.y - pcy) * (m.y - pcy) < 15.0f * 15.0f;
        if (plusHover) dl->AddCircleFilled(UI::Vec2(px + 14.0f, pcy), 14.0f, UI::IsMouseDown(0) ? lk::kPressed : lk::kHover);
        S::DrawBrowserIcon(S::BrowserIcon::Plus, px + 14.0f, pcy, 13.0f, lk::kText);
        if (plusHover) {
            if (UI::IsMouseClicked(0)) doAction(A_NewTab);
            S::BrowserDelayedTooltip(0x9e3779b9u, "New tab (Ctrl+T)");
        }
        if (toClose >= 0) { closeTab(toClose); dragTab = -1; dragging = false; }
    }
    UI::End();
    if (tabs.find(order.Active()) == tabs.end()) return;   // the tab bar closed it: draw again next frame
    TabView& cur = active();
    TabHost& h = *cur.host;
    BrowserTab& t = h.Tab();

    // ---- the bottom: find bar, download shelf, notice ----
    const float findH = findOpen ? 36.0f : 0.0f;
    const float shelfH = shelfOpen && !downloads.Items().empty() ? 30.0f : 0.0f;
    const float noticeH = notice.empty() ? 0.0f : 24.0f;
    const float consoleH = consoleOpen ? std::min(160.0f, std::max(0.0f, H * 0.3f)) : 0.0f;
    const float viewTop = Y + tabBarH, viewH = std::max(40.0f, H - tabBarH - findH - shelfH - noticeH - consoleH);

    // find hits for the page as it is laid out now
    const float zoom = profile().zoom.Get(zoomKey(t.Url()));
    if (findOpen && t.HasPage()) {
        const LayoutResult& lay = t.Layout(cur.pageW / zoom, S::OkayUIMeasure());
        const std::string keyNow = std::string(findText) + "\n" + t.Url() + "\n" + std::to_string(lay.cmds.size()) + "\n" + std::to_string((int)cur.pageW);
        if (keyNow != cur.hitsKey) {
            const bool sameQuery = cur.hitsKey.rfind(std::string(findText) + "\n", 0) == 0;
            cur.hits = FindInPage(lay, findText, S::OkayUIMeasure());
            cur.hitsKey = keyNow;
            if (!sameQuery) { findCurrent = 0; findScrollTo = 0.0f; }
        }
    } else if (!findOpen) { cur.hits.clear(); cur.hitsKey.clear(); }
    const int nHits = (int)cur.hits.size();
    if (nHits > 0) findCurrent = ((findCurrent % nHits) + nHits) % nHits;

    // ---- the address bar's suggestions (round 2): made again whenever the typed text changes ----
    const std::string typedNow = cur.field.text;
    const bool typingAddress = cur.field.focused && !modal && !off && typedNow != t.Url();
    if (typingAddress && typedNow != cur.suggFor) {
        std::vector<OpenTabInfo> others;
        for (int id : order.Ids())
            if (id != order.Active()) { const BrowserTab& ot = tabs[id].host->Tab(); others.push_back({id, ot.Url(), ot.Title()}); }
        cur.sugg = SuggestAddresses(typedNow, profile(), others, 7);
        cur.suggFor = typedNow;
        cur.suggSel = 0;
        cur.suggHidden = false;
    }
    const bool suggOpen = typingAddress && !cur.suggHidden && !cur.sugg.empty() && cur.suggFor == typedNow;
    if (suggOpen) {
        const int n = (int)cur.sugg.size();
        if (UI::IsKeyPressed(UI::KeyId_Down)) cur.suggSel = (cur.suggSel + 1) % n;
        if (UI::IsKeyPressed(UI::KeyId_Up)) cur.suggSel = (cur.suggSel + n - 1) % n;
        if (UI::IsKeyPressed(UI::KeyId_Escape, false)) cur.suggHidden = true;
    }
    const UI::Vec2 mouseNow = UI::GetMousePos();
    const bool overSugg = suggOpen && mouseNow.x >= cur.suggRect.x && mouseNow.x < cur.suggRect.x + cur.suggRect.w &&
                          mouseNow.y >= cur.suggRect.y && mouseNow.y < cur.suggRect.y + cur.suggRect.h;

    // ---- the toolbar and the page ----
    S::BrowserViewOptions opt;
    opt.address = &cur.field;
    opt.hostButtons = false;
    opt.programButtons = true;
    opt.bookmarked = profile().bookmarks.Has(t.Url());
    opt.placeholder = AddressPlaceholder(profile().settings.searchUrl);
    opt.focusAddress = cur.focusAddress && !modal;
    cur.focusAddress = false;
    opt.blockInput = modal || overSugg;
    opt.zoom = zoom;
    opt.edit = &cur.edit;
    opt.keys = &cur.keys;
    if (findOpen && nHits > 0) {
        opt.findHits = &cur.hits;
        opt.findCurrent = findCurrent;
        if (findScrollTo >= 0.0f) { opt.scrollToPageY = cur.hits[(std::size_t)findCurrent].y; findScrollTo = -1.0f; }
    }
    opt.embedState = [&h](const DrawCmd& c) {
        S::EmbedState st;
        const std::string url = h.GameUrlFor(c.tag, c.text);
        if (url.empty()) {
            if (c.tag == "okay-game") st.status = h.CanRunGames() ? "This game is on another site and cannot run here." : "The OkaySpace player is missing, so games cannot run.";
            return st;
        }
        st.playable = true;
        st.title = h.GameTitle();
        const TabHost::GameStatus g = h.Game(url);
        switch (g.phase) {
        case TabHost::GameStatus::None: st.status = "Runs natively with the OkaySpace player"; break;
        case TabHost::GameStatus::Downloading:
            st.busy = true;
            st.status = "Downloading... " + std::to_string((unsigned long long)(g.bytes >> 10)) + " KB";
            break;
        case TabHost::GameStatus::Unpacking: st.busy = true; st.status = "Checking and unpacking..."; break;
        case TabHost::GameStatus::Running: st.busy = true; st.status = "Running in its own window"; break;
        case TabHost::GameStatus::Finished: st.status = g.message; break;
        case TabHost::GameStatus::Failed: st.status = g.message; break;
        }
        return st;
    };
    // the bookmarks bar, between the toolbar and the page
    int barClicked = -1, barMiddle = -1, barRight = -1;
    UI::Vec2 barRightAt;
    if (profile().settings.showBookmarksBar) {
        opt.belowToolbarH = 26.0f;
        opt.belowToolbar = [&](float bx, float by, float bw) {
            const auto& dl = UI::GetWindowDrawList();
            const UI::Vec2 m = UI::GetMousePos();
            float x = bx + 8.0f;
            const auto& items = profile().bookmarks.Items();
            if (items.empty()) {
                dl->AddText(UI::Vec2(x + 4, by + 5), Rgba(140, 148, 165), "Bookmarks appear here (Ctrl+D bookmarks this page)");
                return;
            }
            for (std::size_t i = 0; i < items.size(); ++i) {
                const std::string label = Fit(items[i].title.empty() ? items[i].url : items[i].title, 150.0f);
                const float w = UI::CalcTextWidth(label.c_str()) + 16.0f;
                if (x + w > bx + bw) break;
                const bool hover = !modal && m.x >= x && m.x < x + w && m.y >= by && m.y < by + 24.0f;
                if (hover) dl->AddRectFilled(UI::Vec2(x, by), UI::Vec2(x + w, by + 24.0f), S::browserlook::kHover, 6.0f);
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
    const S::BrowserViewResult r = S::DrawBrowserView(t, X, viewTop, W, viewH,
        [&h](const std::string& src) { return (SDL_Texture*)h.ImageFor(src); }, opt);
    cur.pageW = r.page.w;
    const auto& marks = profile().bookmarks.Items();
    if (!off) {
        if (barClicked >= 0 && barClicked < (int)marks.size()) load(cur, marks[(std::size_t)barClicked].url);
        if (barMiddle >= 0 && barMiddle < (int)marks.size()) newTab(marks[(std::size_t)barMiddle].url, false, false);
        if (barRight >= 0 && barRight < (int)marks.size()) {
            menu.link = marks[(std::size_t)barRight].url;
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
            const bool bar = profile().settings.showBookmarksBar;
            std::vector<MenuItem> items = {{"New tab", "Ctrl+T", A_NewTab}, {"Reopen closed tab", "Ctrl+Shift+T", A_ReopenTab}, {"", "", 0},
                  {"History", "Ctrl+H", A_History}, {"Bookmarks", "Ctrl+Shift+O", A_Bookmarks}, {"Downloads", "Ctrl+J", A_Downloads},
                  {bar ? "Hide bookmarks bar" : "Show bookmarks bar", "Ctrl+Shift+B", A_ToggleBar}, {"", "", 0},
                  {"Zoom in", "Ctrl +", A_ZoomIn}, {"Zoom out", "Ctrl -", A_ZoomOut},
                  {"Actual size (" + std::to_string((int)std::lround(zoom * 100)) + "%)", "Ctrl+0", A_ZoomReset}, {"", "", 0},
                  {"Find on page", "Ctrl+F", A_Find}, {consoleOpen ? "Hide console" : "Console", "F12", A_Console}, {"Settings", "", A_Settings}};
            if (cfg.quitItem) { items.push_back({"", "", 0}); items.push_back({"Quit", "Ctrl+Q", A_Quit}); }
            openMenu(UI::Vec2(std::max(X, r.menuButton.x + r.menuButton.w - 260.0f), r.menuButton.y + r.menuButton.h + 4.0f), std::move(items));
        }
        // a suggestion picked with the mouse (it is drawn over the page, under the address bar)
        int picked = -1;
        cur.suggRect = UI::Rect{0, 0, 0, 0};
        if (suggOpen && r.address.w > 0.0f) {
            namespace lk = S::browserlook;
            const float rowH = 34.0f, sx = r.address.x, sw = r.address.w, sy = r.address.y + r.address.h + 4.0f;
            const float sh = rowH * (float)cur.sugg.size() + 12.0f;
            cur.suggRect = UI::Rect{sx, sy, sw, sh};
            const auto& fg = UI::GetForegroundDrawList();
            for (int k = 3; k >= 1; --k)   // a soft shadow
                fg->AddRectFilled(UI::Vec2(sx - (float)k, sy - (float)k + 3.0f), UI::Vec2(sx + sw + (float)k, sy + sh + (float)k + 3.0f), lk::Rgb(0, 0, 0, 28), 12.0f + (float)k);
            fg->AddRectFilled(UI::Vec2(sx, sy), UI::Vec2(sx + sw, sy + sh), lk::Rgb(36, 40, 50), 12.0f);
            fg->AddRect(UI::Vec2(sx, sy), UI::Vec2(sx + sw, sy + sh), lk::kDivider, 12.0f, 1.0f);
            const float lh = UI::GetTextLineHeight();
            for (std::size_t i = 0; i < cur.sugg.size(); ++i) {
                const Suggestion& sg = cur.sugg[i];
                const float ry = sy + 6.0f + rowH * (float)i;
                const bool hov = mouseNow.x >= sx && mouseNow.x < sx + sw && mouseNow.y >= ry && mouseNow.y < ry + rowH;
                if (hov && UI::GetMouseDelta().x * UI::GetMouseDelta().x + UI::GetMouseDelta().y * UI::GetMouseDelta().y > 0.0f) cur.suggSel = (int)i;
                if ((int)i == cur.suggSel) fg->AddRectFilled(UI::Vec2(sx + 4.0f, ry), UI::Vec2(sx + sw - 4.0f, ry + rowH), lk::Rgb(58, 66, 84), 8.0f);
                const S::BrowserIcon ic = sg.kind == Suggestion::Search ? S::BrowserIcon::Search : sg.kind == Suggestion::Bookmark ? S::BrowserIcon::StarFilled
                                        : sg.kind == Suggestion::OpenTab ? S::BrowserIcon::Tab : sg.kind == Suggestion::History ? S::BrowserIcon::Clock : S::BrowserIcon::Globe;
                const float icx = sx + 24.0f, icy = ry + rowH * 0.5f;
                S::DrawBrowserIcon(ic, icx, icy, 15.0f, sg.kind == Suggestion::Bookmark ? lk::kAccent : lk::kTextDim, true);
                std::string mainText, sideText;
                switch (sg.kind) {
                case Suggestion::Search: mainText = sg.title; sideText = std::string(" - ") + (AddressPlaceholder(profile().settings.searchUrl).find("Google") != std::string::npos ? "Search Google" : "Search"); break;
                case Suggestion::Go: mainText = sg.url; break;
                case Suggestion::OpenTab: mainText = sg.title.empty() ? sg.url : sg.title; sideText = " - Switch to this tab"; break;
                default: mainText = sg.title.empty() ? sg.url : sg.title; sideText = " - " + sg.url; break;
                }
                const float tx = sx + 46.0f, room = sw - 60.0f;
                const std::string mainFit = Fit(mainText, room * 0.7f);
                const float mw = UI::CalcTextWidth(mainFit.c_str());
                fg->AddText(UI::Vec2(tx, icy - lh * 0.5f), lk::kText, mainFit.c_str());
                if (!sideText.empty() && room - mw > 40.0f)
                    fg->AddText(UI::Vec2(tx + mw, icy - lh * 0.5f), sg.kind == Suggestion::OpenTab ? lk::kAccent : lk::kTextDim, Fit(sideText, room - mw).c_str());
                if (hov && UI::IsMouseClicked(0)) picked = (int)i;
            }
        }
        if (!r.navigate.empty() && suggOpen && cur.suggSel >= 0 && cur.suggSel < (int)cur.sugg.size()) picked = cur.suggSel;
        if (picked >= 0) {
            const Suggestion sg = cur.sugg[(std::size_t)picked];
            notice.clear();
            cur.sugg.clear(); cur.suggFor.clear();
            cur.field.shownFor.clear();   // the bar shows the tab's address again
            UI::SetKeyboardFocusId(0);
            if (sg.kind == Suggestion::OpenTab && tabs.count(sg.tabId)) order.Activate(sg.tabId);
            else load(cur, sg.url);
        } else if (!r.navigate.empty()) {
            notice.clear();
            const std::string u = typedAddress(r.navigate);
            if (!u.empty()) load(cur, u);
        }
        if (r.linkClicked >= 0) {
            const LayoutResult& page = t.Layout(r.page.w / zoom, S::OkayUIMeasure());
            if (r.linkClicked < (int)page.links.size()) {
                const std::string href = page.links[(std::size_t)r.linkClicked];
                if (r.linkInNewTab) {
                    const std::string target = t.ResolveImage(href);   // the link resolved against the page (and its <base href>)
                    if (target.rfind("http://", 0) == 0 || target.rfind("https://", 0) == 0) newTab(target, true, false);
                    else notice = "That link cannot be opened (only http and https).";
                } else {
                    const BrowserTab::Action a = t.Activate(href);
                    if (a.kind == BrowserTab::Action::Refused) notice = "That link cannot be opened (only http and https).";
                    else if (a.kind == BrowserTab::Action::Scrolled) { /* the view scrolls */ }
                    else notice.clear();
                }
            }
        }
        if (r.contextMenu) {
            const LayoutResult& page = t.Layout(r.page.w / zoom, S::OkayUIMeasure());
            std::vector<MenuItem> items;
            menu.link.clear();
            menu.text = cur.keys.HasSelection() ? S::SelectedPageText(page, cur.keys) : r.contextText;
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
            if (cur.keys.HasSelection()) items.push_back({"Copy", "Ctrl+C", A_CopyText});
            else if (!r.contextText.empty()) items.push_back({"Copy text", "", A_CopyText});
            items.push_back({"Copy page address", "", A_CopyPage});
            items.push_back({opt.bookmarked ? "Remove bookmark" : "Bookmark this page", "Ctrl+D", A_Bookmark});
            items.push_back({"Find on page", "Ctrl+F", A_Find});
            openMenu(r.contextAt, items);
        }
        // games
        int play = r.playEmbed;
        if (autoplay && !autoplayDone && t.GetState() == BrowserTab::State::Ready) {
            autoplayDone = true;
            const LayoutResult& page = t.Layout(r.page.w / zoom, S::OkayUIMeasure());
            for (int ei : page.embeds)
                if (!h.GameUrlFor(page.cmds[(std::size_t)ei].tag, page.cmds[(std::size_t)ei].text).empty()) { play = ei; break; }
        }
        if (play >= 0) {
            const LayoutResult& page = t.Layout(r.page.w / zoom, S::OkayUIMeasure());
            if (play < (int)page.cmds.size()) {
                const DrawCmd& c = page.cmds[(std::size_t)play];
                std::string why;
                if (!h.Play(h.GameUrlFor(c.tag, c.text), why)) notice = why;
                else notice.clear();
            }
        }
    }

    // ---- find bar ----
    float y = viewTop + viewH;
    if (findOpen) {
        UI::BareNextWindow();
        if (UI::Begin((pre + "_findbar").c_str(), X, y, W, findH)) {
            UI::GetWindowDrawList()->AddRectFilled(UI::Vec2(X, y), UI::Vec2(X + W, y + findH), Rgba(36, 40, 48));
            UI::SetCursorScreenPos(UI::Vec2(X + 8, y + 5));
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
    // ---- the page's console: what its scripts wrote (console.log / warn / error) and their errors ----
    if (consoleH > 0.0f) {
        UI::BareNextWindow();
        if (UI::Begin((pre + "_console").c_str(), X, y, W, consoleH)) {
            const auto& dl = UI::GetWindowDrawList();
            dl->AddRectFilled(UI::Vec2(X, y), UI::Vec2(X + W, y + consoleH), Rgba(24, 26, 32));
            const std::vector<ConsoleLine>& lines = t.ConsoleLines();
            UI::SetCursorScreenPos(UI::Vec2(X + 8, y + 4));
            const std::string head = t.Scripts() ? "Console - " + std::to_string((unsigned long long)t.ConsoleCount()) + " lines from this page"
                                                 : std::string(t.Scripting() ? "Console - this page has no scripts" : "Console - scripts are off");
            UI::TextDisabled(head.c_str());
            UI::SameLine();
            if (UI::Button("Clear") && t.Scripts()) t.Scripts()->ClearConsole();
            UI::SameLine();
            if (UI::Button("Close")) consoleOpen = false;
            const float rowH = 16.0f, top = y + 30.0f;
            const std::size_t fit = (std::size_t)std::max(0.0f, (consoleH - 34.0f) / rowH);
            const std::size_t first = lines.size() > fit ? lines.size() - fit : 0;
            float ly = top;
            for (std::size_t i = first; i < lines.size(); ++i, ly += rowH) {
                const ConsoleLine& l = lines[i];
                const UI::Color32 c = l.level == ConsoleLine::Error ? Rgba(240, 110, 100) : l.level == ConsoleLine::Warn ? Rgba(235, 200, 90) : Rgba(210, 215, 225);
                dl->AddText(UI::Vec2(X + 8, ly), c, Fit(l.text, W - 16.0f).c_str());
            }
        }
        UI::End();
        y += consoleH;
    }
    // ---- download shelf: the newest download and its progress ----
    if (shelfH > 0.0f) {
        UI::BareNextWindow();
        if (UI::Begin((pre + "_shelf").c_str(), X, y, W, shelfH)) {
            UI::GetWindowDrawList()->AddRectFilled(UI::Vec2(X, y), UI::Vec2(X + W, y + shelfH), Rgba(44, 48, 58));
            const DownloadItem& d = downloads.Items().back();
            std::string line = d.fileName + "  -  ";
            switch (d.state) {
            case DownloadItem::Active:
                line += SizeText(d.bytes) + (d.total > 0 ? " of " + SizeText(d.total) : std::string());
                break;
            case DownloadItem::Done: line += "saved (" + SizeText(d.bytes) + ") to " + d.path; break;
            case DownloadItem::Failed: line += "failed: " + d.message; break;
            case DownloadItem::Cancelled: line += "cancelled"; break;
            }
            UI::SetCursorScreenPos(UI::Vec2(X + 10, y + 3));
            UI::TextUnformatted(Fit(line, W - 300.0f).c_str());
            UI::SameLine();
            if (d.state == DownloadItem::Active) {
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
        UI::BareNextWindow();
        if (UI::Begin((pre + "_notice").c_str(), X, Y + H - noticeH, W, noticeH)) {
            const auto& dl = UI::GetWindowDrawList();
            dl->AddRectFilled(UI::Vec2(X, Y + H - noticeH), UI::Vec2(X + W, Y + H), 0xF0302A22u);
            dl->AddText(UI::Vec2(X + 8, Y + H - noticeH + 6), 0xFFB8E8F5u, Fit(notice, W - 16.0f).c_str());
        }
        UI::End();
    }

    // ---- the open menu, over everything ----
    if (menu.open) {
        UI::BareNextWindow();
        if (UI::Begin((pre + "_menu").c_str(), X, Y, W, H)) {
            const auto& dl = UI::GetWindowDrawList();
            const float rowH = 24.0f, sepH = 9.0f, mw = 260.0f;
            float mh = 8.0f;
            for (const MenuItem& it : menu.items) mh += it.action ? rowH : sepH;
            const float mx = std::max(X, std::min(menu.at.x, X + W - mw - 4.0f)), my = std::max(Y, std::min(menu.at.y, Y + H - mh - 4.0f));
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
    auto dialog = [&](const std::string& id, float dw, float dh, const std::function<void(float x, float y)>& body) {
        UI::BareNextWindow();
        if (UI::Begin(id.c_str(), X, Y, W, H)) {
            const auto& dl = UI::GetWindowDrawList();
            dl->AddRectFilled(UI::Vec2(X, Y), UI::Vec2(X + W, Y + H), Rgba(0, 0, 0, 110));
            const float dx = X + std::max(0.0f, (W - dw) * 0.5f), dy = Y + std::max(40.0f, (H - dh) * 0.35f);
            dl->AddRectFilled(UI::Vec2(dx, dy), UI::Vec2(dx + dw, dy + dh), Rgba(40, 44, 54), 8.0f);
            dl->AddRect(UI::Vec2(dx, dy), UI::Vec2(dx + dw, dy + dh), Rgba(90, 96, 112), 8.0f, 1.0f);
            body(dx + 18.0f, dy + 14.0f);
        }
        UI::End();
    };
    if (offer.open) {
        dialog(pre + "_download", 520.0f, 170.0f, [&](float dx, float dy) {
            const BrowserTab::DownloadOffer& o = offer.offer;
            UI::SetCursorScreenPos(UI::Vec2(dx, dy));
            UI::Heading("Save this file?");
            UI::SetCursorScreenPos(UI::Vec2(dx, UI::GetCursorScreenPos().y));
            UI::TextUnformatted(Fit(o.fileName, 480).c_str());
            const std::string meta = (o.contentType.empty() ? std::string("unknown type") : o.contentType) + ", " + SizeText(o.size) +
                                     ", from " + HostOf(o.url);
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
        dialog(pre + "_settings", 620.0f, 330.0f, [&](float dx, float dy) {
            UI::SetCursorScreenPos(UI::Vec2(dx, dy));
            UI::Heading("Settings");
            UI::SetCursorScreenPos(UI::Vec2(dx, UI::GetCursorScreenPos().y));
            UI::TextUnformatted("Search address (%s is replaced by the words; empty = no search)");
            UI::SetCursorScreenPos(UI::Vec2(dx, UI::GetCursorScreenPos().y));
            UI::SetNextItemWidth(580.0f);
            UI::InputText("##search", setSearch, (int)sizeof setSearch);
            UI::SetCursorScreenPos(UI::Vec2(dx, UI::GetCursorScreenPos().y));
            if (UI::Button("Use Google")) std::snprintf(setSearch, sizeof setSearch, "%s", kDefaultSearchUrl);
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
                BrowserSettings s;
                s.searchUrl = okay::str::Trim(setSearch);
                s.homePage = okay::str::Trim(setHome);
                s.downloadDir = okay::str::Trim(setDir);
                s.askBeforeDownload = setAsk;
                s.showBookmarksBar = setBar;
                change([s](Profile& p) { p.settings = s; });
                settingsOpen = false;
            }
            UI::SameLine();
            if (UI::Button("Cancel") || UI::IsKeyPressed(UI::KeyId_Escape, false)) settingsOpen = false;
        });
    }
}

} // namespace okay::browser
