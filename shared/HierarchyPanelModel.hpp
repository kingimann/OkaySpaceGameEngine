#pragma once
// The editor's Hierarchy panel as data (no ImGui, no OkayUI, no engine types), part of shared/EditorPanelModels.hpp (which includes
// it). The panel draws from it: the
// OkayUI body (okayui/screens/OkayUIEditorPanels.cpp DrawHierarchy). docs/own_ui_toolkit.md section 12.
//
//   * The rows. The editor keeps its scene tree as a flat list of rows (editor/HierarchyRowModel.hpp: rebuilt only when the scene's
//     objects, parents, the open state or the sort change). The panel model does not copy that list: it says how many rows there
//     are and lends a fill function, and a version asks only for the rows it draws (the ones in view, plus one either side). A scene
//     of 20,000 objects costs the rows on screen, not 20,000 rows.
//   * What a row shows (its name, depth, arrow, icon, badges, the eye and the padlock, the prefab mark, the dim and blue text) is
//     worked out by the host for those rows only, as plain values; the drawing of the small icons is written once here, against a
//     tiny pen interface each version implements (HierDrawIcon and friends), so both versions draw the same shapes.
//   * What the person did comes back as one intent (select, Ctrl and Shift clicks, the arrow, the eye, the padlock, a drag and drop,
//     a rename, a menu item, the keys); the editor carries it out (undo, the drop rules of InspectorModel.cpp, the menu actions).
//
// Written from the panel's own behaviour (editor/main.cpp DrawHierarchy and its helpers); no code from any other project.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace okaypanels {

// ============================================================================================================================
// Hierarchy
// ============================================================================================================================

/// A colour packed as the draw lists of both toolkits take it: 0xAABBGGRR (bytes R, G, B, A in memory).
constexpr std::uint32_t HierCol(unsigned r, unsigned g, unsigned b, unsigned a = 255u) {
    return (r & 0xffu) | ((g & 0xffu) << 8) | ((b & 0xffu) << 16) | ((a & 0xffu) << 24);
}

/// The small drawn type icon at the start of a row. The host picks it from the object's components in this order (the first match
/// wins): camera, light, canvas, button, toggle, bar, field, dropdown, picture, panel, radial, event system, mesh, text, particles,
/// tilemap, sprite, sound; an object with children and only a transform is a folder; anything else a dot.
enum HierIcon : int {
    HierIconDot = 0, HierIconCamera, HierIconLight, HierIconCanvas, HierIconButton, HierIconToggle, HierIconBar, HierIconField,
    HierIconDropdown, HierIconImage, HierIconPanel, HierIconRadial, HierIconEventSystem, HierIconMesh, HierIconText, HierIconParticles,
    HierIconTilemap, HierIconSprite, HierIconAudio, HierIconFolder, HierIconCount
};

/// The coloured letters at a row's right edge (what is attached).
enum HierBadge : unsigned {
    HierBadgeScript = 1u, HierBadgeLight = 2u, HierBadgeRigidbody = 4u, HierBadgeCollider = 8u, HierBadgeAnimator = 16u, HierBadgeParticles = 32u
};

/// The prefab mark on the icon's corner.
enum HierPrefabMark : int { HierPrefabNone = 0, HierPrefabPart = 1, HierPrefabRoot = 2, HierPrefabVariant = 3 };

/// One line of the list.
struct HierRow {
    std::uint64_t id = 0;            ///< the host's handle for the object (0 on a header row)
    bool header = false;             ///< a combined scene's group title (no object)
    bool headerMain = false;         ///< ...of the main scene (grey; other scenes are blue)
    std::string name;                ///< the object's name, or a header's title
    int depth = 0;                   ///< 0 at the top level
    int childCount = 0;              ///< direct children (the arrow shows when > 0)
    bool open = false;               ///< folded out
    int objectIndex = 0;             ///< place among the object rows (every other one has a faint band)
    bool selected = false;
    bool active = true;              ///< off: "(off)" after the name, the eye struck through, the text dim
    bool dim = false;                ///< drawn grey: off, or outside the isolated object
    bool prefabBlue = false;         ///< a prefab instance's root: its name reads blue
    bool locked = false;             ///< can't be click-picked in the Scene view (the padlock is amber)
    int icon = HierIconDot;          ///< HierIcon
    int prefab = HierPrefabNone;     ///< HierPrefabMark
    bool prefabNested = false;       ///< a prefab inside a bigger one (an inner square on the mark)
    bool prefabChanged = false;      ///< differs from its prefab (an orange dot above the mark)
    std::string prefabTip;           ///< hover text of the mark ("" = none)
    unsigned badges = 0;             ///< HierBadge bits
    int diag = 0;                    ///< problems on it: 0 none, 1 warnings, 2 errors (a marker 48 pixels from the right)
    std::string diagTip;
    std::string path;                ///< search results only: the parents, "Canvas / Lobby" ("" at the top level)
};

