#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "okay/Math/Vec3.hpp"

namespace okay {

class Scene;
class GameObject;

struct ParseSession;

/// A scene document being loaded a few objects at a time. The file is read and
/// unpacked up front (Open*), then Step() creates up to N objects per call; when the
/// last one is made the parent links resolve and the job is Finished. Used by
/// Scene::BeginAsyncLoad; `Step` must be called with the scene the objects go into.
class SceneLoadJob {
public:
    ~SceneLoadJob();
    SceneLoadJob(const SceneLoadJob&) = delete;
    SceneLoadJob& operator=(const SceneLoadJob&) = delete;

    /// Read a scene file. `additive`: objects are added to the scene as a merge (the
    /// host's name / lighting stay, roots move by `offset`); otherwise the scene is
    /// cleared by the first Step and takes the file's settings. Null + `error` if the
    /// file cannot be read.
    static std::unique_ptr<SceneLoadJob> OpenFile(const std::string& path, bool additive,
                                                  const Vec3& offset = Vec3{0, 0, 0}, std::string* error = nullptr);
    static std::unique_ptr<SceneLoadJob> OpenText(std::string text, bool additive,
                                                  const Vec3& offset = Vec3{0, 0, 0});

    /// Create up to `maxObjects` objects (0 = all that are left). False on a parse
    /// error (the job is then Finished and Failed).
    bool Step(Scene& scene, int maxObjects, std::string* error = nullptr);
    bool Started() const { return m_started; }
    bool Finished() const { return m_finished; }
    bool Failed() const { return m_failed; }
    bool Additive() const { return m_additive; }
    /// How much of the document has been read (0..1; 1 once finished OK).
    float Progress() const;
    /// Objects created so far.
    int  ObjectsCreated() const { return m_created; }
    /// Instance ids of the top-level objects made (known once Finished).
    const std::vector<std::uint64_t>& RootIds() const { return m_roots; }
    /// Instance ids of every object made so far.
    std::vector<std::uint64_t> AllIds() const;

private:
    SceneLoadJob() = default;
    std::unique_ptr<ParseSession> m_session;
    std::string m_dir;
    Vec3 m_offset;
    bool m_additive = false, m_started = false, m_finished = false, m_failed = false;
    int  m_created = 0;
    std::size_t m_size = 1;
    std::vector<std::uint64_t> m_roots;
};

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

    /// Collect the unique external asset paths a scene references (sprite
    /// textures, audio WAVs, sprite-animator frames). Used by Build Game to copy
    /// the files a shipped game needs alongside the executable.
    static std::vector<std::string> CollectAssetPaths(const Scene& scene);
};

} // namespace okay
