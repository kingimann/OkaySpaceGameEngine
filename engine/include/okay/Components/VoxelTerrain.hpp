#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Render/Mesh.hpp"
#include "okay/Render/Color.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>
#include <string>

namespace okay {

class GameObject;
class MeshRenderer;

/// A smooth VOXEL terrain — a 3D scalar density field meshed with marching cubes,
/// so unlike the heightmap Terrain it supports caves, tunnels and overhangs. This
/// is the "dig a real hole, not Minecraft cubes" terrain: carve into it at runtime
/// and the surface re-skins smoothly. Lives next to a MeshRenderer and rebuilds it
/// whenever the field changes, so the normal 3D pipeline draws it for free.
///
/// Density convention: > iso is solid rock, < iso is air; the surface is the iso
/// crossing. Digging subtracts density (opens air); adding deposits it.
class VoxelTerrain : public Component {
public:
    int   nx = 64, ny = 36, nz = 64;   ///< sample grid dimensions (denser = smoother, less low-poly)
    float voxelSize = 0.8f;            ///< world units between samples
    float iso = 0.0f;                  ///< surface threshold
    Color color = Color::FromBytes(120, 110, 96);
    bool  autoColor = true;            ///< tint faces by height & slope (grass/rock/snow)

    Color grassColor = Color::FromBytes(96, 150, 80);
    Color rockColor  = Color::FromBytes(122, 112, 102);
    Color soilColor  = Color::FromBytes(112, 86, 60);
    Color snowColor  = Color::FromBytes(236, 240, 248);
    float snowLevel  = 16.0f;          ///< world-Y above which faces read as snow
    float rockSlope  = 0.6f;           ///< steepness (0 flat .. 1 vertical) -> rock

    /// Optional triplanar ground texture (tinted by the auto-colours when on). Build
    /// Game bundles the file. Triplanar projects along the world axes so cave walls,
    /// overhangs and floors all texture cleanly with no seams or stretching.
    std::string texture;
    float       textureTiling = 0.08f; ///< world-space texture scale (smaller = bigger texels)

    std::vector<float> density;        ///< row-major nx*ny*nz field

    VoxelTerrain() { Resize(nx, ny, nz); }

    /// Bounded band the density field is kept within (a clamped signed distance),
    /// so digging/adding with finite strength reliably opens/closes voxels.
    float Clamp() const;

    int  Count() const { return nx * ny * nz; }
    int  Index(int x, int y, int z) const { return x + y * nx + z * nx * ny; }
    bool InBounds(int x, int y, int z) const { return x >= 0 && y >= 0 && z >= 0 && x < nx && y < ny && z < nz; }

    /// World extents (the field is centred on the object in X/Z; Y rises from the
    /// object's origin).
    float HalfX() const { return (nx - 1) * voxelSize * 0.5f; }
    float HalfZ() const { return (nz - 1) * voxelSize * 0.5f; }
    float SizeY() const { return (ny - 1) * voxelSize; }

    /// Resize the field (clears it to all-air).
    void  Resize(int X, int Y, int Z);
    float Get(int x, int y, int z) const { return InBounds(x, y, z) ? density[Index(x, y, z)] : -1.0f; }
    void  Set(int x, int y, int z, float v) {
        if (!InBounds(x, y, z)) return;
        density[Index(x, y, z)] = v;
        TouchCell(x, y, z);
    }

    // ---- Generation ----------------------------------------------------
    /// Fill a landscape: a noisy ground surface at `surfaceFrac` of the height,
    /// with relief `amplitude`, optionally riddled with `caveAmount` (0..1) worth
    /// of winding caverns below the surface. Replaces the field.
    void Generate(float surfaceFrac, float amplitude, float caveAmount, unsigned seed);
    /// Fill the bottom `frac` of the volume solid (a flat slab to sculpt from).
    void FillSlab(float frac);

    // ---- Runtime editing (local-space point, relative to the object) ---
    /// Carve material away inside a sphere (open a hole/cave). `amount` scales how
    /// much density is removed per call. Returns true if any voxel actually changed
    /// (false when the brush only touched already-empty air), so callers can skip the
    /// expensive surface rebuild when nothing moved.
    bool Dig(const Vec3& local, float radius, float amount);
    /// Deposit material inside a sphere (build up terrain). Returns true if changed.
    bool Add(const Vec3& local, float radius, float amount);
    /// Relax (average) the density field inside a sphere — smooths the jagged faces
    /// a freshly dug hole can leave, and rounds off lumps from filling. `amount` in
    /// [0,1] per call. Returns true if any voxel actually changed.
    bool SmoothAt(const Vec3& local, float radius, float amount);

