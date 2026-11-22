#pragma once
// World Partition (feature map 2.6.6, 27.4): split a big scene into a grid of streaming cells. See
// docs/engine/world_streaming.md.
//
// Each top-level object (with everything under it) goes to one cell, the cell of the centre of its bounds
// (what it draws) on the XZ plane, or to the "always loaded" part of the world:
//   - a PartitionLayer on the object says so (Always Loaded / Streamed), else
//   - cameras, lights that light the whole world (directional), UI canvases, streamers, level volumes,
//     streaming sources, voxel worlds and objects tagged Player stay loaded;
//   - an object that draws nothing and has no collider (a game manager, a script holder) stays loaded;
//   - an object whose bounds cover more than `maxSpanCells` cells across (a terrain, a long road) stays
//     loaded, or goes to the cell of its centre when `spanRule` is BoundsCentre.
//
// WritePartition is non-destructive: the source scene is not changed. It writes one prefab per non-empty cell
// (`<prefix>_<x>_<z>.okayprefab`, objects at their world positions under one root named like the file) into
// the cell folder, removes cell files of that prefix the new plan no longer has, and writes a new scene with
// the always-loaded objects plus a "World Streamer" object set up to stream the cells. Running it again on
// the source scene updates the cells.
#include "okay/Math/Vec3.hpp"
#include "okay/Scene/Component.hpp"
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace okay {

class GameObject;
class Scene;

/// Per-object World Partition setting (put it on a top-level object).
class PartitionLayer : public Component {
public:
    enum class Mode { Auto, AlwaysLoaded, Streamed };
    Mode mode = Mode::Auto;
};

struct PartitionSettings {
    float cellSize = 50.0f;
    std::string prefix = "cell";
    enum class SpanRule { AlwaysLoaded, BoundsCentre };
    SpanRule spanRule = SpanRule::AlwaysLoaded;
    float maxSpanCells = 1.5f;       ///< an object wider than this many cells is "spanning"
    int   loadRadius = 1;            ///< the World Streamer the output scene gets
    int   unloadRadius = 2;
};

struct PartitionPlan {
    struct Entry {
        GameObject* object = nullptr;
        int cellX = 0, cellZ = 0;
        bool alwaysLoaded = false;
        std::string reason;           ///< why ("camera", "spans 4 cells", "Partition Layer", "cell 2,3")
        Vec3 centre;
    };
    std::vector<Entry> entries;       ///< one per top-level object
    std::map<std::pair<int, int>, int> cellCounts;   ///< objects per cell
    int streamed = 0, alwaysLoaded = 0;
    float minX = 0, minZ = 0, maxX = 0, maxZ = 0;    ///< the cells' extent in world units (for an overlay)
};

/// Decide where every top-level object of `scene` goes. Reads the scene only.
PartitionPlan PlanPartition(const Scene& scene, const PartitionSettings& settings);

struct PartitionResult {
    bool ok = false;
    std::string error;
    int cellsWritten = 0, cellsRemoved = 0, objectsStreamed = 0, objectsAlwaysLoaded = 0;
    std::string scenePath, cellFolder;
};

/// Write the cells of `scene` into `cellFolder` and the streaming scene to `outScenePath`. The streamer's
/// folder is written relative to the output scene when the cell folder sits under the scene's folder.
/// Refused (ok false) when the scene already contains a World Streamer that streams this folder and prefix
/// (that is a partitioned scene: run the tool on its source instead), or when a file cannot be written.
PartitionResult WritePartition(const Scene& scene, const PartitionSettings& settings,
                               const std::string& outScenePath, const std::string& cellFolder);

} // namespace okay
