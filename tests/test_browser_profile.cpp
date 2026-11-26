// Browser (docs/feature_map/17-launcher-platform.md 17.18): the browser program's model, round 1 of "a regular browser"
// (okay/Browser/Profile.hpp and the tab's new parts in okay/Browser/Browser.hpp): the address bar's address-or-search rule with
// the default Google search address, the urlencoded serializer, tab order, history, bookmarks, zoom per site, the profile file,
// downloads (which answers are files, their names), our own pages, POST loads, Stop, and find on the page.
#include "test_framework.hpp"
#include "okay/Browser/Browser.hpp"
#include "okay/Browser/Profile.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <set>
#include <string>

using namespace okay::browser;

static const FixedMeasure kMeasure(0.5f);

static bool Decides(const std::string& typed, AddressDecision::Kind kind, const std::string& url,
                    const std::string& tmpl = kDefaultSearchUrl) {
    const AddressDecision d = DecideAddress(typed, tmpl);
    if (d.kind != kind || d.url != url) {
        std::cerr << "  typed '" << typed << "': got kind " << (int)d.kind << " '" << d.url << "', want kind " << (int)kind << " '" << url << "'\n";
        return false;
    }
    return true;
}

static bool TakenIn(const std::string& path, void* user) { return static_cast<std::set<std::string>*>(user)->count(path) != 0; }

