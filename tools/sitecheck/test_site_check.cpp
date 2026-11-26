// Site check (tools/sitecheck): the causes and the ranking (SiteReport), and the reporting hooks the tool reads - the page
// engine's "not implemented: X" console line (Script.hpp MissingWebApi), the CSS reporter (Css.hpp SetCssNotUsedReporter) and
// the tab host's fetch reporter (shared/BrowserHost.hpp SetFetchReporter, against a closed port on 127.0.0.1; no real site).
#include "test_framework.hpp"
#include "SiteReport.hpp"

#include "BrowserHost.hpp"
#include "okay/Browser/Browser.hpp"
#include "okay/Core/Json.hpp"

#include <chrono>
#include <iostream>
#include <map>
#include <memory>
#include <string>
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
        const std::vector<std::string> c = ConsoleOf("new IntersectionObserver(function () {});");
        CHECK(Has(c, "not implemented: IntersectionObserver"));
        const std::vector<std::string> d = ConsoleOf("myOwnLib.start();");
        CHECK(!Has(d, "not implemented"));
        if (!Has(c, "not implemented: IntersectionObserver")) for (const std::string& l : c) std::cerr << "  console: " << l << "\n";
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
        Fetch font; font.kind = "font"; font.reached = true; font.status = 200; font.note = "font not TrueType or OpenType"; font.head = "wOF2abcd";
        r.fetches.push_back(font);
        Fetch js; js.kind = "script"; js.reached = false; js.netError = "TLS handshake failed"; r.fetches.push_back(js);
        Fetch gone; gone.kind = "image"; gone.reached = true; gone.status = 404; r.fetches.push_back(gone);
        Fetch br2; br2.kind = "style"; br2.reached = true; br2.status = 200; br2.contentEncoding = "br"; r.fetches.push_back(br2);
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
        CHECK(HasKey(cs, "Content-Encoding 'br' not read"));
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

    TEST_MAIN_RESULT();
}
