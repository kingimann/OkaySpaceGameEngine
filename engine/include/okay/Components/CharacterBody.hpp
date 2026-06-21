#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Render/Mesh.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Mat4.hpp"

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
    int   subdivisions = 1;        // 0..3 Subdivide+Smooth passes (low -> high poly)
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
    bool  realistic = true;       // use the bundled anatomical human base mesh (default)
    bool  lowPoly = true;         // flat-shaded low-poly modelled body
    bool  smoothBody = true;      // seamless single-surface body (SDF + Surface Nets)
    int   smoothRes = 48;         // grid resolution for the seamless body

    // Limb animation, played during Play. 0 None,1 Idle,2 Walk,3 Run,4 Wave,5 Jump.
    int   anim = 0;
    float animSpeed = 1.0f;
    bool  rootMotion = true;      // Walk/Run travel the GameObject forward
    float animTime = 0.0f;        // runtime clock (not serialized)
    float restY = 0.0f;           // captured base height for Jump (runtime)
    bool  restYset = false;       // (runtime)
    Vec3  lastPos = {0, 0, 0};    // previous world position (Auto anim, runtime)
    bool  lastPosSet = false;     // (runtime)

    std::vector<Accessory> accessories;   // user-added custom parts

    // ---- Skeletal rig: pose the character by rotating bones. The body mesh is
    //      skinned to a humanoid skeleton (linear-blend skinning), so rotating a
    //      bone bends the limb. `pose[i]` is bone i's local rotation (euler deg);
    //      all-zero (or empty) = the rest pose, mesh unchanged. ----
    struct Bone { int parent; Vec3 joint; Vec3 tail; };
    std::vector<Vec3> pose;                 // per-bone local rotation (euler deg)
    static int  BoneCount();                // number of rig bones
    static const char* BoneName(int i);     // UI label for bone i
    std::vector<Bone> BuildSkeleton(const HumanoidParams& p) const;
    void ApplyPose(Mesh& m, const HumanoidParams& p) const;   // deform m by `pose`
    /// The bundled CC0 anatomical human base mesh, loaded once and normalized
    /// (centered on X/Z, feet at y=0, height 1). Empty if the asset isn't found.
    static const Mesh& HumanBaseMesh();

    /// Build the mesh for an explicit parameter set (used for animation frames).
    Mesh Build(const HumanoidParams& pp0) const {
        HumanoidParams pp = pp0; pp.ClampHuman();   // keep proportions human
        HumanoidColors c;
        c.skin = color; c.shirt = outfit; c.pants = pants; c.shoes = shoes;
        c.hair = hair;  c.eye = eye;      c.hasHair = hasHair; c.hasFace = hasFace;
        c.hat = hat; c.glasses = glasses; c.hasHat = hasHat; c.hasGlasses = hasGlasses;
        c.beard = beard; c.mustache = mustache;
        Mesh m;
        bool usedBase = false;
        if (realistic) {
            const Mesh& base = HumanBaseMesh();           // bundled anatomical human
            if (!base.vertices.empty()) {
                m = base;                                 // normalized: feet y=0, height 1
                // Tint faces by region so the nude base reads as clothed: legs ->
                // pants, torso -> shirt, arms/head/hands -> skin, feet -> shoes.
                const float shoulderHalf = 0.19f;
                m.triColors.clear();
                m.triColors.reserve(m.TriangleCount());
                for (std::size_t t = 0; t + 2 < m.triangles.size(); t += 3) {
                    const Vec3& a = m.vertices[m.triangles[t]];
                    const Vec3& b = m.vertices[m.triangles[t + 1]];
                    const Vec3& d = m.vertices[m.triangles[t + 2]];
                    float yy = (a.y + b.y + d.y) / 3.0f;
                    float xx = (std::fabs(a.x) + std::fabs(b.x) + std::fabs(d.x)) / 3.0f;
                    Color col;
                    if (yy < 0.06f) col = shoes;
                    else if (xx > shoulderHalf && yy > 0.42f && yy < 0.86f) col = color; // arms
                    else if (yy < 0.50f) col = pants;
                    else if (yy < 0.80f) col = outfit;
                    else if (yy > 0.93f && hasHair) col = hair;   // skullcap
                    else col = color;                     // face / neck
                    m.triColors.push_back(col);
                }
                // ---- Parametric morph: reshape the base mesh per region from the
                //      sliders + gender, MakeHuman-style. Operates on the
                //      normalized mesh (feet y=0, head y=1, centered X/Z). ----
                auto lerpf = [](float a, float b, float t) { return a + (b - a) * t; };
                const float g = pp.gender;                 // 0 female .. 1 male
                const float shoulderMul = pp.shoulderWidth * lerpf(0.88f, 1.14f, g);
                const float hipMul      = pp.hipWidth      * lerpf(1.14f, 0.90f, g);
                const float waistMul    = pp.waist         * lerpf(0.80f, 1.00f, g);
                const float musc        = lerpf(0.86f, 1.14f, pp.muscle);
                const float armTh = pp.armThickness * musc, legTh = pp.legThickness * musc;
                // vertical width profile of the trunk (hips -> waist -> shoulders)
                auto wprofile = [&](float y) {
                    if (y >= 0.81f) return shoulderMul;
                    if (y >= 0.62f) return lerpf(waistMul, shoulderMul, (y - 0.62f) / 0.19f);
                    if (y >= 0.50f) return lerpf(hipMul, waistMul, (y - 0.50f) / 0.12f);
                    return hipMul;
                };
                for (Vec3& v : m.vertices) {
                    float y = v.y, ax = std::fabs(v.x);
                    if (y < 0.47f) {                       // ---- LEG ----
                        float s = v.x < 0 ? -1.0f : 1.0f, lc = s * 0.09f * hipMul;
                        v.x += (hipMul - 1.0f) * 0.09f * s;        // follow the hips outward
                        v.y = 0.50f + (v.y - 0.50f) * pp.legLength;// length
                        v.x = lc + (v.x - lc) * legTh; v.z *= legTh;// girth
                    } else if (y > 0.86f) {                // ---- HEAD ----
                        Vec3 C{0.0f, 0.86f, 0.0f};
                        v = C + (v - C) * pp.headSize;
                        v.y += (pp.neckLength - 1.0f) * 0.04f;
                    } else if (ax > 0.18f) {               // ---- ARM ----
                        float s = v.x < 0 ? -1.0f : 1.0f;
                        v.x += (shoulderMul - 1.0f) * 0.18f * s;   // follow the shoulders
                        Vec3 S{s * 0.18f * shoulderMul, 0.80f, 0.0f};
                        Vec3 dir{s * 0.34f, -0.34f, 0.0f}; float dl = std::sqrt(dir.x*dir.x+dir.y*dir.y);
                        dir.x /= dl; dir.y /= dl;
                        Vec3 rel = v - S; float t = rel.x * dir.x + rel.y * dir.y;
                        Vec3 axis{S.x + dir.x * t, S.y + dir.y * t, 0.0f};
                        Vec3 perp = v - axis;
                        axis = {S.x + dir.x * (t * pp.armLength), S.y + dir.y * (t * pp.armLength), 0.0f};
                        v = {axis.x + perp.x * armTh, axis.y + perp.y * armTh, axis.z + v.z * armTh};
                        v.z *= armTh;
                    } else {                               // ---- TORSO ----
                        float w = wprofile(y);
                        v.x *= w;
                        if (y > 0.54f && y < 0.70f) v.z *= waistMul;            // waist depth
                        if (g < 0.5f && y > 0.66f && y < 0.80f && v.z > 0.0f)   // female bust
                            v.z += (0.5f - g) * 0.05f;
                    }
                }
                // Global size: height, plus overall build thickness + body depth.
                float Hgt = 1.85f * pp.height;
                float wx = Hgt * pp.build, wz = Hgt * pp.build * pp.bodyDepth;
                for (Vec3& v : m.vertices) { v.x *= wx; v.y *= Hgt; v.z *= wz; }
                usedBase = true;
            }
        }
        if (usedBase) {
            // base mesh ready
        } else if (lowPoly) {
            m = BuildLowPolyHumanoid(pp, &c);             // flat-shaded modelled body
        } else if (smoothBody) {
            m = BuildSmoothHumanoid(pp, &c, smoothRes);   // seamless single-surface body
        } else {
            m = Mesh::Humanoid(pp, &c);
            int n = subdivisions < 0 ? 0 : (subdivisions > 4 ? 4 : subdivisions);
            if (n > 0) m.SubdivideSmooth(n, smoothAmount);
        }
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
                    float sw = 0.24f * pp.shoulderWidth;
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
        // The seamless body is a single CLOSED, consistently-wound surface, so we
        // flip it to outward winding and render it single-sided (Apply() turns off
        // doubleSided) — that lets the renderer backface-cull ~half the triangles,
        // a big win when several characters share a scene. (The part-based body
        // has mixed winding and must stay double-sided.)
        if (smoothBody && !lowPoly && !usedBase)
            for (std::size_t i = 0; i + 2 < m.triangles.size(); i += 3)
                std::swap(m.triangles[i + 1], m.triangles[i + 2]);
        ApplyPose(m, pp);           // bend the mesh to the posed skeleton (if any)
        if (usedBase || !lowPoly) m.ComputeSmoothNormals();   // smooth shading (low-poly stays flat)
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
