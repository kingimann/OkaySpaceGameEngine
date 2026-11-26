// Browser: the program's lasting state and rules (see Profile.hpp). Written from the WHATWG URL standard (the urlencoded
// serializer, via Url.hpp), RFC 9110 (header fields, media types), RFC 6266 and RFC 8187 (Content-Disposition filenames) and
// RFC 8259 (the profile's JSON, via okay/Core/Json.hpp).
#include "okay/Browser/Profile.hpp"

#include "okay/Browser/Url.hpp"
#include "okay/Core/Json.hpp"
#include "okay/Core/StringUtil.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace okay::browser {

using okay::str::LowerAscii;
using okay::str::Trim;

namespace {

bool IsDigit(char c) { return c >= '0' && c <= '9'; }

bool LooksLikeIpv4(const std::string& host) {
    int dots = 0, digits = 0;
    for (char c : host) {
        if (c == '.') { if (digits == 0) return false; ++dots; digits = 0; }
        else if (IsDigit(c)) { if (++digits > 3) return false; }
        else return false;
    }
    return dots == 3 && digits > 0;
}

// A host name as typed: letters, digits, '-' and '.', with a dot that is not at either end, and a last label that is not all
// digits (so "3.14" is a search, "example.com" an address).
bool LooksLikeHostName(const std::string& host) {
    if (host.empty() || host.front() == '.' || host.back() == '.' || host.find('.') == std::string::npos) return false;
    for (unsigned char c : host)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || IsDigit((char)c) || c == '-' || c == '.' || c >= 0x80)) return false;
    const std::string last = host.substr(host.rfind('.') + 1);
    bool allDigits = true;
    for (char c : last) if (!IsDigit(c)) allDigits = false;
    return !allDigits || LooksLikeIpv4(host);
}

} // namespace

// ---- the address bar ---------------------------------------------------------------------------------------------------

std::string SearchUrl(const std::string& tmpl, const std::string& terms) {
    if (tmpl.empty()) return std::string();
    const std::string enc = FormUrlEncode(terms);
    const std::size_t at = tmpl.find("%s");
    if (at == std::string::npos) return tmpl + enc;
    return tmpl.substr(0, at) + enc + tmpl.substr(at + 2);
}

std::string AddressPlaceholder(const std::string& tmpl) {
    if (Trim(tmpl).empty()) return "Type an address";
    if (HostOf(tmpl) == "www.google.com" || HostOf(tmpl) == "google.com") return "Search Google or type an address";
    return "Search or type an address";
}

AddressDecision DecideAddress(const std::string& typedIn, const std::string& tmpl) {
    AddressDecision d;
    const std::string typed = Trim(typedIn);
    if (typed.empty()) return d;
    const std::string lower = LowerAscii(typed);
    auto address = [&](const std::string& u) { d.kind = AddressDecision::Address; d.url = u; return d; };
    auto search = [&]() {
        d.url = SearchUrl(Trim(tmpl), typed);
        d.kind = d.url.empty() ? AddressDecision::NoSearch : AddressDecision::Search;
        return d;
    };
    if (lower.rfind("about:", 0) == 0 && typed.find(' ') == std::string::npos) return address(lower);
    if (lower.rfind("http://", 0) == 0 || lower.rfind("https://", 0) == 0) {   // typed with its scheme: an address, spaces encoded
        std::string u;
        for (char c : typed) { if (c == ' ') u += "%20"; else if (c != '\t') u += c; }
        return address(u);
    }
    // A space anywhere else means words (an address never has one).
    if (typed.find_first_of(" \t") != std::string::npos) return search();
    // "scheme:" of another kind (file:, javascript:, mailto: ...): kept as typed, the tab refuses it.
    const std::size_t colon = typed.find(':');
    const std::size_t slash = typed.find('/');
    if (colon != std::string::npos && (slash == std::string::npos || colon < slash) && typed.compare(colon, 3, "://") == 0) return address(typed);
    // host[:port][/path][?query][#fragment]
    const std::size_t end = typed.find_first_of("/?#");
    std::string hostPort = typed.substr(0, end);
    std::string host = hostPort, port;
    const std::size_t pc = hostPort.rfind(':');
    if (pc != std::string::npos && hostPort.find(']') == std::string::npos) {
        host = hostPort.substr(0, pc);
        port = hostPort.substr(pc + 1);
        for (char c : port) if (!IsDigit(c)) return search();   // "word:thing" is not an address
        if (port.empty() || port.size() > 5) return search();
    }
    const std::string lhost = LowerAscii(host);
    if (lhost == "localhost" || LooksLikeIpv4(lhost) || (lhost.size() > 2 && lhost.front() == '[' && lhost.back() == ']'))
        return address("http://" + typed);
    if (LooksLikeHostName(host)) return address("https://" + typed);
    if (!port.empty() && !host.empty()) return address("http://" + typed);   // "myserver:8080"
    return search();
}

