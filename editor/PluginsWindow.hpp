#pragma once
// Editor plugins (PluginsWindow.cpp): load, watch and run OkayScript editor extensions; the Plugins menu and the
// Plugin Manager window. Moved out of main.cpp unchanged (docs/editor/main_split_plan.md, move 5).
#include "EditorState.hpp"

namespace oked {

// Save which plugins are turned off in the Plugin Manager (plugins_disabled.txt next to the editor).
void SavePluginPrefs();
// Create a starter plugin in the project (or user folder) and open it in the Script Editor.
void NewPluginFromTemplate(okay::editor::EditorState& ed);
// Rescan and reload changed plugins, run their on_tick / on_selection hooks and draw their windows (every frame).
void TickPlugins(okay::editor::EditorState& ed);
// The Plugins menu's entries (the main menu bar).
void DrawPluginsMenu(okay::editor::EditorState& ed);
// The Plugin Manager window, drawn while g_showPluginManager (MainShared.hpp) is set.
void DrawPluginManager(okay::editor::EditorState& ed);

} // namespace oked
