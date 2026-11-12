#pragma once
// Validators that fill a Diagnostics list from a loaded scene. Cheap enough to
// run again when the scene changes: they only read. Scene::Validate (the file
// checks) and SceneValidator (the loaded-scene checks) are both folded in, so
// a missing file, a duplicate id and a script that does not compile land in
// the same list as a player with no collider or a graph with a bad wire.
#include "okay/Debug/Diagnostics.hpp"

#include <functional>
#include <string>
#include <vector>

namespace okay {

struct ProjectDiagOptions {
    /// Relative asset paths are looked up under this folder (empty = the working directory).
    std::string assetRoot;
    /// Replace the file test (a unit test, a packed archive).
    std::function<bool(const std::string& path)> fileExists;

    bool checkSceneFile = true;     // Scene::Validate (format, duplicate guids, broken parents)
    bool checkSemantic = true;      // SceneValidator (assets, scripts, names, physics, camera)
    bool checkLevelDesign = true;   // inside the semantic pass: degenerate geometry, duplicates, light leaks, nav reach, camera and light sanity
    bool checkRequirements = true;  // RequireComponent that is not satisfied
    bool checkGameplay = true;      // main camera, player body, animator, navmesh
    bool checkGraphs = true;        // visual-script graph errors and node requirements
    /// Scan `assetRoot` with AssetDatabase and report FindMissing. Off when the root is empty.
    /// A first scan writes a `.okaymeta` beside an asset that does not have one, which is how the index tracks it.
    bool checkAssetIndex = true;
};

struct ProjectDiagResult {
    std::vector<Diagnostic> items;
};

/// Every current problem in `scene`, in scene order. Does not log and does not
/// touch EditorDiagnostics; the caller ReplaceOwner's the result.
ProjectDiagResult CollectProjectDiagnostics(const Scene& scene, const ProjectDiagOptions& options = {});

} // namespace okay
