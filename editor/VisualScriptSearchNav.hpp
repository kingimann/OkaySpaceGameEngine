#pragma once
// The node search popup's pure logic: how big the popup is and how it is kept on screen, how the selection moves for
// Up / Down / Home / End / PageUp / PageDown, how far a list scrolls to show the selected row, and how Left / Right step
// through the categories. No ImGui here, so tests/test_vs_search_nav.cpp covers it; VisualScriptPalette.hpp draws it.
//
// Written from scratch for this engine from plain list-box behaviour (move, clamp, reveal); the pixel numbers are our own
// choices and the row height reads the shared Density table (ScriptingDensity.hpp).
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace okay::editor::vsed::searchnav {

// ---- Panel size ------------------------------------------------------------------------------------------------------

struct PanelSize { float w = 0, h = 0; bool bad = false; };

constexpr float kScreenMargin = 24.0f;   // the popup never touches the edge of the screen
constexpr float kMinW = 420.0f, kMinH = 320.0f;

/// Never bigger than the screen minus a margin; never smaller than a usable size (unless the screen itself is smaller).
inline PanelSize ClampPanelSize(PanelSize want, float screenW, float screenH) {
    const float maxW = std::max(160.0f, screenW - kScreenMargin);
    const float maxH = std::max(120.0f, screenH - kScreenMargin);
    PanelSize r;
    r.w = std::min(std::max(want.w, kMinW), maxW);
    r.h = std::min(std::max(want.h, kMinH), maxH);
    if (!(want.w > 0)) r.w = std::min(kMinW, maxW);
    if (!(want.h > 0)) r.h = std::min(kMinH, maxH);
    return r;
}

/// The size a fresh editor opens at: about 560 x 520 at Comfortable (the default Density), a little smaller at Compact
/// and bigger at Spacious, clamped to the screen.
inline PanelSize DefaultPanelSize(int density, float screenW, float screenH) {
    PanelSize p{560.0f, 520.0f};
    if (density <= 0) p = {520.0f, 480.0f};
    else if (density >= 2) p = {640.0f, 580.0f};
    return ClampPanelSize(p, screenW, screenH);
}

/// "700x600" in the settings file. `bad` is set when the text is not two positive numbers.
inline PanelSize ParsePanelSize(const std::string& s) {
    PanelSize r; r.bad = true;
    const std::size_t x = s.find('x');
    if (x == std::string::npos || x == 0 || x + 1 >= s.size()) return r;
    char* e1 = nullptr; char* e2 = nullptr;
    const std::string a = s.substr(0, x), b = s.substr(x + 1);
    const float w = std::strtof(a.c_str(), &e1), h = std::strtof(b.c_str(), &e2);
    if (*e1 != '\0' || *e2 != '\0' || !(w > 0) || !(h > 0)) return r;
    r.w = w; r.h = h; r.bad = false;
    return r;
}
inline std::string FormatPanelSize(PanelSize p) {
    return std::to_string((int)std::lround(p.w)) + "x" + std::to_string((int)std::lround(p.h));
}

/// The category list on the left: about a third of the panel, within limits, so the node rows keep the room.
inline float CategoryPaneWidth(float panelW) { return std::min(240.0f, std::max(160.0f, std::floor(panelW * 0.34f))); }

/// One node row: the Density's row height, or two lines of text plus padding when that is taller (name, then description).
inline float RowHeight(float densityRowH, float textH, float pad) { return std::max(densityRowH, 2.0f * textH + 2.0f * pad); }

// ---- Selection and scrolling -----------------------------------------------------------------------------------------

enum class Move { None, Up, Down, Home, End, PageUp, PageDown };

/// Where the selection goes. Stops at both ends (no wrap, so holding a key does not jump around). An out-of-range
/// selection is pulled back into the list; an empty list gives 0.
inline int MoveSelection(int sel, int count, Move m, int pageRows) {
    if (count <= 0) return 0;
    const int last = count - 1, page = std::max(1, pageRows);
    int r = sel;
    switch (m) {
        case Move::Up:       r = sel - 1; break;
        case Move::Down:     r = sel + 1; break;
        case Move::Home:     r = 0; break;
        case Move::End:      r = last; break;
        case Move::PageUp:   r = sel - page; break;
        case Move::PageDown: r = sel + page; break;
        case Move::None:     break;
    }
    return std::min(std::max(r, 0), last);
}

/// The new scroll offset that shows the row at [rowTop, rowTop + rowH) inside a view `viewH` tall, with `margin` of air.
/// The offset does not change while the row is already visible. A row taller than the view shows its top.
inline float RevealScroll(float scroll, float viewH, float rowTop, float rowH, float margin) {
    const float top = rowTop - margin, bottom = rowTop + rowH + margin;
    float s = scroll;
    if (bottom - top > viewH) s = top;
    else if (top < s) s = top;
    else if (bottom > s + viewH) s = bottom - viewH;
    return std::max(0.0f, s);
}
inline float MaxScroll(float contentH, float viewH) { return std::max(0.0f, contentH - viewH); }

// ---- Categories ------------------------------------------------------------------------------------------------------

/// Left / Right: the id one step along `ids` (the visible categories, top to bottom). Stops at the ends. When `cur` is not
/// in the list, the first id; an empty list keeps `cur`.
inline int StepCategory(const std::vector<int>& ids, int cur, int dir) {
    if (ids.empty()) return cur;
    const auto it = std::find(ids.begin(), ids.end(), cur);
    if (it == ids.end()) return ids.front();
    const int i = (int)(it - ids.begin()) + (dir < 0 ? -1 : (dir > 0 ? 1 : 0));
    return ids[(std::size_t)std::min(std::max(i, 0), (int)ids.size() - 1)];
}

}  // namespace okay::editor::vsed::searchnav
