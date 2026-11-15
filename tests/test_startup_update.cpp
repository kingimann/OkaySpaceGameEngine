// Decisions of the start-up update screen (shared/StartupUpdate.hpp). Prints a line per failure and exits 1 if any failed.
#include "StartupUpdate.hpp"
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

    std::printf(g_fail ? "startup_update: %d FAILED\n" : "startup_update: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
