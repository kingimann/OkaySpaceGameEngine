#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Render/Mesh.hpp"
#include "okay/Render/Color.hpp"
#include <vector>
#include <string>

namespace okay {

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
    int   nx = 48, ny = 28, nz = 48;   ///< sample grid dimensions
    float voxelSize = 1.0f;            ///< world units between samples
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
    void  Set(int x, int y, int z, float v) { if (InBounds(x, y, z)) density[Index(x, y, z)] = v; }

    // ---- Generation ----------------------------------------------------
    /// Fill a landscape: a noisy ground surface at `surfaceFrac` of the height,
    /// with relief `amplitude`, optionally riddled with `caveAmount` (0..1) worth
    /// of winding caverns below the surface. Replaces the field.
    void Generate(float surfaceFrac, float amplitude, float caveAmount, unsigned seed);
    /// Fill the bottom `frac` of the volume solid (a flat slab to sculpt from).
    void FillSlab(float frac);

    // ---- Runtime editing (local-space point, relative to the object) ---
    /// Carve material away inside a sphere (open a hole/cave). `amount` scales how
    /// much density is removed per call.
    void Dig(const Vec3& local, float radius, float amount);
    /// Deposit material inside a sphere (build up terrain).
    void Add(const Vec3& local, float radius, float amount);
    /// Relax (average) the density field inside a sphere — smooths the jagged faces
    /// a freshly dug hole can leave, and rounds off lumps from filling. `amount` in
    /// [0,1] per call.
    void SmoothAt(const Vec3& local, float radius, float amount);

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
    /// Rebuild the sibling MeshRenderer's mesh from the field. Call after edits.
    void Apply();
    void Start() override { Apply(); }

    // ---- Compact (de)serialization of the field ------------------------
    /// Quantized + base64 encoding of the density field (so editor edits persist
    /// without bloating the scene file with raw floats).
    std::string EncodeDensity() const;
    bool DecodeDensity(const std::string& b64);

    // local <-> grid helpers
    Vec3  GridToLocal(int x, int y, int z) const {
        return Vec3{x * voxelSize - HalfX(), y * voxelSize, z * voxelSize - HalfZ()};
    }
};

} // namespace okay
