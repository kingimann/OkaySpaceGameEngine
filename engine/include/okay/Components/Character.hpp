#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Render/Mesh.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Mat4.hpp"
#include <vector>
#include <string>

namespace okay {

/// A blocky, flat-shaded low-poly character creator (Unturned style) with deep
/// customization: one height slider, a big palette of colors, and lots of
/// clothing / hair / accessory options. The parts are bound to a 15-bone humanoid
/// skeleton (rigid skinning) so the character can be posed and plays procedural
/// animations (idle / walk / run / wave / jump). Lives next to a MeshRenderer and
/// rebuilds its mesh when changed (like Terrain).
class Character : public Component {
public:
    // ---- Proportions ----
    float height = 1.0f;     // overall height multiplier

    // ---- Colors ----
    Color skin   = Color::FromBytes(220, 176, 150);
    Color shirt  = Color::FromBytes(60, 95, 75);
    Color pants  = Color::FromBytes(40, 42, 55);
    Color shoes  = Color::FromBytes(38, 35, 38);
    Color hair   = Color::FromBytes(80, 50, 30);
    Color eyes   = Color::FromBytes(30, 26, 28);
    Color hat    = Color::FromBytes(150, 45, 45);
    Color pack   = Color::FromBytes(75, 60, 45);
    Color gloves = Color::FromBytes(45, 40, 35);
    Color jacket = Color::FromBytes(70, 70, 80);
    Color belt   = Color::FromBytes(35, 28, 22);

    // ---- Hair & facial hair ----
    bool hasHair    = true;
    int  hairStyle  = 0;     // 0 short,1 long,2 mohawk,3 bun,4 spiky,5 afro,6 ponytail,7 buzz
    int  beardStyle = 0;     // 0 none,1 full,2 goatee,3 mustache

    // ---- Clothing ----
    int  shirtStyle = 2;     // 0 tank (bare arms),1 short sleeve,2 long sleeve
    int  legStyle   = 0;     // 0 trousers,1 shorts
    bool hasBelt    = true;
    bool hasJacket  = false;
    bool hasGloves  = false;

    // ---- Accessories ----
    bool hasHat        = false;
    int  hatStyle      = 0;  // 0 cap,1 helmet,2 top hat,3 wizard,4 beanie,5 cowboy,6 crown,7 bandana
    int  glassesStyle  = 0;  // 0 none,1 glasses,2 sunglasses
    bool hasMask       = false;
    bool hasScarf      = false;
    bool hasShoulderPads = false;
    bool hasBackpack   = false;
    bool hasCape       = false;

    // ---- Animation (plays in Play mode) ----
    int   anim      = 1;     // 0 none,1 idle,2 walk,3 run,4 wave,5 jump,6 crouch,7 prone,
                             // 8 point,9 clap,10 thumbs-up,11 salute,12 wave-both,
                             // 13 cheer/happy,14 sad,15 angry,16 think (gestures + emotions)
    float animSpeed = 1.0f;
    float animTime  = 0.0f;  // runtime clock (not serialized)

    // Head look: layered on top of the current animation so the head turns/tilts
    // toward where the player (or camera) is aiming. Degrees; not serialized — the
    // controllers drive these as TARGETS every frame, and the head eases toward
    // them (headTurnSpeed) so it moves smoothly instead of snapping.
    float lookYaw   = 0.0f;       // + turns the head to its right
    float lookPitch = 0.0f;       // + lifts the gaze up
    float headTurnSpeed = 9.0f;   // how fast the head eases toward the look target
    float HeadYaw()   const { return m_headYaw; }
    float HeadPitch() const { return m_headPitch; }

    // Body lean: roll the upper body sideways (peeking around cover). Target in
    // degrees (+ leans to the body's right); eased like the head. Not serialized —
    // the controllers drive it each frame.
    float bodyLean = 0.0f;
    float BodyLean() const { return m_bodyLean; }

    // ---- Manual pose: per-bone local rotation (euler deg). Applied when
    //      anim == 0. Empty / all-zero = rest pose. ----
    std::vector<Vec3> pose;

    // ---- Rig ----
    struct Bone { int parent; Vec3 joint; };
    static int  BoneCount();
    static const char* BoneName(int i);
    std::vector<Bone> Skeleton() const;

    Mesh BuildRig(std::vector<int>& bone) const;
    Mesh Build() const { std::vector<int> b; return BuildRig(b); }

    std::vector<Vec3> PoseAt(float t) const;
    /// World-space body offset for the current anim (e.g. lowering the whole body
    /// for crouch / prone, since the rig's root is otherwise pinned at hip height).
    Vec3 StanceOffset() const;
    void Skin(Mesh& m, const std::vector<int>& bone, const std::vector<Vec3>& rot) const;

    void Apply();
    void Start() override { Apply(); }
    void Update(float dt) override;

    std::string ToText() const;
    void FromText(const std::string& text);

private:
    mutable Mesh m_rest;
    mutable std::vector<int> m_bone;
    mutable std::vector<Vec3> m_restPos;
    mutable bool m_built = false;
    float m_headYaw = 0.0f;        // eased head turn (toward lookYaw)
    float m_headPitch = 0.0f;      // eased head tilt (toward lookPitch)
    float m_bodyLean = 0.0f;       // eased body roll (toward bodyLean)
    void EnsureRest() const;
};

} // namespace okay
