#pragma once
#include "EditorState.hpp"
#include "okay/Components/FirstPersonHands.hpp"

namespace okay::editor {
void DrawFpHandsBody(EditorState& ed, FirstPersonHands* hands);
// The same body as OkayUI field rows with a Remove button (the Inspector, stage 8; docs/own_ui_toolkit.md section 21). False: not drawn
// (the preview switch is off), so DrawFpHandsBody is drawn instead. `remove`: Remove was clicked.
bool DrawFpHandsBodyOkayUI(EditorState& ed, FirstPersonHands* hands, bool& remove);
void DrawFpHandsPreviewWindow(EditorState& ed);
}
