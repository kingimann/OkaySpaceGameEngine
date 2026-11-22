#pragma once
// Bringing models and sounds that are already in the open scene up to date when their file changes on disk, is
// reimported, or gets new import settings (the Project panel's Browser::TakeChangedFiles names those files). Pictures
// have their own path (okay/Render/TextureCacheFiles.hpp).
//
// A placed model is refreshed in place. Kept: every object (same instance id and GUID), the transforms of the model's
// parts and of the top object, objects you added under it, and the material of every part (colour, textures, shader
// and the other material fields of its Mesh Renderer). Taken from the file again: the geometry of each part (Mesh
// Renderer mesh, skinned mesh bind pose and weights, blend shapes) and its animation clips. A part the file gained
// is added; a part the file lost is removed, and anything you put under it moves up to the nearest part that is still
// there, keeping its place in the world. A part you renamed counts as yours (it is kept) and the file's part is added
// again. Import settings take effect the same way: Normals and Import animations through the fresh import, and Scale
// by changing the top object's size by new Scale / old Scale.
// Header note: keep this header free of <mutex>, <thread> and <condition_variable>.
#include <string>
#include <vector>

namespace okay {

class Scene;
class GameObject;
class ModelSource;

/// Give a model placed from `path` its ModelSource (or update it): the file, the import Scale already multiplied
/// into its size, and its parts as they are now. The editor calls this right after placing a model.
ModelSource* MarkModelSource(GameObject* root, const std::string& path, float appliedScale);

/// The path of `node` below `root` ("" for the root itself, or when `node` is not under it): names joined by '/',
/// with "#k" added for the k-th later sibling of the same name ("Arm", "Arm#1").
std::string ModelNodePath(const GameObject* root, const GameObject* node);

/// What a refresh did.
struct ModelRefreshReport {
    int instances = 0;            ///< placed models refreshed
    int meshesUpdated = 0;        ///< parts whose geometry was replaced
    int added = 0;                ///< parts the file gained (added under the model)
    int removed = 0;              ///< parts the file lost (removed)
    int userChildrenKept = 0;     ///< your objects moved up because the part they were under was removed
    bool scaleChanged = false;    ///< a new import Scale changed a model's size
    std::vector<std::string> failed;   ///< files that did not import (the placed models were left as they were)
    /// One line for the Console: "Refreshed 2 copies of crate.glb: 3 meshes updated, 1 part added".
    std::string Summary() const;
};

/// Refresh one placed model (its ModelSource names the file). False, with nothing changed, when it has no
/// ModelSource or the file does not import.
bool RefreshModelInstance(Scene& scene, GameObject* root, ModelRefreshReport* report = nullptr);

/// Refresh every model in `scene` placed from `file` (any spelling of the same file).
ModelRefreshReport RefreshModelsForFile(Scene& scene, const std::string& file);

/// Every Audio Source in `scene` that has `file` loaded (as a clip or a stream) loads it again, so a changed sound
/// or a new Mix to mono / Load mode setting is heard at once. Sources that name the file but have nothing loaded are
/// left alone (they read it when they load). Returns how many were loaded again.
int RefreshAudioForFile(Scene& scene, const std::string& file);

/// True when `a` and `b` name the same file: relative (to the working folder), "./" and ".." forms, other separators
/// and absolute spellings agree; a spelling the names cannot settle is asked of the file system.
bool SameAssetFile(const std::string& a, const std::string& b);

namespace detail {
void RegisterModelSourceProperties();   // Inspector rows (called with the built-in properties)
void RegisterModelSourceSerializer();   // the `modelsource` scene record (called with the built-in serializers)
}

} // namespace okay
