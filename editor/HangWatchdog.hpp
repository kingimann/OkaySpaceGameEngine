#pragma once
// The hang watchdog (feature map 15.11, next to the slow-frame log): when the editor's main loop stops, a thread of its own notices,
// writes where it was stuck, and (Windows) asks "Wait / Close", so nobody has to end the editor in Task Manager and the next freeze
// tells us where it was. Owner bug 2026-11-25: "sometimes the editor freezes and i gotta use task manager to close it".
//
//  * The heartbeat is the crash context's frame counter (okaycrash::ctx::NextFrame, bumped at the top of every editor frame). The
//    stage is the profiler's open scopes (okay::Profiler::LiveScopes: the innermost is what the frame is inside) and the newest
//    breadcrumbs (okaycrash::ctx::Breadcrumb: the project-open steps add theirs). Nothing new is counted: the watchdog only reads.
//  * After OKAY_HANG_REPORT_S seconds without a beat (default 5): one line in okay_editor_perf.log and okay_hang.txt in the crash
//    report folder. After OKAY_HANG_ASK_S (default 15): the "not responding" question (Windows); Wait asks again after as long.
//    "0" turns that step off. Time the editor spends waiting on the person in a native dialog (okaycrash::ctx::WaitingOnPerson)
//    does not count.
//  * This header is the pure half (no threads, no files): the thresholds, the decisions and the report text, so
//    tests/test_hang_watchdog.cpp runs them with a fake clock. The thread is editor/HangWatchdog.cpp.
//
// Written from the plain idea of a heartbeat watchdog (a counter that must keep moving, checked from outside); nothing copied.
// Windows build rule (CLAUDE.md 21a): only <cstdio>, <cstdlib>, <cmath>, <string> and <vector> here.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace okay::editor::hang {

struct Settings {
    double reportSeconds = 5.0;   // 0 = no report
    double askSeconds = 15.0;     // 0 = never ask (the report still comes)

    /// One threshold from an environment value: null or unreadable = `def`, "0" (or less) = off, otherwise at least `least` seconds.
    static double SecondsFrom(const char* env, double def, double least) {
        if (!env || !*env) return def;
        char* end = nullptr;
        const double v = std::strtod(env, &end);
        if (end == env || !std::isfinite(v)) return def;
        if (v <= 0.0) return 0.0;
        return v < least ? least : v;
    }
    /// From OKAY_HANG_REPORT_S and OKAY_HANG_ASK_S (the callers pass std::getenv of each). The question never comes before the report.
    static Settings From(const char* reportEnv, const char* askEnv) {
        Settings s;
        s.reportSeconds = SecondsFrom(reportEnv, 5.0, 1.0);
        s.askSeconds = SecondsFrom(askEnv, 15.0, 2.0);
        if (s.askSeconds > 0.0 && s.reportSeconds > 0.0 && s.askSeconds < s.reportSeconds) s.askSeconds = s.reportSeconds;
        return s;
    }
};

enum class Action {
    None,       // nothing to do
    Report,     // the loop has been stuck reportSeconds: write the log line and the report (once per hang)
    Ask,        // stuck askSeconds (and again after every Wait): ask Wait / Close
    Recovered,  // it moves again after a report: one log line saying how long it was stuck
};

/// What the watchdog thread knows between its looks (every ~250 ms). `beat` is the heartbeat counter, `now` seconds on a steady
/// clock, `waiting` true while the editor waits on the person (a native file dialog), which never counts as a hang.
struct Tracker {
    std::uint32_t lastBeat = 0;
    double lastMove = 0.0;      // when the beat last changed (or waiting ended)
    double nextAsk = 0.0;       // when the next question is due (0: none yet this hang)
    bool started = false, reported = false;
    double stuckFor = 0.0;      // seconds without a beat, as of the last Step

