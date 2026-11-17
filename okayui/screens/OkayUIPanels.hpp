#pragma once
// More real screens drawn with OkayUI, picked from docs/imgui_port_report.md (small, self-contained, fully covered by the OkayUI
// API): the launcher's Tips page, the editor's Crash Log window and the editor's Audio Mixer window. Same shape as
// OkayUIScreens.hpp: a plain function takes the data to show (or pointers to the host's own values) and a rectangle, draws into a
// bare OkayUI window that fills it, and reports what the person did through callbacks or the result. No SDL, no Dear ImGui.
#include "okay/UI/OkayUI.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace okayuiscreens {

// ---- Tips (the launcher's page of that name) -------------------------------------------------------------------------------

struct TipEntry { int cat = 0; std::string title, body; };   // cat indexes TipsModel::cats (0 is "All": no tip has it)

struct TipsModel {
    std::vector<std::string> cats;       // "All" first
    std::vector<TipEntry> tips;
    long long dayNumber = 0;             // days since the epoch: picks the tip of the day the first time
};

/// What the person has set on the page; the host keeps one for the page's lifetime.
struct TipsState {
    int cat = 0;                         // selected category pill
    char query[96] = "";                 // search text
    int daily = -1;                      // the tip of the day, -1 = pick from dayNumber
    bool focusSearch = false;            // the host sets this (Ctrl+F); the page clears it once the field has the keyboard
};

struct TipsResult {
    OkayUI::Rect dailyCard, nextTipButton, searchField;
    std::vector<OkayUI::Rect> categoryButtons;
    std::vector<OkayUI::Rect> cards;     // the tip cards drawn this frame (rows scrolled out of view are not laid out)
    std::vector<int> shown;              // indexes into TipsModel::tips that pass the filter, in order
    int columns = 1;
    bool noMatch = false;                // "No tips match" is showing
};

/// The case-insensitive filter the page applies: category (0 = all) and the query against title and body.
bool TipMatches(const TipEntry& tip, int cat, const std::string& query);

/// Draw the page inside (x, y, w, h). Call between OkayUI::BeginFrame and EndFrame.
TipsResult DrawTips(const TipsModel& model, TipsState& state, float x, float y, float w, float h);

// ---- Crash Log (the editor's window of that name) ---------------------------------------------------------------------------

struct CrashLogModel {
    std::string text;                    // every saved report, newest last; empty = none
    std::string path;                    // where they are kept, shown next to the buttons
    std::uint64_t revision = 0;          // the host raises it whenever `text` changed (a reload, a clear): the page re-reads the text then
};

struct CrashLogHost {
    std::function<void()> refresh;                        // "Refresh": reload the files
    std::function<void(const std::string&)> copyAll;      // "Copy All": gets the whole text
    std::function<void()> openFolder;                     // "Open Folder"
    std::function<void()> clear;                          // "Delete" in the confirmation: remove the files
};

struct CrashLogState {
    bool confirming = false;             // the "Clear crash reports?" dialog is open
    // Caches the page keeps between frames (the text split into lines, and the revision it was split at).
    std::uint64_t seenRevision = ~0ull;
    std::vector<std::pair<std::size_t, std::size_t>> lines;   // (offset, length) of each line in the model's text
    bool scrollToEnd = true;             // the newest report is at the bottom: start there, and again after each reload
    float pitch = 0.0f, gap = 0.0f;      // measured row spacing and the gap an item leaves below itself (0 = not measured yet)
    int settle = 0;                      // frames spent on it (the content is measured one frame late, so it takes a few)
};

struct CrashLogResult {
    OkayUI::Rect refreshButton, copyAllButton, openFolderButton, clearButton, body;
    OkayUI::Rect confirmDelete, confirmCancel;   // zero unless the dialog is open
    int lineCount = 0, linesDrawn = 0;
    int firstDrawn = -1, lastDrawn = -1;   // line indexes at the top and bottom of what was laid out
    bool empty = false;                  // "No crash reports" is showing
};

CrashLogResult DrawCrashLog(const CrashLogModel& model, CrashLogState& state, const CrashLogHost& host,
                            float x, float y, float w, float h);

// ---- Audio Mixer (the editor's window of that name) -------------------------------------------------------------------------

/// Pointers into the host's own mixer state: the page edits them in place, like the Dear ImGui version. Null pointers are skipped.
struct AudioMixerModel {
    float peakLeft = 0.0f, peakRight = 0.0f;          // 0..1 linear levels, read only
    float* masterVolume = nullptr;
    bool* masterMuted = nullptr;
    std::vector<std::string> groupNames;
    std::vector<float*> groupVolume;                  // one per group name
    std::vector<bool*> groupMuted;
    bool* duckMusic = nullptr;
    float* duckLevel = nullptr;
    float* duckAttack = nullptr;
    float* duckRelease = nullptr;
    float duckGain = 1.0f;                            // read only: how far the music is ducked right now
};

struct AudioMixerResult {
    OkayUI::Rect leftMeter, rightMeter, masterSlider, masterMute, resetButton, duckToggle;
    std::vector<OkayUI::Rect> groupSliders, groupMutes;
    bool reset = false;                  // "Reset" was pressed (the values are already back to their defaults)
    bool changed = false;                // any slider or toggle changed this frame
};

/// Level in dB for a linear peak, floored at -60 (what the meter tooltip shows).
float MeterDb(float linear);
/// The meter's fill 0..1 for a linear level: -60 dB is empty, 0 dB is full.
float MeterFill(float linear);

AudioMixerResult DrawAudioMixer(AudioMixerModel& model, float x, float y, float w, float h);

// ---- Stats (the editor's window of that name) -------------------------------------------------------------------------------

/// What the window shows: the frame rate and its history (a ring buffer the host owns), the scene counts as name / number rows, and
/// pointers to the host's culling and batching switches (edited in place; null ones are skipped).
struct StatsModel {
    float framerate = 0.0f;                          // frames per second now
    const float* history = nullptr; int historyCount = 0; int historyOffset = 0;   // frame rates, a ring starting at historyOffset; zeros are unused slots
    struct Row { std::string name; int value = 0; };
    std::vector<Row> rows;                           // shown two per line (name, value, name, value)
    bool* frustumCulling = nullptr;
    bool* occlusionCulling = nullptr;
    bool* staticBatching = nullptr;
    bool* dynamicBatching = nullptr;
    std::string skinning = "CPU";                    // "GPU" or "CPU"
};

struct StatsResult {
    OkayUI::Rect fpsReadout, plot, frustum, occlusion, staticBatch, dynamicBatch;
    int fpsBand = 0;                                 // 0 good (55 or more), 1 middling (30 or more), 2 poor
    int cellsDrawn = 0;                              // name / value cells laid out in the grid
    float plotMax = 0.0f;                            // the top of the plot's scale
    bool changed = false;                            // a switch changed this frame
};

/// 0 for 55 fps or more, 1 for 30 or more, 2 below that (the colour of the big readout); a non-finite rate is 2.
int FpsBand(float framerate);
/// Smallest, largest and mean of the used (> 0 and finite) history slots; all zero when there are none.
void HistoryStats(const float* history, int count, float* outMin, float* outMax, float* outAvg);
/// The plot's top: 20 percent above the largest value but never below 75.
float PlotScale(float maxValue);
/// "999", "12.3k" from 10,000 up, "1.5M" from a million (what the grid shows).
std::string FormatCount(int value);

StatsResult DrawStats(const StatsModel& model, float x, float y, float w, float h);

} // namespace okayuiscreens
