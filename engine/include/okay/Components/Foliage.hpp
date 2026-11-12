#pragma once
// Foliage painting (feature map 29.2): trees, bushes, rocks, grass tufts painted onto a terrain or any
// surface with a brush. One Foliage component holds one kind of plant (a built-in prop, a mesh file or a
// prefab) and as many placed copies of it as the budget allows, as compact records (position, turn, scale),
// not as objects. The copies are drawn merged into one mesh per square of ground (a "chunk"), so a forest
// is a handful of draw calls and the renderers cull whole chunks. The chunk objects are children made
// again on load and never saved; the instance list is what the scene file keeps (record `foliage`).
#include "okay/Scene/Component.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Render/Mesh.hpp"
#include "okay/Math/Mat4.hpp"
#include "okay/Math/Vec3.hpp"
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace okay {

class Scene;
class Terrain;
class Random;

/// One placed plant, in the Foliage object's local space.
struct FoliageInstance {
    Vec3  position;
    float yaw = 0.0f;       ///< turn about up, degrees
    float scale = 1.0f;     ///< uniform
    float leanX = 0.0f;     ///< the copy's up direction (x and z; y follows): 0,0 = straight up, else leaning with a slope
    float leanZ = 0.0f;
    int   variant = 0;      ///< which of the layer's shapes this copy uses
};

class Foliage : public Component {
public:
    // ---- what is planted ----
    std::string mesh = "Tree";      ///< a built-in prop (Tree, Pine, PalmTree, DeadTree, Bush, Rock, Mushroom, Cactus, Crystal)
    std::string meshPath;           ///< or a .obj / .okaymesh file (wins over `mesh`)
    std::string prefabPath;         ///< or a prefab: its meshes are flattened into one shape (wins over both)
    Color       tint{1, 1, 1, 1};
    bool        castShadows = false; ///< copies throw shadows (costs a lot with thousands of copies; off by default)
    int         variants = 4;       ///< built-in props: how many distinct shapes are cycled through (1..8)

    // ---- the brush (saved with the layer so a scene keeps its settings) ----
    float radius = 6.0f;            ///< brush radius, world units
    float density = 0.15f;          ///< copies per square metre at full coverage
    float minScale = 0.8f, maxScale = 1.3f;
    float maxSlope = 35.0f;         ///< skip ground steeper than this many degrees
    float minHeight = -10000.0f, maxHeight = 10000.0f;   ///< world-Y band that may be planted
    bool  randomYaw = true;
    float alignToSurface = 0.0f;    ///< 0 = stand straight up .. 1 = lean fully with the ground
    float spacing = 0.6f;           ///< never plant closer than spacing / sqrt(density) to a neighbour
    std::uint32_t seed = 1;

    // ---- budget ----
    int   maxTriangles = 300000;    ///< merged triangles in the whole layer (stops painting beyond it)
    float chunkSize = 16.0f;        ///< side of a chunk square, world units

    std::vector<FoliageInstance> instances;

    /// The ground at a world XZ: the surface point and its unit normal, or false for "no ground here".
    using Surface = std::function<bool(float wx, float wz, Vec3& position, Vec3& normal)>;
    /// The surface of a Terrain (its height field, with the object's transform).
    static Surface TerrainSurface(const Terrain& t);
    /// Any mesh in the scene: a ray from `castFromY` straight down. Objects of this layer and its chunks are ignored.
    Surface SceneSurface(Scene& scene, float castFromY) const;

    // ---- brush operations (world-space centre) ----
    /// One brush stamp: plant up to density * area copies inside the disc. Returns how many were added.
    int Paint(float wx, float wz, const Surface& ground);
    /// Remove copies within `radius` (probability `strength` each, 1 = all). Returns how many.
    int Erase(float wx, float wz, float eraseRadius, float strength = 1.0f);
    /// Give every copy within the radius a new random scale from [minScale, maxScale]. Returns how many.
    int Rescale(float wx, float wz, float rescaleRadius);
    /// Scatter `count` copies over a Terrain at random (the Terrain inspector's Scatter button). Returns how many.
    int ScatterOnTerrain(const Terrain& t, int count, std::uint32_t seed);
    void Clear();

    /// Which of the layer's shapes there are (built-in props with variants: `variants`; else 1).
    int  ShapeCount() const;
    int  TrianglesPerInstance() const;
    /// Most copies the triangle budget allows.
    int  Capacity() const;
    int  Count() const { return (int)instances.size(); }
    int  ChunkCount() const { return (int)chunkObjects_.size(); }
    int  TotalTriangles() const { return Count() * TrianglesPerInstance(); }

    /// Make the chunk objects for whatever changed since the last call (everything the first time).
    /// The editor calls it every frame; it does nothing when nothing changed.
    void EnsureBuilt();
    /// Drop and rebuild every chunk (after the shape, tint or chunk size changed).
    void RebuildAll();
    void MarkAllDirty() { allDirty_ = true; sourceOk_ = false; indexDirty_ = true; }
    /// Why the shape could not be built ("" when it can).
    const std::string& Problem() const { return problem_; }

    void Start() override { EnsureBuilt(); }
    void OnDestroy() override;

    /// Chunk of a local XZ point (for tests).
    static long long ChunkKey(int cx, int cz) { return ((long long)cx << 32) ^ (unsigned)cz; }

private:
    struct Chunk { std::uint64_t objectId = 0; };
    std::map<long long, std::vector<int>> index_;        ///< chunk -> instance numbers
    std::map<long long, Chunk> chunkObjects_;
    std::vector<long long> dirty_;
    bool indexDirty_ = true, allDirty_ = true, sourceOk_ = false;
    std::vector<Mesh> shapes_;
    std::string shapeTexture_;
    std::string problem_;
    int  tris_ = 0;

    void BuildSource();
    void ReIndex();
    void Touch(long long key);
    long long KeyOf(const Vec3& local) const;
    void BuildChunk(long long key);
    void DestroyChunks();
    Mat4 WorldToLocal() const;
};

} // namespace okay
