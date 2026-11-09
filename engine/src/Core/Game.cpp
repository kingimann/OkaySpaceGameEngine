#include "okay/Core/Game.hpp"
#include "okay/Core/Log.hpp"

#include <algorithm>
#include <mutex>

namespace okay {

namespace {

struct Hold { std::string name; int count; };
std::vector<Hold> g_holds;

bool g_pauseOnFocusLoss = false;
bool g_focused = true;

std::function<bool()> g_guard;
bool g_inGuard = false;
int  g_vetoes = 0;

std::mutex  g_quitMutex;
std::string g_quitReason;
int         g_exitCode = 0;

std::vector<Hold>::iterator Find(const std::string& name) {
    return std::find_if(g_holds.begin(), g_holds.end(), [&](const Hold& h) { return h.name == name; });
}
std::string Name(const std::string& reason) { return reason.empty() ? std::string("unnamed") : reason; }

}  // namespace

void Game::Refresh() {
    const bool p = !g_holds.empty();
    if (p == s_paused) return;
    s_paused = p;
    Time::SetFrozen(p);
}

void Game::SetPaused(bool paused) {
    auto it = Find(kManualPause);
    if (paused) {
        if (it == g_holds.end()) g_holds.push_back({kManualPause, 1});
        else it->count = 1;
    } else if (it != g_holds.end()) {
        g_holds.erase(it);
    }
    Refresh();
}

void Game::PushPause(const std::string& reason) {
    const std::string n = Name(reason);
    auto it = Find(n);
    if (it == g_holds.end()) g_holds.push_back({n, 1});
    else ++it->count;
    Refresh();
}

bool Game::PopPause(const std::string& reason) {
    auto it = Find(Name(reason));
    if (it == g_holds.end()) return false;
    if (--it->count <= 0) g_holds.erase(it);
    Refresh();
    return true;
}

void Game::ClearPause(const std::string& reason) {
    auto it = Find(Name(reason));
    if (it == g_holds.end()) return;
    g_holds.erase(it);
    Refresh();
}

bool Game::IsPausedBy(const std::string& reason) { return Find(Name(reason)) != g_holds.end(); }

std::vector<std::string> Game::PauseReasons() {
    std::vector<std::string> out;
    out.reserve(g_holds.size());
    for (const Hold& h : g_holds) out.push_back(h.name);
    return out;
}

// ---- focus -------------------------------------------------------------------

void Game::SetPauseOnFocusLoss(bool on) {
    g_pauseOnFocusLoss = on;
    if (on && !g_focused) { if (!IsPausedBy(kFocusPause)) PushPause(kFocusPause); }
    else if (!on) ClearPause(kFocusPause);
}
bool Game::PauseOnFocusLoss() { return g_pauseOnFocusLoss; }
bool Game::HasFocus() { return g_focused; }

void Game::NotifyFocus(bool focused) {
    g_focused = focused;
    if (!g_pauseOnFocusLoss) return;
    if (!focused) { if (!IsPausedBy(kFocusPause)) PushPause(kFocusPause); }
    else ClearPause(kFocusPause);
}

// ---- quit ----------------------------------------------------------------------

void Game::ForceQuit(const std::string& reason, int exitCode) {
    std::lock_guard<std::mutex> lock(g_quitMutex);
    if (!s_quit.load(std::memory_order_relaxed)) {
        g_quitReason = reason;
        g_exitCode = exitCode;
    } else if (exitCode != 0 && g_exitCode == 0) {
        g_exitCode = exitCode;   // an error is not hidden by an earlier clean quit
        if (!reason.empty()) g_quitReason = reason;
    }
    s_quit.store(true, std::memory_order_release);
}

bool Game::RequestQuit(const std::string& reason, int exitCode) {
    if (g_inGuard) return false;   // the guard itself asked: it is deciding, not quitting
    if (!QuitRequested() && g_guard) {
        bool allow = true;
        g_inGuard = true;
        try { allow = g_guard(); }
        catch (const std::exception& e) { Log::Warning("Game quit guard threw: ", e.what(), " (quit allowed)"); }
        catch (...) { Log::Warning("Game quit guard threw (quit allowed)"); }
        g_inGuard = false;
        if (!allow) { ++g_vetoes; return false; }
    }
    ForceQuit(reason, exitCode);
    return true;
}

void Game::CancelQuit() {
    std::lock_guard<std::mutex> lock(g_quitMutex);
    s_quit.store(false, std::memory_order_release);
    g_quitReason.clear();
    g_exitCode = 0;
}

std::string Game::QuitReason() {
    std::lock_guard<std::mutex> lock(g_quitMutex);
    return g_quitReason;
}

int Game::ExitCode() {
    std::lock_guard<std::mutex> lock(g_quitMutex);
    return g_exitCode;
}

void Game::SetQuitGuard(std::function<bool()> guard) { g_guard = std::move(guard); }
int Game::QuitVetoCount() { return g_vetoes; }

void Game::Reset() {
    CancelQuit();
    g_guard = nullptr;
    g_vetoes = 0;
    g_holds.clear();
    if (g_pauseOnFocusLoss && !g_focused) g_holds.push_back({kFocusPause, 1});
    Refresh();
    Time::ClearEffects();
}

void Game::BeginSession() {
    Reset();
    Time::SetTimeScale(1.0f);
}

}  // namespace okay
