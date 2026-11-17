#pragma once
// The data six editor panels show, taken out of the editor so that both of their versions can run without the editor:
// the Dear ImGui version (editor/EditorPanelViews.cpp) and the OkayUI port (okayui/screens/OkayUIEditorPanels.cpp).
//
//   Problems        everything wrong in the open scene, grouped by object, asset or script (editor/ProblemsPanel.cpp builds it)
//   Stats           frame rate, its history and the scene census, plus the culling and batching switches (editor/main.cpp)
//   History         the undo and redo stacks as a list of states (editor/main.cpp)
//   Tags and Layers Project Settings: the tag list, Strict tags and the 32 layer names (editor/main.cpp)
//   Physics         Project Settings: gravity, the step rate, smoothing and the open scene's physics quality (editor/main.cpp)
//   Profiler        the frame timeline, CPU categories, the diagnosis and the per-section table (editor/main.cpp)
//   Import settings the selected texture's, model's or sound's import settings, Apply and Revert (editor/ProjectPanelUI.cpp)
//   Post-processing Lighting: the look preset, tonemapping, bloom, colour grading, vignette, FXAA, SSAO, depth of field (editor/main.cpp)
//   Player, Rendering  Project Settings: company, version and window size; ambient light and the skybox (editor/main.cpp)
//
// Standard C++ only: no ImGui, no OkayUI, no engine types. The editor fills a model from its own state each frame, a panel
// version draws it and answers with an intent (what the person asked for), and the editor carries the intent out. The rules
// both versions must agree on (the filter, the grouping, the number format, the row order) live here, once.
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace okaypanels {

// ============================================================================================================================
// Problems
// ============================================================================================================================

enum ProblemSeverity : int { ProblemInfo = 0, ProblemWarning = 1, ProblemError = 2 };

struct ProblemRow {
    std::uint64_t id = 0;
    int severity = ProblemError;            // ProblemSeverity
    std::string code, message, hint;
    std::string objectPath, objectGuid, nodeId;
    std::string group;                      // the object path, script, asset or graph it belongs to ("Scene" when none)
    bool canFix = false;                    // a one-click repair exists
};

struct ProblemsModel {
    std::vector<ProblemRow> rows;
    int errors = 0, warnings = 0, infos = 0;   // totals over every row (the pills show them whatever the filter)
};

/// What the person set on the panel; the host keeps one for the panel's lifetime.
struct ProblemsState {
    char filter[96] = "";
    bool showErr = true, showWarn = true, showInfo = false;
};

/// What the person asked for this frame (indexes into ProblemsModel::rows, -1 = nothing).
struct ProblemsIntent {
    int open = -1;          // a click on a row, or "Go to source" in its right-click menu: select the object, open the script
    int fix = -1;           // "Fix", or "Fix" in the right-click menu
    int copy = -1;          // "Copy message" in the right-click menu: the host puts ProblemCopyText(row) on the clipboard
};

struct ProblemGroup {
    std::string name;
    std::vector<int> rows;                  // indexes into ProblemsModel::rows, in display order
    int errors = 0, warnings = 0;
};

inline const char* ProblemSeverityName(int s) {
    return s == ProblemError ? "Error" : s == ProblemWarning ? "Warning" : "Info";
}

/// Text colour of a row, 0..1.
inline void ProblemSeverityColor(int s, float rgb[3]) {
    if (s == ProblemError)        { rgb[0] = 0.96f; rgb[1] = 0.45f; rgb[2] = 0.42f; }
    else if (s == ProblemWarning) { rgb[0] = 0.95f; rgb[1] = 0.80f; rgb[2] = 0.35f; }
    else                          { rgb[0] = 0.70f; rgb[1] = 0.78f; rgb[2] = 0.88f; }
}

inline std::string ProblemLower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

/// The rows that pass the filter, grouped. Rows are ordered by severity (errors first) and then by group, keeping the model's
/// order otherwise; groups come in the order of their first row; the same problem twice (code, message, object, node) is shown once.
inline std::vector<ProblemGroup> GroupProblems(const ProblemsModel& m, const ProblemsState& st) {
    std::vector<int> order((std::size_t)m.rows.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = (int)i;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        const ProblemRow& x = m.rows[(std::size_t)a];
        const ProblemRow& y = m.rows[(std::size_t)b];
        if (x.severity != y.severity) return x.severity > y.severity;
        return x.group < y.group;
    });
    std::string needle(st.filter, std::find(st.filter, st.filter + sizeof(st.filter), '\0'));
    needle = ProblemLower(needle);
    std::vector<ProblemGroup> groups;
    std::unordered_map<std::string, std::size_t> at;
    std::unordered_set<std::string> seen;
    for (int i : order) {
        const ProblemRow& d = m.rows[(std::size_t)i];
        const int sev = d.severity == ProblemError || d.severity == ProblemWarning ? d.severity : ProblemInfo;   // anything else is a note
        if (sev == ProblemError && !st.showErr) continue;
        if (sev == ProblemWarning && !st.showWarn) continue;
        if (sev == ProblemInfo && !st.showInfo) continue;
        if (!needle.empty()) {
            const std::string hay = ProblemLower(d.message + " " + d.code + " " + d.objectPath + " " + d.hint);
            if (hay.find(needle) == std::string::npos) continue;
        }
        if (!seen.insert(d.code + "\n" + d.message + "\n" + d.objectGuid + "\n" + d.nodeId).second) continue;
        const std::string g = d.group.empty() ? std::string("Scene") : d.group;
        auto it = at.find(g);
        if (it == at.end()) { it = at.emplace(g, groups.size()).first; groups.push_back(ProblemGroup{g, {}, 0, 0}); }
        ProblemGroup& pg = groups[it->second];
        pg.rows.push_back(i);
        if (sev == ProblemError) ++pg.errors;
        else if (sev == ProblemWarning) ++pg.warnings;
    }
    return groups;
}

