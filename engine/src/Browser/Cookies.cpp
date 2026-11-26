// Browser (page engine), round 3: the profile's cookies (see Cookies.hpp). Written from RFC 6265 sections 5.1.1 (the cookie
// date algorithm), 5.1.3 (domain matching), 5.1.4 (paths), 5.2 (reading Set-Cookie), 5.3 (the storage model) and 5.4 (the
// Cookie header), and from the RFC 6265bis draft for SameSite, cookie name prefixes and "a page that is not https may not set
// or overwrite a Secure cookie".
#include "okay/Browser/Cookies.hpp"

#include "okay/Browser/Url.hpp"
#include "okay/Core/Json.hpp"
#include "okay/Core/StringUtil.hpp"

#include <algorithm>
#include <limits>

namespace okay::browser {

namespace {

using okay::str::LowerAscii;
using okay::str::Trim;

bool IsDigit(char c) { return c >= '0' && c <= '9'; }

// RFC 6265 5.1.1 "delimiter": %x09 / %x20-2F / %x3B-40 / %x5B-60 / %x7B-7E.
bool IsDateDelimiter(unsigned char c) {
    return c == 0x09 || (c >= 0x20 && c <= 0x2F) || (c >= 0x3B && c <= 0x40) || (c >= 0x5B && c <= 0x60) || (c >= 0x7B && c <= 0x7E);
}

// Days from 1970-01-01 to year-month-day of the proleptic Gregorian calendar (months 1..12). A year is moved to start in
// March so the leap day is the last day of the shifted year; 146097 days make 400 years.
std::int64_t DaysSinceEpoch(std::int64_t y, int m, int d) {
    y -= m <= 2 ? 1 : 0;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const std::int64_t yoe = y - era * 400;                                   // 0 .. 399
    const std::int64_t doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1; // 0 .. 365, from March 1st
    const std::int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;          // 0 .. 146096
    return era * 146097 + doe - 719468;                                       // 719468: days from 0000-03-01 to 1970-01-01
}

// Up to `maxDigits` digits at the start of `t` (at least `minDigits`), then anything that is not a digit.
bool LeadingNumber(const std::string& t, std::size_t minDigits, std::size_t maxDigits, int& out, std::size_t* used = nullptr) {
    std::size_t i = 0;
    int v = 0;
    while (i < t.size() && IsDigit(t[i])) { if (i >= maxDigits) return false; v = v * 10 + (t[i] - '0'); ++i; }
    if (i < minDigits) return false;
    out = v;
    if (used) *used = i;
    return true;
}

// "hh:mm:ss" with 1-2 digits each, then anything that is not a digit.
bool ReadTime(const std::string& t, int& h, int& mi, int& s) {
    std::size_t i = 0, n = 0;
    int parts[3];
    for (int k = 0; k < 3; ++k) {
        if (!LeadingNumber(t.substr(i), 1, 2, parts[k], &n)) return false;
        i += n;
        if (k < 2) { if (i >= t.size() || t[i] != ':') return false; ++i; }
    }
    h = parts[0]; mi = parts[1]; s = parts[2];
    return true;
}

// Domain attribute values that many owners share (a short stand-in for the public suffix list, which we do not have yet).
bool IsSharedSuffix(const std::string& domain) {
    static const char* const kShared[] = {
        "co.uk", "org.uk", "ac.uk", "gov.uk", "me.uk", "net.uk", "ltd.uk", "plc.uk", "com.au", "net.au", "org.au", "edu.au",
        "gov.au", "co.nz", "org.nz", "co.jp", "ne.jp", "or.jp", "ac.jp", "go.jp", "com.br", "com.cn", "net.cn", "org.cn",
        "co.in", "co.kr", "or.kr", "com.mx", "com.tr", "co.za", "com.sg", "com.hk", "com.tw", "github.io", "blogspot.com",
        "herokuapp.com", "appspot.com", "netlify.app", "vercel.app", "pages.dev", "workers.dev", "web.app", "firebaseapp.com"};
    if (domain.find('.') == std::string::npos) return true;
    for (const char* k : kShared) if (domain == k) return true;
    return false;
}

bool IsIpAddress(const std::string& host) {
    if (host.find(':') != std::string::npos || (!host.empty() && host[0] == '[')) return true;   // IPv6
    if (host.empty()) return false;
    for (char c : host) if (!IsDigit(c) && c != '.') return false;
    return true;
}

// RFC 6265 5.1.4: the directory of the request path (up to, not including, its last '/'), or "/".
std::string DefaultPath(const std::string& path) {
    if (path.empty() || path[0] != '/') return "/";
    const std::size_t last = path.rfind('/');
    return last == 0 ? std::string("/") : path.substr(0, last);
}

bool PathMatches(const std::string& requestPath, const std::string& cookiePath) {
    if (requestPath == cookiePath) return true;
    if (requestPath.compare(0, cookiePath.size(), cookiePath) != 0) return false;
    return cookiePath.back() == '/' || (requestPath.size() > cookiePath.size() && requestPath[cookiePath.size()] == '/');
}

bool CleanText(const std::string& s) {
    for (char ch : s) { const unsigned char c = (unsigned char)ch; if (c < 0x20 || c == 0x7F || c == ';') return false; }
    return true;
}

struct Where { std::string host, path; bool https = false; bool ok = false; };
Where Parts(const std::string& url) {
    Where w;
    const Url u = ParseUrl(url);
    if ((u.scheme != "http" && u.scheme != "https") || !u.hasAuthority) return w;
    w.host = HostOf(url);
    if (w.host.empty()) return w;
    w.path = u.path.empty() ? "/" : u.path;
    w.https = u.scheme == "https";
    w.ok = true;
    return w;
}

bool Expired(const Cookie& c, std::int64_t now) { return c.persistent && c.expires <= now; }

} // namespace

bool ParseCookieDate(const std::string& text, std::int64_t& out) {
    bool haveTime = false, haveDay = false, haveMonth = false, haveYear = false;
    int h = 0, mi = 0, s = 0, day = 0, month = 0, year = 0;
    static const char* const kMonths[] = {"jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec"};
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && IsDateDelimiter((unsigned char)text[i])) ++i;
        const std::size_t b = i;
        while (i < text.size() && !IsDateDelimiter((unsigned char)text[i])) ++i;
        if (i == b) continue;
        const std::string tok = text.substr(b, i - b);
        int v = 0;
        if (!haveTime && ReadTime(tok, h, mi, s)) { haveTime = true; continue; }
        if (!haveDay && LeadingNumber(tok, 1, 2, v)) { day = v; haveDay = true; continue; }
        if (!haveMonth && tok.size() >= 3) {
            const std::string m3 = LowerAscii(tok.substr(0, 3));
            bool found = false;
            for (int k = 0; k < 12; ++k) if (m3 == kMonths[k]) { month = k + 1; found = true; break; }
            if (found) { haveMonth = true; continue; }
        }
        if (!haveYear && LeadingNumber(tok, 2, 4, v)) { year = v; haveYear = true; continue; }
    }
    if (haveYear && year >= 70 && year <= 99) year += 1900;
    else if (haveYear && year >= 0 && year <= 69) year += 2000;
    if (!haveTime || !haveDay || !haveMonth || !haveYear) return false;
    if (day < 1 || day > 31 || year < 1601 || h > 23 || mi > 59 || s > 59) return false;
    static const int kDays[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (day > kDays[month - 1]) return false;
    if (month == 2 && day == 29 && !((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) return false;
    out = DaysSinceEpoch(year, month, day) * 86400 + h * 3600 + mi * 60 + s;
    return true;
}

bool DomainMatches(const std::string& host, const std::string& domain) {
    if (domain.empty()) return false;
    if (host == domain) return true;
    if (IsIpAddress(host)) return false;
    return host.size() > domain.size() && host.compare(host.size() - domain.size(), domain.size(), domain) == 0 &&
           host[host.size() - domain.size() - 1] == '.';
}

std::string SiteOf(const std::string& hostIn) {
    const std::string host = LowerAscii(hostIn);
    if (IsIpAddress(host)) return host;
    const std::size_t last = host.rfind('.');
    if (last == std::string::npos || last == 0) return host;
    std::size_t second = host.rfind('.', last - 1);
    if (second == std::string::npos) return host;
    if (IsSharedSuffix(host.substr(second + 1))) {
        if (second == 0) return host;
        const std::size_t third = host.rfind('.', second - 1);
        return third == std::string::npos ? host : host.substr(third + 1);
    }
    return host.substr(second + 1);
}

bool CookieStore::Set(const std::string& url, const std::string& setCookie, std::int64_t now, const Context& ctx) {
    const Where w = Parts(url);
    if (!w.ok || setCookie.size() > kMaxCookieBytes * 2) return false;
    // 5.2 steps 1-6: the name-value pair before the first ';'
    const std::size_t semi = setCookie.find(';');
    const std::string pair = setCookie.substr(0, semi);
    const std::size_t eq = pair.find('=');
    if (eq == std::string::npos) return false;
    Cookie c;
    c.name = Trim(pair.substr(0, eq));
    c.value = Trim(pair.substr(eq + 1));
    if (c.name.empty() || !CleanText(c.name) || !CleanText(c.value) || c.name.find_first_of(" \t=") != std::string::npos) return false;
    if (c.name.size() + c.value.size() > kMaxCookieBytes) return false;
    // the attributes (5.2.1 - 5.2.6 and SameSite)
    std::string domainAttr, pathAttr;
    bool haveMaxAge = false, haveExpires = false;
    std::int64_t maxAgeAt = 0, expiresAt = 0;
    std::size_t i = semi;
    while (i != std::string::npos && i < setCookie.size()) {
        const std::size_t j = setCookie.find(';', i + 1);
        const std::string av = setCookie.substr(i + 1, (j == std::string::npos ? setCookie.size() : j) - i - 1);
        i = j;
        const std::size_t e = av.find('=');
        const std::string name = LowerAscii(Trim(av.substr(0, e)));
        const std::string val = e == std::string::npos ? std::string() : Trim(av.substr(e + 1));
        if (val.size() > 1024) continue;   // 6265bis: an attribute value over 1024 bytes is ignored
        if (name == "expires") {
            std::int64_t t = 0;
            if (ParseCookieDate(val, t)) { haveExpires = true; expiresAt = t; }
        } else if (name == "max-age") {
            std::size_t k = (!val.empty() && val[0] == '-') ? 1 : 0;
            if (k >= val.size()) continue;
            bool digits = true;
            std::int64_t n = 0;
            for (; k < val.size(); ++k) {
                if (!IsDigit(val[k])) { digits = false; break; }
                if (n < 400LL * 86400) n = n * 10 + (val[k] - '0');   // more than 400 days is cut (6265bis 5.5)
            }
            if (!digits) continue;
            n = std::min<std::int64_t>(n, 400LL * 86400);
            haveMaxAge = true;
            maxAgeAt = val[0] == '-' || n == 0 ? std::numeric_limits<std::int64_t>::min() : now + n;
        } else if (name == "domain") {
            std::string d = LowerAscii(val);
            if (!d.empty() && d[0] == '.') d.erase(0, 1);
            domainAttr = d;
        } else if (name == "path") {
            pathAttr = (!val.empty() && val[0] == '/') ? val : std::string();
        } else if (name == "secure") {
            c.secure = true;
        } else if (name == "httponly") {
            c.httpOnly = true;
        } else if (name == "samesite") {
            const std::string v = LowerAscii(val);
            c.sameSite = v == "strict" ? Cookie::Strict : v == "lax" ? Cookie::Lax : v == "none" ? Cookie::None : Cookie::Unset;
        }
    }
    // 5.3: the storage model
    if (haveMaxAge) { c.persistent = true; c.expires = maxAgeAt; }
    else if (haveExpires) { c.persistent = true; c.expires = std::min<std::int64_t>(expiresAt, now + 400LL * 86400); }
    if (!domainAttr.empty()) {
        if (IsSharedSuffix(domainAttr) && domainAttr != w.host) return false;
        if (IsIpAddress(w.host) && domainAttr != w.host) return false;
        if (!DomainMatches(w.host, domainAttr)) return false;
        c.domain = domainAttr;
        c.hostOnly = domainAttr == w.host && IsSharedSuffix(domainAttr);
    } else {
        c.domain = w.host;
        c.hostOnly = true;
    }
    c.path = pathAttr.empty() ? DefaultPath(w.path) : pathAttr;
    if (c.secure && !w.https) return false;
    if (c.httpOnly && ctx.fromScript) return false;
    if (c.sameSite == Cookie::None && !c.secure) return false;
    if (c.name.compare(0, 9, "__Secure-") == 0 && !c.secure) return false;
    if (c.name.compare(0, 7, "__Host-") == 0 && (!c.secure || !c.hostOnly || c.path != "/")) return false;
    // a page that is not https may not overwrite a Secure cookie (6265bis 5.6 step 16)
    if (!w.https)
        for (const Cookie& o : m_items)
            if (o.secure && o.name == c.name && (DomainMatches(c.domain, o.domain) || DomainMatches(o.domain, c.domain)) && PathMatches(c.path, o.path))
                return false;
    c.created = c.lastAccess = now;
    for (std::size_t k = 0; k < m_items.size(); ++k) {
        Cookie& o = m_items[k];
        if (o.name != c.name || o.domain != c.domain || o.hostOnly != c.hostOnly || o.path != c.path) continue;
        if (o.httpOnly && ctx.fromScript) return false;
        if (o.persistent || c.persistent) m_changed = true;
        if (Expired(c, now)) { m_items.erase(m_items.begin() + (std::ptrdiff_t)k); return true; }
        c.created = o.created;
        o = c;
        return true;
    }
    if (Expired(c, now)) return true;   // nothing to remove
    if (c.persistent) m_changed = true;
    const std::string domain = c.domain;
    m_items.push_back(std::move(c));
    Evict(domain);
    return true;
}

void CookieStore::Evict(const std::string& domain) {
    // too many for one domain, or in all: the one used longest ago goes (5.3 step 12, with our own limits)
    auto dropOldest = [this](const std::string* only) {
        std::size_t at = m_items.size();
        for (std::size_t k = 0; k < m_items.size(); ++k) {
            if (only && m_items[k].domain != *only) continue;
            if (at == m_items.size() || m_items[k].lastAccess < m_items[at].lastAccess) at = k;
        }
        if (at == m_items.size()) return;
        if (m_items[at].persistent) m_changed = true;
        m_items.erase(m_items.begin() + (std::ptrdiff_t)at);
    };
    std::size_t same = 0;
    for (const Cookie& c : m_items) if (c.domain == domain) ++same;
    while (same-- > kMaxPerDomain) dropOldest(&domain);
    while (m_items.size() > kMaxCookies) dropOldest(nullptr);
}

std::string CookieStore::Header(const std::string& url, std::int64_t now, const Context& ctx) {
    const Where w = Parts(url);
    if (!w.ok) return std::string();
    std::vector<Cookie*> hit;
    for (Cookie& c : m_items) {
        if (Expired(c, now)) continue;
        if (c.hostOnly ? w.host != c.domain : !DomainMatches(w.host, c.domain)) continue;
        if (!PathMatches(w.path, c.path)) continue;
        if (c.secure && !w.https) continue;
        if (c.httpOnly && ctx.fromScript) continue;
        if (!ctx.sameSite) {
            if (c.sameSite == Cookie::Strict) continue;
            if (c.sameSite != Cookie::None && !(ctx.topLevel && ctx.safeMethod)) continue;   // Lax, and no attribute
        }
        hit.push_back(&c);
    }
    std::stable_sort(hit.begin(), hit.end(), [](const Cookie* a, const Cookie* b) {
        if (a->path.size() != b->path.size()) return a->path.size() > b->path.size();
        return a->created < b->created;
    });
    std::string out;
    for (Cookie* c : hit) {
        const std::size_t add = c->name.size() + 1 + c->value.size() + (out.empty() ? 0 : 2);
        if (out.size() + add > kMaxHeaderBytes) break;
        if (!out.empty()) out += "; ";
        out += c->name + "=" + c->value;
        c->lastAccess = now;
    }
    return out;
}

void CookieStore::ClearSite(const std::string& hostIn) {
    const std::string host = LowerAscii(hostIn);
    const std::size_t before = m_items.size();
    m_items.erase(std::remove_if(m_items.begin(), m_items.end(), [&](const Cookie& c) { return DomainMatches(c.domain, host); }), m_items.end());
    if (m_items.size() != before) m_changed = true;
}

void CookieStore::Expire(std::int64_t now) {
    const std::size_t before = m_items.size();
    m_items.erase(std::remove_if(m_items.begin(), m_items.end(), [&](const Cookie& c) { return Expired(c, now); }), m_items.end());
    if (m_items.size() != before) m_changed = true;
}

std::string CookieStore::ToJson(std::int64_t now) const {
    using okay::json::Escape;
    std::string o = "{\"version\": 1, \"cookies\": [";
    bool first = true;
    for (const Cookie& c : m_items) {
        if (!c.persistent || Expired(c, now)) continue;
        o += first ? "\n  {\"name\": " : ",\n  {\"name\": ";
        first = false;
        Escape(c.name, o);
        o += ", \"value\": ";
        Escape(c.value, o);
        o += ", \"domain\": ";
        Escape(c.domain, o);
        o += ", \"path\": ";
        Escape(c.path, o);
        o += std::string(", \"hostOnly\": ") + (c.hostOnly ? "true" : "false") + ", \"secure\": " + (c.secure ? "true" : "false") +
             ", \"httpOnly\": " + (c.httpOnly ? "true" : "false") + ", \"sameSite\": " + std::to_string((int)c.sameSite) +
             ", \"expires\": " + std::to_string((long long)c.expires) + ", \"created\": " + std::to_string((long long)c.created) + "}";
    }
    o += "]}\n";
    return o;
}

bool CookieStore::FromJson(const std::string& text, std::int64_t now) {
    m_items.clear();
    okay::json::Value v;
    if (!okay::json::Parse(text, v) || !v.IsObject()) return false;
    const okay::json::Value* list = v.Get("cookies");
    if (!list || !list->IsArray()) return false;
    auto str = [](const okay::json::Value& e, const char* k) { const okay::json::Value* x = e.Get(k); return x && x->IsString() ? x->string : std::string(); };
    auto flag = [](const okay::json::Value& e, const char* k) { const okay::json::Value* x = e.Get(k); return x && x->IsBool() && x->boolean; };
    auto num = [](const okay::json::Value& e, const char* k) -> std::int64_t {
        const okay::json::Value* x = e.Get(k);
        return x && x->IsNumber() && x->number > -9.0e15 && x->number < 9.0e15 ? (std::int64_t)x->number : 0;
    };
    for (const okay::json::Value& e : list->array) {
        if (m_items.size() >= kMaxCookies) break;
        Cookie c;
        c.name = str(e, "name");
        c.value = str(e, "value");
        c.domain = LowerAscii(str(e, "domain"));
        c.path = str(e, "path");
        if (c.name.empty() || c.domain.empty() || c.path.empty() || c.path[0] != '/' || !CleanText(c.name) || !CleanText(c.value) ||
            c.name.size() + c.value.size() > kMaxCookieBytes)
            continue;
        c.hostOnly = flag(e, "hostOnly");
        c.secure = flag(e, "secure");
        c.httpOnly = flag(e, "httpOnly");
        const std::int64_t ss = num(e, "sameSite");
        c.sameSite = ss >= 0 && ss <= 3 ? (Cookie::SameSite)ss : Cookie::Unset;
        c.persistent = true;
        c.expires = num(e, "expires");
        c.created = c.lastAccess = num(e, "created");
        if (Expired(c, now)) continue;
        m_items.push_back(std::move(c));
    }
    m_changed = false;
    return true;
}

} // namespace okay::browser
