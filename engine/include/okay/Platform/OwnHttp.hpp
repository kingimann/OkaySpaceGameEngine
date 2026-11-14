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
// https -> http redirect are all refused. We always send `Connection: close` and
// `Accept-Encoding: identity`, so there is no keep-alive and no gzip.
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
                         Err& e, std::string* msg = nullptr, bool acceptGzip = false) {
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
    r += acceptGzip ? "Accept-Encoding: gzip, deflate\r\nConnection: close\r\n" : "Accept-Encoding: identity\r\nConnection: close\r\n";
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
    if (method == "HEAD" || r.status == 204 || r.status == 304) { f = Framing::None; e = Err::None; return true; }
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
// Fetch with redirects
// ----------------------------------------------------------------------------
struct Connector {
    virtual ~Connector() = default;
    /// Open a stream to `u` (TCP, proxy tunnel and TLS as needed).
    virtual std::unique_ptr<Stream> Open(const Url& u, Clock::time_point deadline, Err& e, std::string& msg) = 0;
    /// True when `u` is plain http reached through a proxy (request line uses the absolute form).
    virtual bool AbsoluteForm(const Url&) { return false; }
};

struct FetchRequest {
    std::string method = "GET";
    std::string url;
    std::vector<std::string> headerLines;   ///< "Name: value"
    std::string body;
    bool acceptGzip = false;                ///< ask for gzip/deflate and decode it (Limits::allowGzip is set for the call)
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
        std::unique_ptr<Stream> st = conn.Open(url, deadline, e, msg);
        if (!st) return bad(e == Err::None ? Err::Connect : e, msg);
        std::string wire;
        if (!BuildRequest(method, url, headers, body, conn.AbsoluteForm(url), wire, e, &msg, req.acceptGzip)) return bad(e, msg);
        if (!st->Write(wire.data(), wire.size(), e)) return bad(e == Err::None ? Err::Io : e, "write failed");
        Reader rd(*st);
        Response head;
        if (!ReadHead(rd, lim, head, e, &msg)) return bad(e, msg);
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
            ReadBody(rd, head, method, small, DataFn(), tot, e2, nullptr);
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
        res.ok = true; res.err = Err::None;
        res.response = std::move(head);
        res.finalUrl = url;
        return res;
    }
}

} // namespace okay::ownhttp