    Action Step(std::uint32_t beat, double now, bool waiting, const Settings& s) {
        if (!started || beat != lastBeat || waiting) {
            const bool wasReported = started && reported && beat != lastBeat;
            const double was = now - lastMove;
            started = true;
            lastBeat = beat;
            lastMove = now;
            nextAsk = 0.0;
            reported = false;
            stuckFor = wasReported ? was : 0.0;
            return wasReported ? Action::Recovered : Action::None;
        }
        stuckFor = now - lastMove;
        if (s.reportSeconds > 0.0 && !reported && stuckFor >= s.reportSeconds) { reported = true; return Action::Report; }
        if (s.askSeconds > 0.0 && stuckFor >= s.askSeconds) {
            if (nextAsk == 0.0) nextAsk = lastMove + s.askSeconds;
            if (now >= nextAsk) { nextAsk = now + s.askSeconds; return Action::Ask; }
        }
        return Action::None;
    }
    /// The person chose Wait: the next question comes askSeconds from now (Step already set that; kept for clarity at the call).
    void Waited(double now, const Settings& s) { nextAsk = now + s.askSeconds; }
};

/// What the editor was doing, as plain text: the innermost open profiler scope wins, then the newest breadcrumb.
inline std::string StageText(const std::vector<std::string>& scopes, const std::string& lastStep) {
    if (!scopes.empty()) {
        std::string s;
        for (std::size_t i = 0; i < scopes.size(); ++i) s += (i ? " > " : "") + scopes[i];
        return s;
    }
    return lastStep.empty() ? std::string("startup") : lastStep;
}

/// The one line for okay_editor_perf.log (no newline).
inline std::string LogLine(Action a, double stuckSeconds, unsigned frame, const std::string& stage) {
    char buf[96];
    if (a == Action::Recovered) {
        std::snprintf(buf, sizeof(buf), "hang over: the editor moved again after %.1f s (frame %u)", stuckSeconds, frame);
        return std::string(buf) + "; it was stuck in: " + stage;
    }
    std::snprintf(buf, sizeof(buf), "HANG: no frame for %.1f s (frame %u)", stuckSeconds, frame);
    return std::string(buf) + "; stuck in: " + stage;
}

/// okay_hang.txt: what someone sends us. `crumbs` oldest first; `stack` (Windows: the main thread's return addresses as
/// module+offset) may be empty.
inline std::string ReportText(const std::string& version, const std::string& when, double stuckSeconds, unsigned frame,
                              const std::string& stage, const std::vector<std::string>& scopes, const std::vector<std::string>& crumbs,
                              const std::string& scene, const std::vector<std::string>& stack) {
    char buf[128];
    std::string r = "OkaySpace editor hang report\n";
    r += "Version: " + version + "\n";
    if (!when.empty()) r += "When: " + when + "\n";
    std::snprintf(buf, sizeof(buf), "No frame for: %.1f s (the main loop stopped at frame %u)\n", stuckSeconds, frame);
    r += buf;
    r += "Stuck in: " + stage + "\n";
    r += "Scene: " + (scene.empty() ? std::string("(none)") : scene) + "\n";
    r += "--- Open profiler scopes (outermost first) ---\n";
    if (scopes.empty()) r += "(none: the profiler was off, or the loop stopped outside every timed scope)\n";
    for (const std::string& s : scopes) r += "  " + s + "\n";
    r += "--- Breadcrumbs (oldest first) ---\n";
    for (const std::string& c : crumbs) r += "  " + c + "\n";
    r += "--- Main thread stack ---\n";
    if (stack.empty()) r += "(not captured on this platform)\n";
    for (const std::string& s : stack) r += "  " + s + "\n";
    return r;
}

/// The question's text (the Windows box: Yes = Wait, No = Close).
inline std::string AskText(double stuckSeconds, const std::string& stage) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.0f seconds", stuckSeconds);
    return "The editor is not responding (stuck in: " + stage + ", for " + buf + ").\n\n"
           "Yes: wait for it.\nNo: close the editor now. Changes since the last save or autosave are lost.\n\n"
           "What it was doing is saved in okay_hang.txt next to the crash reports.";
}

/// The exit code when the person chose Close.
constexpr int kClosedExitCode = 3;

// ---- the thread (editor/HangWatchdog.cpp) ----
/// Start watching (once, after the crash handler is installed and before the main loop). False when both steps are turned off.
bool Start(const char* version);
/// Stop and join the thread (at shutdown, before the main loop's state goes away).
void Stop();

}  // namespace okay::editor::hang