// ---- tabs --------------------------------------------------------------------------------------------------------------

int TabOrder::IndexOf(int id) const {
    for (std::size_t i = 0; i < m_ids.size(); ++i) if (m_ids[i] == id) return (int)i;
    return -1;
}

int TabOrder::Open(bool fromActive, bool activate) {
    const int id = m_next++;
    const int ai = IndexOf(m_active);
    if (fromActive && ai >= 0) {
        int at = ai + 1;
        // after the tabs already opened from this one (they sit right after it, the last opened last)
        if (m_openedFrom == m_active && IndexOf(m_lastOpened) >= 0) at = IndexOf(m_lastOpened) + 1;
        m_ids.insert(m_ids.begin() + at, id);
        m_openedFrom = m_active;
        m_lastOpened = id;
    } else {
        m_ids.push_back(id);
        m_openedFrom = -1;
    }
    if (activate || m_active < 0) { m_active = id; m_openedFrom = -1; }
    return id;
}

bool TabOrder::Close(int id, const std::string& url) {
    const int i = IndexOf(id);
    if (i < 0) return false;
    if (!url.empty()) {
        m_closed.push_back(url);
        if (m_closed.size() > kMaxClosed) m_closed.erase(m_closed.begin());
    }
    m_ids.erase(m_ids.begin() + i);
    if (id == m_lastOpened) m_lastOpened = -1;
    if (id == m_openedFrom) m_openedFrom = -1;
    if (m_active == id) {
        if (m_ids.empty()) m_active = -1;
        else m_active = m_ids[(std::size_t)std::min(i, (int)m_ids.size() - 1)];
    }
    return true;
}

void TabOrder::Activate(int id) {
    if (IndexOf(id) >= 0 && id != m_active) { m_active = id; m_openedFrom = -1; }
}

void TabOrder::Next() {
    if (m_ids.empty()) return;
    Activate(m_ids[(std::size_t)((IndexOf(m_active) + 1) % (int)m_ids.size())]);
}

void TabOrder::Previous() {
    if (m_ids.empty()) return;
    const int n = (int)m_ids.size();
    Activate(m_ids[(std::size_t)((IndexOf(m_active) - 1 + n) % n)]);
}

void TabOrder::ActivateNumber(int n) {
    if (m_ids.empty() || n < 1) return;
    if (n >= 9) { Activate(m_ids.back()); return; }
    if (n <= (int)m_ids.size()) Activate(m_ids[(std::size_t)(n - 1)]);
}

void TabOrder::Move(int id, int index) {
    const int i = IndexOf(id);
    if (i < 0) return;
    m_ids.erase(m_ids.begin() + i);
    index = std::max(0, std::min(index, (int)m_ids.size()));
    m_ids.insert(m_ids.begin() + index, id);
}

std::string TabOrder::TakeClosed() {
    if (m_closed.empty()) return std::string();
    std::string u = m_closed.back();
    m_closed.pop_back();
    return u;
}

