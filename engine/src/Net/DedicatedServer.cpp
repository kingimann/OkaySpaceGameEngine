#include "okay/Net/DedicatedServer.hpp"
#include "okay/Net/NetworkManager.hpp"
#include "okay/Net/SessionTicket.hpp"
#include "okay/Net/Matchmaking.hpp"
#include "okay/Net/MatchResults.hpp"
#include "okay/Platform/Account/Account.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Core/Game.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iterator>
#include <functional>
#include <map>
#include <memory>
#include <vector>
#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>   // native threads: MinGW's "win32" thread model has no std::thread
#else
#  include <condition_variable>
#  include <mutex>
#  include <thread>
#endif

namespace okay {

namespace {
std::atomic<bool> g_stop{false};
void OnSignal(int) { g_stop = true; }

std::string EnvStr(const char* name) { const char* v = std::getenv(name); return v ? std::string(v) : std::string(); }

bool ParseInt(const std::string& s, int lo, int hi, int& out) {
    char* end = nullptr;
    long v = std::strtol(s.c_str(), &end, 10);
    if (s.empty() || !end || *end != '\0' || v < lo || v > hi) return false;
    out = (int)v;
    return true;
}
bool ParseFloat(const std::string& s, float lo, float hi, float& out) {
    char* end = nullptr;
    double v = std::strtod(s.c_str(), &end);
    if (s.empty() || !end || *end != '\0' || v < lo || v > hi) return false;
    out = (float)v;
    return true;
}

// The host identity seed from a file (base64url, 32 bytes; whitespace around it
// is fine). Never echoed: an error names the file, not what's in it.
bool LoadHostKey(const std::string& path, std::string& seed, std::string& why) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { why = "can't open " + path; return false; }
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const auto a = text.find_first_not_of(" \t\r\n"), b = text.find_last_not_of(" \t\r\n");
    text = a == std::string::npos ? std::string() : text.substr(a, b - a + 1);
    std::vector<std::uint8_t> raw;
    if (!okay::net::SessionTicket::Base64UrlDecode(text, raw) || raw.size() != 32) {
        why = path + " must hold a 32-byte ed25519 seed in base64url";
        return false;
    }
    seed = text;
    return true;
}

// "1h 02m 05s" style uptime.
std::string Uptime(double secs) {
    long s = (long)secs;
    char b[32];
    if (s >= 3600) std::snprintf(b, sizeof(b), "%ldh %02ldm %02lds", s / 3600, s / 60 % 60, s % 60);
    else std::snprintf(b, sizeof(b), "%ldm %02lds", s / 60, s % 60);
    return b;
}
} // namespace

const char* DedicatedServerUsage() {
    return
        "Dedicated server:\n"
        "  --server               run headless as the game's server (no window)\n"
        "  --config FILE          read settings from FILE (default: server.cfg beside the game)\n"
        "  --port N               UDP port to listen on (default 45000)\n"
        "  --scene PATH           scene to run (default: the game's startup scene)\n"
        "  --max-players N        player limit (default 16)\n"
        "  --tick HZ              simulation + network rate (default 30)\n"
        "  --name TEXT            server name shown to players\n"
        "  --password TEXT        require a password to join\n"
        "  --auth                 only signed-in OkaySpace accounts may join\n"
        "  --no-encryption        don't encrypt traffic\n"
        "  --status SECONDS       print a status line this often (default 60, 0 = off)\n"
        "  --run-for SECONDS      stop after this long (for tests)\n"
        "  --log FILE             write everything the server prints to FILE (on Windows\n"
        "                         the game has no console, so this is where output goes)\n"
        "  --advertise            list this server in the launcher's server browser\n"
        "                         (sign in to OkaySpace on this machine first)\n"
        "  --public-addr HOST     the address players connect to (IP or hostname)\n"
        "  --region TEXT          region shown in the browser (e.g. EU)\n"
        "  --max-speed UNITS      reject player moves faster than this (units/second)\n"
        "  --rate-limit N         drop a player's messages beyond N per second\n"
        "  --kick-after N         kick after N rejected moves in a minute\n"
        "  --anticheat FILE       enforce the anti-cheat rules in FILE (.okayac)\n"
        "  --host-key FILE        sign joins with the identity seed in FILE (clients can pin it)\n"
        "Environment: OKAY_SERVER_PORT, OKAY_SERVER_MAX_PLAYERS, OKAY_SERVER_TICK,\n"
        "OKAY_SERVER_NAME, OKAY_SERVER_PASSWORD, OKAY_SERVER_AUTH=1, OKAY_SERVER_ADVERTISE=1,\n"
        "OKAY_SERVER_PUBLIC_ADDR, OKAY_SERVER_REGION (flags win).\n";
}

