// Decisions of the start-up update screen (shared/StartupUpdate.hpp). Prints a line per failure and exits 1 if any failed.
#include "StartupUpdate.hpp"
#include "LauncherDemo.hpp"
#include <string>
#include <vector>
#include <cstdlib>
#include <cstdio>
using namespace okay::startupupdate;

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

int main() {
    StartInputs in; Skip why;
    CHECK(ShouldRunStartupCheck(in, &why) && why == Skip::No);                       // default: on
    in = {}; in.settingOn = false; CHECK(!ShouldRunStartupCheck(in, &why) && why == Skip::SettingOff);
    in = {}; in.envValue = "0"; CHECK(!ShouldRunStartupCheck(in, &why) && why == Skip::EnvOff);
    in = {}; in.envValue = "1"; CHECK(ShouldRunStartupCheck(in));                    // only "0" switches it off
    in = {}; in.demoMode = true; CHECK(!ShouldRunStartupCheck(in, &why) && why == Skip::DemoMode);
    in = {}; in.editorRunning = true; CHECK(!ShouldRunStartupCheck(in, &why) && why == Skip::EditorRunning);
    in = {}; in.verdictShown = true; CHECK(!ShouldRunStartupCheck(in, &why) && why == Skip::VerdictShown);
    in = {}; in.msSinceLastStartupCheck = 5000; CHECK(!ShouldRunStartupCheck(in, &why) && why == Skip::RecentCheck);
    in = {}; in.msSinceLastStartupCheck = kRecentCheckMs; CHECK(ShouldRunStartupCheck(in));
    in = {}; in.msSinceLastStartupCheck = -1; CHECK(ShouldRunStartupCheck(in));

    ScreenInputs s; ScreenView v;
    s.phase = Phase::Checking; s.elapsedMs = 500; v = EvaluateScreen(s);
    CHECK(v.visible && v.canSkip && !v.locked && v.text == "Checking for updates");
    s.skipped = true; v = EvaluateScreen(s); CHECK(!v.visible);                      // Skip hides it
    s.skipped = false; s.elapsedMs = kCheckTimeoutMs - 1; CHECK(EvaluateScreen(s).visible);
    s.elapsedMs = kCheckTimeoutMs; v = EvaluateScreen(s); CHECK(!v.visible && v.giveUpNow);   // offline / slow: go on, say so
    s.timedOutShown = true; v = EvaluateScreen(s); CHECK(!v.visible && !v.giveUpNow);          // ...only once

    s = {}; s.phase = Phase::Downloading; s.step = 1; s.steps = 3; s.elapsedMs = 60000; v = EvaluateScreen(s);
    CHECK(v.visible && !v.canSkip && v.locked && v.text == "Downloading 2 of 3");  // still up after the timeout, no Skip
    s.skipped = true; CHECK(EvaluateScreen(s).visible);                              // a stale Skip cannot hide an install
    s = {}; s.phase = Phase::Downloading; s.installing = true; v = EvaluateScreen(s);
    CHECK(v.visible && !v.canSkip && v.locked && v.text == "Installing");
    s = {}; s.phase = Phase::Downloading; s.step = 9; s.steps = 3; CHECK(EvaluateScreen(s).text == "Downloading 3 of 3");
    s = {}; s.phase = Phase::Downloading; s.steps = 0; CHECK(EvaluateScreen(s).text == "Downloading 1 of 1");
    s = {}; s.phase = Phase::Updated; s.relaunchNeeded = true; v = EvaluateScreen(s);
    CHECK(v.visible && v.locked && !v.canSkip && v.text == "Restarting");
    s.relaunchNeeded = false; CHECK(!EvaluateScreen(s).visible);
    for (Phase p : {Phase::Idle, Phase::UpToDate, Phase::Failed}) { s = {}; s.phase = p; CHECK(!EvaluateScreen(s).visible); }

    // The restart must never leave a full-screen locked panel (finding: antivirus / spawn failure / editor open).
    s = {}; s.phase = Phase::Updated; s.relaunchNeeded = true; s.restartFailed = true; v = EvaluateScreen(s);
    CHECK(!v.visible && !v.locked && v.restartGaveUp);
    s = {}; s.phase = Phase::Updated; s.relaunchNeeded = true; s.sinceUpdatedMs = kRestartGiveUpMs - 1; CHECK(EvaluateScreen(s).visible);
    s.sinceUpdatedMs = kRestartGiveUpMs; v = EvaluateScreen(s); CHECK(!v.visible && !v.locked && v.restartGaveUp);
    s = {}; s.phase = Phase::Updated; s.relaunchNeeded = false; s.restartFailed = true; CHECK(!EvaluateScreen(s).restartGaveUp);

    // The failure toast says the real reason, not always "offline".
    CHECK(FailureToast("Couldn't reach the update server (offline?).") == "Could not check for updates (offline)");
    CHECK(FailureToast("") == "Could not check for updates (offline)");
    CHECK(FailureToast("The update to v2.9.0 failed 2 times, so it is not tried again automatically.") == "The update to v2.9.0 failed 2 times, so it is not tried again automatically.");

    // Demo modes: ANY OKAY_UI_DEMO_* or OKAY_*_DEMO* variable.
    for (const char* n : {"OKAY_UI_DEMO_USER", "OKAY_UI_DEMO_TAB", "OKAY_UI_DEMO_ACVIEW", "OKAY_CHAT_DEMO", "OKAY_MP_DEMO", "OKAY_MP_DEMO_TAB",
                          "OKAY_VOICE_DEMO", "OKAY_VOICE_DEMO_SERVER", "OKAY_UI_DEMO_UPDATE", "OKAY_OA_DEMO_FOLDER"})
        CHECK(okay::IsLauncherDemoMode(std::vector<std::string>{"PATH", n}));
    CHECK(!okay::IsLauncherDemoMode(std::vector<std::string>{}));
    CHECK(!okay::IsLauncherDemoMode(std::vector<std::string>{"PATH", "HOME", "OKAY_UPDATE_ON_LAUNCH", "OKAY_UPDATE_SERVER", "MY_DEMO", "DEMO", "OKAY_DEMOLITION"}));
    CHECK(!okay::IsLauncherDemoMode(std::vector<std::string>{"OKAY_UI_BACKEND"}));
#ifndef _WIN32
    { setenv("OKAY_ZZ_DEMO_TEST", "1", 1); CHECK(okay::IsLauncherDemoMode()); unsetenv("OKAY_ZZ_DEMO_TEST"); }   // the real environment is scanned
#endif

    // A stalled or endless download must give up (the locked screen needs an exit).
    CHECK(!DownloadShouldAbort(0, 1000, 30000000));
    CHECK(!DownloadShouldAbort(kDownloadStallMs - 1, 5000, 30000000));
    CHECK(DownloadShouldAbort(kDownloadStallMs, 5000, 30000000));                       // no data for a minute
    CHECK(DownloadCapMs(0) == kDownloadMinCapMs);
    CHECK(DownloadCapMs(1000) == kDownloadMinCapMs);
    CHECK(DownloadCapMs(100LL * 1024 * 1024) > kDownloadMinCapMs);                       // a big file gets more time
    CHECK(!DownloadShouldAbort(0, DownloadCapMs(100LL * 1024 * 1024) - 1, 100LL * 1024 * 1024));
    CHECK(DownloadShouldAbort(0, DownloadCapMs(100LL * 1024 * 1024), 100LL * 1024 * 1024));   // the hard cap, even while trickling
    CHECK(DownloadShouldAbort(0, kDownloadMinCapMs, 0));

    // Only a release that really failed counts toward "failed 2 times, so it is not tried again automatically".
    CHECK(CountsTowardAutoRetry(FailKind::Release));
    CHECK(!CountsTowardAutoRetry(FailKind::ProgramsRunning));
    CHECK(!CountsTowardAutoRetry(FailKind::Network));

    std::printf(g_fail ? "startup_update: %d FAILED\n" : "startup_update: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
