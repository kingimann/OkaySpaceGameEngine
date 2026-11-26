#pragma once
// Browser (docs/feature_map/17-launcher-platform.md 17.18): the launcher's Browser page (sidebar: MENU > Browser, owner
// 2026-11-26: "can you allow user to launch browser in the launcher"). It is the whole browser of the browser program, drawn in
// the page area: shared/BrowserApp.hpp (tabs, the address bar with Google search, back / forward / reload / stop, bookmarks
// bar, history, downloads to the Downloads folder after a prompt, find, zoom, the context menu, Play for OkaySpace web games
// run by the player). The profile (history, bookmarks, zoom, settings) is the program's own profile.json, shared with it.
// Links from chat: while the Settings > Experimental item "Browser" (OKAY_BROWSER=1, on by default) is on and OkayUI draws the
// page, a web link opens in a new tab of this page (our own server's pages and other sites alike); off, or without OkayUI, it
// opens in the system browser as before.
#include "okay/Browser/Url.hpp"

#include <string>

namespace okl {

/// The sidebar's Browser entry: its page id (its icon is kIcoGlobe, NavIcons.hpp).
inline constexpr int kBrowserTab = 18;

/// The "Browser" switch's value in the launcher: shared/OwnSwitches.hpp sets OKAY_BROWSER=1 when the item is on (the default)
/// and removes the variable when the person turned it off.
inline bool BrowserSwitchValueOn(const char* value) { return value && value[0] == '1'; }

enum class LinkTarget { SystemBrowser, BrowserPage };
/// Where a web link from chat opens (kept pure so it is tested, tests/test_browser_url.cpp): an http(s) address goes to the
/// Browser page when the switch is on and an OkayUI frame can draw it; anything else, or the switch off, the system browser.
inline LinkTarget RouteLink(const std::string& url, bool switchOn, bool okayUiFrame) {
    if (!switchOn || !okayUiFrame) return LinkTarget::SystemBrowser;
    const okay::browser::Url u = okay::browser::ParseUrl(url);
    if ((u.scheme != "http" && u.scheme != "https") || okay::browser::HostOf(url).empty()) return LinkTarget::SystemBrowser;
    return LinkTarget::BrowserPage;
}

/// What the Browser page shows: the browser, or why not (the switch is off; the launcher draws with Dear ImGui only).
enum class BrowserPageMode { Browser, SwitchedOff, NeedsOkayUi };
inline BrowserPageMode PageMode(bool switchOn, bool okayUiFrame) {
    if (!switchOn) return BrowserPageMode::SwitchedOff;
    return okayUiFrame ? BrowserPageMode::Browser : BrowserPageMode::NeedsOkayUi;
}

/// The page the launcher opens on from the saved one: any sidebar page up to the Browser page, never Admin (11).
inline int RestoreTab(int saved) { return saved >= 0 && saved <= kBrowserTab && saved != 11 ? saved : 0; }

/// The "Browser" switch is on now.
bool BrowserSwitchOn();
/// Open `url` where RouteLink says: a new tab of the Browser page (and the page is asked to show: TakeShowRequest), else the
/// system browser. Returns true when the Browser page shows it.
bool OpenPageLink(const std::string& url);
/// True once after a link asked for the Browser page: main.cpp then shows it (sidebar > Browser).
bool TakeShowRequest();
/// main.cpp says each frame whether an OkayUI frame is open, so the page can be drawn.
void SetBrowserHostAvailable(bool okayFrame);
/// Move the browser's fetches, downloads, games and profile along. Each frame, whichever page is shown.
void PumpBrowser();
/// Draw the browser in (x, y, w, h). Only while an OkayUI frame is open.
void DrawBrowserPage(float x, float y, float w, float h);
/// Stop running games and free the page's textures (before the renderer goes away).
void ShutdownBrowser();

} // namespace okl
