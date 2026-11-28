#pragma once
// The menus' scene actions, out of main.cpp so the running editor and the automation ops (AutomationOps.hpp) run the
// same code for the same menu item: Edit (undo, redo, clipboard, duplicate, delete, select), GameObject (create empty,
// child, parent, snap), File > New 2D / 3D Scene, View > Isolate, and the Hierarchy's menus (a row's right-click menu,
// the empty space's menu and the [+] popup). The menus themselves are data (MenuBarModel.hpp, HierarchyContextModel.hpp);
// what an item does that needs a window (a dialog, a panel, the Scene view camera, the OS clipboard) stays in main.cpp.
// ImGui-free, so the headless host and tests/test_automation.cpp run it. Moved from main.cpp's RunMenuAction,
// HierEmptyAction and HierRowAction, which call it; our own code.
#include "EditorState.hpp"
#include "UIMenuModel.hpp"

#include <functional>
#include <string>
#include <vector>

namespace oked {
namespace menuactions {

/// What the actions need from the editor around them: main.cpp gives its own (the Console, its clipboard, the Modeling
/// window's mesh history, the snap size, Isolate); the headless host gives plain ones.
struct Env {
    std::function<void(const std::string& text, int level)> log;   ///< the Console (0 info, 1 warning, 2 error)
    std::string* clipboard = nullptr;                               ///< the copied object, as scene text
    float snapSize = 1.0f;
    std::function<bool(bool redo)> meshHas;                         ///< the Modeling window has a mesh step to undo / redo
    std::function<bool(bool redo)> meshUndo;                        ///< take it back (true when it did)
    okay::GameObject** isolate = nullptr;                           ///< View > Isolate's object
    /// Run `then` once unsaved changes are dealt with (the editor asks the person to save first). False: not run, it
    /// needs a person (the headless host cannot ask).
    std::function<bool(okay::editor::EditorState& ed, const char* what, std::function<void()> then)> afterSavePrompt;
};

/// What a row's menu leaves for after the Hierarchy's rows are drawn (they still use the object this frame).
struct After { bool cut = false, deleteSelection = false, deleteChildren = false, revealSelection = false; };

enum class Outcome { NotHandled, Done, NeedsPerson };

/// Run the menu item `id`. `row` is the Hierarchy row the menu was opened on (null for the menu bar, the empty space's menu
/// and the [+] popup, which act on the selection). NotHandled: the id is not one of these (main.cpp has it).
Outcome Run(okay::editor::EditorState& ed, const std::string& id, okay::GameObject* row, Env& env, After& after);
/// Do what `after` left (the editor calls it once the rows are drawn; the automation ops at once).
void ApplyAfter(okay::editor::EditorState& ed, const After& after, Env& env);

/// Wrap one object in a new empty parent that sits where it is (Hierarchy > Create Parent, GameObject > Create Empty Parent).
void CreateEmptyParentOf(okay::editor::EditorState& ed, okay::GameObject* node);
/// Select these objects (the last one active).
void SelectMany(okay::editor::EditorState& ed, const std::vector<okay::GameObject*>& objs);
/// The objects a Hierarchy row action affects: the whole multi-selection when the row is part of it, else the row.
std::vector<okay::GameObject*> RowTargets(okay::editor::EditorState& ed, okay::GameObject* node);
/// GameObject > UI's ready-made screens: the screen registry's sections (okay/Scene/UIScreens.hpp), as the menu lists them.
const std::vector<oked::uimenu::ScreenGroup>& UIScreenGroups();
/// Lock or unlock an object and everything below it against click-selecting in the Scene view.
void SetLockedRecursive(okay::GameObject* go, bool on);

}  // namespace menuactions
}  // namespace oked
