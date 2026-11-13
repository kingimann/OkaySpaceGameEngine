// NetGuard — small pure helpers shared by the launcher's network paths and
// unit-tested directly (tests/test_net_guard.cpp). Header-only so both the
// launcher and the test target can include it without linking anything.
//
// Covers the confirmed audit findings around downloads and link previews:
//   - Content-Length parsing that cannot overflow or wrap (used by RemoteSize /
//     HeadSize),
//   - bounded Tenor "dims" parsing (no out-of-bounds pointers into a response),
//   - URL host extraction + a public-routability check so a chat link preview
//     never fetches a private / loopback / link-local address (SSRF).
#pragma once

#include <cctype>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <string>

#if defined(_WIN32)
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#endif

namespace okay::netguard {

// Sanity cap for a Content-Length value: 1 TiB. Anything above that is a
// malformed/garbage header, not a real download.
inline constexpr long long kMaxSaneContentLength = 1ll << 40;

// Parse the value of one "Content-Length:" header line (any position in the
// line, case-insensitive). Returns 0 when it is absent, non-numeric, negative
// or implausibly large — never a wrapped or truncated value.
inline long long ParseContentLengthLine(const std::string& line) {
    std::string low = line;
    for (char& c : low) c = (char)std::tolower((unsigned char)c);
    std::size_t s = 0;                        // optional leading whitespace, then the header name at line start
    while (s < low.size() && (low[s] == ' ' || low[s] == '\t')) ++s;
    if (low.compare(s, 15, "content-length:") != 0) return 0;   // "X-Content-Length:" is a different header
    std::size_t b = s + 15;
    while (b < line.size() && (line[b] == ' ' || line[b] == '\t')) ++b;
    std::size_t e = b;
    while (e < line.size() && line[e] >= '0' && line[e] <= '9') ++e;
    if (e == b || e - b > 15) return 0;   // no digits, or too wide to be sane
    try {
        const long long v = std::stoll(line.substr(b, e - b));
        if (v <= 0 || v > kMaxSaneContentLength) return 0;
        return v;
    } catch (...) {
        return 0;
    }
}

// Keep the LAST Content-Length in a dumped header file (so a redirect chain
// reports the final resource's size, matching the previous scan behaviour).
inline long long ParseLastContentLength(const std::string& headers) {
    long long sz = 0;
    std::size_t at = 0;
    while (at <= headers.size()) {
        const std::size_t nl = headers.find('\n', at);
        const std::string line = headers.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
        const long long v = ParseContentLengthLine(line);
        if (v > 0) sz = v;
        if (nl == std::string::npos) break;
        at = nl + 1;
    }
    return sz;
}

// Parse the "[w,h]" dims object Tenor puts inside a result. `from` is the
// offset to start searching from (e.g. right after the enclosing "tinygif").
// Bounds-checked: no pointer past the string is ever formed, and the parse
// window is capped so a corrupt response cannot make sscanf run wild.
inline bool ParseGifDims(const std::string& obj, std::size_t from, int& w, int& h) {
    w = 0; h = 0;
    if (from >= obj.size()) return false;
    const std::size_t d = obj.find("\"dims\"", from);
    if (d == std::string::npos) return false;
    const std::size_t b = obj.find('[', d);
    if (b == std::string::npos || b - d > 32) return false;
    const std::size_t e = obj.find(']', b);
    if (e == std::string::npos || e - b > 64) return false;   // not a "[n,n]" window
    return std::sscanf(obj.substr(b + 1, e - b - 1).c_str(), "%d , %d", &w, &h) == 2 && w > 0 && h > 0;
}

// The host a URL would connect to, with scheme, userinfo (user@host tricks),
// IPv6 brackets and :port stripped — what a resolver actually needs to see.
inline std::string UrlHostForResolve(const std::string& url) {
    std::string u = url;
    const std::size_t scheme = u.find("://");
    if (scheme != std::string::npos) u = u.substr(scheme + 3);
    const std::size_t path = u.find_first_of("/?#");
    if (path != std::string::npos) u = u.substr(0, path);
    const std::size_t at = u.rfind('@');                    // userinfo: ignore the part before '@'
    if (at != std::string::npos) u = u.substr(at + 1);
    if (!u.empty() && u.front() == '[') {                   // [v6 literal] or [v6 literal]:port
        const std::size_t close = u.find(']');
        return close == std::string::npos ? u.substr(1) : u.substr(1, close - 1);
    }
    const std::size_t colon = u.find(':');
    if (colon != std::string::npos) u = u.substr(0, colon);
    return u;
}

// RFC 5736/1918/3927/6598-style ranges: addresses a remote site must never be
// fetched from on the user's behalf. Returns the octets as a 32-bit host value.
inline bool AddressIsPrivateV4(unsigned long a) {
    const unsigned o1 = (a >> 24) & 0xffu, o2 = (a >> 16) & 0xffu;
    if (o1 == 0 || o1 == 10 || o1 == 127) return true;                       // 0/8 this-network, 10/8, 127/8 loopback
    if (o1 == 100 && (o2 & 0xC0) == 0x40) return true;                       // 100.64/10 CGNAT
    if (o1 == 169 && o2 == 254) return true;                                 // 169.254/16 link-local
    if (o1 == 172 && (o2 & 0xF0) == 0x10) return true;                       // 172.16/12
    if (o1 == 192 && o2 == 168) return true;                                 // 192.168/16
    return o1 >= 224;                                                        // 224/4 multicast + 240/4 reserved
}

// IPv6 equivalents: unspecified/loopback, v4-mapped (and v4-compatible) embeds,
// fe80::/10 link-local, fc00::/7 unique-local, ff00::/8 multicast and the
// 2001:db8::/32 documentation range. Returns true when the address must not be
// fetched.
inline bool AddressIsPrivateV6(const in6_addr& a) {
    const unsigned char* b = a.s6_addr;
    bool allZero = true;
    for (int i = 0; i < 16; ++i) if (b[i]) { allZero = false; break; }
    if (allZero) return true;                                                // :: unspecified
    bool onlyLast = true;                                                    // exactly b[15] != 0
    for (int i = 0; i < 15; ++i) if (b[i]) { onlyLast = false; break; }
    if (onlyLast && b[15] == 1) return true;                                 // ::1 loopback
    // v4-mapped ::ffff:0:0/96 and v4-compatible ::x.y.z.w (top 12 bytes zero):
    // the embedded IPv4 address decides.
    if (b[0] == 0 && b[1] == 0 && b[2] == 0 && b[3] == 0 && b[4] == 0 && b[5] == 0 &&
        b[6] == 0 && b[7] == 0 && b[8] == 0 && b[9] == 0 &&
        ((b[10] == 0xff && b[11] == 0xff) || (b[10] == 0 && b[11] == 0))) {
        const unsigned long v4 = ((unsigned long)b[12] << 24) | ((unsigned long)b[13] << 16) |
                                 ((unsigned long)b[14] << 8) | (unsigned long)b[15];
        return AddressIsPrivateV4(v4);
    }
    if (b[0] == 0xfe && (b[1] & 0xC0) == 0x80) return true;                  // fe80::/10 link-local
    if (b[0] == 0xfc || b[0] == 0xfd) return true;                           // fc00::/7 unique-local
    if (b[0] == 0xff) return true;                                           // ff00::/8 multicast
    if (b[0] == 0x20 && b[1] == 0x01 && b[2] == 0x0d && b[3] == 0xb8) return true;   // 2001:db8::/32
    return false;
}

// True only when every address `host` resolves to is publicly routable. A
// literal IP is parsed directly (no network traffic); anything else goes
// through getaddrinfo and every returned address must pass. Resolution failure
// counts as not routable (fail closed). On Windows without WSAStartup the
// call fails and returns false — previews are skipped, never mis-fetched.
inline bool HostIsPubliclyRoutable(const std::string& host) {
    if (host.empty() || host.size() > 255) return false;
    in_addr a4{};
    if (inet_pton(AF_INET, host.c_str(), &a4) == 1) return !AddressIsPrivateV4(ntohl(a4.s_addr));
    in6_addr a6{};
    if (inet_pton(AF_INET6, host.c_str(), &a6) == 1) return !AddressIsPrivateV6(a6);
    addrinfo hints{}; hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) return false;
    bool allPublic = true;
    for (addrinfo* ai = res; ai; ai = ai->ai_next) {
        if (ai->ai_family == AF_INET && ai->ai_addr) {
            if (AddressIsPrivateV4(ntohl(((const sockaddr_in*)ai->ai_addr)->sin_addr.s_addr))) allPublic = false;
        } else if (ai->ai_family == AF_INET6 && ai->ai_addr) {
            if (AddressIsPrivateV6(((const sockaddr_in6*)ai->ai_addr)->sin6_addr)) allPublic = false;
        } else {
            allPublic = false;
        }
    }
    freeaddrinfo(res);
    return allPublic;
}

}   // namespace okay::netguard
