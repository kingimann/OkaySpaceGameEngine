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
    /// Raise (or lower, with a negative delta) heights within `radius` of a point
    /// in the terrain's local XZ space, with a soft falloff — the sculpt brush.
    void RaiseAt(float localX, float localZ, float radius, float delta);

    // ---- Rendering -----------------------------------------------------
    /// Build a Mesh (vertices + triangles + UVs) from the heightmap.
    Mesh BuildMesh() const;
    /// Build the mesh into the sibling MeshRenderer (adds one if missing) and
    /// push the terrain color. Call after any edit.
    void Apply();

    void Start() override { Apply(); }
};

} // namespace okay
