// Site check: causes, per-site JSON and the ranking (see SiteReport.hpp).
#include "SiteReport.hpp"

#include "okay/Browser/Script.hpp"
#include "okay/Core/Json.hpp"
#include "okay/Core/StringUtil.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace sitecheck {

namespace str = okay::str;

const char* AreaName(Area a) {
    switch (a) {
        case Area::Network: return "network";
        case Area::JavaScript: return "JavaScript language";
        case Area::DomApi: return "DOM API";
        case Area::Css: return "CSS";
        case Area::Layout: return "layout";
        case Area::Images: return "images";
        case Area::Fonts: return "fonts";
        case Area::Encoding: return "encoding";
    }
    return "?";
}

namespace {

bool StartsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

// the inside of a JSON string (okay::json::Escape writes the quotes too; the callers here write their own)
std::string Json(const std::string& s) {
    std::string o;
    okay::json::Escape(s, o);
    return o.substr(1, o.size() - 2);
}

std::string Num(double v) {
    char b[48];
    std::snprintf(b, sizeof b, "%.1f", v);
    return b;
}

// the JavaScript built-ins (Script.hpp IsJavaScriptGlobal): a missing one is a language gap, not a DOM one
bool IsLanguageName(const std::string& api) { return okay::browser::IsJavaScriptGlobal(api.substr(0, api.find('.'))); }

void Add(std::vector<Cause>& out, std::set<std::string>& seen, Area a, const std::string& key, const std::string& detail) {
    if (key.empty() || !seen.insert(key).second) return;
    Cause c;
    c.area = a;
    c.key = key;
    c.detail = detail.size() > 300 ? detail.substr(0, 300) + "..." : detail;
    out.push_back(std::move(c));
}

} // namespace

std::string ImageFormatOf(const std::string& h, const std::string& contentType) {
    auto at = [&](std::size_t i, const char* s) { return h.compare(i, std::string(s).size(), s) == 0; };
    if (h.size() >= 8 && at(0, "\x89PNG")) return "PNG";
    if (h.size() >= 3 && (unsigned char)h[0] == 0xFF && (unsigned char)h[1] == 0xD8) return "JPEG";
    if (at(0, "GIF8")) return "GIF";
    if (h.size() >= 12 && at(0, "RIFF") && at(8, "WEBP")) return "WebP";
    if (h.size() >= 12 && at(4, "ftyp")) {
        const std::string brand = h.substr(8, 4);
        return brand == "avif" || brand == "avis" ? "AVIF" : "HEIF/" + brand;
    }
    if (h.size() >= 4 && h[0] == 0 && h[1] == 0 && h[2] == 1 && h[3] == 0) return "ICO";
    if (at(0, "BM")) return "BMP";
    const std::string lower = str::LowerAscii(h);
    if (lower.find("<svg") != std::string::npos || lower.find("<?xml") != std::string::npos) return "SVG";
    const std::string ct = str::LowerAscii(contentType);
    if (ct.find("svg") != std::string::npos) return "SVG";
    if (ct.find("text/html") != std::string::npos || lower.find("<!doctype") != std::string::npos || lower.find("<html") != std::string::npos)
        return "HTML (not an image)";
    if (h.empty()) return "empty";
    return "";
}

std::string FontFormatOf(const std::string& h) {
    if (h.size() < 4) return "";
    if (h.compare(0, 4, "wOFF") == 0) return "WOFF";
    if (h.compare(0, 4, "wOF2") == 0) return "WOFF2";
    if (h.compare(0, 4, "OTTO") == 0) return "OpenType";
    if (h.compare(0, 4, std::string("\0\1\0\0", 4)) == 0 || h.compare(0, 4, "true") == 0) return "TrueType";
    return "";
}

