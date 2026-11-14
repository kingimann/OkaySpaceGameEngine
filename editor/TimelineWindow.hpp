#pragma once
// The Timeline window (TimelineWindow.cpp): the cutscene editor for .okayseq
// sequences. Tracks on the left, their shots, keys and cues on a shared ruler,
// the selection's fields on the right; the playhead previews the sequence live
// in the Scene and Game views while the editor is stopped.
#include "EditorState.hpp"
#include "okay/Components/Sequence.hpp"
#include <string>

namespace oked {

// Window visibility flag (defined in TimelineWindow.cpp; the View menu toggles it).
extern bool g_showTimeline;

// Draw the Timeline window (no-op while hidden).
void DrawTimeline(okay::editor::EditorState& ed);

// True while the Timeline window has keyboard focus, so Delete, Space and
// Ctrl+Z act on the sequence, not the scene.
bool TimelineFocused();

// Open a .okayseq file in the window (the Project panel's double-click, the
// Sequence Player inspector) and show it.
void OpenTimelineFile(const std::string& path);

// Put back everything the preview moved (objects and the camera). Called before
// Play and before the scene is rebuilt, so neither snapshots a previewed pose.
void StopTimelinePreview();

// Ctrl+Z / Ctrl+Y while the window is focused: undo or redo a sequence edit.
// False when the window isn't focused or has nothing to undo.
bool TimelineUndo(bool redo);

// The fade and subtitle at the playhead while the preview runs (the Game view
// draws them in edit mode). False when there is no preview.
bool TimelinePreviewOverlay(okay::SeqOverlay& out);

} // namespace oked
