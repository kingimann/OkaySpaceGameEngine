#pragma once
// The Behavior Tree window (BehaviorTreeWindow.cpp): the tree editor for `.okaybt`
// assets (a top-down canvas, a node palette, the node inspector, the blackboard,
// validation, a live view while the game runs), the Behavior Tree Runner's
// Inspector and Add Component row, and the dev screenshot hooks.
#include "EditorState.hpp"
#include <functional>
#include <string>

namespace okay { class Component; class GameObject; }

namespace oked {

// Window visibility flag (defined in BehaviorTreeWindow.cpp).
extern bool g_showBehaviorTree;

// Draw the Behavior Tree window (no-op while hidden).
void DrawBehaviorTreeWindow(okay::editor::EditorState& ed);

// True while the window (or a panel inside it) has keyboard focus, so its
// Delete, Ctrl+Z/Y/C/V/D and F act on nodes, not on the scene selection.
bool BehaviorTreeWindowFocused();

// Open a .okaybt file in the window (the Project panel's double-click).
void OpenBehaviorTreeFile(okay::editor::EditorState& ed, const std::string& path);

// The Inspector of a Behavior Tree Runner (a no-op for other components).
// `header` is the Inspector's collapsing component header (CompHeader).
using BtCompHeaderFn = bool (*)(const char*, okay::Component*, okay::Component**, bool);
void DrawBehaviorTreeInspector(okay::editor::EditorState& ed, okay::GameObject* go, okay::Component* comp,
                               okay::Component*& toRemove, BtCompHeaderFn header);

// The Add Component row (Gameplay). `item(absent, label)` is the menu's row widget.
void BehaviorTreeAddComponentItems(okay::editor::EditorState& ed, okay::GameObject* go,
                                   const std::function<bool(bool, const char*)>& item);

// Dev screenshot hooks: OKAY_UI_BT=1 opens the window, OKAY_UI_BTGUARD=1 writes the
// Guard sample tree into the project, puts a guard crate with waypoints and a baked
// navmesh into the scene and opens the tree, OKAY_UI_BTSEL=<node name> selects that
// node, OKAY_UI_BTTAB=<n> picks the side tab.
void BehaviorTreeDevHooks(okay::editor::EditorState& ed);

} // namespace oked
