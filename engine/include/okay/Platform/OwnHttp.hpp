#pragma once
// ---------------------------------------------------------------------------
// okay::ownhttp - our own HTTP/1.1 client logic (pure: no sockets, no TLS).
//
// This header is the part of the curl replacement that needs no operating
// system: URL parsing, the request builder, the response parser (status line,
// headers, Content-Length, chunked, close-delimited bodies), redirects and the
// limits that keep a hostile server from exhausting memory or time. It talks to
// an abstract `Stream` and an abstract `Connector`, so it is unit tested and
// fuzzed with in-memory streams (tests/test_own_http.cpp). The real transports
// (TCP, CONNECT proxy, TLS through the system OpenSSL) are in OwnHttpNet.hpp.
// See docs/own_http_client.md for the threat model and the list of gaps.
//
// Deliberately strict: bare LF or CR in a head, obsolete line folding, duplicate
// or conflicting Content-Length, Content-Length together with Transfer-Encoding,
// unknown transfer or content encodings, CR/LF/NUL in anything we send, and an
// https -> http redirect are all refused. We send `Accept-Encoding: identity` (gzip only when asked) and
// `Connection: close`, unless the caller asks for keep-alive (FetchRequest::keepAlive): then a finished
// connection with clear framing can be handed to the Connector and used again by the next request to the same
// host (idempotent methods only, one safe retry on a connection that went stale). Cookies are off unless the caller
// passes a CookieJar (a per-call jar: no disk, no shared state).
// ---------------------------------------------------------------------------
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "okay/Platform/OwnHttpInflate.hpp"

namespace okay::ownhttp {

using Clock = std::chrono::steady_clock;

enum class Err {
    None, BadUrl, BadHeader, Connect, Tls, Timeout, Io, BadResponse,
    TooLarge, TooManyRedirects, DowngradeRefused, Unsupported, Proxy, Aborted
};

inline const char* ErrName(Err e) {
    switch (e) {
    case Err::None: return "none";
    case Err::BadUrl: return "bad-url";
    case Err::BadHeader: return "bad-header";
    case Err::Connect: return "connect";
    case Err::Tls: return "tls";
    case Err::Timeout: return "timeout";
    case Err::Io: return "io";
    case Err::BadResponse: return "bad-response";
    case Err::TooLarge: return "too-large";
    case Err::TooManyRedirects: return "too-many-redirects";
    case Err::DowngradeRefused: return "downgrade-refused";
    case Err::Unsupported: return "unsupported";
    case Err::Proxy: return "proxy";
    case Err::Aborted: return "aborted";
    }
    return "?";
}

/// Byte stream the parser reads from and the builder writes to.
struct Stream {
    virtual ~Stream() = default;
    /// >0 bytes read, 0 = orderly end of stream, <0 = error (sets `e`).
    virtual long Read(void* buf, size_t n, Err& e) = 0;
    /// Writes all `n` bytes or fails (sets `e`).
    virtual bool Write(const void* buf, size_t n, Err& e) = 0;
    /// Keep-alive support: called when an idle connection is taken from the pool again (new deadline, silence limit and
    /// cancel check for the new request) and when it is put back (cancel = nullptr). Default: nothing to do.
    virtual void Rebind(Clock::time_point /*deadline*/, int /*ioMs*/, const std::function<bool()>* /*cancel*/) {}
    /// Keep-alive support: false when the peer has closed the connection or sent bytes nobody asked for.
    virtual bool StillUsable() { return true; }
};

struct Limits {
    size_t   maxHeaderBytes = 64 * 1024;   ///< whole response head
    size_t   maxHeaders     = 100;
    uint64_t maxBodyBytes   = 64ull * 1024 * 1024;
    int      maxRedirects   = 10;
    bool     allowGzip      = false;       ///< accept Content-Encoding gzip, x-gzip and deflate (else they are refused)
    uint64_t maxEncodedBytes = 64ull * 1024 * 1024;   ///< compressed body held in memory before it is decoded
};

struct Header { std::string name, value; };

struct Url {
    std::string scheme;   ///< "http" or "https"
    std::string host;     ///< lower case, without [] for IPv6
    int         port = 0;
    std::string target;   ///< path and query, never empty, starts with '/'
    bool Https() const { return scheme == "https"; }
    int  DefaultPort() const { return Https() ? 443 : 80; }
    bool IsV6() const { return host.find(':') != std::string::npos; }
    std::string HostPort() const {   // authority as sent in Host: and CONNECT
        std::string h = IsV6() ? "[" + host + "]" : host;
        if (port != DefaultPort()) h += ":" + std::to_string(port);
        return h;
    }
    bool SameOrigin(const Url& o) const { return scheme == o.scheme && host == o.host && port == o.port; }
};

struct Response {
    int         status = 0;
    std::string reason;
    int         minor = 1;                 ///< HTTP/1.x
    std::vector<Header> headers;
    std::string body;                      ///< unless a data callback took it
    uint64_t    bodyBytes = 0;                 ///< bytes handed on (decoded size for gzip)
    const std::string* Get(const std::string& name) const;
};

namespace detail {
inline char Lc(char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; }
inline std::string Lower(std::string s) { for (char& c : s) c = Lc(c); return s; }
inline bool IEq(const std::string& a, const char* b) {
    size_t n = std::strlen(b);
    if (a.size() != n) return false;
    for (size_t i = 0; i < n; ++i) if (Lc(a[i]) != Lc(b[i])) return false;
    return true;
}
inline bool IPrefix(const std::string& a, const char* p) {
    size_t n = std::strlen(p);
    if (a.size() < n) return false;
    for (size_t i = 0; i < n; ++i) if (Lc(a[i]) != Lc(p[i])) return false;
    return true;
}
inline std::string Trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t')) --e;
    return s.substr(b, e - b);
}
inline bool IsTokenChar(unsigned char c) {
    if (c >= '0' && c <= '9') return true;
    if (c >= 'a' && c <= 'z') return true;
    if (c >= 'A' && c <= 'Z') return true;
    return c && std::strchr("!#$%&'*+-.^_`|~", c) != nullptr;
}
inline bool IsToken(const std::string& s) {
    if (s.empty()) return false;
    for (unsigned char c : s) if (!IsTokenChar(c)) return false;
    return true;
}
/// A header value we are willing to send or accept: no CR, LF, NUL or other control
/// characters except HT.
inline bool CleanValue(const std::string& s) {
    for (unsigned char c : s) if ((c < 0x20 && c != '\t') || c == 0x7f) return false;
    return true;
}
inline bool Set(Err& e, std::string* m, Err code, const char* msg) {
    e = code; if (m) *m = msg; return false;
}
} // namespace detail

