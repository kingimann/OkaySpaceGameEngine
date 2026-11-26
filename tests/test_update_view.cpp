// The launcher's Software Update dialog and start-up screen, as a view of the updater's state (launcher/UpdateView.hpp): each
// state's title, buttons and lines; "12.4 of 35.0 MB (35%)", the speed and the time left; failures in plain words with the
// technical text kept for Details; "Checked 5 min ago"; and the dialog's size at 960 x 600 and 1280 x 800, UI scale 1.0 and 1.5.
#include "test_framework.hpp"
#include "UpdateView.hpp"

#include <string>

namespace U = okay::updateview;

int main() {
    RUN_SUITE("update_view");
    U::Inputs in; in.fromVersion = "2.903.9"; in.toVersion = "2.904.0";
    in.phase = U::Phase::Checking;
    { const U::View v = U::Evaluate(in); CHECK(v.title == "Checking for updates"); CHECK(v.file == -2.0f); CHECK(v.primary == U::Button::Hide); }
    in.phase = U::Phase::Available;
    { const U::View v = U::Evaluate(in); CHECK(v.primary == U::Button::InstallNow && v.secondary == U::Button::Later); CHECK(v.sub.find("v2.904.0") == 0); CHECK(v.tertiary == U::Button::Skip); }
    in.testTag = "Test build of walk-vs-run (abc1234)";
    { const U::View v = U::Evaluate(in); CHECK(v.title == "A test build is ready"); CHECK(v.sub.find("walk-vs-run") != std::string::npos); }
    in.testTag.clear();
    // "Skip this version" (owner, 2026-11-26): the third choice next to Install now / Later, a test build too; a version already skipped is not
    // offered for skipping again and says so; Ask and "you already have everything" notes are shown in the line under the title.
    in.phase = U::Phase::Available;
    { const U::View v = U::Evaluate(in); CHECK(v.tertiary == U::Button::Skip); CHECK(std::string(U::ButtonLabel(U::Button::Skip)) == "Skip this version"); }
    in.testTag = "Test build of main (abc1234)";
    { const U::View v = U::Evaluate(in); CHECK(v.tertiary == U::Button::Skip); }
    in.testTag.clear();
    in.skipped = true;
    { const U::View v = U::Evaluate(in); CHECK(v.tertiary == U::Button::None); CHECK(v.primary == U::Button::InstallNow); CHECK(v.sub.find("skipped") != std::string::npos); }
    in.skipped = false;
    in.note = "Your test build may already have some of it.";
    { const U::View v = U::Evaluate(in); CHECK(v.sub.find("may already have") != std::string::npos); }
    in.phase = U::Phase::UpToDate; in.note = "You already have everything in v2.904.0 (your test build is newer).";
    { const U::View v = U::Evaluate(in); CHECK(v.sub == in.note); CHECK(v.tertiary == U::Button::None); }
    in.note.clear();
    // The phase the dialog shows: a version that was skipped gives no Available phase on its own (no dialog, no badge), a newer one does,
    // and a check the person started shows the skipped version again (marked skipped), so it can be installed after all.
    CHECK(U::AvailablePhaseFor("2.904.0", "2.904.0", false) == U::Shown::No);
    CHECK(U::AvailablePhaseFor("2.904.1", "2.904.0", false) == U::Shown::Yes);
    CHECK(U::AvailablePhaseFor("2.904.0", "", false) == U::Shown::Yes);
    CHECK(U::AvailablePhaseFor("2.904.0", "v2.904.0", true) == U::Shown::Skipped);
    CHECK(U::AvailablePhaseFor("2.904.0.7", "2.904.0.7", false) == U::Shown::No);          // a test build is skipped by its version the same way
    CHECK(U::AvailablePhaseFor("2.904.0.8", "2.904.0.7", false) == U::Shown::Yes);
    CHECK(U::AvailablePhaseFor("", "2.904.0", true) == U::Shown::No);
    // The What's New card and the Versions page say the skipped version is there (the way back).
    CHECK(U::SkippedLine("2.904.0") == "v2.904.0 is available (skipped)");
    CHECK(U::SkippedLine("").empty());
    // The three buttons fit side by side in the normal dialog, and stack when it is narrow.
    { const U::DialogBox d = U::LayoutDialog(1280, 800, 560, 20, 260, true, 80, 320, 110, 90, 8, 24, 140);
      CHECK(!d.stackButtons); }
    { const U::DialogBox d = U::LayoutDialog(360, 800, 560, 20, 260, true, 80, 320, 110, 90, 8, 24, 140);
      CHECK(d.stackButtons); }
    in.phase = U::Phase::Downloading; in.step = "Downloading editor (OkayEngine)"; in.stepIndex = 1; in.stepCount = 3;
    in.bytesNow = 13002342; in.bytesTotal = 36700160; in.secondsInStep = 4.0;
    {
        const U::View v = U::Evaluate(in);
        CHECK(v.stepLine == "Downloading editor (OkayEngine) (2 of 3)");
        CHECK(v.bytesLine == "12.4 of 35.0 MB  (35%)");
        CHECK(v.speedLine == "3.1 MB/s");
        CHECK(v.etaLine.find("left") != std::string::npos);
        CHECK(v.file > 0.35f && v.file < 0.36f);
        CHECK(v.overall > 0.44f && v.overall < 0.46f);   // one file of three done, the second a third through
    }
    in.bytesTotal = 0;
    { const U::View v = U::Evaluate(in); CHECK(v.file == -2.0f); CHECK(v.bytesLine == "12.4 MB"); CHECK(v.etaLine.empty()); }
    in.phase = U::Phase::Verifying;
    { const U::View v = U::Evaluate(in); CHECK(v.title == "Checking the download"); }
    in.phase = U::Phase::Ready; in.restartNeeded = true;
    { const U::View v = U::Evaluate(in); CHECK(v.title == "Ready to restart"); CHECK(v.primary == U::Button::RestartNow && v.secondary == U::Button::RestartLater); }
    in.phase = U::Phase::UpToDate; in.lastCheckedUnix = 1000; in.nowUnix = 1000 + 300;
    { const U::View v = U::Evaluate(in); CHECK(v.checked == "Checked 5 min ago"); CHECK(v.secondary == U::Button::CheckAgain); }
    in.phase = U::Phase::Failed;
    for (const char* raw : {"Update refused. versions.json has no SHA-256 for OkayEngine.exe", "Couldn't reach the update server (offline?).",
                            "Update failed. Could not write OkayEngine.exe: No space left on device (disk full)", "Update failed. HTTP 503"}) {
        in.error = raw;
        const U::View v = U::Evaluate(in);
        CHECK(v.primary == U::Button::TryAgain);
        CHECK(v.details == raw);                                    // the technical text, for Details
        CHECK(v.error.find("HTTP") == std::string::npos && v.error.find("SHA") == std::string::npos && v.error.find("json") == std::string::npos);
    }
    CHECK(U::PlainFailure("no SHA-256 checksum").find("safety check") != std::string::npos);
    CHECK(U::PlainFailure("Couldn't reach the update server").find("internet") != std::string::npos);
    CHECK(U::PlainFailure("disk full").find("disk space") != std::string::npos);
    CHECK(U::Duration(42) == "42 s"); CHECK(U::Duration(125) == "2 min 05 s");
    // The dialog fits the window at the small and normal sizes, UI scale 1.0 and 1.5.
    for (float s : {1.0f, 1.5f})
        for (const auto& wh : {std::pair<float, float>{960, 600}, std::pair<float, float>{1280, 800}}) {
            const float fixedH = 260.0f * s;   // header, step, bars, buttons
            const U::DialogBox d = U::LayoutDialog(wh.first, wh.second, 560.0f * s, 20.0f * s, fixedH, true, 80.0f * s, 320.0f * s,
                                                   110.0f * s, 90.0f * s, 8.0f * s, 24.0f * s);
            CHECK(d.w <= wh.first - 40.0f * s + 0.5f);
            CHECK(d.h <= wh.second - 40.0f * s + 0.5f || d.notesH <= 80.0f * s);   // the notes give way first
            CHECK(d.notesH >= 0.0f);
            if (s == 1.0f) CHECK(!d.stackButtons);
        }
    TEST_MAIN_RESULT();
}
