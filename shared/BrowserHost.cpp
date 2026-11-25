// Browser: the host half of one tab (see BrowserHost.hpp). Fetching goes through the one HTTP seam (okay/Platform/NativeFetch.hpp)
// on a worker thread; images are read by our own decoders (okay/Core/ImageDecode.hpp); games are downloaded, checked and unpacked
// on a second worker thread and run by the player through okaycli::Child (tools/okay-cli/Process.hpp).
#include "BrowserHost.hpp"

#include "OkayThreads.hpp"
#include "Process.hpp"
#include "okay/Browser/GamePack.hpp"
#include "okay/Core/ImageDecode.hpp"
#include "okay/Platform/NativeFetch.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>
#include <vector>

namespace okay::browser {

namespace fs = std::filesystem;

namespace {

struct FetchJob { std::string url; bool image = false; int generation = 0; };
struct FetchDone { std::string url; bool image = false; int generation = 0; bool ok = false; long status = 0; std::string body, error; };

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
        const std::uint64_t cap = job.image ? TabHost::kMaxImageBytes : (std::uint64_t)BrowserTab::kMaxPageBytes;
        const okay::http::OwnOutcome o = okay::http::FetchPrefix(job.url, args->userAgent, cap, cap - 1, 15000, d.body, &d.status);
        d.ok = o == okay::http::OwnOutcome::Ok;
        if (!d.ok) {
            d.body.clear();
            if (o == okay::http::OwnOutcome::TooBig) d.error = "The page is too large.";
            else if (o == okay::http::OwnOutcome::NotHandled) d.error = "Could not reach the server. Check your connection and try again.";
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
    m_tab = BrowserTab();
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

void TabHost::Pump() {
    TabHostShared& s = *m_impl->sh;
    // pages and images
    const std::vector<std::string> urls = m_tab.TakeFetches();
    if (!urls.empty()) {
        s.Lock();
        for (const std::string& u : urls) s.jobs.push_back({u, u != WithoutFragment(m_tab.Url()), m_impl->generation});
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
        if (!d.image) { m_tab.DeliverPage(d.url, d.ok, (int)d.status, d.body, d.error); continue; }
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

} // namespace okay::browser
