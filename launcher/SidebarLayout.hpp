#pragma once
// Launcher sidebar: how tall the scrolling list of rows (MENU, SOCIAL, LEARN,
// MODERATION) may be, given the space left under the header and the pinned
// footer (update card, voice panel, Notifications, Settings, profile card).
// Pure arithmetic, no SDL or ImGui, so tests/test_launcher_sidebar_layout.cpp
// can check it. Written for issue #888 finding 55: on a short window the rows
// were clipped with no scrollbar, so Friends, Chat, Tips and the rest could not
// be reached.
#include <algorithm>

namespace okaysidebar {

struct Layout {
    float itemsH = 1.0f;          // height to give the rows' child window (always >= 1: 0 would mean "fill")
    bool  compactUpdate = false;  // draw the update card as one row instead of the full card
    bool  scrolls = false;        // the rows are taller than itemsH: show the scrollbar and the fade
};

// avail          height left in the sidebar under its header
// footFull       pinned footer height with the full update card
// footCompact    pinned footer height with the one-row update card (== footFull when no card is shown)
// rowsH          height the rows need (measured last frame; 0 when not known yet)
// minItemsH      the smallest list worth showing (a couple of rows), kept even when the footer is big
inline Layout Plan(float avail, float footFull, float footCompact, float rowsH, float minItemsH) {
    Layout L;
    float h = avail - footFull;
    // Rows do not fit under the full card: shrink the card to one row first.
    if (rowsH > h + 0.5f && footCompact < footFull) {
        L.compactUpdate = true;
        h = avail - footCompact;
    }
    h = std::max(h, minItemsH);
    h = std::max(h, 1.0f);
    L.itemsH = h;
    L.scrolls = rowsH > h + 0.5f;
    return L;
}

}  // namespace okaysidebar
