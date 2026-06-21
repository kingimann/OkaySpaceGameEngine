#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Render/Mesh.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Mat4.hpp"
#include <vector>
#include <string>

namespace okay {

/// A stylized, riggable character creator. Builds a low-poly humanoid in one of
/// several game art styles (blocky like Minecraft/Unturned, or rounded low-poly
/// like RuneScape) from primitive parts, with lots of customization: body
/// proportions, skin/clothing/hair colors, and clothing + accessories (hats,
/// glasses, backpack, cape, beard, hair styles). The parts are bound to a 15-bone
/// humanoid skeleton (rigid skinning) so the character can be posed and plays
/// procedural animations (idle / walk / run / wave / jump). Lives next to a
/// MeshRenderer and rebuilds its mesh when changed (like Terrain / the old
/// CharacterBody).
class Character : public Component {
public:
    // ---- Style & proportions ----
    int   style    = 2;      // 0 Minecraft (boxy), 1 Unturned (boxy+detail), 2 RuneScape (rounded)
    float height   = 1.0f;   // overall height multiplier
    float build    = 1.0f;   // body/limb width (girth)
    float headSize = 1.0f;   // head scale

    // ---- Colors ----
    Color skin  = Color::FromBytes(220, 176, 150);
    Color shirt = Color::FromBytes(110, 80, 55);
    Color pants = Color::FromBytes(90, 95, 110);
    Color shoes = Color::FromBytes(70, 55, 40);
    Color hair  = Color::FromBytes(120, 70, 35);
    Color eyes  = Color::FromBytes(35, 28, 30);
    Color hat   = Color::FromBytes(150, 45, 45);
    Color pack  = Color::FromBytes(75, 60, 45);

    // ---- Clothing & accessories ----
    bool hasHair    = true;
    int  hairStyle  = 0;     // 0 short, 1 long, 2 mohawk, 3 bun
    int  shirtStyle = 2;     // 0 tank (bare arms), 1 short sleeve, 2 long sleeve
    bool hasHat      = false;
    int  hatStyle    = 0;    // 0 cap, 1 helmet, 2 top hat, 3 wizard
    bool hasGlasses  = false;
    bool hasBackpack = false;
    bool hasCape     = false;
    bool hasBeard    = false;

    // ---- Animation (plays in Play mode) ----
    int   anim      = 1;     // 0 none, 1 idle, 2 walk, 3 run, 4 wave, 5 jump
    float animSpeed = 1.0f;
    float animTime  = 0.0f;  // runtime clock (not serialized)

    // ---- Manual pose: per-bone local rotation (euler deg). Applied when
    //      anim == 0. Empty / all-zero = rest pose. ----
    std::vector<Vec3> pose;

    // ---- Rig ----
    struct Bone { int parent; Vec3 joint; };
    static int  BoneCount();
    static const char* BoneName(int i);
    std::vector<Bone> Skeleton() const;     // rest skeleton in build units (pre-height-scale)

    /// Build the rest mesh, filling `bone` with the bone index that drives each
    /// vertex (parallel to mesh.vertices). Used internally by Apply()/Update().
    Mesh BuildRig(std::vector<int>& bone) const;
    /// Build the rest mesh only.
    Mesh Build() const { std::vector<int> b; return BuildRig(b); }

    /// Local bone rotations (euler deg, parallel to the skeleton) for the current
    /// animation at time `t`, or the manual `pose` when anim == 0.
    std::vector<Vec3> PoseAt(float t) const;
    /// Deform `m` (rest mesh + `bone` weights) by local bone rotations `rot`.
    void Skin(Mesh& m, const std::vector<int>& bone, const std::vector<Vec3>& rot) const;

    /// Build into the sibling MeshRenderer (adding one if needed) at the current pose.
    void Apply();

    void Start() override { Apply(); }
    void Update(float dt) override;     // advance + apply the animation

    std::string ToText() const;
    void FromText(const std::string& text);

private:
    // Cached rest mesh + bone weights so animation doesn't rebuild geometry per frame.
    mutable Mesh m_rest;
    mutable std::vector<int> m_bone;
    mutable std::vector<Vec3> m_restPos;
    mutable bool m_built = false;
    void EnsureRest() const;
};

} // namespace okay