/// Where a dragged row lands on another row: in its top 30% above it, in its bottom 30% below it, in between inside it (as its
/// last child). `Root`: dropped on the empty space under the rows, to the top level.
enum HierDropWhere : int { HierDropBefore = 0, HierDropInside = 1, HierDropAfter = 2, HierDropRoot = 3 };

/// The zone of a row under the pointer, `t` = how far down the row (0 top, 1 bottom).
inline int HierDropZone(float t) { return t < 0.30f ? HierDropBefore : t > 0.70f ? HierDropAfter : HierDropInside; }

/// A menu as data (the editor's menu lists, editor/MenuBarModel.hpp, converted by the host; its host-drawn parts are recorded into
/// plain entries first). `id` comes back in the intent when the item is chosen.
enum PanelMenuKind : int { PanelMenuItem = 0, PanelMenuToggle, PanelMenuSub, PanelMenuSeparator, PanelMenuHeading };
struct PanelMenuEntry {
    int kind = PanelMenuItem;
    std::string id, label, shortcut, tip, reason;   ///< reason: why it is greyed out
    bool enabled = true, checked = false;
    std::uint32_t color = 0;                        ///< PanelMenuHeading: its text colour (0: grey, Dear ImGui's TextDisabled)
    std::vector<PanelMenuEntry> children;           ///< PanelMenuSub only
};

/// Which menu is open.
enum HierMenuKind : int { HierMenuNone = 0, HierMenuRow, HierMenuEmpty, HierMenuCreate, HierMenuOptions };

/// The options button's popup (Expand all, Collapse all, Sort A-Z): the same in both versions, so it is data here.
inline std::vector<PanelMenuEntry> HierOptionsMenu(bool sortAZ) {
    std::vector<PanelMenuEntry> m(4);
    m[0].id = "h.expandall"; m[0].label = "Expand all";
    m[1].id = "h.collapseall"; m[1].label = "Collapse all";
    m[2].kind = PanelMenuSeparator;
    m[3].kind = PanelMenuToggle; m[3].id = "h.sortaz"; m[3].label = "Sort A-Z"; m[3].checked = sortAZ;
    return m;
}

struct HierarchyModel {
    // the strip above the list
    std::string sceneName;           ///< "" shows "Untitled"
    bool dirty = false;              ///< "  ·  unsaved" after the name
    int objectCount = 0;             ///< right-aligned
    int sceneDiag = 0;               ///< scene-wide problems: 0, 1 warnings, 2 errors
    std::string sceneDiagTip;
    bool sortAZ = false;             ///< the options button shows lit while it is on
    // the list
    bool searching = false;          ///< the search field has text: the rows are the matches (flat), not the tree
    int rowCount = 0;                ///< how many rows the list has
    std::function<void(int index, HierRow& out)> fillRow;   ///< the row at `index`; asked only for rows that are drawn or clicked
    int activeRow = -1;              ///< the row of the active (last selected) object, -1 when it is not listed
    int selectedCount = 0;           ///< how many objects are selected
    int revealRow = -1;              ///< this frame only: scroll so this row shows (an object picked in the Scene view)
    bool focused = false;            ///< the panel has the keyboard (the arrows, Ctrl+A and F2 act)
    /// Fills the open menu, asked only while one is open. `row` is the right-clicked row's id for HierMenuRow.
    std::function<void(int kind, std::uint64_t row, std::vector<PanelMenuEntry>& out)> buildMenu;
    /// A drag that began outside the panel (the Project panel's files): its type ("ASSET_PATH") and text; empty when none.
    std::string externalDragType, externalDragText;
    bool externalDragReleased = false;   ///< the button was let go this frame (the drop happens now)
};