    /// Carve (or fill, with a negative amount) an axis-aligned BOX of voxels between
    /// two local-space corners — sharp rectangular digs (rooms, corridors, trenches),
    /// unlike the round sphere brush. Returns true if any voxel actually changed.
    bool DigBox(const Vec3& localMin, const Vec3& localMax, float amount);

    /// Bottom grid rows digging cannot open. The field ends at local y = 0, so
    /// carving that layer is a hole out of the world and the player falls through.
    /// 1 keeps a solid floor. 0 is the old behaviour (a dig can open the bottom).
    /// Not stored in the scene file — loaded volumes use this default.
    int floorLayers = 1;

    /// Delete disconnected solid "crumbs": label connected components of solid voxels
    /// (6-connectivity, solid = density > iso) and clear every component with fewer
    /// than `minVoxels` voxels to air. Fixes the little floating chunks a dig leaves
    /// behind that the player can't reach. Returns the number of voxels cleared.
    int RemoveFloaters(int minVoxels = 8);
    /// The same cleanup, but only around the cells edited since the last cleanup: a dig can only cut
    /// a small piece loose next to the cells it removed, so the walk starts from the solid voxels near
    /// the edit box and stops as soon as a piece has `minVoxels` voxels (it is attached). The very first
    /// call (and the first one after Generate / Resize / a load) sweeps the whole field, so crumbs that
    /// were already there still go. Returns the number of voxels cleared. What the Voxel Digger calls.
    int RemoveFloatersEdited(int minVoxels = 8);

    /// Trilinear density at a local-space point (for queries / collision).
    float SampleDensity(const Vec3& local) const;
    bool  SolidAt(const Vec3& local) const { return SampleDensity(local) > iso; }
    /// Outward surface normal at a local point (unit; points from solid toward air),
    /// from the density gradient. Used by physics to push bodies out of voxels.
    Vec3  SurfaceNormal(const Vec3& local) const;
    /// True if a local point is inside the field's volume (with a small margin).
    bool  WithinBounds(const Vec3& local, float margin = 0.0f) const {
        return local.x >= -HalfX() - margin && local.x <= HalfX() + margin &&
               local.z >= -HalfZ() - margin && local.z <= HalfZ() + margin &&
               local.y >= -margin && local.y <= SizeY() + margin;
    }
    /// Highest solid surface Y (local) over a column at local XZ — for walking on
    /// the top. Returns false if the column is entirely air. (Top surface only, so
    /// it doesn't see cave floors beneath an overhang.)
    bool  SurfaceY(float lx, float lz, float& outY) const;

    // ---- Rendering -----------------------------------------------------
    /// Marching-cubes mesh of the iso surface (smooth, with gradient normals).
    Mesh BuildMesh() const;
    /// The same surface from the ORIGINAL marching-cubes loop, kept word for word as the reference: the
    /// tests compare BuildMesh and every incremental update against it bit for bit, and
    /// OKAY_VOXEL_OLD_REMESH=1 meshes with it. Slow (about twice the work of BuildMesh).
    Mesh BuildMeshReference() const;
    /// Rebuild the surface from the field. Call after edits. Only the 16-cube chunks the edits reached are
    /// meshed again (a vertex normal reads two cells past its cube, so the chunks beside an edit count too).
    ///
    /// How the surface reaches the renderer (see RenderMode): one combined mesh on the sibling MeshRenderer
    /// while a scene is being built or edited, and one small renderer object per chunk once the running game
    /// edits the terrain, so a dig re-uploads only the chunks it touched instead of the whole world.
    void Apply();
    void Start() override { Apply(); }
    void Update(float dt) override;
    void OnDestroy() override;
    void ReleaseUnstarted() override;

    /// How Apply hands the surface to the renderer.
    ///   Auto    - one combined mesh on this object's MeshRenderer (what scenes, tests and the editor's
    ///             Edit mode always had) until the RUNNING game edits the terrain; from then on one
    ///             renderer object per chunk. The picture is the same either way. A terrain object that also
    ///             has a collider, an LOD Group or an Occluder keeps the combined mesh (they read it).
    ///   Single  - always the combined mesh (every edit rebuilds and re-uploads all of it).
    ///   Chunked - always one object per chunk: transient children named "Voxel Chunk x,y,z" (never saved),
    ///             with this object's MeshRenderer left empty. They copy its material settings.
    /// The environment variable OKAY_VOXEL_OLD_REMESH=1 forces Single and the original (slower) loop, to
    /// compare against the previous behaviour.
    enum class RenderMode { Auto, Single, Chunked };
    RenderMode renderMode = RenderMode::Auto;

