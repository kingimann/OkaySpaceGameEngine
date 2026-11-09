#pragma once
// Prefab tools, wave 2 (see docs/prefab_system.md): nested prefabs, prefab variants,
// unpack, per-record and per-field apply / revert, prefab queries and an asset
// validator. Built on the wave 1 data in GameObject (prefabSource, prefabOverrides)
// and the text format of SceneSerializer. No threads or locks, so it is safe to
// include anywhere.
#include <string>
#include <vector>
#include "okay/Scene/GameObject.hpp"

namespace okay {

class Scene;

/// One thing the validator or a load found wrong in a prefab file.
struct PrefabIssue {
    enum class Severity { Info, Warning, Error };
    Severity severity = Severity::Warning;
    std::string file;      ///< the prefab file the problem is in
    std::string message;   ///< readable, names the object / file involved
};

/// What PrefabTools::ValidateAsset found.
struct PrefabReport {
    std::string path;                 ///< the file that was checked
    std::vector<std::string> files;   ///< every prefab file reached (the file itself first)
    std::vector<PrefabIssue> issues;
    /// True when there are no errors (warnings are allowed).
    bool Ok() const;
    int Count(PrefabIssue::Severity s) const;
    /// One line per issue, "error: file: message".
    std::string ToString() const;
};

/// One field of a record that differs between an instance and its prefab. Fields of
/// well known records have names (transform: position.x ... scale.z; light:
/// intensity ...); a `key=value` record uses its keys; anything else is `arg<N>`.
struct PrefabFieldDiff {
    std::string name;
    std::string prefabValue;
    std::string instanceValue;
    bool inPrefab = true;      ///< false: the prefab record does not have this field
    bool inInstance = true;
};

/// A record (one component's saved line, or `tag`, `active`...) that differs.
struct PrefabRecordDiff {
    enum class Kind {
        Changed,   ///< both have it, the text differs
        Added,     ///< only the instance has it (a component added to the instance)
        Removed    ///< only the prefab has it (a component the instance lost)
    };
    Kind kind = Kind::Changed;
    int object = 0;                ///< depth-first index of the object in the prefab (0 = root)
    std::string objectName;
    std::string record;            ///< record name, e.g. "transform", "light", "tag"
    int occurrence = 0;            ///< which one of that name on the object (two colliders)
    std::string prefabLine;        ///< empty for Added
    std::string instanceLine;      ///< empty for Removed
    /// Per-field differences (Changed only). Empty with `wholeRecordOnly` when the two
    /// lines have a different shape, so only the whole record can be applied or reverted.
    std::vector<PrefabFieldDiff> fields;
    bool wholeRecordOnly = false;
    /// "Transform position.x: 0 -> 4" style summary.
    std::string Describe() const;
};

/// An object the instance gained or lost compared with its prefab. Reported only:
/// apply and revert of single records leave these alone (see docs).
struct PrefabObjectDiff {
    enum class Kind { Added, Removed };
    Kind kind = Kind::Added;
    int object = -1;               ///< Removed: prefab index; Added: -1
    int parentObject = -1;         ///< prefab index of the parent (Added); -1 if unknown
    std::string name;
    std::string path;              ///< "Root/Arm/Hand" inside the instance
    GameObject* live = nullptr;    ///< Added: the instance's object (only until it is destroyed)
};

/// How an instance differs from its source prefab.
struct PrefabDiff {
    std::vector<PrefabRecordDiff> records;
    std::vector<PrefabObjectDiff> objects;
    bool Empty() const { return records.empty() && objects.empty(); }
    const PrefabRecordDiff* Find(int object, const std::string& record, int occurrence = 0) const;
};

class PrefabTools {
public:
    /// Most levels of nesting plus variant chain a prefab may have. Deeper is an error.
    static constexpr int kMaxDepth = 16;

    // ---- File kinds ---------------------------------------------------------------

    /// True when `text` is a variant file (`variant "<base>"`) or has nested prefab
    /// objects (`nestedprefab "<file>"`). SceneSerializer routes such files here.
    static bool UsesPrefabStructure(const std::string& text);
    /// True when the prefab file at `path` is a variant or has nested prefabs.
    static bool FileUsesPrefabStructure(const std::string& path);

    // ---- Instantiate / re-apply ---------------------------------------------------

    /// Instantiate a prefab file (plain, nested or variant) with `overrides` swapped in.
    /// Nested instances are instantiated and linked (their own prefabSource and
    /// overrides). All or nothing: on error the scene is untouched and nullptr is
    /// returned. A cycle (A holds B holds A), nesting deeper than kMaxDepth, or a bad
    /// top-level file is an error; a nested instance whose file is missing or broken is
    /// skipped with a warning and the rest is built.
    static GameObject* Instantiate(Scene& scene, const std::string& path,
                                   const std::vector<GameObject::PrefabOverride>& overrides = {},
                                   std::string* error = nullptr, std::vector<std::string>* warnings = nullptr);
    /// Rebuild an instance from its (edited) file keeping its stored overrides, name,
    /// parent, sibling slot and persistent ids (matched by position and name), and the
    /// objects added under it. The old instance is queued for destruction. nullptr
    /// (instance untouched) on any error, including a broken nested instance.
    static GameObject* Reapply(Scene& scene, GameObject& instanceRoot, std::string* error = nullptr,
                               std::vector<std::string>* warnings = nullptr);
    /// Compare the instance (and its nested instances) with their files and store the
    /// differing records in `prefabOverrides`. Works for plain, nested and variant files.
    static bool CaptureOverrides(GameObject& instanceRoot, std::string* error = nullptr);

