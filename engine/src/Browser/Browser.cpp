// Browser (page engine): one tab's navigation, history and cache (see Browser.hpp).
#include "okay/Browser/Browser.hpp"
#include "okay/Browser/Encoding.hpp"

#include "okay/Browser/Profile.hpp"
#include "okay/Core/StringUtil.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <set>

namespace okay::browser {

namespace {

bool IsHttp(const std::string& url) {
    const Url u = ParseUrl(url);
    return (u.scheme == "http" || u.scheme == "https") && u.hasAuthority && !HostOf(url).empty();
}

} // namespace

bool IsBrowserShortcut(const KeyInfo& k) {
    // the program's own keys (shared/BrowserApp.cpp): with Ctrl (or Cmd), with Alt, and the function keys it uses
    std::string key = k.key;
    if (key.size() == 1) key[0] = (char)std::tolower((unsigned char)key[0]);
    if (key == "F3" || key == "F5" || key == "F6" || key == "F12") return true;
    const bool ctrl = k.ctrl || k.meta;
    if (ctrl && !k.alt) {
        static const char* const kCtrl[] = {"t", "w", "n", "l", "q", "h", "j", "d", "r", "f", "g", "o", "b", "Tab", "PageUp", "PageDown",
                                            "F4", "=", "+", "-", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
        for (const char* c : kCtrl) if (key == c) return true;
    }
    if (k.alt && !ctrl) {
        static const char* const kAlt[] = {"ArrowLeft", "ArrowRight", "Home", "d"};
        for (const char* c : kAlt) if (key == c) return true;
    }
    return false;
}

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
    m_styleRequests.clear();
    m_fontRequests.clear();
    m_fontFor.clear();
    m_sources.clear();
    m_sheetPtrs.clear();
    m_scripts.reset();
    m_scriptQueue.clear();
    m_scriptRequests.clear();
    m_scriptNav.clear();
    m_asyncScripts.clear();
    m_scriptFetches.clear();
    m_openRequests.clear();
    m_dialogs.clear();
    m_traverse = 0;
    m_linkedCss.clear();
    m_initiator.clear();
    m_stylesDirty = false;
    m_forms = FormState();
    m_focus = m_hover = nullptr;
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
    m_history.push_back(Entry{url, 0.0f, false, std::string(), 0, std::string(), std::string()});
    StartLoad();
    return true;
}

void BrowserTab::PushEntry(Entry e) {
    m_history.erase(m_history.begin() + (std::ptrdiff_t)(m_index + 1), m_history.end());
    m_history.push_back(std::move(e));
    if (m_history.size() > kMaxHistory) {
        const std::size_t drop = m_history.size() - kMaxHistory;
        m_history.erase(m_history.begin(), m_history.begin() + (std::ptrdiff_t)drop);
        if (m_shownValid) { if (m_shownIndex >= drop) m_shownIndex -= drop; else m_shownValid = false; }
    }
    m_index = m_history.size() - 1;
}

void BrowserTab::Navigate(const std::string& url) {
    if (m_history.empty()) { Open(url); return; }
    m_initiator.clear();
    PushEntry(Entry{url, 0.0f, false, std::string(), 0, std::string(), std::string()});
    StartLoad();
}

bool BrowserTab::SameDocument(std::size_t index) const {
    return m_shownValid && m_doc.root && index < m_history.size() && m_shownIndex < m_history.size() &&
           m_history[index].doc != 0 && m_history[index].doc == m_history[m_shownIndex].doc;
}

void BrowserTab::Traverse(std::size_t index) {
    m_index = index;
    m_initiator.clear();
    if (!SameDocument(index)) { StartLoad(); return; }
    // HTML 7.4.6.2 "traverse the history" within one page: no load, the page gets popstate with the entry's state
    if (m_state == State::Loading) {
        m_fetches.erase(std::remove(m_fetches.begin(), m_fetches.end(), m_loading), m_fetches.end());
        m_loading.clear();
    }
    m_state = State::Ready;
    m_error.clear();
    m_shownIndex = index;
    m_justShown = true;
    if (m_scripts) {
        m_scripts->SetUrl(m_history[index].url);
        m_scripts->PopState(m_history[index].state);
        AfterScripts();
    }
}

bool BrowserTab::GoBack() {
    if (!CanGoBack()) return false;
    Traverse(m_index - 1);
    return true;
}

bool BrowserTab::GoTo(std::size_t index) {
    if (index >= m_history.size() || index == m_index) return false;
    Traverse(index);
    return true;
}

std::vector<BrowserTab::HistoryItem> BrowserTab::HistoryEntries() const {
    std::vector<HistoryItem> out;
    out.reserve(m_history.size());
    for (const Entry& e : m_history) out.push_back(HistoryItem{e.url, e.title});
    return out;
}

bool BrowserTab::GoForward() {
    if (!CanGoForward()) return false;
    Traverse(m_index + 1);
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
    m_initiator.clear();
    PushEntry(Entry{url, 0.0f, post, post ? body : std::string(), 0, std::string(), std::string()});
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

void BrowserTab::DeliverPage(const std::string& url, bool ok, int status, const std::string& body, const std::string& error,
                             const std::string& contentType) {
    const std::string page = WithoutFragment(url);
    if (m_state != State::Loading || page != m_loading) return;   // an old answer
    m_loading.clear();
    if (!ok) {
        m_state = State::Error;
        m_scripts.reset();
        m_doc = HtmlDocument();
        m_title.clear();
        if (!error.empty()) m_error = error;
        else if (status > 0) m_error = "The server answered " + std::to_string(status) + ".";
        else m_error = "The page could not be loaded.";
        if (status > 0 && error.empty() == false) m_error += " (" + std::to_string(status) + ")";
        m_layoutDirty = true;
        return;
    }
    // The bytes as UTF-8 (Encoding.hpp: BOM, then the Content-Type charset, then <meta charset>, then UTF-8 or windows-1252).
    const std::string text = DecodePage(body.size() > kMaxPageBytes ? body.substr(0, kMaxPageBytes) : body, contentType).text;
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
    m_boxIndex.clear();
    m_boxIndexFor = m_layoutSerial;   // the last layout is the old page's: no element of the new page has a box until the next one
    const std::string ext = ExtensionOf(url);
    m_scripts.reset();   // the old page's scripts go before its tree
    m_scriptQueue.clear();
    m_scriptRequests.clear();
    m_nextScript = 0;
    m_scriptNav.clear();
    m_asyncScripts.clear();
    m_scriptFetches.clear();
    m_openRequests.clear();
    m_dialogs.clear();
    m_traverse = 0;
    m_contentLoaded = m_loadFired = false;
    m_history[m_index].doc = ++m_docSerial;
    m_doc = (ext == "txt" || ext == "md") ? ParseHtml(PlainTextPage(body)) : ParseHtml(body);
    if (m_scripting && m_doc.root) {   // scripting is on: <noscript> is not shown (HTML 4.12.2)
        std::vector<Node*> todo{m_doc.root.get()};
        while (!todo.empty()) {
            Node* n = todo.back();
            todo.pop_back();
            for (std::size_t k = n->children.size(); k-- > 0;) {
                if (n->children[k]->IsElement("noscript")) n->children.erase(n->children.begin() + (std::ptrdiff_t)k);
                else todo.push_back(n->children[k].get());
            }
        }
    }
    m_title = m_doc.title;
    m_history[m_index].title = m_title;
    m_forms.Reset(m_doc);
    m_focus = nullptr;
    m_hover = nullptr;
    m_boxScroll.clear();
    // the style sheets, in page order: <style> elements now, <link rel=stylesheet> when they arrive
    {
        std::string base = url;
        if (!m_doc.baseHref.empty()) base = ResolveUrl(base, m_doc.baseHref);
        CollectSheets(base);
    }
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
    std::string base = url;
    if (!m_doc.baseHref.empty()) base = ResolveUrl(base, m_doc.baseHref);
    StartScripts(base);
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

namespace {
bool IsCheckedFn(const Node& el, const void* user) { return static_cast<const FormState*>(user)->Checked(el); }
// url(x) in a sheet read from `base`: made absolute, so the page resolves it the same wherever the sheet came from.
std::string AbsoluteUrls(const std::string& value, const std::string& base) {
    std::string out;
    std::size_t i = 0;
    while (true) {
        std::size_t at = value.find("url(", i);
        if (at == std::string::npos) { out.append(value, i, std::string::npos); break; }
        const std::size_t close = value.find(')', at);
        if (close == std::string::npos) { out.append(value, i, std::string::npos); break; }
        out.append(value, i, at - i);
        std::string inner = value.substr(at + 4, close - at - 4);
        while (!inner.empty() && (inner.front() == ' ' || inner.front() == '"' || inner.front() == '\'')) inner.erase(inner.begin());
        while (!inner.empty() && (inner.back() == ' ' || inner.back() == '"' || inner.back() == '\'')) inner.pop_back();
        const std::string abs = inner.compare(0, 5, "data:") == 0 ? std::string() : ResolveUrl(base, inner);
        out += "url(\"";
        for (char c : abs) if (c != '"' && c != '\n') out += c;
        out += "\")";
        i = close + 1;
    }
    return out;
}
} // namespace

void BrowserTab::AddSheet(std::size_t at, const std::string& baseUrl, const std::string& css, int depth) {
    SheetSource src;
    src.url = baseUrl;
    src.depth = depth;
    src.sheet.reset(new StyleSheet(ParseStyleSheet(css)));
    StyleSheet& sh = *src.sheet;
    for (Rule& r : sh.rules)
        for (Declaration& d : r.declarations)
            if (d.value.find("url(") != std::string::npos) d.value = AbsoluteUrls(d.value, baseUrl);
    for (FontFace& f : sh.fontFaces) {
        f.url = WithoutFragment(ResolveUrl(baseUrl, f.url));
        if (InScope(f.url) && m_fontRequests.size() < 32 && std::find(m_fontRequests.begin(), m_fontRequests.end(), f.url) == m_fontRequests.end()) {
            m_fontRequests.push_back(f.url);
            m_fontFor[f.url] = f;
            m_fetches.push_back(f.url);
        }
    }
    // @import: fetched, and placed before this sheet
    std::vector<SheetSource> imports;
    if (depth < 4)
        for (const std::string& imp : sh.imports) {
            const std::string abs = WithoutFragment(ResolveUrl(baseUrl, imp));
            if (!InScope(abs) || m_sources.size() + imports.size() >= 64) continue;
            if (std::find(m_styleRequests.begin(), m_styleRequests.end(), abs) == m_styleRequests.end()) {
                m_styleRequests.push_back(abs);
                m_fetches.push_back(abs);
            }
            SheetSource is;
            is.url = abs;
            is.depth = depth + 1;
            imports.push_back(std::move(is));
        }
    at = std::min(at, m_sources.size());
    m_sources.insert(m_sources.begin() + (std::ptrdiff_t)at, std::move(src));
    for (std::size_t k = 0; k < imports.size(); ++k) m_sources.insert(m_sources.begin() + (std::ptrdiff_t)(at + k), std::move(imports[k]));
}

// The page's style sheets in page order, read from the tree as it is now: <style> elements, and <link rel=stylesheet> (fetched
// once; one that already arrived is used from m_linkedCss). Called when a page is shown and when a script changed them.
void BrowserTab::CollectSheets(const std::string& base) {
    m_sources.clear();
    m_stylesDirty = false;
    if (!m_doc.root) { RebuildSheets(); return; }
    std::vector<const Node*> todo{m_doc.root.get()};
    while (!todo.empty()) {
        const Node* n = todo.back();
        todo.pop_back();
        if (n->kind == NodeKind::Element) {
            const std::string* media = n->Attr("media");
            const bool print = media && media->find("print") != std::string::npos && media->find("screen") == std::string::npos &&
                               media->find("all") == std::string::npos;
            if (n->name == "style" && !print) AddSheet(m_sources.size(), base, n->TextContent(), 0);
            else if (n->name == "link" && !print && m_sources.size() < 64) {
                const std::string* rel = n->Attr("rel");
                const std::string* href = n->Attr("href");
                std::string relL = rel ? *rel : std::string();
                for (char& c : relL) if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
                if (href && relL.find("stylesheet") != std::string::npos && relL.find("alternate") == std::string::npos) {
                    const std::string abs = WithoutFragment(ResolveUrl(base, *href));
                    if (InScope(abs) && std::find(m_styleRequests.begin(), m_styleRequests.end(), abs) == m_styleRequests.end()) {
                        m_styleRequests.push_back(abs);
                        m_fetches.push_back(abs);
                    }
                    SheetSource src;
                    src.url = abs;
                    m_sources.push_back(std::move(src));
                }
            }
        }
        for (std::size_t k = n->children.size(); k-- > 0;) todo.push_back(n->children[k].get());
    }
    FillArrived();
    RebuildSheets();
}

void BrowserTab::FillArrived() {
    for (int round = 0; round < 8; ++round) {   // @import inside @import: a few levels
        bool any = false;
        for (std::size_t i = 0; i < m_sources.size(); ++i) {
            if (m_sources[i].sheet) continue;
            auto it = m_linkedCss.find(m_sources[i].url);
            if (it == m_linkedCss.end()) continue;
            const int depth = m_sources[i].depth;
            const std::string u = m_sources[i].url;
            m_sources.erase(m_sources.begin() + (std::ptrdiff_t)i);
            AddSheet(i, u, it->second, depth);
            any = true;
        }
        if (!any) break;
    }
}

void BrowserTab::RebuildSheets() {
    m_sheetPtrs.clear();
    for (const SheetSource& s : m_sources) if (s.sheet) m_sheetPtrs.push_back(s.sheet.get());
    m_layoutDirty = true;
}

void BrowserTab::DeliverStyle(const std::string& url, bool ok, const std::string& css) {
    const std::string u = WithoutFragment(url);
    if (m_state != State::Ready && m_state != State::Loading) return;
    if (ok) {   // kept for the page's later sheet changes (and its next pages); at most 16 MB of them
        std::size_t total = 0;
        for (const auto& kv : m_linkedCss) total += kv.second.size();
        if (total > (16u << 20)) m_linkedCss.clear();
        m_linkedCss[u] = css.size() > (2u << 20) ? css.substr(0, 2u << 20) : css;
    }
    for (std::size_t i = 0; i < m_sources.size(); ++i) {
        if (m_sources[i].sheet || m_sources[i].url != u) continue;
        const int depth = m_sources[i].depth;
        m_sources.erase(m_sources.begin() + (std::ptrdiff_t)i);
        if (ok) AddSheet(i, u, css.size() > (2u << 20) ? css.substr(0, 2u << 20) : css, depth);
        RebuildSheets();
        return;
    }
}

BrowserTab::FetchKind BrowserTab::KindOfFetch(const std::string& url) const {
    const std::string u = WithoutFragment(url);
    if (!m_loading.empty() && u == m_loading) return FetchKind::Page;
    if (std::find(m_styleRequests.begin(), m_styleRequests.end(), u) != m_styleRequests.end()) return FetchKind::Style;
    if (std::find(m_fontRequests.begin(), m_fontRequests.end(), u) != m_fontRequests.end()) return FetchKind::Font;
    if (std::find(m_scriptRequests.begin(), m_scriptRequests.end(), u) != m_scriptRequests.end()) return FetchKind::Script;
    if (!m_history.empty() && u == WithoutFragment(Url())) return FetchKind::Page;
    return FetchKind::Image;
}

const FontFace* BrowserTab::FontFor(const std::string& url) const {
    auto it = m_fontFor.find(WithoutFragment(url));
    return it == m_fontFor.end() ? nullptr : &it->second;
}

bool BrowserTab::SubmitForm(const Node& form, const Node* submitter, std::string& note) {
    if (m_state != State::Ready) { note = "The page is not loaded."; return false; }
    if (m_scripts && m_scripts->Dispatch(form, "submit")) { note.clear(); AfterScripts(); return false; }   // the page handled it
    AfterScripts();
    if (m_state != State::Ready) { note = "The page is not loaded."; return false; }
    const Submission s = m_forms.Submit(form, submitter, Url(), m_doc);
    note = s.note;
    if (!s.ok) return false;
    if (!m_anyOrigin && !InScope(s.url)) { note = "The form sends to another site."; return false; }
    const std::string from = SiteOf(HostOf(Url()));
    Submit(s.url, s.method, s.body);
    m_initiator = from;
    return true;
}

void BrowserTab::SetFocus(const Node* el) { FocusTo(el, false); }

void BrowserTab::FocusTo(const Node* el, bool fromScript) {
    if (el == m_focus) return;
    const Node* old = m_focus;
    m_focus = el;
    if (m_layout.dynamic) m_layoutDirty = true;
    if (m_scripts && m_state == State::Ready) {   // blur / focusout, focus / focusin
        m_scripts->FocusChanged(old, el);
        if (!fromScript) AfterScripts();
    }
}

void BrowserTab::SetHover(const Node* el) {
    if (el == m_hover) return;
    const Node* old = m_hover;
    m_hover = el;
    if (m_layout.dynamic) m_layoutDirty = true;
    if (m_scripts && m_state == State::Ready) {   // UI Events 5.3.3: mouseout at the old element, mouseover at the new one
        MouseInfo mi;
        if (old) { mi.related = el; m_scripts->DispatchMouse(*old, "mouseout", mi); }
        if (el && m_hover == el) { mi.related = old; m_scripts->DispatchMouse(*el, "mouseover", mi); }
        AfterScripts();
    }
}

void BrowserTab::SetViewportHeight(float h) {
    if (std::fabs(h - m_viewH) < 0.5f) return;
    m_viewH = h;
    m_layoutDirty = true;
}

void BrowserTab::RequestImagesOf(const LayoutResult& r) {
    for (const std::string& src : r.images) {
        const std::string abs = WithoutFragment(ResolveImage(src));
        if (InScope(abs) && IsInternalPage(abs) == false && std::find(m_imageRequests.begin(), m_imageRequests.end(), abs) == m_imageRequests.end() &&
            m_imageRequests.size() < 512) {
            m_imageRequests.push_back(abs);
            m_fetches.push_back(abs);
        }
    }
}

const LayoutResult& BrowserTab::Layout(float width, const TextMeasure& measure) {
    if (m_state == State::Loading && HasPage() && !m_layoutDirty && width == m_layoutWidth) return m_layout;   // the last page
    if (m_state != State::Ready) {
        if (m_layoutDirty || width != m_layoutWidth) { m_layout = LayoutResult(); m_layout.width = width; }
        m_layoutWidth = width;
        m_layoutDirty = false;
        return m_layout;
    }
    if (m_stylesDirty) CollectSheets(BaseUrl());
    if (m_layoutDirty || width != m_layoutWidth) {
        LayoutContext ctx;
        ctx.sheets = &m_sheetPtrs;
        ctx.viewportHeight = m_viewH;
        ctx.style.hover = m_hover;
        ctx.style.focus = m_focus;
        ctx.style.isChecked = IsCheckedFn;
        ctx.style.checkedUser = &m_forms;
        ctx.controlText = [this](const Node& n) { return m_forms.Display(n); };
        m_layout = LayoutPage(m_doc, width, measure, [this](const std::string& src, int& w, int& h) { return ImageSize(src, w, h); }, &ctx);
        m_layoutWidth = width;
        m_layoutDirty = false;
        ++m_layoutSerial;
        RequestImagesOf(m_layout);
        if (m_scripts) {   // the new layout no longer points at nodes a script removed: free the ones no script holds
            if (m_focus && !m_scripts->Connected(m_focus)) m_focus = nullptr;
            if (m_hover && !m_scripts->Connected(m_hover)) m_hover = nullptr;
            m_scripts->FreeRemoved([this](const Node& n) { m_forms.Forget(n); ForgetScriptNode(n); });
        }
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
    const std::string from = SiteOf(HostOf(Url()));
    Navigate(target);
    m_initiator = from;   // a link (or a script) of this page started the load: SameSite cookies look at it
    return a;
}

// ---- round 2 and 3: JavaScript ----

std::string BrowserTab::BaseUrl() const {
    std::string base = Url();
    if (!m_doc.baseHref.empty()) base = ResolveUrl(base, m_doc.baseHref);
    return base;
}

std::int64_t BrowserTab::Now(std::int64_t now) const { return now ? now : (std::int64_t)std::time(nullptr); }

// One <script> element into the run order (HTML 4.12.1.1 "prepare the script element"): `at` in the parser's queue, or the
// async list for an async src script. A src file of another site (in the launcher) is not fetched: it is "refused".
void BrowserTab::QueueScript(const Node& el, const std::string& baseUrl, std::size_t at, bool parser) {
    const std::string* type = el.Attr("type");
    if (!IsJavaScriptType(type ? *type : std::string())) return;
    if (m_scriptQueue.size() + m_asyncScripts.size() >= 512) return;
    ScriptSource src;
    src.el = &el;
    if (const std::string* s = el.Attr("src")) {
        src.url = WithoutFragment(ResolveUrl(baseUrl, *s));
        src.async = el.Attr("async") != nullptr || !parser;
        if (InScope(src.url) && !IsInternalPage(src.url) && IsHttp(src.url)) {
            src.waiting = true;
            if (std::find(m_scriptRequests.begin(), m_scriptRequests.end(), src.url) == m_scriptRequests.end()) m_scriptRequests.push_back(src.url);
            if (std::find(m_fetches.begin(), m_fetches.end(), src.url) == m_fetches.end()) m_fetches.push_back(src.url);
        } else {
            src.refused = true;   // not fetched: a line in the console says why
        }
    } else {
        src.text = el.TextContent();
    }
    if (m_scripts) m_scripts->MarkStarted(el);
    if (src.async) { m_asyncScripts.push_back(std::move(src)); return; }
    at = std::min(at, m_scriptQueue.size());
    m_scriptQueue.insert(m_scriptQueue.begin() + (std::ptrdiff_t)at, std::move(src));
}

StorageArea* BrowserTab::StorageFor(bool local) {
    const std::string origin = OriginOf(Url());
    if (origin.empty() || IsInternalPage(Url())) return nullptr;
    if (local) return m_local ? &m_local->Area(origin) : nullptr;
    return &m_session.Area(origin);
}

void BrowserTab::StartScripts(const std::string& baseUrl) {
    if (!m_scripting || !m_doc.root) return;
    // the classic scripts in page order (defer ones after the rest); a page with none (and no on...="" attributes) gets no
    // JavaScript engine at all
    bool handlers = false;
    std::vector<const Node*> scripts;
    std::vector<const Node*> todo{m_doc.root.get()};
    while (!todo.empty()) {
        const Node* n = todo.back();
        todo.pop_back();
        if (n->kind == NodeKind::Element) {
            for (const auto& a : n->attrs) if (a.first.size() > 2 && a.first[0] == 'o' && a.first[1] == 'n') handlers = true;
            if (n->name == "script" && scripts.size() < 256) {
                const std::string* type = n->Attr("type");
                if (IsJavaScriptType(type ? *type : std::string())) scripts.push_back(n);
            }
        }
        for (std::size_t k = n->children.size(); k-- > 0;) todo.push_back(n->children[k].get());
    }
    if (scripts.empty() && !handlers) return;
    PageScripts::Hooks hooks;
    hooks.changed = [this]() { m_layoutDirty = true; };
    hooks.title = [this](const std::string& t) {
        m_title = t;
        if (m_shownValid && m_shownIndex < m_history.size()) m_history[m_shownIndex].title = t;
    };
    hooks.navigate = [this](const std::string& where) { m_scriptNav = where; };
    // fetch() / XMLHttpRequest: the same rules as a <script src>; another origin (the browser program only) gets no cookies
    // and must allow the page to read its answer (CORS), and only "simple" requests are sent there (no preflight yet)
    hooks.request = [this](ScriptRequest& req, std::string& why) {
        if (!IsHttp(req.url) || IsInternalPage(req.url) || !InScope(req.url)) {
            why = "Failed to fetch: the page may not reach " + req.url + " (another site)";
            return false;
        }
        const bool cross = !SameOrigin(req.url, Url());
        if (cross) {
            bool simple = req.method == "GET" || req.method == "HEAD" || req.method == "POST";
            for (const auto& h : req.headers) {
                const std::string n = okay::str::LowerAscii(h.first);
                if (n != "accept" && n != "accept-language" && n != "content-language" && n != "content-type") simple = false;
                if (n == "content-type") {
                    const std::string v = okay::str::LowerAscii(h.second.substr(0, h.second.find(';')));
                    if (v != "text/plain" && v != "application/x-www-form-urlencoded" && v != "multipart/form-data") simple = false;
                }
            }
            if (!simple) { why = "Failed to fetch: a request to another site that needs a CORS preflight is not supported yet"; return false; }
            req.headers.emplace_back("Origin", OriginOf(Url()));
        } else if (m_cookies) {
            CookieStore::Context ctx;
            ctx.sameSite = true;
            const std::string c = m_cookies->Header(req.url, Now(0), ctx);
            if (!c.empty()) req.headers.emplace_back("Cookie", c);
        }
        // the id is the tab's own (the page's would start again at 1 on the next page, and an old page's late answer
        // would then reach the new page's request); the page keeps the id it is given here
        if (++m_nextRequestId > 2000000000) m_nextRequestId = 1;
        req.id = m_nextRequestId;
        m_openRequests[req.id] = OpenRequest{req.url, cross};
        m_scriptFetches.push_back(req);
        return true;
    };
    hooks.getCookie = [this]() {
        if (!m_cookies) return std::string();
        CookieStore::Context ctx;
        ctx.fromScript = true;
        return m_cookies->Header(Url(), Now(0), ctx);
    };
    hooks.setCookie = [this](const std::string& text) {
        if (!m_cookies) return;
        CookieStore::Context ctx;
        ctx.fromScript = true;
        m_cookies->Set(Url(), text, Now(0), ctx);
    };
    hooks.storage = [this](bool local) { return StorageFor(local); };
    hooks.storageChanged = [this]() { if (m_local) m_local->MarkChanged(); };
    hooks.storageQuota = m_local ? m_local->quota : WebStorage::kMaxBytesPerOrigin;
    hooks.pushState = [this](const std::string& url, const std::string& state, bool replace) {
        if (m_state != State::Ready || m_history.empty() || !InScope(url)) return false;
        if (replace) {
            m_history[m_index].url = url;
            m_history[m_index].state = state;
        } else {
            Entry e{url, 0.0f, false, std::string(), m_history[m_shownIndex].doc, state, m_title};
            PushEntry(std::move(e));
            m_shownIndex = m_index;
        }
        return true;
    };
    hooks.traverse = [this](int delta) { m_traverse += delta; };
    hooks.historyLength = [this]() { return (int)m_history.size(); };
    hooks.focus = [this](const Node* el) { FocusTo(el, true); };
    hooks.loadScript = [this](const Node& el, const std::string& url) {
        if (m_asyncScripts.size() >= 256) return;
        ScriptSource src;
        src.el = &el;
        src.url = url;
        src.async = true;
        if (InScope(url) && !IsInternalPage(url) && IsHttp(url)) {
            src.waiting = true;
            if (std::find(m_scriptRequests.begin(), m_scriptRequests.end(), url) == m_scriptRequests.end()) m_scriptRequests.push_back(url);
            m_fetches.push_back(url);   // asked again even when an earlier script used it: its answer may be gone
        } else {
            src.refused = true;
        }
        m_asyncScripts.push_back(std::move(src));
    };
    hooks.styles = [this]() { m_stylesDirty = true; m_layoutDirty = true; };
    hooks.dialog = [this](const PageDialog& d) { if (m_dialogs.size() < kMaxQueuedDialogs) m_dialogs.push_back(d); };
    hooks.computedStyle = [this](const Node& el, ComputedStyle& out) {
        if (m_stylesDirty) CollectSheets(BaseUrl());
        std::vector<const Node*> chain;
        for (const Node* n = &el; n; n = n->parent) if (n->kind == NodeKind::Element) chain.push_back(n);
        StyleContext ctx;
        ctx.hover = m_hover;
        ctx.focus = m_focus;
        ctx.isChecked = IsCheckedFn;
        ctx.checkedUser = &m_forms;
        if (m_layoutWidth > 0) ctx.media.width = m_layoutWidth;
        if (m_viewH > 0) ctx.media.height = m_viewH;
        ComputedStyle st;
        for (std::size_t i = chain.size(); i-- > 0;) st = ComputeStyle(*chain[i], st, m_sheetPtrs, &ctx);
        out = st;
        return true;
    };
    // round 4: the view, element boxes and media queries (CSSOM View), navigator
    hooks.userAgent = m_userAgent;
    hooks.view = [this]() { return View(); };
    hooks.box = [this](const Node& el, PageRect& out) { return BoxOf(el, out); };
    hooks.matchMedia = [this](const std::string& query) {
        MediaEnv env;
        const PageView v = View();
        env.width = v.width;
        env.height = v.height;
        return MediaMatches(ParseMediaQuery(query), env);
    };
    hooks.scrollTo = [this](float, float y) {   // the page scrolls only up and down (the view has no sideways scroll)
        const float most = std::max(0.0f, m_layout.height - (m_viewH > 0 ? m_viewH : m_layoutWidth * 0.75f));
        m_pendingScroll = std::max(0.0f, std::min(std::isfinite(y) ? y : 0.0f, most));
    };
    m_scripts.reset(new PageScripts(m_doc, m_forms, Url(), std::move(hooks)));
    m_scripts->SetHistoryState(m_history[m_index].state);
    std::vector<const Node*> deferred;
    for (const Node* n : scripts) {
        if (n->Attr("src") && n->Attr("defer") && !n->Attr("async")) { deferred.push_back(n); continue; }
        QueueScript(*n, baseUrl, m_scriptQueue.size(), true);
    }
    for (const Node* n : deferred) QueueScript(*n, baseUrl, m_scriptQueue.size(), true);
    RunReadyScripts();
}

void BrowserTab::RunOne(ScriptSource& s) {
    if (!m_scripts) return;
    const Node* el = s.el;
    if (s.refused) {
        m_scripts->AddConsole(ConsoleLine::Warn, "The script " + s.url + " was not run: it is from another site or could not be fetched.");
        if (el && !s.url.empty()) m_scripts->ElementEvent(*el, "error");
        return;
    }
    const std::string text = std::move(s.text);
    s.text.clear();
    m_scripts->RunScript(text, s.url, el, !s.async);
    if (m_scripts && el && !s.url.empty()) m_scripts->ElementEvent(*el, "load");
}

void BrowserTab::RunReadyScripts() {
    if (!m_scripts) return;
    while (m_nextScript < m_scriptQueue.size()) {
        ScriptSource& s = m_scriptQueue[m_nextScript];
        if (s.waiting) break;   // in order: the rest waits for this file
        ++m_nextScript;
        ScriptSource run = std::move(s);
        RunOne(run);
        if (!m_scripts) return;
        // the scripts document.write put in run next, in order
        const std::vector<const Node*> written = m_scripts->TakeWrittenScripts();
        const std::string base = BaseUrl();
        for (std::size_t k = 0; k < written.size(); ++k) QueueScript(*written[k], base, m_nextScript + k, true);
    }
    if (m_nextScript >= m_scriptQueue.size() && !m_contentLoaded) {
        m_contentLoaded = true;
        m_scripts->ContentLoaded();
    }
    RunAsyncScripts();
}

// Async scripts (and the ones a script added) run as their files arrive; the window's load waits for them.
void BrowserTab::RunAsyncScripts() {
    if (!m_scripts) return;
    for (int guard = 0; guard < 512; ++guard) {
        std::size_t i = 0;
        while (i < m_asyncScripts.size() && m_asyncScripts[i].waiting) ++i;
        if (i == m_asyncScripts.size()) break;
        ScriptSource run = std::move(m_asyncScripts[i]);
        m_asyncScripts.erase(m_asyncScripts.begin() + (std::ptrdiff_t)i);
        RunOne(run);
        if (!m_scripts) return;
    }
    if (m_contentLoaded && !m_loadFired && m_asyncScripts.empty()) {
        m_loadFired = true;
        m_scripts->Loaded();
    }
}

void BrowserTab::AfterScripts() {
    if (m_scripts && m_state == State::Ready) RunAsyncScripts();
}

void BrowserTab::ForgetScriptNode(const Node& n) {
    for (ScriptSource& s : m_scriptQueue) if (s.el == &n) s.el = nullptr;
    for (ScriptSource& s : m_asyncScripts) if (s.el == &n) s.el = nullptr;
    if (m_focus == &n) m_focus = nullptr;
    if (m_hover == &n) m_hover = nullptr;
}

void BrowserTab::DeliverScript(const std::string& url, bool ok, const std::string& text) {
    const std::string u = WithoutFragment(url);
    bool any = false;
    auto fill = [&](ScriptSource& s) {
        if (!s.waiting || s.url != u) return;
        s.waiting = false;
        s.text = ok ? text : std::string();
        s.refused = !ok;
        any = true;
    };
    for (std::size_t i = m_nextScript; i < m_scriptQueue.size(); ++i) fill(m_scriptQueue[i]);
    for (ScriptSource& s : m_asyncScripts) fill(s);
    if (any) RunReadyScripts();
}

PageView BrowserTab::View() {
    PageView v;
    if (m_layoutWidth > 0) v.width = m_layoutWidth;
    v.height = m_viewH > 0 ? m_viewH : v.width * 0.75f;
    v.scrollY = m_history.empty() ? 0.0f : ScrollY();
    v.pageWidth = m_layout.width;
    v.pageHeight = m_layout.height;
    return v;
}

bool BrowserTab::BoxOf(const Node& el, PageRect& out) {
    if (m_layoutSerial == 0) return false;
    if (m_boxIndexFor != m_layoutSerial) {   // made once per layout: block boxes, then the runs and images of inline elements
        m_boxIndexFor = m_layoutSerial;
        m_boxIndex.clear();
        auto unite = [this](const Node* n, float x, float y, float w, float h, bool fixed) {
            auto ins = m_boxIndex.emplace(n, PageRect());
            PageRect& r = ins.first->second;
            if (ins.second) { r.x = x; r.y = y; r.w = w; r.h = h; r.fixed = fixed; return; }
            const float x0 = std::min(r.x, x), y0 = std::min(r.y, y), x1 = std::max(r.x + r.w, x + w), y1 = std::max(r.y + r.h, y + h);
            r.x = x0; r.y = y0; r.w = x1 - x0; r.h = y1 - y0;
        };
        std::set<const Node*> blocks;
        for (const BoxInfo& b : m_layout.boxes) {
            if (!b.node) continue;
            const bool first = blocks.insert(b.node).second;
            unite(b.node, b.x, b.y, b.w, b.h, b.fixed);
            if (first) for (int i = 0; i < 4; ++i) m_boxIndex[b.node].border[i] = b.border[i];
        }
        for (const DrawCmd& c : m_layout.cmds) {
            if (!c.node || (c.kind != DrawCmd::Text && c.kind != DrawCmd::Image && c.kind != DrawCmd::Embed)) continue;
            int depth = 0;
            for (const Node* n = c.node->kind == NodeKind::Element ? c.node : c.node->parent; n && n->kind == NodeKind::Element && depth < 64; n = n->parent, ++depth) {
                if (blocks.count(n)) break;   // a block box's own box already holds it
                unite(n, c.x, c.y, c.w, c.h, c.fixed);
            }
        }
        for (const ControlBox& cb : m_layout.controls)
            if (cb.node && !blocks.count(cb.node)) unite(cb.node, cb.x, cb.y, cb.w, cb.h, cb.fixed);
    }
    auto it = m_boxIndex.find(&el);
    if (it == m_boxIndex.end()) return false;
    out = it->second;
    return true;
}

void BrowserTab::Tick(double ms) {
    if (m_scripts && m_state == State::Ready) { m_scripts->Tick(ms); AfterScripts(); }
    if (m_scripts && m_state == State::Ready && m_layoutSerial > 0) {   // a new layout, or the view scrolled or changed size
        const PageView v = View();
        if (m_viewSentSerial != m_layoutSerial || !(v == m_viewSent)) {
            m_viewSentSerial = m_layoutSerial;
            m_viewSent = v;
            m_scripts->ViewChanged();
            AfterScripts();
        }
    }
    if (m_traverse != 0) {   // history.back / forward / go from a script: after it ran, like a person's Back
        const long target = (long)m_index + m_traverse;
        m_traverse = 0;
        if (target >= 0 && target < (long)m_history.size() && (std::size_t)target != m_index && m_state == State::Ready) Traverse((std::size_t)target);
    }
    if (!m_scriptNav.empty()) {
        const std::string where = m_scriptNav;
        m_scriptNav.clear();
        if (m_state == State::Ready) {   // the link rules: same origin in the launcher, never javascript: or file:
            const Action a = Activate(where);
            if (a.kind == Action::Scrolled) m_pendingScroll = a.scrollY;
            else if (a.kind != Action::Navigated && m_scripts)
                m_scripts->AddConsole(ConsoleLine::Warn, "The page asked to go to " + a.url + "; that is not followed in this tab.");
        }
    }
}

bool BrowserTab::Click(const Node& el) {
    if (!m_scripts || m_state != State::Ready) return false;
    const bool prevented = m_scripts->Dispatch(el, "click");
    AfterScripts();
    return prevented;
}

void BrowserTab::ControlInput(const Node& control) {
    if (!m_scripts || m_state != State::Ready) return;
    const ControlKind k = KindOf(control);
    m_scripts->Dispatch(control, "input");
    if (m_scripts && (k == ControlKind::Checkbox || k == ControlKind::Radio || k == ControlKind::Select)) m_scripts->Dispatch(control, "change");
    AfterScripts();
}

bool BrowserTab::KeyEvent(const std::string& type, const KeyInfo& k) {
    if (!m_scripts || m_state != State::Ready) return false;
    const bool prevented = m_scripts->DispatchKey(m_focus, type, k);
    AfterScripts();
    return prevented;
}

bool BrowserTab::PointerEvent(const Node& el, const std::string& type, const MouseInfo& mi) {
    if (!m_scripts || m_state != State::Ready) return false;
    const bool prevented = m_scripts->DispatchMouse(el, type, mi);
    AfterScripts();
    return prevented;
}

bool BrowserTab::TakeDialog(PageDialog& out) {
    if (m_dialogs.empty()) return false;
    out = std::move(m_dialogs.front());
    m_dialogs.erase(m_dialogs.begin());
    return true;
}

const std::vector<ConsoleLine>& BrowserTab::ConsoleLines() const {
    static const std::vector<ConsoleLine> none;
    return m_scripts ? m_scripts->Console() : none;
}

// ---- cookies on the tab's own fetches, and the page's fetch() / XMLHttpRequest

std::string BrowserTab::CookieHeader(const std::string& url, std::int64_t now) const {
    if (!m_cookies) return std::string();
    CookieStore::Context ctx;
    const std::string site = SiteOf(HostOf(url));
    ctx.topLevel = !m_loading.empty() && WithoutFragment(url) == m_loading;
    if (ctx.topLevel) {
        ctx.sameSite = m_initiator.empty() || m_initiator == site;
        ctx.safeMethod = m_history.empty() || !m_history[m_index].post;
    } else {
        ctx.sameSite = site == SiteOf(HostOf(Url()));
    }
    return m_cookies->Header(url, Now(now), ctx);
}

void BrowserTab::ReceiveCookies(const std::string& url, const std::vector<std::string>& setCookies, std::int64_t now) {
    if (!m_cookies) return;
    std::size_t n = 0;
    for (const std::string& sc : setCookies) {
        if (++n > 64) break;
        m_cookies->Set(url, sc, Now(now));
    }
}

std::vector<ScriptRequest> BrowserTab::TakeScriptRequests() {
    std::vector<ScriptRequest> out;
    out.swap(m_scriptFetches);
    return out;
}

void BrowserTab::DeliverScriptResponse(int id, const ScriptResponse& r) {
    auto it = m_openRequests.find(id);
    if (it == m_openRequests.end()) return;   // not this page's (any more)
    const OpenRequest req = it->second;
    m_openRequests.erase(it);
    if (!m_scripts) return;
    ScriptResponse a = r;
    if (a.url.empty()) a.url = req.url;
    // a same-site request that was redirected to another site is checked like one sent there
    const bool cross = req.cross || !SameOrigin(a.url, Url());
    if (a.ok && cross) {   // Fetch 4.9 "CORS check": the other site must allow this page's origin
        std::string allow;
        for (const auto& h : a.headers) if (okay::str::EqualsNoCaseAscii(h.first, "Access-Control-Allow-Origin")) allow = okay::str::Trim(h.second);
        if (allow != "*" && allow != OriginOf(Url())) {
            a = ScriptResponse();
            a.error = "the other site does not let this page read its answer (CORS)";
            m_scripts->AddConsole(ConsoleLine::Error, "Blocked: " + req.url + " does not allow " + OriginOf(Url()) + " to read it (CORS).");
        }
    } else if (a.ok && m_cookies) {
        std::size_t n = 0;
        for (const auto& h : a.headers)
            if (okay::str::EqualsNoCaseAscii(h.first, "Set-Cookie") && ++n <= 64) m_cookies->Set(a.url, h.second, Now(0));
    }
    m_scripts->DeliverResponse(id, a);
    AfterScripts();
}

} // namespace okay::browser
