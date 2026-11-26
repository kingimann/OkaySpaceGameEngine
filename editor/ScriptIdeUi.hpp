#pragma once
// ScriptIdeUi: the Script Editor's IDE panels (ScriptIdeUi.cpp), drawn inside the Script Editor window (DrawScriptEditor in
// editor/main.cpp, which calls these as small hooks). The decisions are the headless model's (editor/ScriptIde.hpp) and the
// plugin registry's (editor/PluginRegistry.hpp); this file only draws them and keeps the per-editor state:
//   - recent scripts (per project, .okay/recent_scripts.txt) and Quick Open (Go to File: Ctrl+Shift+N; Ctrl+P stays Play/Stop)
//   - the script tree beside the code (every project script, a filter box; click opens, drag a file onto the code to open it)
//   - the project symbol index, built on a worker thread; Go to Definition and Find References across files
//   - Rename in all project scripts with a preview of every changed line
//   - the quick-fix menu (Alt+Enter, and the light bulb beside the caret's line)
//   - Local History: every save and every 5 minutes of edits keep a copy in <project>/.okay/history; a diff and Restore
//   - plugin side panels and plugin command shortcuts
#include "ScriptIde.hpp"
#include "PluginRegistry.hpp"

#include <functional>
#include <string>
#include <vector>

namespace oked::ide {

// ---- state fed by the Script Editor ----
/// Every frame the Script Editor is drawn: refresh the project's script list now and then, start or collect the index build.
void Tick(const std::string& projectDir);
/// A script was opened in a tab (recent list).
void NoteOpened(const std::string& path);
/// A script was saved: a Local History copy, and the index takes the new text.
void NoteSaved(const std::string& path, const std::string& text);
/// The active script's text this frame: a Local History copy every 5 minutes of edits, and the index sees unsaved text.
void NoteEdited(const std::string& path, const std::string& text);
const std::vector<std::string>& ProjectScripts();
const std::vector<std::string>& RecentScripts();
scriptide::SymbolIndex& Index();
bool SymbolIndexBusy();

// ---- Quick Open and recent files ----
void OpenQuickOpen();
void DrawQuickOpen();
/// Menu items for File > Open Recent (call inside an open menu).
void DrawRecentMenu();

// ---- the script tree ----
/// The tree in a child of the given size; `activePath` is highlighted. Clicking a script opens it.
void DrawScriptTree(float width, float height, const std::string& activePath);

// ---- across files ----
/// F12 when the file itself has no definition: the project's (opens that file at the line). False when there is none.
bool GoToDefinitionInProject(const std::string& word, const std::string& currentPath);
/// Uses of `word` in other project scripts than `currentPath` (for the references list and the rename choice).
std::vector<scriptide::Occurrence> ReferencesElsewhere(const std::string& word, const std::string& currentPath);
/// Open the rename preview for `from` -> `to` in every project script.
void OpenRenamePreview(const std::string& from, const std::string& to);
/// Draw it. `applyOpen(path, text)`: put the new text into an open tab (true) or not open (false: the file is written).
void DrawRenamePreview(const std::function<bool(const std::string&, const std::string&)>& applyOpen);

// ---- quick fixes ----
/// Show the quick-fix menu (Alt+Enter or the light bulb) at a screen point (below the caret); x < 0: at the mouse.
void OpenQuickFixes(const std::vector<scriptide::QuickFix>& fixes, float x = -1.0f, float y = -1.0f);
void DrawQuickFixes(const std::function<void(const scriptide::QuickFix&)>& apply);

// ---- local history ----
void OpenLocalHistory(const std::string& path, const std::string& currentText);
/// Draw it; `restore(text)` puts an old version back into the open script (one undo step).
void DrawLocalHistory(const std::function<void(const std::string&)>& restore);

// ---- plugins ----
/// Run the plugin command whose shortcut was pressed this frame (call while the Script Editor has the focus).
bool RunPluginShortcut();
/// The plugin side panels, each in a collapsing section (nothing when there are none). Returns true if any was drawn.
bool DrawPluginPanels();

} // namespace oked::ide
