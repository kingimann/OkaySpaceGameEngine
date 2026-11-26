#pragma once
// Browser (system "Browser", docs/feature_map/17-launcher-platform.md 17.18): our own web browser's page engine. Step 1 shows
// our own web pages (HTML and a CSS subset; JavaScript since round 2, below) with links, back / forward and a loading and error state; the
// launcher is its first user, and a standalone browser program will use the same library later (plan:
// docs/platform/browser_plan.md). This is the entry file: BrowserTab is one tab - its history, the current page, and what a
// click does. The parts it uses: Html.hpp (the HTML reader), Css.hpp (style sheets), Layout.hpp (layout, including the
// box an embedded game will get), Url.hpp (addresses).
//
// Round 2: the page's JavaScript runs on our own engine (Script.hpp, PageScripts): <script> elements in page order once the
// page is read (a <script src> waits for its file, fetched like an image: same origin only in the launcher), then
// DOMContentLoaded and load. The host moves the page's clock with Tick() and passes a person's clicks and typing on
// (Click, ControlInput); a page that cancels a click keeps its link from being followed. Scripting is on by default.
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
//
// Round 3: the page's fetch() / XMLHttpRequest go through the tab (TakeScriptRequests / DeliverScriptResponse, same rules as
// a <script src>), cookies (Cookies.hpp, one store per profile: SetCookieStore) go on every request and come back from every
// answer (CookieHeader / ReceiveCookies for the fetches the host makes itself), localStorage (SetLocalStorage, per profile)
// and sessionStorage (per tab) are kept per site, history.pushState / back / forward move within one page's own entries
// (popstate, no load), the person's keys, mouse and focus reach the page (KeyEvent, PointerEvent, SetFocus, SetHover), and
// alert / confirm / prompt become dialogs the host shows (TakeDialog). Scripts run in page order, with defer ones after the
// rest and async ones when they arrive; document.write puts its text right after its script.
// Standard library only.
#include "okay/Browser/Cookies.hpp"
#include "okay/Browser/Css.hpp"
#include "okay/Browser/Forms.hpp"
#include "okay/Browser/Html.hpp"
#include "okay/Browser/Layout.hpp"
#include "okay/Browser/Script.hpp"
#include "okay/Browser/Storage.hpp"
#include "okay/Browser/Url.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
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

    // ---- round 1 of the browser program: style sheets, web fonts, forms, :hover / :focus, scrolling areas ----
    /// What a fetch the tab asked for is: the page, an image, a style sheet (<link rel=stylesheet>, @import) or a web font.
    enum class FetchKind { Page, Image, Style, Font, Script };
    FetchKind KindOfFetch(const std::string& url) const;
    /// A linked style sheet arrived (`ok` false: it could not be fetched). Its url() values are resolved against its address.
    void DeliverStyle(const std::string& url, bool ok, const std::string& css);
    /// The @font-face rule a font fetch is for (null: none).
    const FontFace* FontFor(const std::string& url) const;
    /// The style sheets of the page in order (the <style> elements and the linked sheets that arrived), already read.
    const std::vector<const StyleSheet*>& Sheets() const { return m_sheetPtrs; }
    /// The form controls' state of the page shown (reset when a page is shown).
    FormState& Forms() { return m_forms; }
    /// A control's value changed: lay out again.
    void FormChanged() { m_layoutDirty = true; }
    /// Send `form` (with the button `submitter`, or null for Enter): a GET or POST load in this tab. False with `note` when it
    /// cannot be sent; `note` may also say what was left out.
    bool SubmitForm(const Node& form, const Node* submitter, std::string& note);
    const Node* Focus() const { return m_focus; }
    const Node* Hover() const { return m_hover; }
    /// The element with the keyboard / under the pointer (null: none). A page whose styles use :hover / :focus is laid out
    /// again when they change.
    void SetFocus(const Node* el);
    void SetHover(const Node* el);
    /// The view's height (vh units and fixed boxes); the layout is redone when it changes.
    void SetViewportHeight(float h);
    /// Scroll offsets of the page's scrolling areas (LayoutResult::scrollers), kept per page.
    ScrollOffsets& BoxScroll() { return m_boxScroll; }

    // ---- round 2: JavaScript (Script.hpp) ----
    /// Run the pages' scripts (default: on). Off: <script> is skipped, <noscript> is shown. Takes effect at the next page.
    void SetScripting(bool on) { m_scripting = on; }
    bool Scripting() const { return m_scripting; }
    /// A <script src> file arrived (`ok` false: it could not be fetched); the scripts waiting for it run now.
    void DeliverScript(const std::string& url, bool ok, const std::string& text);
    /// Move the page's clock on by `ms` (the time since the last call, once a frame): its timers run, and a location change
    /// a script asked for is followed (same rules as a link).
    void Tick(double ms);
    /// A person clicked `el` (the innermost element under the pointer, a link or a control): the page's click listeners run.
    /// True when the page cancelled it (preventDefault): the host then does not follow the link or work the control.
    bool Click(const Node& el);
    /// A person changed a control's value (typed, ticked, chose): the page's input listeners run (and change, for a
    /// checkbox, radio button or select).
    void ControlInput(const Node& control);
    /// The page's console (empty when it has no scripts).
    const std::vector<ConsoleLine>& ConsoleLines() const;
    /// Lines written so far on this page (also ones the console has dropped): the host shows new ones.
    std::uint64_t ConsoleCount() const { return m_scripts ? m_scripts->ConsoleCount() : 0; }
    /// The page's scripts (null: the page has none, scripting is off, or no page is shown).
    PageScripts* Scripts() { return m_scripts.get(); }

    // ---- round 3: fetch, cookies, storage, history, keys and mouse, dialogs ----
    /// The profile's cookies (null: no cookies are kept or sent). Not owned; it must outlive the tab (or be set to null).
    void SetCookieStore(CookieStore* store) { m_cookies = store; }
    CookieStore* Cookies() const { return m_cookies; }
    /// The profile's localStorage (null: off). Not owned. sessionStorage is the tab's own.
    void SetLocalStorage(WebStorage* store) { m_local = store; }
    WebStorage& SessionStorage() { return m_session; }
    /// The Cookie header value for a fetch the host makes for this tab (TakeFetches' addresses): "" when none. `now` is seconds
    /// since 1970 (0 = the system clock).
    std::string CookieHeader(const std::string& url, std::int64_t now = 0) const;
    /// The Set-Cookie header values of an answer the host fetched for this tab (pages, images, styles, scripts).
    void ReceiveCookies(const std::string& url, const std::vector<std::string>& setCookies, std::int64_t now = 0);
    /// The page's fetch() / XMLHttpRequest calls the host should make now (each once). Their headers already carry the
    /// cookies; the host sends them as they are and hands the answer to DeliverScriptResponse.
    std::vector<ScriptRequest> TakeScriptRequests();
    /// The answer for request `id`. Its Set-Cookie headers are stored (and not shown to the page). An answer for a page that
    /// is gone is ignored.
    void DeliverScriptResponse(int id, const ScriptResponse& r);
    /// A key event (`type` keydown / keyup / keypress) for the focused element (or the body). True when the page cancelled it:
    /// the host then does not type the key into a control or scroll.
    bool KeyEvent(const std::string& type, const KeyInfo& k);
    /// A mouse event (`type` mousedown / mouseup / mousemove / dblclick / contextmenu ...) at `el` (the element under the
    /// pointer). True when cancelled. SetHover fires mouseover / mouseout.
    bool PointerEvent(const Node& el, const std::string& type, const MouseInfo& m);
    /// A dialog the page opened (alert / confirm / prompt); the host shows it. False when there is none.
    bool TakeDialog(PageDialog& out);
    static constexpr std::size_t kMaxQueuedDialogs = 8;
    /// The site of the page that started the current load ("" = the person: typed, bookmark, back). For SameSite cookies.
    const std::string& Initiator() const { return m_initiator; }

    // ---- the host's round 3: the Back / Forward history list ----
    /// One entry of the tab's history (the pages visited in this tab, and the entries a page made with history.pushState).
    struct HistoryItem {
        std::string url;
        std::string title;   ///< the page's title when it was last shown ("" when it was never shown in this tab)
    };
    /// Every entry, oldest first (at most kMaxHistory); HistoryIndex() is the current one.
    std::vector<HistoryItem> HistoryEntries() const;
    std::size_t HistoryIndex() const { return m_index; }
    /// Go to entry `index` as Back / Forward would (several steps at once): an entry of the shown page's own (pushState) gets
    /// popstate and loads nothing; any other is loaded. False (nothing happens) for the current entry or one that is not there.
    bool GoTo(std::size_t index);

