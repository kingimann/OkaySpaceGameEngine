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
    Color color = Color::White;
    bool  wireframe = false;   // solid by default (Unity-like); true = edges only
    /// Optional .OBJ model file. When set, the scene loader replaces `mesh` with
    /// the loaded geometry (and Build Game bundles the file alongside the exe).
    std::string meshPath;

    MeshRenderer() : mesh(Mesh::Cube()) {}
    explicit MeshRenderer(Mesh m) : mesh(std::move(m)) {}
};

} // namespace okay
