#pragma once
// The editor closes when the OkaySpace launcher that started it has gone (owner bug 2026-11-25: "if i quit the launcher
// from the tray, the editor doesnt close"). The launcher passes "--launcher <pid>"; the launcher gate in editor/main.cpp
// checked it once at start-up and never again. Now the main loop asks this watch about once a second (a check that never
// blocks the frame: WaitForSingleObject with a zero wait on a handle opened at start-up, or kill(pid, 0)). The first time
// the launcher is found gone, it asks for one normal close: the same quit request as File > Exit, so unsaved work shows
// the existing "Unsaved Changes" prompt (with a line saying the launcher was closed) and nothing is lost. Cancel keeps
// the editor open; it is not asked again.
// Pure decisions, no platform calls, so tests/test_editor_launcher_watch.cpp runs them headless.

#include <cstdint>

namespace oked::launcherwatch {

/// How often (ms) the launcher is checked.
constexpr uint32_t kCheckMs = 1000;

struct State {
    bool watching = false;     // started from a launcher (--launcher <pid>); false for --dev / OKAY_DEV and tests
    bool launcherGone = false; // the launcher was found gone (the quit prompt says so)
    bool quitAsked = false;    // the one close request was made
    bool checkedOnce = false;
    uint32_t lastCheckMs = 0;
};

/// Watch the launcher from now on (call once, after the launcher gate let the editor in with a token).
inline void Start(State& s) { s = State{}; s.watching = true; }

/// One frame: true when the launcher should be checked now (at most once per kCheckMs; never once it is gone).
/// Unsigned subtraction keeps it right when the millisecond counter wraps.
inline bool CheckDue(State& s, uint32_t nowMs) {
    if (!s.watching || s.launcherGone) return false;
    if (s.checkedOnce && nowMs - s.lastCheckMs < kCheckMs) return false;
    s.checkedOnce = true;
    s.lastCheckMs = nowMs;
    return true;
}

/// The check's answer. True exactly once: the launcher has just been found gone, so ask for a normal close.
inline bool OnChecked(State& s, bool launcherAlive) {
    if (!s.watching || launcherAlive || s.quitAsked) return false;
    s.launcherGone = true;
    s.quitAsked = true;
    return true;
}

/// The quit prompt shows the "launcher was closed" line.
inline bool ShowLauncherClosedNote(const State& s) { return s.launcherGone; }

}  // namespace oked::launcherwatch
