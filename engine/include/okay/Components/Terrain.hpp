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
    /// type: 0 Mountains, 1 Hills, 2 Plains, 3 Plateau (mesas), 4 Islands.
    /// Replaces the current heights.
    void Generate(int type, float amplitude, float frequency, int octaves, unsigned seed);

    /// Raise (or lower, with a negative delta) heights within `radius` of a point
    /// in the terrain's local XZ space, with a soft falloff — the sculpt brush.
    void RaiseAt(float localX, float localZ, float radius, float delta);
    /// Brush that relaxes (averages) heights within `radius` toward their
    /// neighborhood — the Smooth brush. `amount` in [0,1] per application.
    void SmoothAt(float localX, float localZ, float radius, float amount);
    /// Brush that pulls heights within `radius` toward `target` — the Flatten /
    /// Set-Height brush. `amount` in [0,1] per application.
    void FlattenAt(float localX, float localZ, float radius, float target, float amount);
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
