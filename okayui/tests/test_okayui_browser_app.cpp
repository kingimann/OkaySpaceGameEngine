// Browser: the whole browser (shared/BrowserApp.hpp), the one the browser program draws over its window and the launcher's
// Browser page (sidebar > Browser) draws in its page area. Driven headless at an offset rectangle, as the launcher places it:
// three tabs with pages delivered by the test (no network), the golden picture of the launcher's page area with them, a click
// on another tab, Ctrl+T / Ctrl+W, Ctrl+D (a bookmark) and Ctrl+= (zoom) kept in the profile, a second browser on the same
// profile file seeing them (the launcher and the program share one profile.json), an answer that is a file opening the "Save
// this file?" prompt (Esc closes it), and the switch off (nothing is loaded, a line says why). Round 2 of the look: the address
// bar's suggestions (Down + Enter switches to an open tab), key scrolling that glides (End, Home, Space, Shift+Space), Tab focus
// with a ring and Enter on the focused link, the error page, dragging a tab; goldens of each. OKAY_SHOT_DIR=<folder> also
// writes pictures for a person to look at (never committed).
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "BrowserApp.hpp"
#include "BrowserView.hpp"
#include "okay/Core/StringUtil.hpp"

#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#if !defined(_WIN32)   // the fake web site of the host's round 3 test (a test only; never built into a Windows program)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#endif

using namespace okayuitest;
namespace UI = OkayUI;
namespace br = okay::browser;
namespace fs = std::filesystem;

static const char* kNotes =
    "<!DOCTYPE html><html><head><title>Patch notes</title><style>"
    "body { margin: 16px; background: #f4f6fa; color: #1c2230; font-family: sans-serif }"
    "h1 { color: #3a7bd5; margin: 0 0 8px 0 } .card { border: 1px solid #c8d0de; background: #fff; padding: 10px; margin: 8px 0 }"
    ".row { display: flex; gap: 10px } .row div { flex: 1; background: #e3ebf8; padding: 8px }"
    "</style></head><body>"
    "<h1>OkaySpace 0.9 patch notes</h1>"
    "<p>The launcher has a <b>Browser</b> page now. <a href='/more.html'>Read more</a> about it.</p>"
    "<div class=row><div>Tabs</div><div>Bookmarks</div><div>Downloads</div></div>"
    "<div class=card>Games on a page run natively with the OkaySpace player.</div>"
    "<ul><li>Google search from the address bar</li><li>Find on the page, zoom per site</li></ul>"
    "</body></html>";

// The tab's waiting fetch answered by the test (the page engine's own entry point; no worker, no network).
static void Deliver(br::BrowserApp& app, int id, const std::string& html) {
    br::BrowserTab* t = app.TabById(id);
    if (!t) return;
    for (const std::string& u : t->TakeFetches()) t->DeliverPage(u, true, 200, html);
}


#if !defined(_WIN32)
// A fake web site on 127.0.0.1 for the host's round 3 (never a real site): HTTP/1.1, one request per connection. `route` makes
// the whole answer (status line, headers and body) from the request; every request's head and body is kept for the checks.
struct FakeSite {
    struct Request { std::string method, path, head, body; };
    std::function<std::string(const Request&)> route;
    int port = 0;
    int listener = -1;
    std::atomic<bool> quit{false};
    std::thread loop;
    std::mutex lock;
    std::vector<Request> seen;