/// The dim line above a group: its name, how many rows, and "error" or "warning" when it has one.
inline std::string ProblemGroupHeader(const ProblemGroup& g) {
    char tail[32];
    std::snprintf(tail, sizeof(tail), "    %d%s", (int)g.rows.size(), g.errors ? "  error" : g.warnings ? "  warning" : "");
    return g.name + tail;
}

inline std::string ProblemRowText(const ProblemRow& d) { return std::string(ProblemSeverityName(d.severity)) + "   " + d.message; }

/// The tooltip of a row: the message, the hint and the code, a blank line between each.
inline std::string ProblemRowTooltip(const ProblemRow& d) {
    std::string tip = d.message;
    if (!d.hint.empty()) tip += "\n\n" + d.hint;
    if (!d.code.empty()) tip += "\n\n" + d.code;
    return tip;
}

/// What "Copy message" puts on the clipboard.
inline std::string ProblemCopyText(const ProblemRow& d) {
    std::string s = std::string(ProblemSeverityName(d.severity)) + ": " + d.message;
    if (!d.group.empty()) s = d.group + ": " + s;
    if (!d.hint.empty()) s += "\n" + d.hint;
    if (!d.code.empty()) s += " [" + d.code + "]";
    return s;
}

inline const char* ProblemsEmptyText(const ProblemsModel& m) {
    return m.errors + m.warnings + m.infos == 0 ? "Nothing is wrong in this scene." : "Nothing matches this filter.";
}

// ============================================================================================================================
// Stats
// ============================================================================================================================

struct StatCounter { std::string name; int value = 0; };

/// The frame-rate history: the last 120 frames, a ring.
struct FpsHistory {
    static constexpr int kCount = 120;
    float values[kCount] = {};
    int head = 0;                           // the next slot to write; also the oldest value
    void Push(float fps) { values[head] = fps; head = (head + 1) % kCount; }
};

struct StatsModel {
    float framerate = 0.0f;                 // frames per second now
    const FpsHistory* history = nullptr;
    std::vector<StatCounter> counters;      // shown two to a row
    bool* frustumCulling = nullptr;         // the four switches, edited in place (null: not shown)
    bool* occlusionCulling = nullptr;
    bool* staticBatching = nullptr;
    bool* dynamicBatching = nullptr;
    bool gpuSkinning = false;
};

/// What the person asked for this frame.
struct StatsIntent {
    bool changed = false;                   // a switch was flipped (already written through its pointer)
    bool copy = false;                      // "Copy stats" in the census's right-click menu: StatsCopyText goes to the clipboard
};

struct FpsSummary { float min = 0.0f, max = 0.0f, avg = 0.0f, scale = 75.0f; int count = 0; };

/// min, max and average over the history slots that hold a frame (> 0), and the plot's top: 20% above the highest, at least 75.
inline FpsSummary SummarizeFps(const FpsHistory& h) {
    FpsSummary s; float sum = 0.0f; s.min = 1e9f;
    for (float v : h.values) if (v > 0.0f) { s.min = std::min(s.min, v); s.max = std::max(s.max, v); sum += v; ++s.count; }
    if (!s.count) s.min = 0.0f;
    s.avg = s.count ? sum / (float)s.count : 0.0f;
    s.scale = s.max * 1.2f < 75.0f ? 75.0f : s.max * 1.2f;
    return s;
}

