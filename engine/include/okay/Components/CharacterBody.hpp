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
    Color color  = Color::FromBytes(214, 178, 150);  // skin (head/neck/hands/nose)
    Color outfit = Color::FromBytes(70, 110, 170);   // shirt (torso/arms)
    Color pants  = Color::FromBytes(50, 55, 70);     // hips/legs
    Color shoes  = Color::FromBytes(35, 35, 40);     // feet
    Color hair   = Color::FromBytes(60, 40, 30);     // hair cap / brows / mouth
    Color eye    = Color::FromBytes(40, 40, 50);     // eyes
    bool  hasHair = true;
    bool  hasFace = true;

    /// Build the mesh described by the current parameters (per-part colors).
    Mesh Build() const {
        HumanoidColors c;
        c.skin = color; c.shirt = outfit; c.pants = pants; c.shoes = shoes;
        c.hair = hair;  c.eye = eye;      c.hasHair = hasHair; c.hasFace = hasFace;
        Mesh m = Mesh::Humanoid(params, &c);
        int n = subdivisions < 0 ? 0 : (subdivisions > 3 ? 3 : subdivisions);
        if (n > 0) m.SubdivideSmooth(n, smoothAmount);
        return m;
    }

    /// Build into the sibling MeshRenderer (adding one if needed) and push color.
    void Apply();

    void Start() override { Apply(); }
};

} // namespace okay
