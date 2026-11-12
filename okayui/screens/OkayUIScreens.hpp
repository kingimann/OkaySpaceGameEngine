#pragma once
// Real screens drawn with OkayUI, ported from the Dear ImGui ones so the launcher and the editor can offer them as the
// opt-in "OkayUI (preview)" UI backend (docs/own_ui_toolkit.md, section 5). A screen is a plain function: it takes the
// data to show and a rectangle, draws with OkayUI, and either returns what the person asked for (WhatsNew) or does it
// through callbacks the host supplies (Console). No SDL, no Dear ImGui, no globals of its own besides OkayUI's context,
// so the headless harness drives them exactly as the apps do.
//
// Both draw into a bare OkayUI window (OkayUI::BareNextWindow) that fills the rectangle, so the host only has to
// reserve the rectangle in its own layout and call BeginFrame / EndFrame around the frame.
#include "okay/UI/OkayUI.hpp"
#include "okay/Debug/ConsoleModel.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace okayuiscreens {

// ---- What's New (the launcher's page of that name) -------------------------------------------------------------------

enum class UpdateState {
    Idle,        // nothing known yet: updates are checked automatically
    Checking,    // a check or a download is running (`message` is its progress text, may be empty)
    Available,   // `availableVersion` is ready to install
    Installed,   // the update is installed; `restartNeeded` says whether the launcher must restart
    Failed,      // `message` says why
    UpToDate     // the latest version is the one running
};

struct Release {
    std::string version;                 // "v2.859.0"
    std::vector<std::string> notes;      // one bullet each
};

struct WhatsNewModel {
    std::string currentVersion;          // "2.860.0" (shown as "You're on v...")
    UpdateState state = UpdateState::Idle;
    std::string availableVersion;
    std::string message;
    bool restartNeeded = false;
    std::vector<Release> releases;       // newest first; the first one carries the "Latest" pill
    bool placeholder = false;            // `releases` is the built-in highlights, not fetched notes: say so under them
};

/// What the person did this frame, and where the controls are (screen pixels, for the host and for tests).
struct WhatsNewResult {
    bool checkForUpdates = false;        // "Check for updates" pressed
    bool installNow = false;             // "Install now" pressed
    bool restartNow = false;             // "Restart now" pressed
    bool openReleasesPage = false;       // "View all releases on GitHub" pressed
    OkayUI::Rect updateButton;           // whichever of the three update buttons is showing
    OkayUI::Rect releasesButton;
    OkayUI::Rect firstEarlierHeader;     // the first "Earlier releases" header, zero when there are none
    bool updateButtonEnabled = true;     // false while a check or download runs
};

/// The page's title line, shown above the content. Kept as text so the host and the tests agree on it.
inline const char* WhatsNewTitle() { return "What's New"; }
inline const char* WhatsNewSubtitle() { return "Release notes for OkaySpace, newest first."; }
inline const char* WhatsNewReleasesUrl() { return "https://github.com/kingimann/OkaySpaceGameEngine/releases"; }

/// Draw the page inside (x, y, w, h). Call between OkayUI::BeginFrame and EndFrame.
WhatsNewResult DrawWhatsNew(const WhatsNewModel& model, float x, float y, float w, float h);

// ---- Console (the editor's panel of that name) ---------------------------------------------------------------------------

/// What the person has set in the panel; the host keeps one for the panel's lifetime.
struct ConsoleViewState {
    char filter[96] = "";                // search text
    bool autoScroll = true;
    bool collapse = false;               // collapse repeated lines
    bool showInfo = true, showWarn = true, showError = true;
    std::uint64_t selectedSeq = ~0ull;   // the selected entry, ~0 = none
    int category = 0;                    // index into the category list (0 = All)
    std::string preferCategory;          // when set, the next frame selects this category if it exists
};

/// What the host owns or does. Every member is optional; a missing callback hides or disables its control and the doc
/// says so, nothing is faked.
struct ConsoleHost {
    bool* clearOnPlay = nullptr;         // "Clear on Play" checkbox (hidden when null)
    bool* pauseOnError = nullptr;        // "Pause on error" checkbox (hidden when null)
    std::function<void(const std::string&)> copyText;                 // clipboard; "Copy" buttons need it
    std::function<void(const std::string&)> saveLog;                  // gets the whole log text; "Save to console_log.txt"
    std::function<void(const okay::ConsoleSource&)> activateSource;   // a row with a source was clicked
    std::function<void(const std::string& path, int line)> openScript;   // "Open Script"
    std::function<void()> cleared;                                    // after "Clear"
};

struct ConsoleResult {
    OkayUI::Rect clearButton, searchField, categoryCombo, warnToggle, errorToggle, collapseToggle, autoScrollToggle;
    OkayUI::Rect copyAllButton, saveButton, logArea, firstRow;
    OkayUI::Rect clearOnPlayToggle, pauseOnErrorToggle;   // zero when the host does not offer them
    OkayUI::Rect detailsCopyButton, openScriptButton;   // zero when the selected entry has none
    int rowsShown = 0;                   // rows submitted this frame (after filter and collapse)
    int rowsDrawn = 0;                   // rows actually laid out (the rest are off screen)
    std::uint64_t firstDrawnSeq = ~0ull, lastDrawnSeq = ~0ull;   // the entries at the top and bottom of the visible part
    std::string saveHint;                // the file name "Save" writes, for the label
};

/// "HH:MM:SS" for a model time in seconds.
std::string ConsoleClockText(double sec);

/// Draw the panel inside (x, y, w, h) over `model` (its filter and collapse settings are set from `state`).
ConsoleResult DrawConsole(okay::ConsoleModel& model, ConsoleViewState& state, const ConsoleHost& host,
                          float x, float y, float w, float h);

} // namespace okayuiscreens
