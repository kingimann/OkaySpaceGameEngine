#pragma once
// Scene Manager window: project scenes, file actions, and the build order.

#include <string>
#include <vector>

struct SDL_Texture;

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

SDL_Texture* GetSceneThumb(const std::string& path);

} // namespace oked