/// What the panel keeps between frames (owned by the host, one per panel).
struct HierarchyState {
    char filter[96] = "";            ///< the search field
    std::uint64_t pressedMulti = 0;  ///< pressed on one of several selected rows: decided on release (a drag moves them all)
    float pressX = 0.0f, pressY = 0.0f;   ///< where that press was (a release 4 pixels or more away is a drag, not a click)
    std::uint64_t renameId = 0;      ///< the row being renamed in place (0 = none)
    char renameBuf[128] = "";
    int renameAge = 0;               ///< frames since the rename began (the field takes the keyboard on the first)
    int menuKind = HierMenuNone;     ///< the open menu
    std::uint64_t menuRow = 0;       ///< the right-clicked row (HierMenuRow)
    std::uint64_t dragging = 0;      ///< the row being dragged (0 = none)
    int rowsFilled = 0;              ///< how many rows the last frame asked the host for (the tests check it stays small)
    float scrollY = 0.0f;            ///< the list's scroll position (pixels), as the last frame left it
};

struct HierarchyIntent {
    std::uint64_t select = 0;        ///< select only this object
    std::uint64_t toggle = 0;        ///< Ctrl+click: add it to or take it out of the selection
    std::uint64_t rangeTo = 0;       ///< Shift+click: everything listed from the active object to this one
    bool selectAll = false;          ///< Ctrl+A: every listed object
    std::uint64_t toggleActive = 0;  ///< the eye
    std::uint64_t toggleLock = 0;    ///< the padlock
    std::uint64_t openId = 0;        ///< fold this row out (open = 1) or in (open = 0): the arrow, or Right / Left
    int open = -1;
    std::uint64_t frame = 0;         ///< a double click on a search result: look at it in the Scene view
    std::uint64_t renameId = 0;      ///< a rename in place finished (Enter, or the field was left); renameTo is the new name
    std::string renameTo;
    std::uint64_t renameBegan = 0;   ///< a rename in place began on this row (double click, F2 or the menu's Rename)
    std::uint64_t dragged = 0;       ///< a row was dropped: this one (the host moves the whole selection when it is part of it) ...
    std::uint64_t dropTarget = 0;    ///< ... on this row (0 with HierDropRoot: on the empty space, to the top level) ...
    int dropWhere = -1;              ///< ... HierDropWhere
    std::string dropAsset;           ///< a file from the Project panel was dropped (on dropTarget, at dropWhere)
    std::string menuId;              ///< a menu item was chosen: its id, which menu, and the row of a row menu
    int menuKind = HierMenuNone;
    std::uint64_t menuRow = 0;
    bool filterChanged = false;
};

// ---- rules both versions share ----------------------------------------------------------------------------------------------

/// After the name: the child count of a parent and an "(off)" marker.
inline std::string HierTail(int childCount, bool active) {
    char cnt[24] = "";
    if (childCount > 0) std::snprintf(cnt, sizeof cnt, "  (%d)", childCount);
    return std::string(cnt) + (active ? "" : "  (off)");
}

/// A combined scene's group title: the main scene's name with " (main)" (or "Main Scene"), else the source scene's name.
inline std::string HierHeaderLabel(const std::string& section, const std::string& sceneName) {
    if (!section.empty()) return section;
    return sceneName.empty() ? std::string("Main Scene") : sceneName + " (main)";
}

/// The strip above the list: the scene's name and the unsaved marker.
inline std::string HierSceneTitle(const std::string& name, bool dirty) {
    return (name.empty() ? std::string("Untitled") : name) + (dirty ? "  \xC2\xB7  unsaved" : "");
}

/// The line under the search results.
inline std::string HierSearchFooter(int hits, const char* filter) {
    if (hits == 0) return std::string("No objects match '") + (filter ? filter : "") + "'.";
    return std::to_string(hits) + " match(es)";
}