    /// What the last Apply did (also what the Profiler rows Voxel.Mesh / Voxel.Upload measure).
    struct MeshStats {
        int         chunksTotal = 0;       ///< chunks in the grid
        int         chunksRemeshed = 0;    ///< chunks whose triangles were rebuilt
        int         chunksHandedOff = 0;   ///< chunk meshes given to a renderer object (Chunked)
        int         vertices = 0;          ///< vertices of the rebuilt chunks
        int         triangles = 0;         ///< triangles of the rebuilt chunks
        std::size_t bytesHandedOff = 0;    ///< bytes of mesh data the renderer has to take in again
        double      remeshMs = 0.0;        ///< time spent in the marching-cubes loop
        double      handoffMs = 0.0;       ///< time spent giving the result to the renderer
        double      totalMs = 0.0;         ///< the whole Apply
        bool        chunked = false;       ///< the surface is drawn by chunk objects
    };
    MeshStats lastStats;
    /// How many renderer objects currently draw the surface (0 while it is one combined mesh).
    int  ChunkObjectCount() const;
    bool UsesChunkObjects() const { return chunked_; }
    /// Test hook: -1 follows the environment variable, 0 forces the new path, 1 the old one.
    static void ForceOldRemesh(int mode);
    static bool OldRemeshActive();

    // ---- Compact (de)serialization of the field ------------------------
    /// Quantized + base64 encoding of the density field (so editor edits persist
    /// without bloating the scene file with raw floats).
    std::string EncodeDensity() const;
    bool DecodeDensity(const std::string& b64);

    // local <-> grid helpers
    Vec3  GridToLocal(int x, int y, int z) const {
        return Vec3{x * voxelSize - HalfX(), y * voxelSize, z * voxelSize - HalfZ()};
    }

private:
    struct McChunk {
        Mesh mesh;                  ///< Single: this chunk's triangles, kept so a combine only copies
        std::uint64_t obj = 0;      ///< Chunked: InstanceId of the renderer object (0 = none yet)
        int lastVerts = 0;          ///< vertex count of the last mesh (a size hint for the next one)
    };
    static constexpr int kChunk = 16;
    int mcNx_ = -1, mcNy_ = -1, mcNz_ = -1;
    float mcVox_ = -1.0f;
    unsigned mcStamp_ = 0;
    int ncx_ = 0, ncy_ = 0, ncz_ = 0;                // chunks along each axis
    std::vector<McChunk> mcChunks_;
    std::vector<unsigned char> mcDirty_;             // per chunk: must be meshed again
    int  mcDirtyCount_ = 0;
    bool mcAllDirty_ = true;
    bool chunked_ = false;                           // chunk objects draw the surface
    bool builtOnce_ = false;                         // a full build has run (the Auto switch waits for it)
    int  sealedLayers_ = -1;                         // floorLayers at the last SealFloor
    bool floorTouched_ = false;                      // an edit reached the floor rows
    const void* combinedPtr_ = nullptr;              // Single: where the combined vertices live, to notice a replaced mesh
    std::size_t combinedVerts_ = 0;
    std::uint64_t matSig_ = 0;                       // the terrain MeshRenderer's material when chunks last copied it
    // Cells edited since the last floater cleanup (inclusive grid box).
    bool editAny_ = false;
    int  editMin_[3] = {0, 0, 0}, editMax_[3] = {-1, -1, -1};
    bool floatersSwept_ = false;

    /// A density write at this cell changes every cube whose corner OR normal sampling reads it: a vertex
    /// normal is a central difference half a cell either side, which reaches two cells below and one above.
    void TouchCell(int x, int y, int z) { TouchBox(x, y, z, x, y, z); }
    void TouchBox(int x0, int y0, int z0, int x1, int y1, int z1);
    void MarkCubes(int a0, int b0, int c0, int a1, int b1, int c1);
    /// Fill air in the protected bottom rows so the volume has a floor.
    bool SealFloor();
    unsigned MeshStamp() const;
    void AppendCubes(Mesh& m, int x0, int y0, int z0, int x1, int y1, int z1) const;
    void AppendCubesReference(Mesh& m, int x0, int y0, int z0, int x1, int y1, int z1) const;

    // chunk table / renderer hand-off
    bool EnsureChunkTable();
    void CubeRange(int cx, int cy, int cz, int& x1, int& y1, int& z1) const;
    void CombineInto(MeshRenderer& mr);
    void EnterChunked(MeshRenderer& parent);
    void LeaveChunked();
    GameObject* CreateChunkObject(int cx, int cy, int cz);
    void HandOffChunk(int ci, int cx, int cy, int cz, Mesh&& fresh, const MeshRenderer& parent);
    void SyncChunkMaterials(const MeshRenderer& parent);
    void DestroyChunkObjects();
    bool RunningGame() const;
    bool MeshReadByOthers() const;
};

} // namespace okay
