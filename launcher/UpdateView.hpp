// UpdateView.hpp - what the Software Update dialog and the start-up screen show for the updater's state: the title, the line
// under it, the progress (overall and for the file being downloaded), "12.4 of 35.0 MB", the speed and the time left, the
// buttons, and a failure in plain words with the technical text kept for a "Details" fold. UI only: the update logic itself
// (RunUpdateCheck and the updater core) is not changed. Our own code, pure, tested headless (tests/test_update_view.cpp).
#pragma once
#include "okay/Core/StringUtil.hpp"   // okay::str::LowerAscii
#include <algorithm>
#include <cstdio>
#include <string>

namespace okay::updateview {

enum class Phase { Idle, Checking, Available, Downloading, Verifying, Ready, Installed, UpToDate, Failed };

struct Inputs {
    Phase phase = Phase::Idle;
    std::string fromVersion, toVersion, testTag;   // "2.903.9", "2.904.0", "Test build of walk-vs-run (abc1234)" or ""
    std::string step;                              // the updater's step line ("Downloading editor (OkayEngine)")
    int stepIndex = 0, stepCount = 0;              // 0-based index of the step running, of how many
    long long bytesNow = 0, bytesTotal = 0;        // the file being downloaded (total 0 = not known)
    double secondsInStep = 0;                      // how long this file has been downloading
    std::string error;                             // the updater's message when it failed
    long long lastCheckedUnix = 0, nowUnix = 0;    // "Checked 5 min ago"
    bool restartNeeded = false;
    bool skipped = false;                          // toVersion is the version the person skipped (shown again because they checked)
    std::string note;                              // one plain sentence from the update check (why it asks, "you already have everything in ...")
};

enum class Button { None, InstallNow, Later, Hide, Cancel, RestartNow, RestartLater, TryAgain, Close, CheckAgain, Skip };

struct View {
    std::string title, sub, stepLine, bytesLine, speedLine, etaLine, error, details, checked;
    float overall = -1.0f;    // 0..1, -1 = no bar
    float file = -1.0f;       // 0..1 for the current file, -2 = moving bar (size not known), -1 = no bar
    Button primary = Button::None, secondary = Button::None;
    Button tertiary = Button::None;   // drawn left of the other two ("Skip this version")
};

inline std::string MB(long long b) { char t[32]; std::snprintf(t, sizeof(t), "%.1f MB", (double)b / (1024.0 * 1024.0)); return t; }
inline std::string Duration(double s) {
    char t[32];
    if (s < 1) return "a moment";
    if (s < 60) { std::snprintf(t, sizeof(t), "%d s", (int)s); return t; }
    if (s < 3600) { std::snprintf(t, sizeof(t), "%d min %02d s", (int)s / 60, (int)s % 60); return t; }
    std::snprintf(t, sizeof(t), "%d h %02d min", (int)s / 3600, ((int)s / 60) % 60); return t;
}
inline std::string Ago(long long then, long long now) {
    if (then <= 0) return "";
    const long long d = now > then ? now - then : 0;
    char t[48];
    if (d < 60) return "Checked just now";
    if (d < 3600) { std::snprintf(t, sizeof(t), "Checked %lld min ago", d / 60); return t; }
    if (d < 86400) { std::snprintf(t, sizeof(t), "Checked %lld h ago", d / 3600); return t; }
    std::snprintf(t, sizeof(t), "Checked %lld days ago", d / 86400); return t;
}

/// The updater's failure in plain words (what happened and what to do); the original text goes to Details.
inline std::string PlainFailure(const std::string& raw) {
    const std::string e = okay::str::LowerAscii(raw);
    auto has = [&](const char* k) { return e.find(k) != std::string::npos; };
    if (has("checksum") || has("sha-256") || has("sha256"))
        return "The update didn't pass its safety check, so nothing was installed. Try again later; your current version is unchanged.";
    if (has("reach") || has("offline") || has("network") || has("timed out") || has("timeout"))
        return "Can't reach the update server. Check your internet connection, then try again.";
    if (has("disk") || has("space") || has("no room") || has("enospc"))
        return "There isn't enough free disk space for the update. Free some space, then try again.";
    if (has("in use") || has("running") || has("locked") || has("access"))
        return "A file the update needs is in use. Close the editor and any games, then try again.";
    if (has("paused")) return "Updates are paused after several failed tries. Try again later.";
    return "The update didn't finish. Your current version is unchanged. Try again, or see Details.";
}

inline View Evaluate(const Inputs& in) {
    View v;
    const std::string to = in.toVersion.empty() ? std::string() : "v" + in.toVersion;
    switch (in.phase) {
    case Phase::Idle: case Phase::Checking:
        v.title = "Checking for updates";
        v.sub = in.fromVersion.empty() ? "" : "You have v" + in.fromVersion + ".";
        v.file = -2.0f;
        v.primary = Button::Hide;
        break;
    case Phase::Available:
        v.title = in.testTag.empty() ? "An update is ready" : "A test build is ready";
        v.sub = to + (in.testTag.empty() ? "" : "  \xC2\xB7  " + in.testTag) + (in.fromVersion.empty() ? "" : "  (you have v" + in.fromVersion + ")");
        if (in.skipped) v.sub += "  \xC2\xB7  you skipped this version";
        if (!in.note.empty()) v.sub += ". " + in.note;
        v.primary = Button::InstallNow; v.secondary = Button::Later;
        v.tertiary = in.skipped ? Button::None : Button::Skip;   // Later = ask again next time; Skip = never again for this version
        break;
    case Phase::Downloading: {
        v.title = "Downloading " + (to.empty() ? std::string("the update") : to);
        const int n = std::max(1, in.stepCount), i = std::clamp(in.stepIndex, 0, n - 1);
        char sl[160]; std::snprintf(sl, sizeof(sl), "%s (%d of %d)", in.step.empty() ? "Downloading" : in.step.c_str(), i + 1, n);
        v.stepLine = sl;
        float f = 0.0f;
        if (in.bytesTotal > 0) {
            f = std::clamp((float)((double)in.bytesNow / (double)in.bytesTotal), 0.0f, 1.0f);
            v.file = f;
            char b[64]; std::snprintf(b, sizeof(b), "%.1f of %.1f MB  (%d%%)", (double)in.bytesNow / 1048576.0, (double)in.bytesTotal / 1048576.0, (int)(f * 100.0f));
            v.bytesLine = b;
        } else { v.file = -2.0f; v.bytesLine = MB(in.bytesNow); }
        if (in.secondsInStep > 0.5 && in.bytesNow > 0) {
            const double bps = (double)in.bytesNow / in.secondsInStep;
            char s[48]; std::snprintf(s, sizeof(s), "%.1f MB/s", bps / (1024.0 * 1024.0));
            v.speedLine = s;
            if (in.bytesTotal > in.bytesNow && bps > 1) v.etaLine = Duration((double)(in.bytesTotal - in.bytesNow) / bps) + " left";
        }
        v.overall = std::clamp(((float)i + f) / (float)n, 0.0f, 1.0f);
        v.primary = Button::Hide;
        break;
    }
    case Phase::Verifying:
        v.title = "Checking the download";
        v.sub = "Making sure every file is exactly what was published.";
        v.file = -2.0f;
        v.primary = Button::Hide;
        break;
    case Phase::Ready: case Phase::Installed:
        v.title = in.restartNeeded ? "Ready to restart" : "Update installed";
        v.sub = in.restartNeeded ? "Restart OkaySpace to finish updating to " + (to.empty() ? std::string("the new version") : to) + "."
                                 : (to.empty() ? std::string("You're on the new version.") : "You're on " + to + ".");
        v.overall = 1.0f;
        v.primary = in.restartNeeded ? Button::RestartNow : Button::Close;
        v.secondary = in.restartNeeded ? Button::RestartLater : Button::None;
        break;
    case Phase::UpToDate:
        v.title = "You're up to date";
        v.sub = !in.note.empty() ? in.note : in.fromVersion.empty() ? "" : "OkaySpace v" + in.fromVersion + " is the newest version.";
        v.checked = Ago(in.lastCheckedUnix, in.nowUnix);
        v.primary = Button::Close; v.secondary = Button::CheckAgain;
        break;
    case Phase::Failed:
        v.title = "The update didn't finish";
        v.error = PlainFailure(in.error);
        v.details = in.error;
        v.primary = Button::TryAgain; v.secondary = Button::Close;
        break;
    }
    return v;
}

inline const char* ButtonLabel(Button b) {
    switch (b) {
    case Button::InstallNow: return "Install now";
    case Button::Later: return "Later";
    case Button::Hide: return "Hide";
    case Button::Cancel: return "Cancel";
    case Button::RestartNow: return "Restart now";
    case Button::RestartLater: return "Restart later";
    case Button::TryAgain: return "Try again";
    case Button::Close: return "Close";
    case Button::CheckAgain: return "Check again";
    case Button::Skip: return "Skip this version";
    default: return "";
    }
}

/// The dialog's size in a window `winW` x `winH`: `prefW` wide when it fits (else the window less a margin on each side), the
/// release notes get what is left of the height (at least `minNotesH`, at most `maxNotesH`; 0 = no notes), and the two buttons
/// sit side by side when they fit, else one above the other.
struct DialogBox { float w = 0, notesH = 0; bool stackButtons = false; float h = 0; };
/// `button3W` is "Skip this version", left of the other two (0 = not shown).
inline DialogBox LayoutDialog(float winW, float winH, float prefW, float margin, float fixedH, bool hasNotes, float minNotesH, float maxNotesH,
                              float button1W, float button2W, float gap, float padX, float button3W = 0.0f) {
    DialogBox d;
    d.w = std::min(prefW, std::max(0.0f, winW - 2 * margin));
    const float spare = winH - 2 * margin - fixedH;
    d.notesH = hasNotes ? std::clamp(spare, std::min(minNotesH, std::max(0.0f, spare)), maxNotesH) : 0.0f;
    d.stackButtons = button1W + (button2W > 0 ? gap + button2W : 0.0f) + (button3W > 0 ? gap + button3W : 0.0f) > d.w - 2 * padX;
    d.h = fixedH + d.notesH;
    return d;
}

// ---- "Skip this version" (owner, 2026-11-26) -----------------------------------------------------------------------------------
// The skipped version lives in launcher.cfg (skipped_version, okayversions::Settings::skippedVersion). The update check never installs it by
// itself (okayupdater::DecideOffer); here is what the dialog, the sidebar and What's New show for a version the update check found.

enum class Shown { No, Yes, Skipped };
/// `found`: the newer version the check found. `skipped`: launcher.cfg's skipped version. `personAsked`: the person started this check ("Check
/// for updates"). A skipped version is never shown by itself (no dialog, no badge, no notification); when the person checks it is shown again,
/// marked skipped, so it can still be installed. Any other version, a newer one included, is shown as usual.
inline Shown AvailablePhaseFor(const std::string& found, const std::string& skipped, bool personAsked) {
    if (found.empty()) return Shown::No;
    const std::string f = found[0] == 'v' ? found.substr(1) : found, k = !skipped.empty() && skipped[0] == 'v' ? skipped.substr(1) : skipped;
    if (k.empty() || f != k) return Shown::Yes;
    return personAsked ? Shown::Skipped : Shown::No;
}
/// "v2.904.0 is available (skipped)": the What's New card and the Versions page, with an Install button (the way back). "" for no version.
inline std::string SkippedLine(const std::string& version) {
    if (version.empty()) return {};
    return (version[0] == 'v' ? version : "v" + version) + " is available (skipped)";
}

}  // namespace okay::updateview