inline const std::string* Response::Get(const std::string& name) const {
    for (const Header& h : headers) if (detail::IEq(h.name, detail::Lower(name).c_str())) return &h.value;
    return nullptr;
}

// ----------------------------------------------------------------------------
// URL
// ----------------------------------------------------------------------------
inline bool ParseUrl(const std::string& in, Url& u, std::string* why = nullptr) {
    auto fail = [&](const char* m) { if (why) *why = m; return false; };
    if (in.size() > 8192) return fail("url too long");
    for (unsigned char c : in)
        if (c <= 0x20 || c == 0x7f || c >= 0x80 || c == '\\') return fail("illegal character in url");
    Url r;
    size_t p = 0;
    if (detail::IPrefix(in, "https://")) { r.scheme = "https"; p = 8; }
    else if (detail::IPrefix(in, "http://")) { r.scheme = "http"; p = 7; }
    else return fail("unsupported scheme");
    size_t aend = in.find_first_of("/?#", p);
    if (aend == std::string::npos) aend = in.size();
    std::string auth = in.substr(p, aend - p), portStr;
    if (auth.empty()) return fail("empty host");
    if (auth.find('@') != std::string::npos) return fail("userinfo in url is not supported");
    if (auth[0] == '[') {
        size_t cl = auth.find(']');
        if (cl == std::string::npos) return fail("bad ipv6 literal");
        r.host = auth.substr(1, cl - 1);
        if (r.host.find(':') == std::string::npos) return fail("bad ipv6 literal");
        for (unsigned char c : r.host)
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') || c == ':' || c == '.'))
                return fail("bad ipv6 literal");
        std::string rest = auth.substr(cl + 1);
        if (!rest.empty()) { if (rest[0] != ':') return fail("bad authority"); portStr = rest.substr(1); }
    } else {
        size_t col = auth.rfind(':');
        if (col == std::string::npos) r.host = auth;
        else { r.host = auth.substr(0, col); portStr = auth.substr(col + 1); }
        if (r.host.find(':') != std::string::npos) return fail("bad authority");
        for (unsigned char c : r.host)
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' || c == '_' || c == '.'))
                return fail("illegal character in host");
    }
    if (r.host.empty() || r.host.size() > 253) return fail("bad host");
    r.host = detail::Lower(r.host);
    if (portStr.empty()) r.port = r.Https() ? 443 : 80;
    else {
        if (portStr.size() > 5) return fail("bad port");
        int v = 0;
        for (unsigned char c : portStr) { if (c < '0' || c > '9') return fail("bad port"); v = v * 10 + (c - '0'); }
        if (v < 1 || v > 65535) return fail("bad port");
        r.port = v;
    }
    std::string rest = in.substr(aend);
    size_t hash = rest.find('#');
    if (hash != std::string::npos) rest.erase(hash);
    if (rest.empty() || rest[0] == '?') rest = "/" + rest;
    r.target = rest;
    u = r;
    return true;
}

inline std::string RemoveDotSegments(const std::string& path) {
    // `path` starts with '/'. Segments are split on '/', "." and ".." are resolved.
    std::vector<std::string> out;
    bool dirEnd = false;
    size_t i = 1;
    while (i <= path.size()) {
        size_t j = path.find('/', i);
        const bool last = (j == std::string::npos);
        std::string seg = path.substr(i, (last ? path.size() : j) - i);
        if (seg == "..") { if (!out.empty()) out.pop_back(); dirEnd = last; }
        else if (seg == ".") dirEnd = last;
        else if (seg.empty() && last) dirEnd = true;
        else { out.push_back(seg); dirEnd = false; }
        if (last) break;
        i = j + 1;
    }
    std::string r;
    for (const std::string& s : out) r += "/" + s;
    if (r.empty() || dirEnd) r += "/";
    return r;
}

/// Resolve a Location header against the URL that produced it.
inline bool ResolveLocation(const Url& base, const std::string& locIn, Url& out, std::string* why = nullptr) {
    auto fail = [&](const char* m) { if (why) *why = m; return false; };
    std::string loc = detail::Trim(locIn);
    if (loc.empty()) return fail("empty Location");
    for (unsigned char c : loc) if (c <= 0x20 || c == 0x7f || c >= 0x80 || c == '\\') return fail("illegal character in Location");
    if (detail::IPrefix(loc, "http://") || detail::IPrefix(loc, "https://")) return ParseUrl(loc, out, why);
    size_t colon = loc.find(':'), slash = loc.find_first_of("/?#");
    if (colon != std::string::npos && (slash == std::string::npos || colon < slash)) return fail("unsupported scheme in Location");
    if (loc.compare(0, 2, "//") == 0) return ParseUrl(base.scheme + ":" + loc, out, why);
    std::string origin = base.scheme + "://" + base.HostPort();
    if (loc[0] == '/') return ParseUrl(origin + loc, out, why);
    std::string path = base.target, query;
    size_t q = path.find('?');
    if (q != std::string::npos) { query = path.substr(q); path.erase(q); }
    if (loc[0] == '?') return ParseUrl(origin + path + loc, out, why);
    if (loc[0] == '#') return ParseUrl(origin + path + query, out, why);
    size_t ls = path.rfind('/');
    std::string dir = ls == std::string::npos ? "/" : path.substr(0, ls + 1);
    std::string rel = loc, tail;
    size_t t = rel.find_first_of("?#");
    if (t != std::string::npos) { tail = rel.substr(t); rel.erase(t); }
    return ParseUrl(origin + RemoveDotSegments(dir + rel) + tail, out, why);
}

