#include "test_framework.hpp"
#include "NetSafety.hpp"

#include <map>
#include <string>
#include <vector>

using namespace okaynet;

// The launcher treats server responses and chat links as hostile: Content-Length parsing saturates instead of
// overflowing, downloads have a hard cap, the GIF search reads its "dims" inside bounds, and a link preview is only
// fetched from public hosts (never the player's own machine or LAN).
int main() {
    RUN_SUITE("launcher_net_safety");

    // ---- Content-Length -------------------------------------------------------
    {
        long long v = -1;
        CHECK(ParseContentLength("Content-Length: 1234\r", v) && v == 1234);
        CHECK(ParseContentLength("content-length:99", v) && v == 99);
        CHECK(ParseContentLength("CONTENT-LENGTH:   7  ", v) && v == 7);
        // 12 digits used to overflow an int accumulator; 40 digits must still not overflow anything.
        CHECK(ParseContentLength("Content-Length: 999999999999", v) && v == 999999999999LL);
        CHECK(ParseContentLength("Content-Length: 99999999999999999999999999999999999999999999", v) && v == kMaxReportedSize);
        CHECK(!ParseContentLength("Content-Type: text/html", v));
        CHECK(!ParseContentLength("Content-Length: abc", v));
        CHECK(!ParseContentLength("Content-Length:", v));
    }

    // ---- Download cap -----------------------------------------------------------
    {
        CHECK(DownloadAllowed(0));                       // unknown size: allowed, the transfer itself is capped
        CHECK(DownloadAllowed(5 * 1024 * 1024));
        CHECK(DownloadAllowed(kMaxDownloadBytes));
        CHECK(!DownloadAllowed(kMaxDownloadBytes + 1));
        CHECK(!DownloadAllowed(kMaxReportedSize));
        CHECK(!DownloadOverCap(kMaxDownloadBytes));
        CHECK(DownloadOverCap(kMaxDownloadBytes + 1));
    }

    // ---- GIF "dims" -----------------------------------------------------------
    {
        int w = 0, h = 0;
        const std::string ok = "{\"tinygif\":{\"url\":\"x\",\"dims\":[220, 124],\"size\":1}}";
        ParseGifDims(ok, ok.find("\"tinygif\""), w, h);
        CHECK(w == 220 && h == 124);
        // Truncated at every point after "dims": used to hand an out-of-bounds pointer to sscanf. Must never touch w/h.
        const std::size_t at = ok.find("\"dims\"");
        for (std::size_t cut = at; cut < ok.find(']'); ++cut) {
            int a = 5, b = 6;
            ParseGifDims(ok.substr(0, cut), 0, a, b);
            CHECK(a == 5 && b == 6);
        }
        int a = 5, b = 6;
        ParseGifDims("{\"dims\"", 0, a, b);                    CHECK(a == 5 && b == 6);   // no '[' at all
        ParseGifDims("{\"dims\":[1,", 0, a, b);                CHECK(a == 5 && b == 6);
        ParseGifDims("{\"dims\":[x,2]}", 0, a, b);             CHECK(a == 5 && b == 6);
        ParseGifDims("{\"dims\":[99999999999,2]}", 0, a, b);   CHECK(a == 5 && b == 6);   // would overflow %d
        ParseGifDims("{\"dims\":[-3,2]}", 0, a, b);            CHECK(a == 5 && b == 6);
        ParseGifDims("{\"url\":\"x\"}", 0, a, b);              CHECK(a == 5 && b == 6);
    }

    // ---- Link-preview hosts ---------------------------------------------------
    {
        const std::map<std::string, std::vector<std::string>> dns = {
            {"example.com", {"93.184.216.34", "2606:2800:220:1:248:1893:25c8:1946"}},
            {"evil.example", {"93.184.216.34", "127.0.0.1"}},     // one bad answer is enough
            {"rebind.example", {"10.0.0.5"}},
            {"v6loop.example", {"::1"}},
            {"mapped.example", {"::ffff:192.168.1.1"}},
        };
        int lookups = 0;
        const Resolver res = [&](const std::string& h, std::vector<std::string>& out) {
            ++lookups;
            auto it = dns.find(h);
            if (it == dns.end()) return false;
            out = it->second; return true;
        };
        auto pub = [&](const char* url) { return IsPublicUrl(url, res); };

        CHECK(pub("https://example.com/page?x=1"));
        CHECK(pub("http://93.184.216.34/"));
        CHECK(pub("http://[2606:2800:220:1:248:1893:25c8:1946]/"));
        CHECK(pub("https://user:pw@example.com:8443/a"));

        // Loopback / LAN / link-local literals
        CHECK(!pub("http://127.0.0.1:8080/"));
        CHECK(!pub("http://127.255.0.9/"));
        CHECK(!pub("http://0.0.0.0/"));
        CHECK(!pub("http://10.1.2.3/"));
        CHECK(!pub("http://172.16.0.1/"));
        CHECK(!pub("http://172.31.255.255/"));
        CHECK(pub("http://172.32.0.1/"));
        CHECK(!pub("http://192.168.0.1/admin"));
        CHECK(!pub("http://169.254.169.254/latest/meta-data/"));
        CHECK(!pub("http://100.64.0.1/"));
        CHECK(!pub("http://224.0.0.1/"));
        CHECK(!pub("http://[::1]:9000/"));
        CHECK(!pub("http://[::]/"));
        CHECK(!pub("http://[fe80::1%25eth0]/"));
        CHECK(!pub("http://[fd00::1]/"));
        CHECK(!pub("http://[::ffff:127.0.0.1]/"));
        CHECK(!pub("http://[::ffff:7f00:1]/"));
        CHECK(!pub("http://[64:ff9b::10.0.0.1]/"));
        // Other spellings curl would accept for 127.0.0.1
        CHECK(!pub("http://2130706433/"));
        CHECK(!pub("http://0x7f.0.0.1/"));
        CHECK(!pub("http://0x7f000001/"));
        CHECK(!pub("http://0177.0.0.1/"));
        CHECK(!pub("http://127.1/"));
        // Names
        CHECK(!pub("http://localhost:3000/"));
        CHECK(!pub("http://LOCALHOST./"));
        CHECK(!pub("http://app.localhost/"));
        CHECK(!pub("http://printer.local/"));
        CHECK(!pub("http://nas/"));                       // single label = LAN host
        CHECK(!pub("http://router.home.arpa/"));
        CHECK(!pub("https://evil.example/"));             // resolves to a public AND a loopback address
        CHECK(!pub("https://rebind.example/"));
        CHECK(!pub("https://v6loop.example/"));
        CHECK(!pub("https://mapped.example/"));
        CHECK(!pub("https://nxdomain.example/"));         // does not resolve: not fetched
        CHECK(!pub("not a url"));
        CHECK(!pub("http:///path"));
        // Literals never need a lookup.
        const int before = lookups;
        CHECK(!pub("http://127.0.0.1/"));
        CHECK(pub("http://8.8.8.8/"));
        CHECK(lookups == before);

        std::string host;
        CHECK(UrlHost("https://Example.COM:8443/x", host) && host == "example.com");
        CHECK(UrlHost("http://a@b@c.org/", host) && host == "c.org");
        CHECK(UrlHost("http://[::1]:80/", host) && host == "::1");
    }

    // ---- Redirects ----------------------------------------------------------------
    {
        CHECK(RedirectTarget("https://a.com/x/y", " https://b.com/z\r") == "https://b.com/z");
        CHECK(RedirectTarget("https://a.com/x/y?q", "/other") == "https://a.com/other");
        CHECK(RedirectTarget("https://a.com/x", "//b.com/p") == "https://b.com/p");
        CHECK(RedirectTarget("https://a.com/x", "relative") == "");
        CHECK(RedirectTarget("https://a.com/x", "file:///etc/passwd") == "");
        CHECK(RedirectTarget("https://a.com/x", "gopher://127.0.0.1/") == "");
    }

    TEST_MAIN_RESULT();
}