/// The colour of the big number: green from 55 FPS, amber from 30, red below.
inline void FpsColor(float fps, float rgb[3]) {
    if (fps >= 55.0f)      { rgb[0] = 0.45f; rgb[1] = 0.85f; rgb[2] = 0.55f; }
    else if (fps >= 30.0f) { rgb[0] = 0.95f; rgb[1] = 0.78f; rgb[2] = 0.35f; }
    else                   { rgb[0] = 0.96f; rgb[1] = 0.45f; rgb[2] = 0.42f; }
}

/// A census number: 12, 12.3k from 10,000, 1.2M from a million.
inline void FormatStatNumber(int v, char* out, std::size_t n) {
    if (v >= 1000000)    std::snprintf(out, n, "%.1fM", v / 1e6);
    else if (v >= 10000) std::snprintf(out, n, "%.1fk", v / 1e3);
    else                 std::snprintf(out, n, "%d", v);
}

inline std::string StatsCopyText(const StatsModel& m) {
    char line[96];
    std::snprintf(line, sizeof(line), "FPS %.0f (%.2f ms)\n", m.framerate, m.framerate > 0 ? 1000.0f / m.framerate : 0.0f);
    std::string s = line;
    for (const StatCounter& c : m.counters) { std::snprintf(line, sizeof(line), "%s: %d\n", c.name.c_str(), c.value); s += line; }
    return s;
}

inline const char* kOcclusionTip = "Skip meshes hidden behind an Occluder. Off, or with no Occluder\nin the scene, nothing extra is hidden.";
inline const char* kBatchingTip = "Skip meshes the camera (or the sun's shadow map) can't see.\nTurn off only to check whether culling hides something it shouldn't.";
inline const char* kStatsFooter = "Sky, lighting, post FX and debug view moved to the Lighting tab.";

// ============================================================================================================================
// History (undo and redo)
// ============================================================================================================================

struct HistoryModel {
    std::vector<std::string> undoNames;     // most recent first; may be shorter than undoDepth
    std::vector<std::string> redoNames;     // the next redo first
    int undoDepth = 0, redoDepth = 0;
};

enum HistoryKind : int { HistoryAhead = -1, HistoryCurrent = 0, HistoryBehind = 1 };

struct HistoryRow {
    std::string label;                      // "After: Move", "Current state", "Before: Rename"
    int kind = HistoryCurrent;              // HistoryKind
    int steps = 0;                          // redos (ahead) or undos (behind) to get there
};

/// The rows top to bottom: future states newest-forward first, then the current state, then the past, most recent first.
inline std::vector<HistoryRow> HistoryRows(const HistoryModel& m) {
    std::vector<HistoryRow> rows;
    for (int i = m.redoDepth; i >= 1; --i)
        rows.push_back({"After: " + ((std::size_t)i <= m.redoNames.size() ? m.redoNames[(std::size_t)i - 1] : std::string("Edit")), HistoryAhead, i});
    rows.push_back({"Current state", HistoryCurrent, 0});
    for (int i = 1; i <= m.undoDepth; ++i)
        rows.push_back({"Before: " + ((std::size_t)i <= m.undoNames.size() ? m.undoNames[(std::size_t)i - 1] : std::string("Edit")), HistoryBehind, i});
    return rows;
}

/// What the person asked for this frame. The host applies `undo` undos then `redo` redos (one of them is zero).
struct HistoryIntent {
    int undo = 0, redo = 0;
    int copy = -1;                          // "Copy name" in a row's right-click menu: the row index into HistoryRows
};

inline std::string HistoryStatusText(const HistoryModel& m) {
    char b[64];
    std::snprintf(b, sizeof(b), "%d behind  \xE2\x80\xA2  %d ahead", m.undoDepth, m.redoDepth);
    return b;
}

inline const char* kHistoryEmpty = "No edits yet \xE2\x80\x94 changes will appear here.";

// ============================================================================================================================
// Tags and Layers (Project Settings)
// ============================================================================================================================

constexpr int kLayerCount = 32;
constexpr std::size_t kTagBufSize = 64 * 4 + 1;       // okay::Tags::kMaxLength characters of up to four bytes, and the terminator
constexpr std::size_t kLayerBufSize = 32 * 4 + 1;     // okay::Layers::kMaxNameLength likewise

struct TagRow { std::string name; int used = 0; };   // a custom tag and how many objects of the open scene carry it
struct LayerRow {
    std::string name;                   // the layer's name ("" = unnamed)
    std::string defaultName;            // its built-in name ("" for most)
};

