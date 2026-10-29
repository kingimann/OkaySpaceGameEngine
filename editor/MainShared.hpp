#pragma once
// Functions and state still defined in main.cpp that the split-out panels use.
// This list shrinks as main.cpp is split further; each entry moves to the
// header of the module that ends up owning it.
#include <string>

namespace okay { class GameObject; namespace editor { class EditorState; } }

namespace oked {

// Window visibility and play-mode flags (main.cpp).
extern bool g_showConsole;
extern bool g_paused;              // the simulation is paused while staying in Play
extern bool g_clearConsoleOnPlay;  // wipe the Console each time Play starts

// Script Editor: open a script file in a tab, scrolled to `line` (1-based).
void OpenScriptFileInEditorAt(const std::string& path, int line);

// "Add animations from file...": pick clip files (FBX, glTF, DAE, BVH) and add
// every clip to the Model Animator on `modelRoot`.
void AddAnimationsFromFilesUI(okay::editor::EditorState& ed, okay::GameObject* modelRoot);

} // namespace oked