// ---- history, bookmarks, zoom ------------------------------------------------------------------------------------------

void History::Add(const std::string& url, const std::string& title, std::int64_t time) {
    if (url.empty() || url == "about:blank") return;
    if (!m_items.empty() && m_items.back().url == url) {
        m_items.back().time = time;
        if (!title.empty()) m_items.back().title = title;
        return;
    }
    m_items.push_back({url, title, time});
    if (m_items.size() > kMax) m_items.erase(m_items.begin(), m_items.begin() + (std::ptrdiff_t)(m_items.size() - kMax));
}

void History::SetTitle(const std::string& url, const std::string& title) {
    for (std::size_t i = m_items.size(); i-- > 0;)
        if (m_items[i].url == url) { m_items[i].title = title; return; }
}

void History::Remove(const std::string& url) {
    m_items.erase(std::remove_if(m_items.begin(), m_items.end(), [&](const HistoryEntry& e) { return e.url == url; }), m_items.end());
}

bool Bookmarks::Has(const std::string& url) const {
    for (const Bookmark& b : m_items) if (b.url == url) return true;
    return false;
}

bool Bookmarks::Add(const std::string& url, const std::string& title) {
    if (url.empty() || Has(url) || m_items.size() >= kMax) return false;
    m_items.push_back({title.empty() ? url : title, url});
    return true;
}

bool Bookmarks::Remove(const std::string& url) {
    const std::size_t n = m_items.size();
    m_items.erase(std::remove_if(m_items.begin(), m_items.end(), [&](const Bookmark& b) { return b.url == url; }), m_items.end());
    return m_items.size() != n;
}

bool Bookmarks::Toggle(const std::string& url, const std::string& title) {
    if (Remove(url)) return false;
    return Add(url, title);
}

const std::vector<float>& SiteZoom::Steps() {
    static const std::vector<float> steps = {0.25f, 0.33f, 0.5f, 0.67f, 0.75f, 0.8f, 0.9f, 1.0f, 1.1f, 1.25f, 1.5f, 1.75f,
                                             2.0f, 2.5f, 3.0f, 4.0f, 5.0f};
    return steps;
}

float SiteZoom::Get(const std::string& origin) const {
    auto it = m_zoom.find(origin);
    return it == m_zoom.end() ? 1.0f : it->second;
}

void SiteZoom::Set(const std::string& origin, float z) {
    if (!std::isfinite(z)) return;
    z = std::max(Steps().front(), std::min(Steps().back(), z));
    if (std::fabs(z - 1.0f) < 0.001f) m_zoom.erase(origin);
    else m_zoom[origin] = z;
}

float SiteZoom::In(const std::string& origin) {
    const float z = Get(origin);
    for (float s : Steps()) if (s > z + 0.001f) { Set(origin, s); return s; }
    return z;
}

float SiteZoom::Out(const std::string& origin) {
    const float z = Get(origin);
    for (std::size_t i = Steps().size(); i-- > 0;) if (Steps()[i] < z - 0.001f) { Set(origin, Steps()[i]); return Steps()[i]; }
    return z;
}

void SiteZoom::Reset(const std::string& origin) { m_zoom.erase(origin); }

// ---- the profile as JSON -----------------------------------------------------------------------------------------------

