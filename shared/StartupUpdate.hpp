#pragma once
// The decisions of the launcher's START-UP update screen ("Checking for updates", like Steam), kept free of windows, threads and the
// network so a test can link it (tests/test_startup_update.cpp). The check and the install themselves are the launcher's existing
// updater (RunUpdateCheck, shared/SelfUpdate.hpp); nothing here downloads or verifies anything. Docs: docs/updater.md.
#include <string>

namespace okay::startupupdate {

inline constexpr long long kCheckTimeoutMs = 8000;      // the check may take this long before we go on (slow or no network)
inline constexpr long long kRestartGiveUpMs = 12 * 1000;  // "Restarting" must end by itself: after this the person is told to reopen the launcher
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
    bool restartFailed = false;       // the restart helper could not start (or the editor is open and has to be closed first)
    long long sinceUpdatedMs = 0;     // how long the state has been "Updated"
};

struct ScreenView {
    bool visible = false;             // draw the centred panel instead of the normal launcher
    bool canSkip = false;             // never while an update is being installed
    bool locked = false;              // an install is running: the editor and games must not start
    bool giveUpNow = false;           // the check took too long: continue and tell the person (once)
    bool restartGaveUp = false;       // the restart did not happen: step aside and say "Update installed; close and reopen OkaySpace"
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
        if (!in.relaunchNeeded) break;
        if (in.restartFailed || in.sinceUpdatedMs >= kRestartGiveUpMs) { v.restartGaveUp = true; break; }   // never a screen with no way out
        v.visible = true; v.locked = true; v.text = "Restarting"; v.progress = 1.0f;
        break;   // an update that needs no restart: carry on into the launcher
    case Phase::Failed:   // the existing Failed state (reason + Retry) is shown by the update panel; the screen steps aside
    case Phase::UpToDate:
    case Phase::Idle:
        break;
    }
    return v;
}

// What to tell the person when the update check failed before anything was installed: the updater's own words, except for the plain
// "no network" case which gets the short offline line. (An install that failed is explained by the update dialog instead.)
inline std::string FailureToast(const std::string& upMsg) {
    if (upMsg.empty() || upMsg.rfind("Couldn't reach", 0) == 0) return "Could not check for updates (offline)";
    return upMsg;
}

// ---- A download that stops getting data must not hold the locked screen for ever ----
inline constexpr long long kDownloadStallMs = 60 * 1000;           // no new bytes for this long = stalled
inline constexpr long long kDownloadMinCapMs = 10 * 60 * 1000;     // the hard cap is at least this ...
inline constexpr long long kDownloadSlowestBytesPerSec = 20 * 1024; // ... and allows a connection this slow for the whole file

inline long long DownloadCapMs(long long totalBytes) {
    const long long bySize = totalBytes > 0 ? totalBytes * 1000 / kDownloadSlowestBytesPerSec : 0;
    return bySize > kDownloadMinCapMs ? bySize : kDownloadMinCapMs;
}
// Give up on the download now? (`sinceProgressMs`: since the file last grew; `elapsedMs`: since it started; `totalBytes` 0 = unknown.)
inline bool DownloadShouldAbort(long long sinceProgressMs, long long elapsedMs, long long totalBytes) {
    return sinceProgressMs >= kDownloadStallMs || elapsedMs >= DownloadCapMs(totalBytes);
}
inline constexpr const char* kDownloadStalledReason = "The download stopped (no data for a minute), so the update was not installed and your current version is unchanged. Check your connection, then use Retry.";

// ---- Which failures count toward "failed 2 times, so it is not tried again automatically" ----
enum class FailKind { Release, ProgramsRunning, Network };   // the release itself did not install / the person had the editor or a game open / the connection stalled
inline bool CountsTowardAutoRetry(FailKind k) { return k == FailKind::Release; }

inline constexpr const char* kRestartGaveUpText = "Update installed. Close and reopen OkaySpace to finish.";

} // namespace okay::startupupdate
