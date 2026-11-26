// Site check (okay-sitecheck, docs/feature_map/17-launcher-platform.md 17.20; plan: docs/platform/browser_plan.md "Site check"):
// loads a list of real sites through our own browser exactly as the browser program does - the same tab host
// (shared/BrowserHost: our HTTP client, redirects hop by hop with cookies, the page's fetch / XMLHttpRequest, images through our
// decoders), the same page engine (okay/Browser: HTML, styles, layout, page scripts on our JavaScript engine with the same
// budgets) and the same OkayUI browser view drawn by the software painter - and writes, per site, a JSON report and a
// screenshot, then a summary that ranks the failure causes by how many sites each one hits.
//
//   okay-sitecheck --sites FILE --out DIR [--only N[,N...]] [--first N] [--delay-ms MS] [--timeout-s S] [--width W] [--height H]
//
// Each site gets a fresh profile (no cookies or storage carried over). Only the address in the list is opened: no link is
// followed, no form is sent, nothing is typed. Sites are loaded one at a time with a pause between them (--delay-ms, default
// 3000). What is not like the browser program: web fonts are fetched and checked but not drawn in the screenshot (OkayUI's
// font store keeps at most a few files for the program's life), and no games are run.
// Written for this tool from the browser's own interfaces (BrowserHost.hpp, Browser.hpp, BrowserView.hpp). Standard library,
// the engine and OkayUI only.
#include "SiteReport.hpp"

#include "BrowserApp.hpp"   // only its Config: the user agent the browser program sends
#include "BrowserHost.hpp"
#include "BrowserView.hpp"
#include "OkayUI_SoftPainter.hpp"
#include "okay/Browser/Css.hpp"
#include "okay/Browser/Profile.hpp"
#include "okay/Core/ImageWrite.hpp"
#include "okay/UI/OkayUI.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <time.h>
#endif

namespace br = okay::browser;
namespace S = okayuiscreens;
namespace UI = OkayUI;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace {

void PauseMs(int ms) {
#if defined(_WIN32)
    ::Sleep((DWORD)ms);
#else
    timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    ::nanosleep(&ts, nullptr);
#endif
}

double MsSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

// the CSS reporter is a plain function pointer (Css.hpp): it writes into the site being checked
sitecheck::SiteResult* g_current = nullptr;
void OnCssNotUsed(const char* what, const std::string& text) {
    if (!g_current) return;
    std::map<std::string, int>& m = std::string(what) == "selector" ? g_current->cssSelectors : g_current->cssProperties;
    if (m.size() < 2000 || m.count(text)) ++m[text.size() > 300 ? text.substr(0, 300) : text];
}

void CountNodes(const br::Node& n, sitecheck::SiteResult& r) {
    ++r.nodes;
    if (n.kind == br::NodeKind::Element) ++r.elements;
    if (n.kind == br::NodeKind::Text) r.textBytes += (int)n.data.size();
    for (const auto& c : n.children) CountNodes(*c, r);
}

std::string Slug(const std::string& url) {
    std::string s = url;
    for (const char* p : {"https://", "http://"}) if (s.rfind(p, 0) == 0) s = s.substr(std::string(p).size());
    std::string o;
    for (char c : s) o += (std::isalnum((unsigned char)c) || c == '-' || c == '.') ? c : '_';
    while (!o.empty() && (o.back() == '_' || o.back() == '.')) o.pop_back();
    return o.size() > 80 ? o.substr(0, 80) : o;
}

// The share of the page area (inside `page`) in its most common colour, on a coarse grid.
double BlankShare(const std::vector<std::uint8_t>& rgba, int w, int h, const UI::Rect& page) {
    std::map<std::uint32_t, int> counts;
    int total = 0;
    const int x0 = std::max(0, (int)page.x), y0 = std::max(0, (int)page.y);
    const int x1 = std::min(w, (int)(page.x + page.w)), y1 = std::min(h, (int)(page.y + page.h));
    for (int y = y0; y < y1; y += 4)
        for (int x = x0; x < x1; x += 4) {
            const std::uint8_t* q = &rgba[((std::size_t)y * (std::size_t)w + (std::size_t)x) * 4];
            ++counts[((std::uint32_t)q[0] << 16) | ((std::uint32_t)q[1] << 8) | q[2]];
            ++total;
        }
    int top = 0;
    for (const auto& c : counts) top = std::max(top, c.second);
    return total ? (double)top / total : 1.0;
}

struct Options {
    std::string sitesFile, outDir;
    std::vector<int> only;   // 1-based indexes into the list
    int first = 0;
    int delayMs = 3000;
    double timeoutS = 40.0;
    int width = 1280, height = 900;
};

class Checker {
public:
    explicit Checker(const Options& o) : m_o(o) {
        m_painter = UI::soft::CreateBitmapPainter(o.width, o.height);
        if (m_painter) m_drawer.reset(new UI::soft::FrameDrawer(*m_painter));
        UI::UseLauncherLook(true);
        UI::Style().caretBlink = false;
    }
    bool Ready() const { return m_painter != nullptr; }