std::string Profile::ToJson() const {
    using okay::json::Escape;
    std::string o = "{\n  \"version\": 1,\n  \"settings\": {\"searchUrl\": ";
    Escape(settings.searchUrl, o);
    o += ", \"homePage\": ";
    Escape(settings.homePage, o);
    o += ", \"downloadDir\": ";
    Escape(settings.downloadDir, o);
    o += std::string(", \"showBookmarksBar\": ") + (settings.showBookmarksBar ? "true" : "false");
    o += std::string(", \"askBeforeDownload\": ") + (settings.askBeforeDownload ? "true" : "false") + "},\n  \"bookmarks\": [";
    bool first = true;
    for (const Bookmark& b : bookmarks.Items()) {
        o += first ? "\n    {\"title\": " : ",\n    {\"title\": ";
        first = false;
        Escape(b.title, o);
        o += ", \"url\": ";
        Escape(b.url, o);
        o += "}";
    }
    o += "],\n  \"zoom\": {";
    first = true;
    for (const auto& z : zoom.All()) {
        if (!first) o += ", ";
        first = false;
        Escape(z.first, o);
        char num[32];
        std::snprintf(num, sizeof num, ": %.3f", (double)z.second);
        o += num;
    }
    o += "},\n  \"history\": [";
    first = true;
    for (const HistoryEntry& h : history.Items()) {
        o += first ? "\n    {\"url\": " : ",\n    {\"url\": ";
        first = false;
        Escape(h.url, o);
        o += ", \"title\": ";
        Escape(h.title, o);
        o += ", \"time\": " + std::to_string((long long)h.time) + "}";
    }
    o += "]\n}\n";
    return o;
}

bool Profile::FromJson(const std::string& text, std::string* why) {
    okay::json::Value v;
    if (!okay::json::Parse(text, v, why) || !v.IsObject()) { if (why && why->empty()) *why = "not a JSON object"; return false; }
    Profile p;
    auto str = [](const okay::json::Value* x, std::string& out) { if (x && x->IsString()) out = x->string; };
    auto flag = [](const okay::json::Value* x, bool& out) { if (x && x->IsBool()) out = x->boolean; };
    if (const okay::json::Value* s = v.Get("settings")) {
        str(s->Get("searchUrl"), p.settings.searchUrl);
        str(s->Get("homePage"), p.settings.homePage);
        str(s->Get("downloadDir"), p.settings.downloadDir);
        flag(s->Get("showBookmarksBar"), p.settings.showBookmarksBar);
        flag(s->Get("askBeforeDownload"), p.settings.askBeforeDownload);
    }
    if (const okay::json::Value* b = v.Get("bookmarks"))
        for (const okay::json::Value& e : b->array) {
            std::string t, u;
            str(e.Get("title"), t);
            str(e.Get("url"), u);
            p.bookmarks.Add(u, t);
        }
    if (const okay::json::Value* z = v.Get("zoom"))
        for (const auto& kv : z->object) if (kv.second.IsNumber()) p.zoom.Set(kv.first, (float)kv.second.number);
    if (const okay::json::Value* h = v.Get("history"))
        for (const okay::json::Value& e : h->array) {
            HistoryEntry he;
            str(e.Get("url"), he.url);
            str(e.Get("title"), he.title);
            if (const okay::json::Value* t = e.Get("time")) if (t->IsNumber()) he.time = (std::int64_t)t->number;
            if (!he.url.empty()) p.history.MutableItems().push_back(he);
        }
    if (p.history.Items().size() > History::kMax) {
        auto& items = p.history.MutableItems();
        items.erase(items.begin(), items.begin() + (std::ptrdiff_t)(items.size() - History::kMax));
    }
    *this = std::move(p);
    return true;
}

bool SaveProfile(const Profile& p, const std::string& path, std::string* why) {
    namespace fs = std::filesystem;
    const fs::path target = fs::u8path(path);
    std::error_code ec;
    if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);
    const fs::path tmp = fs::u8path(path + ".part");
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) { if (why) *why = "cannot write " + path; return false; }
        const std::string text = p.ToJson();
        f.write(text.data(), (std::streamsize)text.size());
        if (!f) { if (why) *why = "cannot write " + path; return false; }
    }
    fs::rename(tmp, target, ec);
    if (ec) { fs::remove(tmp, ec); if (why) *why = "cannot replace " + path; return false; }
    return true;
}

bool LoadProfile(Profile& p, const std::string& path, std::string* why) {
    std::ifstream f(std::filesystem::u8path(path), std::ios::binary);
    if (!f) { if (why) *why = "no profile yet"; return false; }
    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return p.FromJson(text, why);
}

