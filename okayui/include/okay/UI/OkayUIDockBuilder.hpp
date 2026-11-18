#pragma once
// Describe a dock layout in code, the way the editor's layout presets are written (split a node, put windows in the pieces, finish),
// and say "this window goes in that node" for the next window. Own design over the OkayUI dock (OkayUIDock.hpp): nothing here is a
// second dock system. A builder tree is a PLAN of nodes with ids you choose (or get back); DockBuilderFinish turns the plan into the
// real DockLayout (GetDockLayout()), and SetNextWindowDockID / later DockBuilderDockWindow calls then act on the live layout.
// Written from what the names mean; no Dear ImGui code was read.
//
// Usage (the editor's "Default" preset, in order):
//     DockBuilderRemoveNode(space);                       // forget the old layout
//     DockBuilderAddNode(space);                          // the root, id chosen by you (GetID("Dock") will do)
//     DockBuilderSetNodeSize(space, {1280, 720});         // the area the dock will fill (needed for sizes given in pixels)
//     DockId center = space, left, right, down;
//     left = DockBuilderSplitNode(center, DockSide_Left, 0.20f, nullptr, &center);     // 20 percent to the new left node
//     right = DockBuilderSplitNode(center, DockSide_Right, 0.42f, nullptr, &center);
//     down = DockBuilderSplitNode(center, DockSide_Bottom, 0.22f, nullptr, &center);
//     DockBuilderDockWindow("Hierarchy", left); DockBuilderDockWindow("Scene", center); DockBuilderDockWindow("Game", center); ...
//     DockBuilderFinish(space);                           // the plan becomes the live layout
//
// Differences from the Dear ImGui calls of the same names (all on purpose, listed in docs/own_ui_toolkit.md 6.9):
//  * One dock tree. Finish replaces the layout (windows the plan does not mention stay, floating). A second AddNode root is a second
//    plan; the last one finished wins.
//  * An empty node cannot exist in a DockLayout, so a plan node that holds no window when Finish runs is dropped and its sibling takes
//    its space. The id of a dropped node answers "not found" in DockBuilderGetNode of the live layout and is ignored by
//    SetNextWindowDockID.
//  * A split node keeps its own id; the two halves get new ids (returned by DockBuilderSplitNode). Windows already in the node move to
//    the half on the opposite side of `dir`.
//  * A window is in one node only: docking it again moves it.
#include "okay/UI/OkayUIDock.hpp"

#include <string>
#include <vector>

namespace OkayUI {

using DockId = unsigned;
enum DockNodeFlags : int { DockNode_None = 0, DockNode_DockSpace = 1, DockNode_NoTabBar = 4 /* accepted, ignored */ };

/// Start a plan whose root has `id` (0: make an id). Returns the id, or 0 when `id` is already in use. `flags` is accepted and ignored.
DockId DockBuilderAddNode(DockId id = 0, int flags = DockNode_None);
/// Remove the node and everything below it from the plan; the sibling takes the place of the parent. Removing a root also empties the
/// live layout (its windows float again the next time they are drawn).
void DockBuilderRemoveNode(DockId id);
/// The size the node should have, in pixels. On a root: the area the whole tree fills (used by Finish to turn pixel sizes into ratios).
/// On another node: its size along its parent's split (the ratio is derived at Finish from the root size; ignored when no size is known).
/// On a node of the live layout (after Finish): moves the divider now, from the parent's current size.
void DockBuilderSetNodeSize(DockId id, Vec2 size);
/// Split leaf `id` in two. `dir` is the side the NEW node goes on (Left, Right, Top, Bottom; Center is refused), `sizeRatio` its share
/// of the space (0.02 to 0.98). Returns the new node's id (0 when the node is unknown, already split, or the arguments are bad);
/// `*outIdAtDir` gets the same, `*outIdAtOpposite` the id of the remaining half. Either pointer may be null.
DockId DockBuilderSplitNode(DockId id, DockSide dir, float sizeRatio, DockId* outIdAtDir, DockId* outIdAtOpposite);
/// With a Dear ImGui direction as an int: 0 left, 1 right, 2 up (top), 3 down (bottom); anything else is left.
inline DockId DockBuilderSplitNode(DockId id, int imguiDir, float sizeRatio, DockId* outIdAtDir, DockId* outIdAtOpposite) {
    const DockSide side = imguiDir == 1 ? DockSide_Right : imguiDir == 2 ? DockSide_Top : imguiDir == 3 ? DockSide_Bottom : DockSide_Left;
    return DockBuilderSplitNode(id, side, sizeRatio, outIdAtDir, outIdAtOpposite);
}
/// Put the window in the node (a leaf, or the first leaf under a split). Moves it from any node it was in. On a node that has already
/// been finished the window is docked into the live layout at once. Empty names are ignored.
void DockBuilderDockWindow(const char* window, DockId node);
/// Turn the plan the node belongs to into the live layout. Returns false when the node is unknown or the plan holds no window at all.
bool DockBuilderFinish(DockId id);

/// What a node holds. After Finish, windows and size come from the live layout while the node exists there.
struct DockNodeInfo {
    bool found = false, isLeaf = false, isSplit = false, isRoot = false, live = false;   // live: read from the live layout
    DockId parent = 0, child[2] = {0, 0};          // child[0] is the left / top half
    bool horizontal = true;                        // split only: halves side by side
    float ratio = 0.5f;                            // split only: share of child[0]
    Vec2 size{0, 0};
    std::vector<std::string> windows;              // leaf: its tabs, in order
};
DockNodeInfo DockBuilderGetNode(DockId id);

/// The next DockBegin puts its window in node `id` (a node of a finished plan), or floats it when `id` is 0. Cond_Always: every time;
/// the other conditions (Once, FirstUseEver, Appearing): only while the window is not in the layout at all (a layout loaded from a save
/// that has it wins). One request, used up by the next DockBegin. An unknown or dropped id does nothing.
void SetNextWindowDockID(DockId id, Cond cond = Cond_Always);
/// The builder id of the node the window sits in (live layout), 0 when it floats or is unknown.
DockId GetWindowDockID(const char* window);

// Called by DockBegin and DockReset (OkayUIDock.cpp); not for hosts.
void DockBuilderApplyNext(const std::string& window);
void DockBuilderDropNext();
void DockBuilderReset();

} // namespace OkayUI