std::string NetReason(const std::string& e) {
    const std::string l = str::LowerAscii(e);
    if (l.empty()) return "unknown";
    if (StartsWith(l, "too-large")) return "too large";
    if (l.find("resolve") != std::string::npos || l.find("dns") != std::string::npos || l.find("getaddrinfo") != std::string::npos ||
        l.find("name or service") != std::string::npos || l.find("host not found") != std::string::npos) return "DNS";
    if (l.find("tls") != std::string::npos || l.find("ssl") != std::string::npos || l.find("certificate") != std::string::npos ||
        l.find("handshake") != std::string::npos) return "TLS";
    if (l.find("timeout") != std::string::npos || l.find("timed out") != std::string::npos) return "timeout";
    if (l.find("refused") != std::string::npos || l.find("connect") != std::string::npos) return "connect";
    if (l.find("cancel") != std::string::npos || StartsWith(l, "aborted")) return "cancelled";
    // the own HTTP client's error names (okay/Platform/OwnHttp.hpp ErrName)
    if (StartsWith(l, "unsupported")) return "unsupported: " + e.substr(0, 60);
    for (const char* n : {"bad-response", "too-many-redirects", "downgrade-refused", "bad-header", "io", "proxy"})
        if (StartsWith(l, n)) return n;
    if (l.find("scheme") != std::string::npos || l.find("url") != std::string::npos) return "address";
    return "other: " + e.substr(0, 60);
}

std::string JsErrorKey(const std::string& line) {
    if (line.find("instruction budget") != std::string::npos) return "script stopped: ran past its instruction budget";
    if (line.find("more memory than allowed") != std::string::npos) return "script stopped: memory limit";
    static const char* const kTypes[] = {"SyntaxError", "TypeError", "ReferenceError", "RangeError", "URIError", "EvalError", "Error"};
    std::size_t at = std::string::npos;
    std::string type;
    for (const char* t : kTypes) {
        const std::size_t p = line.find(std::string(t) + ":");
        if (p != std::string::npos && (at == std::string::npos || p < at)) { at = p; type = t; }
    }
    if (at == std::string::npos) return "";
    std::string msg = line.substr(at);
    const std::size_t nl = msg.find('\n');
    if (nl != std::string::npos) msg.resize(nl);
    // numbers (line, column, counts) and quoted page names vary per site: they are folded so the same gap groups together
    std::string out;
    bool inQuote = false;
    for (std::size_t i = 0; i < msg.size(); ++i) {
        const char c = msg[i];
        if (c == '\'') {
            if (!inQuote) { out += "'X'"; inQuote = true; }
            else inQuote = false;
            continue;
        }
        if (inQuote) continue;
        if (std::isdigit((unsigned char)c)) { if (out.empty() || out.back() != 'N') out += 'N'; continue; }
        out += c;
    }
    if (type == "SyntaxError") {
        // a syntax error names the construct we do not read: keep its own words (the quoted token says which)
        std::string keep = msg.size() > 140 ? msg.substr(0, 140) : msg;
        for (char& c : keep) if (std::isdigit((unsigned char)c)) c = 'N';
        return keep;
    }
    if (type == "ReferenceError" && out.find("is not defined") != std::string::npos)
        return "ReferenceError: a global of the page is not defined (a script before it failed or was not loaded)";
    return out.size() > 140 ? out.substr(0, 140) : out;
}

std::string SelectorKey(const std::string& sel) {
    std::set<std::string> parts;
    for (std::size_t i = 0; i < sel.size(); ++i) {
        if (sel[i] != ':') continue;
        std::size_t j = i + 1;
        if (j < sel.size() && sel[j] == ':') ++j;
        std::size_t k = j;
        while (k < sel.size() && (std::isalnum((unsigned char)sel[k]) || sel[k] == '-' || sel[k] == '_')) ++k;
        if (k > j) parts.insert(sel.substr(i, k - i));
        i = k - 1;
    }
    if (sel.find("|=") != std::string::npos || sel.find(" i]") != std::string::npos || sel.find(" s]") != std::string::npos)
        parts.insert("[attribute form]");
    if (sel.find('\\') != std::string::npos) parts.insert("escaped characters");
    if (sel.find('&') != std::string::npos) parts.insert("&");
    if (parts.empty()) return "other selector form";
    std::string o;
    for (const std::string& p : parts) o += (o.empty() ? "" : " ") + p;
    return o;
}

std::string CharsetOf(const std::string& contentType, const std::string& page) {
    auto after = [](const std::string& l, std::size_t at) {
        std::size_t i = at;
        while (i < l.size() && (l[i] == ' ' || l[i] == '"' || l[i] == '\'' || l[i] == '=')) ++i;
        std::size_t e = i;
        while (e < l.size() && (std::isalnum((unsigned char)l[e]) || l[e] == '-' || l[e] == '_')) ++e;
        return l.substr(i, e - i);
    };
    const std::string ct = str::LowerAscii(contentType);
    std::size_t p = ct.find("charset");
    if (p != std::string::npos) return after(ct, p + 7);
    const std::string head = str::LowerAscii(page.substr(0, std::min<std::size_t>(page.size(), 4096)));
    p = head.find("charset");
    if (p != std::string::npos) return after(head, p + 7);
    return "";
}