// ---- downloads ---------------------------------------------------------------------------------------------------------

std::string HeaderValue(const std::string& head, const std::string& nameIn) {
    const std::string name = LowerAscii(nameIn);
    // the last answer in the head (after redirects the head may hold several, each starting with "HTTP/")
    std::size_t start = 0;
    for (std::size_t p = head.find("HTTP/"); p != std::string::npos; p = head.find("HTTP/", p + 5))
        if (p == 0 || head[p - 1] == '\n') start = p;
    std::string value;
    std::size_t i = start;
    while (i < head.size()) {
        std::size_t e = head.find('\n', i);
        if (e == std::string::npos) e = head.size();
        std::string line = head.substr(i, e - i);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        i = e + 1;
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        if (LowerAscii(Trim(line.substr(0, colon))) == name) value = Trim(line.substr(colon + 1));
    }
    return value;
}

std::string MediaType(const std::string& ct) {
    const std::size_t semi = ct.find(';');
    return LowerAscii(Trim(ct.substr(0, semi)));
}

AnswerUse UseOfAnswer(const std::string& ct, const std::string& disposition, const std::string& url) {
    if (LowerAscii(Trim(disposition)).rfind("attachment", 0) == 0) return AnswerUse::Download;
    std::string t = MediaType(ct);
    if (t.empty()) {
        const std::string ext = ExtensionOf(url);
        if (ext.empty() || ext == "html" || ext == "htm" || ext == "xhtml" || ext == "php" || ext == "asp" || ext == "aspx") return AnswerUse::Page;
        if (ext == "txt" || ext == "md" || ext == "css" || ext == "js" || ext == "json" || ext == "csv") return AnswerUse::PlainText;
        if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "gif" || ext == "bmp") return AnswerUse::Image;
        return AnswerUse::Download;
    }
    if (t == "text/html" || t == "application/xhtml+xml") return AnswerUse::Page;
    if (t.rfind("text/", 0) == 0 || t == "application/json" || t == "application/javascript") return AnswerUse::PlainText;
    if (t == "image/png" || t == "image/jpeg" || t == "image/gif" || t == "image/bmp" || t == "image/webp" || t == "image/x-tga")
        return AnswerUse::Image;
    return AnswerUse::Download;
}

namespace {

int HexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string PercentDecode(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() && HexVal(s[i + 1]) >= 0 && HexVal(s[i + 2]) >= 0) {
            out += (char)(HexVal(s[i + 1]) * 16 + HexVal(s[i + 2]));
            i += 2;
        } else out += s[i];
    }
    return out;
}

// The value of parameter `name` in a header value ("attachment; filename=\"a b.zip\""), unquoted; "" when missing.
std::string Param(const std::string& value, const std::string& name) {
    std::size_t i = 0;
    while (i < value.size()) {
        const std::size_t semi = value.find(';', i);
        if (semi == std::string::npos) break;
        i = semi + 1;
        while (i < value.size() && (value[i] == ' ' || value[i] == '\t')) ++i;
        const std::size_t eq = value.find('=', i);
        if (eq == std::string::npos) break;
        const std::string key = LowerAscii(Trim(value.substr(i, eq - i)));
        std::size_t j = eq + 1;
        std::string v;
        if (j < value.size() && value[j] == '"') {
            ++j;
            while (j < value.size() && value[j] != '"') { if (value[j] == '\\' && j + 1 < value.size()) ++j; v += value[j++]; }
            if (j < value.size()) ++j;
        } else {
            const std::size_t e = value.find(';', j);
            v = Trim(value.substr(j, e == std::string::npos ? std::string::npos : e - j));
            j = e == std::string::npos ? value.size() : e;
        }
        if (key == name) return v;
        i = j;
    }
    return std::string();
}

