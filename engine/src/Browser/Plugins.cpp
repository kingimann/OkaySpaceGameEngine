// Browser (page engine): browser plugins (see Plugins.hpp). The manifest is JSON (RFC 8259, okay/Core/Json.hpp); hosts come
// from RFC 3986 addresses (Url.hpp); InertCss knows CSS Syntax Level 3's at-rules, strings, comments and url() tokens. Our own
// design: no plugin system's code or manifest format was copied.
#include "okay/Browser/Plugins.hpp"

#include "okay/Browser/Url.hpp"
#include "okay/Core/Json.hpp"
#include "okay/Core/StringUtil.hpp"

#include <algorithm>
#include <cctype>

namespace okay::browser {

namespace {

using okay::str::LowerAscii;
bool HttpAddress(const std::string& u) {
    const std::string l = LowerAscii(u);
    return (l.compare(0, 7, "http://") == 0 || l.compare(0, 8, "https://") == 0) && u.size() <= 4096 &&
           u.find_first_of(std::string("\r\n\t\0 ", 5)) == std::string::npos;
}
std::string Clip(const std::string& s, std::size_t n) {
    if (s.size() <= n) return s;
    std::size_t k = n;
    while (k > 0 && ((unsigned char)s[k] & 0xC0) == 0x80) --k;   // not in the middle of a UTF-8 character
    return s.substr(0, k);
}
std::string Str(const json::Value& v, const char* key, std::size_t n) {
    const json::Value* x = v.Get(key);
    return x && x->IsString() ? Clip(x->string, n) : std::string();
}
std::vector<std::string> Strings(const json::Value& v, const char* key, std::size_t maxItems, std::size_t n) {
    std::vector<std::string> out;
    const json::Value* x = v.Get(key);
    if (x && x->IsString()) out.push_back(Clip(x->string, n));
    else if (x && x->IsArray()) for (const json::Value& e : x->array) if (e.IsString() && out.size() < maxItems) out.push_back(Clip(e.string, n));
    return out;
}
std::string Escape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '&') o += "&amp;"; else if (c == '<') o += "&lt;"; else if (c == '>') o += "&gt;";
        else if (c == '"') o += "&quot;"; else if (c == '\'') o += "&#39;"; else o += c;
    }
    return o;
}
// '*' matches any run; everything else one character, ASCII case ignored (one star to go back to: no exponential backtracking).
bool Glob(const std::string& s, const std::string& p) {
    std::size_t i = 0, j = 0, star = std::string::npos, mark = 0;
    auto eq = [](char a, char b) { if (a >= 'A' && a <= 'Z') a = (char)(a + 32); if (b >= 'A' && b <= 'Z') b = (char)(b + 32); return a == b; };
    while (i < s.size()) {
        if (j < p.size() && p[j] == '*') { star = j++; mark = i; continue; }
        if (j < p.size() && eq(s[i], p[j])) { ++i; ++j; continue; }
        if (star == std::string::npos) return false;
        j = star + 1;
        i = ++mark;
    }
    while (j < p.size() && p[j] == '*') ++j;
    return j == p.size();
}
std::string HostOnly(const std::string& url) {
    std::string h = LowerAscii(HostOf(url));
    const std::size_t at = h.rfind('@');
    if (at != std::string::npos) h = h.substr(at + 1);
    if (!h.empty() && h[0] != '[') { const std::size_t colon = h.find(':'); if (colon != std::string::npos) h = h.substr(0, colon); }
    return h;
}
bool HostUnder(const std::string& host, const std::string& domain) {
    if (domain.empty()) return false;
    return host == domain || (host.size() > domain.size() && host.compare(host.size() - domain.size(), domain.size(), domain) == 0 &&
                              host[host.size() - domain.size() - 1] == '.');
}
// Skip to the end of a quoted string or a bracket run that starts at css[j] (j is past the opening part); returns the stop index.
std::size_t SkipTo(const std::string& css, std::size_t j, char stop) {
    char q = 0;
    while (j < css.size() && (q || css[j] != stop)) {
        if (q) { if (css[j] == '\\') ++j; else if (css[j] == q) q = 0; }
        else if (css[j] == '"' || css[j] == '\'') q = css[j];
        ++j;
    }
    return j;
}

} // namespace

