#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Render/Mesh.hpp"
#include "okay/Render/Color.hpp"

namespace okay {

/// A parametric humanoid "character creator": proportion sliders (height, build,
/// head size, shoulders, hips, limb lengths/thickness) plus a subdivision level
/// that takes it from low-poly to smooth high-poly. Lives next to a MeshRenderer
/// and rebuilds that renderer's mesh whenever it changes (like Terrain).
class CharacterBody : public Component {
public:
    HumanoidParams params;
    int   subdivisions = 0;        // 0..3 Subdivide+Smooth passes (low -> high poly)
    float smoothAmount = 0.5f;     // Laplacian relax strength per pass
    Color color = Color::FromBytes(214, 178, 150);   // skin-ish default

    /// Build the mesh described by the current parameters.
    Mesh Build() const {
        Mesh m = Mesh::Humanoid(params);
        int n = subdivisions < 0 ? 0 : (subdivisions > 3 ? 3 : subdivisions);
        if (n > 0) m.SubdivideSmooth(n, smoothAmount);
        return m;
    }

    /// Build into the sibling MeshRenderer (adding one if needed) and push color.
    void Apply();

    void Start() override { Apply(); }
};

} // namespace okay