std::string CleanFileName(std::string n) {
    // only the last part of a path
    const std::size_t slash = n.find_last_of("/\\");
    if (slash != std::string::npos) n = n.substr(slash + 1);
    std::string out;
    for (unsigned char c : n) {
        if (c < 0x20 || c == 0x7F || std::string("<>:\"/\\|?*").find((char)c) != std::string::npos) out += '_';
        else out += (char)c;
    }
    while (!out.empty() && (out.back() == '.' || out.back() == ' ')) out.pop_back();
    while (!out.empty() && (out.front() == '.' || out.front() == ' ')) out.erase(out.begin());
    if (out.size() > 120) {
        const std::size_t dot = out.rfind('.');
        std::string ext = dot != std::string::npos && out.size() - dot <= 12 ? out.substr(dot) : std::string();
        std::size_t keep = 120 - ext.size();
        while (keep > 0 && ((unsigned char)out[keep] & 0xC0) == 0x80) --keep;   // not inside a character
        out = out.substr(0, keep) + ext;
    }
    // Windows device names, also with an extension (CON.txt)
    std::string stem = LowerAscii(out.substr(0, out.find('.')));
    static const char* const dev[] = {"con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8",
                                      "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
    for (const char* d : dev) if (stem == d) { out = "_" + out; break; }
    return out;
}

std::string ExtensionFor(const std::string& type) {
    struct K { const char* t; const char* e; };
    static const K keys[] = {{"application/zip", ".zip"}, {"application/pdf", ".pdf"}, {"application/gzip", ".gz"},
                             {"application/x-gzip", ".gz"}, {"application/x-tar", ".tar"}, {"application/json", ".json"},
                             {"text/plain", ".txt"}, {"text/html", ".html"}, {"text/csv", ".csv"}, {"image/png", ".png"},
                             {"image/jpeg", ".jpg"}, {"image/gif", ".gif"}, {"audio/mpeg", ".mp3"}, {"audio/ogg", ".ogg"},
                             {"audio/wav", ".wav"}, {"video/mp4", ".mp4"}, {"application/x-msdownload", ".exe"},
                             {"application/vnd.microsoft.portable-executable", ".exe"}, {"application/x-7z-compressed", ".7z"}};
    for (const K& k : keys) if (type == k.t) return k.e;
    return std::string();
}

} // namespace

std::string DownloadFileName(const std::string& url, const std::string& disposition, const std::string& contentType) {
    std::string name;
    // RFC 8187: filename*=UTF-8''name%20here (preferred over filename=)
    std::string star = Param(disposition, "filename*");
    if (!star.empty()) {
        const std::size_t q = star.find("''");
        if (q != std::string::npos && LowerAscii(star.substr(0, q)) == "utf-8") name = PercentDecode(star.substr(q + 2));
    }
    if (name.empty()) name = Param(disposition, "filename");
    if (name.empty()) {
        const Url u = ParseUrl(url);
        const std::size_t slash = u.path.rfind('/');
        name = PercentDecode(u.path.substr(slash == std::string::npos ? 0 : slash + 1));
    }
    name = CleanFileName(name);
    if (name.empty()) name = "download";
    if (name.find('.') == std::string::npos) name += ExtensionFor(MediaType(contentType));
    return name;
}

std::string UniqueDownloadPath(const std::string& folderIn, const std::string& fileName, bool (*exists)(const std::string&, void*),
                               void* user) {
    std::string folder = folderIn;
    if (!folder.empty() && folder.back() != '/' && folder.back() != '\\') folder += '/';
    const std::string first = folder + fileName;
    if (!exists || !exists(first, user)) return first;
    std::string stem = fileName, ext;
    const std::size_t dot = fileName.rfind('.');
    if (dot != std::string::npos && dot > 0) { stem = fileName.substr(0, dot); ext = fileName.substr(dot); }
    if (stem.size() > 4 && LowerAscii(stem.substr(stem.size() - 4)) == ".tar") { ext = stem.substr(stem.size() - 4) + ext; stem.resize(stem.size() - 4); }
    for (int n = 1; n < 10000; ++n) {
        const std::string p = folder + stem + " (" + std::to_string(n) + ")" + ext;
        if (!exists(p, user)) return p;
    }
    return folder + stem + " (" + std::to_string(10000) + ")" + ext;
}