int main() {
    RUN_SUITE("browser program model");

    // ---- the urlencoded serializer (WHATWG URL 5.2) ----
    CHECK(FormUrlEncode("a b") == "a+b");
    CHECK(FormUrlEncode("a&b=c#d") == "a%26b%3Dc%23d");
    CHECK(FormUrlEncode("*-._~!") == "*-._%7E%21");
    CHECK(FormUrlEncode("\xC3\xBC" "ber caf\xC3\xA9") == "%C3%BCber+caf%C3%A9");   // UTF-8 bytes
    CHECK(FormUrlEncode("+%") == "%2B%25");
    CHECK(FormUrlEncodePairs({{"q", "x y"}, {"n", ""}, {"a b", "1&2"}}) == "q=x+y&n=&a+b=1%262");

    // ---- the address bar: Google is the default search (owner, 2026-11-26) ----
    using K = AddressDecision;
    CHECK(std::string(kDefaultSearchUrl) == "https://www.google.com/search?q=%s");
    CHECK(Decides("", K::Empty, ""));
    CHECK(Decides("   ", K::Empty, ""));
    CHECK(Decides("hello world", K::Search, "https://www.google.com/search?q=hello+world"));
    CHECK(Decides("a&b #c", K::Search, "https://www.google.com/search?q=a%26b+%23c"));
    CHECK(Decides("\xC3\xBC" "ber caf\xC3\xA9", K::Search, "https://www.google.com/search?q=%C3%BCber+caf%C3%A9"));
    CHECK(Decides("cats", K::Search, "https://www.google.com/search?q=cats"));
    CHECK(Decides("3.14", K::Search, "https://www.google.com/search?q=3.14"));
    CHECK(Decides("100%", K::Search, "https://www.google.com/search?q=100%25"));
    CHECK(Decides("word:thing", K::Search, "https://www.google.com/search?q=word%3Athing"));
    // typed addresses stay addresses (not searches)
    CHECK(Decides("example.com", K::Address, "https://example.com"));
    CHECK(Decides("example.com/a/b?x=1#top", K::Address, "https://example.com/a/b?x=1#top"));
    CHECK(Decides("  www.example.org  ", K::Address, "https://www.example.org"));
    CHECK(Decides("http://example.com/x", K::Address, "http://example.com/x"));
    CHECK(Decides("HTTPS://Example.com", K::Address, "HTTPS://Example.com"));
    CHECK(Decides("https://example.com/a b", K::Address, "https://example.com/a%20b"));
    CHECK(Decides("localhost", K::Address, "http://localhost"));
    CHECK(Decides("localhost:8000/page.html", K::Address, "http://localhost:8000/page.html"));
    CHECK(Decides("127.0.0.1:8080", K::Address, "http://127.0.0.1:8080"));
    CHECK(Decides("192.168.1.20/admin", K::Address, "http://192.168.1.20/admin"));
    CHECK(Decides("myserver:8080", K::Address, "http://myserver:8080"));
    CHECK(Decides("about:history", K::Address, "about:history"));
    CHECK(Decides("ftp://files.example.com/x", K::Address, "ftp://files.example.com/x"));   // the tab refuses it
    // another search address, and none
    CHECK(Decides("a b", K::Search, "https://search.example/?q=a+b&lang=en", "https://search.example/?q=%s&lang=en"));
    CHECK(Decides("a b", K::Search, "https://search.example/find?q=a+b", "https://search.example/find?q="));
    CHECK(Decides("a b", K::NoSearch, "", ""));
    CHECK(Decides("example.com", K::Address, "https://example.com", ""));
    CHECK(AddressPlaceholder(kDefaultSearchUrl) == "Search Google or type an address");
    CHECK(AddressPlaceholder("https://search.example/?q=%s") == "Search or type an address");
    CHECK(AddressPlaceholder("") == "Type an address");

    // ---- tab order ----
    {
        TabOrder t;
        const int a = t.Open(false, true);
        const int b = t.Open(false, true);    // Ctrl+T: last, shown
        CHECK(t.Ids() == std::vector<int>({a, b}) && t.Active() == b);
        t.Activate(a);
        const int c = t.Open(true, false);    // Ctrl+click on a link in a: right after a, in the background
        const int d = t.Open(true, false);    // a second link from a: after c
        CHECK(t.Ids() == std::vector<int>({a, c, d, b}) && t.Active() == a);
        t.Next();
        CHECK(t.Active() == c);
        t.Previous(); t.Previous();
        CHECK(t.Active() == b);               // wraps around
        t.ActivateNumber(2);
        CHECK(t.Active() == c);
        t.ActivateNumber(9);
        CHECK(t.Active() == b);               // Ctrl+9: the last tab
        t.Activate(c);
        CHECK(t.Close(c, "https://c.example/"));
        CHECK(t.Active() == d);               // the one to its right
        t.Activate(b);
        CHECK(t.Close(b, "https://b.example/"));
        CHECK(t.Active() == d);               // the last tab closed: the one to its left
        CHECK(t.TakeClosed() == "https://b.example/");
        CHECK(t.TakeClosed() == "https://c.example/");
        CHECK(t.TakeClosed().empty());
        t.Move(d, 0);
        CHECK(t.Ids() == std::vector<int>({d, a}));
        CHECK(!t.Close(999));
        CHECK(t.Close(d) && t.Close(a) && t.Active() == -1 && t.Count() == 0);
    }

    // ---- history, bookmarks, zoom ----
    {
        History h;
        h.Add("https://a.example/", "A", 100);
        h.Add("https://a.example/", "", 200);   // the same page again: refreshed, not doubled
        h.Add("https://b.example/", "B", 300);
        h.Add("about:blank", "", 400);           // never kept
        CHECK(h.Items().size() == 2 && h.Items()[0].time == 200 && h.Items()[0].title == "A");
        h.SetTitle("https://b.example/", "Bee");
        CHECK(h.Items()[1].title == "Bee");
        h.Remove("https://a.example/");
        CHECK(h.Items().size() == 1);
        for (int i = 0; i < (int)History::kMax + 10; ++i) h.Add("https://x.example/" + std::to_string(i), "", i);
        CHECK(h.Items().size() == History::kMax);
        CHECK(h.Items().back().url == "https://x.example/" + std::to_string(History::kMax + 9));

        Bookmarks b;
        CHECK(b.Add("https://a.example/", "A"));
        CHECK(!b.Add("https://a.example/", "again"));
        CHECK(b.Toggle("https://b.example/", "B") && b.Has("https://b.example/"));
        CHECK(!b.Toggle("https://b.example/", "B") && !b.Has("https://b.example/"));
        CHECK(b.Add("https://c.example/", "") && b.Items().back().title == "https://c.example/");

        SiteZoom z;
        const std::string o = "https://a.example";
        CHECK(z.Get(o) == 1.0f);
        CHECK(z.In(o) == 1.1f && z.In(o) == 1.25f);
        CHECK(z.Out(o) == 1.1f && z.Out(o) == 1.0f && z.Out(o) == 0.9f);
        CHECK(z.Get("https://other.example") == 1.0f);   // per site
        z.Reset(o);
        CHECK(z.Get(o) == 1.0f && z.All().empty());
        for (int i = 0; i < 40; ++i) z.In(o);
        CHECK(z.Get(o) == 5.0f);
        for (int i = 0; i < 40; ++i) z.Out(o);
        CHECK(z.Get(o) == 0.25f);
    }

    // ---- the profile file ----
    {
        Profile p;
        p.settings.searchUrl = "https://search.example/?q=%s";
        p.settings.homePage = "https://home.example/";
        p.settings.showBookmarksBar = false;
        p.bookmarks.Add("https://a.example/?x=\"1\"&y=\\", "A \"quoted\"\ttab\nline \xE2\x9C\x93");
        p.history.Add("https://h.example/", "H\x01", 1700000000);
        p.zoom.Set("https://z.example", 1.5f);
        const std::string dir = (std::filesystem::temp_directory_path() / "okay_browser_profile_test").u8string();
        std::filesystem::remove_all(std::filesystem::u8path(dir));
        const std::string path = dir + "/profile.json";
        std::string why;
        CHECK(SaveProfile(p, path, &why));
        Profile q;
        CHECK(LoadProfile(q, path, &why));
        CHECK(q.settings.searchUrl == p.settings.searchUrl && q.settings.homePage == p.settings.homePage && !q.settings.showBookmarksBar);
        CHECK(q.bookmarks.Items().size() == 1 && q.bookmarks.Items()[0].title == p.bookmarks.Items()[0].title &&
              q.bookmarks.Items()[0].url == p.bookmarks.Items()[0].url);
        CHECK(q.history.Items().size() == 1 && q.history.Items()[0].title == "H\x01" && q.history.Items()[0].time == 1700000000);
        CHECK(q.zoom.Get("https://z.example") == 1.5f);
        // a damaged file is refused and changes nothing
        Profile r;
        r.bookmarks.Add("https://keep.example/", "keep");
        CHECK(!r.FromJson("{\"bookmarks\": [", &why));
        CHECK(!r.FromJson("[1,2]", &why));
        CHECK(r.bookmarks.Items().size() == 1);
        // a profile from before any setting: the defaults (Google search) stay
        Profile s;
        CHECK(s.FromJson("{}") && s.settings.searchUrl == kDefaultSearchUrl);
        CHECK(!LoadProfile(s, dir + "/missing.json"));
        std::filesystem::remove_all(std::filesystem::u8path(dir));
    }

    // ---- one profile file, two programs (the browser program and the launcher's Browser page): ProfileStore ----
    {
        namespace fs = std::filesystem;
        const std::string dir = (fs::temp_directory_path() / "okay_browser_profile_store_test").u8string();
        fs::remove_all(fs::u8path(dir));
        const std::string path = dir + "/profile.json";
        ProfileStore a(path), b(path);
        CHECK(!a.Load() && !b.Load());   // no file yet: both start fresh
        a.Change([](Profile& p) { p.bookmarks.Add("https://a.example/", "A"); });
        CHECK(a.Dirty() && a.Get().bookmarks.Has("https://a.example/"));
        CHECK(a.Save());
        CHECK(!a.Dirty());
        // b has not changed anything: it reads a's file when it looks again
        CHECK(b.Refresh() && b.Get().bookmarks.Has("https://a.example/"));
        CHECK(!b.Refresh());   // nothing new since
        // both change something before either saves: neither change is lost
        a.Change([](Profile& p) { p.history.Add("https://ha.example/", "HA", 100); });
        b.Change([](Profile& p) { p.bookmarks.Add("https://b.example/", "B"); });
        b.Change([](Profile& p) { p.zoom.Set("https://b.example", 1.25f); });
        CHECK(a.Save());
        CHECK(b.Save());   // reads a's newer file first, then applies its own two changes on top
        CHECK(b.Get().bookmarks.Has("https://a.example/") && b.Get().bookmarks.Has("https://b.example/"));
        CHECK(b.Get().history.Items().size() == 1 && b.Get().history.Items()[0].url == "https://ha.example/");
        CHECK(b.Get().zoom.Get("https://b.example") == 1.25f);
        CHECK(a.Refresh() && a.Get().bookmarks.Has("https://b.example/") && a.Get().zoom.Get("https://b.example") == 1.25f);
        // a change that is not saved yet stays on top of a newer file read by Refresh
        a.Change([](Profile& p) { p.settings.homePage = "https://home.example/"; });
        b.Change([](Profile& p) { p.bookmarks.Remove("https://a.example/"); });
        CHECK(b.Save());
        CHECK(a.Refresh());
        CHECK(a.Dirty() && a.Get().settings.homePage == "https://home.example/" && !a.Get().bookmarks.Has("https://a.example/"));
        CHECK(a.Save());
        Profile disk;
        CHECK(LoadProfile(disk, path) && disk.settings.homePage == "https://home.example/" && !disk.bookmarks.Has("https://a.example/") &&
              disk.bookmarks.Has("https://b.example/"));
        // writes never leave a temporary file behind, and each writer uses its own temporary name
        int others = 0;
        for (const auto& e : fs::directory_iterator(fs::u8path(dir))) if (e.path().filename() != "profile.json") ++others;
        CHECK(others == 0);
        // a damaged file (half written by an old program) is not read over a good profile
        { std::FILE* f = std::fopen(path.c_str(), "wb"); if (f) { std::fputs("{\"bookmarks\": [", f); std::fclose(f); } }
        CHECK(!b.Refresh() && b.Get().bookmarks.Has("https://b.example/"));
        fs::remove_all(fs::u8path(dir));
    }

    // ---- downloads ----
    {
        const std::string head = "HTTP/1.1 302 Found\r\nLocation: /b\r\nContent-Type: text/html\r\n\r\n"
                                 "HTTP/1.1 200 OK\r\ncontent-type: application/zip\r\nContent-Disposition: attachment; filename=\"a b.zip\"\r\n\r\n";
        CHECK(HeaderValue(head, "Content-Type") == "application/zip");   // the last answer's
        CHECK(HeaderValue(head, "location").empty());
        CHECK(MediaType("Text/HTML; charset=UTF-8") == "text/html");
        CHECK(UseOfAnswer("text/html; charset=utf-8", "", "https://a/") == AnswerUse::Page);
        CHECK(UseOfAnswer("application/xhtml+xml", "", "https://a/") == AnswerUse::Page);
        CHECK(UseOfAnswer("text/plain", "", "https://a/x") == AnswerUse::PlainText);
        CHECK(UseOfAnswer("image/png", "", "https://a/x") == AnswerUse::Image);
        CHECK(UseOfAnswer("text/html", "attachment; filename=x.html", "https://a/") == AnswerUse::Download);
        CHECK(UseOfAnswer("application/zip", "", "https://a/x.zip") == AnswerUse::Download);
        CHECK(UseOfAnswer("application/octet-stream", "", "https://a/x") == AnswerUse::Download);
        CHECK(UseOfAnswer("", "", "https://a/page") == AnswerUse::Page);
        CHECK(UseOfAnswer("", "", "https://a/setup.exe") == AnswerUse::Download);

        CHECK(DownloadFileName("https://a.example/files/game%20one.zip?v=2", "", "application/zip") == "game one.zip");
        CHECK(DownloadFileName("https://a.example/get", "attachment; filename=\"report.pdf\"", "") == "report.pdf");
        CHECK(DownloadFileName("https://a.example/get", "attachment; filename*=UTF-8''na%C3%AFve%20file.txt; filename=\"x.txt\"", "") ==
              "na\xC3\xAFve file.txt");
        CHECK(DownloadFileName("https://a.example/get", "attachment; filename=\"../../evil.exe\"", "") == "evil.exe");
        CHECK(DownloadFileName("https://a.example/get", "attachment; filename=\"..\\\\..\\\\x.bat\"", "") == "x.bat");
        CHECK(DownloadFileName("https://a.example/get", "attachment; filename=\"a<b>:c|d?.txt\"", "") == "a_b__c_d_.txt");
        CHECK(DownloadFileName("https://a.example/CON.txt", "", "") == "_CON.txt");
        CHECK(DownloadFileName("https://a.example/", "", "application/zip") == "download.zip");
        CHECK(DownloadFileName("https://a.example/data", "", "application/pdf") == "data.pdf");
        CHECK(DownloadFileName("https://a.example/x.", "", "") == "x");
        CHECK(DownloadFileName("https://a.example/" + std::string(300, 'n') + ".zip", "", "").size() == 120);
        std::set<std::string> taken = {"/d/a.zip", "/d/a (1).zip", "/d/b.tar.gz"};
        CHECK(UniqueDownloadPath("/d", "new.zip", TakenIn, &taken) == "/d/new.zip");
        CHECK(UniqueDownloadPath("/d/", "a.zip", TakenIn, &taken) == "/d/a (2).zip");
        CHECK(UniqueDownloadPath("/d", "b.tar.gz", TakenIn, &taken) == "/d/b (1).tar.gz");
        CHECK(SizeText(300) == "300 bytes" && SizeText(12 * 1024) == "12 KB" && SizeText(1536 * 1024) == "1.5 MB" && SizeText(-1) == "unknown size");
    }

    // ---- our own pages ----
    {
        Profile p;
        p.bookmarks.Add("https://a.example/?q=<b>", "<script>alert(1)</script>");
        p.history.Add("https://h.example/", "Home \"page\"", 0);
        const std::string bm = InternalPage("about:bookmarks", p, {});
        CHECK(bm.find("<script>alert") == std::string::npos && bm.find("&lt;script&gt;") != std::string::npos);
        CHECK(bm.find("href=\"https://a.example/?q=&lt;b&gt;\"") != std::string::npos);
        const HtmlDocument hist = ParseHtml(InternalPage("about:history", p, {}));
        CHECK(hist.title == "History");
        DownloadItem d;
        d.fileName = "game.zip"; d.state = DownloadItem::Done; d.bytes = 2048; d.path = "/x/game.zip"; d.contentType = "application/zip";
        const std::string dl = InternalPage("about:downloads", p, {d});
        CHECK(dl.find("game.zip") != std::string::npos && dl.find("2 KB, saved to /x/game.zip") != std::string::npos);
        CHECK(ParseHtml(InternalPage("about:newtab", p, {})).title == "New tab");
        CHECK(IsInternalPage("about:newtab") && IsInternalPage("ABOUT:history#x") && !IsInternalPage("about:config") && !IsInternalPage("https://a/"));
    }

    // ---- the tab in the browser program: any site, our own pages, POST, Stop, downloads ----
    {
        BrowserTab t;
        t.SetAnyOrigin(true);
        CHECK(t.Open("about:newtab"));
        CHECK(t.TakeFetches() == std::vector<std::string>({"about:newtab"}));   // the host makes our own pages
        t.DeliverPage("about:newtab", true, 200, "<title>New tab</title><a href='https://a.example/'>a</a><a href='about:history'>h</a>");
        CHECK(t.GetState() == BrowserTab::State::Ready && t.TakeVisited());
        CHECK(t.Activate("about:history").kind == BrowserTab::Action::Navigated);   // our pages link to each other
        t.TakeFetches();
        t.DeliverPage("about:history", true, 200, "<title>History</title>");
        CHECK(t.Activate("https://a.example/").kind == BrowserTab::Action::Navigated);   // any site, in the same tab
        t.TakeFetches();
        t.DeliverPage("https://a.example/", true, 200,
                      "<title>A</title><a href='https://b.example/x'>b</a><a href='about:history'>h</a><a href='javascript:x()'>j</a>");
        CHECK(t.Activate("https://b.example/x").kind == BrowserTab::Action::Navigated);   // another site: not External any more
        t.Stop();                                                                         // ... stopped: page A again
        CHECK(t.GetState() == BrowserTab::State::Ready && t.Url() == "https://a.example/" && t.Title() == "A");
        CHECK(t.Activate("about:history").kind == BrowserTab::Action::Refused);   // a web page may not open our own pages
        CHECK(t.Activate("javascript:x()").kind == BrowserTab::Action::Refused);
        CHECK(t.TakeVisited());

        // a form sent with POST: the host asks for the body; the answer is not cached
        t.Submit("https://a.example/login", "POST", "user=me&pw=x+y");
        std::vector<std::string> f = t.TakeFetches();
        CHECK(f.size() == 1 && f[0] == "https://a.example/login");
        std::string body;
        CHECK(t.PostBody("https://a.example/login", body) && body == "user=me&pw=x+y");
        CHECK(!t.PostBody("https://a.example/other", body));
        t.DeliverPage("https://a.example/login", true, 200, "<title>Welcome</title>");
        CHECK(t.Title() == "Welcome" && !t.PostBody("https://a.example/login", body));   // loaded: nothing to send
        t.GoBack();
        t.TakeFetches();
        t.GoForward();   // back to the POST page: sent again (never from the cache)
        f = t.TakeFetches();
        CHECK(f.size() == 1 && t.PostBody(f[0], body) && body == "user=me&pw=x+y");
        t.DeliverPage(f[0], true, 200, "<title>Welcome</title>");
        // a GET form: just a new address
        t.Submit("https://a.example/search?q=x+y", "GET", "");
        f = t.TakeFetches();
        CHECK(f.size() == 1 && f[0] == "https://a.example/search?q=x+y" && !t.PostBody(f[0], body));
        t.DeliverPage(f[0], true, 200, "<title>Results</title>");

        // a link to a file: the tab goes back to the page it was on, and offers the download
        CHECK(t.Activate("https://a.example/game.zip").kind == BrowserTab::Action::Navigated);
        t.TakeFetches();
        t.DeliverDownload("https://a.example/game.zip", "application/zip", "game.zip", 5000);
        CHECK(t.GetState() == BrowserTab::State::Ready && t.Title() == "Results" && t.Url() == "https://a.example/search?q=x+y");
        CHECK(!t.CanGoForward());   // the file is not a history entry
        BrowserTab::DownloadOffer offer;
        CHECK(t.TakeDownloadOffer(offer) && offer.fileName == "game.zip" && offer.size == 5000 && offer.contentType == "application/zip");
        CHECK(!t.TakeDownloadOffer(offer));
        // a download as the very first address: the tab is empty
        BrowserTab e;
        e.SetAnyOrigin(true);
        e.Open("https://a.example/file.bin");
        e.TakeFetches();
        e.DeliverDownload("https://a.example/file.bin", "application/octet-stream", "file.bin", -1);
        CHECK(e.GetState() == BrowserTab::State::Empty && e.TakeDownloadOffer(offer));
        // the launcher's tab (default) still keeps to one site and shows no internal pages
        BrowserTab l;
        CHECK(!l.Open("about:newtab"));
        CHECK(l.Open("https://a.example/"));
        l.TakeFetches();
        l.DeliverPage("https://a.example/", true, 200, "<a href='https://b.example/'>b</a>");
        CHECK(l.Activate("https://b.example/").kind == BrowserTab::Action::External);
    }

    // ---- the last page stays while the next loads; find on the page ----
    {
        BrowserTab t;
        t.SetAnyOrigin(true);
        t.Open("https://a.example/");
        t.TakeFetches();
        t.DeliverPage("https://a.example/", true, 200, "<body style='margin:0'><p style='margin:0'>Find the cat. The CAT sat.</p>");
        const LayoutResult& r = t.Layout(800, kMeasure);
        CHECK(!r.cmds.empty());
        const std::vector<FindHit> hits = FindInPage(r, "cat", kMeasure);
        CHECK(hits.size() == 2);
        if (hits.size() == 2) {
            CHECK(hits[0].x == 9 * 8.0f && hits[0].w == 24.0f);   // "Find the " is 9 characters of 8 px
            CHECK(hits[1].x == 18 * 8.0f);                       // "Find the cat. The " is 18 characters
        }
        CHECK(FindInPage(r, "dog", kMeasure).empty() && FindInPage(r, "", kMeasure).empty());
        t.Activate("https://a.example/next");
        CHECK(t.GetState() == BrowserTab::State::Loading && t.HasPage());
        CHECK(!t.Layout(800, kMeasure).cmds.empty());   // still the last page
    }

    // ---- round 2 of the look: address-bar suggestions while typing ----
    {
        Profile p;
        p.history.Add("https://news.example.com/today", "Today's news", 100);
        p.history.Add("https://www.example.org/", "Example org", 200);
        p.history.Add("https://other.test/example-page", "Other", 300);
        p.history.Add("https://www.example.org/", "Example org", 400);   // the same address again: refreshed, not doubled
        p.history.Add("https://news.example.com/today", "Today's news", 500);
        p.history.Add("https://cats.test/", "All about cats", 600);
        p.bookmarks.Add("https://example.net/docs", "Example docs");
        const std::vector<OpenTabInfo> open = {{7, "https://shop.example.io/", "Example shop"}};

        // the first row is what Enter does: an address for an address-like text ...
        std::vector<Suggestion> s = SuggestAddresses("exa", p, open, 8);
        CHECK(!s.empty() && s[0].kind == Suggestion::Search && s[0].url == "https://www.google.com/search?q=exa");
        s = SuggestAddresses("example.org", p, open, 8);
        CHECK(!s.empty() && s[0].kind == Suggestion::Go && s[0].url == "https://example.org");
        // ... then the matches: the open tab, the bookmark and the history, each address once, a host that starts with the
        // text before one that only has it inside
        s = SuggestAddresses("exa", p, open, 8);
        std::vector<std::string> urls;
        for (const Suggestion& x : s) urls.push_back(x.url);
        CHECK(s.size() == 6);   // the search, the tab, the bookmark, www.example.org, other.test, news.example.com
        bool tabRow = false, bookRow = false;
        for (const Suggestion& x : s) {
            if (x.kind == Suggestion::OpenTab) tabRow = x.tabId == 7 && x.url == "https://shop.example.io/";
            if (x.kind == Suggestion::Bookmark) bookRow = x.url == "https://example.net/docs" && x.title == "Example docs";
        }
        CHECK(tabRow && bookRow);
        CHECK(std::count(urls.begin(), urls.end(), "https://www.example.org/") == 1);
        CHECK(std::find(urls.begin(), urls.end(), "https://cats.test/") == urls.end());
        const auto at = [&](const std::string& u) { return std::find(urls.begin(), urls.end(), u) - urls.begin(); };
        CHECK(at("https://www.example.org/") < at("https://other.test/example-page"));   // "www." is skipped: example.org starts with it
        CHECK(at("https://news.example.com/today") > at("https://www.example.org/"));
        // several words: every one must be in the title or the address; the case does not matter
        s = SuggestAddresses("TODAY news", p, {}, 8);
        CHECK(s.size() == 2 && s[1].url == "https://news.example.com/today");
        // at most `max`, and nothing for an empty bar
        CHECK(SuggestAddresses("e", p, open, 3).size() == 3);
        CHECK(SuggestAddresses("   ", p, open, 8).empty());
        // no search address set: words give no first row, matches still show
        p.settings.searchUrl.clear();
        s = SuggestAddresses("cats", p, {}, 8);
        CHECK(s.size() == 1 && s[0].kind == Suggestion::History && s[0].url == "https://cats.test/");
    }

    // ---- round 2 of the look: the site badge, the address shown with its host first, friendly error pages ----
    {
        CHECK(BadgeOf("https://a.example/x") == SiteBadge::Secure);
        CHECK(BadgeOf("http://a.example/x") == SiteBadge::NotSecure);
        CHECK(BadgeOf("about:history") == SiteBadge::Internal);
        CHECK(BadgeOf("") == SiteBadge::None);
        AddressShown a = ShowAddress("https://www.example.com/path/page.html?x=1#top");
        CHECK(a.host == "www.example.com" && a.rest == "/path/page.html?x=1#top" && a.lead.empty());
        a = ShowAddress("https://example.com/");
        CHECK(a.host == "example.com" && a.rest.empty());   // the lone "/" is left out
        a = ShowAddress("http://user@example.com:8080/a");
        CHECK(a.lead == "http://" && a.host == "example.com:8080" && a.rest == "/a");   // http is spelled out (not secure)
        a = ShowAddress("about:history");
        CHECK(a.host == "about:history" && a.rest.empty());

        LoadErrorInfo e = DescribeLoadError("https://a.example/", "Could not reach the server. Check your connection and try again.");
        CHECK(e.kind == LoadErrorInfo::NoConnection && e.title.find("reached") != std::string::npos && !e.advice.empty());
        e = DescribeLoadError("https://a.example/x", "The server answered 404.");
        CHECK(e.kind == LoadErrorInfo::NotFound && e.code == "404");
        e = DescribeLoadError("https://a.example/x", "The server answered 503.");
        CHECK(e.kind == LoadErrorInfo::ServerError && e.code == "503");
        e = DescribeLoadError("ftp://a.example/", "Only http and https addresses can be opened here.");
        CHECK(e.kind == LoadErrorInfo::Blocked);
        e = DescribeLoadError("about:config", "This page is not available here.");
        CHECK(e.kind == LoadErrorInfo::Blocked);
        CHECK(DescribeLoadError("https://a/", "The page is too large.").kind == LoadErrorInfo::TooLarge);
        CHECK(DescribeLoadError("https://a/", "Loading was stopped.").kind == LoadErrorInfo::Stopped);
        e = DescribeLoadError("https://a/", "something odd");
        CHECK(e.kind == LoadErrorInfo::Other && !e.title.empty() && e.code == "something odd");
    }

    // ---- round 2 of the look: the new-tab page has a search box that sends to the search address, and shortcut tiles ----
    {
        Profile p;
        p.bookmarks.Add("https://a.example/", "A site");
        p.history.Add("https://h.example/page", "H page", 10);
        HtmlDocument nt = ParseHtml(InternalPage("about:newtab", p, {}));
        const Node* form = nullptr; const Node* q = nullptr;
        int tiles = 0;
        std::function<void(const Node&)> walk = [&](const Node& n) {
            if (n.name == "form") form = &n;
            if (n.name == "input" && n.Attr("name") && *n.Attr("name") == "q") q = &n;
            if (const std::string* c = n.Attr("class"); c && *c == "tile") ++tiles;
            for (const auto& k : n.children) walk(*k);
        };
        walk(*nt.root);
        CHECK(form && form->Attr("action") && *form->Attr("action") == "https://www.google.com/search");
        CHECK(q && q->Attr("placeholder") && q->Attr("placeholder")->find("Search Google") != std::string::npos);
        CHECK(tiles == 2);   // the bookmark and the page from the history
        // a search address with more than the terms keeps its other parts as hidden fields; none: no search box
        p.settings.searchUrl = "https://search.example/find?lang=en&q=%s";
        const std::string other = InternalPage("about:newtab", p, {});
        CHECK(other.find("action=\"https://search.example/find\"") != std::string::npos &&
              other.find("type=hidden name=\"lang\" value=\"en\"") != std::string::npos);
        p.settings.searchUrl.clear();
        CHECK(InternalPage("about:newtab", p, {}).find("<form") == std::string::npos);
    }
    // ---- the host's round 3: the profile's cookies and localStorage (SiteData), kept across a restart and shared by two
    // programs on the same folder (the browser program and the launcher's Browser page) ----
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "okay_browser_sitedata_test";
        std::error_code ec;
        fs::remove_all(dir, ec);
        const std::string folder = dir.u8string() + "/";
        const std::int64_t now = 1767225600;   // 2026-01-01
        {
            SiteData a(folder);
            a.Load(now);
            CHECK(a.Cookies().Set("https://shop.test/", "sid=abc; Max-Age=3600; Path=/", now));
            CHECK(a.Cookies().Set("https://shop.test/", "tmp=1; Path=/", now));   // a session cookie: not kept
            CHECK(a.LocalStorage().Area("https://shop.test").Set("cart", "3 apples", WebStorage::kMaxBytesPerOrigin));
            a.LocalStorage().MarkChanged();
            a.Pump(now);
            CHECK(fs::exists(fs::u8path(a.CookiesPath())) && fs::exists(fs::u8path(a.LocalStoragePath())));
        }
        {   // a restart: the persistent cookie and the stored value are back, the session cookie is gone
            SiteData b(folder);
            b.Load(now + 10);
            CHECK(b.Cookies().Header("https://shop.test/", now + 10) == "sid=abc");
            std::string v;
            const StorageArea* area = b.LocalStorage().Find("https://shop.test");
            CHECK(area && area->Get("cart", v) && v == "3 apples");
        }
        {   // two programs at once: one saves, the other (with nothing unsaved) takes it in and keeps its session cookies
            SiteData prog(folder), launcher(folder);
            prog.Load(now);
            launcher.Load(now);
            launcher.Cookies().Set("https://shop.test/", "session=s1; Path=/", now);
            prog.Cookies().Set("https://news.test/", "lang=fi; Max-Age=600", now);
            prog.LocalStorage().Area("https://news.test").Set("seen", "42", WebStorage::kMaxBytesPerOrigin);
            prog.LocalStorage().MarkChanged();
            prog.Pump(now);
            launcher.Pump(now);
            CHECK(launcher.Cookies().Header("https://news.test/", now) == "lang=fi");
            CHECK(launcher.Cookies().Header("https://shop.test/", now).find("session=s1") != std::string::npos);
            std::string v;
            CHECK(launcher.LocalStorage().Find("https://news.test") && launcher.LocalStorage().Find("https://news.test")->Get("seen", v) && v == "42");
            // both change before either reads the other: nothing is lost (the later writer adds what only the file has)
            launcher.Cookies().Set("https://shop.test/", "fromLauncher=1; Max-Age=600", now);
            prog.Cookies().Set("https://shop.test/", "fromProgram=1; Max-Age=600", now);
            launcher.Pump(now);
            prog.Pump(now);
            launcher.Pump(now);
            for (SiteData* s : {&prog, &launcher}) {
                const std::string h = s->Cookies().Header("https://shop.test/", now);
                CHECK(h.find("fromLauncher=1") != std::string::npos && h.find("fromProgram=1") != std::string::npos && h.find("sid=abc") != std::string::npos);
            }
        }
        {   // a damaged file is an empty store (and is written over at the next change), never a crash
            SiteData d(folder);
            std::FILE* f = std::fopen(d.CookiesPath().c_str(), "wb");
            if (f) { std::fputs("{not json", f); std::fclose(f); }
            d.Load(now);
            CHECK(d.Cookies().Count() == 0);
        }
        fs::remove_all(dir, ec);
    }

    // the headers of an answer, for the host's fetches: every Set-Cookie, the reason phrase, the last answer after redirects
    {
        const std::string head = "HTTP/1.1 302 Found\r\nLocation: /b\r\nSet-Cookie: hop=1\r\n\r\n"
                                 "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nSet-Cookie: a=1; Path=/\r\nset-cookie: b=2\r\n\r\n";
        std::string reason;
        const auto lines = HeaderLines(head, &reason);
        CHECK(reason == "Not Found" && lines.size() == 3);
        CHECK(lines.size() == 3 && lines[1].first == "Set-Cookie" && lines[1].second == "a=1; Path=/" && lines[2].second == "b=2");
        CHECK(HeaderValue(head, "content-type") == "text/html" && HeaderValue(head, "Location").empty());
    }
    TEST_MAIN_RESULT();
}