unsigned PermissionFromName(const std::string& n) {
    if (n == "toolbar") return kPermToolbar;
    if (n == "pageActions") return kPermPageActions;
    if (n == "block") return kPermBlock;
    if (n == "styles") return kPermStyles;
    if (n == "newTab") return kPermNewTab;
    return 0;
}
std::string PermissionNames(unsigned p) {
    static const char* names[] = {"toolbar", "pageActions", "block", "styles", "newTab"};
    std::string out;
    for (int k = 0; k < 5; ++k) if (p & (1u << k)) { if (!out.empty()) out += ", "; out += names[k]; }
    return out;
}

bool UrlMatchesPattern(const std::string& url, const std::string& pattern) {
    if (pattern.empty() || url.empty()) return false;
    if (pattern.compare(0, 2, "||") == 0) {
        std::string host = LowerAscii(pattern.substr(2));
        const std::size_t end = host.find_first_of("/^*");
        if (end != std::string::npos) host = host.substr(0, end);
        return HostUnder(HostOnly(url), host);
    }
    return Glob(url, pattern);
}
bool SiteMatches(const std::string& url, const std::string& siteIn) {
    const std::string site = LowerAscii(siteIn);
    const std::string host = HostOnly(url);
    if (site == "*") return !host.empty();
    if (site.compare(0, 2, "*.") == 0) return HostUnder(host, site.substr(2));
    return !host.empty() && host == site;
}

std::string InertCss(const std::string& css) {
    std::string out;
    out.reserve(css.size());
    std::size_t i = 0;
    const std::size_t n = css.size();
    while (i < n) {
        const char c = css[i];
        if (c == '/' && i + 1 < n && css[i + 1] == '*') {   // a comment: kept as it is
            const std::size_t e = css.find("*/", i + 2);
            const std::size_t stop = e == std::string::npos ? n : e + 2;
            out.append(css, i, stop - i);
            i = stop;
            continue;
        }
        if (c == '"' || c == '\'') {   // a string: kept (it loads nothing by itself)
            std::size_t j = i + 1;
            while (j < n && css[j] != c && css[j] != '\n') { if (css[j] == '\\') ++j; ++j; }
            j = std::min(n, j + 1);
            out.append(css, i, j - i);
            i = j;
            continue;
        }
        if (c == '@' && LowerAscii(css.substr(i, 7)) == "@import") { i = std::min(n, SkipTo(css, i, ';') + 1); continue; }   // the whole rule goes
        const bool identBefore = i > 0 && (std::isalnum((unsigned char)css[i - 1]) || css[i - 1] == '-' || css[i - 1] == '_');
        if (!identBefore && (c == 'u' || c == 'U') && LowerAscii(css.substr(i, 4)) == "url(") { out += "none"; i = std::min(n, SkipTo(css, i + 4, ')') + 1); continue; }
        if (!identBefore && (c == 'i' || c == 'I') && LowerAscii(css.substr(i, 10)) == "image-set(") { out += "none"; i = std::min(n, SkipTo(css, i + 10, ')') + 1); continue; }
        out += c;
        ++i;
    }
    return out;
}

