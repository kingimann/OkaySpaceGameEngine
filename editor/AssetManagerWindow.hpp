#pragma once
// The Asset Manager window (AssetManagerWindow.cpp): Marketplace downloads listed by type, imported into the project.
// Moved out of main.cpp unchanged (docs/editor/main_split_plan.md, move 5).
#include "EditorState.hpp"

namespace oked {

// Draws the window while g_showAssetManager (MainShared.hpp) is set (every frame, from main).
void DrawAssetManager(okay::editor::EditorState& ed);

} // namespace oked
