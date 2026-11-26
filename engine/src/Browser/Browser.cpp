// Browser (page engine): one tab's navigation, history and cache (see Browser.hpp).
#include "okay/Browser/Browser.hpp"

#include "okay/Browser/Profile.hpp"

#include <algorithm>

namespace okay::browser {

namespace {

bool IsHttp(const std::string& url) {
    const Url u = ParseUrl(url);
    return (u.scheme == "http" || u.scheme == "https") && u.hasAuthority && !HostOf(url).empty();
}

} // namespace

std::string PlainTextPage(const std::string& text) {
    std::string out = "<!DOCTYPE html><html><body><pre style=\"white-space: pre-wrap\">";
    out.reserve(out.size() + text.size() + 32);
    for (char c : text) {
        if (c == '<') out += "&lt;";
        else if (c == '&') out += "&amp;";
        else out += c;
    }
    out += "</pre></body></html>";
    return out;
}

const std::string& BrowserTab::Url() const {
    static const std::string none;
    return m_history.empty() ? none : m_history[m_index].url;
}

float& BrowserTab::ScrollY() {
    if (m_history.empty()) { m_noScroll = 0.0f; return m_noScroll; }
    return m_history[m_index].scrollY;
}

bool BrowserTab::InScope(const std::string& url) const {
    if (m_anyOrigin) return IsHttp(url) || IsInternalPage(url);
    return IsHttp(url) && !m_origin.empty() && OriginOf(url) == m_origin;
}

bool BrowserTab::Open(const std::string& url) {
    m_history.clear();
    m_index = 0;
    m_cache.clear();
    m_cacheOrder.clear();
    m_fetches.clear();
    m_images.clear();
    m_imageRequests.clear();
    m_doc = HtmlDocument();
    m_title.clear();
    m_loading.clear();
    m_layoutDirty = true;
    m_shownValid = false;
    m_offer = DownloadOffer();
    if (!IsHttp(url) && !(m_anyOrigin && IsInternalPage(url))) {
        m_origin.clear();
        m_state = State::Error;
        m_error = "Only http and https addresses can be opened here.";
        return false;
    }
    m_origin = OriginOf(url);
    m_history.push_back(Entry{url, 0.0f, false, std::string()});
    StartLoad();
    return true;
}

void BrowserTab::Navigate(const std::string& url) {
    if (m_history.empty()) { Open(url); return; }
    m_history.erase(m_history.begin() + (std::ptrdiff_t)(m_index + 1), m_history.end());
    m_history.push_back(Entry{url, 0.0f, false, std::string()});
    if (m_history.size() > kMaxHistory) {
        const std::size_t drop = m_history.size() - kMaxHistory;
        m_history.erase(m_history.begin(), m_history.begin() + (std::ptrdiff_t)drop);
        if (m_shownValid) { if (m_shownIndex >= drop) m_shownIndex -= drop; else m_shownValid = false; }
    }
    m_index = m_history.size() - 1;
    StartLoad();
}

bool BrowserTab::GoBack() {
    if (!CanGoBack()) return false;
    --m_index;
    StartLoad();
    return true;
}

bool BrowserTab::GoForward() {
    if (!CanGoForward()) return false;
    ++m_index;
    StartLoad();
    return true;
}

void BrowserTab::Reload() {
    if (m_history.empty()) return;
    const std::string page = WithoutFragment(Url());
    m_cache.erase(page);
    m_cacheOrder.erase(std::remove(m_cacheOrder.begin(), m_cacheOrder.end(), page), m_cacheOrder.end());
    StartLoad();
}

void BrowserTab::Stop() {
    if (m_state != State::Loading) return;
    m_fetches.erase(std::remove(m_fetches.begin(), m_fetches.end(), m_loading), m_fetches.end());
    m_loading.clear();
    if (m_shownValid && m_doc.root && m_shownIndex < m_history.size()) {
        m_index = m_shownIndex;
        m_state = State::Ready;
    } else {
        m_state = State::Error;
        m_error = "Loading was stopped.";
    }
}

void BrowserTab::Submit(const std::string& url, const std::string& method, const std::string& body) {
    const bool post = method == "POST" || method == "post";
    if (m_history.empty()) { Open(url); if (post && !m_history.empty()) { m_history.back().post = true; m_history.back().body = body; } return; }
    m_history.erase(m_history.begin() + (std::ptrdiff_t)(m_index + 1), m_history.end());
    m_history.push_back(Entry{url, 0.0f, post, post ? body : std::string()});
    if (m_history.size() > kMaxHistory) {
        const std::size_t drop = m_history.size() - kMaxHistory;
        m_history.erase(m_history.begin(), m_history.begin() + (std::ptrdiff_t)drop);
        if (m_shownValid) { if (m_shownIndex >= drop) m_shownIndex -= drop; else m_shownValid = false; }
    }
    m_index = m_history.size() - 1;
    if (post) {   // a POST answer is never taken from the cache
        const std::string page = WithoutFragment(url);
        m_cache.erase(page);
        m_cacheOrder.erase(std::remove(m_cacheOrder.begin(), m_cacheOrder.end(), page), m_cacheOrder.end());
    }
    StartLoad();
}

bool BrowserTab::PostBody(const std::string& url, std::string& body) const {
    if (m_history.empty() || m_state != State::Loading || WithoutFragment(url) != m_loading) return false;
    const Entry& e = m_history[m_index];
    if (!e.post) return false;
    body = e.body;
    return true;
}

void BrowserTab::Redirected(const std::string& from, const std::string& to) {
    if (m_state != State::Loading || m_history.empty() || WithoutFragment(from) != m_loading || to.empty()) return;
    if (!IsHttp(to) || !InScope(to)) return;
    Entry& e = m_history[m_index];
    const std::string frag = FragmentOf(e.url);
    e.url = (FragmentOf(to).empty() && !frag.empty()) ? WithoutFragment(to) + "#" + frag : to;
    e.post = false;
    e.body.clear();
    m_loading = WithoutFragment(to);
}

void BrowserTab::DeliverDownload(const std::string& url, const std::string& contentType, const std::string& fileName, std::int64_t size) {
    const std::string page = WithoutFragment(url);
    if (m_state != State::Loading || page != m_loading) return;
    m_loading.clear();
    m_offer.url = url;
    m_offer.contentType = contentType;
    m_offer.fileName = fileName;
    m_offer.size = size;
    // like other browsers, a download is not a page: the history entry that led to it goes away
    if (m_index + 1 == m_history.size() && m_history.size() > 1 && m_shownValid && m_shownIndex < m_index) {
        m_history.pop_back();
        m_index = m_shownIndex;
        m_state = State::Ready;
    } else if (m_shownValid && m_doc.root && m_shownIndex < m_history.size()) {
        m_index = m_shownIndex;
        m_state = State::Ready;
    } else {
        m_state = State::Empty;
        m_history.clear();
        m_index = 0;
    }
}

void BrowserTab::StartLoad() {
    const std::string page = WithoutFragment(Url());
    m_error.clear();
    m_pendingScroll = -1.0f;
    m_pendingFragment = FragmentOf(Url());
    auto it = m_history[m_index].post ? m_cache.end() : m_cache.find(page);
    if (it != m_cache.end()) {
        m_loading.clear();
        const std::string body = it->second;   // ShowPage may change the cache
        ShowPage(page, body);
        return;
    }
    m_state = State::Loading;
    m_loading = page;
    m_fetches.erase(std::remove(m_fetches.begin(), m_fetches.end(), page), m_fetches.end());
    m_fetches.insert(m_fetches.begin(), page);
}

std::vector<std::string> BrowserTab::TakeFetches() {
    std::vector<std::string> out;
    out.swap(m_fetches);
    return out;
}

void BrowserTab::DeliverPage(const std::string& url, bool ok, int status, const std::string& body, const std::string& error) {
    const std::string page = WithoutFragment(url);
    if (m_state != State::Loading || page != m_loading) return;   // an old answer
    m_loading.clear();
    if (!ok) {
        m_state = State::Error;
        m_doc = HtmlDocument();
        m_title.clear();
        if (!error.empty()) m_error = error;
        else if (status > 0) m_error = "The server answered " + std::to_string(status) + ".";
        else m_error = "The page could not be loaded.";
        if (status > 0 && error.empty() == false) m_error += " (" + std::to_string(status) + ")";
        m_layoutDirty = true;
        return;
    }
    const std::string text = body.size() > kMaxPageBytes ? body.substr(0, kMaxPageBytes) : body;
    if (!m_history[m_index].post && !IsInternalPage(page)) {   // our own pages are made again each time (they change)
        m_cache[page] = text;
        m_cacheOrder.erase(std::remove(m_cacheOrder.begin(), m_cacheOrder.end(), page), m_cacheOrder.end());
        m_cacheOrder.push_back(page);
        while (m_cacheOrder.size() > kMaxCachedPages) { m_cache.erase(m_cacheOrder.front()); m_cacheOrder.erase(m_cacheOrder.begin()); }
    }
    m_visited = true;
    ShowPage(page, text);
}

void BrowserTab::ShowPage(const std::string& url, const std::string& body) {
    const std::string ext = ExtensionOf(url);
    m_doc = (ext == "txt" || ext == "md") ? ParseHtml(PlainTextPage(body)) : ParseHtml(body);
    m_title = m_doc.title;
    m_state = State::Ready;
    m_shownIndex = m_index;
    m_shownValid = true;
    m_justShown = true;
    m_layoutDirty = true;
    // the images this page uses (same origin only)
    std::vector<const Node*> todo{m_doc.root.get()};
    while (!todo.empty()) {
        const Node* n = todo.back();
        todo.pop_back();
        if (n->IsElement("img")) if (const std::string* src = n->Attr("src")) {
            const std::string abs = WithoutFragment(ResolveImage(*src));
            if (InScope(abs) && std::find(m_imageRequests.begin(), m_imageRequests.end(), abs) == m_imageRequests.end() &&
                m_imageRequests.size() < 512) {
                m_imageRequests.push_back(abs);
                m_fetches.push_back(abs);
            }
        }
        for (std::size_t k = n->children.size(); k-- > 0;) todo.push_back(n->children[k].get());
    }
}

std::string BrowserTab::ResolveImage(const std::string& src) const {
    std::string base = Url();
    if (!m_doc.baseHref.empty()) base = ResolveUrl(base, m_doc.baseHref);
    return ResolveUrl(base, src);
}

void BrowserTab::DeliverImage(const std::string& url, int w, int h) {
    m_images[WithoutFragment(url)] = {std::max(0, w), std::max(0, h)};
    m_layoutDirty = true;
}

bool BrowserTab::ImageSize(const std::string& src, int& w, int& h) const {
    auto it = m_images.find(WithoutFragment(ResolveImage(src)));
    if (it == m_images.end() || it->second.first <= 0 || it->second.second <= 0) return false;
    w = it->second.first;
    h = it->second.second;
    return true;
}

const LayoutResult& BrowserTab::Layout(float width, const TextMeasure& measure) {
    if (m_state == State::Loading && HasPage() && !m_layoutDirty && width == m_layoutWidth) return m_layout;   // the last page
    if (m_state != State::Ready) {
        if (m_layoutDirty || width != m_layoutWidth) { m_layout = LayoutResult(); m_layout.width = width; }
        m_layoutWidth = width;
        m_layoutDirty = false;
        return m_layout;
    }
    if (m_layoutDirty || width != m_layoutWidth) {
        m_layout = LayoutPage(m_doc, width, measure, [this](const std::string& src, int& w, int& h) { return ImageSize(src, w, h); });
        m_layoutWidth = width;
        m_layoutDirty = false;
    }
    if (!m_pendingFragment.empty()) {
        for (const auto& a : m_layout.anchors) if (a.first == m_pendingFragment) { m_pendingScroll = a.second; break; }
        m_pendingFragment.clear();
    }
    return m_layout;
}

BrowserTab::Action BrowserTab::Activate(const std::string& href) {
    Action a;
    if (m_history.empty()) return a;
    std::string base = Url();
    // A link that is only "#name" stays on this page even under a <base href> (the standard would resolve it against the
    // base address; on our own pages that would reload another page, which nobody means).
    const bool fragmentOnly = !href.empty() && href[0] == '#';
    if (!m_doc.baseHref.empty() && !fragmentOnly) base = ResolveUrl(base, m_doc.baseHref);
    const std::string target = ResolveUrl(base, href);
    a.url = target;
    // our own pages only link to each other: a web page may not open them (they show the person's history and bookmarks)
    if (!IsHttp(target) && !(m_anyOrigin && IsInternalPage(target) && IsInternalPage(Url()))) { a.kind = Action::Refused; return a; }
    if (!InScope(target)) { a.kind = Action::External; return a; }
    const std::string frag = FragmentOf(target);
    if (WithoutFragment(target) == WithoutFragment(Url()) && !frag.empty() && m_state == State::Ready) {
        a.kind = Action::Scrolled;
        for (const auto& an : m_layout.anchors) if (an.first == frag) { a.scrollY = an.second; break; }
        return a;
    }
    a.kind = Action::Navigated;
    Navigate(target);
    return a;
}

} // namespace okay::browser
