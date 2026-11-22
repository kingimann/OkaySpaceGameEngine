#pragma once
// Saving inside the UWP suspend deadline (pure, no Windows headers; tested on Linux by tests/test_xbox_uwp_runtime.cpp, SuspendTests).
//
// What it decides:
//   * SaveLayoutFor: where the game's own saves, its settings (Prefs) and the suspend snapshot live under ApplicationData.LocalFolder.
//     The install folder is read-only, so Save::SetBaseDir must point here before the game saves anything (the desktop player does the
//     same with its per-user folder). The suspend snapshot has its own folder so it never shows up in the game's load menu.
//   * SuspendRemainingMs / FlushBudgetMs: how long the save may take. SuspendingOperation.Deadline and "now" are Windows DateTime
//     values (100 ns ticks); the shell passes both. Microsoft documents about 5 seconds; we keep 1 second back for the GPU trim and
//     the deferral.
//   * SuspendFlush: the steps run in order (settings first, they are small and matter most; then the game state; then the log), each
//     timed; once the budget is used up the rest are skipped and the report says so (the log file records it). A throwing step is
//     caught. The first step always runs.
// Written from Microsoft's documentation of CoreApplication.Suspending, SuspendingOperation.Deadline / GetDeferral, DateTime (100 ns
// units) and ApplicationData.LocalFolder. Our own code. UNVERIFIED on Windows and on a console.
#include <cstdint>
#include <exception>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace okay::xbox {

struct SaveLayout {
    std::string saveBase;     // Save::SetBaseDir: relative save files and the default slot store (saveBase + "saves/") land here
    std::string prefsFile;    // Prefs::Load / Prefs::Save: the game's settings (volume, language, accessibility, key bindings ...)
    std::string suspendDir;   // the suspend snapshot and its marker (XboxGameRuntime)
    bool Valid() const { return !saveBase.empty(); }
};

/// `localDir` = ApplicationData.LocalFolder (any separators, trailing slash or not). Empty in, empty out.
inline SaveLayout SaveLayoutFor(const std::string& localDir) {
    SaveLayout l;
    std::string d = localDir;
    for (char& c : d) if (c == '\\') c = '/';
    while (d.size() > 1 && d.back() == '/') d.pop_back();
    if (d.empty()) return l;
    l.saveBase = d + "/saves/";
    l.prefsFile = l.saveBase + "game.okayprefs";
    l.suspendDir = d + "/suspend";
    return l;
}

constexpr std::uint64_t kSuspendFallbackMs = 5000;   // Microsoft: an app has about 5 seconds; used when the deadline is unknown
constexpr std::uint64_t kSuspendMaxMs = 10000;       // never plan on more than this, whatever the deadline says

/// Milliseconds from `nowTicks` to `deadlineTicks` (both 100 ns DateTime ticks). 0 when past; the fallback when the deadline is 0 (unknown).
inline std::uint64_t SuspendRemainingMs(long long deadlineTicks, long long nowTicks) {
    if (deadlineTicks <= 0) return kSuspendFallbackMs;
    if (deadlineTicks <= nowTicks) return 0;
    const unsigned long long ms = (unsigned long long)(deadlineTicks - nowTicks) / 10000ull;
    return ms > kSuspendMaxMs ? kSuspendMaxMs : (std::uint64_t)ms;
}

/// The part of the remaining time the save steps may use: 1 s is kept for the trim and the deferral; with less than 2 s, half.
inline std::uint64_t FlushBudgetMs(std::uint64_t remainingMs) {
    return remainingMs >= 2000 ? remainingMs - 1000 : (remainingMs >= 1000 ? remainingMs - 1000 : remainingMs / 2);
}

struct SuspendStepResult {
    std::string name;
    bool ran = false, ok = false;
    std::string error;
    std::uint64_t ms = 0;
};

struct SuspendReport {
    std::vector<SuspendStepResult> steps;
    std::uint64_t totalMs = 0;
    int Skipped() const { int n = 0; for (const auto& s : steps) if (!s.ran) ++n; return n; }
    bool AllOk() const { for (const auto& s : steps) if (!s.ran || !s.ok) return false; return true; }
    /// One line for the log: "settings ok (3 ms), game state FAILED: disk full, log skipped (deadline)".
    std::string Summary() const {
        std::string out;
        for (const auto& s : steps) {
            if (!out.empty()) out += ", ";
            if (!s.ran) out += s.name + " skipped (deadline)";
            else if (s.ok) out += s.name + " ok (" + std::to_string(s.ms) + " ms)";
            else out += s.name + " FAILED: " + (s.error.empty() ? std::string("no reason given") : s.error);
        }
        return out.empty() ? std::string("nothing to save") : out;
    }
};

class SuspendFlush {
public:
    /// A step returns false (and may set the reason) when it failed.
    using Step = std::function<bool(std::string& error)>;
    void Add(std::string name, Step fn) { steps_.emplace_back(std::move(name), std::move(fn)); }
    std::size_t Count() const { return steps_.size(); }

    /// Run the steps in order while the time used is under `budgetMs` (the first step always runs). `nowMs` may be null (no timing).
    SuspendReport Run(const std::function<std::uint64_t()>& nowMs, std::uint64_t budgetMs) const {
        SuspendReport r;
        auto now = [&]() -> std::uint64_t { return nowMs ? nowMs() : 0; };
        const std::uint64_t t0 = now();
        for (std::size_t i = 0; i < steps_.size(); ++i) {
            SuspendStepResult s; s.name = steps_[i].first;
            const std::uint64_t start = now();
            if (i > 0 && start - t0 >= budgetMs) { s.error = "the suspend deadline"; r.steps.push_back(s); continue; }
            s.ran = true;
            try { s.ok = steps_[i].second ? steps_[i].second(s.error) : true; }
            catch (const std::exception& e) { s.ok = false; s.error = std::string("threw: ") + e.what(); }
            catch (...) { s.ok = false; s.error = "threw an unknown exception"; }
            s.ms = now() - start;
            r.steps.push_back(s);
        }
        r.totalMs = now() - t0;
        return r;
    }

private:
    std::vector<std::pair<std::string, Step>> steps_;
};

} // namespace okay::xbox
