#pragma once
// Items, loot tables and crafting recipes (ContentEditors.cpp): one window with
// three tabs. Items are the project's items.okayitems; each loot table is an
// .okayloot file and each recipe book an .okayrecipes file, in the formats the
// gameplay systems already load. The View menu and the Window menu open it.
#include "EditorState.hpp"
#include <string>

namespace oked {

extern bool g_showItemDatabase;

void DrawContentEditors(okay::editor::EditorState& ed);
// True while the window (or its delete confirmation) has focus, so Ctrl+Z / Ctrl+Y
// undo this window and not the scene.
bool ContentEditorsFocused();
// Undo or redo the window's own edits. False when there is nothing to do.
bool ContentEditorsUndo(okay::editor::EditorState& ed, bool redo);

// Open the window on the tab that edits `path` (.okayitems, .okayloot, .okayrecipes).
void OpenContentFile(const std::string& path);
// Open the window on a tab: 0 items, 1 loot tables, 2 recipes.
void OpenContentTab(int tab);
// + New Item, the same action as the Items tab and the Project panel's Item command.
void ContentAddItem(okay::editor::EditorState& ed);
void ContentCreateLoot(okay::editor::EditorState& ed);
void ContentCreateRecipe(okay::editor::EditorState& ed);

// Dev screenshots: OKAY_UI_CONTENT=1 (or OKAY_UI_ITEMS=1) opens the window,
// OKAY_UI_CONTENT_TAB=0|1|2 picks a tab, OKAY_UI_CONTENT_DIR is a folder of sample files.
void ContentEditorsDevHooks(okay::editor::EditorState& ed);

} // namespace oked
