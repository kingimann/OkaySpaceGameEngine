#pragma once
// ModelingSidebar: the fold state and width maths of the Modeling window's left page list. Open it is 196 px wide;
// folded it is a thin strip (36 px) of page buttons. Rules: a click on the chevron or Ctrl+\ is a manual choice and always wins;
// without one, the list folds by itself when the window gets narrow, with two thresholds (fold below 760 px, unfold above 840 px)
// so a small resize never makes it flip back and forth. The choice is kept as one number in the settings file.
// No ImGui, no engine. Written from scratch for this engine.
#include <algorithm>
#include <cmath>
#include <string>

namespace oked::modeltools {

constexpr float kSidebarOpenW = 196.0f;     // the page list, open
constexpr float kSidebarStripW = 36.0f;     // the page list, folded
constexpr float kMinPageAreaW = 420.0f;     // the page area never gets narrower than this because of the list
constexpr float kFoldBelowW = 760.0f;       // the window is narrower than this: fold by itself
constexpr float kUnfoldAboveW = 840.0f;     // ... and open again by itself above this (the gap is the hysteresis)

struct SidebarState {
    bool folded = false;
    bool manual = false;   // the person chose: automatic folding stays out of the way
};

/// The chevron button or the key.
inline void SidebarToggle(SidebarState& s) { s.folded = !s.folded; s.manual = true; }

/// Called every frame with the window width. Does nothing after a manual choice.
inline void SidebarAuto(SidebarState& s, float windowW, float scale = 1.0f) {
    if (s.manual) return;
    if (!s.folded && windowW < kFoldBelowW * scale) s.folded = true;
    else if (s.folded && windowW > kUnfoldAboveW * scale) s.folded = false;
}

/// The width of the list column for this window width: the strip when folded; when open, 196 px but never so wide that the
/// page area drops below kMinPageAreaW (and never below the strip).
/// `scale` is the editor's UI scale (125 % = 1.25): every pixel size here grows with it, because the text does.
inline float SidebarWidth(const SidebarState& s, float windowW, float scale = 1.0f) {
    if (s.folded) return kSidebarStripW * scale;
    return std::max(kSidebarStripW * scale, std::min(kSidebarOpenW * scale, windowW - kMinPageAreaW * scale));
}
/// What is left for the page area (a few pixels go to borders and spacing; the caller passes the window's inner width).
inline float PageAreaWidth(const SidebarState& s, float windowW, float scale = 1.0f) { return windowW - SidebarWidth(s, windowW, scale); }

// ---- Rows. Heights scale with the UI scale; a row is two lines (name, hint) so it is at least two text lines tall.
constexpr float kRowH = 44.0f, kStripRowH = 34.0f, kFoldBtn = 32.0f, kRowPadX = 8.0f;
inline float RowHeight(float scale, float lineH) { return std::max(kRowH * scale, 2.0f * lineH + 10.0f); }
inline float StripRowHeight(float scale, float lineH) { return std::max(kStripRowH * scale, lineH + 12.0f); }

/// Cut `text` so it is at most `maxW` wide by `measure(string)`, ending in "..." when cut. Never returns more than needed.
template <class Measure>
inline std::string FitText(const std::string& text, float maxW, Measure measure) {
    if (measure(text) <= maxW) return text;
    const std::string dots = "...";
    if (measure(dots) > maxW) return "";
    std::string t = text;
    while (!t.empty()) { t.pop_back(); while (!t.empty() && t.back() == ' ') t.pop_back(); if (measure(t + dots) <= maxW) return t + dots; }
    return dots;
}

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
