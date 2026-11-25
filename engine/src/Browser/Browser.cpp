// Browser (page engine): one tab's navigation, history and cache (see Browser.hpp).
#include "okay/Browser/Browser.hpp"

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
    if (!IsHttp(url)) {
        m_origin.clear();
        m_state = State::Error;
        m_error = "Only http and https addresses can be opened here.";
        return false;
    }
    m_origin = OriginOf(url);
    m_history.push_back({url, 0.0f});
    StartLoad();
    return true;
}

void BrowserTab::Navigate(const std::string& url) {
    if (m_history.empty()) { Open(url); return; }
    m_history.erase(m_history.begin() + (std::ptrdiff_t)(m_index + 1), m_history.end());
    m_history.push_back({url, 0.0f});
    if (m_history.size() > kMaxHistory) m_history.erase(m_history.begin(), m_history.begin() + (std::ptrdiff_t)(m_history.size() - kMaxHistory));
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

void BrowserTab::StartLoad() {
    const std::string page = WithoutFragment(Url());
    m_error.clear();
    m_pendingScroll = -1.0f;
    m_pendingFragment = FragmentOf(Url());
    auto it = m_cache.find(page);
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
    m_cache[page] = text;
    m_cacheOrder.erase(std::remove(m_cacheOrder.begin(), m_cacheOrder.end(), page), m_cacheOrder.end());
    m_cacheOrder.push_back(page);
    while (m_cacheOrder.size() > kMaxCachedPages) { m_cache.erase(m_cacheOrder.front()); m_cacheOrder.erase(m_cacheOrder.begin()); }
    ShowPage(page, text);
}

void BrowserTab::ShowPage(const std::string& url, const std::string& body) {
    const std::string ext = ExtensionOf(url);
    m_doc = (ext == "txt" || ext == "md") ? ParseHtml(PlainTextPage(body)) : ParseHtml(body);
    m_title = m_doc.title;
    m_state = State::Ready;
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
    if (!IsHttp(target)) { a.kind = Action::Refused; return a; }
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