namespace { std::string g_configPath; }
const std::string& DedicatedServerConfigPath() { return g_configPath; }

bool LoadDedicatedServerConfig(const std::string& path, DedicatedServerOptions& out, std::string* error) {
    std::ifstream f(path);
    if (!f) { if (error) *error = "can't read " + path; return false; }
    auto trim = [](std::string v) {
        std::size_t a = v.find_first_not_of(" \t\r"), b = v.find_last_not_of(" \t\r");
        return a == std::string::npos ? std::string() : v.substr(a, b - a + 1);
    };
    auto truthy = [](const std::string& v) { return v == "1" || v == "true" || v == "yes" || v == "on"; };
    std::string line; int n = 0; int iv = 0; float fv = 0.0f;
    auto bad = [&](const std::string& why) { if (error) *error = path + " line " + std::to_string(n) + ": " + why; return false; };
    while (std::getline(f, line)) {
        ++n;
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) return bad("expected key = value");
        const std::string k = trim(line.substr(0, eq));
        std::string v = trim(line.substr(eq + 1));
        if (!v.empty() && v.front() == '"') {   // "quoted" (may hold # or spaces), then an optional # comment
            const std::size_t close = v.find('"', 1);
            if (close == std::string::npos) return bad("missing closing quote");
            const std::string rest = trim(v.substr(close + 1));
            if (!rest.empty() && rest[0] != '#' && rest[0] != ';') return bad("unexpected text after the quoted value");
            v = v.substr(1, close - 1);
        } else {                                // a # after whitespace starts a comment: max_speed = 12   # units/s
            for (std::size_t i = 1; i < v.size(); ++i)
                if ((v[i] == '#' || v[i] == ';') && (v[i - 1] == ' ' || v[i - 1] == '\t')) { v = trim(v.substr(0, i)); break; }
        }
        if (k == "port") { if (!ParseInt(v, 1, 65535, iv)) return bad("port must be 1-65535"); out.port = (std::uint16_t)iv; }
        else if (k == "name") out.name = v;
        else if (k == "max_players") { if (!ParseInt(v, 1, 1024, iv)) return bad("max_players must be 1-1024"); out.maxPlayers = iv; }
        else if (k == "tick") { if (!ParseFloat(v, 1.0f, 240.0f, fv)) return bad("tick must be 1-240"); out.tickRate = fv; }
        else if (k == "password") out.password = v;
        else if (k == "auth") out.requireAuth = truthy(v);
        else if (k == "encryption") out.encryption = truthy(v);
        else if (k == "status") { if (!ParseFloat(v, 0.0f, 86400.0f, fv)) return bad("status must be seconds"); out.statusEvery = fv; }
        else if (k == "scene") out.scenePath = v;
        else if (k == "advertise") out.advertise = truthy(v);
        else if (k == "public_addr") out.publicAddr = v;
        else if (k == "region") out.region = v;
        else if (k == "anticheat") {
            std::string why;
            if (!out.anticheat.LoadFile(v, &why)) return bad("anticheat: " + why);
            out.anticheatFile = v;
        }
        else if (k == "host_key") {
            std::string why;
            if (!LoadHostKey(v, out.hostKeySeed, why)) return bad("host_key: " + why);
            out.hostKeyFile = v;
        }
        else if (k == "max_speed") { if (!ParseFloat(v, 0.0f, 1e6f, fv)) return bad("max_speed must be a number"); out.maxSpeed = fv; }
        else if (k == "rate_limit") { if (!ParseInt(v, 0, 100000, iv)) return bad("rate_limit must be a number"); out.rateLimit = iv; }
        else if (k == "kick_after") { if (!ParseInt(v, 0, 100000, iv)) return bad("kick_after must be a number"); out.kickAfter = iv; }
        else return bad("unknown setting '" + k + "'");
    }
    return true;
}

