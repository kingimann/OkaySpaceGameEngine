#pragma once
#include "EditorState.hpp"
#include "okay/Components/FirstPersonHands.hpp"

namespace okay::editor {
void DrawFpHandsBody(EditorState& ed, FirstPersonHands* hands);
void DrawFpHandsPreviewWindow(EditorState& ed);
}
