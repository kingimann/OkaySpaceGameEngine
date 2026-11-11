#pragma once
// File > Examples: build an example scene and save it into the open project, so Play and Stop just work
// (no "save first?" question, and the example is a real scene the user can keep editing). ImGui-free.
#include "EditorState.hpp"
#include <string>

namespace oked {

/// Replace the open scene with the UI Gallery (`portrait`: the phone layout). With a project open the scene is saved
/// as <project>/Assets/Scenes/UI Gallery.okayscene (or "... 2", "... 3" when that name is taken, so nothing is
/// overwritten) and counts as saved. With no project the scene stays unsaved but clean. Returns false with `error`
/// when the file could not be written (the scene is still built and open).
bool BuildUIGalleryExample(okay::editor::EditorState& ed, bool portrait, std::string* savedPath, std::string* error);

} // namespace oked
