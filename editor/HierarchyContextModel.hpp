#pragma once
// The Hierarchy panel's menus as plain data (no ImGui, no scene): the right-click menu of a row, the right-click menu of the
// empty space, and the [+] Create popup. The host (main.cpp, DrawHierarchy) fills in a RowState / EmptyState from the scene,
// draws the entries with MenuEntriesUI.hpp and runs the item ids; what is enabled when, the labels, the plural forms
// ("Delete 3 Objects") and the order live here so tests/test_hierarchy_context_model.cpp can check them without a window.
// Entries are the ones in MenuBarModel.hpp; ids start with "h." (this panel) or "create." (shared with GameObject > Create).
//
// Written from the menu code that was in DrawHierarchy and the owner's list (2026-11-15); no code from another project.
#include "MenuBarModel.hpp"

#include <string>
#include <vector>

namespace oked {
namespace hiermenu {

using menubar::Entry;
using menubar::Kind;

/// The right-clicked row and the selection it belongs to. Right-clicking a row selects it first (unless it is already part
/// of a multi-selection), so `selectedCount` is always at least 1 and says how many objects Delete / Duplicate act on.
struct RowState {
    int  selectedCount = 1;
    bool active = true;            // the row's own active flag: Deactivate when on, Activate when off
    bool locked = false;           // locked against click-selecting in the Scene view
    int  childCount = 0;           // direct children
    bool hasParent = false;
    int  siblingIndex = 0;         // place among its siblings (roots count among the roots)
    int  siblingCount = 1;
    bool clipboardHasObject = false;
    bool isolated = false;         // this row is the isolated object
    bool inPrefab = false;         // the row is part of a prefab instance (has a prefab root)
    std::string prefabName;        // file name of that prefab
    bool prefabChanged = false;    // the instance differs from its prefab (something to apply or revert)
    bool prefabHasNested = false;  // other prefab instances sit inside it
    bool modelTools = false;       // rigging and IK items apply to this object
    std::string kindName;          // "Light", "Camera", ...: what Select Same Type means for this row
    int  sameKindOthers = -1;      // other objects of that kind; -1 = not counted
};

/// The empty-space menu and the [+] popup need less.
struct EmptyState {
    int  objectCount = 0;
    int  selectedCount = 0;
    bool clipboardHasObject = false;
    bool sortAZ = false;           // the Sort A-Z toggle is on
};

namespace id {
inline constexpr const char* kCopy = "h.copy";
inline constexpr const char* kCut = "h.cut";
inline constexpr const char* kPaste = "h.paste";
inline constexpr const char* kPasteChild = "h.pastechild";
inline constexpr const char* kRename = "h.rename";
inline constexpr const char* kDuplicate = "h.duplicate";
inline constexpr const char* kDelete = "h.delete";
inline constexpr const char* kDeleteChildren = "h.deletechildren";
inline constexpr const char* kEmptyChild = "h.emptychild";
inline constexpr const char* kCreateParent = "h.createparent";
inline constexpr const char* kParentTo = "h.parentto";
inline constexpr const char* kUnparent = "h.unparent";
inline constexpr const char* kMoveUp = "h.moveup";
inline constexpr const char* kMoveDown = "h.movedown";
inline constexpr const char* kMoveTop = "h.movetop";
inline constexpr const char* kMoveBottom = "h.movebottom";
inline constexpr const char* kSelChildren = "h.sel.children";
inline constexpr const char* kSelBelow = "h.sel.below";
inline constexpr const char* kSelParent = "h.sel.parent";
inline constexpr const char* kSelSiblings = "h.sel.siblings";
inline constexpr const char* kSelSameName = "h.sel.samename";
inline constexpr const char* kSelSameType = "h.sel.sametype";
inline constexpr const char* kExpandBelow = "h.expandbelow";
inline constexpr const char* kCollapseBelow = "h.collapsebelow";
inline constexpr const char* kActive = "h.active";
inline constexpr const char* kLock = "h.lock";
inline constexpr const char* kFocus = "h.focus";
inline constexpr const char* kIsolate = "h.isolate";
inline constexpr const char* kAddComponent = "h.addcomponent";
inline constexpr const char* kSavePrefab = "h.saveprefab";
inline constexpr const char* kModelTools = "h.modeltools";
inline constexpr const char* kCopyName = "h.copyname";
inline constexpr const char* kCopyPath = "h.copypath";
inline constexpr const char* kSelectAll = "h.selectall";
inline constexpr const char* kDeselect = "h.deselect";
inline constexpr const char* kExpandAll = "h.expandall";
inline constexpr const char* kCollapseAll = "h.collapseall";
inline constexpr const char* kSortAZ = "h.sortaz";
inline constexpr const char* kCreateEmpty = "h.createempty";
inline constexpr const char* kGroupSelected = "h.groupselected";
}  // namespace id

namespace detail {
using menubar::Item; using menubar::Off; using menubar::OnlyIf; using menubar::Sep; using menubar::Sub;
}

/// The menu of one right-clicked row.
inline std::vector<Entry> RowMenu(const RowState& r) {
    using namespace detail;
    namespace key = menubar::key;
    std::vector<Entry> m;
    const int n = r.selectedCount < 1 ? 1 : r.selectedCount;

    // --- clipboard ---
    {
        Entry copy = Item(id::kCopy, "Copy", key::Copy);
        if (n > 1) copy.tip = "Copies the object you right-clicked.";
        m.push_back(copy);
        Entry cut = Item(id::kCut, "Cut", key::Cut);
        // Cut keeps one object on the clipboard but removes the whole selection: not offered for several (nothing is lost).
        m.push_back(n > 1 ? Off(cut, "Cut works on one object: select just one (cutting several would delete them all and keep only one).") : cut);
        m.push_back(OnlyIf(Item(id::kPaste, "Paste", key::Paste, "Paste the copied object next to this one."), r.clipboardHasObject, menubar::kNothingCopied));
        m.push_back(OnlyIf(Item(id::kPasteChild, "Paste as Child", "", "Paste the copied object inside this one."), r.clipboardHasObject, menubar::kNothingCopied));
    }
    m.push_back(Sep());
    // --- rename, duplicate, delete ---
    m.push_back(Item(id::kRename, "Rename", key::Rename));
    m.push_back(Item(id::kDuplicate, menubar::Counted("Duplicate", n), key::Duplicate));
    m.push_back(Item(id::kDelete, menubar::Counted("Delete", n), key::Delete));
    m.push_back(OnlyIf(Item(id::kDeleteChildren, "Delete Children", "", "Empties a container: every child (and what is below it) is removed, this object stays."),
                       r.childCount > 0, "This object has no children."));
    m.push_back(Sep());
    // --- making and changing the hierarchy ---
    m.push_back(Item(id::kEmptyChild, "Create Empty Child"));
    {
        std::vector<Entry> kids;
        for (Entry& c : menubar::CreateCategories()) kids.push_back(std::move(c));
        m.push_back(Sub("h.createchild", "Create Child", std::move(kids), "Create an object inside this one."));
    }
    m.push_back(Item(id::kCreateParent, n > 1 ? "Group " + std::to_string(n) + " Objects" : "Create Parent",
                     n > 1 ? menubar::key::Group : "",
                     "Wraps it in a new empty parent. It keeps its place in the scene."));
    m.push_back(menubar::SlotMenu(id::kParentTo, "Parent To", "Make this object a child of another one (it keeps its place in the scene)."));
    m.push_back(OnlyIf(Item(id::kUnparent, "Unparent (to top level)"), r.hasParent, "It is already at the top level."));
    {
        std::vector<Entry> o;
        const bool first = r.siblingIndex <= 0, last = r.siblingIndex >= r.siblingCount - 1;
        o.push_back(OnlyIf(Item(id::kMoveUp, "Move Up", key::MoveUp), !first, "It is already the first of its siblings."));
        o.push_back(OnlyIf(Item(id::kMoveDown, "Move Down", key::MoveDown), !last, "It is already the last of its siblings."));
        o.push_back(OnlyIf(Item(id::kMoveTop, "Move to Top"), !first, "It is already the first of its siblings."));
        o.push_back(OnlyIf(Item(id::kMoveBottom, "Move to Bottom"), !last, "It is already the last of its siblings."));
        m.push_back(Sub("h.order", "Order", std::move(o), "Where it sits among the objects next to it."));
    }
    m.push_back(Sep());
    // --- selecting and folding ---
    {
        std::vector<Entry> s;
        s.push_back(OnlyIf(Item(id::kSelChildren, "Children"), r.childCount > 0, "This object has no children."));
        s.push_back(OnlyIf(Item(id::kSelBelow, "Everything Below"), r.childCount > 0, "This object has no children."));
        s.push_back(OnlyIf(Item(id::kSelParent, "Parent"), r.hasParent, "It is already at the top level."));
        s.push_back(Item(id::kSelSiblings, "Siblings"));
        s.push_back(Item(id::kSelSameName, "Same Name"));
        {
            Entry t = Item(id::kSelSameType, r.kindName.empty() ? "Same Type" : "Same Type (" + r.kindName + ")",
                           "", "Every object of the same kind: all cameras, all lights, all meshes...");
            s.push_back(r.sameKindOthers == 0 ? Off(t, "No other " + (r.kindName.empty() ? std::string("object of this type") : r.kindName) + " in the scene.") : t);
        }
        m.push_back(Sub("h.select", "Select", std::move(s)));
    }
    if (r.childCount > 0) {
        m.push_back(Item(id::kExpandBelow, "Expand All Below"));
        m.push_back(Item(id::kCollapseBelow, "Collapse All Below"));
    }
    m.push_back(Sep());
    // --- state in the scene ---
    m.push_back(Item(id::kActive, menubar::Counted(r.active ? "Deactivate" : "Activate", n),
                     "", "Takes the whole subtree (and the whole selection) with it."));
    m.push_back(Item(id::kLock, r.locked ? "Unlock in Scene View" : "Lock in Scene View",
                     "", "A locked object (and its children) can't be click-selected in the Scene view."));
    m.push_back(Item(id::kFocus, "Focus in Scene", key::Frame));
    m.push_back(Item(id::kIsolate, r.isolated ? "Exit Isolation" : "Isolate", key::Isolate));
    {
        std::vector<Entry> t;
        t.push_back(Item("h.tf.resetpos", "Reset Position"));
        t.push_back(Item("h.tf.resetrot", "Reset Rotation"));
        t.push_back(Item("h.tf.resetscale", "Reset Scale"));
        t.push_back(Item("h.tf.resetall", "Reset All"));
        t.push_back(Sep());
        t.push_back(Item("h.tf.movetoview", "Move to View Centre", "", "Put it where the Scene view camera is looking."));
        m.push_back(Sub("h.transform", "Transform", std::move(t)));
    }
    m.push_back(Sep());
    // --- components, prefabs, rigs ---
    m.push_back(Item(id::kAddComponent, "Add Component...", "", "Select it and open the Inspector's Add Component browser."));
    if (r.inPrefab) {
        std::vector<Entry> p;
        p.push_back(menubar::Head(r.prefabName));
        p.push_back(Item("h.prefab.open", "Open"));
        p.push_back(Item("h.prefab.selectasset", "Select Prefab Asset", "", "Show the prefab file in the Project panel."));
        p.push_back(Sep());
        p.push_back(OnlyIf(Item("h.prefab.revertall", "Revert All"), r.prefabChanged, "Nothing differs from the prefab."));
        p.push_back(OnlyIf(Item("h.prefab.applyall", "Apply All"), r.prefabChanged, "Nothing differs from the prefab."));
        p.push_back(Item("h.prefab.variant", "Create Variant...", "", "Opens the name box in the Inspector."));
        p.push_back(Item("h.prefab.updateall", "Update All Instances"));
        p.push_back(Sep());
        p.push_back(Item("h.prefab.unpack", "Unpack"));
        p.push_back(OnlyIf(Item("h.prefab.unpackall", "Unpack Completely"), r.prefabHasNested, "No other prefab instances sit inside it."));
        m.push_back(Sub("h.prefab", "Prefab", std::move(p)));
    }
    m.push_back(Item(id::kSavePrefab, "Save As Prefab...", "", "Write this object (and its children) into the project as a .okayprefab."));
    if (r.modelTools) {
        m.push_back(Sep());
        m.push_back(menubar::Slot(id::kModelTools));   // IK targets, built-in animations, humanoid bone map and rigging
    }
    m.push_back(Sep());
    {
        std::vector<Entry> c;
        c.push_back(Item(id::kCopyName, "Name"));
        c.push_back(Item(id::kCopyPath, "Path", "", "The path from the top of the hierarchy, like Canvas/Lobby/Title."));
        m.push_back(Sub("h.copytext", "Copy Name or Path", std::move(c), "Put the name or the path on the clipboard as text."));
    }
    return m;
}

/// The right-click menu of the empty space below the rows.
inline std::vector<Entry> EmptyMenu(const EmptyState& e) {
    using namespace detail;
    std::vector<Entry> m;
    m.push_back(Item(id::kCreateEmpty, "Create Empty"));
    m.push_back(Sep());
    for (Entry& c : menubar::CreateCategories()) m.push_back(std::move(c));
    m.push_back(Sep());
    m.push_back(OnlyIf(Item(id::kPaste, "Paste", menubar::key::Paste), e.clipboardHasObject, menubar::kNothingCopied));
    m.push_back(Sep());
    m.push_back(OnlyIf(Item(id::kSelectAll, "Select All", menubar::key::SelectAll), e.objectCount > 0, "The scene has no objects."));
    m.push_back(OnlyIf(Item(id::kDeselect, "Deselect"), e.selectedCount > 0, "Nothing is selected."));
    m.push_back(Sep());
    m.push_back(OnlyIf(Item(id::kExpandAll, "Expand All"), e.objectCount > 0, "The scene has no objects."));
    m.push_back(OnlyIf(Item(id::kCollapseAll, "Collapse All"), e.objectCount > 0, "The scene has no objects."));
    {
        Entry sort = menubar::Toggle(id::kSortAZ, "Sort A-Z", "List the objects of each level in alphabetical order (only the list: the real order is kept).");
        m.push_back(sort);
    }
    return m;
}

/// The [+] button at the top of the panel: create something, here or inside the selected object.
inline std::vector<Entry> CreatePopup(const EmptyState& e) {
    using namespace detail;
    std::vector<Entry> m;
    const bool sel = e.selectedCount > 0;
    m.push_back(Item(id::kCreateEmpty, "Create Empty"));
    m.push_back(OnlyIf(Item(id::kEmptyChild, "Create Empty Child", "", "A new empty object inside the selected one."), sel, menubar::kNeedSelection));
    m.push_back(OnlyIf(Item(id::kGroupSelected, e.selectedCount > 1 ? "Group " + std::to_string(e.selectedCount) + " Objects" : "Group Selected",
                            menubar::key::Group, "Wraps the selected object(s) in a new empty parent."), sel, menubar::kNeedSelection));
    m.push_back(Sep());
    for (Entry& c : menubar::CreateCategories()) m.push_back(std::move(c));
    return m;
}

}  // namespace hiermenu
}  // namespace oked
