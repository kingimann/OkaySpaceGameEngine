#pragma once
// The Script Editor app inside the editor (ScriptEditorAppHost.cpp): the same script editor as the Script Editor program
// (shared/ScriptIdeApp.hpp, scripteditor/main.cpp), in an editor window of its own ("Script Editor (Files)", View > Editors &
// Tools), on the open project's folder and with the editor's live data: its language words and hover docs (ScriptAssist), its
// code palette, its plugins' lint (PluginRegistry), "Show in Scene Script Editor" (the editor's Script Editor window, which
// edits the scene's Script components), and a Console line on each save. Drawn by OkayUI only (DrawAppWindowOkayUI).
#include "EditorState.hpp"

namespace oked {

/// The window's visibility (View > Editors & Tools > "Script Editor (Files)").
extern bool g_showScriptEditorApp;
/// Draw the window while g_showScriptEditorApp (every frame, from main).
void DrawScriptEditorApp(okay::editor::EditorState& ed);

} // namespace oked
