#pragma once
// Browser (system "Browser", docs/feature_map/17-launcher-platform.md 17.18): our own web browser's page engine. Step 1 shows
// our own web pages (HTML and a CSS subset, no JavaScript) with links, back / forward and a loading and error state; the
// launcher is its first user, and a standalone browser program will use the same library later (plan:
// docs/platform/browser_plan.md). This is the entry file: BrowserTab is one tab - its history, the current page, and what a
// click does. The parts it uses: Html.hpp (the HTML reader), Css.hpp (style sheets), Layout.hpp (layout, including the
// box an embedded game will get), Url.hpp (addresses).
//
// The tab does no network and no drawing itself, so it is the same in the launcher, a browser program and the tests:
//   * the host asks TakeFetches() each frame and fetches those addresses (NativeFetch in the launcher), then hands the
//     answers back with DeliverPage() / DeliverImage();
//   * the host draws Layout() (okayui/screens/BrowserView.hpp draws it with OkayUI) and passes link clicks to Activate().
// Only pages of the origin the tab was opened on are shown in it (`Activate` answers External for any other http(s)
// address, which the host opens in the system browser); images come from that origin too. Every other scheme
// (javascript:, file:, data: ...) is refused. The browser program (browser/) switches that off with SetAnyOrigin(true): a
// tab there goes to any http(s) site like a regular browser's, and also shows our own pages ("about:newtab" ...,
// Profile.hpp), whose HTML the host hands back like any other page.
// Standard library only.
#include "okay/Browser/Css.hpp"
#include "okay/Browser/Html.hpp"
#include "okay/Browser/Layout.hpp"
#include "okay/Browser/Url.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace okay::browser {

class BrowserTab {
public:
    enum class State { Empty, Loading, Ready, Error };

    /// What a click on a link (or the host's own buttons) led to.
    struct Action {
        enum Kind { None, Navigated, Scrolled, External, Refused } kind = None;
        std::string url;       ///< Navigated / External / Refused: the resolved address
        float scrollY = 0.0f;  ///< Scrolled (and Navigated to "page#id" once loaded): where to scroll to
    };

    static constexpr std::size_t kMaxPageBytes = 4u << 20;   ///< the host fetches at most this much of a page
    static constexpr std::size_t kMaxHistory = 64;
    static constexpr std::size_t kMaxCachedPages = 8;

    /// A page answer that is not a page (a zip, a program ...): the host offers to save it instead (see DeliverDownload).
    struct DownloadOffer {
        std::string url, contentType, fileName;
        std::int64_t size = -1;   ///< -1 = unknown
    };

    /// The browser program: any http(s) site and our own "about:" pages may be shown (default: the origin of Open only).
    void SetAnyOrigin(bool any) { m_anyOrigin = any; }
    bool AnyOrigin() const { return m_anyOrigin; }

    /// Start a new visit at `url` (an absolute http or https address): the history is cleared and the origin is fixed to
    /// this address's. Refused (false, state Error) for any other kind of address.
    bool Open(const std::string& url);
    /// Go to `url` (absolute) as a new history entry, dropping the forward entries.
    void Navigate(const std::string& url);
    bool CanGoBack() const { return m_index > 0; }
    bool CanGoForward() const { return m_index + 1 < m_history.size(); }
    bool GoBack();
    bool GoForward();
    /// Fetch the current page again (also drops it from the cache).
    void Reload();
    /// Stop loading: the page that was shown before stays (its address becomes the current one again).
    void Stop();
    /// A form was sent (Forms.hpp): "GET" goes to `url` (the query already in it), "POST" goes to `url` with `body`
    /// (application/x-www-form-urlencoded) as a new history entry. A POST answer is never cached; Back to it sends it again.
    void Submit(const std::string& url, const std::string& method, const std::string& body);
    /// The host is about to fetch `url`: true and the body when it is the page being loaded by a POST.
    bool PostBody(const std::string& url, std::string& body) const;

    /// A link in the page was clicked: `href` as written in the page. Resolves it against the page's address (and its
    /// <base href>), then: same page with a #fragment -> Scrolled; same origin -> Navigated (the load starts); another
    /// http(s) origin -> External; anything else -> Refused.
    Action Activate(const std::string& href);

