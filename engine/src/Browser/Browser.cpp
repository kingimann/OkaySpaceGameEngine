// Browser (page engine): one tab's navigation, history and cache (see Browser.hpp).
#include "okay/Browser/Browser.hpp"

#include "okay/Browser/Profile.hpp"

#include <algorithm>
#include <cmath>

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
    m_styleRequests.clear();
    m_fontRequests.clear();
    m_fontFor.clear();
    m_sources.clear();
    m_sheetPtrs.clear();
    m_scripts.reset();
    m_scriptQueue.clear();
    m_scriptRequests.clear();
    m_scriptNav.clear();
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
    m_scripts.reset();   // the old page's scripts go before its tree
    m_scriptQueue.clear();
    m_scriptRequests.clear();
    m_nextScript = 0;
    m_scriptNav.clear();
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
    m_forms.Reset(m_doc);
    m_focus = nullptr;
    m_hover = nullptr;
    m_boxScroll.clear();
    // the style sheets, in page order: <style> elements now, <link rel=stylesheet> when they arrive
    m_sources.clear();
    {
        std::string base = url;
        if (!m_doc.baseHref.empty()) base = ResolveUrl(base, m_doc.baseHref);
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
        RebuildSheets();
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

void BrowserTab::RebuildSheets() {
    m_sheetPtrs.clear();
    for (const SheetSource& s : m_sources) if (s.sheet) m_sheetPtrs.push_back(s.sheet.get());
    m_layoutDirty = true;
}

void BrowserTab::DeliverStyle(const std::string& url, bool ok, const std::string& css) {
    const std::string u = WithoutFragment(url);
    if (m_state != State::Ready && m_state != State::Loading) return;
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
    if (m_scripts && m_scripts->Dispatch(form, "submit")) { note.clear(); return false; }   // the page handled it
    if (m_state != State::Ready) { note = "The page is not loaded."; return false; }
    const Submission s = m_forms.Submit(form, submitter, Url(), m_doc);
    note = s.note;
    if (!s.ok) return false;
    if (!m_anyOrigin && !InScope(s.url)) { note = "The form sends to another site."; return false; }
    Submit(s.url, s.method, s.body);
    return true;
}

void BrowserTab::SetFocus(const Node* el) {
    if (el == m_focus) return;
    m_focus = el;
    if (m_layout.dynamic) m_layoutDirty = true;
}

void BrowserTab::SetHover(const Node* el) {
    if (el == m_hover) return;
    m_hover = el;
    if (m_layout.dynamic) m_layoutDirty = true;
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
        RequestImagesOf(m_layout);
        if (m_scripts) {   // the new layout no longer points at nodes a script removed: free the ones no script holds
            if (m_focus && !m_scripts->Connected(m_focus)) m_focus = nullptr;
            if (m_hover && !m_scripts->Connected(m_hover)) m_hover = nullptr;
            m_scripts->FreeRemoved([this](const Node& n) { m_forms.Forget(n); });
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
    Navigate(target);
    return a;
}

// ---- round 2: JavaScript ----

void BrowserTab::StartScripts(const std::string& baseUrl) {
    if (!m_scripting || !m_doc.root) return;
    // the classic scripts in page order; a page with none (and no on...="" attributes) gets no JavaScript engine at all
    bool handlers = false;
    std::vector<const Node*> todo{m_doc.root.get()};
    while (!todo.empty()) {
        const Node* n = todo.back();
        todo.pop_back();
        if (n->kind == NodeKind::Element) {
            for (const auto& a : n->attrs) if (a.first.size() > 2 && a.first[0] == 'o' && a.first[1] == 'n') handlers = true;
            if (n->name == "script" && m_scriptQueue.size() < 256) {
                const std::string* type = n->Attr("type");
                if (IsJavaScriptType(type ? *type : std::string())) {
                    ScriptSource src;
                    if (const std::string* s = n->Attr("src")) {
                        src.url = WithoutFragment(ResolveUrl(baseUrl, *s));
                        if (InScope(src.url) && !IsInternalPage(src.url)) {
                            src.waiting = true;
                            if (std::find(m_scriptRequests.begin(), m_scriptRequests.end(), src.url) == m_scriptRequests.end()) {
                                m_scriptRequests.push_back(src.url);
                                m_fetches.push_back(src.url);
                            }
                        } else {
                            src.refused = true;   // not fetched: a line in the console says why
                        }
                    } else {
                        src.text = n->TextContent();
                    }
                    m_scriptQueue.push_back(std::move(src));
                }
            }
        }
        for (std::size_t k = n->children.size(); k-- > 0;) todo.push_back(n->children[k].get());
    }
    if (m_scriptQueue.empty() && !handlers) return;
    PageScripts::Hooks hooks;
    hooks.changed = [this]() { m_layoutDirty = true; };
    hooks.title = [this](const std::string& t) { m_title = t; };
    hooks.navigate = [this](const std::string& where) { m_scriptNav = where; };
    m_scripts.reset(new PageScripts(m_doc, m_forms, Url(), std::move(hooks)));
    RunReadyScripts();
}

void BrowserTab::RunReadyScripts() {
    if (!m_scripts) return;
    while (m_nextScript < m_scriptQueue.size()) {
        ScriptSource& s = m_scriptQueue[m_nextScript];
        if (s.waiting) return;   // in order: the rest waits for this file
        ++m_nextScript;
        if (s.refused) {
            m_scripts->AddConsole(ConsoleLine::Warn, "The script " + s.url + " was not run: it is from another site or could not be fetched.");
            continue;
        }
        const std::string text = std::move(s.text);
        s.text.clear();
        m_scripts->RunScript(text, s.url);
        if (!m_scripts) return;
    }
    if (m_nextScript == m_scriptQueue.size()) {
        ++m_nextScript;   // once
        m_scripts->PageLoaded();
    }
}

void BrowserTab::DeliverScript(const std::string& url, bool ok, const std::string& text) {
    const std::string u = WithoutFragment(url);
    bool any = false;
    for (std::size_t i = m_nextScript; i < m_scriptQueue.size(); ++i) {
        ScriptSource& s = m_scriptQueue[i];
        if (!s.waiting || s.url != u) continue;
        s.waiting = false;
        s.text = ok ? text : std::string();
        s.refused = !ok;
        any = true;
    }
    if (any) RunReadyScripts();
}

void BrowserTab::Tick(double ms) {
    if (m_scripts && m_state == State::Ready) m_scripts->Tick(ms);
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
    return m_scripts->Dispatch(el, "click");
}

void BrowserTab::ControlInput(const Node& control) {
    if (!m_scripts || m_state != State::Ready) return;
    const ControlKind k = KindOf(control);
    m_scripts->Dispatch(control, "input");
    if (k == ControlKind::Checkbox || k == ControlKind::Radio || k == ControlKind::Select) m_scripts->Dispatch(control, "change");
}

const std::vector<ConsoleLine>& BrowserTab::ConsoleLines() const {
    static const std::vector<ConsoleLine> none;
    return m_scripts ? m_scripts->Console() : none;
}

} // namespace okay::browser