    sitecheck::SiteResult Visit(const sitecheck::SiteLine& line) {
        sitecheck::SiteResult r;
        r.site = line.url;
        r.category = line.category;
        g_current = &r;
        br::SiteData data{std::string()};   // a fresh profile: nothing is kept between sites
        okay::platform::Painter* p = m_painter.get();
        br::TabHost::TextureFns tex;
        tex.make = [p](const std::uint8_t* rgba, int w, int h) -> void* {
            okay::platform::TextureDesc d;
            d.width = w; d.height = h; d.scale = okay::platform::ScaleMode::Linear;
            return p->CreateTexture(d, rgba);
        };
        tex.destroy = [p](void* t) { p->DestroyTexture(t); };
        std::unique_ptr<br::TabHost> host(new br::TabHost(tex, br::BrowserApp::Config().userAgent));   // the browser program's user agent
        host->Tab().SetAnyOrigin(true);
        host->SetSiteData(&data.Cookies(), &data.LocalStorage());
        Clock::time_point lastAnswer = Clock::now();
        host->SetFetchReporter([&](const br::TabHost::FetchReport& f) {
            sitecheck::Fetch x;
            x.url = f.url; x.finalUrl = f.finalUrl; x.kind = f.kind; x.reached = f.reached; x.status = f.status;
            x.netError = f.netError; x.note = f.note;
            x.contentType = br::HeaderValue(f.rawHead, "Content-Type");
            x.contentEncoding = br::HeaderValue(f.rawHead, "Content-Encoding");
            if (f.body) x.head = f.body->substr(0, 64);
            if (x.kind == "page") {
                r.status = (int)f.status;
                if (f.body) r.charset = sitecheck::CharsetOf(x.contentType, *f.body);
            }
            if (r.fetches.size() < 5000) r.fetches.push_back(std::move(x));
            lastAnswer = Clock::now();
        });
        const Clock::time_point start = Clock::now();
        if (!host->Open(line.url)) {
            r.pageError = "the tab refused the address";
            g_current = nullptr;
            return r;
        }
        bool readySeen = false;
        Clock::time_point readyAt;
        UI::Rect pageRect;
        for (;;) {
            host->Pump();
            pageRect = Frame(*host);
            const br::BrowserTab::State st = host->Tab().GetState();
            const double total = MsSince(start);
            if (st == br::BrowserTab::State::Ready && !readySeen) { readySeen = true; readyAt = Clock::now(); r.loadMs = total; }
            if (st == br::BrowserTab::State::Error) break;
            // settled: shown for 3 s and no answer for 2.5 s (timers, late images and fetch() calls get their turn)
            if (readySeen && MsSince(readyAt) > 3000.0 && MsSince(lastAnswer) > 2500.0) break;
            if (total > m_o.timeoutS * 1000.0) { r.timedOut = true; break; }
            PauseMs(16);
        }
        br::BrowserTab& tab = host->Tab();
        r.totalMs = MsSince(start);
        r.loaded = tab.GetState() == br::BrowserTab::State::Ready;
        r.finalUrl = tab.Url();
        r.title = tab.Title();
        r.pageError = tab.Error();
        if (const br::HtmlDocument* doc = tab.Document()) if (doc->root) CountNodes(*doc->root, r);
        for (const br::ConsoleLine& c : tab.ConsoleLines())
            if (c.level != br::ConsoleLine::Log) r.console.push_back(c.text);
        // the screenshot: two more frames so the last images and layout are in, then the frame as the view drew it
        for (int i = 0; i < 2; ++i) pageRect = Frame(*host);
        std::vector<std::uint8_t> rgba;
        int w = 0, h = 0;
        if (m_painter->ReadPixels(rgba, w, h) && w > 0 && h > 0) {
            r.blankShare = BlankShare(rgba, w, h, pageRect);
            fs::create_directories(fs::u8path(m_o.outDir) / "shots");
            const std::string png = (fs::u8path(m_o.outDir) / "shots" / (Slug(line.url) + ".png")).u8string();
            if (okay::ImageWrite::WritePNG(png, w, h, 4, rgba.data())) r.screenshot = png;
        }
        if (r.loaded) {
            S::OkayUIMeasure measure;
            const br::LayoutResult& L = tab.Layout(pageRect.w > 0 ? pageRect.w : (float)m_o.width, measure);
            r.drawCmds = (int)L.cmds.size();
            r.pageHeight = L.height;
        }
        host.reset();   // its textures go before the next site
        g_current = nullptr;
        return r;
    }

private:
    // one OkayUI frame of the browser view (the program's look: address field, no launcher buttons), drawn by the software painter
    UI::Rect Frame(br::TabHost& host) {
        UI::Input in;
        in.displayW = (float)m_o.width;
        in.displayH = (float)m_o.height;
        in.mouseX = in.mouseY = -100.0f;   // no pointer over the page (no hover styles)
        UI::BeginFrame(in);
        S::BrowserViewOptions opt;
        opt.address = &m_address;
        opt.hostButtons = false;
        opt.programButtons = true;
        opt.smoothScroll = false;
        const S::BrowserViewResult res = S::DrawBrowserView(host.Tab(), 0, 0, (float)m_o.width, (float)m_o.height,
            [&host](const std::string& src) { return (SDL_Texture*)host.ImageFor(src); }, opt);
        UI::EndFrameData();
        m_painter->SetDrawColor(255, 255, 255, 255);
        m_painter->Clear();
        m_drawer->Draw(UI::GetDrawData());
        m_painter->Present();
        return res.page;
    }