// ----------------------------------------------------------------------------
// Request builder
// ----------------------------------------------------------------------------
/// Split a "Name: value" line. Refuses anything that could inject a second header
/// or request line (CR, LF, NUL, other controls) and malformed names.
inline bool ParseHeaderLine(const std::string& line, Header& h) {
    size_t c = line.find(':');
    if (c == std::string::npos) return false;
    std::string name = line.substr(0, c), value = detail::Trim(line.substr(c + 1));
    if (!detail::IsToken(name) || !detail::CleanValue(value)) return false;
    h.name = name; h.value = value;
    return true;
}

inline bool BuildRequest(const std::string& method, const Url& u, const std::vector<Header>& headers,
                         const std::string& body, bool absoluteForm, std::string& out,
                         Err& e, std::string* msg = nullptr, bool acceptGzip = false, bool keepAlive = false) {
    using detail::Set;
    if (!detail::IsToken(method) || method.size() > 16) return Set(e, msg, Err::BadHeader, "bad method");
    if (u.host.empty() || u.target.empty() || u.target[0] != '/') return Set(e, msg, Err::BadUrl, "bad url");
    for (unsigned char c : u.target) if (c <= 0x20 || c == 0x7f || c >= 0x80) return Set(e, msg, Err::BadUrl, "control character in url");
    static const char* const kOwned[] = {"host", "content-length", "transfer-encoding", "connection",
        "accept-encoding", "expect", "upgrade", "te", "proxy-connection", "trailer"};
    bool haveUa = false;
    for (const Header& h : headers) {
        if (!detail::IsToken(h.name) || !detail::CleanValue(h.value)) return Set(e, msg, Err::BadHeader, "bad header");
        for (const char* o : kOwned) if (detail::IEq(h.name, o)) return Set(e, msg, Err::BadHeader, "header is set by the client");
        if (detail::IEq(h.name, "user-agent")) haveUa = true;
    }
    std::string r = method + " ";
    if (absoluteForm) r += u.scheme + "://" + u.HostPort();
    r += u.target + " HTTP/1.1\r\nHost: " + u.HostPort() + "\r\n";
    if (!haveUa) r += "User-Agent: OkaySpace/1.0\r\n";
    r += acceptGzip ? "Accept-Encoding: gzip, deflate\r\n" : "Accept-Encoding: identity\r\n";
    if (!keepAlive) r += "Connection: close\r\n";
    for (const Header& h : headers) r += h.name + ": " + h.value + "\r\n";
    if (!body.empty() || method == "POST" || method == "PUT" || method == "PATCH")
        r += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    r += "\r\n";
    r += body;
    out = std::move(r);
    e = Err::None;
    return true;
}

// ----------------------------------------------------------------------------
// Response reader
// ----------------------------------------------------------------------------
struct Reader {
    Stream& s;
    std::string buf;
    size_t pos = 0;
    bool eof = false;
    explicit Reader(Stream& st) : s(st) {}
    size_t Avail() const { return buf.size() - pos; }
    /// Read more. false on error (e set) or end of stream (eof set, e None).
    bool Fill(Err& e) {
        if (eof) return false;
        if (pos == buf.size()) { buf.clear(); pos = 0; }
        else if (pos > 65536) { buf.erase(0, pos); pos = 0; }
        char tmp[16384];
        e = Err::None;
        long n = s.Read(tmp, sizeof tmp, e);
        if (n < 0) { if (e == Err::None) e = Err::Io; return false; }
        if (n == 0) { eof = true; return false; }
        buf.append(tmp, size_t(n));
        return true;
    }
    bool More(Err& e, std::string* m) {
        if (Fill(e)) return true;
        if (e == Err::None) detail::Set(e, m, Err::BadResponse, "connection closed early");
        else if (m) *m = std::string("read failed: ") + ErrName(e);
        return false;
    }
    /// One CRLF terminated line without the CRLF. Strict: bare LF or CR is an error.
    bool Line(size_t maxLen, std::string& line, Err& e, std::string* m) {
        size_t scanned = 0;   // bytes after pos already searched (Fill may move pos)
        for (;;) {
            size_t nl = buf.find('\n', pos + scanned);
            if (nl != std::string::npos) {
                if (nl == pos || buf[nl - 1] != '\r') return detail::Set(e, m, Err::BadResponse, "bare LF in response");
                if (nl - pos > maxLen) return detail::Set(e, m, Err::BadResponse, "response line too long");
                line.assign(buf, pos, nl - 1 - pos);
                if (line.find('\r') != std::string::npos || line.find('\0') != std::string::npos)
                    return detail::Set(e, m, Err::BadResponse, "bare CR or NUL in response");
                pos = nl + 1;
                return true;
            }
            if (Avail() > maxLen) return detail::Set(e, m, Err::BadResponse, "response line too long");
            scanned = Avail();
            if (!More(e, m)) return false;
        }
    }
};

