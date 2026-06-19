#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Render/Mesh.hpp"
#include "okay/Render/Color.hpp"
#include <string>

namespace okay {

/// Holds a 3D mesh to be drawn at the GameObject's Transform. Rendered solid
/// (flat-shaded, back-face-culled, depth-sorted) by both the editor and the
/// player; enable `wireframe` for an edges-only view.
class MeshRenderer : public Component {
public:
    Mesh  mesh;
    Color color = Color::White;        // base (albedo) color
    bool  wireframe = false;   // solid by default (Unity-like); true = edges only
    /// Render both faces of each triangle (no back-face culling) — for planes,
    /// flags, foliage cards, and the insides of open meshes like Tube.
    bool  doubleSided = false;

    // ---- Material (honored by the software renderer) ------------------
    /// Self-illumination added on top of lighting (glowing screens, lava, neon).
    Color emissive = Color::Black;
    /// Blinn-Phong specular highlight: intensity [0,1] and tightness (exponent).
    float specular  = 0.0f;
    float shininess = 16.0f;
    /// Skip lighting entirely and draw the flat base color.
    bool  unlit = false;
    /// Optional texture image (PNG/JPG…). When set, the solid renderer maps it
    /// onto the mesh (planar/box projection) tinted by `color`. Build Game
    /// bundles the file alongside the exe.
    std::string texture;

    /// Optional .OBJ model file. When set, the scene loader replaces `mesh` with
    /// the loaded geometry (and Build Game bundles the file alongside the exe).
    std::string meshPath;

    MeshRenderer() : mesh(Mesh::Cube()) {}
    explicit MeshRenderer(Mesh m) : mesh(std::move(m)) {}
};

} // namespace okay