struct TagsLayersModel {
    bool strict = false;                // Tags::Strict
    std::vector<std::string> builtin;   // always there, cannot be renamed or removed
    std::vector<TagRow> custom;
    std::vector<std::string> unlisted;  // tags the open scene uses that the list lacks
    std::vector<LayerRow> layers;       // kLayerCount of them
};

/// What the panel keeps between frames. The host writes the error lines after it carried out an intent.
struct TagsLayersState {
    char newTag[kTagBufSize] = "";      // the "new tag name" field
    char renameBuf[kTagBufSize] = "";   // the open rename popup's field
    int renaming = -1;                  // the custom tag whose rename popup is open
    bool closeRename = false;           // the host renamed it: the popup closes on the next frame
    std::string tagError, renameError, layerError;
    int layerErrorAt = -1;
    char layerBuf[kLayerCount][kLayerBufSize] = {};   // the layer fields' text while they are edited (the OkayUI version)
};

/// What the person asked for this frame (-1 = nothing). The host validates, saves the project and sets the errors in the state.
struct TagsLayersIntent {
    int strict = -1;                    // 0 or 1: set Strict tags
    bool addUnlisted = false;           // "Add them": put every unlisted tag on the list
    bool addTag = false;                // "Add tag" or Enter: add state.newTag (clear it and the error on success)
    int removeTag = -1;                 // a custom tag's Remove
    int renameTag = -1;                 // a custom tag's rename popup: rename it to renameTo
    std::string renameTo;
    int renameLayer = -1;               // a layer field committed (Enter, or the field left after an edit): rename it to layerName
    std::string layerName;
    int resetLayer = -1;                // a layer's Reset: back to its default name
    bool resetAllLayers = false;
};

inline const char* kStrictTagsTip =
    "On: only tags on this list can be given to an object (the Inspector, scripts and SetTag refuse the rest),\nso a typo such as \"Enmey\" is caught. Off: any text is a valid tag.";
inline const char* kRemoveTagTip = "Take it off the list. Objects that carry it keep it.";

/// "2 tags in this scene are not on the list: Enemy, Boss" (the first four names, then "...").
inline std::string UnlistedTagsText(const std::vector<std::string>& unlisted) {
    std::string names;
    for (std::size_t i = 0; i < unlisted.size() && i < 4; ++i) names += (i ? ", " : "") + unlisted[i];
    if (unlisted.size() > 4) names += ", ...";
    const bool one = unlisted.size() == 1;
    return std::to_string(unlisted.size()) + " tag" + (one ? "" : "s") + " in this scene " + (one ? "is" : "are") + " not on the list: " + names;
}
inline std::string BuiltinTagsText(const std::vector<std::string>& builtin) {
    std::string b;
    for (const std::string& t : builtin) b += (b.empty() ? "" : "  ") + t;
    return "Built in: " + b;
}
inline std::string TagUseText(int used) { return std::to_string(used) + " object" + (used == 1 ? "" : "s"); }
/// The hint a layer field shows while empty: its built-in name, or "Layer N".
inline std::string LayerHint(int i, const LayerRow& r) { return r.defaultName.empty() ? "Layer " + std::to_string(i) : r.defaultName; }
/// A layer has a Reset button when it is named and the name is not its built-in one.
inline bool LayerCanReset(const LayerRow& r) { return !r.name.empty() && r.name != r.defaultName; }
inline std::string LayerErrorText(const TagsLayersState& st) { return "Layer " + std::to_string(st.layerErrorAt) + ": " + st.layerError; }
/// Copy `src` into a fixed buffer, cut at a whole UTF-8 character.
inline void CopyToBuf(char* dst, std::size_t n, const std::string& src) {
    if (n == 0) return;
    std::size_t k = std::min(src.size(), n - 1);
    while (k > 0 && k < src.size() && ((unsigned char)src[k] & 0xC0) == 0x80) --k;
    std::copy(src.begin(), src.begin() + (std::ptrdiff_t)k, dst);
    dst[k] = '\0';
}

// ============================================================================================================================
// Physics (Project Settings)
// ============================================================================================================================

struct PhysicsSettingsModel {
    float* gravity2D = nullptr;         // float[2], edited in place
    float* gravity3D = nullptr;         // float[3], edited in place
    float* stepsPerSecond = nullptr;    // edited in place (the host clamps it, ClampStepsPerSecond)
    bool* smooth = nullptr;             // edited in place
    int quality = 1;                    // the open scene's: 0 Classic, 1 Improved
    int solverIterations = 10;          // shown when Improved
    bool autoContinuous = true;         // shown when Improved
};