/// Reads the response head (skipping 1xx interim responses except 101).
inline bool ReadHead(Reader& rd, const Limits& lim, Response& out, Err& e, std::string* m = nullptr) {
    using detail::Set;
    size_t headBytes = 0;
    for (int interim = 0; interim < 8; ++interim) {
        Response r;
        std::string line;
        if (!rd.Line(4096, line, e, m)) return false;
        headBytes += line.size() + 2;
        if (line.size() < 12 || line.compare(0, 7, "HTTP/1.") != 0 || (line[7] != '0' && line[7] != '1') || line[8] != ' ')
            return Set(e, m, Err::BadResponse, "bad status line");
        r.minor = line[7] - '0';
        for (int i = 9; i < 12; ++i) if (line[i] < '0' || line[i] > '9') return Set(e, m, Err::BadResponse, "bad status code");
        r.status = (line[9] - '0') * 100 + (line[10] - '0') * 10 + (line[11] - '0');
        if (r.status < 100) return Set(e, m, Err::BadResponse, "bad status code");
        if (line.size() > 12) {
            if (line[12] != ' ') return Set(e, m, Err::BadResponse, "bad status line");
            r.reason = line.substr(13);
            if (!detail::CleanValue(r.reason)) return Set(e, m, Err::BadResponse, "bad reason phrase");
        }
        for (;;) {
            if (!rd.Line(lim.maxHeaderBytes, line, e, m)) return false;
            headBytes += line.size() + 2;
            if (headBytes > lim.maxHeaderBytes) return Set(e, m, Err::TooLarge, "response head too large");
            if (line.empty()) break;
            if (line[0] == ' ' || line[0] == '\t') return Set(e, m, Err::BadResponse, "obsolete line folding");
            size_t c = line.find(':');
            if (c == std::string::npos) return Set(e, m, Err::BadResponse, "header without colon");
            Header h;
            h.name = line.substr(0, c);
            h.value = detail::Trim(line.substr(c + 1));
            if (!detail::IsToken(h.name)) return Set(e, m, Err::BadResponse, "bad header name");
            if (!detail::CleanValue(h.value)) return Set(e, m, Err::BadResponse, "bad header value");
            if (r.headers.size() >= lim.maxHeaders) return Set(e, m, Err::TooLarge, "too many headers");
            r.headers.push_back(std::move(h));
        }
        if (r.status == 101) return Set(e, m, Err::BadResponse, "protocol switch refused");
        if (r.status >= 100 && r.status < 200) continue;
        out = std::move(r);
        e = Err::None;
        return true;
    }
    return Set(e, m, Err::BadResponse, "too many interim responses");
}

enum class Framing { None, Length, Chunked, UntilClose };

/// 0 = none/identity or not decodable, 1 = gzip, 2 = zlib "deflate". Only when Limits::allowGzip.
inline int ContentCoding(const Response& r, const Limits& lim) {
    const std::string* ce = r.Get("content-encoding");
    if (!ce || !lim.allowGzip) return 0;
    std::string v = detail::Trim(*ce);
    if (detail::IEq(v, "gzip") || detail::IEq(v, "x-gzip")) return 1;
    if (detail::IEq(v, "deflate")) return 2;
    return 0;
}

inline bool DetermineFraming(const Response& r, const std::string& method, const Limits& lim,
                             Framing& f, uint64_t& len, Err& e, std::string* m = nullptr) {
    using detail::Set;
    len = 0;
    // No body follows a HEAD answer, 204 or 304: what its headers say about encodings and lengths does not matter.
    if (method == "HEAD" || r.status == 204 || r.status == 304) { f = Framing::None; e = Err::None; return true; }
    if (const std::string* ce = r.Get("content-encoding"))
        if (!detail::IEq(detail::Trim(*ce), "identity") && ContentCoding(r, lim) == 0) return Set(e, m, Err::Unsupported, "content encoding not supported");
    std::string te; bool haveTe = false, haveCl = false; std::string clVal;
    for (const Header& h : r.headers) {
        if (detail::IEq(h.name, "transfer-encoding")) { te += (haveTe ? "," : "") + h.value; haveTe = true; }
        else if (detail::IEq(h.name, "content-length")) {
            std::string v = detail::Trim(h.value);
            if (haveCl && v != clVal) return Set(e, m, Err::BadResponse, "conflicting Content-Length");
            haveCl = true; clVal = v;
        }
    }
    if (haveTe) {
        if (haveCl) return Set(e, m, Err::BadResponse, "Content-Length with Transfer-Encoding");
        if (!detail::IEq(detail::Trim(te), "chunked")) return Set(e, m, Err::Unsupported, "transfer encoding not supported");
        f = Framing::Chunked; e = Err::None; return true;
    }
    if (haveCl) {
        if (clVal.empty() || clVal.size() > 19) return Set(e, m, Err::BadResponse, "bad Content-Length");
        uint64_t v = 0;
        for (unsigned char c : clVal) { if (c < '0' || c > '9') return Set(e, m, Err::BadResponse, "bad Content-Length"); v = v * 10 + (c - '0'); }
        if (v > lim.maxBodyBytes) return Set(e, m, Err::TooLarge, "body larger than the limit");
        len = v; f = Framing::Length; e = Err::None; return true;
    }
    f = Framing::UntilClose; e = Err::None; return true;
}

using DataFn = std::function<bool(const char*, size_t)>;