std::vector<Cause> CausesOf(const SiteResult& r) {
    std::vector<Cause> out;
    std::set<std::string> seen;
    const Fetch* page = nullptr;
    for (const Fetch& f : r.fetches) if (f.kind == "page") page = &f;   // the last page answer (after a script's navigation)
    if (!r.loaded) {
        if (page && !page->reached) Add(out, seen, Area::Network, "page not reached: " + NetReason(page->netError), page->netError);
        else if (r.timedOut && !page) Add(out, seen, Area::Network, "page load timed out", r.site);
        else Add(out, seen, Area::Network, "page not shown", r.pageError);
    }
    if (page && page->reached && page->status >= 400)
        Add(out, seen, Area::Network, "page answered HTTP " + std::to_string(page->status), page->finalUrl);
    for (const Fetch& f : r.fetches) {
        const std::string enc = str::LowerAscii(str::Trim(f.contentEncoding));
        if (!enc.empty() && enc != "identity" && enc != "gzip" && enc != "x-gzip" && enc != "deflate")
            Add(out, seen, Area::Network, "Content-Encoding '" + enc + "' not read", f.url);
        if (f.note == "redirect refused") Add(out, seen, Area::Network, "redirect refused (" + f.kind + ")", f.url);
        if (f.kind == "page") continue;
        if (!f.reached) Add(out, seen, Area::Network, f.kind + " request failed: " + NetReason(f.netError), f.url + " (" + f.netError + ")");
        else if (f.status >= 400) Add(out, seen, Area::Network, f.kind + " answered HTTP " + std::to_string(f.status), f.url);
        if (f.note == "image not decoded") {
            const std::string fmt = ImageFormatOf(f.head, f.contentType);
            Add(out, seen, Area::Images, "image format " + (fmt.empty() ? std::string("unknown") : fmt) + " not decoded", f.url);
        }
        if (StartsWith(f.note, "font not decoded")) {   // the host could not turn it into a TrueType / OpenType file (WebFont.hpp)
            const std::string fmt = FontFormatOf(f.head);
            Add(out, seen, Area::Fonts, "font format " + (fmt.empty() ? std::string("unknown") : fmt) + " not used", f.url);
        }
    }
    for (std::size_t i = 0; i < r.console.size(); ++i) {
        const std::string& l = r.console[i];
        if (StartsWith(l, "not implemented: ")) {
            const std::string api = l.substr(17);
            Add(out, seen, IsLanguageName(api) ? Area::JavaScript : Area::DomApi, "not implemented: " + api, api);
            continue;
        }
        // an error line followed by its own "not implemented" line (the engine adds it right after) is filed under that line only
        if (i + 1 < r.console.size() && StartsWith(r.console[i + 1], "not implemented: ")) {
            const std::string api = r.console[i + 1].substr(17);
            const std::string last = api.substr(api.rfind('.') == std::string::npos ? 0 : api.rfind('.') + 1);
            if (l.find("'" + api + "'") != std::string::npos || l.find("." + last + "'") != std::string::npos) continue;
        }
        const std::string k = JsErrorKey(l);
        if (k.empty()) continue;
        Add(out, seen, Area::JavaScript, StartsWith(k, "SyntaxError") ? "syntax not read: " + k : "script error: " + k, l);
    }
    for (const auto& p : r.cssProperties) Add(out, seen, Area::Css, "CSS property not used: " + p.first, std::to_string(p.second) + " times");
    for (const auto& s : r.cssSelectors) Add(out, seen, Area::Css, "CSS selector not read: " + SelectorKey(s.first), s.first);
    if (r.loaded && r.elements > 50 && r.drawCmds < 10)
        Add(out, seen, Area::Layout, "page drew almost nothing", std::to_string(r.elements) + " elements, " + std::to_string(r.drawCmds) + " draw commands");
    else if (r.loaded && r.blankShare >= 0.97)
        Add(out, seen, Area::Layout, "screenshot is nearly one flat colour", Num(r.blankShare * 100.0) + "% of the page area");
    if (!r.charset.empty() && r.charset != "utf-8" && r.charset != "utf8")
        Add(out, seen, Area::Encoding, "page charset " + r.charset, r.finalUrl);
    return out;
}