struct PhysicsSettingsIntent {
    bool gravityChanged = false;        // a gravity cell changed (written through its pointer)
    bool rateChanged = false;           // steps per second changed (written; the host clamps, applies and saves)
    bool smoothChanged = false;         // the smoothing box (written; the host applies and saves)
    int quality = -1;                   // 0 or 1: set the scene's physics quality
    int solverIterations = -1;          // 4..30: set the solver iterations
    int autoContinuous = -1;            // 0 or 1
    bool apply = false;                 // "Apply to current scene"
};

inline const char* const kPhysicsQualityNames[2] = {"Classic (the original step: old scenes)", "Improved (stacks stand, bodies sleep, fast bodies stop)"};
inline const char* kStepRateTip = "How many times a second physics runs. 50 is the usual rate.\nA drop lands in the same place at a low frame rate and a high one.\nSaved with the project.";
inline const char* kSmoothTip = "Draw each body part way between its last two physics steps,\nso motion stays smooth when the frame rate and the step rate differ.";
inline const char* kQualityTip = "Classic plays old scenes exactly as they always played.\nImproved solves contacts several times a step and remembers them, so crates stack and stay put,\nresting bodies sleep together, fast bodies cannot pass through thin walls and balls roll to a stop.\nNew scenes start Improved. The setting is part of the scene file.";
inline const char* kIterationsTip = "How many times the contact solver repeats each step. 10 is enough for towers up to about 6 crates;\nraise it for taller stacks (costs time).";
inline const char* kContinuousTip = "A body that moves farther in one step than its own size is checked along its path against walls and floors,\nso a bullet cannot pass through a thin wall without ticking Continuous Collision.";
inline const char* kPhysicsQualityHeading = "Physics quality of the open scene (saved with the scene)";

/// The step rate the editor accepts: 1 to 500 a second (anything else, NaN included, is pulled into that range; NaN is 50).
inline float ClampStepsPerSecond(float hz) { return !(hz == hz) ? 50.0f : hz < 1.0f ? 1.0f : hz > 500.0f ? 500.0f : hz; }

/// Dear ImGui's default item width: 65% of the window (a panel's body plus the editor's 16-pixel padding on each side).
inline float DefaultItemWidth(float bodyWidth) { return std::floor((bodyWidth + 32.0f) * 0.65f); }

// ============================================================================================================================
// Profiler
// ============================================================================================================================

struct ProfilerSection {
    std::string name;
    double self = 0.0, inclusive = 0.0;  // ms
    int calls = 0, depth = 0;
    double min = 0.0, avg = 0.0, max = 0.0;   // self ms over the recorded window
};

struct ProfilerModel {
    bool* paused = nullptr;              // edited in place (null: not shown)
    int* selected = nullptr;             // the scrubbed frame, edited in place (-1 = the latest)
    std::vector<float> total, gpu;       // ms per frame, oldest first (the same length)
    double lastFrameMs = 0.0, avg = 0.0, peak = 0.0;
    // the frame shown (the latest, or the scrubbed one while paused)
    double frameMs = 0.0, cpuMs = 0.0, gpuMs = 0.0;
    int meshes = 0, triangles = 0, particles = 0;
    double scriptsMs = 0.0, physicsMs = 0.0, renderMs = 0.0;   // the CPU categories' self time
    std::vector<ProfilerSection> sections;                       // the shown frame's sections, in the profiler's order
    bool playing = false;
};

struct ProfilerIntent {
    bool pauseChanged = false;           // written through `paused`
    bool selectChanged = false;          // written through `selected`
    bool copyCsv = false;                // "Copy CSV": ProfilerCsv goes to the clipboard
};

inline const char* kProfilerHelp =
    "Self ms  = time spent in this section, NOT counting nested sections.\n"
    "Total ms = time including everything it called (indented = nested).\n"
    "%        = share of this frame's CPU.\n"
    "Calls    = how many times it ran this frame.\n"
    "Avg/Max  = over the recorded window.\n"
    "Red rows are 'hot' (>30% of CPU) \xE2\x80\x94 start optimising there.";
inline const char* const kProfilerColumns[7] = {"Section", "Self ms", "Total ms", "%", "Calls", "Avg", "Max"};

