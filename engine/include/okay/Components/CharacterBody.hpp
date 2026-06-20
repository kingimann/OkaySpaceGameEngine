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
    // Attach point: 0 Origin, 1 Head, 2 Torso, 3 Hips, 4 Left Hand, 5 Right Hand,
    // 6 Back. Non-origin anchors track proportions and animation (e.g. a sword in
    // the hand swings with the arm; a hat on the head rises with height).
    int   attach = 0;
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
    bool  beard = false;
    bool  mustache = false;

    // Limb animation, played during Play. 0 None,1 Idle,2 Walk,3 Run,4 Wave,5 Jump.
    int   anim = 0;
    float animSpeed = 1.0f;
    bool  rootMotion = true;      // Walk/Run travel the GameObject forward
    float animTime = 0.0f;        // runtime clock (not serialized)
    float restY = 0.0f;           // captured base height for Jump (runtime)
    bool  restYset = false;       // (runtime)

    std::vector<Accessory> accessories;   // user-added custom parts

    /// Build the mesh for an explicit parameter set (used for animation frames).
    Mesh Build(const HumanoidParams& pp) const {
        HumanoidColors c;
        c.skin = color; c.shirt = outfit; c.pants = pants; c.shoes = shoes;
        c.hair = hair;  c.eye = eye;      c.hasHair = hasHair; c.hasFace = hasFace;
        c.hat = hat; c.glasses = glasses; c.hasHat = hasHat; c.hasGlasses = hasGlasses;
        c.beard = beard; c.mustache = mustache;
        Mesh m = Mesh::Humanoid(pp, &c);
        int n = subdivisions < 0 ? 0 : (subdivisions > 3 ? 3 : subdivisions);
        if (n > 0) m.SubdivideSmooth(n, smoothAmount);
        // Anchor (position + euler) of an attach region, matching Humanoid()'s
        // layout so accessories follow proportions and animation.
        auto anchorOf = [&](int region, Vec3& pos, Vec3& euler) {
            float H = pp.height, B = pp.build, bd = pp.bodyDepth;
            float up = 0.78f * H * (pp.torsoLength - 1.0f);
            float headY = 1.78f * H + up;
            float torsoY = (0.71f * H) + 0.39f * H * pp.torsoLength;
            pos = {0, 0, 0}; euler = {0, 0, 0};
            switch (region) {
                case 1: pos = {0, headY, 0}; break;                         // head
                case 2: pos = {0, torsoY, 0}; break;                        // torso
                case 3: pos = {0, 0.66f * H, 0}; break;                     // hips
                case 6: pos = {0, torsoY, -0.34f * B * bd}; break;          // back
                case 4: case 5: {                                          // hands
                    int s = (region == 4) ? -1 : 1;
                    float sw = 0.46f * pp.shoulderWidth;
                    Vec3 shoulder{s * sw, 1.50f * H + up, 0};
                    Vec3 handRest{s * sw, (1.18f - 0.54f * pp.armLength) * H + up, 0};
                    Vec3 armRot{(float)s * pp.armSwing, 0.0f, (float)s * -pp.armSpread};
                    Quat q = Quat::Euler(armRot);
                    pos = shoulder + q * (handRest - shoulder);
                    euler = armRot;
                    break;
                }
                default: break;                                            // 0 = local origin
            }
        };
        // User accessories on top (not subdivided, so edges stay crisp).
        for (const Accessory& a : accessories) {
            Vec3 ap, ae; anchorOf(a.attach, ap, ae);
            Quat q = Quat::Euler(ae);
            Vec3 world = ap + q * a.offset;
            Vec3 eul{ae.x + a.rotation.x, ae.y + a.rotation.y, ae.z + a.rotation.z};
            m.AddPosed(Mesh::FromName(a.shape), world, a.scale, eul, world, &a.color);
        }
        return m;
    }
    /// Build the mesh described by the current (rest) parameters.
    Mesh Build() const { return Build(params); }

    /// Build into the sibling MeshRenderer (adding one if needed) and push color.
    void Apply();

    void Start() override { Apply(); }
    void Update(float dt) override;   // animate limbs while playing

    /// Serialize all character settings to a portable ".okaychar" text blob, and
    /// restore them — for a reusable character preset library.
    std::string ToText() const;
    void FromText(const std::string& text);
};

} // namespace okay
