#pragma once
// ModelingSidebar: the fold state and width maths of the Modeling window's left page list. Open, each row shows the page's
// icon, its name and a short hint, and the list is as wide as its longest text needs (at least 196 px): text is never cut.
// Folded, it is a thin strip (44 px) of page icons whose names show as tooltips. Rules: a click on the fold button (or the key Ctrl+Backslash)
// is a manual choice and always wins; without one, the list folds by itself when the window is too narrow for the open list
// and a usable page area, with two thresholds (fold below, open again 80 px above) so a small resize never makes it flip
// back and forth. The choice is kept as one number in the settings file.
// No ImGui, no engine. Written from scratch for this engine.
#include <algorithm>
#include <cmath>
#include <string>

namespace oked::modeltools {

constexpr float kSidebarOpenW = 196.0f;     // the page list, open: at least this wide (wider when its text needs it)
constexpr float kSidebarStripW = 44.0f;     // the page list, folded: one column of icons
constexpr float kMinPageAreaW = 420.0f;     // the automatic rule keeps at least this much for the page area
constexpr float kFoldBelowW = 660.0f;       // the window is narrower than this (or than the open list + kMinPageAreaW): fold by itself
constexpr float kUnfoldGapW = 80.0f;        // ... and open again by itself once it is this much wider (the hysteresis)
constexpr float kUnfoldAboveW = kFoldBelowW + kUnfoldGapW;
constexpr float kIconColW = 30.0f;          // the icon column at the left of every row
constexpr float kRowH = 46.0f, kStripRowH = 38.0f, kFoldBtn = 32.0f, kRowPadX = 8.0f;

struct SidebarState {
    bool folded = false;
    bool manual = false;   // the person chose: automatic folding stays out of the way
};

/// The chevron button or the key.
inline void SidebarToggle(SidebarState& s) { s.folded = !s.folded; s.manual = true; }

/// The width the open list needs: the icon column, the longest name or hint (`longestTextW`, measured by the caller in
/// the current font), the row padding and `chromeW` (the list's own window padding and border). Never below kSidebarOpenW.
inline float SidebarOpenWidth(float longestTextW, float scale = 1.0f, float chromeW = 0.0f) {
    return std::max(kSidebarOpenW * scale, kIconColW * scale + 2.0f * kRowPadX * scale + longestTextW + chromeW);
}
/// The window width below which the list folds by itself (`openW` = SidebarOpenWidth, 0 = the default 196 px).
inline float FoldBelowWidth(float scale = 1.0f, float openW = 0.0f) {
    const float ow = openW > 0.0f ? openW : kSidebarOpenW * scale;
    return std::max(kFoldBelowW * scale, ow + kMinPageAreaW * scale);
}

/// Called every frame with the window width. Does nothing after a manual choice.
inline void SidebarAuto(SidebarState& s, float windowW, float scale = 1.0f, float openW = 0.0f) {
    if (s.manual) return;
    const float foldBelow = FoldBelowWidth(scale, openW);
    if (!s.folded && windowW < foldBelow) s.folded = true;
    else if (s.folded && windowW > foldBelow + kUnfoldGapW * scale) s.folded = false;
}

/// The width of the list column: the icon strip when folded; when open, the width its text needs (`openW`, 0 = 196 px).
/// The open list never shrinks below that, so its text is never cut: when the window is too narrow the automatic rule
/// folds it to icons instead. (A list opened by hand in a narrow window keeps its width; the page area then scrolls.)
/// `scale` is the editor's UI scale (125 % = 1.25): every pixel size here grows with it, because the text does.
inline float SidebarWidth(const SidebarState& s, float /*windowW*/, float scale = 1.0f, float openW = 0.0f) {
    if (s.folded) return kSidebarStripW * scale;
    return openW > 0.0f ? openW : kSidebarOpenW * scale;
}
/// What is left for the page area (a few pixels go to borders and spacing; the caller passes the window's inner width).
inline float PageAreaWidth(const SidebarState& s, float windowW, float scale = 1.0f, float openW = 0.0f) { return windowW - SidebarWidth(s, windowW, scale, openW); }

// ---- Rows. Heights scale with the UI scale; a row is two lines (name, hint) so it is at least two text lines tall.
inline float RowHeight(float scale, float lineH) { return std::max(kRowH * scale, 2.0f * lineH + 10.0f); }
inline float StripRowHeight(float scale, float lineH) { return std::max(kStripRowH * scale, lineH + 12.0f); }

/// The one-line reason a page button is greyed out ("" when it is usable).
inline const char* PageDisabledReason(bool needsMesh, bool haveMesh) {
    return (needsMesh && !haveMesh) ? "Click a model in the scene first. No model yet? Open Add shapes and make one." : "";
}

// ---- Settings: one number. 0 = never chosen (open, automatic folding allowed), 1 = folded by choice, 2 = open by choice.
inline int SidebarToPref(const SidebarState& s) { return !s.manual ? 0 : (s.folded ? 1 : 2); }
inline SidebarState SidebarFromPref(int v) {
    SidebarState s;
    if (v == 1) { s.folded = true; s.manual = true; }
    else if (v == 2) { s.folded = false; s.manual = true; }
    return s;
}

} // namespace oked::modeltools