    Options m_o;
    std::unique_ptr<okay::platform::Painter> m_painter;
    std::unique_ptr<UI::soft::FrameDrawer> m_drawer;
    S::AddressField m_address;
};

int Usage(const char* self) {
    std::printf("Usage: %s --sites FILE --out DIR [--only N[,N...]] [--first N] [--delay-ms MS] [--timeout-s S] [--width W] [--height H]\n"
                "  Loads each site of FILE (one address per line) through our own browser and writes DIR/<site>.json,\n"
                "  DIR/shots/<site>.png, DIR/summary.md and DIR/summary.json. Public pages only: nothing is typed or sent.\n", self);
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "--sites") o.sitesFile = next();
        else if (a == "--out") o.outDir = next();
        else if (a == "--only") {
            std::stringstream ss(next());
            std::string part;
            while (std::getline(ss, part, ',')) if (!part.empty()) o.only.push_back(std::atoi(part.c_str()));
        } else if (a == "--first") o.first = std::atoi(next().c_str());
        else if (a == "--delay-ms") o.delayMs = std::atoi(next().c_str());
        else if (a == "--timeout-s") o.timeoutS = std::atof(next().c_str());
        else if (a == "--width") o.width = std::atoi(next().c_str());
        else if (a == "--height") o.height = std::atoi(next().c_str());
        else return Usage(argv[0]);
    }
    if (o.sitesFile.empty() || o.outDir.empty() || o.width < 320 || o.width > 4096 || o.height < 240 || o.height > 4096 ||
        o.timeoutS <= 0 || o.timeoutS > 600 || o.delayMs < 0 || o.delayMs > 600000)
        return Usage(argv[0]);
    std::ifstream f(fs::u8path(o.sitesFile), std::ios::binary);
    if (!f) { std::fprintf(stderr, "[sitecheck] cannot read %s\n", o.sitesFile.c_str()); return 1; }
    std::stringstream ss;
    ss << f.rdbuf();
    const std::vector<sitecheck::SiteLine> list = sitecheck::ReadSiteList(ss.str());
    std::error_code ec;
    fs::create_directories(fs::u8path(o.outDir), ec);
    if (ec) { std::fprintf(stderr, "[sitecheck] cannot make %s\n", o.outDir.c_str()); return 1; }

    br::SetCssNotUsedReporter(&OnCssNotUsed);
    Checker checker(o);
    if (!checker.Ready()) { std::fprintf(stderr, "[sitecheck] no software painter\n"); return 1; }
    std::vector<sitecheck::SiteResult> all;
    bool firstVisit = true;
    for (std::size_t i = 0; i < list.size(); ++i) {
        const int n = (int)i + 1;
        if (!o.only.empty() && std::find(o.only.begin(), o.only.end(), n) == o.only.end()) continue;
        if (o.first > 0 && n > o.first) break;
        if (!firstVisit) PauseMs(o.delayMs);   // polite: one site at a time, a pause between them
        firstVisit = false;
        std::fprintf(stderr, "[sitecheck] %d/%zu %s ...\n", n, list.size(), list[i].url.c_str());
        sitecheck::SiteResult r = checker.Visit(list[i]);
        const std::vector<sitecheck::Cause> causes = sitecheck::CausesOf(r);
        std::fprintf(stderr, "[sitecheck]   %s status %d, %s in %.0f ms, %d nodes, %zu fetches, %zu causes\n", r.loaded ? "shown" : "NOT shown",
                     r.status, r.finalUrl.c_str(), r.loadMs, r.nodes, r.fetches.size(), causes.size());
        std::ofstream(fs::u8path(o.outDir) / (Slug(r.site) + ".json"), std::ios::binary) << sitecheck::SiteJson(r);
        all.push_back(std::move(r));
    }
    br::SetCssNotUsedReporter(nullptr);
    const std::vector<sitecheck::Ranked> ranked = sitecheck::Rank(all);
    std::ofstream(fs::u8path(o.outDir) / "summary.md", std::ios::binary) << sitecheck::SummaryMarkdown(all, ranked);
    std::ofstream(fs::u8path(o.outDir) / "summary.json", std::ios::binary) << sitecheck::SummaryJson(all, ranked);
    std::fprintf(stderr, "[sitecheck] %zu sites; summary in %s\n", all.size(), (fs::u8path(o.outDir) / "summary.md").u8string().c_str());
    return 0;
}
