#pragma once
// The menu bar in window mode (docs/ui/own_ui_toolkit.md 6.24, round 12): the editor's real MenuBarModel (editor/MenuBarModel.hpp: the entries, their shortcuts,
// greying and reasons) drawn by the editor's own MenuEntriesUI.hpp compiled against OkayUI, with the actions the window-mode editor can do on its own:
// File (New 2D / 3D, Save, Save As, Exit), Edit (Undo, Redo, Cut, Copy, Paste, Duplicate, Delete, Select All, Deselect), GameObject (Create Empty / Child / Parent
// and the primitives), View (the panels' show/hide flags, Frame Selected) and Help > Keyboard Shortcuts; every other entry is shown and tells the Console it is
// not available in window mode yet. HandleShortcuts runs the keys listed next to the entries.
#include "OkayUIHostMode.hpp"
#include "okay/UI/OkayUI.hpp"

#include <string>

namespace okay { namespace editor { class EditorState; } }

namespace oked {
namespace wm {

/// Wire the menu bar and the shortcuts over the editor state into `panels` (panels.menu). `st` (may be null) owns the panels' open flags the View > Panels items flip.
void AddMenuBar(HostModeState::Panels& panels, HostModeState* st, okay::editor::EditorState* ed);

/// Height of the bar in window pixels (what the dock area starts below).
float MenuBarHeight();

/// One menu action by id ("edit.undo", "go.empty", ...). True when it was done, false when window mode has no implementation (it is logged).
bool RunMenuAction(okay::editor::EditorState& ed, HostModeState* st, const std::string& id);

/// The key combinations of the menu entries. Call once per frame with the frame's input; skips when a text field has the keyboard. Returns the id it ran, or "".
std::string HandleShortcuts(okay::editor::EditorState& ed, HostModeState* st, const OkayUI::Input& in);

struct MenuProbe {
    OkayUI::Rect topMenu[6];     // File, Edit, GameObject, View, Plugins, Help (window pixels; empty when not drawn)
    std::string lastAction;      // the id of the last action run (menu or shortcut)
    int itemsDrawn = 0;          // menu entries drawn while a menu was open, last frame
    OkayUI::Rect lastItem;       // the last entry drawn
};
const MenuProbe& GetMenuProbe();

} // namespace wm
} // namespace oked
