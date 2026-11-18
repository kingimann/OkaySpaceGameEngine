#pragma once
// ScriptingDensity: how roomy the two scripting windows are. The Visual Script window (node graph, palette, events list)
// and the Script editor (code, tabs, script list, problems) read their sizes and paddings from ONE table here, so a pane
// never keeps a private number. Three steps: Compact (about what the windows used to be), Comfortable (the default, roomier)
// and Spacious. The choice is kept in okay_settings.txt (key "scriptdensity", see ViewPrefs in EditorLayoutMath.hpp).
//
// Pure maths and data, no ImGui, so tests/test_scripting_density.cpp covers it. Written from scratch for this engine; the
// numbers are our own choices (pixel sizes at 100% canvas zoom), the width rules are plain "the widest part wins" layout.
#include <algorithm>
#include <functional>
#include <string>

namespace oked::scriptui {

constexpr int kDefaultDensity = 1;   // Comfortable

inline int DensityCount() { return 3; }
inline int ClampDensity(int d) { return d <= 0 ? 0 : (d >= 2 ? 2 : d); }
inline const char* DensityName(int d) {
    switch (ClampDensity(d)) { case 0: return "Compact"; case 1: return "Comfortable"; default: return "Spacious"; }
}
inline const char* DensityHint(int d) {
    switch (ClampDensity(d)) {
        case 0:  return "Tight spacing: the most on screen.";
        case 1:  return "Relaxed spacing (default).";
        default: return "Large rows, bigger buttons and more air.";
    }
}

struct Size2 { float x = 0, y = 0; };

/// Every number the two windows read. Pixels, except where a name says otherwise.
struct Metrics {
    // Shared by both windows: the look of buttons, menu rows, tab strips and window edges.
    Size2 framePad;          // inside a button / field (sets its height)
    Size2 itemSpacing;       // between controls
    Size2 windowPad;         // inside a panel
    Size2 tabPad;            // inside a tab of the tab strip
    float smallHit = 0;      // the smallest clickable square (fold chevrons, star, close icons)

    // Script editor (code).
    float codeLineSpacing = 0;   // line height as a multiple of the font height
    int   codeLeftMargin = 0;    // gutter width in the code widget
    float codeZoom = 0;          // starting code zoom (1.0 = 16 px text)
    float minimapW = 0;
    float sideListW = 0;         // the script list on the left
    float sideFoldW = 0;         // the same list folded to a strip
    float sideRowPad = 0;        // above and below the text of one list row
    float problemsRowPad = 0;    // same, in the Problems list
    float findPadY = 0;          // vertical padding of the Find bar's fields
    float sideRowH(float textH) const { return textH + 2.0f * sideRowPad; }

    // Visual Script: the node graph at 100% canvas zoom.
    float nodeW = 0;         // narrowest a node gets
    float nodeMaxW = 0;      // a title longer than this is cut with "..."
    float headerH = 0;
    float rowH = 0;          // one pin row
    float pinR = 0;
    float pinHit = 0;        // side of the square you can grab a pin by (at least this, whatever the zoom)
    float boxW = 0;          // the inline value box on an unconnected input
    float nodePad = 0;       // inside the node, left and right
    float pinLabelGap = 0;   // between an input label and its value box
    float round = 0;         // corner radius
    float outlineW = 0;      // the Events list beside the graph

