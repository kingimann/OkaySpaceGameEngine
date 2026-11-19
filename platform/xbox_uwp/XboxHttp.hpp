#pragma once
// HTTP GET with Range for the Xbox UWP build, as PURE logic (no Windows, no engine, no threads, no sleeping): URL checks, request headers,
// Content-Range / Content-Length / Retry-After parsing, redirect and retry policy, and the driver that turns "GET this name, optionally from a byte
// offset" into the head + body pieces the content downloader wants. The real network client (Windows.Web.Http, C++/WinRT) is a separate, UNVERIFIED
// glue (app/xbox_winhttp.hpp) that implements `HttpClient`; tests use a fake one.
// Written from RFC 9110 (HTTP semantics: Range, Content-Range, Retry-After, redirects) and RFC 3986 (URLs), in our own words. The result shapes
// (GetHead / GetHandlers / GetResult) mirror shared/ContentTransport.hpp of the branch claude/content-delivery-1 so an adapter drops in later
// (XboxContentTransport.hpp). That branch is NOT merged here. UNVERIFIED on a console.
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

namespace okay::xbox::http {

// ---- shapes shared with the content downloader ----
struct GetHead { int status = 0; std::uint64_t bodyStart = 0; std::uint64_t totalSize = 0; };
struct GetHandlers { std::function<bool(const GetHead&)> onHead; std::function<bool(const std::uint8_t*, std::size_t)> onData; };
struct GetResult { bool reached = false; bool aborted = false; std::string error; };

// ---- the network client the driver talks to (implemented by the WinRT glue, faked in tests) ----
struct Header { std::string name, value; };
struct Request { std::string url; std::vector<Header> headers; };
struct Head { int status = 0; std::vector<Header> headers; };
struct Sink { std::function<bool(const Head&)> onHead; std::function<bool(const std::uint8_t*, std::size_t)> onData; };   // return false to stop reading
struct ClientResult { bool reached = false; bool aborted = false; std::string error; };
class Client { public: virtual ~Client() = default; virtual ClientResult Send(const Request& r, const Sink& s) = 0; };

// ---- small helpers ----
inline std::string Lower(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }
inline std::string Trim(const std::string& s) { std::size_t a = 0, b = s.size(); while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a; while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b; return s.substr(a, b - a); }
inline const std::string* FindHeader(const std::vector<Header>& h, const std::string& name) { const std::string n = Lower(name); for (const Header& x : h) if (Lower(x.name) == n) return &x.value; return nullptr; }
inline bool ParseU64(const std::string& s, std::uint64_t& out) {
    if (s.empty() || s.size() > 19) return false;
    std::uint64_t v = 0; for (char c : s) { if (c < '0' || c > '9') return false; v = v * 10 + (std::uint64_t)(c - '0'); }
    out = v; return true;
}

/// "https://host[:port]/path?query" parsed and checked: scheme http or https, no user info, a host of letters, digits, '-', '.' (or a [IPv6] literal),
/// a numeric port 1..65535, no control characters or spaces anywhere. Not a full RFC 3986 parser: it refuses what a content URL never needs.
struct Url { std::string scheme, host, pathAndQuery; int port = 0; };
inline bool ParseUrl(const std::string& u, Url& out, std::string* why = nullptr) {
    auto no = [&](const char* m) { if (why) *why = m; return false; };
    for (unsigned char c : u) if (c <= 32 || c == 127) return no("the URL has a space or control character");
    const std::size_t se = u.find("://");
    if (se == std::string::npos) return no("the URL has no scheme");
    Url r; r.scheme = Lower(u.substr(0, se));
    if (r.scheme != "https" && r.scheme != "http") return no("only http and https URLs are supported");
    std::size_t ae = u.find_first_of("/?#", se + 3); if (ae == std::string::npos) ae = u.size();
    std::string auth = u.substr(se + 3, ae - (se + 3));
    if (auth.find('@') != std::string::npos) return no("a URL with a user name or password is refused");
    std::string portS; std::string host = auth;
    if (!auth.empty() && auth[0] == '[') { const std::size_t rb = auth.find(']'); if (rb == std::string::npos) return no("bad IPv6 host"); host = auth.substr(0, rb + 1); if (rb + 1 < auth.size()) { if (auth[rb + 1] != ':') return no("bad host"); portS = auth.substr(rb + 2); } }
    else { const std::size_t c = auth.rfind(':'); if (c != std::string::npos) { host = auth.substr(0, c); portS = auth.substr(c + 1); } }
    if (host.empty()) return no("the URL has no host");
    if (host[0] != '[') for (char c : host) if (!(std::isalnum((unsigned char)c) || c == '-' || c == '.')) return no("the host name has a character that is not allowed");
    r.host = Lower(host); r.port = r.scheme == "https" ? 443 : 80;
    if (!portS.empty()) { std::uint64_t p; if (!ParseU64(portS, p) || p < 1 || p > 65535) return no("bad port"); r.port = (int)p; }
    std::string rest = u.substr(ae); const std::size_t hash = rest.find('#'); if (hash != std::string::npos) rest.resize(hash);   // the fragment is never sent
    r.pathAndQuery = rest.empty() || rest[0] == '?' ? "/" + rest : rest;
    out = r; return true;
}
inline std::string UrlToString(const Url& u) { const bool def = (u.scheme == "https" && u.port == 443) || (u.scheme == "http" && u.port == 80); return u.scheme + "://" + u.host + (def ? "" : ":" + std::to_string(u.port)) + u.pathAndQuery; }

/// "https://host/base" + "models/my ship.glb" -> "https://host/base/models/my%20ship.glb": unreserved characters and '/' stay, everything else is %XX (UTF-8 bytes).
inline std::string JoinUrl(std::string base, const std::string& name) {
    while (!base.empty() && base.back() == '/') base.pop_back();
    static const char* hx = "0123456789ABCDEF"; std::string o = base + "/";
    for (unsigned char c : name) { const bool plain = std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/'; if (plain) o += (char)c; else { o += '%'; o += hx[c >> 4]; o += hx[c & 15]; } }
    return o;
}
/// A redirect's Location against the URL it came from: absolute, scheme-relative ("//host/x"), absolute-path ("/x") or relative ("x") forms.
inline bool ResolveLocation(const Url& from, const std::string& loc, Url& out, std::string* why = nullptr) {
    const std::string l = Trim(loc);
    if (l.empty()) { if (why) *why = "empty Location"; return false; }
    if (l.find("://") != std::string::npos) return ParseUrl(l, out, why);
    if (l.compare(0, 2, "//") == 0) return ParseUrl(from.scheme + ":" + l, out, why);
    Url r = from;
    if (l[0] == '/') r.pathAndQuery = l;
    else { std::string p = from.pathAndQuery; const std::size_t q = p.find('?'); if (q != std::string::npos) p.resize(q); const std::size_t s = p.rfind('/'); p = (s == std::string::npos ? std::string("/") : p.substr(0, s + 1)) + l; r.pathAndQuery = p; }
    const std::size_t hash = r.pathAndQuery.find('#'); if (hash != std::string::npos) r.pathAndQuery.resize(hash);
    out = r; return true;
}

inline std::string RangeHeaderValue(std::uint64_t from) { return "bytes=" + std::to_string((unsigned long long)from) + "-"; }
inline std::string RangeHeaderValue(std::uint64_t from, std::uint64_t to) { return "bytes=" + std::to_string((unsigned long long)from) + "-" + std::to_string((unsigned long long)to); }

/// "bytes 100-199/1234" or "bytes */1234" or "bytes 100-199/*". `unsatisfied`: the "*" form (a 416 reply).
struct ContentRange { std::uint64_t first = 0, last = 0, total = 0; bool hasRange = false, hasTotal = false; };
inline bool ParseContentRange(const std::string& v, ContentRange& out) {
    std::string s = Trim(v); if (Lower(s).compare(0, 6, "bytes ") != 0) return false; s = Trim(s.substr(6));
    const std::size_t slash = s.find('/'); if (slash == std::string::npos) return false;
    const std::string r = Trim(s.substr(0, slash)), t = Trim(s.substr(slash + 1)); ContentRange c;
    if (t != "*") { if (!ParseU64(t, c.total)) return false; c.hasTotal = true; }
    if (r != "*") { const std::size_t dash = r.find('-'); if (dash == std::string::npos || !ParseU64(r.substr(0, dash), c.first) || !ParseU64(r.substr(dash + 1), c.last) || c.last < c.first) return false; c.hasRange = true; if (c.hasTotal && c.last >= c.total) return false; }
    else if (!c.hasTotal) return false;
    out = c; return true;
}
/// Retry-After as a number of seconds (the HTTP-date form is not parsed: it gives `fallbackMs`). Capped at `capMs`.
inline std::uint64_t RetryAfterMs(const std::string* v, std::uint64_t fallbackMs, std::uint64_t capMs) {
    std::uint64_t s = 0; if (!v || !ParseU64(Trim(*v), s)) return std::min(fallbackMs, capMs);
    return s > capMs / 1000 ? capMs : s * 1000;
}

struct Options {
    bool allowHttp = false;        // plain http only for tests / a local server; a console game uses https
    int maxRedirects = 5, maxRetries = 2;
    std::uint64_t retryFallbackMs = 1000, retryCapMs = 15000;
    std::function<void(std::uint64_t)> sleepMs;   // the caller's way to wait (the driver never sleeps itself); null = retry at once
    std::string userAgent = "OkaySpace-Xbox/1";
};

/// GET `name` below `baseUrl`, from byte `rangeFrom` (0 = whole file), at most `maxBytes` of body (0 = no cap). Follows up to maxRedirects redirects (never from
/// https to http, never to another scheme), retries 429 / 503 up to maxRetries times, checks Content-Range on a 206, and reports a body that is cut short or too big.
inline GetResult Get(Client& client, const std::string& baseUrl, const std::string& name, std::uint64_t rangeFrom, std::uint64_t maxBytes, const GetHandlers& h, const Options& opt = Options()) {
    GetResult res;
    if (name.empty() || name.find("..") != std::string::npos || name[0] == '/' || name.find('\\') != std::string::npos || name.find(':') != std::string::npos) { res.error = "refused an unsafe name"; return res; }
    Url cur; std::string why;
    if (!ParseUrl(JoinUrl(baseUrl, name), cur, &why)) { res.error = why; return res; }
    if (cur.scheme == "http" && !opt.allowHttp) { res.error = "plain http is not allowed here: use an https content URL"; return res; }
    int redirects = 0, retries = 0;
    for (int guard = 0; guard < 64; ++guard) {
        Request rq; rq.url = UrlToString(cur);
        rq.headers.push_back({"Accept-Encoding", "identity"});   // a compressed body would make Content-Length and Range offsets mean something else
        rq.headers.push_back({"User-Agent", opt.userAgent});
        if (rangeFrom > 0) rq.headers.push_back({"Range", RangeHeaderValue(rangeFrom)});
        enum class Next { Done, Redirect, Retry } next = Next::Done; Url target; std::uint64_t waitMs = 0;
        GetHead gh; bool haveHead = false, bodyWanted = false; std::uint64_t expected = 0; bool haveExpected = false, tooBig = false, headAborted = false, dataAborted = false; std::uint64_t got = 0; std::string headErr;
        Sink sink;
        sink.onHead = [&](const Head& hd) {
            haveHead = true; gh.status = hd.status;
            const std::string* loc = FindHeader(hd.headers, "Location");
            if ((hd.status == 301 || hd.status == 302 || hd.status == 303 || hd.status == 307 || hd.status == 308) && loc) {
                if (redirects >= opt.maxRedirects) { headErr = "too many redirects"; return false; }
                Url t; std::string w2;
                if (!ResolveLocation(cur, *loc, t, &w2)) { headErr = "bad redirect: " + w2; return false; }
                if (cur.scheme == "https" && t.scheme != "https") { headErr = "a redirect from https to plain http was refused"; return false; }
                if (t.scheme == "http" && !opt.allowHttp) { headErr = "plain http is not allowed here"; return false; }
                target = t; next = Next::Redirect; return false;
            }
            if ((hd.status == 429 || hd.status == 503) && retries < opt.maxRetries) { waitMs = RetryAfterMs(FindHeader(hd.headers, "Retry-After"), opt.retryFallbackMs, opt.retryCapMs); next = Next::Retry; return false; }
            const std::string* cl = FindHeader(hd.headers, "Content-Length"); std::uint64_t n = 0; if (cl && ParseU64(Trim(*cl), n)) { expected = n; haveExpected = true; }
            if (hd.status == 200) { gh.bodyStart = 0; gh.totalSize = haveExpected ? expected : 0; bodyWanted = true; }
            else if (hd.status == 206) {
                ContentRange cr; const std::string* v = FindHeader(hd.headers, "Content-Range");
                if (!v || !ParseContentRange(*v, cr) || !cr.hasRange) { headErr = "the server sent a partial reply without a usable Content-Range"; return false; }
                if (rangeFrom > 0 && cr.first != rangeFrom) { headErr = "the server answered a different byte range than the one asked for"; return false; }
                if (haveExpected && expected != cr.last - cr.first + 1) { headErr = "the Content-Length does not match the Content-Range"; return false; }
                gh.bodyStart = cr.first; gh.totalSize = cr.hasTotal ? cr.total : 0; expected = cr.last - cr.first + 1; haveExpected = true; bodyWanted = true;
            } else if (hd.status == 416) { ContentRange cr; const std::string* v = FindHeader(hd.headers, "Content-Range"); if (v && ParseContentRange(*v, cr) && cr.hasTotal) gh.totalSize = cr.total; }
            else if (hd.status >= 200 && hd.status < 300) { gh.bodyStart = 0; gh.totalSize = haveExpected ? expected : 0; bodyWanted = true; }   // 201, 204 ...: a success with whatever body it has
            if (h.onHead && !h.onHead(gh)) { headAborted = true; return false; }
            return bodyWanted;   // errors (404, 416, 5xx after the retries) carry no body we want
        };
        sink.onData = [&](const std::uint8_t* d, std::size_t n) {
            got += n;
            if (maxBytes && got > maxBytes) { tooBig = true; return false; }
            if (haveExpected && got > expected) { headErr = "the body is longer than its Content-Length"; return false; }
            if (h.onData && !h.onData(d, n)) { dataAborted = true; return false; }
            return true;
        };
        const ClientResult cr = client.Send(rq, sink);
        if (!headErr.empty()) { res.error = headErr; return res; }
        if (headAborted || dataAborted) { res.aborted = true; return res; }
        if (tooBig) { res.error = "the body is larger than allowed"; return res; }
        if (next == Next::Redirect) { cur = target; ++redirects; continue; }
        if (next == Next::Retry) { ++retries; if (opt.sleepMs) opt.sleepMs(waitMs); continue; }
        if (haveHead && !bodyWanted) { res.reached = true; return res; }   // 404, 416 ...: the head was all we wanted
        if (!cr.reached) { res.error = cr.error.empty() ? "no reply from the server" : cr.error; return res; }
        if (!haveHead) { res.error = "no reply from the server"; return res; }
        if (bodyWanted && haveExpected && got < expected) { res.error = "the download was cut short"; return res; }
        res.reached = true; return res;
    }
    res.error = "too many attempts"; return res;
}

} // namespace okay::xbox::http