    /// The addresses the host should fetch now (each is returned once): the page being loaded, then the images it uses.
    std::vector<std::string> TakeFetches();
    /// The answer for a page fetch. `ok` = the server answered 2xx and `body` is the page; otherwise `error` (or the status)
    /// says what went wrong. An answer for an address that is no longer being loaded is ignored.
    void DeliverPage(const std::string& url, bool ok, int status, const std::string& body, const std::string& error = std::string());
    /// The page being loaded at `from` answered from `to` after redirects: the history entry takes the new address (so the page's
    /// links resolve against it), and a POST that was redirected became a GET. Ignored for any other address, or when `to` may
    /// not be shown in this tab.
    void Redirected(const std::string& from, const std::string& to);
    /// The answer for the page being loaded is not a page but a file: the tab goes back to the page shown before (or is empty)
    /// and keeps the offer for the host (TakeDownloadOffer), which asks the person and saves it.
    void DeliverDownload(const std::string& url, const std::string& contentType, const std::string& fileName, std::int64_t size);
    bool TakeDownloadOffer(DownloadOffer& out) { if (m_offer.url.empty()) return false; out = m_offer; m_offer = DownloadOffer(); return true; }
    /// True once after a page was loaded from the network or our own pages (not from the cache): the host adds it to the history.
    bool TakeVisited() { const bool v = m_visited; m_visited = false; return v; }
    /// The size of an image the host has decoded (0 x 0 = it could not be read). The layout is redone at the next Layout().
    void DeliverImage(const std::string& url, int w, int h);
    /// The size the host gave for `src` (as written in the page), for the layout.
    bool ImageSize(const std::string& src, int& w, int& h) const;
    /// `src` as written in the page, resolved to the address the host was asked to fetch.
    std::string ResolveImage(const std::string& src) const;

    /// The layout of the current page for a view `width` px wide (cached until the width, the page or an image changes).
    /// While another page loads, the last page's layout stays (the view keeps showing it, like other browsers).
    const LayoutResult& Layout(float width, const TextMeasure& measure);
    /// A page is there to draw (shown, or the last one while the next loads).
    bool HasPage() const { return m_state == State::Ready || (m_state == State::Loading && m_doc.root != nullptr && m_shownValid); }

    State GetState() const { return m_state; }
    const std::string& Url() const;           ///< the current address ("" before Open)
    const std::string& Title() const { return m_title; }
    const std::string& Error() const { return m_error; }
    const std::string& Origin() const { return m_origin; }
    const HtmlDocument* Document() const { return m_state == State::Ready ? &m_doc : nullptr; }
    /// The scroll position of the current history entry (the host reads and writes it, so Back returns to where you were).
    float& ScrollY();
    /// Set when a load finished with a #fragment to scroll to; the host scrolls there and clears it (-1 = nothing).
    float PendingScroll() { const float s = m_pendingScroll; m_pendingScroll = -1.0f; return s; }
    /// True once after a page was shown (loaded, or taken from the cache by Back / Forward): the view then scrolls to ScrollY().
    bool TakeJustShown() { const bool j = m_justShown; m_justShown = false; return j; }
    /// True when `url` may be shown in this viewer (same origin as the visit, http or https).
    bool InScope(const std::string& url) const;

private:
    struct Entry { std::string url; float scrollY = 0.0f; bool post = false; std::string body; };
    void StartLoad();
    void ShowPage(const std::string& url, const std::string& body);
    std::vector<Entry> m_history;
    std::size_t m_index = 0;
    std::string m_origin;
    State m_state = State::Empty;
    std::string m_title, m_error, m_loading;   // m_loading: the address (without fragment) being fetched
    HtmlDocument m_doc;
    std::vector<std::string> m_fetches;
    std::map<std::string, std::string> m_cache;    // address -> body (at most kMaxCachedPages)
    std::vector<std::string> m_cacheOrder;
    std::map<std::string, std::pair<int, int>> m_images;   // resolved address -> size
    std::vector<std::string> m_imageRequests;              // resolved addresses already asked for
    LayoutResult m_layout;
    float m_layoutWidth = -1.0f;
    bool m_layoutDirty = true;
    float m_pendingScroll = -1.0f;
    std::string m_pendingFragment;
    bool m_justShown = false;   // the #fragment of the address being shown, until the layout knows where it is
    float m_noScroll = 0.0f;
    bool m_anyOrigin = false;
    bool m_visited = false;
    std::size_t m_shownIndex = 0;   // the history entry whose page m_doc holds
    bool m_shownValid = false;
    DownloadOffer m_offer;
};

/// The page shown for a plain-text address (".txt"): the text in a <pre>, with < and & escaped.
std::string PlainTextPage(const std::string& text);

} // namespace okay::browser
