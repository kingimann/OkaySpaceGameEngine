// Browser (page engine): addresses (engine/include/okay/Browser/Url.hpp, against the examples of RFC 3986 section 5.4) and
// one tab's navigation (engine/include/okay/Browser/Browser.hpp): loading, errors, links in and out of the site, back and
// forward with the cache and the scroll position, images, fragments and refused schemes. The "network" is the test itself.
#include "test_framework.hpp"
#include "okay/Browser/Browser.hpp"
#include "BrowserPage.hpp"   // launcher/: which links the launcher shows itself

#include <string>
#include <vector>

using namespace okay::browser;

static bool Res(const char* ref, const char* want) {
    const std::string got = ResolveUrl("http://a/b/c/d;p?q", ref);
    if (got != want) { std::cerr << "  resolve '" << ref << "': want " << want << " got " << got << "\n"; return false; }
    return true;
}

int main() {
    RUN_SUITE("browser url");

    // RFC 3986 5.4.1 normal examples.
    CHECK(Res("g:h", "g:h"));
    CHECK(Res("g", "http://a/b/c/g"));
    CHECK(Res("./g", "http://a/b/c/g"));
    CHECK(Res("g/", "http://a/b/c/g/"));
    CHECK(Res("/g", "http://a/g"));
    CHECK(Res("//g", "http://g"));
    CHECK(Res("?y", "http://a/b/c/d;p?y"));
    CHECK(Res("g?y", "http://a/b/c/g?y"));
    CHECK(Res("#s", "http://a/b/c/d;p?q#s"));
    CHECK(Res("g#s", "http://a/b/c/g#s"));
    CHECK(Res("g?y#s", "http://a/b/c/g?y#s"));
    CHECK(Res(";x", "http://a/b/c/;x"));
    CHECK(Res("g;x", "http://a/b/c/g;x"));
    CHECK(Res("g;x?y#s", "http://a/b/c/g;x?y#s"));
    CHECK(Res("", "http://a/b/c/d;p?q"));
    CHECK(Res(".", "http://a/b/c/"));
    CHECK(Res("./", "http://a/b/c/"));
    CHECK(Res("..", "http://a/b/"));
    CHECK(Res("../", "http://a/b/"));
    CHECK(Res("../g", "http://a/b/g"));
    CHECK(Res("../..", "http://a/"));
    CHECK(Res("../../", "http://a/"));
    CHECK(Res("../../g", "http://a/g"));
    // 5.4.2 abnormal examples.
    CHECK(Res("../../../g", "http://a/g"));
    CHECK(Res("../../../../g", "http://a/g"));
    CHECK(Res("/./g", "http://a/g"));
    CHECK(Res("/../g", "http://a/g"));
    CHECK(Res("g.", "http://a/b/c/g."));
    CHECK(Res(".g", "http://a/b/c/.g"));
    CHECK(Res("g..", "http://a/b/c/g.."));
    CHECK(Res("..g", "http://a/b/c/..g"));
    CHECK(Res("./../g", "http://a/b/g"));
    CHECK(Res("./g/.", "http://a/b/c/g/"));
    CHECK(Res("g/./h", "http://a/b/c/g/h"));
    CHECK(Res("g/../h", "http://a/b/c/h"));
    CHECK(Res("g;x=1/./y", "http://a/b/c/g;x=1/y"));
    CHECK(Res("g;x=1/../y", "http://a/b/c/y"));
    CHECK(Res("g?y/./x", "http://a/b/c/g?y/./x"));
    CHECK(Res("g?y/../x", "http://a/b/c/g?y/../x"));
    CHECK(Res("g#s/./x", "http://a/b/c/g#s/./x"));
    CHECK(Res("g#s/../x", "http://a/b/c/g#s/../x"));
    CHECK(Res("http:g", "http:g"));

    // Parts, origins, hosts.
    {
        const Url u = ParseUrl("  HTTPS://User@Example.COM:8443/p/a?q=1#frag ");
        CHECK(u.scheme == "https" && u.authority == "User@example.com:8443" && u.path == "/p/a" && u.query == "q=1" && u.fragment == "frag");
        CHECK(ToString(ParseUrl("http://a/b?#")) == "http://a/b?#");
        CHECK(OriginOf("https://Example.com:443/x") == "https://example.com");
        CHECK(OriginOf("http://example.com:8080/x") == "http://example.com:8080");
        CHECK(OriginOf("mailto:a@b") == "");
        CHECK(SameOrigin("https://a.test/x", "https://A.test:443/y#z"));
        CHECK(!SameOrigin("https://a.test/x", "http://a.test/x"));
        CHECK(!SameOrigin("https://a.test/x", "https://b.a.test/x"));
        CHECK(!SameOrigin("mailto:x", "mailto:x"));
        CHECK(HostOf("https://u:p@Host.Test:1/x") == "host.test");
        CHECK(WithoutFragment("http://a/b#c") == "http://a/b" && FragmentOf("http://a/b#c") == "c" && FragmentOf("http://a/b") == "");
        CHECK(RemoveDotSegments("/a/b/c/./../../g") == "/a/g");
        CHECK(RemoveDotSegments("mid/content=5/../6") == "mid/6");
    }

    RUN_SUITE("browser tab");
    const FixedMeasure measure;
    const std::string home = "https://site.test/docs/index.html";
    const std::string homePage = "<title>Docs</title><base href='/docs/'><h1 id=a>Hi</h1><a href='b.html'>b</a><img src='/img/x.png'>"
                                 "<img src='https://elsewhere.test/y.png'><p style='height:2000px'>long</p><h2 id=end>End</h2>";
    {
        BrowserTab t;
        CHECK(t.GetState() == BrowserTab::State::Empty && t.Url().empty());
        CHECK(!t.Open("ftp://site.test/x") && t.GetState() == BrowserTab::State::Error);
        CHECK(!t.Open("javascript:alert(1)"));
        CHECK(t.Open(home));
        CHECK(t.GetState() == BrowserTab::State::Loading && t.Url() == home && t.Origin() == "https://site.test");
        std::vector<std::string> f = t.TakeFetches();
        CHECK(f.size() == 1 && f[0] == home);
        CHECK(t.TakeFetches().empty());
        t.DeliverPage("https://site.test/other.html", true, 200, "<p>stale</p>");   // not the page being loaded: ignored
        CHECK(t.GetState() == BrowserTab::State::Loading);
        t.DeliverPage(home, true, 200, homePage);
        CHECK(t.GetState() == BrowserTab::State::Ready && t.Title() == "Docs" && t.Document() != nullptr);
        f = t.TakeFetches();   // the same-origin image only
        CHECK(f.size() == 1 && f[0] == "https://site.test/img/x.png");
        CHECK(t.ResolveImage("/img/x.png") == "https://site.test/img/x.png");
        int w = 0, h = 0;
        CHECK(!t.ImageSize("/img/x.png", w, h));
        t.DeliverImage("https://site.test/img/x.png", 64, 32);
        CHECK(t.ImageSize("/img/x.png", w, h) && w == 64 && h == 32);
        const LayoutResult& lay = t.Layout(500, measure);
        bool sawImage = false;
        for (const DrawCmd& c : lay.cmds) if (c.kind == DrawCmd::Image && c.w == 64.0f) sawImage = true;
        CHECK(sawImage);

        // In-page fragment: scroll, no load.
        BrowserTab::Action a = t.Activate("#end");
        CHECK(a.kind == BrowserTab::Action::Scrolled && a.scrollY > 2000.0f);
        CHECK(t.GetState() == BrowserTab::State::Ready);
        // Other sites go to the system browser; other schemes are refused.
        a = t.Activate("https://other.test/page");
        CHECK(a.kind == BrowserTab::Action::External && a.url == "https://other.test/page");
        CHECK(t.Activate("javascript:alert(1)").kind == BrowserTab::Action::Refused);
        CHECK(t.Activate("file:///etc/passwd").kind == BrowserTab::Action::Refused);
        CHECK(t.Activate("data:text/html,hi").kind == BrowserTab::Action::Refused);
        CHECK(t.Activate("http://site.test/docs/b.html").kind == BrowserTab::Action::External);   // http is another origin

        // A same-site link, resolved against <base href>: a new entry and a load; a 404 is an error page.
        t.ScrollY() = 120.0f;
        a = t.Activate("b.html");
        CHECK(a.kind == BrowserTab::Action::Navigated && a.url == "https://site.test/docs/b.html");
        CHECK(t.GetState() == BrowserTab::State::Loading && t.CanGoBack() && !t.CanGoForward());
        f = t.TakeFetches();
        CHECK(f.size() == 1 && f[0] == "https://site.test/docs/b.html");
        t.DeliverPage("https://site.test/docs/b.html", false, 404, "<h1>Not found</h1>");
        CHECK(t.GetState() == BrowserTab::State::Error && t.Error().find("404") != std::string::npos);
        CHECK(t.ScrollY() == 0.0f);

        // Back: the cached page shows at once, at the old scroll position, with no fetch.
        CHECK(t.GoBack());
        CHECK(t.GetState() == BrowserTab::State::Ready && t.Url() == home && t.Title() == "Docs" && t.ScrollY() == 120.0f);
        CHECK(t.TakeFetches().empty());
        CHECK(t.CanGoForward());
        CHECK(t.GoForward());
        CHECK(t.GetState() == BrowserTab::State::Loading);   // the error page was not cached
        CHECK(t.TakeFetches().size() == 1);
        t.DeliverPage("https://site.test/docs/b.html", true, 200, "<p>B</p>");
        CHECK(t.GetState() == BrowserTab::State::Ready && t.Title().empty());
        CHECK(!t.GoForward());

        // A link to another page with a fragment scrolls once that page has loaded.
        t.Activate("index.html#end");
        t.TakeFetches();
        CHECK(t.GetState() == BrowserTab::State::Ready);   // the page was cached: shown at once
        t.Layout(500, measure);
        CHECK(t.PendingScroll() > 2000.0f);
        CHECK(t.PendingScroll() < 0.0f);

        // Reload drops the page from the cache and fetches it again.
        t.Reload();
        CHECK(t.GetState() == BrowserTab::State::Loading);
        f = t.TakeFetches();
        CHECK(f.size() == 1 && f[0] == home);

        // A transport failure.
        t.DeliverPage(home, false, 0, "", "could not connect");
        CHECK(t.GetState() == BrowserTab::State::Error && t.Error().find("could not connect") != std::string::npos);
    }
    {   // The history is capped; a plain-text address shows as text.
        BrowserTab t;
        t.Open("https://site.test/0");
        for (int i = 1; i < 100; ++i) t.Navigate("https://site.test/" + std::to_string(i));
        int back = 0;
        while (t.GoBack()) ++back;
        CHECK(back == (int)BrowserTab::kMaxHistory - 1);
        CHECK(PlainTextPage("a<b&c") .find("a&lt;b&amp;c") != std::string::npos);
        t.Open("https://site.test/notes.txt");
        t.TakeFetches();
        t.DeliverPage("https://site.test/notes.txt", true, 200, "<b>not bold</b>\nline 2");
        CHECK(t.GetState() == BrowserTab::State::Ready && t.Document() && t.Document()->Body()->TextContent() == "<b>not bold</b>\nline 2");
    }

    RUN_SUITE("launcher: our own pages");
    {
        const std::string server = "https://abcd.supabase.co";
        CHECK(okl::IsServerPage("https://abcd.supabase.co/storage/v1/object/public/pages/news.html", server));
        CHECK(okl::IsServerPage("https://ABCD.supabase.co:443/storage/v1/object/public/pages/help", server));
        CHECK(okl::IsServerPage("https://abcd.supabase.co/notes.TXT#top", server));
        CHECK(!okl::IsServerPage("https://abcd.supabase.co/storage/v1/object/public/workshop/game.zip", server));   // a download
        CHECK(!okl::IsServerPage("https://abcd.supabase.co/x.exe", server));
        CHECK(!okl::IsServerPage("http://abcd.supabase.co/news.html", server));      // another origin (http)
        CHECK(!okl::IsServerPage("https://evil.test/news.html", server));
        CHECK(!okl::IsServerPage("https://abcd.supabase.co.evil.test/news.html", server));
        CHECK(!okl::IsServerPage("https://abcd.supabase.co/news.html", ""));          // no server configured
        CHECK(!okl::IsServerPage("javascript:alert(1)", server));
    }

    TEST_MAIN_RESULT();
}
