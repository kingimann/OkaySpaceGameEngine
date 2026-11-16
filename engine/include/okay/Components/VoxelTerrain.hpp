#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Render/Mesh.hpp"
#include "okay/Render/Color.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>
#include <functional>
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
    /// One cell of a brush: `cell - amount * fall`, clamped to +-c. Dig and Edit both call this one function (defined once, not inline) so
    /// they give the very same bits on every compiler and CPU: with fused multiply-add the same expression written twice can round
    /// differently in two places, which made Edit with default options differ from Dig on CPUs that have FMA.
    static float StepDensity(float cell, float amount, float fall, float c);

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

    // ---- Materials and real-life digging (docs/terrain_water.md, "Materials") --------------------------------
    /// What a kind of ground is made of. A terrain with materials keeps one byte per voxel naming its entry here.
    struct VoxelMaterial {
        std::string name = "Dirt";
        Color  color = Color::FromBytes(112, 86, 60);
        std::string texture;          ///< optional picture (not drawn yet; kept for scenes and scripts)
        float  hardness = 1.0f;       ///< how long it takes to dig (1 = dirt; stone 4). A tool needs `power` >= this
        float  density = 1500.0f;     ///< kilograms per cubic metre (a debris piece weighs volume x density)
        bool   loose = false;         ///< sand and dirt slump to an angle of repose after a dig
        std::string drop;             ///< inventory item a cubic metre turns into ("" = none)
        float  dropPerM3 = 10.0f;     ///< how many of that item one cubic metre gives
        std::string digSound;         ///< clip played while digging it (a script or the digger's AudioSource can use it)
        Color  particle = Color::FromBytes(130, 104, 78);   ///< colour of the dust and debris
    };
    std::vector<VoxelMaterial> materials;       ///< the table (empty until EnableMaterials)
    std::size_t matCountExpected_ = 0;          ///< (scene loader)
    bool loadedMaterialTable_ = false;          ///< (scene loader: the first `voxelmat` record replaces the default table)
    std::vector<unsigned char> material;        ///< one id per voxel, same order as `density`; empty = materials off
    bool UseMaterials() const { return !material.empty() && !materials.empty(); }
    /// dirt, grass, sand, clay, stone, ore, snow (ids 0..6)
    static std::vector<VoxelMaterial> DefaultMaterials();
    /// Turn materials on: the default table if there is none, every solid voxel `fillId`.
    void EnableMaterials(int fillId = 0);
    /// Call after editing the table by hand (colour, texture) so the surface is skinned again.
    void MaterialsChanged() { ++matRev_; mcAllDirty_ = true; }
    void DisableMaterials() { material.clear(); mcAllDirty_ = true; }
    int  MaterialIndex(const std::string& name) const;
    int  MaterialAt(int x, int y, int z) const { return (UseMaterials() && InBounds(x, y, z)) ? (int)material[Index(x, y, z)] : 0; }
    /// How the world generator layers the ground: grass on gentle top soil, dirt below it, clay lenses, stone deeper
    /// with ore pockets, sand low down, snow high up, bare stone on steep slopes.
    struct LayerRules {
        float topsoil = 0.6f, dirtDepth = 3.0f;       ///< metres of grass / dirt under the surface
        float stoneSlope = 0.65f;                     ///< steepness (0..1) above which the surface is stone
        float sandBelow = -1.0f;                      ///< local height under which the top is sand (-1 = 12% of the height)
        float snowAbove = -1.0f;                      ///< local height over which the top is snow (-1 = the top 15% of the field)
        float oreAmount = 0.06f, clayAmount = 0.08f;  ///< share of the stone / dirt that is ore / clay
        int   oreVeins = 0;                           ///< long winding veins of ore through the stone (0 = none); each is a worm about 3 voxels thick and up to 60 long
        float veinThickness = 1.2f;                   ///< vein radius in voxels
    };
    void GenerateMaterials(unsigned seed, const LayerRules& rules);
    void GenerateMaterials(unsigned seed) { GenerateMaterials(seed, LayerRules{}); }
    /// Budgeted generation for big worlds (OFF unless you call it; `Generate` still does everything at once). BeginGenerate starts
    /// the same generation `Generate` (and, with `withMaterials`, `GenerateMaterials`) would do, but StepGenerate(budgetMs) fills a few
    /// slices of the field per call and stops once the budget is spent (at least one slice per call), so a 256 x 64 x 256 world does
    /// not freeze the game for a second. The result is bit for bit the one the one-shot calls give. While `Generating()` the running
    /// game steps it every frame by `generateBudgetMs` and skins the terrain when it is done; the field is only partly filled until then.
    void  BeginGenerate(float surfaceFrac, float amplitude, float caveAmount, unsigned seed, bool withMaterials, const LayerRules& rules);
    void  BeginGenerate(float surfaceFrac, float amplitude, float caveAmount, unsigned seed, bool withMaterials = false) { BeginGenerate(surfaceFrac, amplitude, caveAmount, seed, withMaterials, LayerRules{}); }
    bool  StepGenerate(float budgetMs);          ///< true when finished (and the surface is flagged to be built)
    bool  Generating() const { return genPhase_ != 0; }
    float GenerateProgress() const;              ///< 0..1
    float generateBudgetMs = 4.0f;

    /// Water or lava that flows into dug holes (OFF by default; not saved). A fluid cell falls, or slides down a diagonal, through open
    /// air; it rests on the ground or on other fluid, so a pour fills a hole from the bottom and finds its own heap (a slope of up to 45
    /// degrees, not a flat sheet). Bounded: at most `fluidBudget` active cells are looked at per step, `fluidRate` steps per second (4 at most
    /// per frame), and `fluidMaxCells` cells in all (more pouring is refused). Resting cells cost nothing. Building ground over fluid removes
    /// that fluid; digging next to fluid wakes it. It is drawn as one transient child object, rebuilt about ten times a second while it moves.
    bool  fluidOn = false;
    int   fluidBudget = 600;
    int   fluidMaxCells = 30000;
    float fluidRate = 20.0f;
    Color fluidColor = Color::FromBytes(50, 120, 220, 170);   ///< water; lava: (255, 110, 30, 255) with fluidGlow on and fluidRate ~5
    bool  fluidGlow = false;                                  ///< draw unlit (lava)
    int   PourFluid(const Vec3& local, int cells);            ///< put up to `cells` cells of fluid around a point; returns how many fitted
    int   FluidStep();                                        ///< one flow step (Update does it); returns how many cells moved
    int   FluidCells() const;
    int   FluidActiveCells() const;                           ///< cells still waiting to be looked at (0 = everything is at rest)
    bool  FluidAt(int x, int y, int z) const;
    void  ClearFluid();
    void  UpdateFluid(float dt);
    /// Footprints and tracks (OFF by default). Objects with the tag `footprintTag` (the player), and every rigid body when
    /// `footprintBodies` is on, press a shallow dent into soft ground (materials the digger-free power `footprintPower` can dig, so dirt,
    /// grass, sand, snow and clay but not stone) every `footprintStep` metres they travel while they are on the ground. Each dent is a
    /// small bounded edit of radius `footprintRadius` and depth about `footprintDepth`. Not logged for undo.
    bool  footprints = false;
    std::string footprintTag = "Player";
    bool  footprintBodies = false;
    float footprintStep = 0.7f, footprintRadius = 0.45f, footprintDepth = 0.12f, footprintPower = 2.0f;
    int   footprintCount = 0;                                  ///< dents pressed so far
    void  UpdateFootprints();
    double FluidVolume() const { return (double)FluidCells() * voxelSize * voxelSize * voxelSize; }
    /// Solid volume in cubic metres: every voxel counts the fraction of itself that lies under the surface.
    double SolidVolume() const;

    /// Options for Edit (the brush with all its extras). The defaults make Edit act exactly like Dig / Add.
    struct EditOptions {
        enum class Shape { Sphere, Cube, Cylinder, Plane };
        Shape shape = Shape::Sphere;
        float hardnessScale = 1.0f;     ///< > 1 digs everything slower
        float power = 1.0e9f;           ///< tool power: a material with `hardness` above this cannot be dug
        int   addMaterial = -1;         ///< material given to voxels that become solid (-1 = leave them)
        float protectBelowY = -1.0f;    ///< local height under which nothing can be dug (bedrock); -1 = off
        float maxDepth = -1.0f;         ///< do not dig deeper than this under `depthOriginY`; -1 = off
        float depthOriginY = 0.0f;
        bool  snap = false;             ///< centre the brush on the nearest voxel
    };
    struct EditResult {
        bool changed = false;
        double removed = 0.0, added = 0.0;            ///< cubic metres taken out / put in
        std::vector<double> removedBy;                ///< removed volume per material id (index 0 when materials are off)
    };
    EditResult Edit(const Vec3& local, float radius, float amount, const EditOptions& opts);
    /// Put `volume` cubic metres of `materialId` back around a point (steps until it is in). Returns what went in.
    double AddVolume(const Vec3& local, double volume, int materialId, float radius = 1.2f);

    /// Loose materials (sand, dirt) near the last edits slide down to an angle of repose: at most `maxMoves` voxels move
    /// per call, only inside the edit box, no volume is lost. Returns how many moved.
    int Slump(int maxMoves);

    /// A solid piece that has no way to the ground (smaller than `minVoxels` is a crumb; every piece counts here).
    struct Piece {
        std::vector<int> cells;       ///< grid indices
        std::vector<float> dens;      ///< the density each cell had (the field itself is air there once the piece is cut out)
        double volume = 0.0;
        int material = 0;             ///< the most common material
        Vec3 lo{0, 0, 0}, hi{0, 0, 0};///< local box
    };
    /// Cut loose pieces out of the field (their cells become air) and return them. Starts from the edited cells, as
    /// RemoveFloatersEdited does; a piece touching the floor never is one.
    std::vector<Piece> ExtractFloaters(int maxPieces = 8);
    /// Optional cave-in rule. A ceiling (solid ground with air right under it) that is more than `maxSpan` voxels sideways from a
    /// wall (solid ground beside it that rests on something) fails, together with up to `maxSpan` voxels of ground above it. The failed
    /// ground is cut out as pieces (like ExtractFloaters), so a tunnel or cave wider than about twice `maxSpan` brings its roof down.
    /// Only looks around the edited cells plus `maxSpan + 2` and skips a work box over 400000 cells. At most `maxPieces` pieces are
    /// returned; ground in further pieces stays.
    std::vector<Piece> CaveIn(int maxSpan, int maxPieces = 6);
    /// The surface of one piece as a mesh in the terrain's local space.
    Mesh PieceMesh(const Piece& p) const;

    /// One stroke can be undone as a whole. BeginStroke starts recording (and forgets the previous stroke).
    void BeginStroke();
    void EndStroke() { strokeOpen_ = false; }
    bool UndoStroke();
    bool CanUndo() const { return !strokeLog_.empty(); }

    std::string EncodeMaterials() const;
    bool DecodeMaterials(const std::string& b64);

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
    /// World-edge catch (OFF by default). A rigid body that has fallen more than `catchMargin` below the bottom of the grid
    /// (it left the side of the world, where nothing holds it) is put back on the ground at the nearest point inside the
    /// grid with its speed cleared. `onFall` is a respawn hook: when set it is called instead and does the moving itself.
    bool  catchFalling = false;
    float catchMargin  = 4.0f;
    std::function<void(GameObject&)> onFall;
    int   caughtCount  = 0;     ///< bodies caught so far
    /// Do the catch once now (Update does it every few frames when `catchFalling` is on).
    int   CatchFallers();
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
    /// How many edits changed the field since it was created or loaded (a World Streamer keeps a cell that was dug).
    std::uint32_t editCount = 0;
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
    bool strokeOpen_ = false;
    struct StrokeCell { int idx; float density; unsigned char material; };
    std::vector<StrokeCell> strokeLog_;
    std::vector<unsigned char> strokeSeen_;
    Piece BuildPiece(const std::vector<int>& comp) const;
    std::vector<std::pair<std::uint64_t, Vec3>> footLast_;   // object id and where it last pressed a dent
    unsigned footTick_ = 0;
    std::vector<unsigned char> fluid_;
    std::vector<int> fluidActive_;
    int fluidCount_ = 0;
    unsigned fluidRev_ = 0, fluidMeshRev_ = 0, fluidTick_ = 0;
    float fluidAcc_ = 0.0f, fluidMeshTimer_ = 0.0f;
    GameObject* fluidObj_ = nullptr;
    void WakeFluidBox(int x0, int y0, int z0, int x1, int y1, int z1);
    void RebuildFluidMesh();
    void GenerateSlices(int zFrom, int zTo, float surfaceFrac, float amplitude, float caveAmount, unsigned seed);
    void BeginMaterialPass(const LayerRules& r);
    void GenerateMaterialSlices(int zFrom, int zTo, unsigned seed, const LayerRules& r);
    void FinishMaterials(unsigned seed, const LayerRules& r);
    int genPhase_ = 0;                  // 0 idle, 1 density slices, 2 material slices
    int genZ_ = 0;
    bool genMaterials_ = false;
    float genSurface_ = 0.5f, genAmp_ = 6.0f, genCaves_ = 0.0f;
    unsigned genSeed_ = 0;
    LayerRules genRules_;
    unsigned catchTick_ = 0;
    unsigned matRev_ = 0;                            ///< changes when the material table does
    void LogStroke(int idx);

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