/// The top of the timeline graphs: 15% above the peak, or 33.4 ms (30 FPS) before there is one.
inline float ProfilerGraphTop(const ProfilerModel& m) { return (float)(m.peak > 0.0 ? m.peak * 1.15 : 33.4); }
inline int ProfilerFrameCount(const ProfilerModel& m) { return (int)std::min(m.total.size(), m.gpu.size()); }
/// While running the scrub position is the latest frame (-1); once paused it starts at the newest frame and stays in range.
inline void ProfilerFixSelection(ProfilerModel& m) {
    if (!m.selected) return;
    const int n = ProfilerFrameCount(m);
    if (!m.paused || !*m.paused) { *m.selected = -1; return; }
    if (*m.selected < 0 || *m.selected >= n) *m.selected = n - 1;
}
inline double ProfilerOtherMs(const ProfilerModel& m) {
    const double o = m.cpuMs - m.scriptsMs - m.physicsMs - m.renderMs;
    return o > 0.0 ? o : 0.0;
}
inline std::string ProfilerOverlay(const ProfilerModel& m) {
    char b[96]; std::snprintf(b, sizeof(b), "%.2f ms   %.0f FPS", m.lastFrameMs, m.lastFrameMs > 0.001 ? 1000.0 / m.lastFrameMs : 0.0);
    return b;
}
/// "Scripts 1.20 ms (35%)" and the bar's fill (the share of this frame's CPU).
inline std::string ProfilerBarText(const char* label, double ms, double cpu, float* frac) {
    const float f = cpu > 0.0001 ? (float)(ms / cpu) : 0.0f;
    if (frac) *frac = f;
    char t[64]; std::snprintf(t, sizeof(t), "%s %.2f ms (%.0f%%)", label, ms, f * 100.0f);
    return t;
}
/// The plain-language diagnosis under the bars.
inline std::string ProfilerDiagnosis(const ProfilerModel& m) {
    const double sc = m.scriptsMs, ph = m.physicsMs, rd = m.renderMs, ot = ProfilerOtherMs(m);
    const char* cat = "Scripts"; double cm = sc;
    if (ph > cm) { cm = ph; cat = "Physics"; }
    if (rd > cm) { cm = rd; cat = "Render"; }
    if (ot > cm) { cm = ot; cat = "Other (engine/UI)"; }
    std::string top; double hs = 0.0;
    for (const ProfilerSection& s : m.sections) if (s.self > hs) { hs = s.self; top = s.name; }
    std::string d;
    if (m.frameMs < 0.001) d = "Waiting for frames...";
    else if (m.gpuMs > m.cpuMs * 1.3) d = "GPU-bound: rendering dominates. Lower resolution/AA, fewer triangles, or simpler shaders.";
    else if (m.cpuMs > m.gpuMs * 1.3) d = "CPU-bound: " + std::string(cat) + " is heaviest" + (top.empty() ? "" : " (top: " + top + ").");
    else d = "Balanced CPU/GPU.";
    if (m.frameMs > 0.001) {
        const double fps = 1000.0 / m.frameMs;
        if (fps >= 58) d += "  Running smoothly (60+ FPS).";
        else if (fps >= 28) d += "  ~30 FPS.";
        else d += "  Below 30 FPS \xE2\x80\x94 optimise the heaviest section above.";
    }
    return d;
}
/// The table's rows: the sections by self time, heaviest first (ties keep the profiler's order).
inline std::vector<int> ProfilerRows(const ProfilerModel& m) {
    std::vector<int> r(m.sections.size());
    for (std::size_t i = 0; i < r.size(); ++i) r[i] = (int)i;
    std::stable_sort(r.begin(), r.end(), [&](int a, int b) { return m.sections[(std::size_t)a].self > m.sections[(std::size_t)b].self; });
    return r;
}
/// A hot section (red): more than 30% of a frame that took more than half a millisecond of CPU.
inline bool ProfilerHot(const ProfilerModel& m, const ProfilerSection& s) { return s.self > m.cpuMs * 0.30 && m.cpuMs > 0.5; }
/// The seven cells of a row: name, self, total, %, calls, avg, max.
inline void ProfilerCells(const ProfilerModel& m, const ProfilerSection& s, char cells[6][24]) {
    const float frac = m.cpuMs > 0.0001 ? (float)(s.self / m.cpuMs) : 0.0f;
    std::snprintf(cells[0], 24, "%.3f", s.self);
    std::snprintf(cells[1], 24, "%.3f", s.inclusive);
    std::snprintf(cells[2], 24, "%.0f%%", frac * 100.0f);
    std::snprintf(cells[3], 24, "%d", s.calls);
    std::snprintf(cells[4], 24, "%.3f", s.avg);
    std::snprintf(cells[5], 24, "%.3f", s.max);
}
inline std::string ProfilerCsv(const ProfilerModel& m) {
    std::string csv = "Section,Self ms,Total ms,Calls,Avg ms,Min ms,Max ms\n";
    for (const ProfilerSection& s : m.sections) {
        char line[256];
        std::snprintf(line, sizeof(line), "%s,%.4f,%.4f,%d,%.4f,%.4f,%.4f\n", s.name.c_str(), s.self, s.inclusive, s.calls, s.avg, s.min, s.max);
        csv += line;
    }
    return csv;
}
inline const char* ProfilerEmptyText(const ProfilerModel& m) {
    return m.playing ? "Collecting samples..." : "Press Play to time Scripts/Physics; the editor render is always timed.";
}