    // Visual Script: the add-node palette.
    float paletteSideW = 0;          // the topic list
    Size2 paletteSideItemSpacing;    // between topic rows
    Size2 paletteSideFramePad;
    float paletteSideRowPad = 0;     // extra height added to each topic row
    float paletteRowPad = 0;         // above and below a node row
    Size2 paletteFilterPad;          // the level filter buttons (All / Beginner ...)
};

/// The table. Compact keeps the sizes the windows had before this setting existed; the others grow from there.
inline Metrics ComputeMetrics(int density) {
    Metrics m;
    switch (ClampDensity(density)) {
    case 0:
        m.framePad = {10, 6};  m.itemSpacing = {6, 6};  m.windowPad = {12, 10}; m.tabPad = {12, 6};  m.smallHit = 22;
        m.codeLineSpacing = 1.45f; m.codeLeftMargin = 20; m.codeZoom = 1.10f; m.minimapW = 84;
        m.sideListW = 260; m.sideFoldW = 76; m.sideRowPad = 6; m.problemsRowPad = 6; m.findPadY = 5;
        m.nodeW = 292; m.nodeMaxW = 460; m.headerH = 64; m.rowH = 40; m.pinR = 9;  m.pinHit = 26; m.boxW = 136;
        m.nodePad = 16; m.pinLabelGap = 8;  m.round = 12; m.outlineW = 268;
        m.paletteSideW = 262; m.paletteSideItemSpacing = {4, 2}; m.paletteSideFramePad = {6, 3}; m.paletteSideRowPad = 2;
        m.paletteRowPad = 6; m.paletteFilterPad = {6, 3};
        break;
    case 1:
        m.framePad = {14, 9};  m.itemSpacing = {10, 8}; m.windowPad = {16, 14}; m.tabPad = {16, 10}; m.smallHit = 26;
        m.codeLineSpacing = 1.60f; m.codeLeftMargin = 26; m.codeZoom = 1.25f; m.minimapW = 96;
        m.sideListW = 300; m.sideFoldW = 84; m.sideRowPad = 8; m.problemsRowPad = 8; m.findPadY = 8;
        m.nodeW = 316; m.nodeMaxW = 500; m.headerH = 70; m.rowH = 44; m.pinR = 10; m.pinHit = 30; m.boxW = 148;
        m.nodePad = 18; m.pinLabelGap = 10; m.round = 12; m.outlineW = 300;
        m.paletteSideW = 300; m.paletteSideItemSpacing = {6, 4}; m.paletteSideFramePad = {8, 5}; m.paletteSideRowPad = 4;
        m.paletteRowPad = 8; m.paletteFilterPad = {8, 5};
        break;
    default:
        m.framePad = {18, 12}; m.itemSpacing = {12, 11}; m.windowPad = {20, 18}; m.tabPad = {20, 12}; m.smallHit = 30;
        m.codeLineSpacing = 1.80f; m.codeLeftMargin = 32; m.codeZoom = 1.40f; m.minimapW = 110;
        m.sideListW = 340; m.sideFoldW = 92; m.sideRowPad = 10; m.problemsRowPad = 10; m.findPadY = 11;
        m.nodeW = 348; m.nodeMaxW = 560; m.headerH = 78; m.rowH = 50; m.pinR = 11.5f; m.pinHit = 34; m.boxW = 164;
        m.nodePad = 22; m.pinLabelGap = 12; m.round = 14; m.outlineW = 340;
        m.paletteSideW = 340; m.paletteSideItemSpacing = {8, 6}; m.paletteSideFramePad = {10, 7}; m.paletteSideRowPad = 6;
        m.paletteRowPad = 10; m.paletteFilterPad = {10, 7};
        break;
    }
    return m;
}

// ---- Node width ------------------------------------------------------------------------------------------------------

/// What one node has to fit. Text widths are in screen pixels (already measured at the canvas zoom); `zoom` scales the
/// paddings and the base width.
struct NodeWidthIn {
    float zoom = 1.0f;
    float titleW = 0;       // the node's title (or its one-line step text)
    float badgeW = 0;       // the status badge ("Ready" / "Missing" / "Error") drawn at the right end of the title row
    bool  hasDataIn = false;
    float inLabelW = 0;     // widest input label
    float outLabelW = 0;    // widest output label (data or flow)
    float boxW = 0;         // the value box, already scaled
};

/// The width of a node: the base, or wider so that the title and its badge do not overlap, or so that input labels, the
/// value box and output labels sit side by side on one row. A very long title is limited to `nodeMaxW` (the caller
/// elides it); the label row is never limited, because overlapping labels are worse than a wide node.
inline float NodeWidth(const Metrics& m, const NodeWidthIn& in) {
    const float z = in.zoom > 0.0f ? in.zoom : 1.0f;
    const float base = m.nodeW * z, pad = m.nodePad * z, maxW = m.nodeMaxW * z;
    float w = base;
    const float titleNeed = in.titleW + (in.badgeW > 0.0f ? in.badgeW + pad * 0.5f : 0.0f) + 2.0f * pad;
    w = std::max(w, std::min(titleNeed, maxW));
    if (in.hasDataIn)
        w = std::max(w, pad + in.inLabelW + m.pinLabelGap * z + in.boxW + pad + in.outLabelW + pad);
    return w;
}

/// Half-gap added between a long title and the badge is part of the rule above; the badge itself is drawn by the caller.
inline float PinHitSize(const Metrics& m, float zoom) { return std::max(m.pinHit, 1.23f * m.pinHit * (zoom > 0 ? zoom : 1.0f)); }

// ---- Eliding ---------------------------------------------------------------------------------------------------------

/// Cut `text` so that it fits `maxW`, ending in "...". `measure` returns the width of a string. Text that fits comes back
/// unchanged; when not even "..." fits the result is empty. Never cuts through a UTF-8 character.
inline std::string ElideToWidth(const std::string& text, float maxW, const std::function<float(const std::string&)>& measure) {
    if (text.empty()) return text;
    if (measure(text) <= maxW) return text;
    std::string out = text;
    while (!out.empty()) {
        out.pop_back();
        while (!out.empty() && ((unsigned char)out.back() & 0xC0) == 0x80) out.pop_back();   // drop a half-cut character
        if (out.empty()) break;
        const std::string cand = out + "...";
        if (measure(cand) <= maxW) return cand;
    }
    return measure("...") <= maxW ? std::string("...") : std::string();
}

// ---- The choice, shared by the two windows -----------------------------------------------------------------------------

inline int  g_scriptDensity = kDefaultDensity;   // what the windows read this frame
inline bool g_scriptDensityChanged = false;      // set by a menu; the host saves the settings once and clears it

/// The numbers for the current choice.
inline Metrics Current() { return ComputeMetrics(g_scriptDensity); }

/// Pick a density (clamped). Flags a save only when it really changed.
inline void SetDensity(int d) {
    d = ClampDensity(d);
    if (d == g_scriptDensity) return;
    g_scriptDensity = d;
    g_scriptDensityChanged = true;
}
/// The host asks once per frame: true when the choice changed since the last ask (it then writes the settings file).
inline bool TakeDensityChanged() { const bool c = g_scriptDensityChanged; g_scriptDensityChanged = false; return c; }

} // namespace oked::scriptui