private:
    // doc: the page (ShowPage count) the entry belongs to; entries made by pushState share their page's number
    struct Entry { std::string url; float scrollY = 0.0f; bool post = false; std::string body; std::uint64_t doc = 0; std::string state; std::string title; };
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
    // style sheets in page order: inline ones are read at once, linked ones when they arrive
    struct SheetSource { std::string url; std::unique_ptr<StyleSheet> sheet; int depth = 0; };
    std::vector<SheetSource> m_sources;
    std::vector<const StyleSheet*> m_sheetPtrs;
    std::vector<std::string> m_styleRequests, m_fontRequests;
    std::map<std::string, FontFace> m_fontFor;
    void RebuildSheets();
    void AddSheet(std::size_t at, const std::string& baseUrl, const std::string& css, int depth);
    void RequestImagesOf(const LayoutResult& r);
    FormState m_forms;
    const Node* m_focus = nullptr;
    const Node* m_hover = nullptr;
    float m_viewH = 0.0f;
    ScrollOffsets m_boxScroll;
    // the page's scripts, in page order: inline text, or a src file that is fetched first
    struct ScriptSource {
        std::string url, text;
        bool waiting = false, refused = false;   // refused: another site, or the fetch failed
        const Node* el = nullptr;                // the <script> element (null once the page changed)
        bool async = false;                      // runs when it arrives, out of order (async, or added by a script)
    };
    std::unique_ptr<PageScripts> m_scripts;
    std::vector<ScriptSource> m_scriptQueue;
    std::size_t m_nextScript = 0;
    std::vector<std::string> m_scriptRequests;
    std::string m_scriptNav;   // location.href set by a script: followed at the next Tick (never inside a script)
    bool m_scripting = true;
    void StartScripts(const std::string& baseUrl);
    void RunReadyScripts();
    // round 3
    void RunOne(ScriptSource& s);
    void QueueScript(const Node& el, const std::string& baseUrl, std::size_t at, bool parser);
    void RunAsyncScripts();
    void AfterScripts();             // the effects of a host entry: written scripts, styles, focus
    void CollectSheets(const std::string& base);
    std::string BaseUrl() const;
    std::int64_t Now(std::int64_t now) const;
    CookieStore* m_cookies = nullptr;
    WebStorage* m_local = nullptr;
    WebStorage m_session;
    std::vector<ScriptSource> m_asyncScripts;
    std::vector<ScriptRequest> m_scriptFetches;   // asked by the page, not taken by the host yet
    struct OpenRequest { std::string url; bool cross = false; };
    std::map<int, OpenRequest> m_openRequests;    // asked by the page, not answered (cross: another origin, CORS rules)
    std::vector<PageDialog> m_dialogs;
    int m_traverse = 0;                           // history.back / forward asked by a script
    bool m_stylesDirty = false;
    bool m_contentLoaded = false, m_loadFired = false;
    std::uint64_t m_docSerial = 0;
    std::string m_initiator;
    std::map<std::string, std::string> m_linkedCss;   // linked sheets that arrived (address -> text), for CollectSheets
    bool SameDocument(std::size_t index) const;
    void PushEntry(Entry e);                      // a new last entry after the current one (forward entries go)
    void Traverse(std::size_t index);             // back / forward: within the page's own entries, or a load
    int m_nextRequestId = 0;                      // fetch / XMLHttpRequest ids are the tab's own, so an old page's answer never reaches a new page
    void FocusTo(const Node* el, bool fromScript);
    void ForgetScriptNode(const Node& n);         // a freed node: no queued script points at it any more
    void FillArrived();                           // linked sheets that already arrived go in their places
    StorageArea* StorageFor(bool local);
};

/// Keys the browser keeps for itself (Ctrl+T, Ctrl+W, Ctrl+L, Ctrl+Tab, Alt+Left ...: the program's shortcuts): the host does not
/// pass them to the page, so a page can never cancel them.
bool IsBrowserShortcut(const KeyInfo& k);

/// The page shown for a plain-text address (".txt"): the text in a <pre>, with < and & escaped.
std::string PlainTextPage(const std::string& text);

} // namespace okay::browser