std::string SiteJson(const SiteResult& r) {
    std::string o = "{\n";
    o += "  \"site\": \"" + Json(r.site) + "\",\n  \"category\": \"" + Json(r.category) + "\",\n";
    o += "  \"finalUrl\": \"" + Json(r.finalUrl) + "\",\n  \"status\": " + std::to_string(r.status) + ",\n";
    o += "  \"title\": \"" + Json(r.title) + "\",\n  \"loaded\": " + (r.loaded ? "true" : "false") + ",\n";
    o += "  \"timedOut\": " + std::string(r.timedOut ? "true" : "false") + ",\n  \"pageError\": \"" + Json(r.pageError) + "\",\n";
    o += "  \"loadMs\": " + Num(r.loadMs) + ",\n  \"totalMs\": " + Num(r.totalMs) + ",\n";
    o += "  \"nodes\": " + std::to_string(r.nodes) + ",\n  \"elements\": " + std::to_string(r.elements) + ",\n";
    o += "  \"textBytes\": " + std::to_string(r.textBytes) + ",\n  \"drawCmds\": " + std::to_string(r.drawCmds) + ",\n";
    o += "  \"pageHeight\": " + Num(r.pageHeight) + ",\n  \"blankShare\": " + Num(r.blankShare * 100.0) + ",\n";
    o += "  \"charset\": \"" + Json(r.charset) + "\",\n  \"screenshot\": \"" + Json(r.screenshot) + "\",\n";
    o += "  \"fetches\": " + std::to_string(r.fetches.size()) + ",\n  \"failedFetches\": [";
    bool first = true;
    for (const Fetch& f : r.fetches) {
        if (f.reached && f.status < 400 && f.note.empty()) continue;
        o += std::string(first ? "\n" : ",\n") + "    {\"kind\": \"" + Json(f.kind) + "\", \"url\": \"" + Json(f.url) + "\", \"status\": " +
             std::to_string(f.status) + ", \"reason\": \"" +
             Json(!f.reached ? NetReason(f.netError) + " (" + f.netError + ")" : !f.note.empty() ? f.note : "HTTP " + std::to_string(f.status)) + "\"}";
        first = false;
    }
    o += first ? "],\n" : "\n  ],\n";
    o += "  \"console\": [";
    first = true;
    for (const std::string& l : r.console) { o += std::string(first ? "\n" : ",\n") + "    \"" + Json(l.size() > 600 ? l.substr(0, 600) + "..." : l) + "\""; first = false; }
    o += first ? "],\n" : "\n  ],\n";
    o += "  \"cssPropertiesNotUsed\": {";
    first = true;
    for (const auto& p : r.cssProperties) { o += std::string(first ? "" : ", ") + "\"" + Json(p.first) + "\": " + std::to_string(p.second); first = false; }
    o += "},\n  \"cssSelectorsNotRead\": [";
    first = true;
    int shown = 0;
    for (const auto& s : r.cssSelectors) {
        if (++shown > 200) break;
        o += std::string(first ? "" : ", ") + "\"" + Json(s.first.size() > 200 ? s.first.substr(0, 200) : s.first) + "\"";
        first = false;
    }
    o += "],\n  \"causes\": [";
    first = true;
    for (const Cause& c : CausesOf(r)) {
        o += std::string(first ? "\n" : ",\n") + "    {\"area\": \"" + AreaName(c.area) + "\", \"key\": \"" + Json(c.key) + "\", \"detail\": \"" + Json(c.detail) + "\"}";
        first = false;
    }
    o += first ? "]\n" : "\n  ]\n";
    return o + "}\n";
}