/// Read the body of `head` into `data`. Returns false with e/m set on any violation.
inline bool ReadBody(Reader& rd, const Response& head, const std::string& method, const Limits& lim,
                     const DataFn& data, uint64_t& total, Err& e, std::string* m = nullptr) {
    using detail::Set;
    total = 0;
    Framing f; uint64_t len = 0;
    if (!DetermineFraming(head, method, lim, f, len, e, m)) return false;
    // gzip / deflate: collect the encoded body (capped), decode it at the end (capped), hand the result on.
    const int coding = (f == Framing::None) ? 0 : ContentCoding(head, lim);
    std::string encoded;
    uint64_t encodedTotal = 0;
    auto give = [&](size_t n) -> bool {
        if (coding) {
            if (encodedTotal + n > std::min(lim.maxEncodedBytes, lim.maxBodyBytes)) return Set(e, m, Err::TooLarge, "encoded body larger than the limit");
            encoded.append(rd.buf.data() + rd.pos, n);
            encodedTotal += n; rd.pos += n; return true;
        }
        if (total + n > lim.maxBodyBytes) return Set(e, m, Err::TooLarge, "body larger than the limit");
        if (n && data && !data(rd.buf.data() + rd.pos, n)) return Set(e, m, Err::Aborted, "aborted by the caller");
        total += n; rd.pos += n; return true;
    };
    auto finish = [&]() -> bool {
        if (!coding) return true;
        std::string out, why;
        if (!InflateBody(encoded, coding == 1, size_t(std::min<uint64_t>(lim.maxBodyBytes, SIZE_MAX / 2)), out, why)) {
            Set(e, m, why.find("larger than the limit") != std::string::npos ? Err::TooLarge : Err::BadResponse, why.c_str());
            return false;
        }
        encoded.clear(); encoded.shrink_to_fit();
        if (!out.empty() && data && !data(out.data(), out.size())) return Set(e, m, Err::Aborted, "aborted by the caller");
        total = out.size(); e = Err::None; return true;
    };
    if (f == Framing::None) { e = Err::None; return true; }
    if (f == Framing::Length) {
        uint64_t left = len;
        while (left) {
            if (!rd.Avail() && !rd.More(e, m)) return false;
            size_t n = size_t(std::min<uint64_t>(left, rd.Avail()));
            if (!give(n)) return false;
            left -= n;
        }
        e = Err::None; return finish();
    }
    if (f == Framing::UntilClose) {
        for (;;) {
            if (rd.Avail() && !give(rd.Avail())) return false;
            if (!rd.Fill(e)) {
                if (e != Err::None) { if (m) *m = "read failed"; return false; }
                e = Err::None; return finish();   // end of stream ends the body
            }
        }
    }
    // chunked
    for (;;) {
        std::string line;
        if (!rd.Line(4096, line, e, m)) return false;
        size_t semi = line.find(';');
        std::string hex = semi == std::string::npos ? line : line.substr(0, semi);
        if (hex.empty() || hex.size() > 16) return Set(e, m, Err::BadResponse, "bad chunk size");
        uint64_t sz = 0;
        for (unsigned char c : hex) {
            int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
            if (d < 0) return Set(e, m, Err::BadResponse, "bad chunk size");
            sz = sz * 16 + uint64_t(d);
        }
        if (sz == 0) {
            size_t trailer = 0;
            for (;;) {   // trailers are read, validated and ignored
                if (!rd.Line(4096, line, e, m)) return false;
                if (line.empty()) { e = Err::None; return finish(); }
                trailer += line.size() + 2;
                if (trailer > 16384) return Set(e, m, Err::TooLarge, "trailers too large");
                if (line[0] == ' ' || line[0] == '\t' || line.find(':') == std::string::npos) return Set(e, m, Err::BadResponse, "bad trailer");
            }
        }
        if (sz > lim.maxBodyBytes || total + sz > lim.maxBodyBytes) return Set(e, m, Err::TooLarge, "body larger than the limit");
        uint64_t left = sz;
        while (left) {
            if (!rd.Avail() && !rd.More(e, m)) return false;
            size_t n = size_t(std::min<uint64_t>(left, rd.Avail()));
            if (!give(n)) return false;
            left -= n;
        }
        while (rd.Avail() < 2) if (!rd.More(e, m)) return false;
        if (rd.buf[rd.pos] != '\r' || rd.buf[rd.pos + 1] != '\n') return Set(e, m, Err::BadResponse, "chunk not followed by CRLF");
        rd.pos += 2;
    }
}

// ----------------------------------------------------------------------------
// Cookies (opt-in, per call)
// ----------------------------------------------------------------------------
/// A small cookie jar for ONE request and its redirects (or for a few calls a caller chooses to chain). Off unless the
/// caller hands one to FetchRequest::jar. Deliberately simple and strict, not RFC 6265 complete:
///  - host-only: a cookie goes back only to the exact host that set it (a Domain attribute is accepted only when it
///    names that same host; anything else drops the cookie);
///  - Path default and matching as in the RFC; Secure is sent only over https, and a Secure cookie from plain http is dropped;
///  - Max-Age is honoured (zero or negative deletes); Expires is ignored (the cookie lasts as long as the jar);
///  - at most 64 cookies, 4096 bytes each, a Cookie header of at most 8000 bytes; names must be tokens and values clean.
/// Not thread safe: one jar belongs to one call at a time.
class CookieJar {
public:
    struct Cookie { std::string name, value, host, path; bool secure = false; bool hasExpiry = false; Clock::time_point expires; };
    static constexpr size_t kMaxCookies = 64, kMaxCookieBytes = 4096, kMaxHeaderBytes = 8000;

