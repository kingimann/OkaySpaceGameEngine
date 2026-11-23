#pragma once
// Find in Files (FindInFiles.cpp): the project-wide script search window (Ctrl+Shift+F in the Script Editor).
// Moved out of main.cpp unchanged (docs/editor/main_split_plan.md, move 5).
#include "EditorState.hpp"

namespace oked {

// The window's visibility, and "focus the search box next frame" (Ctrl+Shift+F and the Script Editor's menu set both).
extern bool g_showFindInFiles;
extern bool g_fifFocus;
// Draws the window while g_showFindInFiles (every frame, from main).
void DrawFindInFiles(okay::editor::EditorState& ed);

} // namespace oked
