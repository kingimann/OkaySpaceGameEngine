// Rules for the launcher's reading of network input, kept free of SDL / ImGui / sockets so they can be tested
// (tests/test_launcher_net_safety.cpp). Everything here treats a server response or a chat link as hostile:
//   * Content-Length is parsed with saturation, never into an int that can overflow;
//   * a download has a hard size cap, checked against the advertised size and again against the bytes on disk;
//   * a link preview is only fetched from a host that is a public address, never loopback, LAN or link-local;
//   * the GIF search's "dims" array is read inside its own bounds.
#pragma once
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace okaynet {

// ---- Content-Length ---------------------------------------------------------
/// Largest size the launcher will ever report for a file (1 PiB): digits beyond this saturate instead of overflowing.
constexpr long long kMaxReportedSize = 1LL << 50;

/// The number that follows the first "content-length:" in `line` (any case), saturated at kMaxReportedSize.
/// False when the line has none.
inline bool ParseContentLength(const std::string& line, long long& out) {
    std::string low = line;
    for (char& c : low) c = (char)std::tolower((unsigned char)c);
    const std::size_t p = low.find("content-length:");
    if (p == std::string::npos) return false;
    long long v = 0; bool any = false;
    for (std::size_t i = p + 15; i < line.size(); ++i) {
        const char d = line[i];
        if (d >= '0' && d <= '9') {
            any = true;
            v = v > (kMaxReportedSize - (d - '0')) / 10 ? kMaxReportedSize : v * 10 + (d - '0');
        } else if (any) break;
        else if (d != ' ' && d != '\t') break;
    }
    if (!any) return false;
    out = v;
    return true;
}

// ---- Download cap -----------------------------------------------------------
/// No marketplace or chat download may exceed this many bytes (2 GiB).
constexpr long long kMaxDownloadBytes = 2LL << 30;

/// True when a download advertising `total` bytes (0 = unknown) may start.
inline bool DownloadAllowed(long long total) { return total <= kMaxDownloadBytes; }
/// True when `received` bytes on disk mean a download must be stopped (covers servers that never sent a length).
inline bool DownloadOverCap(long long received) { return received > kMaxDownloadBytes; }

// ---- GIF search: the "dims" array -------------------------------------------
/// Reads `"dims": [w, h]` after offset `from` in one result object. Leaves w/h alone when the key, the '[',
/// either number (up to 5 digits), the ',' or the closing ']' is missing, so a truncated or hostile response cannot read past the string.
inline void ParseGifDims(const std::string& obj, std::size_t from, int& w, int& h) {
    const std::size_t d = obj.find("\"dims\"", from);
    if (d == std::string::npos) return;
    std::size_t i = obj.find('[', d);
    if (i == std::string::npos) return;
    auto num = [&](int& v) {
        while (i < obj.size() && (obj[i] == ' ' || obj[i] == '\t')) ++i;
        int digits = 0; long n = 0;
        while (i < obj.size() && obj[i] >= '0' && obj[i] <= '9' && digits < 6) { n = n * 10 + (obj[i] - '0'); ++i; ++digits; }
        if (digits == 0 || digits > 5) return false;
        v = (int)n;
        return true;
    };
    ++i;
    int a = 0, b = 0;
    if (!num(a)) return;
    while (i < obj.size() && (obj[i] == ' ' || obj[i] == '\t')) ++i;
    if (i >= obj.size() || obj[i] != ',') return;
    ++i;
    if (!num(b)) return;
    while (i < obj.size() && (obj[i] == ' ' || obj[i] == '\t')) ++i;
    if (i >= obj.size() || obj[i] != ']') return;
    w = a; h = b;
}

// ---- Link previews: public hosts only ---------------------------------------
/// The host of an http(s) URL, lower-cased, without user info, port or IPv6 brackets. False when there is none.
inline bool UrlHost(const std::string& url, std::string& host) {
    std::size_t s = url.find("://");
    if (s == std::string::npos) return false;
    s += 3;
    std::size_t e = url.find_first_of("/?#", s);
    std::string auth = url.substr(s, e == std::string::npos ? std::string::npos : e - s);
    const std::size_t at = auth.rfind('@');
    if (at != std::string::npos) auth.erase(0, at + 1);
    if (!auth.empty() && auth[0] == '[') {
        const std::size_t r = auth.find(']');
        if (r == std::string::npos) return false;
        host = auth.substr(1, r - 1);
    } else {
        const std::size_t c = auth.find(':');
        host = auth.substr(0, c);
    }
    for (char& c : host) c = (char)std::tolower((unsigned char)c);
    while (!host.empty() && host.back() == '.') host.pop_back();
    return !host.empty();
}

/// 4 bytes of a strict dotted quad ("1.2.3.4": no leading zeros, no hex, no short forms). False for anything else.
inline bool ParseIPv4(const std::string& s, std::uint8_t out[4]) {
    std::size_t i = 0;
    for (int part = 0; part < 4; ++part) {
        if (i >= s.size() || !std::isdigit((unsigned char)s[i])) return false;
        std::size_t start = i; int v = 0;
        while (i < s.size() && std::isdigit((unsigned char)s[i])) { v = v * 10 + (s[i] - '0'); if (v > 255) return false; ++i; }
        if (i - start > 1 && s[start] == '0') return false;
        out[part] = (std::uint8_t)v;
        if (part < 3) { if (i >= s.size() || s[i] != '.') return false; ++i; }
    }
    return i == s.size();
}

/// 16 bytes of an IPv6 text address (with "::" and an optional trailing dotted quad). False for anything else.
inline bool ParseIPv6(const std::string& s, std::uint8_t out[16]) {
    if (s.find(':') == std::string::npos) return false;
    std::string t = s;
    const std::size_t pct = t.find('%');        // zone id ("fe80::1%eth0")
    if (pct != std::string::npos) t.erase(pct);
    std::vector<std::uint16_t> head, tail;
    std::vector<std::uint16_t>* cur = &head;
    bool gap = false;
    std::size_t i = 0;
    if (t.compare(0, 2, "::") == 0) { gap = true; cur = &tail; i = 2; }
    else if (!t.empty() && t[0] == ':') return false;
    while (i < t.size()) {
        std::size_t j = i; int v = 0, n = 0;
        while (j < t.size() && std::isxdigit((unsigned char)t[j])) {
            const char c = t[j];
            v = v * 16 + (std::isdigit((unsigned char)c) ? c - '0' : std::tolower((unsigned char)c) - 'a' + 10);
            ++j; if (++n > 4) return false;
        }
        if (j < t.size() && t[j] == '.') {       // trailing dotted quad
            std::uint8_t q[4];
            if (!ParseIPv4(t.substr(i), q)) return false;
            cur->push_back((std::uint16_t)(q[0] << 8 | q[1])); cur->push_back((std::uint16_t)(q[2] << 8 | q[3]));
            i = t.size();
            break;
        }
        if (n == 0) return false;
        cur->push_back((std::uint16_t)v);
        i = j;
        if (i >= t.size()) break;
        if (t[i] != ':') return false;
        ++i;
        if (i < t.size() && t[i] == ':') { if (gap) return false; gap = true; cur = &tail; ++i; }
        else if (i >= t.size()) return false;
    }
    const std::size_t total = head.size() + tail.size();
    if (gap ? total > 7 : total != 8) return false;
    std::uint16_t g[8] = {};
    for (std::size_t k = 0; k < head.size(); ++k) g[k] = head[k];
    for (std::size_t k = 0; k < tail.size(); ++k) g[8 - tail.size() + k] = tail[k];
    for (int k = 0; k < 8; ++k) { out[k * 2] = (std::uint8_t)(g[k] >> 8); out[k * 2 + 1] = (std::uint8_t)g[k]; }
    return true;
}

/// Loopback, private (RFC 1918), link-local, carrier-grade NAT, unspecified, multicast and reserved IPv4.
inline bool IsNonPublicIPv4(const std::uint8_t a[4]) {
    if (a[0] == 0 || a[0] == 10 || a[0] == 127) return true;
    if (a[0] == 100 && a[1] >= 64 && a[1] <= 127) return true;
    if (a[0] == 169 && a[1] == 254) return true;
    if (a[0] == 172 && a[1] >= 16 && a[1] <= 31) return true;
    if (a[0] == 192 && a[1] == 168) return true;
    if (a[0] == 192 && a[1] == 0 && a[2] == 0) return true;
    if (a[0] >= 224) return true;
    return false;
}

/// Loopback, unspecified, unique-local, link-local, multicast, and IPv6 forms that embed a non-public IPv4.
inline bool IsNonPublicIPv6(const std::uint8_t a[16]) {
    bool zeroTo10 = true;
    for (int i = 0; i < 10; ++i) if (a[i] != 0) zeroTo10 = false;
    if (zeroTo10 && a[10] == 0 && a[11] == 0 && a[12] == 0 && a[13] == 0 && a[14] == 0 && a[15] <= 1) return true;   // :: and ::1
    if (zeroTo10 && ((a[10] == 0xff && a[11] == 0xff) || (a[10] == 0 && a[11] == 0))) return IsNonPublicIPv4(a + 12);   // ::ffff:v4, ::v4
    if (a[0] == 0x00 && a[1] == 0x64 && a[2] == 0xff && a[3] == 0x9b) return IsNonPublicIPv4(a + 12);                    // 64:ff9b::v4
    if ((a[0] & 0xfe) == 0xfc) return true;                      // fc00::/7
    if (a[0] == 0xfe && (a[1] & 0xc0) == 0x80) return true;      // fe80::/10
    if (a[0] == 0xff) return true;                               // ff00::/8
    return false;
}

/// True when the text is an address literal that must not be fetched. A host whose last label is
/// numeric ("2130706433", "0x7f.1", "0177.0.0.1", "127.1") is an address spelling curl would accept but
/// that is not a strict dotted quad; no real top-level domain looks like that, so it counts as non-public.
inline bool IsNonPublicLiteral(const std::string& host) {
    std::uint8_t b[16];
    if (ParseIPv4(host, b)) return IsNonPublicIPv4(b);
    if (ParseIPv6(host, b)) return IsNonPublicIPv6(b);
    const std::size_t dot = host.rfind('.');
    const std::string last = dot == std::string::npos ? host : host.substr(dot + 1);
    if (last.empty()) return false;
    if (last.size() > 2 && last[0] == '0' && (last[1] == 'x' || last[1] == 'X')) return true;
    for (char c : last) if (!std::isdigit((unsigned char)c)) return false;
    return true;
}

/// Looks `host` up and appends each address as text. False when it does not resolve.
using Resolver = std::function<bool(const std::string& host, std::vector<std::string>& addrs)>;

/// True only for a host that is a name or address on the public internet. Rejects "localhost" and the
/// local-only suffixes, single-label names (LAN hosts), address literals in non-public ranges, and any name
/// that resolves to even one non-public address (or does not resolve at all).
inline bool IsPublicHost(const std::string& host, const Resolver& resolve) {
    if (host.empty() || host.size() > 253) return false;
    std::uint8_t b[16];
    if (ParseIPv4(host, b)) return !IsNonPublicIPv4(b);
    if (ParseIPv6(host, b)) return !IsNonPublicIPv6(b);
    if (IsNonPublicLiteral(host)) return false;
    if (host.find('.') == std::string::npos) return false;
    for (const char* sfx : {".localhost", ".local", ".internal", ".lan", ".home", ".corp", ".intranet", ".home.arpa"}) {
        const std::string x = sfx;
        if (host.size() >= x.size() && host.compare(host.size() - x.size(), x.size(), x) == 0) return false;
    }
    if (host == "localhost") return false;
    std::vector<std::string> addrs;
    if (!resolve || !resolve(host, addrs) || addrs.empty()) return false;
    for (const std::string& a : addrs) {
        std::uint8_t r[16];
        if (ParseIPv4(a, r)) { if (IsNonPublicIPv4(r)) return false; }
        else if (ParseIPv6(a, r)) { if (IsNonPublicIPv6(r)) return false; }
        else return false;
    }
    return true;
}

/// True when the URL's host passes IsPublicHost.
inline bool IsPublicUrl(const std::string& url, const Resolver& resolve) {
    std::string host;
    return UrlHost(url, host) && IsPublicHost(host, resolve);
}

/// The absolute http(s) URL a redirect `location` header value points at, given the URL it came from.
/// Empty when it is not one the launcher will follow (other schemes, relative paths without a leading '/').
inline std::string RedirectTarget(const std::string& base, std::string location) {
    while (!location.empty() && (location.back() == '\r' || location.back() == '\n' || location.back() == ' ')) location.pop_back();
    while (!location.empty() && (location.front() == ' ' || location.front() == '\t')) location.erase(0, 1);
    if (location.compare(0, 7, "http://") == 0 || location.compare(0, 8, "https://") == 0) return location;
    const std::size_t scheme = base.find("://");
    if (scheme == std::string::npos) return {};
    if (location.compare(0, 2, "//") == 0) return base.substr(0, scheme + 1) + location;
    if (!location.empty() && location[0] == '/') {
        const std::size_t e = base.find_first_of("/?#", scheme + 3);
        return base.substr(0, e == std::string::npos ? std::string::npos : e) + location;
    }
    return {};
}

}  // namespace okaynet
