// Browser: the launcher's page for our own web pages (see BrowserPage.hpp). Fetching goes through the one HTTP seam
// (okay/Platform/NativeFetch.hpp) on a worker thread; images are read by our own decoders (okay/Core/ImageDecode.hpp).
#include "LauncherShared.hpp"
#include "BrowserPage.hpp"
#include "OkayThreads.hpp"
#include "PainterBridge.hpp"
#include "okay/Browser/Browser.hpp"
#include "okay/Core/ImageDecode.hpp"
#include "okay/Platform/Account/AccountService.hpp"
#include "okay/Platform/NativeFetch.hpp"
#ifdef OKAY_HAVE_OKAYUI
#include "BrowserView.hpp"
#endif

#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace okl {

namespace {

namespace br = okay::browser;

constexpr std::uint64_t kMaxImageBytes = 8u << 20;

struct FetchJob { std::string url; bool image = false; int generation = 0; };
struct FetchDone { std::string url; bool image = false; int generation = 0; bool ok = false; long status = 0; std::string body, error; };

br::BrowserTab g_tab;
bool g_open = false;
bool g_hostAvailable = false;
int g_generation = 0;                                // a new visit makes old answers stale
std::map<std::string, SDL_Texture*> g_textures;      // resolved image address -> texture

okay::thr::Mutex* g_mutex = nullptr;                 // guards the two lists and g_workerRunning
std::vector<FetchJob> g_jobs;
std::vector<FetchDone> g_done;
bool g_workerRunning = false;

bool SwitchOn() {
    const char* e = std::getenv("OKAY_BROWSER");   // Settings > Experimental > "Browser" sets it (shared/OwnSwitches.hpp)
    return e && e[0] == '1';
}

int Worker(void*) {
    while (true) {
        FetchJob job;
        okay::thr::LockMutex(g_mutex);
        if (g_jobs.empty()) { g_workerRunning = false; okay::thr::UnlockMutex(g_mutex); break; }
        job = g_jobs.front();
        g_jobs.erase(g_jobs.begin());
        okay::thr::UnlockMutex(g_mutex);
        FetchDone d;
        d.url = job.url; d.image = job.image; d.generation = job.generation;
        const std::uint64_t cap = job.image ? kMaxImageBytes : (std::uint64_t)br::BrowserTab::kMaxPageBytes;
        const okay::http::OwnOutcome o = okay::http::FetchPrefix(job.url, "OkaySpace Browser", cap, cap - 1, 15000, d.body, &d.status);
        d.ok = o == okay::http::OwnOutcome::Ok;
        if (!d.ok) {
            d.body.clear();
            if (o == okay::http::OwnOutcome::TooBig) d.error = "The page is too large.";
            else if (o == okay::http::OwnOutcome::NotHandled) d.error = "Could not reach the server. Check your connection and try again.";
        }
        okay::thr::LockMutex(g_mutex);
        g_done.push_back(std::move(d));
        okay::thr::UnlockMutex(g_mutex);
    }
    return 0;
}

void Queue(const std::vector<std::string>& urls) {
    if (urls.empty()) return;
    if (!g_mutex) g_mutex = okay::thr::CreateMutex();
    okay::thr::LockMutex(g_mutex);
    for (const std::string& u : urls) {
        const bool image = u != br::WithoutFragment(g_tab.Url());
        g_jobs.push_back({u, image, g_generation});
    }
    const bool start = !g_workerRunning;
    if (start) g_workerRunning = true;
    okay::thr::UnlockMutex(g_mutex);
    if (start) {
        if (okay::thr::Thread* t = okay::thr::CreateThread(Worker, "okay-browser", nullptr)) okay::thr::DetachThread(t);
        else { okay::thr::LockMutex(g_mutex); g_workerRunning = false; okay::thr::UnlockMutex(g_mutex); }
    }
}

void FreeTextures() {
    for (auto& t : g_textures) if (t.second) okay::pb::DestroyTexture(g_renderer, t.second);
    g_textures.clear();
}

// Hand the finished fetches to the tab (pages) or decode them (images).
void Pump() {
    Queue(g_tab.TakeFetches());
    if (!g_mutex) return;
    std::vector<FetchDone> done;
    okay::thr::LockMutex(g_mutex);
    done.swap(g_done);
    okay::thr::UnlockMutex(g_mutex);
    for (FetchDone& d : done) {
        if (d.generation != g_generation) continue;
        if (!d.image) { g_tab.DeliverPage(d.url, d.ok, (int)d.status, d.body, d.error); continue; }
        int w = 0, h = 0;
        if (d.ok) {
            okay::DecodedImage img;
            if (okay::DecodeImage((const std::uint8_t*)d.body.data(), d.body.size(), img) && img.width > 0 && img.height > 0 &&
                img.width <= 8192 && img.height <= 8192) {
                if (SDL_Texture* t = TextureFromRgba(g_renderer, img.rgba.data(), img.width, img.height)) {
                    auto old = g_textures.find(d.url);
                    if (old != g_textures.end() && old->second) okay::pb::DestroyTexture(g_renderer, old->second);
                    g_textures[d.url] = t;
                    w = img.width; h = img.height;
                }
            }
        }
        g_tab.DeliverImage(d.url, w, h);
    }
}

} // namespace

bool OpenPageLink(const std::string& url) {
    const std::string server = g_presenceAccount ? g_presenceAccount->ServerUrl() : std::string();
    if (!SwitchOn() || !g_hostAvailable || !IsServerPage(url, server)) { OpenExternal(url); return false; }
    FreeTextures();
    ++g_generation;
    if (!g_tab.Open(url)) { OpenExternal(url); return false; }
    g_open = true;
    return true;
}

bool BrowserPageOpen() { return g_open; }

void CloseBrowserPage() {
    if (!g_open) return;
    g_open = false;
    ++g_generation;
    FreeTextures();
    g_tab = br::BrowserTab();
}

void SetBrowserHostAvailable(bool okayFrame) {
    g_hostAvailable = okayFrame;
    if (!okayFrame && g_open) CloseBrowserPage();   // the OkayUI screens were switched off: nothing can draw the page
}

void DrawBrowserPage(float x, float y, float w, float h) {
#ifdef OKAY_HAVE_OKAYUI
    if (!g_open) return;
    Pump();
    const okayuiscreens::BrowserViewResult r = okayuiscreens::DrawBrowserView(g_tab, x, y, w, h, [](const std::string& src) -> SDL_Texture* {
        auto it = g_textures.find(br::WithoutFragment(g_tab.ResolveImage(src)));
        return it == g_textures.end() ? nullptr : it->second;
    });
    if (r.back) g_tab.GoBack();
    if (r.forward) g_tab.GoForward();
    if (r.reload) g_tab.Reload();
    if (r.openInSystemBrowser && !g_tab.Url().empty()) OpenExternal(g_tab.Url());
    if (r.linkClicked >= 0) {
        const br::LayoutResult& page = g_tab.Layout(r.page.w, okayuiscreens::OkayUIMeasure());
        if (r.linkClicked < (int)page.links.size()) {
            const std::string href = page.links[(std::size_t)r.linkClicked];
            const br::BrowserTab::Action a = g_tab.Activate(href);
            if (a.kind == br::BrowserTab::Action::External) OpenExternal(a.url);   // OpenExternal checks the address is shell-safe
            else if (a.kind == br::BrowserTab::Action::Refused) Toast("That link can't be opened", 2);
        }
    }
    if (r.close) { CloseBrowserPage(); return; }
    Queue(g_tab.TakeFetches());
#else
    (void)x; (void)y; (void)w; (void)h;
#endif
}

} // namespace okl
