#pragma once
// The data three editor panels show, taken out of the editor so that both of their versions can run without the editor:
// the Dear ImGui version (editor/EditorPanelViews.cpp) and the OkayUI port (okayui/screens/OkayUIEditorPanels.cpp).
//
//   Problems   everything wrong in the open scene, grouped by object, asset or script (editor/ProblemsPanel.cpp builds it)
//   Stats      frame rate, its history and the scene census, plus the culling and batching switches (editor/main.cpp)
//   History    the undo and redo stacks as a list of states (editor/main.cpp)
//
// Standard C++ only: no ImGui, no OkayUI, no engine types. The editor fills a model from its own state each frame, a panel
// version draws it and answers with an intent (what the person asked for), and the editor carries the intent out. The rules
// both versions must agree on (the filter, the grouping, the number format, the row order) live here, once.
#include <algorithm>
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

} // namespace okaypanels
