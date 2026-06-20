#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Render/Mesh.hpp"
#include "okay/Render/Color.hpp"

namespace okay {

/// A user-defined accessory part attached to a character: a primitive shape
/// placed in the character's local space, with its own color. Users add these in
/// the inspector to build hats, swords, wings, antennae, etc.
struct Accessory {
    std::string name  = "Accessory";
    std::string shape = "Cube";          // primitive (Mesh::FromName)
    Vec3  offset = {0.0f, 1.9f, 0.25f};
    Vec3  scale  = {0.2f, 0.2f, 0.2f};
    Vec3  rotation = {0.0f, 0.0f, 0.0f}; // euler degrees
    Color color = Color::FromBytes(200, 200, 205);
};

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
    Color hat    = Color::FromBytes(150, 40, 40);    // hat
    Color glasses= Color::FromBytes(20, 20, 25);     // glasses frame
    bool  hasHair = true;
    bool  hasFace = true;
    bool  hasHat = false;
    bool  hasGlasses = false;

    // Simple limb animation, played during Play. 0 None, 1 Idle, 2 Walk, 3 Run.
    int   anim = 0;
    float animSpeed = 1.0f;
    float animTime = 0.0f;        // runtime clock (not serialized)

    std::vector<Accessory> accessories;   // user-added custom parts

    /// Build the mesh for an explicit parameter set (used for animation frames).
    Mesh Build(const HumanoidParams& pp) const {
        HumanoidColors c;
        c.skin = color; c.shirt = outfit; c.pants = pants; c.shoes = shoes;
        c.hair = hair;  c.eye = eye;      c.hasHair = hasHair; c.hasFace = hasFace;
        c.hat = hat; c.glasses = glasses; c.hasHat = hasHat; c.hasGlasses = hasGlasses;
        Mesh m = Mesh::Humanoid(pp, &c);
        int n = subdivisions < 0 ? 0 : (subdivisions > 3 ? 3 : subdivisions);
        if (n > 0) m.SubdivideSmooth(n, smoothAmount);
        // User accessories on top (not subdivided, so edges stay crisp).
        for (const Accessory& a : accessories)
            m.AddPosed(Mesh::FromName(a.shape), a.offset, a.scale, a.rotation, a.offset, &a.color);
        return m;
    }
    /// Build the mesh described by the current (rest) parameters.
    Mesh Build() const { return Build(params); }

    /// Build into the sibling MeshRenderer (adding one if needed) and push color.
    void Apply();

    void Start() override { Apply(); }
    void Update(float dt) override;   // animate limbs while playing
};

} // namespace okay
