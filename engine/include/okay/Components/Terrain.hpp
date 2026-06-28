#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Render/Mesh.hpp"
#include "okay/Render/Color.hpp"
#include <vector>

namespace okay {

/// A heightmap terrain like Unity's Terrain: a square grid of height samples you
/// sculpt with brushes (raise/lower/smooth/flatten) or generate (random / hills),
/// rendered as a mesh built from the heights. It lives next to a MeshRenderer and
/// (re)builds that renderer's mesh whenever it changes — so the existing 3D
/// pipeline (editor view + player) draws it for free.
class Terrain : public Component {
public:
    int   resolution = 32;     // cells per side; (resolution+1)^2 vertices
    float size = 50.0f;        // world width & depth (centered on the object)
    Color color = Color::FromBytes(96, 140, 80);
    std::vector<float> heights; // row-major, Dim()*Dim()

    // ---- Auto-coloring "layers" (a simplified Unity splat): each face is tinted
    //      by its elevation and steepness — water/sand at the bottom, grass in
    //      the middle, rock on steep slopes, snow on the peaks. ----
    bool  autoColor  = true;
    // ---- Texturing (leverages the material pipeline: triplanar + normal maps) ----
    /// Ground texture tiled across the surface (grass/rock/dirt PNG). Empty = flat
    /// auto-colours only. When `autoColor` is also on, the texture is TINTED by the
    /// elevation/slope colours, so one grass texture reads green on flats and grey on
    /// cliffs. Build Game bundles the file.
    std::string texture;
    float       textureTiling = 12.0f;  ///< how many times the texture repeats across the terrain
    /// Triplanar projection: texture by world axes so steep cliffs don't smear (the
    /// classic terrain-texture fix). Uses `textureTiling` as world-space scale.
    bool        triplanarTex = true;
    /// Optional normal/bump map for fine surface detail (pebbles, grain) under lighting.
    std::string normalMap;
    float       normalStrength = 1.0f;

    Color waterColor = Color::FromBytes(54, 110, 158);
    Color sandColor  = Color::FromBytes(205, 195, 145);
    Color grassColor = Color::FromBytes(96, 150, 80);
    Color rockColor  = Color::FromBytes(122, 112, 102);
    Color snowColor  = Color::FromBytes(236, 240, 248);
    float waterLevel = 0.0f;    // world-Y at/below which terrain reads as shore/water
    float snowLevel  = 12.0f;   // world-Y above which terrain reads as snow
    float rockSlope  = 0.55f;   // face steepness (0 flat .. 1 vertical) -> rock

    Terrain() { Resize(resolution); }

    int   Dim() const { return resolution + 1; }
    float CellSize() const { return resolution > 0 ? size / resolution : size; }

    /// Resize the grid to (res+1)^2, preserving nothing (all flat at 0).
    void  Resize(int res);
    float GetHeight(int x, int z) const;
    void  SetHeight(int x, int z, float h);

    // ---- Sculpting -----------------------------------------------------
    void Flatten(float h = 0.0f);
    void Smooth();
    void Randomize(float amount, unsigned seed = 12345u);
    void Hills(int count, float maxHeight, unsigned seed = 1u);

    /// Procedurally generate a whole landscape from fractal (Perlin-like) noise.
    /// type: 0 Mountains, 1 Hills, 2 Plains, 3 Plateau (mesas), 4 Islands,
    ///       5 Ridged Mountains (sharp ridgelines), 6 Canyons (carved channels).
    /// Types 5/6 use ridged noise + domain warping for natural, organic shapes.
    /// Replaces the current heights.
    void Generate(int type, float amplitude, float frequency, int octaves, unsigned seed);

    // ---- Erosion (geological realism) ----------------------------------
    /// Hydraulic erosion: simulate `droplets` rain drops that flow downhill,
    /// picking up soil on steep ground and dropping it in flats — the standard
    /// technique that carves river valleys, gullies and sediment fans, turning
    /// blobby noise into believable landscape. `strength` scales how aggressively
    /// material is moved (0..~1). Operates in place on the heightmap.
    void Erode(int droplets, float strength = 0.3f, unsigned seed = 1337u);

    /// Thermal erosion: material on slopes steeper than `talus` (height step per
    /// cell) slumps to the lower neighbours, forming talus piles and softening
    /// cliffs into scree slopes. `iterations` passes, `strength` in [0,1].
    void ThermalErode(int iterations, float talus, float strength = 0.5f);

    // ---- Heightmap I/O (interop with World Machine / Gaea / Photoshop) --
    /// Lowest and highest height in the map (for normalizing exports / UI).
    void HeightRange(float& lo, float& hi) const;
    /// Write the heightmap to a grayscale PNG, normalized so the lowest point is
    /// black and the highest is white (the standard heightmap convention).
    bool ExportHeightmap(const std::string& path) const;
    /// Replace the heights from a grayscale PNG: black -> `lowY`, white -> `highY`.
    /// Bilinearly resampled to the current resolution, so any image size works.
    bool ImportHeightmap(const std::string& path, float lowY, float highY);

    /// Brush falloff shape. `hardness` in [0,1] sets the radius of the full-strength
    /// core (Photoshop-style): 0 = soft smoothstep from the centre, 1 = hard edge.
    /// Used by all the brushes below for a natural, pressure-like feel.
    static float BrushWeight(float dist, float radius, float hardness);

    /// Raise (or lower, with a negative delta) heights within `radius` of a point
    /// in the terrain's local XZ space, with a soft falloff — the sculpt brush.
    void RaiseAt(float localX, float localZ, float radius, float delta, float hardness = 0.4f);
    /// Brush that relaxes (averages) heights within `radius` toward their
    /// neighborhood — the Smooth brush. `amount` in [0,1] per application.
    void SmoothAt(float localX, float localZ, float radius, float amount, float hardness = 0.4f);
    /// Brush that pulls heights within `radius` toward `target` — the Flatten /
    /// Set-Height brush. `amount` in [0,1] per application.
    void FlattenAt(float localX, float localZ, float radius, float target, float amount, float hardness = 0.4f);
    /// Brush that adds fractal noise detail within `radius` — roughen up flats with
    /// bumps/grain. `amount` scales the bump height; `seed` varies the pattern.
    void NoiseAt(float localX, float localZ, float radius, float amount, unsigned seed, float hardness = 0.4f);
    /// Brush that runs a localized hydraulic erosion pass within `radius` — carve a
    /// gully / weather a patch right under the cursor. `amount` scales the effect.
    void ErodeAt(float localX, float localZ, float radius, float amount);

    /// Surface normal at a local XZ point, from the height gradient (bilinear).
    /// Used by physics for slope-aware terrain collision and by tools.
    Vec3 NormalAt(float localX, float localZ) const;
    /// Sample the terrain height at a point in local XZ (bilinear) — handy for the
    /// Flatten brush to pick up the height under the cursor.
    float SampleHeight(float localX, float localZ) const;

    // ---- Rendering -----------------------------------------------------
    /// Build a Mesh (vertices + triangles + UVs) from the heightmap.
    Mesh BuildMesh() const;
    /// Build the mesh into the sibling MeshRenderer (adds one if missing) and
    /// push the terrain color. Call after any edit.
    void Apply();

    void Start() override { Apply(); }
};

} // namespace okay
