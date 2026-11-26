#pragma once
// The Script Editor in the editor (ScriptEditorAppHost.cpp): the editor's one "Script Editor" window (View > Editors & Tools >
// Script Editor, flag g_showScriptEditor) hosts the same app as the Script Editor program (shared/ScriptIdeApp.hpp,
// scripteditor/main.cpp), on the open project's folder and with the editor's live data: the scene's Script components (the
// selected object's scripts open, a scene-only script is saved to Assets/Scripts), its language words, hover docs, completion
// and signatures (ScriptAssist), its code palette, its plugins (lint, formatters, snippets, commands, script_* calls), the
// Scripting Reference, Float / Dock and files dropped from the Project panel. The rest of the editor reaches the open scripts
// through the functions below (Open Script, the quit and save prompts, Save Project, autosave and recovery, Play's compile of
// what is on screen through the code buffers). Drawn by OkayUI only (DrawAppWindowOkayUI).
#include "EditorState.hpp"

#include <string>
#include <utility>
#include <vector>

namespace okay { class ScriptComponent; }

namespace oked {

/// The window's visibility (main.cpp; View > Editors & Tools > Script Editor). Saved layouts know it as "tool.scripteditor".
extern bool g_showScriptEditor;
/// The Script Editor's code (or one of its fields) has the keyboard this frame: the editor hands the hosted window every key
/// (okayuihost::HostedInputWanted) and Space / Enter do not press the ImGui item that had focus. Set when the window draws.
inline bool g_scriptCodeOwnsKeys = false;
/// Draw the window while g_showScriptEditor (every frame, from main).
void DrawScriptEditorApp(okay::editor::EditorState& ed);
/// The Script Editor's Float / Dock request (g_scriptDockReq), applied for one frame before its window begins.
void ApplyScriptDockRequest();

/// Open a script file in a tab at a 1-based line (0: where it was) and show the window.
void ScriptEditorOpenFile(const std::string& path, int line = 0);
/// Show a Script component's script in a tab (the Inspector's "Open in Script Editor").
void ScriptEditorShowComponent(okay::editor::EditorState& ed, okay::ScriptComponent* sc);
/// "Script: <file>" for every tab with unsaved edits (the quit and save prompts), and whether there is any.
std::vector<std::string> ScriptEditorUnsavedTitles();
bool ScriptEditorHasUnsaved();
/// Save every tab with unsaved edits (`withFileOnly`: skip the scene-only ones, as autosave does). The count saved; the first
/// failure's file in `failed`.
int ScriptEditorSaveAll(bool withFileOnly, std::string* failed = nullptr);
/// Every tab with unsaved edits as {file or "untitled-script", text} (document recovery), and putting one back.
std::vector<std::pair<std::string, std::string>> ScriptEditorUnsavedTexts();
void ScriptEditorRestore(const std::string& key, const std::string& text);
/// A tab is open (Ctrl+S in the Script Editor saves it).
bool ScriptEditorHasDocument();
/// Screenshot hooks (OKAY_UI_SCRIPTNEW, OKAY_UI_IDE_POPUP): New Script, Go to File, the commands, Search Everywhere,
/// Rename, the quick fixes.
void ScriptEditorDemo(const std::string& what);

} // namespace oked