    /// Store one Set-Cookie value that came from `from`. False when the cookie was refused (nothing stored).
    bool Store(const Url& from, const std::string& setCookie) {
        if (setCookie.size() > kMaxCookieBytes || !detail::CleanValue(setCookie)) return false;
        size_t semi = setCookie.find(';');
        const std::string pair = detail::Trim(setCookie.substr(0, semi));
        size_t eq = pair.find('=');
        if (eq == std::string::npos) return false;
        Cookie c;
        c.name = detail::Trim(pair.substr(0, eq)); c.value = detail::Trim(pair.substr(eq + 1));
        if (!detail::IsToken(c.name)) return false;
        if (c.value.size() >= 2 && c.value.front() == '"' && c.value.back() == '"') { /* quoted value is kept as sent */ }
        c.host = from.host;
        std::string path;
        bool deleteIt = false;
        size_t i = semi;
        while (i != std::string::npos && i < setCookie.size()) {
            size_t j = setCookie.find(';', i + 1);
            std::string attr = detail::Trim(setCookie.substr(i + 1, (j == std::string::npos ? setCookie.size() : j) - i - 1));
            i = j;
            size_t e2 = attr.find('=');
            const std::string an = detail::Lower(detail::Trim(attr.substr(0, e2)));
            const std::string av = e2 == std::string::npos ? std::string() : detail::Trim(attr.substr(e2 + 1));
            if (an == "domain") {
                std::string d = detail::Lower(av);
                if (!d.empty() && d[0] == '.') d.erase(0, 1);
                if (d != from.host) return false;   // host-only jar: a wider domain is refused
            } else if (an == "path") {
                if (!av.empty() && av[0] == '/') path = av;
            } else if (an == "secure") {
                c.secure = true;
            } else if (an == "max-age") {
                bool neg = !av.empty() && av[0] == '-';
                size_t k = neg ? 1 : 0; long long v = 0; bool digits = k < av.size() && av.size() - k < 10;
                for (; k < av.size(); ++k) { if (av[k] < '0' || av[k] > '9') { digits = false; break; } v = v * 10 + (av[k] - '0'); }
                if (!digits) continue;   // an unreadable Max-Age is ignored
                if (neg || v == 0) deleteIt = true;
                else { c.hasExpiry = true; c.expires = Clock::now() + std::chrono::seconds(v); }
            }
        }
        if (c.secure && !from.Https()) return false;
        if (path.empty()) {   // default path: the request path up to (not including) its last '/'
            std::string rp = from.target.substr(0, from.target.find('?'));
            size_t ls = rp.rfind('/');
            path = (ls == std::string::npos || ls == 0) ? "/" : rp.substr(0, ls);
        }
        c.path = path;
        for (size_t k = 0; k < items_.size(); ++k) {
            if (items_[k].name == c.name && items_[k].host == c.host && items_[k].path == c.path) {
                if (deleteIt) items_.erase(items_.begin() + long(k));
                else items_[k] = c;
                return true;
            }
        }
        if (deleteIt) return true;
        if (items_.size() >= kMaxCookies) return false;
        items_.push_back(c);
        return true;
    }

    /// The value for a Cookie header for a request to `u` ("a=1; b=2"), empty when none match.
    std::string HeaderFor(const Url& u) const {
        const std::string rp = u.target.substr(0, u.target.find('?'));
        std::vector<const Cookie*> hit;
        const Clock::time_point now = Clock::now();
        for (const Cookie& c : items_) {
            if (c.host != u.host) continue;
            if (c.secure && !u.Https()) continue;
            if (c.hasExpiry && c.expires <= now) continue;
            const bool pathOk = rp == c.path ||
                (rp.compare(0, c.path.size(), c.path) == 0 && (c.path.back() == '/' || rp[c.path.size()] == '/'));
            if (!pathOk) continue;
            hit.push_back(&c);
        }
        std::stable_sort(hit.begin(), hit.end(), [](const Cookie* a, const Cookie* b) { return a->path.size() > b->path.size(); });
        std::string out;
        for (const Cookie* c : hit) {
            std::string one = c->name + "=" + c->value;
            if (out.size() + one.size() + 2 > kMaxHeaderBytes) break;
            if (!out.empty()) out += "; ";
            out += one;
        }
        return out;
    }
    size_t Size() const { return items_.size(); }
    void Clear() { items_.clear(); }
    const std::vector<Cookie>& Items() const { return items_; }
private:
    std::vector<Cookie> items_;
};

// ----------------------------------------------------------------------------
// Fetch with redirects
// ----------------------------------------------------------------------------
struct Connector {
    virtual ~Connector() = default;
    /// Open a stream to `u` (TCP, proxy tunnel and TLS as needed).
    virtual std::unique_ptr<Stream> Open(const Url& u, Clock::time_point deadline, Err& e, std::string& msg) = 0;
    /// True when `u` is plain http reached through a proxy (request line uses the absolute form).
    virtual bool AbsoluteForm(const Url&) { return false; }
    /// Keep-alive: an idle connection that can serve `u` again, rebound to `deadline`, or null. Default: no pool.
    virtual std::unique_ptr<Stream> TakeIdle(const Url&, Clock::time_point /*deadline*/) { return nullptr; }
    /// Keep-alive: a connection whose last response was read completely and cleanly; keep it for the next request or close it.
    virtual void GiveIdle(const Url&, std::unique_ptr<Stream>) {}
};

struct FetchRequest {
    std::string method = "GET";
    std::string url;
    std::vector<std::string> headerLines;   ///< "Name: value"
    std::string body;
    bool acceptGzip = false;                ///< ask for gzip/deflate and decode it (Limits::allowGzip is set for the call)
    /// Do not send `Connection: close`; offer finished connections to the Connector and reuse its idle ones. Only
    /// idempotent methods (GET, HEAD, OPTIONS, PUT, DELETE) ever ride on a reused connection, and a reused connection
    /// that turns out to be stale (closed before one byte of the answer) is replaced once by a fresh one.
    bool keepAlive = false;
    std::shared_ptr<CookieJar> jar;         ///< null = cookies off (the default)
};

