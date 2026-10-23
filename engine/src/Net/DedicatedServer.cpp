#include "okay/Net/DedicatedServer.hpp"
#include "okay/Net/NetworkManager.hpp"
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
        "Environment: OKAY_SERVER_PORT, OKAY_SERVER_MAX_PLAYERS, OKAY_SERVER_TICK,\n"
        "OKAY_SERVER_NAME, OKAY_SERVER_PASSWORD, OKAY_SERVER_AUTH=1 (flags win).\n";
}

bool ParseDedicatedServerArgs(int argc, char** argv, DedicatedServerOptions& out, std::string* error) {
    bool server = false;
    for (int i = 1; i < argc; ++i) if (std::string(argv[i]) == "--server") server = true;
    if (!server) return false;
    auto fail = [&](const std::string& why) { if (error) *error = why; return true; };

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

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto value = [&](std::string& v) -> bool {
            if (i + 1 >= argc) return false;
            v = argv[++i];
            return true;
        };
        std::string v;
        if (a == "--server") continue;
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
        } else if (a.rfind("--", 0) == 0) return fail("unknown option " + a);
        else if (out.scenePath.empty()) out.scenePath = a;   // a bare path = the scene
    }
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
    if (!net->StartServer(opts.port)) {
        std::fprintf(stderr, "[server] couldn't listen on UDP port %u (is it in use?)\n", (unsigned)opts.port);
        return 2;
    }
    std::printf("[server] \"%s\" listening on UDP %u \xC2\xB7 up to %d players \xC2\xB7 %.0f Hz%s%s%s\n",
                opts.name.c_str(), (unsigned)net->ServerPort(), opts.maxPlayers, opts.tickRate,
                opts.requireAuth ? " \xC2\xB7 sign-in required" : "",
                opts.password.empty() ? "" : " \xC2\xB7 password",
                opts.encryption ? " \xC2\xB7 encrypted" : "");
    std::fflush(stdout);

    using clock = std::chrono::steady_clock;
    const auto tick = std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(1.0 / opts.tickRate));
    const float dt = 1.0f / opts.tickRate;
    const auto started = clock::now();
    auto next = started;
    double lastStatus = 0.0, busySum = 0.0; long busyTicks = 0;
    std::map<std::uint32_t, std::string> known;   // roster last tick (for join/leave lines)

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

        const double up = std::chrono::duration<double>(clock::now() - started).count();
        if (opts.statusEvery > 0.0f && up - lastStatus >= opts.statusEvery) {
            lastStatus = up;
            std::printf("[server] %lu/%d players \xC2\xB7 up %s \xC2\xB7 tick %.2f ms avg\n",
                        (unsigned long)known.size(), opts.maxPlayers, Uptime(up).c_str(), busyTicks ? busySum / busyTicks : 0.0);
            std::fflush(stdout);
            busySum = 0.0; busyTicks = 0;
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
    net->Stop();
    return 0;
}

} // namespace okay