inline const char* kHierSearchTip =
    "Search by name, tag or type: camera, light, mesh, script, ui...\nt:enemy  finds a tag      c:rigidbody  finds a component\n"
    "l:water  finds a layer    is:off / locked / static / prefab\n-word leaves matches out. Parent/Child lists every object that path names.";
inline const char* kHierEmptyTitle = "Empty scene";
inline const char* kHierEmptyHint = "Add objects from the GameObject menu, or right-click here.";

/// `text` broken into lines no wider than `maxW` at the spaces (a word longer than that keeps its own line), measured by
/// `width(const std::string&)`: the editor's empty-panel note (main.cpp EmptyState).
template <class Measure>
std::vector<std::string> HierWrapWords(const std::string& text, float maxW, Measure width) {
    std::vector<std::string> lines;
    std::string line;
    std::size_t i = 0;
    while (i <= text.size()) {
        std::size_t j = text.find(' ', i);
        if (j == std::string::npos) j = text.size();
        const std::string word = text.substr(i, j - i);
        const std::string trial = line.empty() ? word : line + " " + word;
        if (!line.empty() && width(trial) > maxW) { lines.push_back(line); line = word; }
        else line = trial;
        i = j + 1;
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

inline const char* HierEyeTip(bool active) {
    return active ? "Hide (deactivate) this object and its children" : "Show (activate) this object and its children";
}
inline const char* HierLockTip(bool locked) {
    return locked ? "Unlock: allow selecting this object in the Scene view again"
                  : "Lock: this object (and children) can't be click-selected in the Scene view";
}

/// The text that follows the pointer while rows are dragged.
inline std::string HierDragLabel(const std::string& name, bool partOfSelection, int selectedCount) {
    if (partOfSelection && selectedCount > 1) return name + "  (+" + std::to_string(selectedCount - 1) + " more)";
    return name;
}

/// `text` cut to fit in `maxW` pixels with an ellipsis (UTF-8 safe), measured by `width(const std::string&)`. Nothing is cut when
/// `maxW` is under 12 pixels (too narrow to read anyway).
template <class Measure>
std::string HierEllipsize(const std::string& text, float maxW, Measure width) {
    if (maxW < 12.0f || width(text) <= maxW) return text;
    const char* ell = "\xE2\x80\xA6";
    std::string s = text;
    while (!s.empty() && width(s + ell) > maxW) {   // drop the last character: its continuation bytes, then its first byte
        while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80) s.pop_back();
        if (!s.empty()) s.pop_back();
    }
    return s.empty() ? std::string(ell) : s + ell;
}

/// Shift+click: the objects listed from `anchor` to `clicked`, both included, top to bottom. Empty when either is not listed
/// (the host then selects just the clicked one).
template <class T>
std::vector<T> HierRange(const std::vector<T>& order, const T& anchor, const T& clicked) {
    int a = -1, b = -1;
    for (int i = 0; i < (int)order.size(); ++i) {
        if (order[(std::size_t)i] == anchor) a = i;
        if (order[(std::size_t)i] == clicked) b = i;
    }
    std::vector<T> out;
    if (a < 0 || b < 0) return out;
    if (a > b) std::swap(a, b);
    for (int i = a; i <= b; ++i) out.push_back(order[(std::size_t)i]);
    return out;
}

/// Which rows of a list of `rowCount` rows `rowHeight` tall show when it is scrolled by `scrollY` in a view `viewHeight` tall:
/// [first, last), with `overscan` rows more on each side. (editor/HierarchyRowModel.hpp uses this one.)
struct HierClipRange {
    int first = 0, last = 0;
    bool operator==(const HierClipRange& o) const { return first == o.first && last == o.last; }
};
inline HierClipRange HierVisibleRange(int rowCount, float rowHeight, float scrollY, float viewHeight, int overscan = 1) {
    HierClipRange r;
    if (rowCount <= 0) return r;
    if (!(rowHeight > 0.0f)) { r.last = rowCount; return r; }   // unknown height: everything
    if (!(scrollY > 0.0f)) scrollY = 0.0f;
    if (!(viewHeight > 0.0f)) viewHeight = 0.0f;
    if (scrollY > 1e9f) scrollY = 1e9f;
    if (viewHeight > 1e9f) viewHeight = 1e9f;
    long first = (long)(scrollY / rowHeight);
    long last = (long)((scrollY + viewHeight) / rowHeight) + 1;   // the row cut by the bottom edge too
    first -= overscan; last += overscan;
    r.first = (int)std::max(0L, std::min(first, (long)rowCount));
    r.last = (int)std::max((long)r.first, std::min(last, (long)rowCount));
    return r;
}
/// The scroll position that shows row `row`: unchanged when it is already in view, else the row `anchor` of the way down.
inline float HierRevealScroll(int row, float rowHeight, float scrollY, float viewHeight, float anchor = 0.4f) {
    if (row < 0 || !(rowHeight > 0.0f)) return scrollY;
    const float top = row * rowHeight, bottom = top + rowHeight;
    if (top >= scrollY && bottom <= scrollY + viewHeight) return scrollY;
    const float s = top - (viewHeight - rowHeight) * anchor;
    return s > 0.0f ? s : 0.0f;
}

// ---- the small drawings, once for both versions ------------------------------------------------------------------------------
// A Pen is what each version draws with (the Dear ImGui draw list or OkayUI's), all in screen pixels:
//   line(x0, y0, x1, y1, col, thickness)            rect(x0, y0, x1, y1, col, rounding, thickness)
//   rectFilled(x0, y0, x1, y1, col, rounding)       circle(cx, cy, r, col, segments, thickness)
//   circleFilled(cx, cy, r, col, segments)          triangleFilled(ax, ay, bx, by, cx, cy, col)
//   polyline(const float* xy, n, col, closed, thickness)   arc(cx, cy, r, a0, a1, segments, col, thickness)
//   text(x, y, col, const char*)

/// The type icon in an `sz` square whose top-left is (x, y). Written from the editor's HierKindIcon (the same shapes and colours).
template <class Pen>
void HierDrawIcon(Pen& p, int icon, float x, float y, float sz) {
    const float cx = x + sz * 0.5f, cy = y + sz * 0.55f, r = sz * 0.36f;
    auto col = [](unsigned cr, unsigned cg, unsigned cb) { return HierCol(cr, cg, cb, 235); };
    switch (icon) {
    case HierIconCamera: {
        const std::uint32_t ic = col(120, 170, 250);
        p.rect(cx - r, cy - r * 0.7f, cx + r * 0.45f, cy + r * 0.7f, ic, 1.5f, 1.6f);
        p.triangleFilled(cx + r * 0.5f, cy, cx + r, cy - r * 0.55f, cx + r, cy + r * 0.55f, ic);
        break;
    }
    case HierIconLight: {
        const std::uint32_t ic = col(250, 215, 120);
        p.circleFilled(cx, cy, r * 0.5f, ic, 10);
        for (int k = 0; k < 8; ++k) {
            // the eight rays, every 45 degrees (cos and sin of k * 45 degrees, written out)
            static const float kc[8] = {1.0f, 0.70710678f, 0.0f, -0.70710678f, -1.0f, -0.70710678f, 0.0f, 0.70710678f};
            static const float ks[8] = {0.0f, 0.70710678f, 1.0f, 0.70710678f, 0.0f, -0.70710678f, -1.0f, -0.70710678f};
            p.line(cx + kc[k] * r * 0.72f, cy + ks[k] * r * 0.72f, cx + kc[k] * r * 1.05f, cy + ks[k] * r * 1.05f, ic, 1.4f);
        }
        break;
    }
    case HierIconCanvas: {
        const std::uint32_t ic = col(120, 200, 235);
        p.rect(cx - r, cy - r * 0.75f, cx + r, cy + r * 0.55f, ic, 1.5f, 1.5f);
        p.line(cx - r * 0.4f, cy + r * 0.95f, cx + r * 0.4f, cy + r * 0.95f, ic, 1.5f);
        break;
    }
    case HierIconButton: {
        const std::uint32_t ic = col(110, 170, 250);
        p.rectFilled(cx - r, cy - r * 0.55f, cx + r, cy + r * 0.55f, ic, r * 0.45f);
        p.circleFilled(cx, cy, r * 0.22f, HierCol(20, 24, 32), 8);
        break;
    }
    case HierIconToggle: {
        const std::uint32_t ic = col(110, 200, 150);
        p.rect(cx - r, cy - r * 0.5f, cx + r, cy + r * 0.5f, ic, r * 0.5f, 1.4f);
        p.circleFilled(cx + r * 0.45f, cy, r * 0.32f, ic, 10);
        break;
    }
    case HierIconBar: {
        const std::uint32_t ic = col(110, 200, 150);
        p.rect(cx - r, cy - r * 0.28f, cx + r, cy + r * 0.28f, ic, r * 0.28f, 1.3f);
        p.rectFilled(cx - r, cy - r * 0.28f, cx + r * 0.15f, cy + r * 0.28f, ic, r * 0.28f);
        break;
    }
    case HierIconField: case HierIconDropdown: {
        const std::uint32_t ic = col(170, 180, 200);
        p.rect(cx - r, cy - r * 0.55f, cx + r, cy + r * 0.55f, ic, 2.0f, 1.4f);
        if (icon == HierIconDropdown) p.triangleFilled(cx + r * 0.2f, cy - r * 0.15f, cx + r * 0.7f, cy - r * 0.15f, cx + r * 0.45f, cy + r * 0.2f, ic);
        else p.line(cx - r * 0.55f, cy - r * 0.3f, cx - r * 0.55f, cy + r * 0.3f, ic, 1.4f);
        break;
    }
    case HierIconImage: {
        const std::uint32_t ic = col(130, 220, 150);
        p.rect(cx - r, cy - r * 0.8f, cx + r, cy + r * 0.8f, ic, 1.5f, 1.4f);
        p.triangleFilled(cx - r * 0.7f, cy + r * 0.6f, cx, cy - r * 0.1f, cx + r * 0.7f, cy + r * 0.6f, ic);
        break;
    }
    case HierIconPanel: {
        const std::uint32_t ic = col(150, 165, 195);
        p.rectFilled(cx - r, cy - r * 0.75f, cx + r, cy + r * 0.75f, HierCol(150, 165, 195, 60), 2.5f);
        p.rect(cx - r, cy - r * 0.75f, cx + r, cy + r * 0.75f, ic, 2.5f, 1.4f);
        break;
    }
    case HierIconRadial: {
        const std::uint32_t ic = col(110, 200, 150);
        p.circle(cx, cy, r * 0.8f, HierCol(110, 200, 150, 90), 16, 1.6f);
        p.arc(cx, cy, r * 0.8f, -1.57f, 1.2f, 10, ic, 1.8f);
        break;
    }
    case HierIconEventSystem: {
        const std::uint32_t ic = col(170, 180, 200);
        const float pts[8] = {cx - r * 0.6f, cy - r, cx - r * 0.6f, cy + r * 0.6f, cx - r * 0.15f, cy + r * 0.2f, cx + r * 0.5f, cy + r * 0.2f};
        p.polyline(pts, 4, ic, true, 1.4f);
        break;
    }
    case HierIconMesh: {   // a little isometric cube
        const std::uint32_t ic = col(120, 170, 250);
        p.rect(cx - r, cy - r * 0.45f, cx + r * 0.45f, cy + r, ic, 0.0f, 1.5f);
        p.line(cx - r, cy - r * 0.45f, cx - r * 0.45f, cy - r, ic, 1.5f);
        p.line(cx - r * 0.45f, cy - r, cx + r, cy - r, ic, 1.5f);
        p.line(cx + r, cy - r, cx + r, cy + r * 0.45f, ic, 1.5f);
        p.line(cx + r, cy + r * 0.45f, cx + r * 0.45f, cy + r, ic, 1.5f);
        p.line(cx + r * 0.45f, cy - r * 0.45f, cx + r, cy - r, ic, 1.2f);
        break;
    }
    case HierIconText: {
        const std::uint32_t ic = col(190, 160, 250);
        p.line(cx - r, cy - r, cx + r, cy - r, ic, 1.8f);
        p.line(cx, cy - r, cx, cy + r, ic, 1.8f);
        break;
    }
    case HierIconParticles: {
        const std::uint32_t ic = col(250, 170, 110);
        p.circleFilled(cx - r * 0.5f, cy + r * 0.45f, r * 0.30f, ic, 8);
        p.circleFilled(cx + r * 0.45f, cy - r * 0.05f, r * 0.24f, ic, 8);
        p.circleFilled(cx - r * 0.05f, cy - r * 0.6f, r * 0.18f, ic, 8);
        break;
    }
    case HierIconTilemap: {
        const std::uint32_t ic = col(110, 210, 210);
        p.rect(cx - r, cy - r, cx + r, cy + r, ic, 0.0f, 1.4f);
        p.line(cx, cy - r, cx, cy + r, ic, 1.2f);
        p.line(cx - r, cy, cx + r, cy, ic, 1.2f);
        break;
    }
    case HierIconSprite: {   // a frame, a sun and a mountain
        const std::uint32_t ic = col(130, 220, 150);
        p.rect(cx - r, cy - r, cx + r, cy + r, ic, 1.5f, 1.4f);
        p.circleFilled(cx - r * 0.4f, cy - r * 0.4f, r * 0.2f, ic, 8);
        p.triangleFilled(cx - r * 0.7f, cy + r * 0.8f, cx + r * 0.1f, cy - r * 0.1f, cx + r * 0.85f, cy + r * 0.8f, ic);
        break;
    }
    case HierIconAudio: {
        const std::uint32_t ic = col(250, 170, 110);
        p.rectFilled(cx - r, cy - r * 0.45f, cx - r * 0.25f, cy + r * 0.45f, ic, 1.0f);
        p.triangleFilled(cx - r * 0.35f, cy, cx + r * 0.5f, cy - r * 0.8f, cx + r * 0.5f, cy + r * 0.8f, ic);
        break;
    }
    case HierIconFolder: {   // an empty group of children
        const std::uint32_t ic = col(170, 175, 190);
        p.rect(cx - r, cy - r * 0.55f, cx + r, cy + r * 0.75f, ic, 1.5f, 1.3f);
        p.line(cx - r, cy - r * 0.55f, cx - r * 0.6f, cy - r * 0.9f, ic, 1.3f);
        p.line(cx - r * 0.6f, cy - r * 0.9f, cx - r * 0.05f, cy - r * 0.9f, ic, 1.3f);
        p.line(cx - r * 0.05f, cy - r * 0.9f, cx + r * 0.15f, cy - r * 0.55f, ic, 1.3f);
        break;
    }
    default:
        p.circleFilled(cx, cy, r * 0.32f, HierCol(150, 155, 165, 200), 8);
        break;
    }
}

/// The prefab mark on the icon's corner (icon square at (x, y), `sz` wide). Blue square: an instance's root; violet: a variant's;
/// an inner square: nested in a bigger prefab; faint blue: a part of an instance; an orange dot: it differs from its prefab.
/// Returns the rectangle to hover for the tip (x0, y0, x1, y1), all zero when nothing was drawn.
template <class Pen>
void HierDrawPrefabMark(Pen& p, const HierRow& row, float x, float y, float sz, float hover[4]) {
    hover[0] = hover[1] = hover[2] = hover[3] = 0.0f;
    if (row.prefab == HierPrefabNone) return;
    const float ax = x + sz - 6.0f, ay = y + sz - 6.0f, bx = x + sz + 1.0f, by = y + sz + 1.0f;
    if (row.prefab == HierPrefabPart) {
        p.rectFilled(ax, ay, bx, by, HierCol(90, 165, 255, 130), 1.5f);
        p.rect(ax, ay, bx, by, HierCol(20, 24, 32, 200), 1.5f, 1.0f);
        return;
    }
    const bool variant = row.prefab == HierPrefabVariant;
    p.rectFilled(ax, ay, bx, by, variant ? HierCol(176, 128, 255) : HierCol(90, 165, 255), 1.5f);
    p.rect(ax, ay, bx, by, HierCol(20, 24, 32, 200), 1.5f, 1.0f);
    if (row.prefabNested) p.rectFilled(ax + 2.0f, ay + 2.0f, bx - 2.0f, by - 2.0f, HierCol(20, 24, 32, 230), 0.0f);
    if (row.prefabChanged) p.circleFilled(bx - 1.0f, ay - 2.0f, 3.0f, HierCol(255, 160, 40), 0);
    hover[0] = ax - 4.0f; hover[1] = ay - 6.0f; hover[2] = bx + 3.0f; hover[3] = by + 3.0f;
}

/// The component letters at the row's right edge, right to left from `right` (stopping before `minX`, so a long name wins).
template <class Pen>
void HierDrawBadges(Pen& p, unsigned badges, float right, float y0, float y1, float minX) {
    struct B { unsigned bit; std::uint32_t col; const char* s; };
    static const B kB[] = {
        {HierBadgeScript, HierCol(150, 140, 235), "S"}, {HierBadgeLight, HierCol(235, 205, 90), "L"},
        {HierBadgeRigidbody, HierCol(235, 140, 70), "R"}, {HierBadgeCollider, HierCol(120, 200, 130), "C"},
        {HierBadgeAnimator, HierCol(110, 200, 160), "A"}, {HierBadgeParticles, HierCol(110, 200, 235), "P"}};
    float x = right - 4.0f;
    const float cy = (y0 + y1) * 0.5f;
    for (const B& b : kB) {
        if (!(badges & b.bit)) continue;
        const float w = 13.0f;
        if (x - w < minX) break;
        p.rectFilled(x - w, cy - 7.0f, x, cy + 7.0f, (b.col & 0x00FFFFFFu) | 0x55000000u, 3.0f);
        p.text(x - w + 3.5f, cy - 8.0f, b.col, b.s);
        x -= (w + 3.0f);
    }
}

/// The width of the eye's hot spot at the row's right edge; the padlock's sits just left of it.
constexpr float kHierEyeW = 20.0f;

/// The eye (open when active, struck through when off) at the right edge of the row (x1 = its right end).
template <class Pen>
void HierDrawEye(Pen& p, bool active, float x1, float y0, float y1) {
    const float cx = x1 - kHierEyeW * 0.5f, cy = (y0 + y1) * 0.5f;
    const std::uint32_t c = active ? HierCol(190, 195, 205, 200) : HierCol(120, 124, 132, 160);
    p.circle(cx, cy, 4.5f, c, 0, 1.5f);
    p.circleFilled(cx, cy, 1.8f, c, 0);
    if (!active) p.line(cx - 6.0f, cy + 5.0f, cx + 6.0f, cy - 5.0f, c, 1.5f);
}

/// The padlock left of the eye: amber when locked, a faint hint while the row is hovered, nothing otherwise.
template <class Pen>
void HierDrawLock(Pen& p, bool locked, bool rowHovered, float x1, float y0, float y1) {
    if (!locked && !rowHovered) return;
    const float cx = x1 - kHierEyeW * 1.5f, cy = (y0 + y1) * 0.5f;
    const std::uint32_t c = locked ? HierCol(235, 190, 90, 230) : HierCol(140, 144, 152, 110);
    p.circle(cx, cy - 2.0f, 2.8f, c, 12, 1.4f);
    p.rectFilled(cx - 3.5f, cy - 0.5f, cx + 3.5f, cy + 5.0f, c, 1.0f);
}

/// The problems marker (editor/ProblemsPanel.cpp DrawMarker): an amber or red dot with a dark rim and a "!" centred at (cx, cy).
/// `bangW` and `bangH` are the size of "!" in the current font.
template <class Pen>
void HierDrawDiag(Pen& p, int diag, float cx, float cy, float bangW, float bangH) {
    if (diag <= 0) return;
    const std::uint32_t c = diag >= 2 ? HierCol(230, 78, 70) : HierCol(230, 176, 64);
    p.circleFilled(cx, cy, 6.5f, c, 14);
    p.circle(cx, cy, 6.5f, HierCol(28, 22, 18, 230), 14, 1.2f);
    p.text(cx - bangW * 0.5f, cy - bangH * 0.5f, HierCol(32, 22, 16), "!");
}

} // namespace okaypanels
