#pragma once
// The Visual Script window of the editor (VisualScriptAppHost.cpp): the window "Visual Script" (View > Editors & Tools > Visual Script,
// flag g_showVisualScript in main.cpp, saved layouts' "tool.visualscript"; the old "tool.visualscriptapp" key of the round-1 "Visual
// Script (App)" window opens the same window) hosts the same app as the Visual Script program (shared/VisualScriptApp.hpp,
// visualscript/main.cpp) on the open project, over the editor's own open graphs (okay::editor::vsed::Session(): the tabs the Inspector,
// Play and Save Project also use), with the editor's hooks: its scene (object lists, Attach to Selected Object, a block's missing
// component and Add Component), saving publishes the graph to the scene's objects (PublishOpenGraph), the running graph's values
// while playing, Stop Play, files and objects dragged from the Project and Hierarchy panels. Drawn by OkayUI only (DrawAppWindowOkayUI).
// It replaced the editor's Dear ImGui Visual Script window (VisualScriptEditor.hpp, deleted: rule 19d).
#include "EditorState.hpp"

namespace oked {

/// Draw the window while main.cpp's g_showVisualScript is set (every frame, from main). Sets g_scriptCodeOwnsKeys while the app has
/// the keyboard.
void DrawVisualScriptApp(okay::editor::EditorState& ed);

} // namespace oked
