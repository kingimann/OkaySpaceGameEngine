#pragma once
// ---------------------------------------------------------------------------
// SceneValidator: static checks on a loaded Scene, for CI and for the editor's
// "check before you ship" step. It reports problems; it never changes the scene.
//
//   ValidatorOptions opt;
//   opt.assetRoot = "assets";
//   ValidationReport r = SceneValidator::ValidateFile("scenes/level1.okayscene", opt);
//   std::cout << r.ToText();           // or r.ToJson()
//   if (r.HasErrors()) return 1;
//
// What it looks at (each check has a stable `code` so tools can filter on it):
//   asset.missing          a texture, audio clip, mesh, prefab, data file ... that does not exist under the asset root
//   asset.prefab_missing   an instance's source prefab file is gone (the instance still works)
//   scene.no_camera        the scene has no Camera at all
//   scene.no_active_camera there are cameras but none can render (disabled or inactive)
//   name.duplicate         siblings that share a name, so Find("X") is ambiguous
//   component.duplicate_singleton   a component that should exist once appears more than once
//   script.missing_file    a Script / Visual Script points at a file that does not exist
//   script.empty           a Script with neither source nor file
//   script.compile         the script text does not compile
//   script.backend         the script's language is not available in this build
//   ref.missing_object     a component names an object (a follow target ...) the scene does not have
//   transform.non_finite   NaN or infinity in a position, rotation or scale
//   transform.zero_scale   a scale component is exactly 0
//   transform.rotation_unnormalized  a rotation quaternion that is not unit length
//   transform.extreme_position       a coordinate beyond ValidatorOptions::maxCoordinate
//   object.inactive        an inactive object: it and its children never run
//   object.layer_range     a layer outside 0..31
//   physics.rigidbody_no_collider    a Rigidbody with no Collider on the object
//   scene.load / scene.load_warning  (ValidateText / ValidateFile) the document did not load cleanly
// ---------------------------------------------------------------------------
#include <functional>
#include <string>
#include <vector>

namespace okay {
class Scene;
class GameObject;
}

namespace okay::testing {

enum class Severity { Info = 0, Warning = 1, Error = 2 };
/// "info", "warning" or "error".
const char* SeverityName(Severity s);

struct ValidationIssue {
    Severity severity = Severity::Info;
    std::string code;        // stable id, e.g. "asset.missing"
    std::string object;      // path of the object ("Root/Child"); empty for a scene-wide issue
    std::string component;   // the component type involved, when there is one
    std::string message;
};

struct ValidationReport {
    std::string sceneName;
    std::vector<ValidationIssue> issues;   // in the order found: objects in scene order, then scene-wide checks

    int Count(Severity s) const;
    int Errors() const { return Count(Severity::Error); }
    int Warnings() const { return Count(Severity::Warning); }
    int Infos() const { return Count(Severity::Info); }
    bool HasErrors() const { return Errors() > 0; }
    bool Clean() const { return issues.empty(); }
    /// Issues with this code.
    int CountCode(const std::string& code) const;
    bool Has(const std::string& code) const { return CountCode(code) > 0; }

    /// Human text: a summary line, then one line per issue, errors first.
    ///   [ERROR] Player/Gun: Missing texture 'gun.png' (SpriteRenderer.texture) [asset.missing]
    /// Only issues at `atLeast` or worse are listed (the summary still counts all).
    std::string ToText(Severity atLeast = Severity::Info) const;
    /// {"scene":"...","errors":N,"warnings":N,"infos":N,"issues":[{"severity":"error",
    ///  "code":"...","object":"...","component":"...","message":"..."},...]}
    std::string ToJson() const;
};

/// One file an object's components point at.
struct AssetRef {
    std::string component;   // "SpriteRenderer"
    std::string field;       // "texture"
    std::string kind;        // "texture", "audio", "mesh", "prefab", "data", "font"
    std::string path;
};

struct ValidatorOptions {
    /// Relative asset paths are looked for under this folder (empty = the working
    /// directory) and then under each of `searchDirs`. Absolute paths are used as is.
    std::string assetRoot;
    std::vector<std::string> searchDirs;
    /// Replace the file test (a packed archive, a virtual file system, a unit test).
    std::function<bool(const std::string& path)> fileExists;

    bool checkAssets = true;
    bool checkCamera = true;
    bool checkNames = true;
    bool checkScripts = true;
    bool checkReferences = true;
    bool checkTransforms = true;
    bool checkInactive = true;
    bool checkPhysics = true;

    Severity missingAssetSeverity = Severity::Error;
    Severity noCameraSeverity = Severity::Warning;
    Severity duplicateNameSeverity = Severity::Warning;

    /// Names that may repeat among siblings (a "Wall", a "Tree").
    std::vector<std::string> allowDuplicateNames;
    /// Positions beyond this on any axis are flagged (float precision falls apart far from the origin).
    float maxCoordinate = 1.0e6f;
};

class SceneValidator {
public:
    /// Check a scene that is already loaded.
    static ValidationReport Validate(const Scene& scene, const ValidatorOptions& options = {});
    /// Load serialized scene text into a scratch scene and check it. A document that
    /// does not load gives a single scene.load error.
    static ValidationReport ValidateText(const std::string& sceneText, const ValidatorOptions& options = {});
    /// Load a scene file into a scratch scene and check it. The scene's own folder is
    /// searched for assets too.
    static ValidationReport ValidateFile(const std::string& path, const ValidatorOptions& options = {});

    /// Every asset file the object's own components name, in a stable order. Script
    /// files are not listed here; the script checks handle them.
    static std::vector<AssetRef> AssetReferences(const GameObject& go);
    /// Does `path` exist under the options' asset root / search dirs (or is it built in, like "proc:grid")?
    static bool AssetExists(const std::string& path, const ValidatorOptions& options);
};

} // namespace okay::testing