std::string SizeText(std::int64_t b) {
    if (b < 0) return "unknown size";
    char buf[32];
    if (b < 1024) { std::snprintf(buf, sizeof buf, "%lld bytes", (long long)b); return buf; }
    if (b < 1024 * 1024) { std::snprintf(buf, sizeof buf, "%.0f KB", (double)b / 1024.0); return buf; }
    if (b < 1024LL * 1024 * 1024) { std::snprintf(buf, sizeof buf, "%.1f MB", (double)b / (1024.0 * 1024.0)); return buf; }
    std::snprintf(buf, sizeof buf, "%.2f GB", (double)b / (1024.0 * 1024.0 * 1024.0));
    return buf;
}

// ---- our own pages -----------------------------------------------------------------------------------------------------

std::string HtmlEscape(const std::string& t) {
    std::string o;
    o.reserve(t.size());
    for (char c : t) {
        if (c == '<') o += "&lt;";
        else if (c == '>') o += "&gt;";
        else if (c == '&') o += "&amp;";
        else if (c == '"') o += "&quot;";
        else o += c;
    }
    return o;
}

bool IsInternalPage(const std::string& url) {
    const std::string u = LowerAscii(WithoutFragment(url));
    return u == "about:newtab" || u == "about:history" || u == "about:bookmarks" || u == "about:downloads" || u == "about:blank";
}

namespace {

const char* kPageStyle =
    "<style>body{font-family:sans-serif;margin:0;background:#f4f6fa;color:#1d2330}"
    ".top{background:#24324a;color:white;padding:14px 24px}.top a{color:#cfe0ff;margin-right:18px;text-decoration:none}"
    "h1{font-size:22px;margin:18px 24px 8px 24px}.list{margin:0 24px 24px 24px;background:white;border:1px solid #d8dde8}"
    ".row{padding:8px 12px;border-bottom:1px solid #e8ebf2}.row a{text-decoration:none}.meta{color:#6a7385;font-size:13px}"
    ".tiles{margin:8px 24px;display:flex;flex-wrap:wrap;gap:12px}.tile{width:180px;padding:12px;background:white;"
    "border:1px solid #d8dde8;border-radius:8px}.tile a{text-decoration:none}.empty{color:#6a7385;margin:12px 24px}</style>";

std::string Shell(const std::string& title, const std::string& body) {
    return "<!DOCTYPE html><html><head><title>" + HtmlEscape(title) + "</title>" + kPageStyle +
           "</head><body><div class=top><a href=\"about:newtab\">New tab</a><a href=\"about:history\">History</a>"
           "<a href=\"about:bookmarks\">Bookmarks</a><a href=\"about:downloads\">Downloads</a></div>" + body + "</body></html>";
}

std::string LinkRow(const std::string& url, const std::string& title, const std::string& meta) {
    return "<div class=row><a href=\"" + HtmlEscape(url) + "\">" + HtmlEscape(title.empty() ? url : title) + "</a>"
           "<div class=meta>" + HtmlEscape(meta) + "</div></div>";
}

std::string When(std::int64_t t) {
    if (t <= 0) return std::string();
    // days since 1970 to a civil date (the proleptic Gregorian calendar)
    std::int64_t days = t / 86400, secs = t % 86400;
    days += 719468;
    const std::int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const std::int64_t doe = days - era * 146097;
    const std::int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    std::int64_t y = yoe + era * 400;
    const std::int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const std::int64_t mp = (5 * doy + 2) / 153;
    const std::int64_t d = doy - (153 * mp + 2) / 5 + 1;
    const std::int64_t m = mp < 10 ? mp + 3 : mp - 9;
    if (m <= 2) ++y;
    char buf[48];
    std::snprintf(buf, sizeof buf, "%04lld-%02lld-%02lld %02lld:%02lld UTC", (long long)y, (long long)m, (long long)d,
                  (long long)(secs / 3600), (long long)(secs / 60 % 60));
    return buf;
}

} // namespace

