#pragma once
#include <string>
#include <vector>
#include "okay/Math/Vec3.hpp"
#include "okay/Scene/GameObject.hpp"

namespace okay {

class Scene;
class GameObject;

/// Saves and loads a Scene to a simple, human-readable text format. Covers the
/// built-in components (Transform, SpriteRenderer, Camera, MeshRenderer,
/// Rigidbody2D, box/circle colliders) and preserves the parent/child hierarchy.
/// Used by the editor's File menu and for prefab instantiation.
class SceneSerializer {
public:
    /// The `okayscene` version this build writes. A file with a larger version
    /// still loads (records this build doesn't know are skipped, with a warning);
    /// version 0 or a non-number is an error.
    static constexpr int kSceneFormatVersion = 3;

    // Every loader below is all-or-nothing: if the text is bad (no header, a bad
    // version or count, a record cut short, a duplicate object index) it returns
    // false / nullptr with `error` set and the scene exactly as it was. Records a
    // newer build wrote that this one doesn't know are skipped, not fatal; their
    // names go in `warnings` when given, else to stderr.

    /// Serialize an entire scene to a string.
    static std::string Serialize(const Scene& scene);
    /// Write a scene to a file. Returns false on I/O error.
    static bool SaveToFile(const Scene& scene, const std::string& path);

    /// Rebuild `scene` (cleared first, but only once the text has parsed) from
    /// serialized text. Returns false on a parse error, with details in `error`.
    static bool Deserialize(Scene& scene, const std::string& text, std::string* error = nullptr,
                            std::vector<std::string>* warnings = nullptr);
    /// Load a scene from a file.
    static bool LoadFromFile(Scene& scene, const std::string& path, std::string* error = nullptr,
                             std::vector<std::string>* warnings = nullptr);

    /// Additively MERGE a serialized scene into `scene` — the host scene is NOT
    /// cleared and keeps its own name / gravity / lighting / fog. Every new root
    /// object is translated by `offset`, so scene "chunks" can be dropped side by
    /// side into one seamless world. `addedRoots` (optional) receives the new
    /// top-level objects. Returns false on a parse error.
    static bool MergeFromText(Scene& scene, const std::string& text, const Vec3& offset = Vec3{0, 0, 0},
                              std::vector<GameObject*>* addedRoots = nullptr, std::string* error = nullptr,
                              std::vector<std::string>* warnings = nullptr);
    /// Merge a scene FILE into `scene` (see MergeFromText).
    static bool MergeFromFile(Scene& scene, const std::string& path, const Vec3& offset = Vec3{0, 0, 0},
                              std::vector<GameObject*>* addedRoots = nullptr, std::string* error = nullptr,
                              std::vector<std::string>* warnings = nullptr);

    /// Serialize a single GameObject and its descendants (a prefab).
    static std::string SerializeObject(const GameObject& root);
    /// Clone `prefab` (and its descendants) into `scene`; returns the new root,
    /// or nullptr (nothing added) if the clone's text fails to parse.
    static GameObject* Instantiate(Scene& scene, const GameObject& prefab);

    /// Save a GameObject (and descendants) to a .okayprefab file.
    static bool SaveObjectToFile(const GameObject& root, const std::string& path);
    /// Instantiate a prefab file into `scene`; returns the new root (or nullptr).
    static GameObject* InstantiateFromFile(Scene& scene, const std::string& path,
                                           std::string* error = nullptr,
                                           std::vector<std::string>* warnings = nullptr);
    /// Instantiate an object from serialized text (e.g. an editor clipboard).
    static GameObject* InstantiateFromText(Scene& scene, const std::string& text,
                                           std::string* error = nullptr,
                                           std::vector<std::string>* warnings = nullptr);

    /// Prefab overrides (data only; see docs/prefabs.md). Compare the instance with
    /// its source prefab file and store every record that differs in
    /// `instanceRoot.prefabOverrides`. Call it while the prefab file still matches
    /// what the instance was made from (a later prefab edit would otherwise read as
    /// an override). False, with `error`, if it is not an instance or the file is bad.
    static bool CapturePrefabOverrides(GameObject& instanceRoot, std::string* error = nullptr);
    /// Instantiate a prefab file with `overrides` swapped in over its records.
    static GameObject* InstantiatePrefab(Scene& scene, const std::string& path,
                                         const std::vector<GameObject::PrefabOverride>& overrides,
                                         std::string* error = nullptr, std::vector<std::string>* warnings = nullptr);
    /// Rebuild an instance from its (possibly edited) prefab file, keeping its
    /// overrides, name, parent, sibling position and persistent ids. Returns the new
    /// root and queues the old one for destruction; nullptr leaves the instance as it was.
    static GameObject* ReapplyPrefab(Scene& scene, GameObject& instanceRoot, std::string* error = nullptr,
                                     std::vector<std::string>* warnings = nullptr);

    /// Collect the unique external asset paths a scene references (sprite
    /// textures, audio WAVs, sprite-animator frames). Used by Build Game to copy
    /// the files a shipped game needs alongside the executable.
    static std::vector<std::string> CollectAssetPaths(const Scene& scene);
};

} // namespace okay