    // ---- Unpack -------------------------------------------------------------------

    /// Remove the prefab link (prefabSource and prefabOverrides) from the outermost
    /// instance containing `go`; with `completely` also from every nested instance.
    /// Nested instances left linked get their overrides refreshed first, so what the
    /// outer instance changed inside them is kept. Objects and ids stay. Returns the
    /// number of links removed (0 when `go` is not in an instance).
    static int Unpack(GameObject& go, bool completely = false);

    // ---- Queries ------------------------------------------------------------------

    /// True when `go` is an instance root or sits under one.
    static bool IsPrefabInstance(const GameObject& go);
    /// The outermost instance root at or above `go`; nullptr when there is none.
    static GameObject* PrefabRoot(const GameObject& go);
    /// The innermost instance root at or above `go`; nullptr when there is none.
    static GameObject* NearestPrefabRoot(const GameObject& go);
    /// Instance roots strictly below `root` (depth-first). `recursive` false: only the
    /// ones not inside another nested instance.
    static std::vector<GameObject*> NestedInstances(const GameObject& root, bool recursive = true);
    /// The overrides stored on an instance root.
    static std::vector<GameObject::PrefabOverride> OverridesOf(const GameObject& root);

    // ---- Differences, apply, revert -----------------------------------------------

    /// How the instance differs from its prefab file, per record and per field, plus
    /// added and removed objects. False (with `error`) if it is not an instance or the
    /// file cannot be read.
    static bool Diff(const GameObject& instanceRoot, PrefabDiff& out, std::string* error = nullptr);
    /// Make one record (or one field of it, when `field` is not empty) of the instance
    /// equal to the prefab again. The instance is rebuilt (ids, name, place and added
    /// objects are kept; other differences stay) and the new root is returned; the old
    /// one is queued for destruction. nullptr, instance untouched, on error.
    static GameObject* RevertOverride(Scene& scene, GameObject& instanceRoot, int object, const std::string& record,
                                      const std::string& field = "", int occurrence = 0,
                                      std::string* error = nullptr, std::vector<std::string>* warnings = nullptr);
    /// Rebuild the instance exactly as its prefab says (every override gone, added
    /// objects dropped, lost objects back).
    static GameObject* RevertAll(Scene& scene, GameObject& instanceRoot, std::string* error = nullptr,
                                 std::vector<std::string>* warnings = nullptr);
    /// Write one record (or one field of it) of the instance into the prefab file, then
    /// re-capture the instance's overrides. The file is rewritten atomically. For a
    /// variant the change is stored as an override of the variant, for a nested
    /// instance as an override on the nested object in the outer prefab. A record the
    /// instance lost cannot be applied to a prefab that gets it from elsewhere.
    static bool ApplyToSource(GameObject& instanceRoot, int object, const std::string& record,
                              const std::string& field = "", int occurrence = 0, std::string* error = nullptr);
    /// ApplyToSource for every differing record (changed, added or lost).
    static bool ApplyAllToSource(GameObject& instanceRoot, std::string* error = nullptr);

    // ---- Authoring ----------------------------------------------------------------

    /// Save `root` and its descendants as a prefab file. Nested instances (descendants
    /// with a prefabSource whose file reads fine) are written as `nestedprefab` objects
    /// with their overrides instead of being copied. Atomic. False on error.
    static bool SavePrefab(const GameObject& root, const std::string& path, std::string* error = nullptr);
    /// Save the instance as a variant of its source prefab: `variant "<source>"`, the
    /// records that differ as overrides, lost objects as `remove`, added objects as
    /// objects. Atomic. False on error (e.g. not an instance, or `variantPath` is the
    /// source itself).
    static bool SaveVariant(const GameObject& instanceRoot, const std::string& variantPath, std::string* error = nullptr);

    // ---- Validation ---------------------------------------------------------------

    /// Check a prefab file and every file it reaches: unreadable or truncated files,
    /// missing nested prefabs and variant bases, cycles, nesting too deep, overrides,
    /// removals and attach points that name objects the prefab no longer has, and a
    /// trial load. Never throws or leaves anything behind.
    static PrefabReport ValidateAsset(const std::string& path);
    /// The stale-override part of ValidateAsset for one live instance: overrides that
    /// name an object the source prefab no longer has.
    static PrefabReport ValidateInstance(const GameObject& instanceRoot);
};

} // namespace okay