bool BrowserPlugins::Load(const std::string& manifest, const std::string& source, std::string& why) {
    if (manifest.size() > m_limits.maxManifestBytes) { why = "the manifest is too big"; return false; }
    json::Value root;
    std::string err;
    json::Limits jl;
    jl.maxDepth = 16;
    if (!json::Parse(manifest, root, &err, jl) || !root.IsObject()) { why = "the manifest is not a JSON object" + (err.empty() ? std::string() : ": " + err); return false; }
    const std::size_t tn = m_limits.maxTextBytes, items = m_limits.maxItems;
    BrowserPlugin p;
    p.id = Str(root, "id", tn);
    p.name = Str(root, "name", tn);
    p.version = Str(root, "version", 40);
    p.description = Str(root, "description", 1000);
    p.source = source;
    if (p.id.empty() || p.name.empty()) { why = "the manifest has no id or no name"; return false; }
    for (char c : p.id) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) { why = "the id may only have a-z, 0-9, - _ ."; return false; }
    for (const std::string& name : Strings(root, "permissions", 16, 40)) {
        const unsigned bit = PermissionFromName(name);
        if (bit) p.requested |= bit;
        else p.problems.push_back("unknown permission '" + name + "' (ignored)");
    }
    auto buttons = [&](const char* key, unsigned perm, std::vector<PluginButton>& out) {
        const json::Value* list = root.Get(key);
        if (!list) return;
        if (!(p.requested & perm)) { p.problems.push_back(std::string("'") + key + "' needs the permission '" + PermissionNames(perm) + "'"); return; }
        if (!list->IsArray()) { p.problems.push_back(std::string("'") + key + "' is not a list"); return; }
        for (const json::Value& b : list->array) {
            if (out.size() >= items) { p.problems.push_back(std::string("too many ") + key); break; }
            PluginButton btn;
            btn.plugin = p.id;
            btn.id = Str(b, "id", tn);
            btn.title = Str(b, "title", tn);
            btn.tooltip = Str(b, "tooltip", 1000);
            btn.open = Str(b, "open", 4097);
            btn.toggleStyle = Str(b, "toggleStyle", m_limits.maxCssBytes + 1);
            btn.sites = Strings(b, "sites", items, tn);
            if (btn.id.empty() || btn.title.empty()) { p.problems.push_back(std::string("a ") + key + " entry has no id or title"); continue; }
            if (!btn.open.empty() && !HttpAddress(btn.open)) { p.problems.push_back("'" + btn.id + "' may only open http or https addresses"); continue; }
            if (btn.toggleStyle.size() > m_limits.maxCssBytes) { p.problems.push_back("'" + btn.id + "': style sheet too big"); continue; }
            if (!btn.toggleStyle.empty() && !(p.requested & kPermStyles)) { p.problems.push_back("'" + btn.id + "' changes styles: needs 'styles'"); continue; }
            if (btn.open.empty() && btn.toggleStyle.empty()) { p.problems.push_back("'" + btn.id + "' does nothing"); continue; }
            btn.toggleStyle = InertCss(btn.toggleStyle);
            out.push_back(std::move(btn));
        }
    };
    buttons("toolbar", kPermToolbar, p.toolbar);
    buttons("pageActions", kPermPageActions, p.pageActions);
    if (root.Get("block")) {
        if (!(p.requested & kPermBlock)) p.problems.push_back("'block' needs the permission 'block'");
        else p.block = Strings(root, "block", items, 2048);
    }
    if (const json::Value* st = root.Get("styles")) {
        if (!(p.requested & kPermStyles)) p.problems.push_back("'styles' needs the permission 'styles'");
        else if (st->IsArray())
            for (const json::Value& s : st->array) {
                if (p.styles.size() >= items) break;
                PluginStyle ps;
                ps.plugin = p.id;
                ps.sites = Strings(s, "sites", items, tn);
                ps.css = Str(s, "css", m_limits.maxCssBytes + 1);
                if (ps.css.empty() || ps.css.size() > m_limits.maxCssBytes || ps.sites.empty()) { p.problems.push_back("a style needs sites and css (at most 64 KB)"); continue; }
                ps.css = InertCss(ps.css);
                p.styles.push_back(std::move(ps));
            }
    }
    if (const json::Value* nt = root.Get("newTab")) {
        if (!(p.requested & kPermNewTab)) p.problems.push_back("'newTab' needs the permission 'newTab'");
        else if (nt->IsArray())
            for (const json::Value& s : nt->array) {
                if (p.newTab.size() >= items) break;
                NewTabSection sec;
                sec.plugin = p.id;
                sec.title = Str(s, "title", tn);
                if (const json::Value* links = s.Get("links"))
                    if (links->IsArray())
                        for (const json::Value& l : links->array) {
                            PluginLink pl{Str(l, "title", tn), Str(l, "url", 4097)};
                            if (pl.title.empty() || !HttpAddress(pl.url)) { p.problems.push_back("a new-tab link needs a title and an http(s) address"); continue; }
                            if (sec.links.size() < items) sec.links.push_back(std::move(pl));
                        }
                if (sec.title.empty()) { p.problems.push_back("a new-tab section has no title"); continue; }
                p.newTab.push_back(std::move(sec));
            }
    }
    for (BrowserPlugin& old : m_plugins)
        if (old.id == p.id) { p.granted = old.granted; p.enabled = old.enabled; old = std::move(p); return true; }
    if (m_plugins.size() >= m_limits.maxPlugins) { why = "too many plugins"; return false; }
    m_plugins.push_back(std::move(p));
    return true;
}

