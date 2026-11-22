#pragma once
// An endless voxel world (feature map 2.6, 27.4): VoxelTerrain chunks generated around the player on the
// job system and streamed in and out by Optimize/ChunkStreamer. See docs/engine/world_streaming.md.
//
// Every chunk is an ordinary VoxelTerrain (diggable, with caves, colours and the Voxel Digger) on a child
// object named "Voxel Chunk x,z" that is never saved. The ground is one function of the world position, so
// neighbouring chunks share their border samples and the surface joins without cracks. Generation (the
// density field) runs on a worker; meshing and the upload run on the main thread, `maxChunksPerFrame`
// chunks and `meshBudgetMs` a frame at most. A chunk that was dug keeps its shape when it streams out and
// back in (`rememberEdits`), for as long as the game runs.
#include "okay/Scene/Component.hpp"
#include "okay/Math/Vec3.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace okay {

class VoxelTerrain;

class VoxelWorld : public Behaviour {
public:
    int   chunkVoxels = 16;        ///< voxels along one chunk edge (X and Z)
    int   heightVoxels = 32;       ///< voxels from the bottom of the world to the top
    float voxelSize = 1.0f;        ///< world units per voxel
    float viewDistance = 64.0f;    ///< chunks this close to a source are generated
    float unloadDistance = 96.0f;  ///< chunks farther than this from every source go (>= viewDistance)
    int   seed = 1;
    float groundHeight = 12.0f;    ///< average ground height above this object
    float hills = 8.0f;            ///< how far the ground rises and falls around that
    float featureSize = 48.0f;     ///< world units across a typical hill
    float caves = 0.0f;            ///< 0 = solid ground, 1 = many caves
    int   maxChunksPerFrame = 2;   ///< chunks meshed and uploaded per frame (at least 1)
    float meshBudgetMs = 4.0f;     ///< main-thread milliseconds per frame for meshing and uploading
    bool  rememberEdits = true;    ///< a dug chunk keeps its shape when it streams out and back in
    std::string target;            ///< object to generate around; empty = the main camera
    bool  followSources = true;    ///< StreamingSource components count too

    VoxelWorld();
    ~VoxelWorld() override;

    void Start() override;
    void Update(float dt) override;
    void OnDestroy() override;

    /// The ground's density at a point in this object's space (> 0 solid). The same function every chunk is
    /// filled from; safe to call from any thread.
    float DensityAt(const Vec3& local) const;
    /// Height of the ground surface (before caves) at local x, z.
    float GroundAt(float x, float z) const;

    int  LoadedChunkCount() const;
    bool IsChunkLoaded(int cx, int cz) const;
    /// The chunk terrain covering a world point (nullptr when that chunk is not loaded).
    VoxelTerrain* ChunkAt(const Vec3& world) const;
    /// Chunks kept because they were dug.
    int  RememberedChunkCount() const;
    /// Generate and mesh every chunk the sources want now (blocking): the first frame, a teleport.
    void SyncNow();

    struct Stats {
        int loaded = 0, generating = 0, waiting = 0, remembered = 0;
        std::uint64_t generated = 0, attached = 0, detached = 0;
        double generateMs = 0.0;      ///< worker time, summed
        double lastMeshMs = 0.0;      ///< main-thread time the last frame spent meshing chunks
        double maxMeshMs = 0.0;       ///< the most any frame spent
    };
    Stats GetStats() const;
    std::string DebugText() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m;
    void Configure();
};

} // namespace okay
