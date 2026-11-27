#pragma once
// The Visual Script app in the editor (VisualScriptAppHost.cpp): the window "Visual Script (App)" (View > Editors & Tools, flag
// g_showVisualScriptApp, saved layouts' "tool.visualscriptapp") hosts the same app as the Visual Script program
// (shared/VisualScriptApp.hpp, visualscript/main.cpp) on the open project, over the editor's own open graphs
// (okay::editor::vsed::Session(): the tabs the Visual Script window, the Inspector, Play and Save Project already use), with the
// editor's hooks: saving publishes the graph to the scene's objects (PublishOpenGraph), the running graph's values while playing,
// Stop Play, a .okayvs dropped from the Project panel. Drawn by OkayUI only (DrawAppWindowOkayUI).
// The editor's Dear ImGui "Visual Script" window stays until every one of its features is in the app (docs/status/UNFINISHED.md,
// "Visual Script app": the checklist), then it is deleted (rule 19d).
#include "EditorState.hpp"

namespace oked {

/// The window's visibility (main.cpp; View > Editors & Tools > Visual Script (App)).
extern bool g_showVisualScriptApp;
/// Draw the window while g_showVisualScriptApp (every frame, from main). Sets g_scriptCodeOwnsKeys while the app has the keyboard.
void DrawVisualScriptApp(okay::editor::EditorState& ed);

} // namespace oked
