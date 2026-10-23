#include "okay/Net/ServerBrowser.hpp"
#include "okay/Net/Matchmaking.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>   // native threads: MinGW's "win32" thread model has no std::thread
#elif !defined(__EMSCRIPTEN__)
#  include <mutex>
#  include <thread>
#endif

namespace okay {

namespace {
bool SameTitle(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return true;
}
} // namespace

struct ServerBrowser::Impl {
    std::string game;
    std::vector<Entry> results;           // (guarded)
    std::atomic<bool> busy{false};
    std::atomic<std::uint32_t> version{0};
    int sources = kAll;
#if defined(_WIN32)
    CRITICAL_SECTION cs;
    HANDLE thread = nullptr;
    Impl() { InitializeCriticalSection(&cs); }
    ~Impl() { DeleteCriticalSection(&cs); }
    void lock() { EnterCriticalSection(&cs); }
    void unlock() { LeaveCriticalSection(&cs); }
    static DWORD WINAPI Entry_(LPVOID p) { static_cast<Impl*>(p)->Run(); return 0; }
    void Join() { if (thread) { WaitForSingleObject(thread, INFINITE); CloseHandle(thread); thread = nullptr; } }
    bool Start() { thread = CreateThread(nullptr, 0, &Entry_, this, 0, nullptr); return thread != nullptr; }
#elif !defined(__EMSCRIPTEN__)
    std::mutex mu;
    std::thread thread;
    void lock() { mu.lock(); }
    void unlock() { mu.unlock(); }
    void Join() { if (thread.joinable()) thread.join(); }
    bool Start() { thread = std::thread([this] { Run(); }); return true; }
#else
    void lock() {}
    void unlock() {}
    void Join() {}
    bool Start() { return false; }   // no threads on the web build
#endif

    void Run() {
        std::string g; int src;
        lock(); g = game; src = sources; unlock();
        std::vector<Entry> rows;
        if (src & kInternet) {
            try {
                for (const auto& s : Matchmaking::Browse(g)) {
                    Entry e;
                    e.info.host = s.hostAddr; e.info.port = (std::uint16_t)s.port; e.info.name = s.name;
                    e.info.players = s.players; e.info.maxPlayers = s.maxPlayers; e.info.map = s.map;
                    e.info.password = s.hasPassword; e.info.dedicated = s.dedicated;
                    e.game = s.game; e.region = s.region; e.listed = true;
                    rows.push_back(e);
                }
            } catch (...) {}
        }
        std::vector<QueryTarget> targets;
        for (const auto& e : rows) targets.push_back({e.info.host, e.info.port});
        const auto res = QueryServers(targets, 1500, (src & kLan) != 0);
        for (std::size_t i = 0; i < rows.size() && i < res.size(); ++i) {
            if (!res[i].responded) continue;
            const std::string game = rows[i].game;
            rows[i].info = res[i];
            rows[i].game = !res[i].game.empty() ? res[i].game : game;
        }
        for (std::size_t i = rows.size(); i < res.size(); ++i) {
            Entry e; e.info = res[i]; e.game = res[i].game;
            // The same server listed publicly and found on the LAN: keep one.
            bool dup = false;
            for (const auto& r : rows) if (r.info.port == e.info.port && r.info.name == e.info.name && r.game == e.game) dup = true;
            if (!dup) rows.push_back(e);
        }
        if (!g.empty())
            rows.erase(std::remove_if(rows.begin(), rows.end(), [&](const Entry& e) { return !e.game.empty() && !SameTitle(e.game, g); }), rows.end());
        std::stable_sort(rows.begin(), rows.end(), [](const Entry& a, const Entry& b) {
            if (a.info.responded != b.info.responded) return a.info.responded;
            return a.info.pingMs < b.info.pingMs;
        });
        lock(); results = std::move(rows); unlock();
        ++version;
        busy = false;
    }
};

ServerBrowser::ServerBrowser() : m(new Impl) {}
ServerBrowser::~ServerBrowser() { m->Join(); }

ServerBrowser& ServerBrowser::Shared() { static ServerBrowser b; return b; }

void ServerBrowser::SetGame(const std::string& title) { m->lock(); m->game = title; m->unlock(); }
const std::string& ServerBrowser::Game() const { return m->game; }

void ServerBrowser::Refresh(int sources) {
    if (m->busy) return;
    m->Join();   // the previous refresh has finished; reap its thread
    m->lock(); m->sources = sources; m->unlock();
    m->busy = true;
    if (!m->Start()) m->busy = false;
}
bool ServerBrowser::Busy() const { return m->busy; }
std::vector<ServerBrowser::Entry> ServerBrowser::Results() const {
    m->lock(); std::vector<Entry> r = m->results; m->unlock();
    return r;
}
std::uint32_t ServerBrowser::Version() const { return m->version; }

} // namespace okay
