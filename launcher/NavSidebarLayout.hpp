// NavSidebarLayout.hpp - which layout the launcher sidebar's rows use so they fit their area.
// Our own code. Pure maths, no ImGui, so it is tested headless (tests/test_launcher_nav_layout.cpp).
#pragma once
#include <vector>

namespace okay::launchernav {

struct NavFit {
    bool compact = false;   // group labels become hairlines, rows 4 px shorter
    bool scroll = false;    // even that does not fit: the area shows its scrollbar
    float rowH = 0.0f;      // the height of one row in this layout
};

/// `rows` / `sections`: last frame's counts of rows and group labels in the sidebar's item area; `itemsH`: that area's
/// height; `rowH`: the full row height; `lineH`: the text line height; `collapsed`: the icons-only sidebar (it already
/// uses hairlines, so it never goes compact). The heights follow the launcher's navRow (row + 2 spacing) and navSection
/// (full: 12 + label at 0.78 scale + 4 plus spacing; hairline: 6 + 7 plus spacing).
inline NavFit FitNavRows(int rows, int sections, float itemsH, float rowH, float lineH, bool collapsed) {
    NavFit f;
    f.rowH = rowH;
    if (rows <= 0) return f;
    const float fullSectionH = 12.0f + 2.0f + lineH * 0.78f + 2.0f + 4.0f + 2.0f;
    const float hairSectionH = 6.0f + 7.0f + 4.0f;
    const float fullNeed = rows * (rowH + 2.0f) + sections * (collapsed ? hairSectionH : fullSectionH);
    f.compact = !collapsed && fullNeed > itemsH;
    if (f.compact) f.rowH = rowH - 4.0f;
    const float compactNeed = rows * (f.rowH + 2.0f) + sections * hairSectionH;
    f.scroll = (f.compact ? compactNeed : fullNeed) > itemsH;
    return f;
}

/// The height of the scrolling rows area: what is left of `avail` above the pinned footer (Notifications, Settings, the
/// profile card, an update card ...). `footEstimate` is the footer worked out from its parts; `footMeasured` is how tall it
/// really was last frame (0 on the first frame). The larger one wins, so the rows area always ends above the footer and its
/// clip rectangle keeps the rows from drawing under it (a 600 px window drew "Achievements" under Notifications).
inline float NavItemsHeight(float avail, float footEstimate, float footMeasured) {
    const float foot = footEstimate > footMeasured ? footEstimate : footMeasured;
    const float h = avail - foot;
    return h > 0.0f ? h : 0.0f;
}

/// The scrolling rows area's height: the bottom of the last row that fits whole in `itemsH` (`rowBottoms`: each row's bottom
/// from the area's top, last frame), so the area's edge never cuts a row in half. `itemsH` itself before any row is known.
inline float WholeRowsHeight(const std::vector<float>& rowBottoms, float itemsH) {
    float best = 0.0f;
    for (float b : rowBottoms) if (b <= itemsH && b > best) best = b;
    return best > 0.0f ? best + 2.0f : itemsH;   // (+ the spacing under the row)
}

}  // namespace okay::launchernav
