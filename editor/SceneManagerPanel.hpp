#pragma once
// Scene Manager window: project scenes, file actions, and the build order.

#include "okay/UI/UIPainter.hpp"   // okay::ui2d::Tex
#include <string>
#include <vector>


namespace okay { namespace editor { class EditorState; } }

namespace oked {

struct SceneBuildEntry {
    std::string path;
    bool on = true;
};

// Docked window (View > Panels > Scene Manager).
extern bool g_showScenes;
void DrawSceneManager(okay::editor::EditorState& ed);

// Once a frame: load this project's saved build list, keep it on disk when the
// live list changes, and publish it to the engine SceneManager.
void SceneBuildSync(okay::editor::EditorState& ed);

// The live "Scenes In Build" list (also what Build Settings edits).
std::vector<SceneBuildEntry> SceneBuildSnapshot();
void SceneBuildApply(okay::editor::EditorState& ed, const std::vector<SceneBuildEntry>& entries);

okay::ui2d::Tex GetSceneThumb(const std::string& path);

} // namespace oked
