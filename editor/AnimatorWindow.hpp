#pragma once
// The Animator window (AnimatorWindow.cpp): the state-machine graph editor.
#include "EditorState.hpp"

namespace oked {

// Window visibility flag (defined in main.cpp with the other window flags).
extern bool g_showAnimatorGraph;

// Draw the Animator window for the selected object's state machine (no-op while hidden).
void DrawAnimatorGraph(okay::editor::EditorState& ed);

// True while the Animator window (or a panel inside it) has keyboard focus, so
// its Delete, Ctrl+C/V/D and F act on states, not on the scene selection.
bool AnimatorGraphFocused();

} // namespace oked