// ============================================================================================================================
// Import settings (the Project panel's inspector for a texture, a model or a sound)
// ============================================================================================================================

enum ImportKind : int { ImportNone = 0, ImportTexture = 1, ImportModel = 2, ImportAudio = 3 };

/// The values the panel edits, copied from and back to the engine's okay::ImportSettings by the host (editor/ProjectPanelUI.cpp).
struct ImportFields {
    int kind = ImportNone;
    int maxSize = 2048;                  // texture: 0 (no limit) or one of kImportSizes
    int filter = 2, wrap = 0;            // texture: Point, Bilinear, Trilinear; Repeat, Clamp, Mirror
    bool mipmaps = true;
    float scale = 1.0f;                  // model
    int normals = 0;                     // model: from the file, calculated (smooth), flat
    float smoothingAngle = 60.0f;        // model, for calculated normals
    bool importAnimations = true;
    int loadMode = 0;                    // audio: load the whole file, read it while it plays
    bool forceMono = false;
};

struct ImportSettingsModel {
    ImportFields* edit = nullptr;        // edited in place
    ImportFields original;               // what is saved (Apply and Revert are enabled while edit differs from it)
    bool newer = false;                  // written by a newer editor: shown, not changed
    bool showSampler = false;            // texture filter, wrap and mipmaps (hidden while the Direct3D renderers do not read them)
};

/// What the panel keeps between frames: the message of a failed Apply. The host sets it (and showFail) when saving fails.
struct ImportSettingsState {
    std::string failMsg;
    bool showFail = false;               // open the failure popup on the next frame
};

struct ImportSettingsIntent {
    bool changed = false;                // a value changed (written through edit)
    bool apply = false;                  // "Apply": save edit next to the asset
    bool revert = false;                 // "Revert": edit back to original
};

inline const int kImportSizes[11] = {0, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384};
inline const char* const kImportSizeNames[11] = {"No limit", "32", "64", "128", "256", "512", "1024", "2048", "4096", "8192", "16384"};
inline const char* const kImportFilterNames[3] = {"Point (pixels)", "Bilinear (smooth)", "Trilinear (smooth + mips)"};
inline const char* const kImportWrapNames[3] = {"Repeat", "Clamp", "Mirror"};
inline const char* const kImportNormalsNames[3] = {"From the file", "Calculate (smooth)", "Flat (faceted)"};
inline const char* const kImportLoadNames[2] = {"Load the whole file", "Read it while it plays"};
inline const char* kImportHeading = "IMPORT SETTINGS";
inline const char* kImportNewerNote = "Made by a newer editor. Shown, but not changed here.";
inline const char* kImportSavedNote = "Saved next to the asset.";

/// The "Largest side" list's row for a size (an unknown size shows as 2048, the default).
inline int ImportSizeIndex(int maxSize) {
    for (int i = 0; i < 11; ++i) if (kImportSizes[i] == maxSize) return i;
    return 7;
}
/// The same settings as far as the panel shows them (the fields of the asset's kind).
inline bool ImportSame(const ImportFields& a, const ImportFields& b) {
    if (a.kind != b.kind) return false;
    switch (a.kind) {
        case ImportTexture: return a.maxSize == b.maxSize && a.filter == b.filter && a.wrap == b.wrap && a.mipmaps == b.mipmaps;
        case ImportModel: return a.scale == b.scale && a.normals == b.normals && a.smoothingAngle == b.smoothingAngle && a.importAnimations == b.importAnimations;
        case ImportAudio: return a.loadMode == b.loadMode && a.forceMono == b.forceMono;
        default: return true;
    }
}

// ============================================================================================================================
// Post-processing (the Lighting window's first section)
// ============================================================================================================================

