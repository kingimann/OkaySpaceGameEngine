#pragma once
#include <string>
#include <vector>
#include "okay/Math/Vec3.hpp"

namespace okay {

class Scene;
class GameObject;

/// Saves and loads a Scene to a simple, human-readable text format. Covers the
/// built-in components (Transform, SpriteRenderer, Camera, MeshRenderer,
/// Rigidbody2D, box/circle colliders) and preserves the parent/child hierarchy.
/// Used by the editor's File menu and for prefab instantiation.
class SceneSerializer {
public:
    /// Serialize an entire scene to a string.
    static std::string Serialize(const Scene& scene);
    /// Write a scene to a file. Returns false on I/O error.
    static bool SaveToFile(const Scene& scene, const std::string& path);

    /// Rebuild `scene` (cleared first) from serialized text. Returns false on
    /// a parse error, with details in `error` when provided.
    static bool Deserialize(Scene& scene, const std::string& text, std::string* error = nullptr);
    /// Load a scene from a file.
    static bool LoadFromFile(Scene& scene, const std::string& path, std::string* error = nullptr);

    /// Additively MERGE a serialized scene into `scene` — the host scene is NOT
    /// cleared and keeps its own name / gravity / lighting / fog. Every new root
    /// object is translated by `offset`, so scene "chunks" can be dropped side by
    /// side into one seamless world. `addedRoots` (optional) receives the new
    /// top-level objects. Returns false on a parse error.
    static bool MergeFromText(Scene& scene, const std::string& text, const Vec3& offset = Vec3{0, 0, 0},
                              std::vector<GameObject*>* addedRoots = nullptr, std::string* error = nullptr);
    /// Merge a scene FILE into `scene` (see MergeFromText).
    static bool MergeFromFile(Scene& scene, const std::string& path, const Vec3& offset = Vec3{0, 0, 0},
                              std::vector<GameObject*>* addedRoots = nullptr, std::string* error = nullptr);

    /// Serialize a single GameObject and its descendants (a prefab).
    static std::string SerializeObject(const GameObject& root);
    /// Clone `prefab` (and its descendants) into `scene`; returns the new root.
    static GameObject* Instantiate(Scene& scene, const GameObject& prefab);

    /// Save a GameObject (and descendants) to a .okayprefab file.
    static bool SaveObjectToFile(const GameObject& root, const std::string& path);
    /// Instantiate a prefab file into `scene`; returns the new root (or nullptr).
    static GameObject* InstantiateFromFile(Scene& scene, const std::string& path,
                                           std::string* error = nullptr);
    /// Instantiate an object from serialized text (e.g. an editor clipboard).
    static GameObject* InstantiateFromText(Scene& scene, const std::string& text,
                                           std::string* error = nullptr);

    /// Collect the unique external asset paths a scene references (sprite
    /// textures, audio WAVs, sprite-animator frames). Used by Build Game to copy
    /// the files a shipped game needs alongside the executable.
    static std::vector<std::string> CollectAssetPaths(const Scene& scene);
};

} // namespace okay
