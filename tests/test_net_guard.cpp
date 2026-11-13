// The launcher's network-path guards (launcher/NetGuard.hpp): Content-Length
// parsing that cannot overflow, bounds-checked Tenor "dims" parsing, and the
// SSRF host guard for chat link previews. Header-only: no engine link, no
// network (a literal IP is parsed without DNS; hostnames resolve only through
// the system resolver, and every expected value below is a literal IP).
#include "test_framework.hpp"
#include "../launcher/NetGuard.hpp"

using namespace okay::netguard;

namespace {

void ContentLengthTests() {
    // Ordinary, redirect-last and malformed headers.
    CHECK(ParseLastContentLength("HTTP/1.1 200 OK\r\nContent-Length: 1234\r\n\r\n") == 1234);
    CHECK(ParseLastContentLength("HTTP/1.1 301\r\nContent-Length: 100\r\n\r\nHTTP/1.1 200 OK\r\nContent-Length: 4321\r\n\r\n") == 4321);
    CHECK(ParseLastContentLength("content-length: 77\r\n") == 77);                 // case-insensitive
    CHECK(ParseLastContentLength("Content-Length:  42 \r\n") == 42);               // padding
    CHECK(ParseLastContentLength("") == 0);
    CHECK(ParseLastContentLength("HTTP/1.1 200 OK\r\n\r\n") == 0);                 // absent
    CHECK(ParseLastContentLength("Content-Length: abc\r\n") == 0);                 // non-numeric
    CHECK(ParseLastContentLength("Content-Length: -5\r\n") == 0);                  // negative
    CHECK(ParseLastContentLength("Content-Length: \r\n") == 0);
    CHECK(ParseLastContentLength("X-Content-Length: 10\r\n") == 0);                // different header name
    // Overflow and absurd values: no wrap, no truncation — garbage is rejected.
    CHECK(ParseLastContentLength("Content-Length: 99999999999999999999999\r\n") == 0);
    CHECK(ParseLastContentLength("Content-Length: 9999999999999999999\r\n") == 0); // > INT64_MAX
    CHECK(ParseLastContentLength("Content-Length: 115292150460684697600\r\n") == 0); // 2^67: would wrap to 0 in unchecked arithmetic
    const long long huge = ParseLastContentLength("Content-Length: 1099511627777\r\n");   // 2^40 + 1: past the sanity cap
    CHECK(huge == 0);
    CHECK(ParseLastContentLength("Content-Length: 1099511627776\r\n") == 1099511627776ll); // exactly 1 TiB passes
    // Long lines and NUL bytes must not read past the buffer.
    std::string withNul = "Content-Length: 12";
    withNul.push_back('\0');
    withNul += "34\r\n";
    CHECK(ParseLastContentLength(withNul) == 12);
    std::string longLine = "Content-Length: ";
    longLine += std::string(2000, '9');
    CHECK(ParseLastContentLength(longLine) == 0);                                  // too wide to be sane
    // A truncated line (no trailing newline) still parses.
    CHECK(ParseLastContentLength("Content-Length: 55") == 55);
}

void GifDimsTests() {
    int w = -1, h = -1;
    // A well-formed dims array right after the "tinygif" marker.
    const std::string ok = "{\"tinygif\":{\"url\":\"u\",\"dims\":[320,240]}}";
    CHECK(ParseGifDims(ok, 0, w, h) && w == 320 && h == 240);
    // `from` skips earlier occurrences.
    CHECK(ParseGifDims(ok, 5, w, h) && w == 320 && h == 240);
    // A truncated response: the window opened but no closing bracket — no
    // pointer past the string is formed, nothing is written.
    w = -1; h = -1;
    CHECK(!ParseGifDims(std::string("{\"tinygif\":{\"dims\":[320,240"), 0, w, h) && w == 0 && h == 0);
    // Truncated before the array even opens.
    CHECK(!ParseGifDims(std::string("{\"tinygif\":{\"dims\":"), 0, w, h) && w == 0 && h == 0);
    // Truncated before the marker itself.
    CHECK(!ParseGifDims(std::string("{\"tinygi"), 0, w, h) && w == 0 && h == 0);
    // Empty and from-past-the-end.
    CHECK(!ParseGifDims(std::string(), 0, w, h));
    CHECK(!ParseGifDims(ok, ok.size(), w, h));
    // Garbage between the marker and the bracket, or a huge gap: rejected.
    CHECK(!ParseGifDims(std::string("{\"tinygif\"") + std::string(64, 'x') + "[1,2]", 0, w, h));
    // Not actually a pair.
    CHECK(!ParseGifDims(std::string("{\"tinygif\":{\"dims\":[320]}}"), 0, w, h));
    CHECK(!ParseGifDims(std::string("{\"tinygif\":{\"dims\":[320,\"x\"]}}"), 0, w, h));
    // Negative or zero dims rejected.
    CHECK(!ParseGifDims(std::string("{\"tinygif\":{\"dims\":[-3,240]}}"), 0, w, h));
    CHECK(!ParseGifDims(std::string("{\"tinygif\":{\"dims\":[0,0]}}"), 0, w, h));
    // A very long "dims" content (a corrupt response) is capped, not parsed wild.
    CHECK(!ParseGifDims(std::string("{\"tinygif\":{\"dims\":[") + std::string(64, '1') + ",1]}}", 0, w, h));
    // The previously-crashing shape: response ends mid-window, sscanf window
    // must stay inside the string.
    const std::string cut = "{\"results\":[" + std::string(1000, 'a') + "{\"tinygif\":{\"dims\":[";
    CHECK(!ParseGifDims(cut, cut.find("\"tinygif\""), w, h) && w == 0 && h == 0);
}

void UrlHostTests() {
    CHECK(UrlHostForResolve("https://example.com/path?q=1") == "example.com");
    CHECK(UrlHostForResolve("http://example.com") == "example.com");
    CHECK(UrlHostForResolve("example.com:8080/x") == "example.com");
    CHECK(UrlHostForResolve("http://user:pass@example.com/") == "example.com");   // userinfo trick
    CHECK(UrlHostForResolve("http://user@10.0.0.1/") == "10.0.0.1");
    CHECK(UrlHostForResolve("http://[2001:db8::1]:8080/x") == "2001:db8::1");     // bracketed v6
    CHECK(UrlHostForResolve("https://example.com#frag") == "example.com");
    CHECK(UrlHostForResolve("") .empty());
}

void PrivateRangeTests() {
    auto v4 = [](unsigned a, unsigned b, unsigned c, unsigned d) {
        return ((unsigned long)a << 24) | ((unsigned long)b << 16) | ((unsigned long)c << 8) | d;
    };
    // Public addresses pass.
    CHECK(!AddressIsPrivateV4(v4(8, 8, 8, 8)));
    CHECK(!AddressIsPrivateV4(v4(1, 2, 3, 4)));
    CHECK(!AddressIsPrivateV4(v4(172, 32, 0, 1)));      // just outside 172.16/12
    CHECK(!AddressIsPrivateV4(v4(100, 128, 0, 1)));     // just outside CGNAT (100.64/10 ends at 100.127)
    CHECK(!AddressIsPrivateV4(v4(192, 169, 0, 1)));     // not 192.168
    // Everything a preview must never fetch.
    CHECK(AddressIsPrivateV4(v4(0, 0, 0, 0)));          // this-network
    CHECK(AddressIsPrivateV4(v4(10, 0, 0, 1)));         // RFC1918
    CHECK(AddressIsPrivateV4(v4(127, 0, 0, 1)));        // loopback
    CHECK(AddressIsPrivateV4(v4(100, 64, 0, 1)));       // CGNAT
    CHECK(AddressIsPrivateV4(v4(100, 127, 255, 255)));  // CGNAT last
    CHECK(AddressIsPrivateV4(v4(169, 254, 1, 1)));      // link-local
    CHECK(AddressIsPrivateV4(v4(172, 16, 0, 1)));       // 172.16/12
    CHECK(AddressIsPrivateV4(v4(172, 31, 255, 255)));
    CHECK(AddressIsPrivateV4(v4(192, 168, 1, 1)));
    CHECK(AddressIsPrivateV4(v4(224, 0, 0, 1)));        // multicast
    CHECK(AddressIsPrivateV4(v4(255, 255, 255, 255)));  // reserved
    // IPv6.
    in6_addr a6{};
    inet_pton(AF_INET6, "2606:4700::1", &a6);
    CHECK(!AddressIsPrivateV6(a6));
    inet_pton(AF_INET6, "::", &a6);                     CHECK(AddressIsPrivateV6(a6));   // unspecified
    inet_pton(AF_INET6, "::1", &a6);                    CHECK(AddressIsPrivateV6(a6));   // loopback
    inet_pton(AF_INET6, "::ffff:10.0.0.1", &a6);        CHECK(AddressIsPrivateV6(a6));   // v4-mapped private
    inet_pton(AF_INET6, "::ffff:8.8.8.8", &a6);         CHECK(!AddressIsPrivateV6(a6));  // v4-mapped public
    inet_pton(AF_INET6, "fe80::1", &a6);                CHECK(AddressIsPrivateV6(a6));   // link-local
    inet_pton(AF_INET6, "fc00::1", &a6);                CHECK(AddressIsPrivateV6(a6));   // unique-local
    inet_pton(AF_INET6, "fd12:3456::1", &a6);           CHECK(AddressIsPrivateV6(a6));
    inet_pton(AF_INET6, "ff02::1", &a6);                CHECK(AddressIsPrivateV6(a6));   // multicast
    inet_pton(AF_INET6, "2001:db8::1", &a6);            CHECK(AddressIsPrivateV6(a6));   // documentation
    inet_pton(AF_INET6, "::ffff:169.254.9.9", &a6);     CHECK(AddressIsPrivateV6(a6));   // mapped link-local
}

// Literal-IP and empty-host checks run without DNS. Hostname resolution (the
// getaddrinfo path) is exercised with a name that always exists locally; its
// loopback/private result must make the guard fail closed.
void RoutableTests() {
    CHECK(!HostIsPubliclyRoutable(""));
    CHECK(HostIsPubliclyRoutable("8.8.8.8"));
    CHECK(HostIsPubliclyRoutable("1.1.1.1"));
    CHECK(!HostIsPubliclyRoutable("127.0.0.1"));
    CHECK(!HostIsPubliclyRoutable("10.1.2.3"));
    CHECK(!HostIsPubliclyRoutable("192.168.0.15"));
    CHECK(!HostIsPubliclyRoutable("169.254.0.4"));
    CHECK(!HostIsPubliclyRoutable("::1"));
    CHECK(!HostIsPubliclyRoutable("::ffff:127.0.0.1"));     // v4-mapped loopback via the literal parser
    CHECK(!HostIsPubliclyRoutable("localhost"));            // resolves to loopback: fail closed
    CHECK(!HostIsPubliclyRoutable("ip6-localhost"));
    CHECK(!HostIsPubliclyRoutable("definitely.invalid.hostname.example"));   // resolution failure: fail closed
}

}   // namespace

int main() {
    RUN_SUITE("launcher net guard");
    ContentLengthTests();
    GifDimsTests();
    UrlHostTests();
    PrivateRangeTests();
    RoutableTests();
    TEST_MAIN_RESULT();
}
