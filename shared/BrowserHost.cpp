// Browser: the host half of one tab (see BrowserHost.hpp). Fetching goes through the one HTTP seam (okay/Platform/NativeFetch.hpp)
// on a worker thread; images are read by our own decoders (okay/Core/ImageDecode.hpp); games are downloaded, checked and unpacked
// on a second worker thread and run by the player through okaycli::Child (tools/okay-cli/Process.hpp).
#include "BrowserHost.hpp"

#include "OkayThreads.hpp"
#include "Process.hpp"
#include "okay/Browser/GamePack.hpp"
#include "okay/Core/ImageDecode.hpp"
#include "okay/Platform/NativeFetch.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>
#include <vector>

namespace okay::browser {

namespace fs = std::filesystem;

namespace {

struct FetchJob { std::string url; bool image = false; int generation = 0; bool post = false; std::string body; };
struct FetchDone {
    std::string url; bool image = false; int generation = 0; bool ok = false; long status = 0; std::string body, error;
    std::string contentType, disposition, finalUrl;   // pages: from the answer's head
    std::int64_t size = -1;                           // pages: Content-Length when the server said it
    bool tooBig = false;                              // pages: more than a page may be (then it is offered as a download)
};

constexpr std::size_t kMaxGameLog = 4096;   // the tail of what a game wrote to stderr, for the message when it fails

std::string Tail(const std::string& s, std::size_t n) { return s.size() <= n ? s : s.substr(s.size() - n); }

} // namespace

// Everything the worker threads touch, behind one mutex. The workers hold a shared_ptr, so a host that is destroyed while a
// download runs does not pull the state out from under them (the worker finishes and drops it).
struct TabHostShared {
    okay::thr::Mutex* mutex = okay::thr::CreateMutex();
    std::vector<FetchJob> jobs;
    std::vector<FetchDone> done;
    bool fetchRunning = false;
    // the game being prepared (one at a time)
    std::string gameUrl;
    TabHost::GameStatus game;
    bool gameReady = false;          // unpacked: the main thread starts the player
    okay::thr::AtomicInt cancel;     // the host went away: stop downloading
    ~TabHostShared() { okay::thr::DestroyMutex(mutex); }
    void Lock() { okay::thr::LockMutex(mutex); }
    void Unlock() { okay::thr::UnlockMutex(mutex); }
};

struct TabHost::Impl {
    std::shared_ptr<TabHostShared> sh = std::make_shared<TabHostShared>();
    int generation = 0;
    okaycli::Child child;
    std::string runningUrl;          // the game the child is running
    std::string childErr;
    std::string userAgent;
    std::chrono::steady_clock::time_point lastTick{};   // the page's clock (its timers) moves by the real time between pumps
    bool ticked = false;
};

namespace {

struct FetchArgs { std::shared_ptr<TabHostShared> sh; std::string userAgent; };

int FetchWorker(void* p) {
    std::unique_ptr<FetchArgs> args(static_cast<FetchArgs*>(p));
    TabHostShared& s = *args->sh;
    while (true) {
        FetchJob job;
        s.Lock();
        if (s.jobs.empty() || okay::thr::AtomicGet(&s.cancel)) { s.fetchRunning = false; s.Unlock(); break; }
        job = s.jobs.front();
        s.jobs.erase(s.jobs.begin());
        s.Unlock();
        FetchDone d;
        d.url = job.url; d.image = job.image; d.generation = job.generation;
        if (job.image) {
            const std::uint64_t cap = TabHost::kMaxImageBytes;
            const okay::http::OwnOutcome o = okay::http::FetchPrefix(job.url, args->userAgent, cap, cap - 1, 15000, d.body, &d.status);
            d.ok = o == okay::http::OwnOutcome::Ok;
            if (!d.ok) d.body.clear();
        } else {
            // A page: one request (GET, or POST with the form's body) whose head says what the answer is (Profile.hpp
            // UseOfAnswer: a page, text, an image, or a file to save).
            okay::http::RequestOptions ro;
            ro.maxBytes = BrowserTab::kMaxPageBytes;
            ro.maxRedirects = 10;
            ro.connectMs = 8000; ro.ioMs = 15000; ro.totalMs = 30000;
            ro.acceptGzip = true;
            std::vector<std::string> hdrs;
            if (!args->userAgent.empty()) hdrs.push_back("User-Agent: " + args->userAgent);
            hdrs.push_back("Accept: text/html,application/xhtml+xml,text/plain;q=0.9,*/*;q=0.8");
            if (job.post) hdrs.push_back("Content-Type: application/x-www-form-urlencoded");
            okay::http::ExResult r;
            if (okay::http::NativeAvailable())
                r = okay::http::RequestEx(job.post ? "POST" : "GET", job.url, okay::http::CleanHeaderLines(hdrs), job.body, std::string(),
                                          std::string(), ro);
            else r.error = okay::http::NoClientReason();
            d.status = r.status;
            d.contentType = HeaderValue(r.rawHead, "Content-Type");
            d.disposition = HeaderValue(r.rawHead, "Content-Disposition");
            d.finalUrl = r.finalUrl;
            const std::string len = HeaderValue(r.rawHead, "Content-Length");
            if (!len.empty() && len.find_first_not_of("0123456789") == std::string::npos && len.size() < 19) d.size = std::stoll(len);
            if (!r.reached) {
                d.tooBig = r.error.rfind("too-large", 0) == 0;
                d.error = d.tooBig ? "The page is too large." : "Could not reach the server. Check your connection and try again.";
            } else if (r.status >= 200 && r.status < 300) {
                d.ok = true;
                d.body = std::move(r.body);
            } else if (r.status >= 400 && !r.body.empty() && MediaType(d.contentType) == "text/html") {
                d.ok = true;   // the server's own error page is shown, like other browsers do
                d.body = std::move(r.body);
            }
        }
        s.Lock();
        s.done.push_back(std::move(d));
        s.Unlock();
    }
    return 0;
}

struct GameArgs { std::shared_ptr<TabHostShared> sh; std::string url, userAgent; GameLaunchPlan plan; };

// Download -> read and check -> unpack, all in the site's folder. The status is published under the mutex as it goes.
int GameWorker(void* p) {
    std::unique_ptr<GameArgs> a(static_cast<GameArgs*>(p));
    TabHostShared& s = *a->sh;
    auto set = [&](TabHost::GameStatus::Phase ph, const std::string& msg) {
        s.Lock();
        s.game.phase = ph;
        s.game.message = msg;
        s.Unlock();
    };
    std::error_code ec;
    fs::create_directories(fs::u8path(a->plan.siteDir) / "games", ec);
    fs::create_directories(fs::u8path(a->plan.saveDir), ec);
    if (ec) { set(TabHost::GameStatus::Failed, "Could not make the game's folder (" + ec.message() + ")."); return 0; }
    const GamePackLimits limits;
    okay::http::OwnDownloadSpec sp;
    sp.userAgent = a->userAgent;
    sp.maxBytes = limits.maxPackedBytes;
    sp.retries = 2;
    sp.connectMs = 20000;
    sp.onBytes = [&s](std::uint64_t n) { s.Lock(); s.game.bytes = n; s.Unlock(); };
    sp.cancelled = [&s] { return okay::thr::AtomicGet(&s.cancel) != 0; };
    const fs::path dl = fs::u8path(a->plan.downloadPath);
    const okay::http::OwnDownloadResult r = okay::http::FetchToFile(a->url, dl, sp);
    if (r.outcome != okay::http::OwnOutcome::Ok) {
        fs::remove(dl, ec);
        std::string why = "Could not download the game.";
        if (r.outcome == okay::http::OwnOutcome::TooBig) why = "The game is too large to download here.";
        else if (r.outcome == okay::http::OwnOutcome::HttpError) why = "The site has no such game (it answered " + std::to_string(r.status) + ").";
        else if (r.outcome == okay::http::OwnOutcome::Cancelled) why = "The download was stopped.";
        else if (r.outcome == okay::http::OwnOutcome::NotHandled) why = "Could not reach the site. Check your connection and try again.";
        set(TabHost::GameStatus::Failed, why);
        return 0;
    }
    set(TabHost::GameStatus::Unpacking, std::string());
    std::vector<std::uint8_t> bytes;
    {
        std::ifstream f(dl, std::ios::binary);
        bytes.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }
    fs::remove(dl, ec);
    GamePack pack;
    std::string why;
    if (!ReadGamePack(bytes.data(), bytes.size(), pack, why, limits)) { set(TabHost::GameStatus::Failed, why); return 0; }
    bytes.clear();
    bytes.shrink_to_fit();
    std::string dir = a->plan.gameDir;
    while (!dir.empty() && (dir.back() == '/' || dir.back() == '\\')) dir.pop_back();
    if (!UnpackGamePack(pack, dir, why)) { set(TabHost::GameStatus::Failed, why); return 0; }
    s.Lock();
    s.gameReady = true;
    s.Unlock();
    return 0;
}

} // namespace

TabHost::TabHost(TextureFns textures, std::string userAgent)
    : m_textures(std::move(textures)), m_userAgent(std::move(userAgent)), m_impl(new Impl()) {
    m_impl->userAgent = m_userAgent;
}

TabHost::~TabHost() {
    okay::thr::AtomicSet(&m_impl->sh->cancel, 1);   // workers stop at their next check and drop their share of the state
    Reset();
}

void TabHost::Reset() {
    ++m_impl->generation;
    if (m_textures.destroy) for (auto& t : m_images) if (t.second) m_textures.destroy(t.second);
    m_images.clear();
    const bool any = m_tab.AnyOrigin();
    m_tab = BrowserTab();
    m_tab.SetAnyOrigin(any);
    m_webConfig = WebExportConfig();
    m_configFor = nullptr;
}

bool TabHost::Open(const std::string& url) {
    Reset();
    return m_tab.Open(url);
}

void* TabHost::ImageFor(const std::string& src) const {
    auto it = m_images.find(WithoutFragment(m_tab.ResolveImage(src)));
    return it == m_images.end() ? nullptr : it->second;
}

void TabHost::RefreshPageInfo() {
    const HtmlDocument* doc = m_tab.Document();
    if (doc == m_configFor) return;
    m_configFor = doc;
    m_webConfig = doc ? FindWebExportConfig(*doc) : WebExportConfig();
}

std::string TabHost::GameUrlFor(const std::string& tag, const std::string& src) const {
    if (!CanRunGames() || m_tab.GetState() != BrowserTab::State::Ready) return std::string();
    return GamePackUrl(m_tab.Url(), tag, src, m_webConfig);
}

void TabHost::DeliverPageAnswerImpl(const std::string& url, bool ok, long status, std::string& body, const std::string& error,
                                    const std::string& contentType, const std::string& disposition, const std::string& finalUrl,
                                    std::int64_t size, bool tooBig) {
    std::string at = url;
    if (!finalUrl.empty() && WithoutFragment(finalUrl) != WithoutFragment(url)) {
        m_tab.Redirected(url, finalUrl);
        if (WithoutFragment(m_tab.Url()) == WithoutFragment(finalUrl)) at = finalUrl;
    }
    const AnswerUse use = UseOfAnswer(contentType, disposition, at);
    if (tooBig || (ok && use == AnswerUse::Download && m_tab.AnyOrigin())) {
        m_tab.DeliverDownload(at, MediaType(contentType), DownloadFileName(at, disposition, contentType), size);
        return;
    }
    if (ok && use == AnswerUse::PlainText) {
        const std::string ext = ExtensionOf(at);
        if (ext != "txt" && ext != "md") body = PlainTextPage(body);   // the tab wraps .txt and .md itself
    } else if (ok && use == AnswerUse::Image) {
        const Url u = ParseUrl(at);
        const std::size_t slash = u.path.rfind('/');
        const std::string name = u.path.substr(slash == std::string::npos ? 0 : slash + 1);
        body = "<!DOCTYPE html><html><head><title>" + HtmlEscape(name.empty() ? at : name) +
               "</title></head><body style=\"margin:0;background:#0e0e0e\"><img src=\"" + HtmlEscape(at) +
               "\" style=\"display:block;margin:0 auto\"></body></html>";
    }
    m_tab.DeliverPage(at, ok, (int)status, body, error);
}

void TabHost::Pump() {
    TabHostShared& s = *m_impl->sh;
    {   // the page's timers, and a location change its scripts asked for (before the fetches, so its load starts now)
        const auto nowT = std::chrono::steady_clock::now();
        double ms = m_impl->ticked ? std::chrono::duration<double, std::milli>(nowT - m_impl->lastTick).count() : 0.0;
        ms = std::max(0.0, std::min(ms, 1000.0));   // a pause (the window hidden) does not fire a second's worth of timers at once
        m_impl->lastTick = nowT;
        m_impl->ticked = true;
        m_tab.Tick(ms);
    }
    // pages and images
    std::vector<std::string> urls = m_tab.TakeFetches();
    // our own pages are made here, not fetched
    for (std::size_t i = 0; i < urls.size();) {
        if (!IsInternalPage(urls[i])) { ++i; continue; }
        const std::string u = urls[i];
        urls.erase(urls.begin() + (std::ptrdiff_t)i);
        if (m_internal) m_tab.DeliverPage(u, true, 200, m_internal(u));
        else m_tab.DeliverPage(u, false, 0, std::string(), "This page is not available here.");
    }
    if (!urls.empty()) {
        s.Lock();
        for (const std::string& u : urls) {
            FetchJob j;
            j.url = u;
            j.image = m_tab.KindOfFetch(u) != BrowserTab::FetchKind::Page;
            j.generation = m_impl->generation;
            j.post = !j.image && m_tab.PostBody(u, j.body);
            s.jobs.push_back(std::move(j));
        }
        const bool start = !s.fetchRunning;
        if (start) s.fetchRunning = true;
        s.Unlock();
        if (start) {
            if (okay::thr::Thread* t = okay::thr::CreateThread(FetchWorker, "okay-browser", new FetchArgs{m_impl->sh, m_userAgent})) okay::thr::DetachThread(t);
            else { s.Lock(); s.fetchRunning = false; s.Unlock(); }
        }
    }
    std::vector<FetchDone> done;
    s.Lock();
    done.swap(s.done);
    s.Unlock();
    for (FetchDone& d : done) {
        if (d.generation != m_impl->generation) continue;
        if (!d.image) {
            DeliverPageAnswerImpl(d.url, d.ok, d.status, d.body, d.error, d.contentType, d.disposition, d.finalUrl, d.size, d.tooBig);
            continue;
        }
        const BrowserTab::FetchKind kind = m_tab.KindOfFetch(d.url);
        if (kind == BrowserTab::FetchKind::Style) { m_tab.DeliverStyle(d.url, d.ok, d.body); continue; }
        if (kind == BrowserTab::FetchKind::Script) { m_tab.DeliverScript(d.url, d.ok, d.body); continue; }
        if (kind == BrowserTab::FetchKind::Font) {
            // only TrueType (0x00010000, "true") and OpenType ("OTTO") files: our rasterizer reads those (not WOFF)
            const std::string& b = d.body;
            const bool sfnt = b.size() > 12 && (b.compare(0, 4, std::string("\0\1\0\0", 4)) == 0 || b.compare(0, 4, "true") == 0 || b.compare(0, 4, "OTTO") == 0);
            if (d.ok && sfnt && m_fontLoader) if (const FontFace* f = m_tab.FontFor(d.url)) m_fontLoader(*f, std::vector<std::uint8_t>(b.begin(), b.end()));
            continue;
        }
        int w = 0, h = 0;
        if (d.ok && m_textures.make) {
            okay::DecodedImage img;
            if (okay::DecodeImage((const std::uint8_t*)d.body.data(), d.body.size(), img) && img.width > 0 && img.height > 0 &&
                img.width <= 8192 && img.height <= 8192) {
                if (void* t = m_textures.make(img.rgba.data(), img.width, img.height)) {
                    auto old = m_images.find(d.url);
                    if (old != m_images.end() && old->second && m_textures.destroy) m_textures.destroy(old->second);
                    m_images[d.url] = t;
                    w = img.width; h = img.height;
                }
            }
        }
        m_tab.DeliverImage(d.url, w, h);
    }
    RefreshPageInfo();

    // games: start the player once the pack is unpacked; follow it while it runs
    bool ready = false;
    std::string url;
    s.Lock();
    ready = s.gameReady;
    s.gameReady = false;
    url = s.gameUrl;
    s.Unlock();
    if (ready) {
        const GameLaunchPlan plan = MakeGameLaunchPlan(m_runner.dataRoot, m_runner.playerExe, url);
        std::string why;
        m_impl->childErr.clear();
        const bool started = m_impl->child.Start(plan.argv, plan.gameDir, why);
        s.Lock();
        s.game.phase = started ? GameStatus::Running : GameStatus::Failed;
        s.game.message = started ? std::string() : "Could not start the OkaySpace player (" + why + ").";
        s.Unlock();
        if (started) m_impl->runningUrl = url;
    }
    if (!m_impl->runningUrl.empty()) {
        std::string out, err;
        const bool alive = m_impl->child.Poll(out, err);   // drained every frame, so the game never blocks on a full pipe
        m_impl->childErr = Tail(m_impl->childErr + err, kMaxGameLog);
        if (!alive) {
            const int code = m_impl->child.ExitCode();
            s.Lock();
            if (s.gameUrl == m_impl->runningUrl) {
                const bool ok = code == 0 || m_impl->child.Stopped();
                s.game.phase = ok ? GameStatus::Finished : GameStatus::Failed;
                s.game.message = ok ? "The game has ended." : "The game stopped with an error (code " + std::to_string(code) + ").";
            }
            s.Unlock();
            m_impl->runningUrl.clear();
        }
    }
}

TabHost::GameStatus TabHost::Game(const std::string& packUrl) const {
    TabHostShared& s = *m_impl->sh;
    GameStatus g;
    s.Lock();
    if (s.gameUrl == packUrl) g = s.game;
    s.Unlock();
    return g;
}

bool TabHost::Play(const std::string& packUrl, std::string& why) {
    if (!CanRunGames()) { why = "This window cannot run games."; return false; }
    if (packUrl.empty()) { why = "This game cannot run here."; return false; }
    TabHostShared& s = *m_impl->sh;
    s.Lock();
    const GameStatus::Phase ph = s.game.phase;
    const bool busy = ph == GameStatus::Downloading || ph == GameStatus::Unpacking || s.gameReady || !m_impl->runningUrl.empty();
    if (!busy) {
        s.gameUrl = packUrl;
        s.game = GameStatus();
        s.game.phase = GameStatus::Downloading;
    }
    s.Unlock();
    if (busy) { why = "A game is already running. Close it first."; return false; }
    GameArgs* a = new GameArgs{m_impl->sh, packUrl, m_userAgent, MakeGameLaunchPlan(m_runner.dataRoot, m_runner.playerExe, packUrl)};
    if (okay::thr::Thread* t = okay::thr::CreateThread(GameWorker, "okay-browser-game", a)) { okay::thr::DetachThread(t); return true; }
    delete a;
    s.Lock();
    s.game.phase = GameStatus::Failed;
    s.game.message = "Could not start the download.";
    s.Unlock();
    why = s.game.message;
    return false;
}

void TabHost::StopGame() {
    if (!m_impl->runningUrl.empty()) m_impl->child.Stop();
}

// ---- downloads -----------------------------------------------------------------------------------------------------------

struct DownloadHost::Shared {
    okay::thr::Mutex* mutex = okay::thr::CreateMutex();
    struct Job { std::int64_t bytes = 0; bool finished = false; DownloadItem::State state = DownloadItem::Active; std::string message; bool cancel = false; };
    std::vector<Job> jobs;
    okay::thr::AtomicInt quit;
    ~Shared() { okay::thr::DestroyMutex(mutex); }
    void Lock() { okay::thr::LockMutex(mutex); }
    void Unlock() { okay::thr::UnlockMutex(mutex); }
};

namespace {

struct DownloadArgs { std::shared_ptr<DownloadHost::Shared> sh; std::size_t index; std::string url, path, userAgent; };

int DownloadWorker(void* p) {
    std::unique_ptr<DownloadArgs> a(static_cast<DownloadArgs*>(p));
    DownloadHost::Shared& s = *a->sh;
    okay::http::OwnDownloadSpec sp;
    sp.userAgent = a->userAgent;
    sp.maxBytes = DownloadHost::kMaxBytes;
    sp.retries = 2;
    sp.onBytes = [&s, i = a->index](std::uint64_t n) { s.Lock(); s.jobs[i].bytes = (std::int64_t)n; s.Unlock(); };
    sp.cancelled = [&s, i = a->index] {
        s.Lock();
        const bool c = s.jobs[i].cancel;
        s.Unlock();
        return c || okay::thr::AtomicGet(&s.quit) != 0;
    };
    std::error_code ec;
    const fs::path target = fs::u8path(a->path);
    if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);
    const okay::http::OwnDownloadResult r = okay::http::FetchToFile(a->url, target, sp);
    s.Lock();
    DownloadHost::Shared::Job& j = s.jobs[a->index];
    j.finished = true;
    if (r.outcome == okay::http::OwnOutcome::Ok) { j.state = DownloadItem::Done; j.bytes = (std::int64_t)r.bytes; }
    else if (r.outcome == okay::http::OwnOutcome::Cancelled) j.state = DownloadItem::Cancelled;
    else {
        j.state = DownloadItem::Failed;
        j.message = r.outcome == okay::http::OwnOutcome::TooBig ? "the file is too large"
                  : r.outcome == okay::http::OwnOutcome::HttpError ? "the server answered " + std::to_string(r.status)
                  : r.outcome == okay::http::OwnOutcome::DiskFull ? "the file could not be written (is the disk full?)"
                  : "the server could not be reached";
    }
    s.Unlock();
    return 0;
}

} // namespace