std::string InternalPage(const std::string& urlIn, const Profile& p, const std::vector<DownloadItem>& downloads) {
    const std::string url = LowerAscii(WithoutFragment(urlIn));
    if (url == "about:blank") return "<!DOCTYPE html><html><head><title>about:blank</title></head><body></body></html>";
    if (url == "about:history") {
        std::string b = "<h1>History</h1><div class=list>";
        const auto& items = p.history.Items();
        std::size_t shown = 0;
        for (std::size_t i = items.size(); i-- > 0 && shown < 500; ++shown) b += LinkRow(items[i].url, items[i].title, items[i].url + "  " + When(items[i].time));
        b += "</div>";
        if (items.empty()) b += "<p class=empty>No pages visited yet.</p>";
        return Shell("History", b);
    }
    if (url == "about:bookmarks") {
        std::string b = "<h1>Bookmarks</h1><div class=list>";
        for (const Bookmark& bm : p.bookmarks.Items()) b += LinkRow(bm.url, bm.title, bm.url);
        b += "</div>";
        if (p.bookmarks.Items().empty()) b += "<p class=empty>No bookmarks yet. Press Ctrl+D or the star to bookmark a page.</p>";
        return Shell("Bookmarks", b);
    }
    if (url == "about:downloads") {
        std::string b = "<h1>Downloads</h1><div class=list>";
        for (std::size_t i = downloads.size(); i-- > 0;) {
            const DownloadItem& d = downloads[i];
            std::string meta = d.contentType.empty() ? std::string() : d.contentType + ", ";
            switch (d.state) {
            case DownloadItem::Active: meta += SizeText(d.bytes) + " of " + SizeText(d.total) + ", downloading"; break;
            case DownloadItem::Done: meta += SizeText(d.bytes) + ", saved to " + d.path; break;
            case DownloadItem::Failed: meta += "failed: " + d.message; break;
            case DownloadItem::Cancelled: meta += "cancelled"; break;
            }
            b += "<div class=row><b>" + HtmlEscape(d.fileName) + "</b><div class=meta>" + HtmlEscape(meta) + "</div><div class=meta>" +
                 HtmlEscape(d.url) + "</div></div>";
        }
        b += "</div>";
        if (downloads.empty()) b += "<p class=empty>No downloads in this session.</p>";
        return Shell("Downloads", b);
    }
    // about:newtab: the bookmarks as tiles, then the most recent pages
    std::string b = "<h1>New tab</h1>";
    if (!p.bookmarks.Items().empty()) {
        b += "<div class=tiles>";
        std::size_t n = 0;
        for (const Bookmark& bm : p.bookmarks.Items()) {
            if (n++ >= 12) break;
            b += "<div class=tile><a href=\"" + HtmlEscape(bm.url) + "\">" + HtmlEscape(bm.title) + "</a><div class=meta>" +
                 HtmlEscape(HostOf(bm.url)) + "</div></div>";
        }
        b += "</div>";
    }
    b += "<h1>Recently visited</h1><div class=list>";
    std::vector<std::string> seen;
    const auto& items = p.history.Items();
    for (std::size_t i = items.size(); i-- > 0 && seen.size() < 10;) {
        if (std::find(seen.begin(), seen.end(), items[i].url) != seen.end()) continue;
        seen.push_back(items[i].url);
        b += LinkRow(items[i].url, items[i].title, items[i].url);
    }
    b += "</div>";
    if (seen.empty()) b += "<p class=empty>Type an address or a search in the bar above.</p>";
    return Shell("New tab", b);
}

} // namespace okay::browser