bool ParseDedicatedServerArgs(int argc, char** argv, DedicatedServerOptions& out, std::string* error) {
    bool server = false;
    for (int i = 1; i < argc; ++i) if (std::string(argv[i]) == "--server") server = true;
    if (!server) return false;
    auto fail = [&](const std::string& why) { if (error) *error = why; return true; };

    // A config file first (the exported server folder ships one), then the
    // environment, then flags — each overrides the one before.
    g_configPath.clear();
    {
        std::string cfg;
        for (int i = 1; i + 1 < argc; ++i) if (std::string(argv[i]) == "--config") cfg = argv[i + 1];
        if (cfg.empty()) {
            std::vector<std::string> tries = {"server.cfg"};
            std::string exe = argc > 0 && argv[0] ? argv[0] : "";
            const std::size_t slash = exe.find_last_of("/\\");
            if (slash != std::string::npos) {
                const std::string dir = exe.substr(0, slash + 1);
                tries.push_back(dir + "server.cfg");
                tries.push_back(dir + "Data/server.cfg");
            }
            for (const auto& t : tries) if (std::ifstream(t).good()) { cfg = t; break; }
        }
        if (!cfg.empty()) {
            std::string e;
            if (!LoadDedicatedServerConfig(cfg, out, &e)) return fail(e);
            g_configPath = cfg;
        }
    }

    // Environment first (container hosts set these), then flags override.
    int iv = 0; float fv = 0.0f;
    if (std::string e = EnvStr("OKAY_SERVER_PORT"); !e.empty()) {
        if (!ParseInt(e, 1, 65535, iv)) return fail("OKAY_SERVER_PORT must be 1-65535");
        out.port = (std::uint16_t)iv;
    }
    if (std::string e = EnvStr("OKAY_SERVER_MAX_PLAYERS"); !e.empty()) {
        if (!ParseInt(e, 1, 1024, iv)) return fail("OKAY_SERVER_MAX_PLAYERS must be 1-1024");
        out.maxPlayers = iv;
    }
    if (std::string e = EnvStr("OKAY_SERVER_TICK"); !e.empty()) {
        if (!ParseFloat(e, 1.0f, 240.0f, fv)) return fail("OKAY_SERVER_TICK must be 1-240");
        out.tickRate = fv;
    }
    if (std::string e = EnvStr("OKAY_SERVER_NAME"); !e.empty()) out.name = e;
    if (std::string e = EnvStr("OKAY_SERVER_PASSWORD"); !e.empty()) out.password = e;
    if (EnvStr("OKAY_SERVER_AUTH") == "1") out.requireAuth = true;
    if (EnvStr("OKAY_SERVER_ADVERTISE") == "1") out.advertise = true;
    if (std::string e = EnvStr("OKAY_SERVER_PUBLIC_ADDR"); !e.empty()) out.publicAddr = e;
    if (std::string e = EnvStr("OKAY_SERVER_REGION"); !e.empty()) out.region = e;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto value = [&](std::string& v) -> bool {
            if (i + 1 >= argc) return false;
            v = argv[++i];
            return true;
        };
        std::string v;
        if (a == "--server") continue;
        else if (a == "--config") { ++i; continue; }   // loaded above
        else if (a == "--port") {
            if (!value(v) || !ParseInt(v, 1, 65535, iv)) return fail("--port needs a number from 1 to 65535");
            out.port = (std::uint16_t)iv;
        } else if (a == "--scene") {
            if (!value(v)) return fail("--scene needs a path");
            out.scenePath = v;
        } else if (a == "--max-players") {
            if (!value(v) || !ParseInt(v, 1, 1024, iv)) return fail("--max-players needs a number from 1 to 1024");
            out.maxPlayers = iv;
        } else if (a == "--tick") {
            if (!value(v) || !ParseFloat(v, 1.0f, 240.0f, fv)) return fail("--tick needs a rate from 1 to 240");
            out.tickRate = fv;
        } else if (a == "--name") {
            if (!value(v)) return fail("--name needs text");
            out.name = v;
        } else if (a == "--password") {
            if (!value(v)) return fail("--password needs text");
            out.password = v;
        } else if (a == "--auth") out.requireAuth = true;
        else if (a == "--no-encryption") out.encryption = false;
        else if (a == "--status") {
            if (!value(v) || !ParseFloat(v, 0.0f, 86400.0f, fv)) return fail("--status needs seconds (0 = off)");
            out.statusEvery = fv;
        } else if (a == "--run-for") {
            if (!value(v) || !ParseFloat(v, 0.0f, 1e7f, fv)) return fail("--run-for needs seconds");
            out.maxRuntime = fv;
        } else if (a == "--log") {
            if (!value(v) || v.empty()) return fail("--log needs a file path");
            out.logPath = v;
        } else if (a == "--advertise") out.advertise = true;
        else if (a == "--public-addr") {
            if (!value(v) || v.empty()) return fail("--public-addr needs the address players connect to");
            out.publicAddr = v;
        } else if (a == "--region") {
            if (!value(v)) return fail("--region needs text");
            out.region = v;
        } else if (a == "--max-speed") {
            if (!value(v) || !ParseFloat(v, 0.0f, 1e6f, fv)) return fail("--max-speed needs units per second");
            out.maxSpeed = fv;
        } else if (a == "--rate-limit") {
            if (!value(v) || !ParseInt(v, 0, 100000, iv)) return fail("--rate-limit needs messages per second");
            out.rateLimit = iv;
        } else if (a == "--kick-after") {
            if (!value(v) || !ParseInt(v, 0, 100000, iv)) return fail("--kick-after needs a count");
            out.kickAfter = iv;
        } else if (a == "--anticheat") {
            if (!value(v) || v.empty()) return fail("--anticheat needs an .okayac file");
            std::string why;
            AntiCheatSettings s;
            if (!s.LoadFile(v, &why)) return fail("--anticheat: " + why);
            out.anticheat = s; out.anticheatFile = v;
        } else if (a == "--host-key") {
            if (!value(v) || v.empty()) return fail("--host-key needs a file");
            std::string why;
            if (!LoadHostKey(v, out.hostKeySeed, why)) return fail("--host-key: " + why);
            out.hostKeyFile = v;
        } else if (a.rfind("--", 0) == 0) return fail("unknown option " + a);
        else if (out.scenePath.empty()) out.scenePath = a;   // a bare path = the scene
    }
    if (out.advertise && out.publicAddr.empty())
        return fail("--advertise needs --public-addr (the address players connect to)");
    return true;
}

