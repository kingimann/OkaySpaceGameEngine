// Browser (page engine): hostile input. Pages and style sheets built to break a reader (very deep nesting, huge and many
// attributes, endless unclosed constructs, random bytes, too many nodes, a page larger than the cap) must be read and laid
// out without a crash, within the limits of HtmlLimits / CssLimits, and in reasonable time.
#include "test_framework.hpp"
#include "okay/Browser/Browser.hpp"

#include <chrono>
#include <cstdint>
#include <string>

using namespace okay::browser;

static int Depth(const Node& n) {
    int d = 0;
    for (const auto& c : n.children) { const int k = Depth(*c); if (k > d) d = k; }
    return d + 1;
}
static std::size_t Count(const Node& n) {
    std::size_t k = 1;
    for (const auto& c : n.children) k += Count(*c);
    return k;
}

int main() {
    RUN_SUITE("browser hostile input");
    const FixedMeasure measure;
    const auto t0 = std::chrono::steady_clock::now();

    {   // 100 000 nested <div>s, then the same with <ul><li> and <table><tr><td>: flattened at the depth limit.
        for (const char* open : {"<div>", "<ul><li>", "<table><tr><td>", "<b><i>", "<blockquote>"}) {
            std::string s;
            for (int i = 0; i < 100000 / (int)std::string(open).size() * 5; ++i) s += open;
            s += "deep";
            const HtmlDocument d = ParseHtml(s);
            CHECK(Depth(*d.root) <= HtmlLimits().maxDepth + 3);
            const LayoutResult r = LayoutPage(d, 800, measure);
            CHECK(r.height >= 0.0f);
        }
    }
    {   // One huge attribute value, a huge name, and a thousand attributes.
        std::string s = "<p title=\"" + std::string(1u << 20, 'x') + "\" " + std::string(5000, 'n') + "=1";
        for (int i = 0; i < 1000; ++i) s += " a" + std::to_string(i) + "=v";
        s += ">t</p>";
        const HtmlDocument d = ParseHtml(s);
        const Node* p = d.Body()->children.empty() ? nullptr : d.Body()->children[0].get();
        CHECK(p && p->name == "p");
        if (p) {
            CHECK(p->attrs.size() <= HtmlLimits().maxAttrs);
            CHECK(p->Attr("title") && p->Attr("title")->size() <= HtmlLimits().maxAttrValueBytes);
            for (const auto& a : p->attrs) CHECK(a.first.size() <= HtmlLimits().maxNameBytes);
        }
        CHECK(d.truncated);
    }
    {   // Unclosed things at the end of the page.
        for (const char* s : {"<", "</", "<!", "<!-", "<!--", "<!-- x", "<!DOCTYPE", "<p", "<p a", "<p a=", "<p a='x", "<p a=\"x",
                              "&", "&#", "&#x", "&#99999999999999;", "&#x110000;", "<style>", "<style>p{", "<script><!--", "<title>t",
                              "<textarea>", "</p", "<p/", "<a href=x", "<?xml ?>", "<![CDATA[x]]>", "\0<p>\0", "<plaintext><p>"}) {
            const HtmlDocument d = ParseHtml(std::string(s, s[0] ? std::char_traits<char>::length(s) : 6));
            CHECK(d.Html() != nullptr && d.Body() != nullptr);
            LayoutPage(d, 300, measure);
        }
    }
    {   // Too many nodes: cut at the limit.
        std::string s;
        for (int i = 0; i < 300000; ++i) s += "<i></i>";
        HtmlLimits lim;
        lim.maxNodes = 5000;
        const HtmlDocument d = ParseHtml(s, lim);
        CHECK(d.truncated && Count(*d.root) <= lim.maxNodes + 8);
    }
    {   // A page past the input cap.
        HtmlLimits lim;
        lim.maxInputBytes = 1000;
        const HtmlDocument d = ParseHtml(std::string(5000, 'a'), lim);
        CHECK(d.truncated && d.Body()->TextContent().size() == 1000);
    }
    {   // Random bytes and random tag soup, many times (a fixed seed, so a failure repeats).
        std::uint32_t seed = 12345u;
        auto next = [&]() { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
        static const char* bits[] = {"<p>", "</p>", "<li>", "<ul>", "</ul>", "<table>", "<td>", "<tr>", "</table>", "<b>", "</b>",
                                     "<a href=x>", "</a>", "<img src=a>", "<br>", "<pre>", "</pre>", "<h1>", "</div>", "<div style='margin:-9999px;width:-5px;padding:99999px'>",
                                     "&amp;", "&#", "<!--", "-->", "<style>p{display:list-item}</style>", "<script>", "</script>", " ", "word ",
                                     "\n", "<okay-game width=99999999 height=-4>", "<canvas>", "<hr>", "<ol start=-2147483648>", "<li value=2147483647>",
                                     "<p style='font-size:1000000px;line-height:-3'>", "<p style='font-size:0'>", "<span style='display:block'>"};
        const int nbits = (int)(sizeof(bits) / sizeof(bits[0]));
        for (int round = 0; round < 300; ++round) {
            std::string s;
            const int len = (int)(next() % 400);
            for (int i = 0; i < len; ++i) {
                if (next() % 4 == 0) s += (char)(next() & 0xFF);
                else s += bits[next() % nbits];
            }
            const HtmlDocument d = ParseHtml(s);
            const float width = (float)(next() % 1200);
            const LayoutResult r = LayoutPage(d, width, measure, [](const std::string&, int& w, int& h) { w = 100000; h = 3; return true; });
            CHECK(r.height == r.height && r.height < 1e12f);   // finite
            for (const DrawCmd& c : r.cmds) if (!(c.x == c.x && c.y == c.y && c.w == c.w && c.h == c.h)) { CHECK(false); break; }
        }
    }
    {   // Style sheets: garbage, deep brackets, unclosed strings and comments, too many rules.
        for (const std::string& css : {std::string(100000, '{'), std::string(100000, '}'), std::string("p{color:red") , std::string("/* never closed"),
                                       std::string("p{content:'never closed}"), std::string(50000, '(') + "p{}", std::string("@media{") + std::string(10000, '{'),
                                       std::string("a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a{color:red}")}) {
            const StyleSheet sh = ParseStyleSheet(css);
            CHECK(sh.rules.size() <= CssLimits().maxRules);
            for (const Rule& r : sh.rules) for (const Selector& s : r.selectors) CHECK(s.parts.size() <= CssLimits().maxSelectorParts);
        }
        std::string many;
        for (int i = 0; i < 30000; ++i) many += "p{color:red}";
        CHECK(ParseStyleSheet(many).rules.size() <= CssLimits().maxRules);
        const HtmlDocument d = ParseHtml("<style>" + many + "</style><p>x</p>");
        LayoutPage(d, 300, measure);
    }
    {   // The tab: an answer bigger than the cap is cut, not refused, and still shows.
        BrowserTab t;
        t.Open("https://site.test/");
        t.TakeFetches();
        t.DeliverPage("https://site.test/", true, 200, "<p>" + std::string(BrowserTab::kMaxPageBytes + 100, 'z'));
        CHECK(t.GetState() == BrowserTab::State::Ready);
    }

    // Round 1 of the browser program's layout: nested flex and grid boxes (each measures its items, so a naive engine is
    // exponential in the depth), floats by the thousand, a grid with absurd spans and lines, var() that doubles at every step,
    // nested @media, selectors that backtrack, and absolutely positioned boxes everywhere.
    {
        auto lay = [&](const std::string& page, double maxSecs) {
            const auto s0 = std::chrono::steady_clock::now();
            const HtmlDocument d = ParseHtml(page);
            const LayoutResult r = LayoutPage(d, 800, measure);
            const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - s0).count();
            if (s > maxSecs) std::cerr << "  slow: " << s << " s for a page of " << page.size() << " bytes\n";
            return s <= maxSecs && r.height >= 0.0f;
        };
        std::string nest;
        for (int i = 0; i < 40; ++i) nest += i % 2 ? "<div style='display:flex;flex-direction:column'><div style='flex:1'>x</div>" : "<div style='display:flex'><div style='flex:1'>x</div>";
        CHECK(lay(nest, 20.0));
        std::string gnest;
        for (int i = 0; i < 40; ++i) gnest += "<div style='display:grid;grid-template-columns:auto 1fr'><span>a</span>";
        CHECK(lay(gnest, 20.0));
        std::string floats;
        for (int i = 0; i < 3000; ++i) floats += i % 3 ? "<div style='float:left;width:7px;height:3px'></div>" : "<div style='float:right'>w</div> text ";
        CHECK(lay(floats, 20.0));
        CHECK(lay("<div style='display:grid;grid-template-columns:repeat(100000,1px);grid-auto-flow:column dense'>"
                  "<i style='grid-column:-99999 / span 99999;grid-row:span 99999'>a</i><i style='grid-area:x'>b</i>"
                  "<i style='grid-row:2147483647'>c</i>" + std::string(2000, 'q') + "</div>", 20.0));
        std::string grid;
        for (int i = 0; i < 2000; ++i) grid += "<i style='grid-column:span " + std::to_string(i % 7 + 1) + "'>x</i>";
        CHECK(lay("<div style='display:grid;grid-template-columns:repeat(auto-fill,minmax(1px,1fr));grid-auto-flow:dense'>" + grid + "</div>", 20.0));
        std::string vars = "<style>:root{--a0:x;";
        for (int i = 1; i < 40; ++i) vars += "--a" + std::to_string(i) + ":var(--a" + std::to_string(i - 1) + ") var(--a" + std::to_string(i - 1) + ");";
        vars += "} p{width:var(--a39);margin:calc(((((((1px+2px)*3)/4)-5px)*6)+7px))}</style><p>v</p>";
        CHECK(lay(vars, 20.0));
        std::string media = "<style>";
        for (int i = 0; i < 50; ++i) media += "@media (min-width:" + std::to_string(i) + "px) { @supports (display:grid) { ";
        media += "p{color:red}" + std::string(100, '}') + "</style><p>m</p>";
        CHECK(lay(media, 20.0));
        std::string sel = "<style>" + std::string(".a ~ .a ~ .a ~ .a ~ .a ~ .a ~ .a ~ .a ~ .a ~ .a ~ .a ~ .a ~ .a ~ .b") + "{color:red} "
                          "div div div div div div div div div div div div div div div div p{color:blue} :not(:not(:not(:nth-child(2n+1)))){width:1px}</style>";
        std::string many;
        for (int i = 0; i < 400; ++i) many += "<span class=a>a</span>";
        std::string deep;
        for (int i = 0; i < 60; ++i) deep += "<div>";
        CHECK(lay(sel + many + deep + "<p>z</p>", 30.0));
        std::string abs;
        for (int i = 0; i < 2000; ++i) abs += "<div style='position:" + std::string(i % 2 ? "absolute" : "relative") + ";left:" + std::to_string(i) + "px;z-index:" + std::to_string(i % 13 - 6) + ";overflow:auto'>a";
        CHECK(lay(abs, 30.0));
    }

    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::cout << "  hostile input took " << secs << " s\n";
    CHECK(secs < 120.0);
    TEST_MAIN_RESULT();
}