bool BrowserPlugins::Remove(const std::string& id) {
    const auto it = std::find_if(m_plugins.begin(), m_plugins.end(), [&](const BrowserPlugin& p) { return p.id == id; });
    if (it == m_plugins.end()) return false;
    m_plugins.erase(it);
    return true;
}
bool BrowserPlugins::Grant(const std::string& id, unsigned perms) {
    for (BrowserPlugin& p : m_plugins) if (p.id == id) { p.granted = perms; return true; }
    return false;
}
bool BrowserPlugins::SetEnabled(const std::string& id, bool on) {
    for (BrowserPlugin& p : m_plugins) if (p.id == id) { p.enabled = on; return true; }
    return false;
}
const BrowserPlugin* BrowserPlugins::Find(const std::string& id) const {
    for (const BrowserPlugin& p : m_plugins) if (p.id == id) return &p;
    return nullptr;
}

namespace {
bool ShowsOn(const PluginButton& b, const std::string& url) {
    if (b.sites.empty()) return true;
    for (const std::string& s : b.sites) if (SiteMatches(url, s)) return true;
    return false;
}
} // namespace

std::vector<PluginButton> BrowserPlugins::ToolbarButtons() const {
    std::vector<PluginButton> out;
    for (const BrowserPlugin& p : m_plugins) if (p.Allowed() & kPermToolbar) for (const PluginButton& b : p.toolbar) out.push_back(b);
    return out;
}
std::vector<PluginButton> BrowserPlugins::PageActionsFor(const std::string& url) const {
    std::vector<PluginButton> out;
    if (!HttpAddress(url)) return out;   // not on the browser's own pages
    for (const BrowserPlugin& p : m_plugins)
        if (p.Allowed() & kPermPageActions)
            for (const PluginButton& b : p.pageActions) if (ShowsOn(b, url)) out.push_back(b);
    return out;
}
PluginEffect BrowserPlugins::Press(const std::string& plugin, const std::string& button, const std::string& url) {
    PluginEffect e;
    for (BrowserPlugin& p : m_plugins) {
        if (p.id != plugin) continue;
        const unsigned allowed = p.Allowed();
        auto press = [&](PluginButton& b) {
            if (!b.open.empty()) { e.kind = PluginEffect::OpenUrl; e.url = b.open; return; }
            if (!b.toggleStyle.empty() && (allowed & kPermStyles)) { b.on = !b.on; e.kind = b.on ? PluginEffect::StyleOn : PluginEffect::StyleOff; }
        };
        if (allowed & kPermToolbar) for (PluginButton& b : p.toolbar) if (b.id == button) { press(b); return e; }
        if (allowed & kPermPageActions)
            for (PluginButton& b : p.pageActions) {
                if (b.id != button) continue;
                if (HttpAddress(url) && ShowsOn(b, url)) press(b);
                return e;
            }
    }
    return e;
}
bool BrowserPlugins::Blocked(const std::string& url, std::string* by) const {
    for (const BrowserPlugin& p : m_plugins) {
        if (!(p.Allowed() & kPermBlock)) continue;
        for (const std::string& pat : p.block) if (UrlMatchesPattern(url, pat)) { if (by) *by = p.id; return true; }
    }
    return false;
}
std::vector<std::string> BrowserPlugins::StylesFor(const std::string& url) const {
    std::vector<std::string> out;
    if (!HttpAddress(url)) return out;
    for (const BrowserPlugin& p : m_plugins) {
        const unsigned allowed = p.Allowed();
        if (!(allowed & kPermStyles)) continue;
        for (const PluginStyle& s : p.styles) for (const std::string& site : s.sites) if (SiteMatches(url, site)) { out.push_back(s.css); break; }
        auto add = [&](const std::vector<PluginButton>& list, unsigned perm) {
            if (!(allowed & perm)) return;
            for (const PluginButton& b : list) if (b.on && !b.toggleStyle.empty() && ShowsOn(b, url)) out.push_back(b.toggleStyle);
        };
        add(p.toolbar, kPermToolbar);
        add(p.pageActions, kPermPageActions);
    }
    return out;
}
std::vector<NewTabSection> BrowserPlugins::NewTabSections() const {
    std::vector<NewTabSection> out;
    for (const BrowserPlugin& p : m_plugins) if (p.Allowed() & kPermNewTab) for (const NewTabSection& s : p.newTab) out.push_back(s);
    return out;
}
std::string BrowserPlugins::NewTabHtml() const {
    std::string out;
    for (const NewTabSection& s : NewTabSections()) {
        out += "<section class=plugin-section><h2>" + Escape(s.title) + "</h2><ul>";
        for (const PluginLink& l : s.links) out += "<li><a href=\"" + Escape(l.url) + "\">" + Escape(l.title) + "</a></li>";
        out += "</ul></section>";
    }
    return out;
}

} // namespace okay::browser