std::vector<Ranked> Rank(const std::vector<SiteResult>& all) {
    std::map<std::string, Ranked> by;
    for (const SiteResult& r : all)
        for (const Cause& c : CausesOf(r)) {
            Ranked& k = by[c.key];
            if (k.sites == 0) { k.area = c.area; k.key = c.key; k.detail = c.detail; }
            ++k.sites;
            if (k.examples.size() < 3) k.examples.push_back(r.site);
        }
    std::vector<Ranked> out;
    out.reserve(by.size());
    for (auto& kv : by) out.push_back(std::move(kv.second));
    std::stable_sort(out.begin(), out.end(), [](const Ranked& a, const Ranked& b) {
        if (a.sites != b.sites) return a.sites > b.sites;
        return (int)a.area < (int)b.area;
    });
    return out;
}

std::string SummaryMarkdown(const std::vector<SiteResult>& all, const std::vector<Ranked>& ranked) {
    int loaded = 0;
    for (const SiteResult& r : all) loaded += r.loaded ? 1 : 0;
    std::string o = "# Site check summary\n\n" + std::to_string(all.size()) + " sites, " + std::to_string(loaded) + " shown (the tab reached Ready).\n\n";
    o += "## Causes ranked by how many sites they hit\n\n| # | Sites | Area | Cause | Examples |\n|---|---|---|---|---|\n";
    int n = 0;
    for (const Ranked& k : ranked) {
        std::string ex;
        for (const std::string& e : k.examples) ex += (ex.empty() ? "" : ", ") + e;
        std::string key = k.key;
        for (char& c : key) if (c == '|') c = '/';
        o += "| " + std::to_string(++n) + " | " + std::to_string(k.sites) + " | " + AreaName(k.area) + " | " + key + " | " + ex + " |\n";
    }
    std::map<std::string, int> areaSites;
    for (const SiteResult& r : all) {
        std::set<std::string> areas;
        for (const Cause& c : CausesOf(r)) areas.insert(AreaName(c.area));
        for (const std::string& a : areas) ++areaSites[a];
    }
    o += "\n## Sites hit per area\n\n| Area | Sites |\n|---|---|\n";
    for (const auto& a : areaSites) o += "| " + a.first + " | " + std::to_string(a.second) + " |\n";
    o += "\n## Per site\n\n| Site | Status | Shown | Load ms | Nodes | Failed fetches | Causes |\n|---|---|---|---|---|---|---|\n";
    for (const SiteResult& r : all) {
        int failed = 0;
        for (const Fetch& f : r.fetches) if (!f.reached || f.status >= 400 || !f.note.empty()) ++failed;
        o += "| " + r.site + " | " + std::to_string(r.status) + " | " + (r.loaded ? "yes" : "no") + " | " + Num(r.loadMs) + " | " +
             std::to_string(r.nodes) + " | " + std::to_string(failed) + "/" + std::to_string(r.fetches.size()) + " | " +
             std::to_string(CausesOf(r).size()) + " |\n";
    }
    return o;
}

std::string SummaryJson(const std::vector<SiteResult>& all, const std::vector<Ranked>& ranked) {
    std::string o = "{\n  \"sites\": " + std::to_string(all.size()) + ",\n  \"causes\": [";
    bool first = true;
    for (const Ranked& k : ranked) {
        o += std::string(first ? "\n" : ",\n") + "    {\"sites\": " + std::to_string(k.sites) + ", \"area\": \"" + AreaName(k.area) +
             "\", \"key\": \"" + Json(k.key) + "\", \"examples\": [";
        for (std::size_t i = 0; i < k.examples.size(); ++i) o += std::string(i ? ", " : "") + "\"" + Json(k.examples[i]) + "\"";
        o += "], \"detail\": \"" + Json(k.detail) + "\"}";
        first = false;
    }
    return o + (first ? "]\n}\n" : "\n  ]\n}\n");
}

std::vector<SiteLine> ReadSiteList(const std::string& text) {
    std::vector<SiteLine> out;
    std::string category;
    std::size_t at = 0;
    while (at <= text.size()) {
        std::size_t e = text.find('\n', at);
        if (e == std::string::npos) e = text.size();
        const std::string line = str::Trim(text.substr(at, e - at));
        at = e + 1;
        if (line.empty()) continue;
        if (line[0] == '#') {
            if (line.size() > 2 && line[1] == ' ' && line.find(' ', 2) == std::string::npos) category = line.substr(2);
            continue;
        }
        if (StartsWith(line, "http://") || StartsWith(line, "https://")) out.push_back({line, category});
    }
    return out;
}

} // namespace sitecheck
