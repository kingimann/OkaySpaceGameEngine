#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Render/Mesh.hpp"
#include "okay/Render/Color.hpp"
#include <string>

namespace okay {

/// Holds a 3D mesh to be drawn at the GameObject's Transform. The editor renders
/// it as a wireframe through the active 3D camera; a GPU backend would shade it.
class MeshRenderer : public Component {
public:
    Mesh  mesh;
    Color color = Color::White;
    bool  wireframe = true;
    /// Optional .OBJ model file. When set, the scene loader replaces `mesh` with
    /// the loaded geometry (and Build Game bundles the file alongside the exe).
    std::string meshPath;

    MeshRenderer() : mesh(Mesh::Cube()) {}
    explicit MeshRenderer(Mesh m) : mesh(std::move(m)) {}
};

} // namespace okay