    bool Start() {
        listener = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listener < 0) return false;
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        socklen_t n = sizeof a;
        if (::bind(listener, (sockaddr*)&a, sizeof a) != 0 || ::listen(listener, 16) != 0 || ::getsockname(listener, (sockaddr*)&a, &n) != 0) return false;
        port = ntohs(a.sin_port);
        loop = std::thread([this] {
            while (!quit) {
                const int c = ::accept(listener, nullptr, nullptr);
                if (c < 0) continue;
                if (!quit) Answer(c);
                ::close(c);
            }
        });
        return true;
    }
    void Answer(int c) {
        std::string in;
        char buf[4096];
        std::size_t headEnd = std::string::npos;
        while (headEnd == std::string::npos) {
            const ssize_t got = ::recv(c, buf, sizeof buf, 0);
            if (got <= 0) return;
            in.append(buf, (std::size_t)got);
            headEnd = in.find("\r\n\r\n");
        }
        Request r;
        r.head = in.substr(0, headEnd);
        const std::size_t sp = r.head.find(' '), sp2 = r.head.find(' ', sp + 1);
        r.method = r.head.substr(0, sp);
        r.path = r.head.substr(sp + 1, sp2 - sp - 1);
        std::size_t want = 0;
        const std::string lower = okay::str::LowerAscii(r.head);
        const std::size_t cl = lower.find("\r\ncontent-length:");
        if (cl != std::string::npos) want = (std::size_t)std::strtoul(r.head.c_str() + cl + 17, nullptr, 10);
        r.body = in.substr(headEnd + 4);
        while (r.body.size() < want) {
            const ssize_t got = ::recv(c, buf, sizeof buf, 0);
            if (got <= 0) break;
            r.body.append(buf, (std::size_t)got);
        }
        const std::string out = route(r);
        { std::lock_guard<std::mutex> g(lock); seen.push_back(r); }
        for (std::size_t sent = 0; sent < out.size();) {
            const ssize_t k = ::send(c, out.data() + sent, out.size() - sent, MSG_NOSIGNAL);
            if (k <= 0) break;
            sent += (std::size_t)k;
        }
    }
    // The Cookie header of the last request for `path` ("<none>" when it had none, "<not asked>" when there was none).
    std::string CookieOf(const std::string& path) {
        std::lock_guard<std::mutex> g(lock);
        for (std::size_t i = seen.size(); i-- > 0;) {
            if (seen[i].path != path) continue;
            const std::string lower = okay::str::LowerAscii(seen[i].head);
            const std::size_t at = lower.find("\r\ncookie:");
            if (at == std::string::npos) return "<none>";
            const std::size_t e = seen[i].head.find("\r\n", at + 2);
            return okay::str::Trim(seen[i].head.substr(at + 9, e - at - 9));
        }
        return "<not asked>";
    }
    bool Saw(const std::function<bool(const Request&)>& match) {
        std::lock_guard<std::mutex> g(lock);
        for (const Request& r : seen) if (match(r)) return true;
        return false;
    }
    void Stop() {
        quit = true;
        ::shutdown(listener, SHUT_RDWR);
        ::close(listener);
        // wake accept() with one last connection, then wait for the thread
        const int w = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = htons((unsigned short)port);
        ::connect(w, (sockaddr*)&a, sizeof a);
        ::close(w);
        if (loop.joinable()) loop.join();
    }
};

static std::string Html(const std::string& body, const std::string& extra = std::string(), const char* status = "200 OK") {
    return std::string("HTTP/1.1 ") + status + "\r\nContent-Type: text/html\r\nContent-Length: " + std::to_string(body.size()) +
           "\r\n" + extra + "Connection: close\r\n\r\n" + body;
}
#endif

