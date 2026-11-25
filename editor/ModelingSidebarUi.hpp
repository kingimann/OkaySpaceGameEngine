#pragma once
// The Modeling window's page list, drawn (ModelingSidebarUi.cpp). Open: one row per page with its icon, name and hint under
// job headings, as wide as the longest text needs, so nothing is cut. Folded: a strip of page icons with the names as
// tooltips. The fold button at the top switches between the two. Sizes and fold rules: ModelingSidebar.hpp (tested);
// pages and icons: ModelingToolIndex.hpp. main.cpp's DrawModeling calls this; tests/modeling_advanced_shot.cpp draws it
// headless for pictures. Written from scratch for this engine.
#include "ModelingSidebar.hpp"
#include <functional>

namespace oked::modeltools {

struct PageListResult {
    int clicked = -1;      // the page (legacy number) clicked this frame, or -1
    bool toggled = false;  // the fold button was clicked (the state is already flipped; the caller saves the choice)
};

/// Draw the list at the cursor as a child window as tall as the space left. `current` is the open page; `whyNot(legacy)`
/// returns "" for a usable page and the plain reason for a greyed one. Applies the automatic fold rule for `windowW`.
PageListResult DrawPageList(SidebarState& s, int current, const std::function<const char*(int)>& whyNot, float uiScale, float windowW);

/// The width the open list needs in the current font (icon column + the longest name, hint or heading + padding).
float PageListOpenWidth(float uiScale);

} // namespace oked::modeltools