/// The open scene's post-processing values (scene render settings), copied in and back by the host.
struct PostValues {
    bool filmic = true;                  // Filmic Rendering (linear lighting + tonemap)
    float exposure = 1.0f;
    int tonemap = 1;                     // None, ACES, Filmic
    bool bloom = false;
    float bloomThreshold = 1.0f, bloomIntensity = 0.6f, bloomRadius = 1.0f, bloomSoftness = 0.5f;
    bool grading = false;
    float temperature = 0.0f, tint = 0.0f, saturation = 1.0f, contrast = 1.0f;
    float lift[3] = {0, 0, 0}, gamma[3] = {0, 0, 0}, gain[3] = {0, 0, 0};
    float vignette = 0.0f, vignetteSmooth = 0.5f;
    bool fxaa = false, ssao = false;
    float ssaoRadius = 0.5f, ssaoIntensity = 1.0f, ssaoPower = 1.0f;
    bool dof = false;
    float dofFocus = 10.0f, dofRange = 5.0f;
};

struct PostProcessModel {
    PostValues* v = nullptr;             // edited in place
    std::vector<std::string> presets;    // the look presets ("Sunset", "Night", ...)
    bool chainActive = false;            // the GPU post chain runs (okay::PostActive)
    std::string vignetteNote, ssaoNote, dofNote;   // what the current renderer does not draw ("" = nothing to say)
};

struct PostProcessIntent {
    bool changed = false;                // a value changed (written through v): the scene is dirty
    int preset = -1;                     // a look preset was chosen (index into presets)
    bool reset = false;                  // "Reset": the host pushes an undo step and resets the look
};

inline const char* kLookPresetTip = "Ready-made looks: fills the post-processing, sky, fog and weather settings\n(Sunset / Night / Foggy morning / Storm also aim the sun light).";
inline const char* kLookResetTip = "Post-processing off, Colour sky, no fog, no weather. The sky gradient colours are kept.";
inline const char* kFilmicTip = "Gamma-correct pipeline: lighting in linear space, a tonemap roll-off and sRGB output.\nOn by default; off gives the legacy flat response (and no tonemap curve).";
inline const char* const kTonemapNames[3] = {"None", "ACES", "Filmic"};
inline const char* kTonemapTip = "How highlights above white roll off (needs Filmic Rendering).\nACES = punchy, Filmic = a softer shoulder, None = clip.";
inline const char* kBloomThresholdTip = "Brightness a pixel must exceed to glow (with Filmic Rendering, 1 = white).";
inline const char* kBloomSoftnessTip = "Softens the edge of the Threshold, so glow fades in instead of popping on.\n0 = the hard cut (the old look). OpenGL and Direct3D 11.";
inline const char* kTemperatureTip = "Cool (blue) to warm (orange).";
inline const char* kLiftTip = "Raises or lowers the blacks, per channel.";
inline const char* kGammaTip = "Brightens or darkens the mid-tones, per channel.";
inline const char* kGainTip = "Scales the whites, per channel.";
inline const char* kVignetteTip = "Darken the frame's edges and corners. 0 = off. Drawn by the post chain when it runs,\notherwise as an overlay that looks the same on every renderer.";
inline const char* kSsaoPowerTip = "Shapes the shadow. 1 = the old look. Higher: only deep creases get dark, edges stay crisp.\nLower: a wider, lighter shadow.";
inline const char* kDofRangeTip = "How far either side of the focus distance stays sharp.";
inline const char* kPostRendererNote = "Linear lighting is not drawn on Direct3D 12 or the software renderer. Exposure, the tonemap curve, bloom, colour grading and anti-aliasing in this section are not drawn on Direct3D 12. The vignette amount is drawn on every renderer. The software renderer also has its own switches under Rendering below; the scene settings here take effect there too.";
inline const char* PostChainText(bool active) { return active ? "GPU post chain: on" : "GPU post chain: off (the frame is untouched)"; }

// ============================================================================================================================
// Player and Rendering (Project Settings)
// ============================================================================================================================

constexpr std::size_t kProjectTextSize = 64;   // ProjectSettings::company and ::version are char[64]

struct PlayerSettingsModel {
    char* company = nullptr;             // char[companySize], edited in place
    std::size_t companySize = kProjectTextSize;
    char* version = nullptr;             // char[versionSize], edited in place
    std::size_t versionSize = kProjectTextSize;
    int* width = nullptr;                // the default window size, edited in place
    int* height = nullptr;
};

struct PlayerSettingsIntent { bool changed = false; };

struct RenderingSettingsModel {
    float* ambient = nullptr;            // 0..1, edited in place
    bool* skybox = nullptr;              // edited in place
};

struct RenderingSettingsIntent {
    bool changed = false;
    bool apply = false;                  // "Apply to current scene"
};

} // namespace okaypanels

// The Hierarchy panel (its own header, std only as well).
#include "HierarchyPanelModel.hpp"
