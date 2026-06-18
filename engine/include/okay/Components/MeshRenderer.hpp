#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Render/Mesh.hpp"
#include "okay/Render/Color.hpp"

namespace okay {

/// Holds a 3D mesh to be drawn at the GameObject's Transform. The editor renders
/// it as a wireframe through the active 3D camera; a GPU backend would shade it.
class MeshRenderer : public Component {
public:
    Mesh  mesh;
    Color color = Color::White;
    bool  wireframe = true;

    MeshRenderer() : mesh(Mesh::Cube()) {}
    explicit MeshRenderer(Mesh m) : mesh(std::move(m)) {}
};

} // namespace okay
