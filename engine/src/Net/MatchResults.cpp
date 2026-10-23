#include "okay/Net/MatchResults.hpp"
#include "okay/Platform/Account/Account.hpp"
#include "okay/Net/NetworkManager.hpp"
#include "okay/Net/ServerBrowser.hpp"

#include <atomic>
#include <chrono>

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
std::atomic<int> g_pending{0}, g_finished{0};
#if defined(_WIN32)
CRITICAL_SECTION* StatusLock() {
    static CRITICAL_SECTION cs;
    static bool init = (InitializeCriticalSection(&cs), true);
    (void)init;
    return &cs;
}
void Lock() { EnterCriticalSection(StatusLock()); }
void Unlock() { LeaveCriticalSection(StatusLock()); }
#elif !defined(__EMSCRIPTEN__)
std::mutex g_mu;
void Lock() { g_mu.lock(); }
void Unlock() { g_mu.unlock(); }
#else
void Lock() {}
void Unlock() {}
#endif
std::string g_status;   // (guarded)

void Send(MatchReport* m) {
    std::string err, id;
    bool ok = false;
    try { ok = MatchResults(Account::Get()).Report(*m, &id, &err); } catch (...) { err = "match report failed"; }
    delete m;
    Lock(); g_status = ok ? std::string("ok") : err; Unlock();
    ++g_finished;
    --g_pending;
}
#if defined(_WIN32)
DWORD WINAPI Entry(LPVOID p) { Send(static_cast<MatchReport*>(p)); return 0; }
#endif
} // namespace

bool SubmitMatchAsync(const MatchReport& report) {
    if (!Account::Exists() || !Account::IsLoggedIn()) return false;
#if defined(__EMSCRIPTEN__)
    (void)report;
    return false;
#else
    MatchReport* m = new MatchReport(report);
    ++g_pending;
#  if defined(_WIN32)
    HANDLE h = CreateThread(nullptr, 0, &Entry, m, 0, nullptr);
    if (!h) { delete m; --g_pending; return false; }
    CloseHandle(h);
#  else
    try { std::thread([m] { Send(m); }).detach(); }
    catch (...) { delete m; --g_pending; return false; }
#  endif
    return true;
#endif
}

bool EndMatchAndReport(NetworkManager& n, const std::vector<std::uint32_t>& winners,
                       const std::string& fallbackGame, const std::string& fallbackMap) {
    if (!n.IsServer()) return false;
    const float len = n.EndMatch();
    MatchReport m = MatchReport::FromRoster(n.Roster(), n.dedicated ? ~0u : 0u, len, winners);
    m.game = !n.gameId.empty() ? n.gameId : !fallbackGame.empty() ? fallbackGame : ServerBrowser::Shared().Game();
    m.map = !n.mapName.empty() ? n.mapName : fallbackMap;
    m.server = n.serverName; m.dedicated = n.dedicated;
    if (m.game.empty() || m.players.empty()) return false;
    return SubmitMatchAsync(m);
}

int PendingMatchReports() { return g_pending.load(); }
int FinishedMatchReports() { return g_finished.load(); }

void WaitForMatchReports(int timeoutMs) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (g_pending.load() > 0 && std::chrono::steady_clock::now() < until) {
#if defined(_WIN32)
        Sleep(10);
#elif !defined(__EMSCRIPTEN__)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
#else
        break;
#endif
    }
}

std::string LastMatchReportStatus() {
    Lock(); std::string s = g_status; Unlock();
    return s;
}

} // namespace okay