struct FetchHooks {
    /// Called once with the final response head (after redirects), before any data.
    std::function<bool(const Response&)> onHead;
    /// Body data of the final response. When empty the body is collected in Response::body.
    DataFn onData;
    /// Progress of the final response body: bytes handed on so far and the expected total (Content-Length, 0 when
    /// unknown or encoded). Return false to stop the transfer (Err::Aborted).
    std::function<bool(uint64_t done, uint64_t total)> onProgress;
};

struct FetchResult {
    bool        ok = false;       ///< a final response was read completely
    Err         err = Err::None;
    std::string message;
    Response    response;
    Url         finalUrl;
    int         redirects = 0;
    bool        partial = false;        ///< the server answered a Range request with 206 (the body starts at rangeStart)
    uint64_t    rangeStart = 0;
    uint64_t    rangeTotal = 0;         ///< complete size from Content-Range, 0 when unknown ("*")
    int         reusedConnections = 0;  ///< requests of this call (hops) that rode on a kept-alive connection
    int         staleRetries = 0;       ///< reused connections found dead and replaced by a fresh one
};

/// "bytes first-last/total" (total may be "*"). Strict: digits only, first <= last < total.
inline bool ParseContentRange(const std::string& v, uint64_t& first, uint64_t& last, uint64_t& total) {
    std::string s = detail::Trim(v);
    if (!detail::IPrefix(s, "bytes ")) return false;
    s = detail::Trim(s.substr(6));
    size_t dash = s.find('-'), slash = s.find('/');
    if (dash == std::string::npos || slash == std::string::npos || slash < dash) return false;
    auto num = [](const std::string& t, uint64_t& o) {
        if (t.empty() || t.size() > 18) return false;
        o = 0; for (unsigned char c : t) { if (c < '0' || c > '9') return false; o = o * 10 + (c - '0'); }
        return true;
    };
    std::string a = s.substr(0, dash), b = s.substr(dash + 1, slash - dash - 1), t = s.substr(slash + 1);
    if (!num(a, first) || !num(b, last) || first > last) return false;
    if (t == "*") total = 0;
    else { if (!num(t, total) || last >= total) return false; }
    return true;
}

/// First byte of a "Range: bytes=N-" or "bytes=N-M" request header; false when there is none or it is another shape.
inline bool RequestedRangeStart(const std::vector<Header>& headers, uint64_t& start, bool& any) {
    any = false;
    for (const Header& h : headers) {
        if (!detail::IEq(h.name, "range")) continue;
        any = true;
        std::string v = detail::Trim(h.value);
        if (!detail::IPrefix(v, "bytes=")) return false;
        v = v.substr(6);
        size_t dash = v.find('-');
        if (dash == std::string::npos || dash == 0 || dash > 18 || v.find(',') != std::string::npos) return false;
        start = 0; for (size_t i = 0; i < dash; ++i) { if (v[i] < '0' || v[i] > '9') return false; start = start * 10 + uint64_t(v[i] - '0'); }
        return true;
    }
    return false;
}

inline bool SensitiveHeader(const std::string& name) {
    std::string n = detail::Lower(name);
    for (const char* k : {"auth", "key", "token", "secret", "cookie", "password", "session"})
        if (n.find(k) != std::string::npos) return true;
    return false;
}

/// True when the connection that carried `head` (its body already read) can serve another request: HTTP/1.1, no
/// `Connection: close`, a body with a clear end (length, chunked or none: not "until close"), and no stray bytes behind it.
inline bool ConnectionReusable(const Response& head, const std::string& method, const Limits& lim, const Reader& rd) {
    if (head.minor != 1 || rd.Avail() != 0 || rd.eof) return false;
    for (const Header& h : head.headers) {
        if (!detail::IEq(h.name, "connection")) continue;
        std::string v = detail::Lower(h.value);
        if (v.find("close") != std::string::npos) return false;
    }
    Framing f; uint64_t len = 0; Err e = Err::None;
    if (!DetermineFraming(head, method, lim, f, len, e)) return false;
    return f != Framing::UntilClose;
}

