#pragma once
// The decisions of the launcher's START-UP update screen ("Checking for updates", like Steam), kept free of windows, threads and the
// network so a test can link it (tests/test_startup_update.cpp). The check and the install themselves are the launcher's existing
// updater (RunUpdateCheck, shared/SelfUpdate.hpp); nothing here downloads or verifies anything. Docs: docs/updater.md.
#include <string>

namespace okay::startupupdate {

inline constexpr long long kCheckTimeoutMs = 8000;      // the check may take this long before we go on (slow or no network)
inline constexpr long long kRecentCheckMs = 60 * 1000;  // a launcher started again within this long of a start-up check does not repeat it (no restart loops)

enum class Skip { No, SettingOff, EnvOff, DemoMode, EditorRunning, VerdictShown, RecentCheck };

struct StartInputs {
    bool settingOn = true;            // Settings > "Check for updates when the launcher starts" (default on)
    std::string envValue;             // OKAY_UPDATE_ON_LAUNCH ("" = not set); "0" switches the check off for developers / CI
    bool demoMode = false;            // a screenshot / demo mode (OKAY_UI_DEMO_*): no network, no screen
    bool editorRunning = false;       // updating replaces OkayEngine.exe: never while the editor runs
    bool verdictShown = false;        // the last update's failure dialog is already on screen
    long long msSinceLastStartupCheck = -1;   // -1 = never (or unknown)
};

// Should the launcher run the start-up check (and show the screen)? `why` (optional) says what stopped it.
inline bool ShouldRunStartupCheck(const StartInputs& in, Skip* why = nullptr) {
    Skip s = Skip::No;
    if (in.envValue == "0") s = Skip::EnvOff;
    else if (in.demoMode) s = Skip::DemoMode;
    else if (!in.settingOn) s = Skip::SettingOff;
    else if (in.editorRunning) s = Skip::EditorRunning;
    else if (in.verdictShown) s = Skip::VerdictShown;
    else if (in.msSinceLastStartupCheck >= 0 && in.msSinceLastStartupCheck < kRecentCheckMs) s = Skip::RecentCheck;
    if (why) *why = s;
    return s == Skip::No;
}

enum class Phase { Idle, Checking, Downloading, UpToDate, Updated, Failed };

struct ScreenInputs {
    Phase phase = Phase::Idle;
    long long elapsedMs = 0;          // since the start-up check began
    bool skipped = false;             // the person pressed Skip (only possible while nothing is being installed)
    bool timedOutShown = false;       // the "could not check (offline)" notice was already given
    bool relaunchNeeded = false;      // the launcher itself was replaced: restart to finish
    int step = 0, steps = 0;          // download step (0-based) of how many
    bool installing = false;          // the downloads are done and the files are being swapped in
};

struct ScreenView {
    bool visible = false;             // draw the centred panel instead of the normal launcher
    bool canSkip = false;             // never while an update is being installed
    bool locked = false;              // an install is running: the editor and games must not start
    bool giveUpNow = false;           // the check took too long: continue and tell the person (once)
    float progress = -1.0f;           // 0..1, or -1 for "unknown" (a moving bar)
    std::string text;                 // plain status
};

inline ScreenView EvaluateScreen(const ScreenInputs& in) {
    ScreenView v;
    switch (in.phase) {
    case Phase::Checking:
        if (in.skipped) break;
        if (in.elapsedMs >= kCheckTimeoutMs) { v.giveUpNow = !in.timedOutShown; break; }
        v.visible = true; v.canSkip = true; v.text = "Checking for updates";
        break;
    case Phase::Downloading:   // an install is running: no Skip, and it stays up even after the 8 s (the person must not launch mid-install)
        v.visible = true; v.locked = true;
        if (in.installing) { v.text = "Installing"; v.progress = 1.0f; }
        else {
            const int total = in.steps > 0 ? in.steps : 1;
            const int cur = in.step < 0 ? 1 : (in.step + 1 > total ? total : in.step + 1);
            v.text = "Downloading " + std::to_string(cur) + " of " + std::to_string(total);
            v.progress = (float)(cur - 1) / (float)total;
        }
        break;
    case Phase::Updated:
        if (in.relaunchNeeded) { v.visible = true; v.locked = true; v.text = "Restarting"; v.progress = 1.0f; }
        break;   // an update that needs no restart: carry on into the launcher
    case Phase::Failed:   // the existing Failed state (reason + Retry) is shown by the update panel; the screen steps aside
    case Phase::UpToDate:
    case Phase::Idle:
        break;
    }
    return v;
}

} // namespace okay::startupupdate
