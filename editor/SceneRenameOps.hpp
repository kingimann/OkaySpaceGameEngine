#pragma once
// Scene Manager > Rename, the file part (ImGui-free so tests can run it).
// Issue #888 finding 141: renaming the open scene used to save the in-memory scene (unsaved edits included)
// under the new name and then delete the saved file. Now every rename only moves the file on disk; the open
// scene follows the new path and keeps its unsaved edits in memory, still unsaved.
#include <filesystem>
#include <functional>
#include <string>
#include <system_error>

namespace oked {

struct SceneRenameHooks {
    /// Never called by RenameSceneFile: renaming must not save. Kept so a caller (or a test) can check that.
    std::function<bool(const std::string&)> saveOpenSceneAs;
    /// Keep editing the open scene under its new path (EditorState::FollowMovedFile).
    std::function<void(const std::string&)> followOpenScene;
};

/// Rename the scene file `from` to `dest` on disk. `isOpenScene` is true when `from` is the scene open in the
/// editor: after the move the editor follows it. Nothing is saved and nothing is deleted. On failure the
/// file and the editor are left as they were and `error` says why.
inline bool RenameSceneFile(const std::string& from, const std::string& dest, bool isOpenScene,
                            const SceneRenameHooks& hooks, std::string& error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_regular_file(from, ec)) { error = "the scene file is missing."; return false; }
    fs::rename(from, dest, ec);
    if (ec) { error = ec.message(); return false; }
    if (isOpenScene && hooks.followOpenScene) hooks.followOpenScene(dest);
    return true;
}

}  // namespace oked
