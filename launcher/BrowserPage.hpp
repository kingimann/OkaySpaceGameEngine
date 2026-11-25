#pragma once
// Browser (docs/feature_map/17-launcher-platform.md 17.18): the launcher's page that shows OkaySpace's own web pages with our
// own page engine (engine/include/okay/Browser/, drawn by okayui/screens/BrowserView.hpp). It is the Browser's first real
// use: a link to one of our own pages (a page on our own server's address, the same origin as the account server) opens
// here instead of in the system browser, while the Settings > Experimental item "Browser" (OKAY_BROWSER=1, on by default)
// and the OkayUI screens are on. Anything else (another site, a download, the item off) opens in the system browser as
// before. The page overlays the current tab; switching tabs closes it.
#include "okay/Browser/Url.hpp"

#include <string>

namespace okl {

/// The rule, kept pure so it is tested (tests/test_browser_url.cpp): `url` is an http(s) address on the same origin as
/// `serverUrl` (our own server) whose path names a page (no extension, or .html, .htm, .txt, .md), not a download.
inline bool IsServerPage(const std::string& url, const std::string& serverUrl) {
    namespace br = okay::browser;
    if (serverUrl.empty() || !br::SameOrigin(url, serverUrl)) return false;
    const br::Url u = br::ParseUrl(url);
    if (u.scheme != "http" && u.scheme != "https") return false;
    const std::string ext = br::ExtensionOf(url);
    if (ext.empty()) return true;
    return ext == "html" || ext == "htm" || ext == "txt" || ext == "md";
}

/// Show `url` in the launcher's Browser page when that is allowed (see the top), else open it with the system browser.
/// Returns true when the launcher shows it.
bool OpenPageLink(const std::string& url);
/// Whether the Browser page is open (it overlays the current tab).
bool BrowserPageOpen();
void CloseBrowserPage();
/// main.cpp says each frame whether an OkayUI frame is open, so a page can be drawn.
void SetBrowserHostAvailable(bool okayFrame);
/// Draw the page in (x, y, w, h) and move its fetches along. Only while an OkayUI frame is open.
void DrawBrowserPage(float x, float y, float w, float h);

} // namespace okl