int main() {
    RUN_SUITE("okayui browser app (the launcher's Browser page and the browser program)");
    Reset();
    UI::UseLauncherLook(true);
    UI::Style().caretBlink = false;

    const fs::path dir = fs::temp_directory_path() / "okay_browser_app_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    br::BrowserApp::Config cfg;
    cfg.profilePath = (dir / "profile.json").u8string();
    cfg.runner.dataRoot = dir.u8string() + "/";
    cfg.idPrefix = "##test_browser";

    // the launcher's page area: right of a 180 px sidebar, under a 40 px title row
    const float X = 180, Y = 40, W = 720, H = 480;
    Sim sim(X + W, Y + H);
    {
        br::BrowserApp app(cfg);
        const int a = app.OpenUrl("https://notes.okayspace.test/notes.html");
        const int b = app.OpenUrl("https://example.test/", false);
        const int c = app.OpenUrl("https://games.example.test/index.html", false);
        CHECK(app.TabCount() == 3 && app.ActiveId() == a);
        Deliver(app, a, kNotes);
        Deliver(app, b, "<html><head><title>Example Domain</title></head><body><h1>Example Domain</h1><p>For examples.</p></body></html>");
        Deliver(app, c, "<html><head><title>Space Run - play</title></head><body><okay-game src='space.okaypack.gz' width=300 height=120></okay-game></body></html>");
        auto ui = [&] { app.Frame(X, Y, W, H); };
        sim.Frames(4, ui);
        CHECK(app.ActiveTab().Title() == "Patch notes");
        CHECK(app.WindowTitle().find("OkaySpace Browser") != std::string::npos);
        {
            std::string why;
            const bool same = MatchesGolden("browser_app", sim.Draw(18, 20, 26), &why);
            if (!same) std::cerr << "  golden browser_app: " << why << "\n";
            CHECK(same);
        }

        // a click on the second tab (the tab bar starts at X + 6, three tabs of 220 px)
        sim.Click(X + 6 + 220 + 60, Y + 18, ui);
        CHECK(app.ActiveId() == b);
        // Ctrl+T: a new tab (the new-tab page), Ctrl+W closes it
        auto chord = [&](UI::KeyId k, bool shift = false) {
            sim.ctrl = true; sim.shift = shift; sim.Hold(k, true); sim.Frame(ui);
            sim.Hold(k, false); sim.Frame(ui); sim.ctrl = false; sim.shift = false;
        };
        chord(UI::KeyId_T);
        CHECK(app.TabCount() == 4 && app.ActiveTab().Url() == "about:newtab");
        sim.Frames(2, ui);   // our own new-tab page is made by the host (no network)
        CHECK(app.ActiveTab().GetState() == br::BrowserTab::State::Ready);
        chord(UI::KeyId_W);
        CHECK(app.TabCount() == 3);
        // back to the notes: Ctrl+1; Ctrl+D bookmarks it, Ctrl+= zooms its site
        chord(UI::KeyId_1);
        CHECK(app.ActiveId() == a);
        chord(UI::KeyId_D);
        CHECK(app.GetProfile().bookmarks.Has("https://notes.okayspace.test/notes.html"));
        chord(UI::KeyId_Equal);
        CHECK(app.GetProfile().zoom.Get("https://notes.okayspace.test") > 1.0f);
        app.Save();

        // the other host (the browser program, or the launcher) on the same profile file sees both
        {
            br::BrowserApp other(cfg);
            CHECK(other.GetProfile().bookmarks.Has("https://notes.okayspace.test/notes.html"));
            CHECK(other.GetProfile().zoom.Get("https://notes.okayspace.test") > 1.0f);
        }

        // an answer that is a file: the "Save this file?" prompt, Esc closes it and nothing is downloaded
        br::BrowserTab& t = app.ActiveTab();
        t.Activate("/setup.zip");
        for (const std::string& u : t.TakeFetches()) t.DeliverDownload(u, "application/zip", "setup.zip", 2048);
        sim.Frames(2, ui);
        CHECK(app.DownloadPromptOpen());
        sim.Hold(UI::KeyId_Escape, true); sim.Frame(ui); sim.Hold(UI::KeyId_Escape, false); sim.Frame(ui);
        CHECK(!app.DownloadPromptOpen() && app.Downloads().empty());

        auto tap = [&](UI::KeyId k, bool shift = false) {
            sim.shift = shift; sim.Hold(k, true); sim.Frame(ui); sim.Hold(k, false); sim.Frame(ui); sim.shift = false;
        };
        auto golden = [&](const char* name) {
            std::string why;
            const bool same = MatchesGolden(name, sim.Draw(18, 20, 26), &why);
            if (!same) std::cerr << "  golden " << name << ": " << why << "\n";
            CHECK(same);
        };
        sim.Move(5, 5);   // the pointer away from the browser (no hover, no tooltip)

        // ---- round 2: the address bar suggests while typing (the open tab "Example Domain" for "exam"); Down picks it, Enter
        // switches to that tab instead of searching ----
        chord(UI::KeyId_L);
        sim.Frame(ui);
        sim.Type("exam");
        sim.Frames(3, ui);
        golden("browser_app_suggest");
        tap(UI::KeyId_Down);
        sim.Press(Key_Enter);
        sim.Frames(2, ui);
        CHECK(app.ActiveId() == b);
        CHECK(app.TabById(a)->Url() == "https://notes.okayspace.test/notes.html");   // nothing was searched in the tab typed in

        // ---- round 2: the keyboard scrolls the page (End, Home, Space glide there) and Tab moves between the links; Enter
        // follows the focused one ----
        const int d = app.OpenUrl("https://long.example.test/");
        std::string longPage = "<html><head><title>Long</title></head><body style='margin:0'><p><a href='/one'>one</a> <a href='/two'>two</a></p>";
        for (int i = 0; i < 80; ++i) longPage += "<p>Line " + std::to_string(i) + "</p>";
        Deliver(app, d, longPage + "</body></html>");
        sim.Frames(3, ui);
        br::BrowserTab& lt = *app.TabById(d);
        CHECK(lt.GetState() == br::BrowserTab::State::Ready && lt.ScrollY() == 0.0f);
        tap(UI::KeyId_End);
        sim.Frames(30, ui);
        const float bottom = lt.ScrollY();
        CHECK(bottom > 1000.0f);
        tap(UI::KeyId_Home);
        sim.Frames(30, ui);
        CHECK(lt.ScrollY() == 0.0f);
        tap(UI::KeyId_Space);
        sim.Frame(ui);
        const float gliding = lt.ScrollY();
        sim.Frames(30, ui);
        CHECK(lt.ScrollY() > 200.0f && lt.ScrollY() < bottom && gliding > 0.0f && gliding < lt.ScrollY());   // it glides there
        tap(UI::KeyId_Space, true);   // Shift+Space: back up
        sim.Frames(30, ui);
        CHECK(lt.ScrollY() == 0.0f);
        tap(UI::KeyId_Tab);
        tap(UI::KeyId_Tab);
        golden("browser_app_focus");
        sim.Hold(UI::KeyId_Enter, true); sim.Frame(ui); sim.Hold(UI::KeyId_Enter, false);
        CHECK(lt.Url() == "https://long.example.test/two");
        for (const std::string& u : lt.TakeFetches()) lt.DeliverPage(u, true, 200, "<p>two</p>");   // answered here: no network
        sim.Frame(ui);
        tap(UI::KeyId_Tab, true);   // Shift+Tab on the next page (no links yet: nothing happens, nothing breaks)

        // ---- round 2: a site that cannot be reached shows a friendly page that says so, with Try again ----
        const int e = app.OpenUrl("https://down.example.test/");
        if (br::BrowserTab* et = app.TabById(e))
            for (const std::string& u : et->TakeFetches()) et->DeliverPage(u, false, 0, std::string(), "Could not reach the server. Check your connection and try again.");
        sim.Frames(3, ui);
        CHECK(app.ActiveTab().GetState() == br::BrowserTab::State::Error);
        golden("browser_app_error");

        // ---- round 2: dragging the last tab to the front of the strip reorders the tabs ----
        {
            const std::vector<int> before = app.TabIds();
            const float n = (float)before.size(), tw = std::max(48.0f, std::min(240.0f, (W - 60.0f) / n));
            sim.Move(X + 8 + tw * (n - 1.0f) + tw * 0.3f, Y + 22);
            sim.Frame(ui);
            sim.Down();
            sim.Frame(ui);
            for (float mx = X + 8 + tw * (n - 1.0f); mx > X + 10; mx -= 25.0f) { sim.Move(mx, Y + 22); sim.Frame(ui); }
            sim.Up();
            sim.Frame(ui);
            CHECK(app.TabIds().size() == before.size() && app.TabIds().front() == before.back());
            CHECK(app.ActiveId() == before.back());
        }
        app.CloseAll();
    }

#if !defined(_WIN32)
    // ---- the host's round 3, against a fake site on 127.0.0.1 through the tab host's real fetches (no real site): cookies on
    // every load (also across a redirect) and after a restart, localStorage after a restart, a page's fetch() answered, a
    // cancelled keydown that types nothing while Ctrl+T still works, alert() as a dialog over the page, the Back history
    // list, and the wheel's glide ----
    {
        ::setenv("NO_PROXY", "127.0.0.1", 1);
        for (const char* v : {"HTTP_PROXY", "http_proxy", "HTTPS_PROXY", "https_proxy", "ALL_PROXY", "all_proxy"}) ::unsetenv(v);
        FakeSite site;
        site.route = [](const FakeSite::Request& r) -> std::string {
            if (r.path == "/login") return Html("<title>Signed in</title><p>Welcome</p>", "Set-Cookie: sid=s123; Max-Age=3600; Path=/\r\n");
            if (r.path == "/hop") return Html("", "Location: /home\r\nSet-Cookie: hop=1; Max-Age=600; Path=/\r\n", "302 Found");
            if (r.path == "/home") return Html("<title>Home</title><p>Home page</p>");
            if (r.path == "/store") return Html("<title>Store</title><script>localStorage.setItem('cart', 'three apples')</script>");
            if (r.path == "/read") return Html("<title>Read</title><script>document.title = 'cart: ' + localStorage.getItem('cart')</script>");
            if (r.path == "/fetch")
                return Html("<title>Fetch</title><script>fetch('/api/n').then(r => r.text()).then(t => { document.title = 'n=' + t; });"
                            "fetch('/api/echo', {method: 'POST', headers: {'X-Mode': 'test'}, body: 'hello'}).then(r => r.text())"
                            ".then(t => console.log('echo ' + t));</script>");
            if (r.path == "/api/n") return "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 2\r\nConnection: close\r\n\r\n41";
            if (r.path == "/api/echo") {
                const std::string b = r.method + " " + r.body;
                return "HTTP/1.1 201 Created\r\nContent-Type: text/plain\r\nContent-Length: " + std::to_string(b.size()) + "\r\nConnection: close\r\n\r\n" + b;
            }
            if (r.path == "/keys")
                return Html("<title>Keys</title><style>body{margin:20px}</style><input id=f name=f>"
                            "<script>document.addEventListener('keydown', e => { console.log('down ' + e.key + (e.ctrlKey ? ' ctrl' : ''));"
                            " if (e.key === 'x' || e.ctrlKey) e.preventDefault(); });</script>");
            if (r.path == "/alert") return Html("<title>Alert</title><p>A page that says something.</p><script>alert('Your game was saved.')</script>");
            if (r.path == "/h1" || r.path == "/h2" || r.path == "/h3") return Html("<title>Page " + r.path.substr(2) + "</title><p>" + r.path + "</p>");
            if (r.path == "/long") {
                std::string b = "<title>Long</title><body style='margin:0'>";
                for (int i = 0; i < 120; ++i) b += "<p>Line " + std::to_string(i) + "</p>";
                return Html(b);
            }
            return Html("<p>missing</p>", std::string(), "404 Not Found");
        };
        CHECK(site.Start());
        const std::string base = "http://127.0.0.1:" + std::to_string(site.port);
        const fs::path hdir = fs::temp_directory_path() / "okay_browser_host_test";
        fs::remove_all(hdir, ec);
        fs::create_directories(hdir, ec);
        br::BrowserApp::Config hc;
        hc.profilePath = (hdir / "profile.json").u8string();
        hc.runner.dataRoot = hdir.u8string() + "/";
        hc.idPrefix = "##host_browser";
        Reset();
        UI::UseLauncherLook(true);
        UI::Style().caretBlink = false;
        Sim hs(X + W, Y + H);
        std::unique_ptr<br::BrowserApp> app(new br::BrowserApp(hc));
        auto ui = [&] { app->Frame(X, Y, W, H); };
        // frames until `done` (the fetches run on the host's worker threads), at most about five seconds
        auto until = [&](const std::function<bool()>& done) {
            for (int i = 0; i < 1000 && !done(); ++i) { hs.Frame(ui); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
            return done();
        };
        auto ready = [&](const std::string& title) {
            return until([&] { return app->ActiveTab().GetState() == br::BrowserTab::State::Ready && app->ActiveTab().Title() == title; });
        };
        auto go = [&](const std::string& path) { app->ActiveTab().Navigate(base + path); };

        // cookies set by one page are sent on the next load, and every hop of a redirect gets its own
        app->OpenUrl(base + "/login");
        CHECK(ready("Signed in"));
        CHECK(site.CookieOf("/login") == "<none>");
        go("/home");
        CHECK(ready("Home"));
        CHECK(site.CookieOf("/home") == "sid=s123");
        go("/hop");
        CHECK(ready("Home"));
        CHECK(site.CookieOf("/hop") == "sid=s123");
        const std::string afterHop = site.CookieOf("/home");
        CHECK(afterHop.find("sid=s123") != std::string::npos && afterHop.find("hop=1") != std::string::npos);
        CHECK(app->ActiveTab().Url() == base + "/home");
        // localStorage written by a page
        go("/store");
        CHECK(ready("Store"));
        // a page's fetch() is sent (method, headers and body as given, with the site's cookies) and answered
        go("/fetch");
        CHECK(ready("n=41"));
        CHECK(until([&] { return app->ActiveTab().ConsoleCount() > 0; }));
        bool echoed = false;
        for (const br::ConsoleLine& l : app->ActiveTab().ConsoleLines()) if (l.text == "echo POST hello") echoed = true;
        CHECK(echoed);
        CHECK(site.Saw([](const FakeSite::Request& r) {
            return r.method == "POST" && r.path == "/api/echo" && r.body == "hello" && r.head.find("X-Mode: test") != std::string::npos;
        }));
        CHECK(site.CookieOf("/api/n") == "sid=s123; hop=1");

        // the Back history list: a right click on Back lists the earlier pages, nearest first; a click on one goes there
        go("/h1"); CHECK(ready("Page 1"));
        go("/h2"); CHECK(ready("Page 2"));
        go("/h3"); CHECK(ready("Page 3"));
        const float backX = X + 8 + 15, backY = Y + 40 + 8 + 15;
        hs.Move(backX, backY); hs.Frame(ui);
        hs.rdown = true; hs.Frame(ui); hs.rdown = false; hs.Frame(ui);
        hs.Move(X + 300, Y + 300); hs.Frames(2, ui);
        if (const char* shots = std::getenv("OKAY_SHOT_DIR"); shots && *shots)
            std::cerr << "  picture: " << WritePng(std::string(shots) + "/host_history_list.png", hs.Draw(18, 20, 26)) << "\n";
        // the menu's rows: Page 2, Page 1, Fetch ... (24 px each, from 4 px under the menu's top, which is 4 px under Back)
        const float rowsTop = Y + 40 + 8 + 30 + 4 + 4;
        hs.Click(X + 60, rowsTop + 24 + 12, ui);
        CHECK(ready("Page 1"));
        CHECK(app->ActiveTab().Url() == base + "/h1" && app->ActiveTab().HistoryIndex() + 2 == app->ActiveTab().HistoryEntries().size() - 1);
        // press and hold on Forward: the forward list (Page 2, Page 3); a click on Page 3 goes there
        const float fwdX = backX + 32;
        hs.Move(fwdX, backY); hs.Frame(ui);
        hs.Down();
        hs.Frames(40, ui);   // 40 frames at 60 a second: held longer than half a second
        hs.Up(); hs.Frame(ui);
        CHECK(app->ActiveTab().Url() == base + "/h1");   // letting go after the hold is not a click on Forward
        hs.Click(fwdX + 40, rowsTop + 24 + 12, ui);
        CHECK(ready("Page 3"));

        // alert(): a dialog over the page that says which site it came from; OK (Enter) closes it
        go("/alert");
        CHECK(ready("Alert"));
        CHECK(until([&] { return app->PageDialogOpen(); }));
        CHECK(app->PageDialogText() == "Your game was saved.");
        hs.Frames(2, ui);
        if (const char* shots = std::getenv("OKAY_SHOT_DIR"); shots && *shots)
            std::cerr << "  picture: " << WritePng(std::string(shots) + "/host_alert_dialog.png", hs.Draw(18, 20, 26)) << "\n";
        hs.Hold(UI::KeyId_Enter, true); hs.Frame(ui); hs.Hold(UI::KeyId_Enter, false); hs.Frame(ui);
        CHECK(!app->PageDialogOpen());

        // keys: the page cancels 'x' (preventDefault on keydown), so the field does not get it; 'y' is typed; Ctrl+T still opens
        // a tab and the page never hears of it
        go("/keys");
        CHECK(ready("Keys"));
        hs.Frames(2, ui);
        {
            br::BrowserTab& kt = app->ActiveTab();
            const br::LayoutResult& lay = kt.Layout(W - 16, okayuiscreens::OkayUIMeasure());
            CHECK(lay.controls.size() == 1);
            const float fx = X + (lay.controls.empty() ? 30.0f : lay.controls[0].x + 10.0f);
            const float fy = Y + 40 + 46 + 26 + 1 + (lay.controls.empty() ? 30.0f : lay.controls[0].y + lay.controls[0].h * 0.5f);
            hs.Click(fx, fy, ui);
            hs.Frame(ui);
            auto typeKey = [&](UI::KeyId k, const char* ch) {
                hs.Hold(k, true); hs.Type(ch); hs.Frame(ui); hs.Hold(k, false); hs.Frame(ui);
            };
            typeKey(UI::KeyId_X, "x");
            typeKey(UI::KeyId_Y, "y");
            typeKey(UI::KeyId_X, "x");
            const br::Node* f = lay.controls.empty() ? nullptr : lay.controls[0].node;
            CHECK(f && app->ActiveTab().Forms().Value(*f) == "y");
            bool sawX = false, sawY = false, sawCtrl = false;
            const std::size_t tabsBefore = app->TabCount();
            hs.ctrl = true; hs.Hold(UI::KeyId_T, true); hs.Frame(ui); hs.Hold(UI::KeyId_T, false); hs.Frame(ui); hs.ctrl = false;
            CHECK(app->TabCount() == tabsBefore + 1);
            for (const br::ConsoleLine& l : kt.ConsoleLines()) {
                if (l.text == "down x") sawX = true;
                if (l.text == "down y") sawY = true;
                if (l.text.find("ctrl") != std::string::npos) sawCtrl = true;
            }
            CHECK(sawX && sawY && !sawCtrl);
        }

        // the wheel glides: a notch moves the page a little in the first frame and the whole step a few frames later
        go("/long");
        CHECK(ready("Long"));
        hs.Move(X + W * 0.5f, Y + H * 0.6f);
        hs.Frames(2, ui);
        hs.wheel = -1.0f;
        hs.Frame(ui);
        const float first = app->ActiveTab().ScrollY();
        hs.Frames(30, ui);
        const float landed = app->ActiveTab().ScrollY();
        CHECK(first > 0.0f && first < landed && landed == 100.0f);
        if (first <= 0.0f || landed != 100.0f) std::cerr << "  wheel: first " << first << ", landed " << landed << "\n";

        // a restart on the same profile: the cookie is sent and localStorage is still there
        app.reset();
        app.reset(new br::BrowserApp(hc));
        app->OpenUrl(base + "/read");
        CHECK(ready("cart: three apples"));
        CHECK(site.CookieOf("/read") == "sid=s123; hop=1");
        app->CloseAll();
        app.reset();
        site.Stop();
        fs::remove_all(hdir, ec);
    }
#endif

    // the switch off: a tab exists but nothing is fetched, and the line says why
    {
        br::BrowserApp::Config off = cfg;
        off.switchedOff = true;
        br::BrowserApp app(off);
        app.OpenUrl("https://example.test/");
        sim.Frames(2, [&] { app.Frame(X, Y, W, H); });
        CHECK(app.Notice().find("switched off") != std::string::npos);
        CHECK(app.ActiveTab().GetState() == br::BrowserTab::State::Empty);
    }
    // OKAY_SHOT_DIR set: pictures of the browser for a person to look at (software render, not a real window; never committed)
    if (const char* shots = std::getenv("OKAY_SHOT_DIR"); shots && *shots) {
        Reset();
        UI::UseLauncherLook(true);
        UI::Style().caretBlink = false;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        const float SW = 1100, SH = 680;
        Sim ss(SW, SH);
        auto shot = [&](const std::string& name) {
            const std::string path = std::string(shots) + "/" + name + ".png";
            std::cerr << "  picture: " << path << (WritePng(path, ss.Draw(18, 20, 26)) ? "" : " (not written)") << "\n";
        };
        br::BrowserApp app(cfg);
        auto ui = [&] { app.Frame(0, 0, SW, SH); };
        const int a = app.OpenUrl("https://notes.okayspace.test/notes.html");
        const int b = app.OpenUrl("https://example.test/", false);
        const int c = app.OpenUrl("http://plain.example.test/", false);
        Deliver(app, a, kNotes);
        Deliver(app, b, "<html><head><title>Example Domain</title></head><body><h1>Example Domain</h1><p>For examples.</p></body></html>");
        if (br::BrowserTab* t = app.TabById(c)) for (const std::string& u : t->TakeFetches()) t->DeliverPage(u, false, 0, std::string(), "Could not reach the server. Check your connection and try again.");
        ss.Frames(4, ui);
        shot("1_page");
        {   // the pointer over the link "Read more": the address bubble at the bottom-left
            const unsigned before = PixelAt(ss.Draw(18, 20, 26), 6, (int)SH - 8);
            const br::LayoutResult& lay = app.ActiveTab().Layout(SW - 16, okayuiscreens::OkayUIMeasure());
            const float lx = lay.areas.empty() ? 200.0f : lay.areas[0].x + lay.areas[0].w * 0.5f;
            for (float y = 40; y < 300; y += 3) {
                ss.Move(lx, y); ss.Frame(ui);
                if (PixelAt(ss.Draw(18, 20, 26), 6, (int)SH - 8) != before) break;
            }
        }
        shot("2_hover");
        ss.ctrl = true; ss.Hold(UI::KeyId_T, true); ss.Frame(ui); ss.Hold(UI::KeyId_T, false); ss.Frame(ui); ss.ctrl = false;
        ss.Frames(3, ui);
        shot("4_newtab");
        ss.Type("exa"); ss.Frames(2, ui);
        shot("5_typing");
        ss.Hold(UI::KeyId_Escape, true); ss.Frame(ui); ss.Hold(UI::KeyId_Escape, false); ss.Frame(ui);
        ss.ctrl = true; ss.Hold(UI::KeyId_3, true); ss.Frame(ui); ss.Hold(UI::KeyId_3, false); ss.Frame(ui); ss.ctrl = false;
        ss.Frames(2, ui);
        shot("6_error");
        ss.Move(500, 20); ss.Frames(40, ui);
        shot("7_tab_hover");
        app.CloseAll();
    }
    fs::remove_all(dir, ec);
    UI::UseLauncherLook(false);
    TEST_MAIN_RESULT();
}