// ---- AsyncTokenCheck ---------------------------------------------------------
// A single worker thread drains a queue of tokens to verify; answers land in a
// cache the network tick reads. The lock/condition/thread are native on Windows
// (MinGW builds there use the win32 thread model, which has no std::thread).

namespace {
#if defined(_WIN32)
struct Lock {
    CRITICAL_SECTION cs; CONDITION_VARIABLE cv;
    Lock()  { InitializeCriticalSection(&cs); InitializeConditionVariable(&cv); }
    ~Lock() { DeleteCriticalSection(&cs); }
    void lock()   { EnterCriticalSection(&cs); }
    void unlock() { LeaveCriticalSection(&cs); }
    void wait()   { SleepConditionVariableCS(&cv, &cs, INFINITE); }   // call while locked
    void notify() { WakeAllConditionVariable(&cv); }
};
#else
struct Lock {
    std::mutex mu; std::condition_variable cv;
    void lock()   { mu.lock(); }
    void unlock() { mu.unlock(); }
    void wait()   { std::unique_lock<std::mutex> lk(mu, std::adopt_lock); cv.wait(lk); lk.release(); }
    void notify() { cv.notify_all(); }
};
#endif
struct Guard { Lock& l; explicit Guard(Lock& x) : l(x) { l.lock(); } ~Guard() { l.unlock(); } };

double NowSeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// Runs web requests (server-browser listing + heartbeats) one at a time on a
// worker, so the game tick never waits on the network.
class BackgroundJobs {
public:
    BackgroundJobs() {
#if defined(_WIN32)
        thread_ = CreateThread(nullptr, 0, &Entry, this, 0, nullptr);
#else
        thread_ = std::thread([this] { Run(); });
#endif
    }
    ~BackgroundJobs() {   // finishes what's queued, then stops
        { Guard g(lock_); stop_ = true; }
        lock_.notify();
#if defined(_WIN32)
        if (thread_) { WaitForSingleObject(thread_, INFINITE); CloseHandle(thread_); }
#else
        if (thread_.joinable()) thread_.join();
#endif
    }
    void Post(std::function<void()> job) {
        { Guard g(lock_); jobs_.push_back(std::move(job)); }
        lock_.notify();
    }
    bool Idle() { Guard g(lock_); return jobs_.empty() && !busy_; }

private:
    void Run() {
        for (;;) {
            std::function<void()> job;
            {
                Guard g(lock_);
                while (!stop_ && jobs_.empty()) lock_.wait();
                if (jobs_.empty()) return;   // stopping and drained
                job = std::move(jobs_.front()); jobs_.pop_front();
                busy_ = true;
            }
            try { job(); } catch (...) {}
            Guard g(lock_); busy_ = false;
        }
    }
    Lock lock_;
    std::deque<std::function<void()>> jobs_;
    bool stop_ = false, busy_ = false;
#if defined(_WIN32)
    HANDLE thread_ = nullptr;
    static DWORD WINAPI Entry(LPVOID p) { static_cast<BackgroundJobs*>(p)->Run(); return 0; }
#else
    std::thread thread_;
#endif
};
} // namespace