DownloadHost::DownloadHost(std::string userAgent) : m_userAgent(std::move(userAgent)), m_shared(std::make_shared<Shared>()) {}

DownloadHost::~DownloadHost() { okay::thr::AtomicSet(&m_shared->quit, 1); }

int DownloadHost::Start(const std::string& url, const std::string& path, const std::string& contentType, std::int64_t total) {
    DownloadItem it;
    it.url = url;
    it.path = path;
    it.fileName = fs::u8path(path).filename().u8string();
    it.contentType = contentType;
    it.total = total;
    const std::size_t index = m_items.size();
    m_items.push_back(it);
    m_shared->Lock();
    m_shared->jobs.emplace_back();   // the worker only touches its own job, under the mutex; the vector grows only here
    m_shared->Unlock();
    DownloadArgs* a = new DownloadArgs{m_shared, index, url, path, m_userAgent};
    if (okay::thr::Thread* t = okay::thr::CreateThread(DownloadWorker, "okay-browser-download", a)) okay::thr::DetachThread(t);
    else {
        delete a;
        m_items[index].state = DownloadItem::Failed;
        m_items[index].message = "could not start the download";
        m_shared->Lock();
        m_shared->jobs[index].finished = true;
        m_shared->Unlock();
    }
    m_changed = true;
    return (int)index;
}

void DownloadHost::Cancel(int index) {
    if (index < 0 || (std::size_t)index >= m_items.size()) return;
    m_shared->Lock();
    m_shared->jobs[(std::size_t)index].cancel = true;
    m_shared->Unlock();
}

void DownloadHost::Pump() {
    m_shared->Lock();
    for (std::size_t i = 0; i < m_items.size() && i < m_shared->jobs.size(); ++i) {
        DownloadItem& it = m_items[i];
        const Shared::Job& j = m_shared->jobs[i];
        if (it.state != DownloadItem::Active) continue;
        it.bytes = j.bytes;
        if (j.finished) { it.state = j.state; it.message = j.message; m_changed = true; }
    }
    m_shared->Unlock();
}

} // namespace okay::browser
