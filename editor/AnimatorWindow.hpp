#pragma once
// The Animator window (AnimatorWindow.cpp): the state-machine graph editor.
#include "EditorState.hpp"

namespace oked {

// Window visibility flag (defined in main.cpp with the other window flags).
extern bool g_showAnimatorGraph;

// Draw the Animator window for the selected object's state machine (no-op while hidden).
void DrawAnimatorGraph(okay::editor::EditorState& ed);

} // namespace oked
