#pragma once
// The Dialogue window (DialogueWindow.cpp): the node-graph editor for
// `.okaydialog` conversations (canvas, palette, inspector, validation, a Test
// panel that plays the graph) and the Quests tab for `.okayquest` files. Also
// the inspectors of the story components (main.cpp draws their headers).
#include "EditorState.hpp"
#include <string>

namespace okay { class DialogueRunner; class QuestLog; class QuestTrigger; class StoryState; }

namespace oked {

// Window visibility flag (View > Editors & Tools > Dialogue & Quests).
extern bool g_showDialogue;

// Draw the Dialogue window (no-op while hidden).
void DrawDialogueWindow(okay::editor::EditorState& ed);

// True while the Dialogue window (or a panel inside it) has keyboard focus, so
// its Delete, Ctrl+C/V/D, Ctrl+Z/Y and F act on nodes, not on the scene.
bool DialogueWindowFocused();

// Open a `.okaydialog` (Dialogue tab) or `.okayquest` (Quests tab) file in the window.
void OpenStoryAsset(const std::string& path);

// Inspector bodies for the story components.
void DialogueRunnerInspector(okay::editor::EditorState& ed, okay::DialogueRunner& dr);
void QuestLogInspector(okay::editor::EditorState& ed, okay::QuestLog& ql);
void QuestTriggerInspector(okay::editor::EditorState& ed, okay::QuestTrigger& qt);
void StoryStateInspector(okay::editor::EditorState& ed, okay::StoryState& st);

} // namespace oked