struct AsyncTokenCheck::Impl {
    struct Entry { int state = -1; std::string userId; double at = 0.0; };
    Verify verify;
    float cacheSeconds = 300.0f;
    Lock lock;
    std::map<std::string, Entry> cache;
    std::deque<std::string> queue;
    bool stop = false;
#if defined(_WIN32)
    HANDLE thread = nullptr;
    static DWORD WINAPI Entry_(LPVOID p) { static_cast<Impl*>(p)->Run(); return 0; }
#else
    std::thread thread;
#endif

    void Run() {
        for (;;) {
            std::string token;
            {
                Guard g(lock);
                while (!stop && queue.empty()) lock.wait();
                if (stop) return;
                token = queue.front();
                queue.pop_front();
            }
            std::string uid;
            bool ok = false;
            try { ok = verify && verify(token, uid); } catch (...) { ok = false; }
            Guard g(lock);
            const double now = NowSeconds();
            cache[token] = Entry{ok ? 1 : 0, ok ? uid : std::string(), now};
            // Forget long-expired answers so a long-running server doesn't grow forever.
            for (auto it = cache.begin(); it != cache.end();)
                it = (it->second.state >= 0 && now - it->second.at > cacheSeconds * 4) ? cache.erase(it) : std::next(it);
        }
    }
};

AsyncTokenCheck::AsyncTokenCheck(Verify verify, float cacheSeconds) : m(new Impl) {
    m->verify = std::move(verify);
    m->cacheSeconds = cacheSeconds;
#if defined(_WIN32)
    m->thread = CreateThread(nullptr, 0, &Impl::Entry_, m.get(), 0, nullptr);
#else
    m->thread = std::thread([this] { m->Run(); });
#endif
}

AsyncTokenCheck::~AsyncTokenCheck() {
    { Guard g(m->lock); m->stop = true; }
    m->lock.notify();
#if defined(_WIN32)
    if (m->thread) { WaitForSingleObject(m->thread, INFINITE); CloseHandle(m->thread); }
#else
    if (m->thread.joinable()) m->thread.join();
#endif
}

int AsyncTokenCheck::Check(const std::string& token, std::string& outUserId) {
    if (token.empty()) return 0;   // anonymous: rejected when sign-in is required
    Guard g(m->lock);
    const double now = NowSeconds();
    auto it = m->cache.find(token);
    if (it != m->cache.end()) {
        if (it->second.state < 0) return -1;   // the worker is on it
        if (now - it->second.at <= m->cacheSeconds) {
            outUserId = it->second.userId;
            return it->second.state;
        }
    }
    m->cache[token] = Impl::Entry{-1, std::string(), now};
    m->queue.push_back(token);
    m->lock.notify();
    return -1;
}

std::size_t AsyncTokenCheck::Pending() const {
    Guard g(m->lock);
    return m->queue.size();
}