inline FetchResult Fetch(Connector& conn, const FetchRequest& req, const Limits& lim,
                         int totalMs, const FetchHooks& hooks = FetchHooks()) {
    FetchResult res;
    auto bad = [&](Err e, const std::string& m) { res.ok = false; res.err = e; res.message = m; return res; };
    Url url;
    std::string why;
    if (!ParseUrl(req.url, url, &why)) return bad(Err::BadUrl, why);
    std::vector<Header> headers;
    for (const std::string& l : req.headerLines) {
        Header h;
        if (!ParseHeaderLine(l, h)) return bad(Err::BadHeader, "malformed header line");
        headers.push_back(h);
    }
    std::string method = req.method, body = req.body;
    const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(totalMs > 0 ? totalMs : 1 << 30);
    for (int hop = 0;; ++hop) {
        if (Clock::now() >= deadline) return bad(Err::Timeout, "total time limit reached");
        Err e = Err::None; std::string msg;
        std::vector<Header> sendHeaders = headers;
        if (req.jar) {   // cookies for this hop (the jar only holds what a response to this call set)
            const std::string ck = req.jar->HeaderFor(url);
            if (!ck.empty()) {
                bool merged = false;
                for (Header& h : sendHeaders) if (detail::IEq(h.name, "cookie")) { h.value += "; " + ck; merged = true; break; }
                if (!merged) sendHeaders.push_back(Header{"Cookie", ck});
            }
        }
        std::string wire;
        if (!BuildRequest(method, url, sendHeaders, body, conn.AbsoluteForm(url), wire, e, &msg, req.acceptGzip, req.keepAlive)) return bad(e, msg);
        const bool idempotent = method == "GET" || method == "HEAD" || method == "OPTIONS" || method == "PUT" || method == "DELETE";
        std::unique_ptr<Stream> st;
        std::unique_ptr<Reader> rdp;
        Response head;
        bool usedIdle = false;
        for (int attempt = 0;; ++attempt) {
            usedIdle = false;
            if (attempt == 0 && req.keepAlive && idempotent) { st = conn.TakeIdle(url, deadline); usedIdle = st != nullptr; }
            if (!st) {
                st = conn.Open(url, deadline, e, msg);
                if (!st) return bad(e == Err::None ? Err::Connect : e, msg);
            }
            bool stale = false;
            if (!st->Write(wire.data(), wire.size(), e)) {
                if (usedIdle && (e == Err::Io || e == Err::None)) stale = true;
                else return bad(e == Err::None ? Err::Io : e, "write failed");
            } else {
                rdp = std::make_unique<Reader>(*st);
                if (!ReadHead(*rdp, lim, head, e, &msg)) {
                    // Closed or reset before the first byte of the answer on a connection we had kept: it went stale.
                    if (usedIdle && rdp->buf.empty() && (e == Err::BadResponse || e == Err::Io)) stale = true;
                    else return bad(e, msg);
                }
            }
            if (!stale) break;
            rdp.reset(); st.reset(); ++res.staleRetries;   // the next attempt opens a fresh connection
        }
        if (usedIdle) ++res.reusedConnections;
        Reader& rd = *rdp;
        if (req.jar) for (const Header& h : head.headers) if (detail::IEq(h.name, "set-cookie")) req.jar->Store(url, h.value);
        const std::string* loc = head.Get("location");
        const int s = head.status;
        const bool redirect = (s == 301 || s == 302 || s == 303 || s == 307 || s == 308) && loc;
        if (redirect) {
            if (hop >= lim.maxRedirects) return bad(Err::TooManyRedirects, "too many redirects");
            Url next;
            if (!ResolveLocation(url, *loc, next, &why)) return bad(Err::BadUrl, why);
            if (url.Https() && !next.Https()) return bad(Err::DowngradeRefused, "refusing https to http redirect");
            // The redirect's own body is not wanted; a broken one does not matter.
            Limits small = lim; small.maxBodyBytes = std::min<uint64_t>(lim.maxBodyBytes, 1 << 20);
            uint64_t tot = 0; Err e2 = Err::None;
            const bool bodyRead = ReadBody(rd, head, method, small, DataFn(), tot, e2, nullptr);
            if (bodyRead && req.keepAlive && ConnectionReusable(head, method, small, rd)) conn.GiveIdle(url, std::move(st));
            else st.reset();
            if (!(url.SameOrigin(next))) {
                std::vector<Header> keep;
                for (const Header& h : headers) if (!SensitiveHeader(h.name)) keep.push_back(h);
                headers.swap(keep);
            }
            if (s == 303 || ((s == 301 || s == 302) && method != "GET" && method != "HEAD")) {
                if (method != "HEAD") method = "GET";
                body.clear();
                std::vector<Header> keep;
                for (const Header& h : headers) if (!detail::IPrefix(h.name, "content-")) keep.push_back(h);
                headers.swap(keep);
            }
            url = next;
            ++res.redirects;
            continue;
        }
        // A Range request: a 206 must be the part we asked for (never a different slice, never unasked).
        uint64_t wantStart = 0; bool haveRange = false;
        const bool rangeOk = RequestedRangeStart(headers, wantStart, haveRange);
        if (head.status == 206) {
            uint64_t a = 0, b = 0, t = 0;
            const std::string* cr = head.Get("content-range");
            if (!haveRange) return bad(Err::BadResponse, "206 answer to a request without Range");
            if (!cr || !ParseContentRange(*cr, a, b, t)) return bad(Err::BadResponse, "206 without a valid Content-Range");
            if (rangeOk && a != wantStart) return bad(Err::BadResponse, "Content-Range does not start where asked");
            if (const std::string* cl = head.Get("content-length")) {
                uint64_t n = 0; bool digits = !cl->empty() && cl->size() < 19;
                for (unsigned char c : *cl) { if (c < '0' || c > '9') digits = false; else n = n * 10 + (c - '0'); }
                if (digits && n != b - a + 1) return bad(Err::BadResponse, "Content-Length does not match Content-Range");
            }
            res.partial = true; res.rangeStart = a; res.rangeTotal = t;
        }
        if (hooks.onHead && !hooks.onHead(head)) return bad(Err::Aborted, "aborted by the caller");
        uint64_t total = 0;
        DataFn sink = hooks.onData;
        if (!sink) sink = [&](const char* p, size_t n) { head.body.append(p, n); return true; };
        Limits bodyLim = lim;
        if (req.acceptGzip) bodyLim.allowGzip = true;
        uint64_t progressDone = 0;   // outlives the sink below
        if (hooks.onProgress) {
            uint64_t expect = 0;
            if (const std::string* cl = head.Get("content-length")) {
                if (!cl->empty() && cl->size() < 19 && !head.Get("content-encoding") && !head.Get("transfer-encoding")) {
                    expect = 0;
                    for (unsigned char c : *cl) { if (c < '0' || c > '9') { expect = 0; break; } expect = expect * 10 + (c - '0'); }
                }
            }
            DataFn inner = sink;
            sink = [&, inner, expect](const char* p, size_t n) {
                if (!inner(p, n)) return false;
                progressDone += n;
                return hooks.onProgress(progressDone, expect);
            };
        }
        if (!ReadBody(rd, head, method, bodyLim, sink, total, e, &msg)) return bad(e, msg);
        head.bodyBytes = total;
        if (req.keepAlive && ConnectionReusable(head, method, bodyLim, rd)) conn.GiveIdle(url, std::move(st));
        res.ok = true; res.err = Err::None;
        res.response = std::move(head);
        res.finalUrl = url;
        return res;
    }
}

} // namespace okay::ownhttp
