#pragma once
// Automation ops: every item of the editor's menus as an automation command, the way Blender makes every UI action an
// operator that Python calls the same way the UI does. The menus are data (MenuBarModel.hpp and the models built on it:
// HierarchyContextModel.hpp, SceneAddMenu.hpp, UIMenuModel.hpp), so the ops are read from them: one command
// `op.<item id>` per Item and Toggle, with the item's label, where it is and its tip as the summary. An item added to a
// model is an op the next time the table is filled, with no other code.
//
//   op.list   every op with its live state: enabled (and why not), checked for a toggle, whether it needs a person
//   op.run    run one by id (the same as calling op.<id>)
//   op.<id>   run the item through the code its click runs: the running editor hands its own menu functions over in
//             Hooks() (main.cpp: RunMenuAction, the Hierarchy's row and empty-space actions, GameObject > UI, the Scene
//             view's add menu, the toggles' flags); without them (the headless host) the menus' shared actions
//             (MenuActions.hpp) run what needs no window, and the rest answers `unavailable`.
//   A greyed-out item is refused with the menu's own reason. An item that opens a dialog a person must answer says so in
//   its summary; when a command does the same with a parameter (a path, a name), the op takes that parameter, and
//   without it answers `needs_ui`. Nothing waits for a click.
// ImGui-free (tests/test_automation.cpp runs it headless). Our own code.
#include "EditorState.hpp"
#include "HierarchyContextModel.hpp"
#include "MenuBarModel.hpp"
#include "okay/Automation/Automation.hpp"

#include <functional>
#include <string>
#include <vector>

namespace oked::automation {

/// One menu the editor draws from data.
struct OpMenu {
    std::string name;         ///< "menubar", "hierarchy.row", ...
    std::string where;        ///< how a person reaches it ("the Hierarchy: right-click a row")
    bool rowTarget = false;   ///< its items act on the object the menu was opened on: the op takes "object"
    /// The entries as the menu shows them now for `ed` (and `row` for a row menu); with `all`, every entry the menu can
    /// show (the state that shows the most): those become the commands.
    std::function<std::vector<oked::menubar::Entry>(okay::editor::EditorState& ed, okay::GameObject* row, bool all)> entries;
};
/// Add a menu. Its items become op.<id> at the next RefreshOps (the editor's own menus are there from the start).
void AddOpMenu(OpMenu m);
const std::vector<OpMenu>& OpMenus();

/// What only the running editor knows and does. main.cpp fills these with the functions its own menus use; left empty
/// (the headless host), the menus' state comes from the editor state alone and MenuActions.hpp runs the items.
struct OpHooks {
    std::function<oked::menubar::State(okay::editor::EditorState&)> menuState;
    std::function<oked::hiermenu::RowState(okay::editor::EditorState&, okay::GameObject*)> rowState;
    std::function<oked::hiermenu::EmptyState(okay::editor::EditorState&)> emptyState;
    /// A Toggle's live flag (null: this host has none).
    std::function<bool*(const std::string& menu, const std::string& id)> flag;
    /// Run an item as its click does. True when this host ran it.
    std::function<bool(okay::editor::EditorState&, const std::string& menu, const std::string& id, okay::GameObject* row)> run;
};
OpHooks& Hooks();

/// Add op.list, op.run and an op.<id> for every Item and Toggle of every menu that has none yet (an id in several menus
/// is one op). Returns how many ops it added. RegisterEditorCommands calls it; call it again after AddOpMenu.
int RefreshOps(okay::automation::Registry& r);
/// How many op.<id> commands there are (aliases included).
int OpCount();
/// The command name of a menu item id: "op." + the id, lower case, other characters as '_'.
std::string OpCommandName(const std::string& id);

/// The ops' commands (op.list, op.run and the items), as a provider (AutomationEditor.cpp adds it).
void AddOpCommands(okay::automation::Registry& r);

}  // namespace oked::automation