// ---- The server loop ---------------------------------------------------------

void RequestDedicatedServerStop() { g_stop = true; }

int RunDedicatedServer(Scene& scene, const DedicatedServerOptions& opts) {
    g_stop = false;
    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);

    // The scene's NetworkManager (its settings and any game code wired to it), or a
    // fresh one when the scene doesn't have one.
    NetworkManager* net = scene.FindObjectOfType<NetworkManager>();
    if (!net) net = scene.CreateGameObject("Network")->AddComponent<NetworkManager>();
    net->autoStart  = NetworkManager::AutoStart::None;   // we host below, on our port
    net->dedicated  = true;
    net->maxPlayers = opts.maxPlayers;
    net->snapshotRate = opts.tickRate;
    net->serverName = opts.name;
    net->password   = opts.password;
    net->encryption = opts.encryption;
    if (opts.maxSpeed > 0.0f) net->maxMoveSpeed = opts.maxSpeed;
    if (opts.rateLimit > 0) net->maxMessagesPerSecond = opts.rateLimit;
    if (opts.kickAfter > 0) net->kickAfterViolations = opts.kickAfter;
    if (!opts.game.empty()) net->gameId = opts.game;
    if (net->mapName.empty()) net->mapName = scene.Name();

    std::unique_ptr<AsyncTokenCheck> auth;
    if (opts.requireAuth) {
        auth = std::make_unique<AsyncTokenCheck>([](const std::string& token, std::string& uid) {
            return Account::VerifyToken(token, uid);
        });
        AsyncTokenCheck* a = auth.get();
        net->SetJoinCheck([a](const std::string& token, std::string& uid) { return a->Check(token, uid); });
    }

    Game::Reset();
    scene.Start();
    // The command line's .okayac wins over a scene's AntiCheatConfig (which
    // applied its own at Start).
    if (!opts.anticheatFile.empty()) net->anticheat.settings = opts.anticheat;
    if (!opts.hostKeySeed.empty()) net->SetHostIdentity(opts.hostKeySeed);
    if (!net->StartServer(opts.port)) {
        std::fprintf(stderr, "[server] couldn't listen on UDP port %u (is it in use?)\n", (unsigned)opts.port);
        return 2;
    }
    if (!g_configPath.empty()) std::printf("[server] settings from %s\n", g_configPath.c_str());
    std::printf("[server] \"%s\" listening on UDP %u \xC2\xB7 up to %d players \xC2\xB7 %.0f Hz%s%s%s\n",
                opts.name.c_str(), (unsigned)net->ServerPort(), opts.maxPlayers, opts.tickRate,
                opts.requireAuth ? " \xC2\xB7 sign-in required" : "",
                opts.password.empty() ? "" : " \xC2\xB7 password",
                opts.encryption ? " \xC2\xB7 encrypted" : "");
    if (!opts.anticheatFile.empty())
        std::printf("[server] anti-cheat %s from %s\n", net->anticheat.settings.enabled ? "on" : "OFF (enabled false)",
                    opts.anticheatFile.c_str());
    if (!opts.hostKeySeed.empty())
        std::printf("[server] host key from %s; clients can pin %s%s\n", opts.hostKeyFile.c_str(),
                    net->HostIdentityPublicKey().c_str(), opts.encryption ? "" : " (needs encryption on)");
    std::fflush(stdout);

    // Server browser listing: register, heartbeat the player count, unlist on stop.
    std::unique_ptr<BackgroundJobs> web;
    auto listingId = std::make_shared<std::string>();
    if (opts.advertise) {
        if (!Account::IsLoggedIn()) {
            std::fprintf(stderr, "[server] advertise: not signed in to OkaySpace on this machine, so not listed (sign in once with --login NAME)\n");
        } else {
            web = std::make_unique<BackgroundJobs>();
            GameSession gs;
            gs.name = opts.name; gs.hostAddr = opts.publicAddr; gs.port = net->ServerPort();
            gs.maxPlayers = opts.maxPlayers; gs.region = opts.region; gs.game = net->gameId;
            gs.map = net->mapName; gs.hasPassword = !opts.password.empty(); gs.dedicated = true;
            web->Post([gs, listingId] {
                *listingId = Matchmaking::Advertise(gs);
                if (listingId->empty()) std::fprintf(stderr, "[server] couldn't list the server (is game_sessions set up?)\n");
                else std::printf("[server] listed in the server browser as %s:%d\n", gs.hostAddr.c_str(), gs.port);
                std::fflush(stdout);
            });
        }
    }
    double lastBeat = 0.0;

    using clock = std::chrono::steady_clock;
    const auto tick = std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(1.0 / opts.tickRate));
    const float dt = 1.0f / opts.tickRate;
    const auto started = clock::now();
    auto next = started;
    double lastStatus = 0.0, busySum = 0.0; long busyTicks = 0;
    std::map<std::uint32_t, std::string> known;   // roster last tick (for join/leave lines)
    int reportsSeen = FinishedMatchReports();

    while (!g_stop && !Game::QuitRequested()) {
        const auto t0 = clock::now();
        Time::Step(dt);
        scene.Update(Time::DeltaTime());
        busySum += std::chrono::duration<double, std::milli>(clock::now() - t0).count(); ++busyTicks;

        // Join / leave lines from the roster.
        std::map<std::uint32_t, std::string> now;
        for (const auto& p : net->Peers()) now[p.id] = p.name;
        for (const auto& [id, nm] : now)
            if (!known.count(id)) {
                const std::string uid = net->PeerUserId(id);
                std::printf("[server] + %s joined (id %u%s%s) \xC2\xB7 %lu online\n", nm.c_str(), id,
                            uid.empty() ? "" : ", account ", uid.c_str(), (unsigned long)now.size());
            }
        for (const auto& [id, nm] : known)
            if (!now.count(id)) std::printf("[server] - %s left (id %u) \xC2\xB7 %lu online\n", nm.c_str(), id, (unsigned long)now.size());
        if (now.size() != known.size() || now != known) std::fflush(stdout);
        known.swap(now);
        if (const int done = FinishedMatchReports(); done != reportsSeen) {   // net_match_end() results
            reportsSeen = done;
            const std::string st = LastMatchReportStatus();
            if (st == "ok") std::printf("[server] match results saved\n");
            else std::fprintf(stderr, "[server] match results not saved: %s\n", st.c_str());
            std::fflush(stdout);
        }

        const double up = std::chrono::duration<double>(clock::now() - started).count();
        if (opts.statusEvery > 0.0f && up - lastStatus >= opts.statusEvery) {
            lastStatus = up;
            std::printf("[server] %lu/%d players \xC2\xB7 up %s \xC2\xB7 tick %.2f ms avg\n",
                        (unsigned long)known.size(), opts.maxPlayers, Uptime(up).c_str(), busyTicks ? busySum / busyTicks : 0.0);
            std::fflush(stdout);
            busySum = 0.0; busyTicks = 0;
        }
        if (web && up - lastBeat >= 30.0 && web->Idle()) {   // keep the listing fresh
            lastBeat = up;
            const int players = (int)known.size();
            web->Post([listingId, players] { if (!listingId->empty()) Matchmaking::Heartbeat(*listingId, players); });
        }
        if (opts.maxRuntime > 0.0f && up >= opts.maxRuntime) break;

        // Fixed rate: sleep to the next tick (catch up without a spiral if we fall behind).
        next += tick;
        const auto t1 = clock::now();
        if (next < t1 - tick * 4) next = t1;
        const auto wait = next - clock::now();
        if (wait > clock::duration::zero()) {
#if defined(_WIN32)
            Sleep((DWORD)std::chrono::duration_cast<std::chrono::milliseconds>(wait).count());
#else
            std::this_thread::sleep_for(wait);
#endif
        }
    }
    std::printf("[server] stopping\n");
    std::fflush(stdout);
    if (web) {   // unlist, then let the worker finish
        web->Post([listingId] { if (!listingId->empty()) Matchmaking::Unregister(*listingId); });
        web.reset();
    }
    if (PendingMatchReports() > 0) {   // a match that just ended: let its results reach the database
        WaitForMatchReports(8000);
        const std::string st = LastMatchReportStatus();
        if (!st.empty() && st != "ok") std::fprintf(stderr, "[server] match report: %s\n", st.c_str());
    }
    net->Stop();
    return 0;
}

} // namespace okay
