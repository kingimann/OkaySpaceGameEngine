// Site check (tools/sitecheck): the causes and the ranking (SiteReport), and the reporting hooks the tool reads - the page
// engine's "not implemented: X" console line (Script.hpp MissingWebApi), the CSS reporter (Css.hpp SetCssNotUsedReporter) and
// the tab host's fetch reporter (shared/BrowserHost.hpp SetFetchReporter, against a closed port on 127.0.0.1; no real site).
#include "test_framework.hpp"
#include "SiteReport.hpp"

#include "BrowserHost.hpp"
#include "okay/Browser/Browser.hpp"
#include "okay/Core/Json.hpp"
#include "okay/Graphics/TtfFont.hpp"   // EmbeddedUIFontData: a real TrueType file to wrap in WOFF

#include <atomic>
#include <chrono>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <thread>   // the test's own little server (this test is not built for Windows)
#include <vector>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

using namespace sitecheck;
namespace br = okay::browser;

namespace {

bool HasKey(const std::vector<Cause>& cs, const std::string& key, Area* area = nullptr) {
    for (const Cause& c : cs) if (c.key == key) { if (area) *area = c.area; return true; }
    return false;
}

std::string Dump(const std::vector<Cause>& cs) {
    std::string s;
    for (const Cause& c : cs) s += std::string("  [") + AreaName(c.area) + "] " + c.key + "\n";
    return s;
}

std::vector<std::string> ConsoleOf(const std::string& script) {
    br::HtmlDocument doc = br::ParseHtml("<!DOCTYPE html><html><body><p id=a>x</p></body></html>");
    br::FormState forms;
    forms.Reset(doc);
    br::PageScripts js(doc, forms, "https://site.test/", br::PageScripts::Hooks());
    js.RunScript(script, "test.js");
    std::vector<std::string> out;
    for (const br::ConsoleLine& l : js.Console()) out.push_back(l.text);
    return out;
}

bool Has(const std::vector<std::string>& lines, const std::string& part) {
    for (const std::string& l : lines) if (l.find(part) != std::string::npos) return true;
    return false;
}

std::map<std::string, std::string> g_cssSeen;   // text -> what
void OnCss(const char* what, const std::string& text) { g_cssSeen[text] = what; }

// A port on 127.0.0.1 nothing listens on: bound, then closed (connect is refused).
int ClosedPort() {
    const int s = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t n = sizeof a;
    int port = 0;
    if (s >= 0 && ::bind(s, (sockaddr*)&a, sizeof a) == 0 && ::getsockname(s, (sockaddr*)&a, &n) == 0) port = ntohs(a.sin_port);
    if (s >= 0) ::close(s);
    return port;
}

// A small HTTP/1.1 server on 127.0.0.1 for the tab host tests: it answers each request from `answer` (path -> whole response) and
// keeps the request heads it saw. Written for this test from RFC 9112 (one request per connection, "Connection: close").
struct LocalServer {
    int fd = -1, port = 0;
    std::map<std::string, std::string> answer;
    std::vector<std::string> heads;
    std::atomic<bool> stop{false};
    std::thread th;
    bool Start() {
        fd = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        socklen_t n = sizeof a;
        if (fd < 0 || ::bind(fd, (sockaddr*)&a, sizeof a) != 0 || ::listen(fd, 16) != 0 || ::getsockname(fd, (sockaddr*)&a, &n) != 0) return false;
        port = ntohs(a.sin_port);
        th = std::thread([this] {
            while (!stop) {
                timeval tv{0, 100000};
                fd_set rd;
                FD_ZERO(&rd);
                FD_SET(fd, &rd);
                if (::select(fd + 1, &rd, nullptr, nullptr, &tv) <= 0) continue;
                const int c = ::accept(fd, nullptr, nullptr);
                if (c < 0) continue;
                std::string req;
                char buf[4096];
                while (req.find("\r\n\r\n") == std::string::npos) {
                    const ssize_t got = ::recv(c, buf, sizeof buf, 0);
                    if (got <= 0) break;
                    req.append(buf, (std::size_t)got);
                }
                heads.push_back(req.substr(0, req.find("\r\n\r\n") + 2));   // each header line ends with CRLF
                const std::size_t sp = req.find(' '), sp2 = req.find(' ', sp + 1);
                const std::string path = sp == std::string::npos || sp2 == std::string::npos ? std::string() : req.substr(sp + 1, sp2 - sp - 1);
                auto it = answer.find(path);
                const std::string out = it != answer.end() ? it->second : std::string("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                std::size_t sent = 0;
                while (sent < out.size()) {
                    const ssize_t w = ::send(c, out.data() + sent, out.size() - sent, MSG_NOSIGNAL);
                    if (w <= 0) break;
                    sent += (std::size_t)w;
                }
                ::close(c);
            }
        });
        return true;
    }
    ~LocalServer() { stop = true; if (th.joinable()) th.join(); if (fd >= 0) ::close(fd); }
};
std::string Answer(const std::string& type, const std::string& body, const std::string& extra = std::string()) {
    return "HTTP/1.1 200 OK\r\nContent-Type: " + type + "\r\nContent-Length: " + std::to_string(body.size()) + "\r\n" + extra +
           "Connection: close\r\n\r\n" + body;
}
// A WOFF 1.0 file (W3C WOFF 1.0, 4 and 5) holding the tables of `sfnt` as they are (stored, not compressed).
std::string WoffOf(const std::vector<unsigned char>& sfnt) {
    auto rd16 = [&](std::size_t at) { return (unsigned)(sfnt[at] << 8 | sfnt[at + 1]); };
    auto rd32 = [&](std::size_t at) { return (unsigned)sfnt[at] << 24 | (unsigned)sfnt[at + 1] << 16 | (unsigned)sfnt[at + 2] << 8 | sfnt[at + 3]; };
    std::string b;
    auto put32 = [&](unsigned v) { for (int s = 24; s >= 0; s -= 8) b += (char)((v >> s) & 0xFF); };
    auto put16 = [&](unsigned v) { b += (char)((v >> 8) & 0xFF); b += (char)(v & 0xFF); };
    auto set32 = [&](std::size_t at, unsigned v) { for (int k = 0; k < 4; ++k) b[at + k] = (char)((v >> (24 - 8 * k)) & 0xFF); };
    const unsigned n = rd16(4);
    put32(0x774F4646u); put32(rd32(0)); put32(0); put16(n); put16(0); put32((unsigned)sfnt.size()); put16(1); put16(0);
    for (int k = 0; k < 5; ++k) put32(0);
    const std::size_t dir = b.size();
    b.resize(b.size() + 20 * n);
    for (unsigned i = 0; i < n; ++i) {
        const std::size_t e = 12 + 16 * i;
        const unsigned off = rd32(e + 8), len = rd32(e + 12);
        while (b.size() % 4) b += '\0';
        const std::size_t at = b.size();
        b.append((const char*)sfnt.data() + off, len);
        set32(dir + 20 * i, rd32(e)); set32(dir + 20 * i + 4, (unsigned)at); set32(dir + 20 * i + 8, len); set32(dir + 20 * i + 12, len);
        set32(dir + 20 * i + 16, rd32(e + 4));
    }
    while (b.size() % 4) b += '\0';
    set32(8, (unsigned)b.size());
    return b;
}

} // namespace

int main() {
    RUN_SUITE("site check");

    // ---- the site list
    {
        const std::vector<SiteLine> l = ReadSiteList("# search\nhttps://a.test/\n\n# a comment line with words\nhttp://b.test/x\nftp://no.test/\n# news\nhttps://c.test/\n");
        CHECK(l.size() == 3);
        CHECK(l.size() == 3 && l[0].category == "search" && l[1].category == "search" && l[2].category == "news" && l[1].url == "http://b.test/x");
    }

    // ---- file signatures (PNG, JPEG, GIF, WebP, AVIF, ICO, SVG; WOFF, WOFF2, TrueType, OpenType)
    CHECK(ImageFormatOf("\x89PNG\r\n\x1a\n....", "") == "PNG");
    CHECK(ImageFormatOf(std::string("\xFF\xD8\xFF\xE0", 4), "") == "JPEG");
    CHECK(ImageFormatOf(std::string("RIFF\x10\0\0\0WEBPVP8 ", 16), "") == "WebP");
    CHECK(ImageFormatOf(std::string("\0\0\0\x1C" "ftypavif", 12), "") == "AVIF");
    CHECK(ImageFormatOf(std::string("\0\0\1\0\1\0", 6), "") == "ICO");
    CHECK(ImageFormatOf("<?xml version=\"1.0\"?><svg", "") == "SVG");
    CHECK(ImageFormatOf("  <svg xmlns=", "image/svg+xml") == "SVG");
    CHECK(ImageFormatOf("<!DOCTYPE html><html>", "text/html") == "HTML (not an image)");
    CHECK(FontFormatOf("wOF2....") == "WOFF2" && FontFormatOf("wOFF....") == "WOFF" && FontFormatOf("OTTO....") == "OpenType");
    CHECK(FontFormatOf(std::string("\0\1\0\0....", 8)) == "TrueType" && FontFormatOf("xx").empty());
    CHECK(NetReason("could not resolve host") == "DNS" && NetReason("TLS handshake failed") == "TLS");
    CHECK(NetReason("connect: connection refused") == "connect" && NetReason("read timed out") == "timeout");
    CHECK(CharsetOf("text/html; charset=Shift_JIS", "") == "shift_jis");
    CHECK(CharsetOf("text/html", "<html><head><meta charset=\"windows-1252\">") == "windows-1252");
    CHECK(CharsetOf("text/html", "<html>").empty());

    // ---- script errors: line numbers and page names folded, so the same gap groups together
    CHECK(JsErrorKey("https://x.test/a.js:12:5: TypeError: 'foo.bar' is undefined, not a function") ==
          JsErrorKey("inline script 3:1: TypeError: 'baz.qux' is undefined, not a function"));
    CHECK(JsErrorKey("a.js:1:1: SyntaxError: unexpected token '?.'").find("SyntaxError") == 0);
    CHECK(JsErrorKey("a.js:1:1: the script ran past its instruction budget (an endless loop?)") == "script stopped: ran past its instruction budget");
    CHECK(JsErrorKey("hello from the page").empty());
    CHECK(SelectorKey("a:focus-visible > b::-webkit-scrollbar") == "::-webkit-scrollbar :focus-visible");

    // ---- the engine's "not implemented: X" hook (Script.hpp MissingWebApi and the console line after the error)
    CHECK(br::MissingWebApi("x.js:1:1: ReferenceError: 'IntersectionObserver' is not defined") == "IntersectionObserver");
    CHECK(br::MissingWebApi("x.js:1:1: ReferenceError: 'myOwnLib' is not defined").empty());
    CHECK(br::MissingWebApi("x.js:1:1: ReferenceError: 'Date' is not defined") == "Date");   // seen on 16 of 61 real sites
    CHECK(br::MissingWebApi("x.js:1:1: ReferenceError: 'decodeURIComponent' is not defined") == "decodeURIComponent");   // the page's own name: not ours to build
    CHECK(br::MissingWebApi("x.js:1:1: TypeError: 'document.fonts.load' is undefined, not a function") == "document.fonts.load");
    CHECK(br::MissingWebApi("x.js:1:1: TypeError: 'window.matchMedia' is undefined, not a function") == "matchMedia");
    CHECK(br::MissingWebApi("x.js:1:1: TypeError: 'el.closest' is undefined, not a function") == "*.closest");
    CHECK(br::MissingWebApi("x.js:1:1: TypeError: 'app.start' is undefined, not a function").empty());
    {
        // (IntersectionObserver was the example until round 4 built it; customElements is still missing)
        const std::vector<std::string> c = ConsoleOf("customElements.define('x-a', class {});");
        CHECK(Has(c, "not implemented: customElements"));
        const std::vector<std::string> d = ConsoleOf("myOwnLib.start();");
        CHECK(!Has(d, "not implemented"));
        if (!Has(c, "not implemented: customElements")) for (const std::string& l : c) std::cerr << "  console: " << l << "\n";
    }

    // ---- the CSS reporter (Css.hpp): a property the cascade does not use, a selector the reader drops; null = off
    {
        br::SetCssNotUsedReporter(&OnCss);
        br::HtmlDocument doc = br::ParseHtml("<!DOCTYPE html><html><body><p id=a style=\"color: red; zz-made-up: 1\">x</p></body></html>");
        const br::StyleSheet sheet = br::ParseStyleSheet("p { color: blue } p:zz-made-up-state { color: green }");
        std::vector<const br::StyleSheet*> sheets{&sheet};
        const br::Node* p = doc.FindAnchor("a");
        CHECK(p != nullptr);
        if (p) br::ComputeStyle(*p, br::ComputedStyle(), sheets);
        CHECK(g_cssSeen.count("zz-made-up") && g_cssSeen["zz-made-up"] == "property");
        CHECK(!g_cssSeen.count("color"));
        bool selector = false;
        for (const auto& kv : g_cssSeen) if (kv.second == "selector" && kv.first.find("zz-made-up-state") != std::string::npos) selector = true;
        CHECK(selector);
        br::SetCssNotUsedReporter(nullptr);
        g_cssSeen.clear();
        br::ParseStyleSheet("p:zz-made-up-state { color: green }");
        CHECK(g_cssSeen.empty());
    }

    // ---- causes of one visit, by area
    {
        SiteResult r;
        r.site = "https://shop.test/";
        r.loaded = true;
        r.status = 200;
        r.elements = 400;
        r.drawCmds = 900;
        r.charset = "windows-1252";
        Fetch page; page.kind = "page"; page.reached = true; page.status = 200; r.fetches.push_back(page);
        Fetch img; img.kind = "image"; img.reached = true; img.status = 200; img.note = "image not decoded"; img.head = std::string("RIFF\x10\0\0\0WEBPVP8 ", 16);
        img.url = "https://shop.test/a.webp"; r.fetches.push_back(img);
        Fetch font; font.kind = "font"; font.reached = true; font.status = 200; font.note = "font not decoded: not a WOFF2 file"; font.head = "wOF2abcd";
        r.fetches.push_back(font);
        Fetch js; js.kind = "script"; js.reached = false; js.netError = "TLS handshake failed"; r.fetches.push_back(js);
        Fetch gone; gone.kind = "image"; gone.reached = true; gone.status = 404; r.fetches.push_back(gone);
        Fetch br2; br2.kind = "style"; br2.reached = true; br2.status = 200; br2.contentEncoding = "zstd"; r.fetches.push_back(br2);
        Fetch br3 = br2; br3.contentEncoding = "br"; r.fetches.push_back(br3);
        r.console = {"a.js:1:1: ReferenceError: 'IntersectionObserver' is not defined", "not implemented: IntersectionObserver",
                     "b.js:2:2: SyntaxError: unexpected token '#'", "not implemented: Object.hasOwn", "not implemented: Date"};
        r.cssProperties["container-type"] = 3;
        r.cssSelectors[".a:focus-visible"] = 1;
        const std::vector<Cause> cs = CausesOf(r);
        Area a = Area::Network;
        CHECK(HasKey(cs, "image format WebP not decoded", &a) && a == Area::Images);
        CHECK(HasKey(cs, "font format WOFF2 not used", &a) && a == Area::Fonts);
        CHECK(HasKey(cs, "script request failed: TLS", &a) && a == Area::Network);
        CHECK(HasKey(cs, "image answered HTTP 404"));
        CHECK(HasKey(cs, "Content-Encoding 'zstd' not read") && !HasKey(cs, "Content-Encoding 'br' not read"));   // br is read since round 4 (the tab host's client decodes it)
        CHECK(HasKey(cs, "not implemented: IntersectionObserver", &a) && a == Area::DomApi);
        CHECK(HasKey(cs, "not implemented: Object.hasOwn", &a) && a == Area::JavaScript);
        CHECK(HasKey(cs, "not implemented: Date", &a) && a == Area::JavaScript);
        CHECK(HasKey(cs, "CSS property not used: container-type", &a) && a == Area::Css);
        CHECK(HasKey(cs, "CSS selector not read: :focus-visible"));
        CHECK(HasKey(cs, "page charset windows-1252", &a) && a == Area::Encoding);
        bool syntax = false, refErr = false;
        for (const Cause& c : cs) {
            if (c.key.find("syntax not read: SyntaxError") == 0) syntax = true;
            if (c.key.find("ReferenceError") != std::string::npos) refErr = true;   // filed under its "not implemented" line only
        }
        CHECK(syntax && !refErr);
        if (!syntax || refErr) std::cerr << Dump(cs);

        // the JSON report is valid JSON and carries the causes
        okay::json::Value v;
        std::string why;
        CHECK(okay::json::Parse(SiteJson(r), v, &why));
        if (!why.empty()) std::cerr << "  json: " << why << "\n";

        // the ranking: by sites, three examples at most
        std::vector<SiteResult> all;
        for (int i = 0; i < 5; ++i) {
            SiteResult s;
            s.site = "https://s" + std::to_string(i) + ".test/";
            s.loaded = true; s.elements = 10; s.drawCmds = 50; s.status = 200;
            if (i < 4) s.console.push_back("not implemented: matchMedia");
            if (i < 2) s.cssProperties["zoom"] = 1;
            all.push_back(s);
        }
        const std::vector<Ranked> rk = Rank(all);
        CHECK(!rk.empty() && rk[0].key == "not implemented: matchMedia" && rk[0].sites == 4 && rk[0].examples.size() == 3);
        CHECK(rk.size() >= 2 && rk[1].key == "CSS property not used: zoom" && rk[1].sites == 2);
        CHECK(SummaryMarkdown(all, rk).find("| 1 | 4 | DOM API | not implemented: matchMedia |") != std::string::npos);
        CHECK(okay::json::Parse(SummaryJson(all, rk), v, &why));
    }

    // ---- the tab host's fetch reporter: a page whose server cannot be reached is reported with the client's own reason
    {
        const int port = ClosedPort();
        CHECK(port > 0);
        br::TabHost host(br::TabHost::TextureFns(), "OkaySpace Browser");
        host.Tab().SetAnyOrigin(true);
        std::vector<br::TabHost::FetchReport> seen;
        host.SetFetchReporter([&](const br::TabHost::FetchReport& f) { seen.push_back(f); });
        CHECK(host.Open("http://127.0.0.1:" + std::to_string(port) + "/"));
        const auto start = std::chrono::steady_clock::now();
        while (seen.empty() && std::chrono::steady_clock::now() - start < std::chrono::seconds(20)) {
            host.Pump();
            timespec ts{0, 10000000L};
            ::nanosleep(&ts, nullptr);
        }
        CHECK(seen.size() == 1);
        if (!seen.empty()) {
            CHECK(std::string(seen[0].kind) == "page" && !seen[0].reached && !seen[0].netError.empty());
            std::cerr << "  closed port: " << seen[0].netError << "\n";
            SiteResult r;
            Fetch f; f.kind = "page"; f.reached = false; f.netError = seen[0].netError; r.fetches.push_back(f);
            CHECK(HasKey(CausesOf(r), "page not reached: connect"));
            if (!HasKey(CausesOf(r), "page not reached: connect")) std::cerr << Dump(CausesOf(r));
        }
        CHECK(host.Tab().GetState() == br::BrowserTab::State::Error);
    }

    // ---- round 4, through the tab host and a local server: the page's charset from its Content-Type, a WOFF font decoded for
    // the font loader, the standard request headers (no client hints, no other browser's names), Brotli answers, navigator
    {
        std::vector<unsigned char> ttf;
        CHECK(okay::EmbeddedUIFontData(ttf));
        LocalServer srv;
        CHECK(srv.Start());
        const std::string base = "http://127.0.0.1:" + std::to_string(srv.port);
        // "caf\xE9" is windows-1252 (no <meta charset>): only the Content-Type says so
        srv.answer["/"] = Answer("text/html; charset=windows-1252",
            "<html><head><style>@font-face { font-family: Shop; src: url(/f.woff) format('woff') } p { font-family: Shop }</style>"
            "<link rel=stylesheet href=/s.css></head><body><p id=p>caf\xE9</p><img src=/i.png>"
            "<script>fetch('/api').then(r => r.text()).then(t => { document.title = navigator.userAgent + '|' + t; });</script></body></html>");
        srv.answer["/f.woff"] = Answer("font/woff", WoffOf(ttf));
        // a Brotli stream (RFC 7932): WBITS 16, one stored meta-block of "p{}" (ISUNCOMPRESSED), then an empty last meta-block
        srv.answer["/s.css"] = Answer("text/css", std::string("\x20\x00\x10p{}\x03", 7), "Content-Encoding: br\r\n");
        srv.answer["/api"] = Answer("text/plain", "ok");
        br::TabHost host(br::TabHost::TextureFns(), "OkaySpace Browser/9");
        host.Tab().SetAnyOrigin(true);
        int fonts = 0;
        std::size_t fontBytes = 0;
        bool sfntFont = false;
        host.SetFontLoader([&](const br::FontFace& face, const std::vector<std::uint8_t>& bytes) {
            ++fonts;
            fontBytes = bytes.size();
            sfntFont = face.family == "shop" && bytes.size() > 4 && bytes[0] == 0 && bytes[1] == 1 && bytes[2] == 0 && bytes[3] == 0;
        });
        std::vector<br::TabHost::FetchReport> seen;
        bool brotliOk = false;
        host.SetFetchReporter([&](const br::TabHost::FetchReport& f) {
            seen.push_back(f);
            if (std::string(f.kind) == "style" && f.reached && f.status == 200 && f.body && *f.body == "p{}") brotliOk = true;   // body: during the call only
        });
        CHECK(host.Open(base + "/"));
        const auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < std::chrono::seconds(20)) {
            host.Pump();
            host.Tab().Layout(800, br::FixedMeasure());
            if (fonts > 0 && host.Tab().Title().find("|ok") != std::string::npos && seen.size() >= 5) break;
            timespec ts{0, 10000000L};
            ::nanosleep(&ts, nullptr);
        }
        const br::Node* p = host.Tab().Document() ? host.Tab().Document()->FindAnchor("p") : nullptr;
        CHECK(p && p->TextContent() == "caf\xC3\xA9");   // windows-1252 E9 read as U+00E9
        CHECK(fonts == 1 && sfntFont && fontBytes == ttf.size());   // the WOFF file decoded back to the TrueType file it holds
        CHECK(host.Tab().Title() == "OkaySpace Browser/9|ok");      // navigator.userAgent is what the requests carry
        CHECK(brotliOk);
        auto head = [&](const std::string& path) {
            for (const std::string& h : srv.heads) if (h.rfind("GET " + path + " ", 0) == 0) return h;
            return std::string();
        };
        const std::string page = head("/"), css = head("/s.css"), img = head("/i.png"), font = head("/f.woff"), api = head("/api");
        CHECK(page.find("\r\nAccept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n") != std::string::npos);
        CHECK(page.find("\r\nAccept-Encoding: gzip, deflate, br\r\n") != std::string::npos);
        CHECK(page.find("\r\nAccept-Language: en-US,en;q=0.9\r\n") != std::string::npos);
        CHECK(page.find("\r\nUpgrade-Insecure-Requests: 1\r\n") != std::string::npos);
        CHECK(page.find("\r\nSec-Fetch-Dest: document\r\nSec-Fetch-Mode: navigate\r\nSec-Fetch-Site: none\r\nSec-Fetch-User: ?1\r\n") != std::string::npos);
        CHECK(page.find("\r\nUser-Agent: OkaySpace Browser/9\r\n") != std::string::npos && page.find("Referer") == std::string::npos);
        CHECK(css.find("\r\nAccept: text/css,*/*;q=0.1\r\n") != std::string::npos && css.find("Sec-Fetch-Dest: style\r\nSec-Fetch-Mode: no-cors\r\nSec-Fetch-Site: same-origin") != std::string::npos);
        CHECK(css.find("\r\nReferer: " + base + "/\r\n") != std::string::npos);
        CHECK(img.find("\r\nAccept: image/png,image/svg+xml,image/*;q=0.8,*/*;q=0.5\r\n") != std::string::npos && img.find("Sec-Fetch-Dest: image") != std::string::npos);
        CHECK(font.find("Sec-Fetch-Dest: font\r\nSec-Fetch-Mode: cors") != std::string::npos);
        CHECK(api.find("\r\nAccept: */*\r\n") != std::string::npos && api.find("Sec-Fetch-Dest: empty\r\nSec-Fetch-Mode: cors\r\nSec-Fetch-Site: same-origin") != std::string::npos);
        for (const std::string& h : srv.heads) {   // what is never sent: client hints, another browser's brand
            CHECK(h.find("Sec-CH-") == std::string::npos && h.find("sec-ch-") == std::string::npos);
            CHECK(h.find("Chrome") == std::string::npos && h.find("Firefox") == std::string::npos && h.find("Safari") == std::string::npos);
        }
        if (page.find("Sec-Fetch-User") == std::string::npos || css.find("Referer") == std::string::npos)
            std::cerr << "  heads seen: " << srv.heads.size() << "\n--- page\n" << page << "\n--- css\n" << css << "\n";
    }

    TEST_MAIN_RESULT();
}
