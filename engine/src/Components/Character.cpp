#include "okay/Components/Character.hpp"
#include "okay/Animation/HumanoidMotion.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/ProceduralObjectHolding.hpp"
#include "okay/Components/FirstPersonHands.hpp"
#include "okay/Components/HoldableObject.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Math/Mathf.hpp"
#include <algorithm>
#include <iomanip>
#include <filesystem>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace okay {

using namespace HumanoidMotion;   // the bone indices (B_HIPS ...)

int Character::BoneCount() { return B_COUNT; }

int Character::BoneIndex(const std::string& token) {
    static const std::unordered_map<std::string, int> m = {
        {"hips", B_HIPS}, {"torso", B_TORSO}, {"head", B_HEAD},
        {"l_uparm", B_LUPARM}, {"l_fore", B_LFORE}, {"l_hand", B_LHAND},
        {"r_uparm", B_RUPARM}, {"r_fore", B_RFORE}, {"r_hand", B_RHAND},
        {"l_thigh", B_LTHIGH}, {"l_shin", B_LSHIN}, {"l_foot", B_LFOOT},
        {"r_thigh", B_RTHIGH}, {"r_shin", B_RSHIN}, {"r_foot", B_RFOOT},
    };
    std::string key;
    key.reserve(token.size());
    for (char c : token) key += (char)std::tolower((unsigned char)c);
    auto it = m.find(key);
    return it != m.end() ? it->second : -1;
}

const char* Character::BoneName(int i) {
    static const char* n[B_COUNT] = {
        "Hips", "Torso", "Head",
        "L Upper Arm", "L Forearm", "L Hand", "R Upper Arm", "R Forearm", "R Hand",
        "L Thigh", "L Shin", "L Foot", "R Thigh", "R Shin", "R Foot" };
    return (i >= 0 && i < B_COUNT) ? n[i] : "?";
}

const char* Character::BoneToken(int i) {
    static const char* n[B_COUNT] = {
        "hips", "torso", "head", "l_uparm", "l_fore", "l_hand", "r_uparm", "r_fore", "r_hand",
        "l_thigh", "l_shin", "l_foot", "r_thigh", "r_shin", "r_foot" };
    return (i >= 0 && i < B_COUNT) ? n[i] : "";
}

std::vector<Character::Bone> Character::Skeleton() const {
    std::vector<Bone> b(B_COUNT);
    b[B_HIPS]   = {-1,       {0.0f,   0.95f, 0}};
    b[B_TORSO]  = {B_HIPS,   {0.0f,   1.05f, 0}};
    b[B_HEAD]   = {B_TORSO,  {0.0f,   1.48f, 0}};
    b[B_LUPARM] = {B_TORSO,  {-0.22f, 1.40f, 0}};
    b[B_LFORE]  = {B_LUPARM, {-0.265f,1.12f, 0}};
    b[B_LHAND]  = {B_LFORE,  {-0.285f,0.86f, 0}};
    b[B_RUPARM] = {B_TORSO,  {0.22f,  1.40f, 0}};
    b[B_RFORE]  = {B_RUPARM, {0.265f, 1.12f, 0}};
    b[B_RHAND]  = {B_RFORE,  {0.285f, 0.86f, 0}};
    b[B_LTHIGH] = {B_HIPS,   {-0.105f,0.92f, 0}};
    b[B_LSHIN]  = {B_LTHIGH, {-0.105f,0.55f, 0}};
    b[B_LFOOT]  = {B_LSHIN,  {-0.105f,0.12f, 0}};
    b[B_RTHIGH] = {B_HIPS,   {0.105f, 0.92f, 0}};
    b[B_RSHIN]  = {B_RTHIGH, {0.105f, 0.55f, 0}};
    b[B_RFOOT]  = {B_RSHIN,  {0.105f, 0.12f, 0}};
    return b;
}

namespace {
Color mul(Color c, float f) { return Color(c.r * f, c.g * f, c.b * f, 1.0f); }

// The block body is modelled facing +Z. Cameras and movement look down -Z, so
// the baked mesh is mirrored onto -Z. That mirror reverses winding; swapping
// each triangle puts the faces back outward, and a single-sided draw then
// shows the shell instead of the cavity.
void FaceMinusZ(Mesh& m) {
    for (Vec3& v : m.vertices) v = Vec3{-v.x, v.y, -v.z};
    for (std::size_t i = 0; i + 2 < m.triangles.size(); i += 3)
        std::swap(m.triangles[i + 1], m.triangles[i + 2]);
}

// The neck is a small box skinned to the torso, so it cannot be hidden on its
// own while it still lives in the torso mesh. In torso-joint space it sits
// just above the collar. Only that box moves off the shirt: the shirt's own
// lid stays, or a look down stares through the open top at whatever is inside.
bool NeckLocalVert(const Vec3& v) {
    return std::fabs(v.x) <= 0.085f && std::fabs(v.z) <= 0.085f
        && v.y >= 0.395f && v.y <= 0.505f;
}
}

Mesh Character::BuildRig(std::vector<int>& bone) const {
    Mesh m;
    bone.clear();
    const Color mouth    = Color::FromBytes(150, 95, 90);
    const Color maskCol  = Color::FromBytes(58, 58, 64);
    const Color upperArmCol = (shirtStyle >= 1) ? shirt : skin;
    const Color foreArmCol  = (shirtStyle >= 2) ? shirt : skin;
    const Color handCol     = hasGloves ? gloves : skin;
    const Color shinCol     = (legStyle == 1) ? skin : pants;   // shorts -> bare shins

    auto add = [&](const Mesh& prim, Vec3 c, Vec3 s, Color col, int b) {
        int before = (int)m.vertices.size();
        m.Add(prim, c, s, &col);
        for (int i = before; i < (int)m.vertices.size(); ++i) bone.push_back(b);
    };
    auto box = [&](Vec3 c, Vec3 s, Color col, int b) { add(Mesh::Cube(1.0f), c, s, col, b); };

    // ---- Body (Unturned: boxy, segmented, flat-shaded) ----
    box({0, 0.95f, 0}, {0.40f, 0.22f, 0.235f}, pants, B_HIPS);
    box({0, 1.25f, 0}, {0.44f, 0.50f, 0.25f}, shirt, B_TORSO);
    if (hasBelt) box({0, 1.00f, 0}, {0.41f, 0.06f, 0.255f}, belt, B_HIPS);
    for (int s = -1; s <= 1; s += 2) {
        int up = s < 0 ? B_LUPARM : B_RUPARM, fo = s < 0 ? B_LFORE : B_RFORE, ha = s < 0 ? B_LHAND : B_RHAND;
        box({s * 0.27f, 1.30f, 0}, {0.13f, 0.34f, 0.18f}, upperArmCol, up);
        // Stops a centimetre short of the upper arm and of the hand. Sharing a
        // face with either one z-fights on a depth buffer: dark vertical lines
        // down the hand, as if the old finger gaps were still there.
        box({s * 0.27f, 0.995f, 0}, {0.10f, 0.25f, 0.14f}, foreArmCol, fo);
        // One brick on the hand bone, the block this rig used before the palm,
        // thumb and finger boxes. No finger bones: the hand is still l_hand / r_hand.
        box({s * 0.27f, 0.80f, 0.02f}, {0.12f, 0.12f, 0.17f}, handCol, ha);
        int th = s < 0 ? B_LTHIGH : B_RTHIGH, sh = s < 0 ? B_LSHIN : B_RSHIN, ft = s < 0 ? B_LFOOT : B_RFOOT;
        box({s * 0.105f, 0.74f, 0}, {0.18f, 0.42f, 0.20f}, pants, th);
        box({s * 0.105f, 0.34f, 0}, {0.16f, 0.44f, 0.18f}, shinCol, sh);
        box({s * 0.105f, 0.05f, 0.05f}, {0.17f, 0.10f, 0.30f}, shoes, ft);
    }
    box({0, 1.50f, 0}, {0.15f, 0.10f, 0.15f}, skin, B_TORSO);                 // neck
    box({0, 1.62f, 0}, {0.30f, 0.32f, 0.30f}, skin, B_HEAD);                  // head
    for (int s = -1; s <= 1; s += 2)
        box({s * 0.07f, 1.64f, 0.16f}, {0.05f, 0.06f, 0.03f}, eyes, B_HEAD);  // eyes

    // ---- Jacket: a slightly larger shell over torso + upper arms ----
    if (hasJacket) {
        box({0, 1.24f, 0}, {0.47f, 0.46f, 0.28f}, jacket, B_TORSO);
        for (int s = -1; s <= 1; s += 2)
            box({s * 0.27f, 1.31f, 0}, {0.16f, 0.33f, 0.21f}, jacket, s < 0 ? B_LUPARM : B_RUPARM);
    }
    if (hasShoulderPads)
        for (int s = -1; s <= 1; s += 2)
            box({s * 0.27f, 1.44f, 0}, {0.20f, 0.12f, 0.24f}, jacket, s < 0 ? B_LUPARM : B_RUPARM);

    // ---- Hair ----
    if (hasHair) {
        switch (hairStyle) {
            case 0:  // short
                box({0, 1.80f, -0.01f}, {0.33f, 0.10f, 0.32f}, hair, B_HEAD);
                box({0, 1.70f, -0.14f}, {0.33f, 0.22f, 0.06f}, hair, B_HEAD);
                box({0, 1.74f, 0.14f},  {0.33f, 0.07f, 0.06f}, hair, B_HEAD); break;
            case 1:  // long
                box({0, 1.80f, -0.01f}, {0.33f, 0.10f, 0.32f}, hair, B_HEAD);
                box({0, 1.50f, -0.15f}, {0.34f, 0.50f, 0.07f}, hair, B_HEAD);
                for (int s = -1; s <= 1; s += 2) box({s * 0.165f, 1.58f, 0}, {0.05f, 0.34f, 0.30f}, hair, B_HEAD); break;
            case 2:  // mohawk
                for (int i = 0; i < 4; ++i) box({0, 1.84f, -0.12f + i * 0.08f}, {0.06f, 0.16f - i * 0.015f, 0.07f}, hair, B_HEAD); break;
            case 3:  // bun
                box({0, 1.80f, -0.01f}, {0.33f, 0.10f, 0.32f}, hair, B_HEAD);
                box({0, 1.86f, -0.14f}, {0.15f, 0.16f, 0.15f}, hair, B_HEAD); break;
            case 4:  // spiky
                box({0, 1.78f, 0}, {0.33f, 0.08f, 0.32f}, hair, B_HEAD);
                for (int sx = -1; sx <= 1; ++sx) for (int sz = -1; sz <= 1; sz += 2)
                    box({sx * 0.10f, 1.88f, sz * 0.09f}, {0.07f, 0.14f, 0.07f}, hair, B_HEAD); break;
            case 5:  // afro
                box({0, 1.82f, 0}, {0.46f, 0.34f, 0.46f}, hair, B_HEAD);
                box({0, 1.66f, -0.16f}, {0.40f, 0.34f, 0.14f}, hair, B_HEAD); break;
            case 6:  // ponytail
                box({0, 1.80f, -0.01f}, {0.33f, 0.10f, 0.32f}, hair, B_HEAD);
                box({0, 1.70f, -0.14f}, {0.30f, 0.20f, 0.07f}, hair, B_HEAD);
                box({0, 1.52f, -0.20f}, {0.12f, 0.42f, 0.12f}, hair, B_HEAD); break;
            default: // 7 buzz
                box({0, 1.79f, 0}, {0.32f, 0.06f, 0.31f}, hair, B_HEAD);
                box({0, 1.71f, -0.13f}, {0.32f, 0.18f, 0.05f}, hair, B_HEAD); break;
        }
    }

    // ---- Facial hair ----
    if (beardStyle == 1)      box({0, 1.54f, 0.10f}, {0.30f, 0.16f, 0.12f}, hair, B_HEAD);     // full
    else if (beardStyle == 2) box({0, 1.52f, 0.14f}, {0.10f, 0.12f, 0.06f}, hair, B_HEAD);     // goatee
    else if (beardStyle == 3) box({0, 1.585f, 0.16f}, {0.16f, 0.04f, 0.04f}, hair, B_HEAD);    // mustache

    // ---- Headgear ----
    if (hasHat) {
        switch (hatStyle) {
            case 0:  // cap
                box({0, 1.83f, 0}, {0.34f, 0.10f, 0.32f}, hat, B_HEAD);
                box({0, 1.82f, 0.22f}, {0.30f, 0.04f, 0.18f}, hat, B_HEAD); break;
            case 1:  // helmet
                box({0, 1.80f, 0}, {0.38f, 0.30f, 0.36f}, hat, B_HEAD); break;
            case 2:  // top hat
                box({0, 1.84f, 0}, {0.50f, 0.04f, 0.50f}, hat, B_HEAD);
                box({0, 2.00f, 0}, {0.30f, 0.30f, 0.30f}, hat, B_HEAD); break;
            case 3:  // wizard
                box({0, 1.84f, 0}, {0.46f, 0.04f, 0.46f}, hat, B_HEAD);
                add(Mesh::Cone(0.5f, 1.0f, 10), {0, 2.06f, 0}, {0.40f, 0.46f, 0.40f}, hat, B_HEAD); break;
            case 4:  // beanie
                box({0, 1.82f, 0}, {0.36f, 0.22f, 0.34f}, hat, B_HEAD); break;
            case 5:  // cowboy
                box({0, 1.83f, 0}, {0.58f, 0.04f, 0.48f}, hat, B_HEAD);
                box({0, 1.92f, 0}, {0.30f, 0.18f, 0.30f}, hat, B_HEAD); break;
            case 6:  // crown
                box({0, 1.83f, 0}, {0.34f, 0.10f, 0.32f}, hat, B_HEAD);
                for (int sx = -1; sx <= 1; ++sx) for (int sz = -1; sz <= 1; sz += 2)
                    box({sx * 0.13f, 1.91f, sz * 0.13f}, {0.05f, 0.08f, 0.05f}, hat, B_HEAD); break;
            default: // 7 bandana
                box({0, 1.77f, 0.02f}, {0.34f, 0.09f, 0.33f}, hat, B_HEAD); break;
        }
    }

    // ---- Glasses / mask ----
    if (glassesStyle == 1) {
        Color gl = Color::FromBytes(28, 28, 34);
        for (int s = -1; s <= 1; s += 2) box({s * 0.07f, 1.64f, 0.17f}, {0.08f, 0.07f, 0.02f}, gl, B_HEAD);
        box({0, 1.64f, 0.17f}, {0.05f, 0.02f, 0.02f}, gl, B_HEAD);
    } else if (glassesStyle == 2) {
        Color gl = Color::FromBytes(15, 15, 18);
        box({0, 1.64f, 0.17f}, {0.30f, 0.08f, 0.03f}, gl, B_HEAD);
    }
    if (hasMask) box({0, 1.55f, 0.13f}, {0.30f, 0.18f, 0.08f}, maskCol, B_HEAD);
    if (hasScarf) box({0, 1.50f, 0.0f}, {0.24f, 0.12f, 0.24f}, mul(shirt, 0.8f), B_TORSO);

    // ---- Back accessories ----
    if (hasBackpack) box({0, 1.22f, -0.21f}, {0.34f, 0.42f, 0.16f}, pack, B_TORSO);
    if (hasCape)     box({0, 1.10f, -0.18f}, {0.42f, 0.74f, 0.04f}, mul(pack, 1.1f), B_TORSO);

    return m;
}

void Character::EnsureRest() const {
    if (m_built) return;
    if (m_hasCustomBind) {
        m_rest = m_customBind;
        AssignBonesForMesh(m_rest, m_bone);   // auto-rig: nearest bone segment
    } else {
        m_rest = BuildRig(m_bone);
    }
    m_restPos = m_rest.vertices;
    m_built = true;
}

// Auto-rig: attach every vertex to its nearest bone SEGMENT (each bone's joint
// toward its child's joint; end bones extrapolate — head up, hands along the
// arm, feet to the ground). Rotating a bone then carries exactly the vertices
// that hug that part of the body — the same one-bone-per-vertex skinning the
// blocky body uses, computed geometrically instead of authored.
void Character::AssignBonesForMesh(const Mesh& m, std::vector<int>& bone) const {
    std::vector<Bone> bones = Skeleton();
    std::vector<Vec3> tip(B_COUNT);
    for (int i = 0; i < B_COUNT; ++i) {
        int child = -1;
        for (int j = 0; j < B_COUNT; ++j) if (bones[j].parent == i) { child = j; break; }
        if (child >= 0) tip[i] = bones[child].joint;
        else if (i == B_HEAD) tip[i] = bones[i].joint + Vec3{0.0f, 0.32f, 0.0f};
        else if (i == B_LFOOT || i == B_RFOOT) tip[i] = Vec3{bones[i].joint.x, 0.0f, bones[i].joint.z + 0.12f};
        else {   // hands: continue the forearm's direction
            Vec3 d = bones[i].joint - bones[bones[i].parent].joint;
            tip[i] = bones[i].joint + d * 0.8f;
        }
    }
    auto segDist = [](const Vec3& p, const Vec3& a, const Vec3& b) {
        Vec3 ab = b - a;
        float len2 = Vec3::Dot(ab, ab);
        float t = len2 > 1e-12f ? Vec3::Dot(p - a, ab) / len2 : 0.0f;
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        Vec3 q = a + ab * t;
        return (p - q).Magnitude();
    };
    bone.assign(m.vertices.size(), B_HIPS);
    for (std::size_t k = 0; k < m.vertices.size(); ++k) {
        float best = 1e30f;
        for (int i = 0; i < B_COUNT; ++i) {
            float d = segDist(m.vertices[k], bones[i].joint, tip[i]);
            if (d < best) { best = d; bone[k] = i; }
        }
    }
}

void Character::BindCustomMesh(const Mesh& m) {
    m_customBind = m;
    // Normalize into the bind frame the skeleton lives in: feet on y=0, height
    // exactly 1.8 (the `height` property rescales at display time), centered on
    // X/Z. Idempotent, so a saved (already-normalized) bind reloads unchanged.
    Vec3 lo, hi; m_customBind.Bounds(lo, hi);
    float h = hi.y - lo.y;
    if (h > 1e-4f) {
        float s = 1.8f / h;
        Vec3 c{(lo.x + hi.x) * 0.5f, lo.y, (lo.z + hi.z) * 0.5f};
        for (Vec3& v : m_customBind.vertices)
            v = Vec3{(v.x - c.x) * s, (v.y - c.y) * s, (v.z - c.z) * s};
    }
    m_customBind.normals.clear();     // skinned rebuild face-shades (like the body)
    m_hasCustomBind = true;
    m_built = false;
    separateParts = false;            // the custom mesh animates via the single-mesh path
}

void Character::ClearCustomBind() {
    m_hasCustomBind = false;
    m_customBind = Mesh();
    m_built = false;
}

// ---- Procedural locomotion -------------------------------------------------
namespace {
constexpr float kRad2Deg = 57.2957795f;
float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
float frac(float v) { return v - std::floor(v); }
} // namespace

// The built-in motion's inputs for a state at clock time t: the stride's speed,
// phase and style, which way the feet step, the landing dip.
HumanoidMotion::Inputs Character::MotionInputs(int state, float t) const {
    HumanoidMotion::Inputs in;
    in.speed = m_gaitSpeed;
    in.phase = GaitPhaseAt(t, state);
    in.runW = RunWeight(state);
    in.travelYaw = m_travelYaw;
    in.lowerYaw = m_lowerYaw;
    in.landK = m_landK;
    return in;
}

// Cycles per second at a state's natural pace (used when no speed is reported).
static float NominalFreq(int state) { return HumanoidMotion::CycleFreq(state, -1.0f, state == 3 ? 1.0f : 0.0f); }

float Character::GaitPhase() const {
    if (anim != 2 && anim != 3 && anim != 17) return 0.0f;
    return frac(m_gaitPhase);
}

// The gait phase at clock time t: the live phase at animTime, extrapolated at the
// current cadence for any other time (editor previews sample around it).
float Character::GaitPhaseAt(float t, int state) const {
    float f = m_gaitFreq != 0.0f ? m_gaitFreq : NominalFreq(state);
    return frac(m_gaitPhase + (t - animTime) * f);
}

float Character::GaitCycleSeconds(int state) const {
    if (state != 2 && state != 3 && state != 17) return 0.0f;
    float f = m_gaitFreq != 0.0f ? m_gaitFreq : NominalFreq(state);   // the rate GaitPhaseAt uses
    f = f < 0.0f ? -f : f;
    return f > 1e-4f ? 1.0f / f : 0.0f;
}

// 0 = walking style, 1 = running style. The live state eases between them; any
// other state asked for (a crossfade source) uses its own style outright.
float Character::RunWeight(int state) const {
    if (state == anim && (state == 2 || state == 3)) return m_runW;
    return state == 3 ? 1.0f : 0.0f;
}

// Advance the built-in animation clocks. The gait phase runs at the cadence the
// reported ground speed calls for (backwards when the body backpedals), so a stride
// always covers exactly the ground the body moved, and a state change never moves it.
void Character::AdvanceAnimClock(float dt) {
    const bool gait = (anim == 2 || anim == 3 || anim == 17);
    float target = -1.0f;
    if (moveSpeed >= 0.0f) {
        float sc = height > 1e-3f ? height : 1.0f;   // a bigger body takes proportionally longer strides
        if (gameObject && gameObject->transform) {
            Vec3 ls = gameObject->transform->LossyScale();
            float s = 0.5f * (std::fabs(ls.x) + std::fabs(ls.z));
            if (s > 1e-3f) sc *= s;
        }
        target = moveSpeed / sc;
    }
    if (target < 0.0f)          m_gaitSpeed = -1.0f;
    else if (m_gaitSpeed < 0.0f) m_gaitSpeed = target;
    else                         m_gaitSpeed += (target - m_gaitSpeed) *
                                                (gaitSpeedSmoothing > 1e-4f ? 1.0f - std::exp(-dt / gaitSpeedSmoothing) : 1.0f);

    // Travel direction relative to the facing: the hips and legs turn toward it
    // (up to 75°, the chest stays forward); pointing behind, they step backwards.
    float a = std::fmod(moveAngle + 540.0f, 360.0f) - 180.0f;
    float limit = m_gaitDir < 0.0f ? 80.0f : 100.0f;            // hysteresis around the side
    m_gaitDir = std::fabs(a) > limit ? -1.0f : 1.0f;
    float yaw = m_gaitDir < 0.0f ? (a > 0.0f ? a - 180.0f : a + 180.0f) : a;
    if ((!gait && anim != 7) || m_gaitSpeed < 0.1f) yaw = 0.0f;   // the crawl goes every way too
    float ek = 1.0f - std::exp(-10.0f * dt);
    m_travelYaw += (yaw - m_travelYaw) * ek;                    // where the feet actually step
    m_lowerYaw  += (clampf(yaw, -75.0f, 75.0f) - m_lowerYaw) * ek;   // how far the hips turn

    // Walk <-> run blends the two styles over ~0.2 s on the live poses.
    float wantRun = anim == 3 ? 1.0f : 0.0f;
    float step = dt / 0.2f;
    m_runW = m_runW < wantRun ? std::fmin(wantRun, m_runW + step) : std::fmax(wantRun, m_runW - step);

    float rate = dt * animSpeed;
    animTime += rate;
    if (gait)           m_gaitFreq = HumanoidMotion::CycleFreq(anim, m_gaitSpeed, m_runW) * m_gaitDir;
    else if (anim == 7) m_gaitFreq = HumanoidMotion::CycleFreq(7, m_gaitSpeed, 0.0f);   // the crawl follows the distance
    else {
        m_gaitFreq = 0.0f;
    }
    m_gaitPhase = frac(m_gaitPhase + rate * m_gaitFreq);

    // The Animator's gait trees: the travel direction (at least as long as the
    // slowest stride clip, so the direction always picks the clips) and speed.
    if (m_poseSource) {
        const float v = m_gaitSpeed >= 0.0f ? m_gaitSpeed : HumanoidMotion::NaturalSpeed(anim);
        const float dir = (m_travelYaw + (m_gaitDir < 0.0f ? 180.0f : 0.0f)) / kRad2Deg;
        const float r = std::fmax(v, 0.3f);
        m_poseSource->Gait(r * std::sin(dir), r * std::cos(dir), v);
    }
}

void Character::DriveLocomotion(const Locomotion& l) {
    // Measure what the body really did: a controller sets the velocity it WANTS
    // before physics runs, so pressed against a wall the velocity still says
    // "moving" while the body stands still. The position change is the truth. It
    // is taken over the last ~0.08 s of frames, so it doesn't matter whether the
    // move landed this frame (a controller moving the transform) or last frame (a
    // physics step), and an uneven frame rate doesn't make the stride shimmer. A
    // teleport/respawn jump restarts the measurement, and a one-frame spike (a
    // hitch, a depenetration push) is set aside unless the next frame agrees.
    Vec3 vel = l.velocity;
    float vyNow = l.velocity.y;   // vertical speed right now (the window lags a takeoff)
    if (gameObject && gameObject->transform && l.dt > 1e-5f) {
        Vec3 pos = gameObject->transform->Position();
        const float al = l.ignoreAxis.Magnitude();
        auto flat = [&](Vec3 d) {
            if (al > 1e-5f) { Vec3 a = l.ignoreAxis * (1.0f / al); d = d - a * Vec3::Dot(d, a); }
            return d;
        };
        m_moveClock += l.dt;
        if (m_histN > 0 && flat(pos - m_hist[m_histN - 1].p).Magnitude() > 2.0f) m_histN = 0;
        if (m_histN > 0) {
            float last = m_moveClock - m_hist[m_histN - 1].t;
            if (last > 1e-5f) vyNow = std::max(vyNow, (pos.y - m_hist[m_histN - 1].p.y) / last);
            Vec3 d = flat(pos - m_hist[0].p);
            float span = m_moveClock - m_hist[0].t;
            if (span > 1e-5f) vel = d * (1.0f / span);
        }
        // Keep one sample at least 0.08 s old (the window's start) and the newer ones.
        while (m_histN > 1 && m_moveClock - m_hist[1].t >= 0.08f) {
            for (int i = 1; i < m_histN; ++i) m_hist[i - 1] = m_hist[i];
            --m_histN;
        }
        if (m_histN == kMoveHist) { for (int i = 1; i < m_histN; ++i) m_hist[i - 1] = m_hist[i]; --m_histN; }
        m_hist[m_histN++] = {pos, m_moveClock};
    }
    // On the ground: a fresh contact, or solid ground just under the feet (contact
    // callbacks stop for a body at rest, and stepping off a curb shouldn't count).
    bool ground = l.onGround;
    if (!ground && !l.jumped && gameObject && gameObject->transform && gameObject->scene()) {
        Scene* sc = gameObject->scene();
        Vec3 o = gameObject->transform->Position();
        RaycastHit3D h = sc->physics3D().Raycast(*sc, o + Vec3{0.0f, 0.15f, 0.0f}, Vec3{0.0f, -1.0f, 0.0f}, 0.45f,
                                                 gameObject, Physics3D::kAllLayers, false);
        ground = h.hit && vyNow < 1.0f;
    }
    Vec3 v{vel.x, 0.0f, vel.z};
    float hs = std::sqrt(v.x * v.x + v.z * v.z);
    const float jumpAt = 2.0f * m_measured + 2.0f;   // what the controller didn't ask for, for one frame
    if (hs > jumpAt && std::sqrt(l.velocity.x * l.velocity.x + l.velocity.z * l.velocity.z) < jumpAt && !m_spike) {
        m_spike = true;
        const float k = hs > 1e-5f ? m_measured / hs : 0.0f;
        v = v * k; hs = m_measured;
    } else {
        m_spike = false;
        m_measured = hs;
    }
    moveSpeed = hs;
    Vec3 f{l.facing.x, 0.0f, l.facing.z};
    float fl = std::sqrt(f.x * f.x + f.z * f.z);
    if (fl > 1e-4f && hs > 0.05f) {
        f = f * (1.0f / fl);
        Vec3 right{-f.z, 0.0f, f.x};                          // the body's right
        moveAngle = std::atan2(v.x * right.x + v.z * right.z, v.x * f.x + v.z * f.z) * kRad2Deg;
    }
    m_airT = (ground && !l.jumped) ? 0.0f : m_airT + l.dt;
    if (l.jumped) m_airT = 1.0f;                               // leave the ground pose at once
    const float runAt = l.walkSpeed * (anim == 3 ? 1.05f : 1.15f);
    const bool run = hs > runAt || (l.sprint && hs > l.walkSpeed * 0.9f);
    m_stanceTarget = l.prone ? 2 : (l.crouch ? 1 : 0);
    m_stanceLevel = l.stance >= 0.0f ? l.stance : (float)m_stanceTarget;
    if (m_poseSource && m_poseSource->Live()) {   // the Animator's graph picks the state from these
        PoseSource::Movement m;
        m.speed = hs; m.run = run; m.crouch = l.crouch; m.prone = l.prone;
        m.stance = m_stanceLevel;
        m.airTime = m_airT; m.verticalSpeed = l.velocity.y;
        m_poseSource->Move(m);
        return;
    }
    const bool wasMoving = anim == 2 || anim == 3 || anim == 17;
    const bool moving = hs > (wasMoving ? 0.2f : 0.35f);       // hysteresis: no idle/walk flicker
    if (l.prone)                   anim = 7;
    else if (l.jumped)             anim = 5;
    else if (m_airT > 0.12f)       anim = l.velocity.y > 0.4f ? 5 : 18;   // short drops keep the stride
    else if (l.crouch)             anim = moving ? 17 : 6;
    else if (!moving)              anim = 1;
    else                           anim = run ? 3 : 2;
}

std::vector<Vec3> Character::PoseAt(float t) const { return PoseFor(anim, t); }

std::vector<Vec3> Character::PoseFor(int state, float t) const {
    std::vector<Vec3> r(B_COUNT, Vec3{0, 0, 0});
    if (state == 0) {
        for (int i = 0; i < B_COUNT && i < (int)pose.size(); ++i) r[i] = pose[i];
        return r;
    }
    HumanoidMotion::Pose(state, t, MotionInputs(state, t), r);
    ApplyLookAndLean(r);
    return r;
}

// Head look: layer the (eased) gaze on top of whatever the animation set, so the
// head turns and tilts toward where the player is looking. Clamped so the neck
// never breaks. (The body is flipped 180° about Y in Apply(), which negates the
// pitch sense — hence the minus on X.)
void Character::ApplyLookAndLean(std::vector<Vec3>& r) const {
    r[B_HEAD].x += -Mathf::Clamp(m_headPitch, -55.0f, 55.0f);
    r[B_HEAD].y +=  Mathf::Clamp(m_headYaw,   -72.0f, 72.0f);
    // Body lean (peek): roll the torso sideways toward the lean direction.
    // Splitting a little onto the hips makes it read as a whole-body lean rather
    // than just a bent waist. (bodyLean > 0 = lean to the player's right, e.g. the
    // 'E' key; this rolls the body that way to match.)
    float lean = Mathf::Clamp(m_bodyLean, -40.0f, 40.0f);
    r[B_TORSO].z += lean * 0.7f;
    r[B_HIPS].z  += lean * 0.3f;
}

bool Character::AnimatorSample(std::vector<Vec3>& pose, Vec3& root) const {
    if (!AnimatorDriven() || !m_poseSource->AnimatorPose(pose, root)) return false;
    pose.resize(B_COUNT, Vec3{0, 0, 0});
    // Landing on the move: the stride gives at the knees (a standing landing
    // plays the Land state instead).
    if (m_landK > 0.0f && (anim == 2 || anim == 3)) root.y += HumanoidMotion::AbsorbLanding(pose, m_landK);
    return true;
}

Vec3 Character::StanceOffset() const {
    if (m_editorPosing) return m_editorOffset;        // a previewed pose carries its own
    {   // the Animator's clips carry the body's offset in their root track
        std::vector<Vec3> p; Vec3 root;
        if (AnimatorSample(p, root)) return root * height;
    }
    Vec3 o = RawStanceOffset(animTime);
    if (m_blendT < 1.0f && (m_blendFromAnim >= 0 || !m_blendFrom.empty())) {   // ease the hip height with the pose crossfade
        float w = m_blendT * m_blendT * (3.0f - 2.0f * m_blendT);
        float from = m_blendFromAnim >= 0 ? RawStanceOffsetFor(m_blendFromAnim, animTime).y : m_blendFromY;
        o.y = from + (o.y - from) * w;
    }
    return o;
}

Vec3 Character::RawStanceOffset(float t) const { return RawStanceOffsetFor(anim, t); }

Vec3 Character::RawStanceOffsetFor(int state, float t) const {
    return {0.0f, HumanoidMotion::HipY(state, MotionInputs(state, t)) * height, 0.0f};   // rig units scale with height
}

void Character::Skin(Mesh& m, const std::vector<int>& bone, const std::vector<Vec3>& rot) const {
    std::vector<Bone> bones = Skeleton();
    int n = (int)bones.size();
    std::vector<Mat4> restW(n), poseW(n), skin(n);
    for (int i = 0; i < n; ++i) {
        Vec3 jp = bones[i].parent >= 0 ? bones[bones[i].parent].joint : Vec3{0, 0, 0};
        Vec3 local = bones[i].joint - jp;
        Mat4 rest = Mat4::Translate(local);
        Mat4 posed = Mat4::Translate(local) * Mat4::Rotate(Quat::Euler(i < (int)rot.size() ? rot[i] : Vec3{0, 0, 0}));
        if (bones[i].parent >= 0) { restW[i] = restW[bones[i].parent] * rest; poseW[i] = poseW[bones[i].parent] * posed; }
        else { restW[i] = rest; poseW[i] = posed; }
    }
    for (int i = 0; i < n; ++i) skin[i] = poseW[i] * restW[i].Inverse();
    for (std::size_t k = 0; k < m.vertices.size() && k < bone.size() && k < m_restPos.size(); ++k)
        m.vertices[k] = skin[bone[k]].MultiplyPoint(m_restPos[k]);
}

void Character::Apply() {
    if (!gameObject) return;
    // External-rig mode renders nothing of its own — keep the baked mesh off
    // (Start() calls Apply, which would otherwise flash the blocky body).
    if (driveExternal) {
        if (auto* emr = gameObject->GetComponent<MeshRenderer>()) emr->enabled = false;
        return;
    }
    auto* mr = gameObject->GetComponent<MeshRenderer>();
    if (!mr) mr = gameObject->AddComponent<MeshRenderer>();
    m_built = false; EnsureRest();
    Mesh m = m_rest;
    Skin(m, m_bone, PoseAt(animTime));
    // Face -Z: the engine's cameras look down -Z and the controllers move along
    // -Z, so the body (modelled facing +Z) is turned 180° about Y to match — this
    // is what stops the walk looking backwards (moonwalk). StanceOffset lowers the
    // whole body for crouch / prone (the rig's root is otherwise pinned at the hip).
    Vec3 so = StanceOffset();
    FaceMinusZ(m);
    for (Vec3& v : m.vertices) { v = v * height; v += so; }
    m.normals.clear();                     // boxy -> flat shading
    mr->mesh = std::move(m);
    mr->doubleSided = false;               // outward faces only: no view into the cavity
}

void Character::AddClip(AnimClip clip) {
    std::string name = clip.name;
    m_clips[name] = std::move(clip);
    ++m_clipsVer;
    if (m_activeClipName == name) m_activeClip = &m_clips[name];  // keep pointer valid on replace
}

int Character::LoadClips(const std::string& text, std::string* error, bool replaceExisting) {
    auto clips = AnimClip::ParseAll(text, &Character::BoneIndex, B_COUNT, error);
    for (auto& c : clips)
        if (replaceExisting || !HasClip(c.name)) AddClip(std::move(c));
    return (int)clips.size();
}

bool Character::LoadClipsFromFile(const std::string& path, std::string* error, bool replaceExisting) {
    std::ifstream f(path);
    if (!f) { if (error) *error = "cannot open " + path; return false; }
    std::stringstream ss; ss << f.rdbuf();
    return LoadClips(ss.str(), error, replaceExisting) > 0;
}

static std::string& ClipsBaseDir() { static std::string d; return d; }
void Character::SetAssetBaseDir(const std::string& dir) { ClipsBaseDir() = dir; }
const std::string& Character::AssetBaseDir() { return ClipsBaseDir(); }

std::string Character::ResolvedClipsFile() const {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path p(clipsFile);
    if (clipsFile.empty() || p.is_absolute() || ClipsBaseDir().empty() || fs::exists(p, ec)) return clipsFile;
    return (fs::path(ClipsBaseDir()) / p).string();
}

bool Character::SaveClipsToFile(const std::string& path, std::string* error) const {
    std::vector<AnimClip> clips;
    clips.reserve(m_clips.size());
    for (const auto& kv : m_clips) clips.push_back(kv.second);
    std::string text = AnimClip::WriteAll(clips, [](int i) { return std::string(BoneToken(i)); });
    std::ofstream f(path, std::ios::trunc);
    if (!f) { if (error) *error = "cannot write " + path; return false; }
    f << text;
    return (bool)f;
}

bool Character::PlayClip(const std::string& name) {
    if (name.empty()) { StopClip(); return true; }
    auto it = m_clips.find(name);
    if (it == m_clips.end()) return false;
    if (m_activeClipName != name) BeginBlend();   // crossfade from the current pose
    m_activeClip = &it->second;
    m_activeClipName = name;
    m_clipTime = 0.0f;
    return true;
}

void Character::StopClip() {
    if (m_activeClip) BeginBlend();               // crossfade back to the built-in animation
    m_activeClip = nullptr;
    m_activeClipName.clear();
    m_clipTime = 0.0f;
}

void Character::BeginBlend() {
    if (blendTime <= 1e-4f) { m_blendT = 1.0f; return; }   // instant
    m_blendFrom = CurrentSourcePose();                     // freeze where we are right now
    m_blendFromY = RawStanceOffset(animTime).y;
    m_blendFromAnim = -1;
    m_blendT = 0.0f;
}

void Character::FireClipEvents(float from, float to) {
    if (!m_activeClip || m_activeClip->events.empty() || to <= from) return;
    auto inRange = [&](float a, float b) {
        for (const auto& e : m_activeClip->events)
            if (e.time > a && e.time <= b && !e.name.empty()) {
                m_animEvents.push_back(e.name);
                if (onAnimEvent) onAnimEvent(e.name);
            }
    };
    float dur = m_activeClip->Duration();
    if (m_activeClip->loop && dur > 0.0f && to > dur) {
        inRange(from, dur);                       // up to the loop point
        float wrapped = std::fmod(to, dur);
        inRange(-1e-6f, wrapped);                 // and from the start after wrapping
    } else {
        inRange(from, to);
    }
}

void Character::AdvanceClip(float dt) {
    if (!m_activeClip) return;
    float from = m_clipTime;
    m_clipTime += dt * animSpeed * m_activeClip->speed;   // per-clip playback rate
    FireClipEvents(from, m_clipTime);             // events on the raw (pre-wrap) timeline
    float dur = m_activeClip->Duration();
    if (dur > 0.0f) {
        if (m_activeClip->loop) m_clipTime = std::fmod(m_clipTime, dur);
        else if (m_clipTime > dur) m_clipTime = dur;
    }
}

std::vector<std::string> Character::ClipNames() const {
    std::vector<std::string> names;
    names.reserve(m_clips.size());
    for (const auto& kv : m_clips) names.push_back(kv.first);
    std::sort(names.begin(), names.end());
    return names;
}

void Character::SyncStateClips() {
    if (m_poseSource) return;     // the Animator's states pick the clips
    if (m_blendTreeOn) return;    // the locomotion blend tree owns the base pose
    if (m_editorPosing) return;   // editor preview owns the pose
    // A manual clip (PlayClip / autoPlayClip) gives way to movement: a looping one
    // stops as soon as the character starts moving or jumps (the state changes into
    // walk/run/jump/fall while it plays), a one-shot once it has finished, so an
    // authored flourish never freezes the player in place. A script that plays a
    // clip while already running keeps it until the state changes. A clip flagged
    // overrideMovement keeps the body until it's stopped explicitly.
    const bool moving = anim == 2 || anim == 3 || anim == 5 || anim == 17 || anim == 18;
    const bool stateChanged = m_syncAnim >= 0 && anim != m_syncAnim;
    m_syncAnim = anim;
    if (m_activeClip && !m_stateDriven) {
        if (m_activeClip->overrideMovement) return;
        const bool finished = !m_activeClip->loop && m_clipTime >= m_activeClip->Duration();
        if (moving && (stateChanged || finished)) StopClip();
    }
    // No-code: when the user has bound a clip to a movement state, make that clip the
    // active one (so YOUR animation plays instead of the built-in for that state).
    const std::string* want = nullptr;
    switch (anim) {
        case 1:  want = &clipIdle; break;
        case 2:  want = &clipWalk; break;
        case 3:  want = &clipRun; break;
        case 5:  want = &clipJump; break;
        case 18: want = &clipFall; break;
        case 6:  want = &clipCrouch; break;
        default: break;
    }
    if (want && want->empty()) want = nullptr;
    if (want) {
        if (m_activeClipName != *want && m_clips.count(*want)) { PlayClip(*want); m_stateDriven = true; }
        else if (m_activeClipName == *want) m_stateDriven = true;
    } else if (m_stateDriven && m_activeClip) {
        // Entered a state with no binding: hand back to the built-in animation. (Only
        // when WE put the clip there — never stomp a manual PlayClip / autoPlayClip.)
        StopClip();
        m_stateDriven = false;
    }
}

void Character::NoteStateChange() {
    if (m_animHold > 0) anim = m_animHold;   // a script's held animation wins over the controller's pick
    // Landing recovery: coming out of the jump pose into any grounded state plays a
    // brief knee-bend "absorb" (sin arc in PoseAt/StanceOffset) so a landing has
    // weight to it instead of the legs snapping straight the frame you touch down.
    if (anim != m_prevAnim) {
        if ((m_prevAnim == 5 || m_prevAnim == 18) && (anim == 1 || anim == 2 || anim == 3)) m_landT = 0.0f;
        auto isGait = [](int a) { return a == 2 || a == 3 || a == 17; };
        if (isGait(anim) && !isGait(m_prevAnim)) {
            // Stepping off: a landing comes down on the left foot (phase 0); from a
            // stand, start with the left foot planted under the hips and the right
            // one swinging through.
            m_runW = anim == 3 ? 1.0f : 0.0f;
            m_gaitPhase = (m_prevAnim == 5 || m_prevAnim == 18) ? 0.0f
                        : HumanoidMotion::StartPhase(anim, m_gaitSpeed);
        }
        // Crossfade between the built-in animations (idle -> walk -> crouch ...) from
        // the state we're leaving, still playing live, so neither side freezes. Walk
        // <-> run needs none: the stride is continuous and its style blends itself.
        bool walkRun = (anim == 2 || anim == 3) && (m_prevAnim == 2 || m_prevAnim == 3);
        if (!m_activeClip && !m_blendTreeOn && !m_editorPosing && blendTime > 1e-4f &&
            m_prevAnim != 0 && anim != 0 && !walkRun) {
            m_blendFromAnim = m_prevAnim;
            m_blendT = 0.0f;
        }
        m_prevAnim = anim;
    }
}

void Character::Update(float dt) {
    NoteStateChange();
    if (m_landT >= 0.0f) {
        m_landT += dt / 0.28f;                       // ~0.28s dip-and-spring
        if (m_landT >= 1.0f) { m_landT = -1.0f; m_landK = 0.0f; }
        else m_landK = std::sin(3.14159265f * m_landT);
    }
    SyncStateClips();
    // Advance any in-progress crossfade (covers every render path below).
    if (m_blendT < 1.0f) {
        m_blendT += (blendTime > 1e-4f ? dt / blendTime : 1.0f);
        if (m_blendT > 1.0f) m_blendT = 1.0f;
    }
    AdvanceLayer(dt);   // tick the partial-body layer clip (if any), every path below uses it
    AdvanceBlendTree(dt);   // tick the locomotion blend tree's shared clock
    // External-rig mode: a HumanoidRetarget next to us transfers CurrentPose()
    // onto an imported skeleton each frame — advance every animation clock and
    // state here but render nothing ourselves (no baked mesh, no part rig).
    if (driveExternal) {
        if (auto* mr = gameObject ? gameObject->GetComponent<MeshRenderer>() : nullptr)
            mr->enabled = false;
        if (m_punchT >= 0.0f && m_punchT < 1.0f) {
            m_punchT += (punchDuration > 1e-3f ? dt / punchDuration : 1.0f);
            if (m_punchT > 1.0f) m_punchT = 1.0f;
        }
        if (m_activeClip) AdvanceClip(dt);
        else AdvanceAnimClock(dt);
        float ek = headTurnSpeed > 0.0f ? (1.0f - std::exp(-headTurnSpeed * dt)) : 1.0f;
        m_headYaw   += (lookYaw   - m_headYaw)   * ek;
        m_headPitch += (lookPitch - m_headPitch) * ek;
        m_bodyLean  += (bodyLean  - m_bodyLean)  * ek;
        return;
    }
    // Separate-parts rig: animate the part transforms instead of baking one mesh.
    if (separateParts) {
        if (!m_partsBuilt) BuildParts();
        if (m_partsBuilt) {
            // Keep the baked single mesh hidden EVERY frame while the rig drives the
            // body — otherwise the original character renders on top of the rig and you
            // see "two characters" (most obvious when leaning). (BuildParts disables it
            // once, but anything that re-enables it, e.g. a re-Apply, would bring it
            // back; this makes it stick.)
            if (auto* mr = gameObject ? gameObject->GetComponent<MeshRenderer>() : nullptr)
                mr->enabled = false;
            if (m_punchT >= 0.0f && m_punchT < 1.0f) {
                m_punchT += (punchDuration > 1e-3f ? dt / punchDuration : 1.0f);
                if (m_punchT > 1.0f) m_punchT = 1.0f;
            }
            // A playing clip drives the parts; otherwise the built-in animation.
            if (m_activeClip) AdvanceClip(dt);
            else AdvanceAnimClock(dt);
            // Ease the head turn / tilt and the body lean toward their targets, just
            // like the single-mesh path — otherwise a separated character never leans
            // or head-tracks ("the camera leans but not the player").
            float k = headTurnSpeed > 0.0f ? (1.0f - std::exp(-headTurnSpeed * dt)) : 1.0f;
            m_headYaw   += (lookYaw   - m_headYaw)   * k;
            m_headPitch += (lookPitch - m_headPitch) * k;
            m_bodyLean  += (bodyLean  - m_bodyLean)  * k;
            // (An Animator's pose is shown in LateUpdate.)
            if (!AnimatorDriven() && (animateParts || m_editorPosing || m_activeClip)) {
                EaseFirstPersonArm(dt);
                DriveParts();   // preview / clip still drive
            }
            return;
        }
    }
    // Editor preview (single mesh): show the forced pose.
    if (m_editorPosing) {
        auto* mr = gameObject ? gameObject->GetComponent<MeshRenderer>() : nullptr;
        if (mr) {
            EnsureRest();
            Mesh m = m_rest;
            std::vector<Vec3> pose = m_editorPose; pose.resize(B_COUNT, Vec3{0, 0, 0});
            Skin(m, m_bone, pose);
            FaceMinusZ(m);
            for (Vec3& v : m.vertices) { v = v * height; v += m_editorOffset; }
            m.normals.clear(); mr->mesh = std::move(m); mr->doubleSided = false;
        }
        return;
    }
    // A custom clip, if one is playing, drives the whole body and overrides the
    // built-in animations.
    if (m_activeClip) {
        AdvanceClip(dt);
        auto* mr = gameObject ? gameObject->GetComponent<MeshRenderer>() : nullptr;
        if (!mr) return;
        EnsureRest();
        Mesh m = m_rest;
        Skin(m, m_bone, CurrentSourcePose());   // crossfade-aware (blends into the clip)
        FaceMinusZ(m);
        for (Vec3& v : m.vertices) v = v * height;  // face -Z (see Apply)
        m.normals.clear();
        mr->mesh = std::move(m);
        mr->doubleSided = false;
        return;
    }
    // Advance the punch arc even when posing manually, so a punch always plays.
    if (m_punchT >= 0.0f && m_punchT < 1.0f) {
        m_punchT += (punchDuration > 1e-3f ? dt / punchDuration : 1.0f);
        if (m_punchT > 1.0f) m_punchT = 1.0f;
    }
    if (anim == 0 && !m_poseSource) return;
    AdvanceAnimClock(dt);

    // Auto look-at: drive lookYaw/lookPitch so the head aims at the main camera or a
    // named target. Same sign convention the controllers use: the head Euler-Y offset
    // is (body heading − target heading), and +pitch raises the gaze.
    if ((lookAtCamera || !lookAtTarget.empty()) && gameObject && gameObject->transform) {
        Scene* sc = gameObject->scene();
        Transform* tgt = nullptr;
        if (sc && !lookAtTarget.empty()) { if (GameObject* g = sc->Find(lookAtTarget)) tgt = g->transform; }
        else if (lookAtCamera && sc && sc->mainCamera) tgt = sc->mainCamera->transform;
        if (tgt) {
            Vec3 self = gameObject->transform->Position(); self.y += lookHeight;
            Vec3 dir = tgt->Position() - self;
            Vec3 fwd = gameObject->transform->localRotation * Vec3{0, 0, -1};
            float bodyYaw = Mathf::Atan2(fwd.x, -fwd.z) * Mathf::Rad2Deg;
            float aimYaw  = Mathf::Atan2(dir.x, -dir.z) * Mathf::Rad2Deg;
            float relY = bodyYaw - aimYaw;
            float horiz = std::sqrt(dir.x * dir.x + dir.z * dir.z);
            lookYaw   = NeckYaw(relY);   // ease back near ±180 instead of cranking sideways
            lookPitch = Mathf::Atan2(dir.y, horiz) * Mathf::Rad2Deg;
        }
    }

    // Ease the head toward the look target so it moves smoothly (frame-rate
    // independent) instead of snapping to the camera each frame.
    float k = headTurnSpeed > 0.0f ? (1.0f - std::exp(-headTurnSpeed * dt)) : 1.0f;
    m_headYaw   += (lookYaw   - m_headYaw)   * k;
    m_headPitch += (lookPitch - m_headPitch) * k;
    m_bodyLean  += (bodyLean  - m_bodyLean)  * k;
    if (!AnimatorDriven()) PresentMesh();   // an Animator's pose is shown in LateUpdate
}

void Character::LateUpdate(float dt) {
    if (driveExternal) { NoteStateChange(); return; }   // the HumanoidRetarget shows it
    if (!AnimatorDriven()) return;
    EaseFirstPersonArm(dt);
    if (separateParts && m_partsBuilt) { if (animateParts) DriveParts(); }
    else PresentMesh();
}

void Character::EmitAnimEvent(const std::string& name) {
    if (name.empty()) return;
    m_animEvents.push_back(name);
    if (onAnimEvent) onAnimEvent(name);
    if (!gameObject) return;
    const auto& cs = gameObject->Components();   // by index: a handler may add components
    for (std::size_t i = 0; i < cs.size(); ++i)
        if (cs[i].get() != this) cs[i]->OnAnimationEvent(name);
}

void Character::PresentMesh() {
    auto* mr = gameObject ? gameObject->GetComponent<MeshRenderer>() : nullptr;
    if (!mr) return;
    EnsureRest();
    Mesh m = m_rest;
    std::vector<Vec3> pose = CurrentSourcePose();   // built-in anim / Animator, crossfade-aware
    // Layer a punch arc over the right arm (swings forward and back), so the
    // character's own arm does the hitting on top of whatever it's already doing.
    if (m_punchT >= 0.0f && m_punchT < 1.0f && (int)pose.size() > B_RFORE) {
        float arc = std::sin(m_punchT * 3.14159265f);   // 0..1..0
        // Swing forward (-Z, the body's facing), so the punch lands in front.
        Vec3 up = {-110.0f, 0.0f, 8.0f}, fore = {18.0f, 0.0f, 0.0f};
        pose[B_RUPARM] = pose[B_RUPARM] + (up   - pose[B_RUPARM]) * arc;
        pose[B_RFORE]  = pose[B_RFORE]  + (fore - pose[B_RFORE])  * arc;
    }
    Skin(m, m_bone, pose);
    // The head is never sunk or scaled away. A first-person camera skips the
    // head part (and the neck) and leaves the mesh here for every other view.
    Vec3 so = StanceOffset();
    FaceMinusZ(m);
    for (Vec3& v : m.vertices) { v = v * height; v += so; }   // face -Z + stance drop (see Apply)
    m.normals.clear();
    if (firstPersonArm) { BuildFpArm(m); m_fpArmReady = true; } else m_fpArmReady = false;
    mr->mesh = std::move(m);
    mr->doubleSided = false;
}

// Copy just the arm bones out of the fully-skinned body mesh (per-face colours kept),
// so a separate first-person arm object can render the real arm geometry.
void Character::BuildFpArm(const Mesh& full) {
    m_fpArm.vertices.clear(); m_fpArm.triangles.clear();
    m_fpArm.triColors.clear(); m_fpArm.normals.clear(); m_fpArm.name.clear();
    const bool cols = full.HasFaceColors();
    auto isArm = [](int b) { return b == B_LUPARM || b == B_LFORE || b == B_LHAND; };
    int faces = (int)full.triangles.size() / 3;
    for (int f = 0; f < faces; ++f) {
        int a = full.triangles[f * 3], b = full.triangles[f * 3 + 1], c = full.triangles[f * 3 + 2];
        if (a >= (int)m_bone.size() || b >= (int)m_bone.size() || c >= (int)m_bone.size()) continue;
        if (!(isArm(m_bone[a]) && isArm(m_bone[b]) && isArm(m_bone[c]))) continue;
        for (int idx : {a, b, c}) {
            m_fpArm.triangles.push_back((int)m_fpArm.vertices.size());
            m_fpArm.vertices.push_back(full.vertices[idx]);
        }
        if (cols) m_fpArm.triColors.push_back(full.triColors[f]);
    }
}

// Build the character as a HIERARCHY of editable part objects (one MeshRenderer per
// bone), so it can be selected / recoloured / animated part-by-part instead of being
// one baked mesh. Geometry is re-centred to each joint; a "Rig" root applies the same
// 180° face flip + height the single mesh used.
GameObject* Character::FindRig() const {
    if (!gameObject || !gameObject->transform) return nullptr;
    for (Transform* c : gameObject->transform->Children())
        if (c && c->gameObject && c->gameObject->name == "Rig") return c->gameObject;
    return nullptr;
}

bool Character::AdoptParts(GameObject* rig) {
    if (!rig || !rig->transform) return false;
    m_rigRoot = rig;
    m_parts.assign(B_COUNT, nullptr);
    // Match each bone object by name (recurse — the rig is a hierarchy, not flat).
    std::function<void(Transform*)> walk = [&](Transform* t) {
        if (!t) return;
        for (Transform* c : t->Children()) {
            if (!c || !c->gameObject) continue;
            for (int bi = 0; bi < B_COUNT; ++bi)
                if (c->gameObject->name == BoneName(bi)) { m_parts[bi] = c->gameObject; break; }
            walk(c);
        }
    };
    walk(rig->transform);
    if (auto* mr = gameObject->GetComponent<MeshRenderer>()) mr->enabled = false;
    RememberPartRest();
    m_partsBuilt = true;
    for (GameObject* p : m_parts)
        if (p) if (auto* pmr = p->GetComponent<MeshRenderer>()) pmr->doubleSided = false;
    SplitNeckFromTorso();
    return true;
}

bool Character::HandsOnAHold() const {
    if (!gameObject) return false;
    auto* hold = gameObject->GetComponent<ProceduralObjectHolding>();
    return hold && hold->IsHolding();
}

void Character::SetOwnerArmActive(bool on) {
    if (m_ownerArm) m_ownerArm->active = on;
    if (m_ownerFore) m_ownerFore->active = on;
    if (m_ownerHand) m_ownerHand->active = on;
}

void Character::ReleaseOwnerArm() {
    if (m_fpHiddenHeld && !m_fpHiddenHeld->IsDestroyed() && m_fpHiddenHeld->ownerCameraSkip == gameObject)
        m_fpHiddenHeld->ownerCameraSkip = nullptr;
    m_fpHiddenHeld = nullptr;
    SetOwnerArmActive(false);
    if (m_ownerItem && !m_ownerItem->IsDestroyed())
        if (Scene* s = GetScene()) s->Destroy(m_ownerItem);
    m_ownerItem = nullptr;
    if (m_ownerArm && !m_ownerArm->IsDestroyed())
        if (Scene* s = GetScene()) s->Destroy(m_ownerArm);
    m_ownerArm = m_ownerFore = m_ownerHand = nullptr;
    m_ownerArmBase = -2;
}

void Character::ReleaseOwnerArm2() {
    if (m_ownerArm2 && !m_ownerArm2->IsDestroyed())
        if (Scene* s = GetScene()) s->Destroy(m_ownerArm2);
    m_ownerArm2 = m_ownerFore2 = m_ownerHand2 = nullptr;
    m_ownerArmBase2 = -2;
}

void Character::DeactivateOwnerArm2() {
    if (m_ownerArm2) m_ownerArm2->active = false;
    if (m_ownerFore2) m_ownerFore2->active = false;
    if (m_ownerHand2) m_ownerHand2->active = false;
}

namespace {
// The owner's first-person view is the character's own hand brick, not a
// made-up cube and not the sleeve. Scale stays inside the range the player
// tests allow. The brick's local +Y is up (the lit face) and local +Z is the
// long axis, pointed toward screen centre, where a held item sits.
constexpr float kFpHandScale = 0.55f;

void PaintFpHand(GameObject* hand, GameObject* srcPart, const FirstPersonHands::HandSide* side) {
    if (!hand) return;
    auto* mr = hand->GetComponent<MeshRenderer>();
    auto* src = srcPart ? srcPart->GetComponent<MeshRenderer>() : nullptr;
    if (!mr || !src) return;
    mr->mesh = src->mesh;
    mr->mesh.normals.clear();
    mr->color = src->color;
    mr->castShadows = false;
    mr->enabled = true;
    if (side && side->useColour) {
        mr->color = side->colour;
        const int faces = (int)mr->mesh.triangles.size() / 3;
        mr->mesh.triColors.assign((std::size_t)std::max(0, faces), side->colour);
        mr->emissive = Color{side->colour.r * 0.37f, side->colour.g * 0.37f, side->colour.b * 0.37f, 1.0f};
    } else {
        mr->emissive = Color::FromBytes(82, 58, 46);
    }
}

void PaintFpSleeve(GameObject* fore, const FirstPersonHands::HandSide* side) {
    if (!fore) return;
    auto* mr = fore->GetComponent<MeshRenderer>();
    if (!mr) return;
    if (!side || !side->sleeve) {
        mr->mesh = Mesh{};
        mr->enabled = false;
        return;
    }
    float L = side->sleeveLength;
    if (L < 0.04f) L = 0.04f;
    if (L > 0.8f) L = 0.8f;
    Mesh m;
    Mesh::AppendBox(m, {-0.055f, -0.055f, -0.02f - L}, {0.055f, 0.055f, -0.02f});
    mr->mesh = std::move(m);
    mr->mesh.normals.clear();
    mr->enabled = true;
    mr->castShadows = false;
    mr->doubleSided = false;
    mr->color = side->sleeveColour;
    mr->emissive = Color{side->sleeveColour.r * 0.37f, side->sleeveColour.g * 0.37f,
                         side->sleeveColour.b * 0.37f, 1.0f};
}

Vec3 FpHandScale(const FirstPersonHands::HandSide* side) {
    if (!side) return Vec3::One;
    return Vec3{side->scale.x * side->size.x, side->scale.y * side->size.y, side->scale.z * side->size.z}
         * side->uniformScale;
}

float FpFovScale(const FirstPersonHands* hands, const Camera* cam) {
    if (!hands || !cam || hands->handFov <= 1.0f || cam->fieldOfView <= 1.0f) return 1.0f;
    return std::tan(cam->fieldOfView * Mathf::Deg2Rad * 0.5f)
         / std::tan(hands->handFov * Mathf::Deg2Rad * 0.5f);
}
}

void Character::EnsureOwnerArm(int base) {
    if (m_ownerArm && m_ownerArm->IsDestroyed()) {
        m_ownerArm = m_ownerFore = m_ownerHand = nullptr;
        m_ownerArmBase = -2;
    }
    if (m_ownerArm && m_ownerFore && m_ownerHand && m_ownerArmBase == base) return;
    ReleaseOwnerArm();
    Scene* s = GetScene();
    if (!s || base < 0 || base + 2 >= (int)m_parts.size()) return;
    GameObject* src[3] = {m_parts[base], m_parts[base + 1], m_parts[base + 2]};
    MeshRenderer* mesh[3] = {};
    for (int i = 0; i < 3; ++i) {
        if (!src[i]) return;
        mesh[i] = src[i]->GetComponent<MeshRenderer>();
        if (!mesh[i] || mesh[i]->mesh.vertices.empty()) return;
    }
    const char* names[3] = {"FP_OwnerArm", "FP_OwnerFore", "FP_OwnerHand"};
    GameObject* made[3] = {};
    for (int i = 0; i < 3; ++i) {
        made[i] = s->CreateGameObject(names[i]);
        auto* mr = made[i]->AddComponent<MeshRenderer>();
        mr->mesh = mesh[i]->mesh;
        mr->doubleSided = false;
        mr->castShadows = false;
        made[i]->firstPersonOnly = true;
        made[i]->firstPersonViewmodel = true;
        made[i]->active = false;
    }
    made[1]->transform->SetParent(made[0]->transform, false);
    made[2]->transform->SetParent(made[1]->transform, false);
    for (int i = 0; i < 3; ++i) made[i]->transient = true;
    // The hand keeps the body's brick (one block, skin or glove). The arm and
    // the forearm stay in the hierarchy so the hand is still their grandchild,
    // but they draw nothing.
    auto* armMr = made[0]->GetComponent<MeshRenderer>();
    armMr->mesh = Mesh{};
    armMr->enabled = false;
    auto* foreMr = made[1]->GetComponent<MeshRenderer>();
    foreMr->mesh = Mesh{};
    foreMr->enabled = false;
    auto* handMr = made[2]->GetComponent<MeshRenderer>();
    handMr->mesh.normals.clear();
    handMr->castShadows = false;
    // The key light sits in front of the lens, so the face you see is in shadow.
    // A warm lift keeps that face skin-coloured; the shade on each side stays.
    handMr->emissive = Color::FromBytes(82, 58, 46);
    // Under the player, not the camera. A first-person-only object is drawn
    // only for the camera that is hiding its owner, and only when it is still
    // inside that owner's subtree. Parenting it to the camera would hide it
    // from the owner too.
    if (gameObject && gameObject->transform)
        made[0]->transform->SetParent(gameObject->transform, false);
    m_ownerArm = made[0];
    m_ownerFore = made[1];
    m_ownerHand = made[2];
    m_ownerArmBase = base;
}

void Character::EnsureOwnerArm2(int base) {
    if (m_ownerArm2 && m_ownerArm2->IsDestroyed()) {
        m_ownerArm2 = m_ownerFore2 = m_ownerHand2 = nullptr;
        m_ownerArmBase2 = -2;
    }
    if (m_ownerArm2 && m_ownerFore2 && m_ownerHand2 && m_ownerArmBase2 == base) return;
    ReleaseOwnerArm2();
    Scene* s = GetScene();
    if (!s || base < 0 || base + 2 >= (int)m_parts.size()) return;
    GameObject* src[3] = {m_parts[base], m_parts[base + 1], m_parts[base + 2]};
    MeshRenderer* mesh[3] = {};
    for (int i = 0; i < 3; ++i) {
        if (!src[i]) return;
        mesh[i] = src[i]->GetComponent<MeshRenderer>();
        if (!mesh[i] || mesh[i]->mesh.vertices.empty()) return;
    }
    const char* names[3] = {"FP_OwnerArm2", "FP_OwnerFore2", "FP_OwnerHand2"};
    GameObject* made[3] = {};
    for (int i = 0; i < 3; ++i) {
        made[i] = s->CreateGameObject(names[i]);
        auto* mr = made[i]->AddComponent<MeshRenderer>();
        mr->mesh = mesh[i]->mesh;
        mr->doubleSided = false;
        mr->castShadows = false;
        made[i]->firstPersonOnly = true;
        made[i]->firstPersonViewmodel = true;
        made[i]->active = false;
    }
    made[1]->transform->SetParent(made[0]->transform, false);
    made[2]->transform->SetParent(made[1]->transform, false);
    for (int i = 0; i < 3; ++i) made[i]->transient = true;
    auto* armMr = made[0]->GetComponent<MeshRenderer>();
    armMr->mesh = Mesh{};
    armMr->enabled = false;
    auto* foreMr = made[1]->GetComponent<MeshRenderer>();
    foreMr->mesh = Mesh{};
    foreMr->enabled = false;
    auto* handMr = made[2]->GetComponent<MeshRenderer>();
    handMr->mesh.normals.clear();
    handMr->castShadows = false;
    handMr->emissive = Color::FromBytes(82, 58, 46);
    if (gameObject && gameObject->transform)
        made[0]->transform->SetParent(gameObject->transform, false);
    m_ownerArm2 = made[0];
    m_ownerFore2 = made[1];
    m_ownerHand2 = made[2];
    m_ownerArmBase2 = base;
}

namespace {
// The camera that is allowed to draw this player's first-person-only arm.
// Prefer the one already hiding the player; otherwise the camera mounted on
// the body. A free orbit camera is not it — posing the arm there parks a
// giant sleeve out in the world.
Camera* OwnerCamera(GameObject* player) {
    if (!player || !player->scene()) return nullptr;
    Scene* sc = player->scene();
    Camera* child = nullptr;
    Camera* ignoring = nullptr;
    for (const auto& o : sc->Objects()) {
        if (!o || !o->active) continue;
        Camera* cam = o->GetComponent<Camera>();
        if (!cam || !cam->enabled || !cam->transform || !o->active) continue;
        if (cam->ignoreObject == player) ignoring = cam;
        if (!child && o.get() != player && o->IsSelfOrDescendantOf(player)) child = cam;
    }
    if (ignoring) return ignoring;
    return child;
}
}

void Character::RefreshOwnerArm() {
    if (!m_partsBuilt) return;
    PoseOwnerArm(CurrentSourcePose());
}

void Character::ApplyFirstPersonHandDrive(bool showLeftArm) {
    FirstPersonHands* h = FirstPersonHands::FindOn(gameObject);
    if (h && !h->enabled) h = nullptr;
    int primary = showLeftArm ? B_RUPARM : B_LUPARM;
    bool two = false;
    if (h) {
        primary = h->holdMode == FirstPersonHands::HoldMode::Left ? B_RUPARM : B_LUPARM;
        two = h->holdMode == FirstPersonHands::HoldMode::Two;
        if (!h->right.enabled && h->left.enabled) { primary = B_RUPARM; two = false; }
        else if (h->right.enabled && !h->left.enabled) { primary = B_LUPARM; two = false; }
        else if (!h->right.enabled && !h->left.enabled) {
            fpArmBase = primary;
            fpArmUp = 0.0f;
            m_fpSecond = false;
            return;
        }
    }
    bool holding = HandsOnAHold();
    if (holding) {
        auto* ph = gameObject->GetComponent<ProceduralObjectHolding>();
        GameObject* held = ph ? ph->Held() : nullptr;
        auto* ho = held ? held->GetComponent<HoldableObject>() : nullptr;
        if (ho && ho->WantsTwoHands()) {
            const bool rightOn = !h || h->right.enabled;
            const bool leftOn = !h || h->left.enabled;
            if (rightOn && leftOn) { two = true; primary = B_LUPARM; }
        }
    }
    const bool run = HideFirstPersonHands();
    const bool show = (holding || (h && h->showWhenEmpty)) && (!run || (h && h->showWhileRunning));
    fpArmBase = primary;
    fpArmUp = show ? 1.0f : 0.0f;
    m_fpSecond = two;
}

void Character::PoseOwnerArm(const std::vector<Vec3>&) {
    const int base = fpArmBase;
    const bool on = firstPersonArm && base >= 0 && base + 2 < (int)m_parts.size();
    auto clearHeld = [&]() {
        if (m_fpHiddenHeld && !m_fpHiddenHeld->IsDestroyed() && m_fpHiddenHeld->ownerCameraSkip == gameObject)
            m_fpHiddenHeld->ownerCameraSkip = nullptr;
        m_fpHiddenHeld = nullptr;
        if (m_ownerItem) m_ownerItem->active = false;
    };
    // A run eases the copy off the corner (m_fpShown). It is not popped off.
    if (!on || m_fpShown < 0.08f) {
        SetOwnerArmActive(false);
        DeactivateOwnerArm2();
        if (m_ownerItem) m_ownerItem->active = false;
        if (!on) clearHeld();
        return;
    }
    EnsureOwnerArm(base);
    if (!m_ownerArm || !m_ownerFore || !m_ownerHand || !m_ownerArm->transform) return;
    if (gameObject && gameObject->transform && m_ownerArm->transform->Parent() != gameObject->transform)
        m_ownerArm->transform->SetParent(gameObject->transform, false);
    Camera* cam = OwnerCamera(gameObject);
    if (!cam || !cam->transform || !gameObject || !gameObject->transform) {
        SetOwnerArmActive(false);
        DeactivateOwnerArm2();
        return;
    }
    const Quat playerRot = gameObject->transform->Rotation();
    const Quat camRot = cam->transform->Rotation();
    const Quat camLocal = (playerRot.Conjugate() * camRot).Normalized();
    const Vec3 camEuler = camLocal.ToEuler();
    const float viewPitch = camEuler.x;
    // followPitch writes fpPitch from the lens. A manual pitch stays put.
    const bool follow = std::fabs(fpPitch - viewPitch) < 8.0f;
    const Quat basis = follow ? camRot : (playerRot * Quat::Euler(fpPitch, camEuler.y, camEuler.z));

    // Mouse look lags: the arm swings a little against the turn, then catches up.
    // The camera itself is not moved.
    const Vec3 camWorld = camRot.ToEuler();
    if (!m_fpTrailInit) {
        m_fpPrevYaw = camWorld.y; m_fpPrevPitch = camWorld.x; m_fpTrailInit = true;
    }
    const float dy = Mathf::DeltaAngle(m_fpPrevYaw, camWorld.y);
    const float dp = camWorld.x - m_fpPrevPitch;
    m_fpPrevYaw = camWorld.y; m_fpPrevPitch = camWorld.x;
    const float dt = m_fpDt > 1e-4f ? m_fpDt : 0.016f;
    const float decay = std::exp(-dt * 9.0f);
    // Against the turn: a look to the right leaves the arm on the left for a
    // moment (it was already there in the world), then it catches the corner.
    m_fpTrailYaw = Mathf::Clamp(m_fpTrailYaw * decay + dy * 0.55f, -14.0f, 14.0f);
    m_fpTrailPitch = Mathf::Clamp(m_fpTrailPitch * decay - dp * 0.45f, -12.0f, 12.0f);

    // Screen right for the L-named bones (the rig's yaw puts them there).
    const float side = base >= B_RUPARM ? -1.0f : 1.0f;
    const float aspect = cam->viewportAspect > 0.05f ? cam->viewportAspect : (16.0f / 9.0f);
    const float vFov = cam->VerticalFovDegrees(aspect) * Mathf::Deg2Rad;
    const float tanV = std::tan(std::max(0.15f, vFov * 0.5f));
    const float tanH = tanV * aspect;
    const float near = std::max(cam->nearClip, 0.04f);
    const float depth = std::max(0.46f, near + 0.32f);

    // The body's hand brick. Local +Z (its long axis) points inward. Local +Y,
    // the broad face, points back at the lens and up so it takes the light.
    // No sleeve.
    float snap = 0.0f;
    if (m_punchT >= 0.0f && m_punchT < 1.0f) {
        if (m_punchT < 0.32f) snap = std::sin(m_punchT / 0.32f * 1.5707963f);
        else snap = std::sin((1.0f - (m_punchT - 0.32f) / 0.68f) * 1.5707963f);
    }
    const float slide = 1.0f - m_fpShown;
    const bool stepping = anim == 2 || anim == 7 || anim == 17
        || (anim == 6 && std::fabs(m_gaitSpeed) > 0.2f);
    float bobS = 0.0f, bobC = 0.0f;
    if (fpArmBob && !fpSteady && stepping) {
        bobS = std::sin(m_gaitPhase * 6.2831853f);
        bobC = std::cos(m_gaitPhase * 6.2831853f);
    }
    FirstPersonHands* fpHands = FirstPersonHands::FindOn(gameObject);
    if (fpHands && !fpHands->enabled) fpHands = nullptr;
    const FirstPersonHands::HandSide* fpSide = nullptr;
    if (fpHands) {
        bobS *= fpHands->bobAmount;
        bobC *= fpHands->bobAmount;
        snap *= fpHands->punchAmount;
        fpSide = base >= B_RUPARM ? &fpHands->left : &fpHands->right;
    }
    // Long axis toward screen centre. The broad face is turned up toward the
    // key light and only a little back at the lens: a face square to the lens
    // is in shadow, because the light sits in front of the camera.
    Vec3 inward{-side * 0.92f, -0.02f, -0.08f};
    inward.y += 0.10f * bobS;
    if (snap > 0.0f) {
        const Vec3 aim{-side * 0.18f, 0.88f, 0.12f};
        inward = inward + (aim - inward) * snap;
    }
    if (inward.SqrMagnitude() < 1e-8f) inward = Vec3{-side, 0.2f, 0.1f};
    inward = inward.Normalized();
    Vec3 topHint{side * 0.04f, 0.86f, 0.40f};
    Vec3 top = topHint - inward * Vec3::Dot(topHint, inward);
    if (top.SqrMagnitude() < 1e-6f) top = Vec3{0.0f, 1.0f, 0.0f};
    top = top.Normalized();
    Quat camSpace = Quat::LookRotation(inward, top);
    camSpace = Quat::Euler(1.6f * bobS, -side * 2.2f * bobC, side * 1.1f * bobS) * camSpace;
    if (fpSide && !m_fpSecond) camSpace = Quat::Euler(fpSide->rotation) * camSpace;
    m_ownerFore->transform->localPosition = Vec3::Zero;
    m_ownerFore->transform->localRotation = Quat::Identity;
    m_ownerFore->transform->localScale = Vec3::One;
    m_ownerHand->transform->localPosition = Vec3::Zero;
    m_ownerHand->transform->localRotation = Quat::Identity;
    m_ownerHand->transform->localScale = (!m_fpSecond) ? FpHandScale(fpSide) : Vec3::One;
    if (!m_fpSecond && base + 2 < (int)m_parts.size()) {
        PaintFpHand(m_ownerHand, m_parts[base + 2], fpSide);
        PaintFpSleeve(m_ownerFore, fpSide);
    }
    const float fovScale = FpFovScale(fpHands, cam);
    m_ownerArm->transform->localScale = Vec3{kFpHandScale * fovScale, kFpHandScale * fovScale, kFpHandScale * fovScale};
    // Measure in the camera, then correct. One shift is not enough: a nearer
    // corner projects further out than the vertex that was extreme before the move.
    m_ownerArm->transform->SetRotation(cam->transform->Rotation() * camSpace);
    m_ownerArm->transform->SetPosition(cam->transform->Position() + cam->transform->Rotation() * Vec3{side * 0.16f, -0.08f, -depth});

    // A hold stays on the real hands for everyone else. This camera sees a
    // copy of the prop on the inner face of the cube, and skips the real one.
    // Placed before the screen pin so the item cannot stick off the corner.
    bool showItem = false;
    ProceduralObjectHolding* hold = gameObject->GetComponent<ProceduralObjectHolding>();
    GameObject* held = (hold && hold->IsHolding()) ? hold->Held() : nullptr;
    if (!held || !held->transform || held->IsDestroyed()) {
        clearHeld();
    } else {
        if (m_fpHiddenHeld && m_fpHiddenHeld != held && !m_fpHiddenHeld->IsDestroyed()
            && m_fpHiddenHeld->ownerCameraSkip == gameObject)
            m_fpHiddenHeld->ownerCameraSkip = nullptr;
        held->ownerCameraSkip = gameObject;
        m_fpHiddenHeld = held;
        if (!m_ownerItem || m_ownerItem->IsDestroyed()) {
            m_ownerItem = nullptr;
            if (Scene* sc = GetScene()) {
                m_ownerItem = sc->CreateGameObject("FP_OwnerItem");
                auto* mr = m_ownerItem->AddComponent<MeshRenderer>();
                mr->castShadows = false;
                mr->doubleSided = false;
                m_ownerItem->firstPersonOnly = true;
                m_ownerItem->firstPersonViewmodel = true;
                m_ownerItem->transient = true;
                m_ownerItem->active = false;
            }
        }
        if (m_ownerItem && m_ownerItem->transform) {
            m_ownerItem->transform->SetParent(m_ownerHand->transform, false);
            MeshRenderer* src = held->GetComponent<MeshRenderer>();
            if (!src)
                for (Transform* c : held->transform->Children())
                    if (c && c->gameObject)
                        if (MeshRenderer* m = c->gameObject->GetComponent<MeshRenderer>()) { src = m; break; }
            auto* dst = m_ownerItem->GetComponent<MeshRenderer>();
            if (!src || !dst || src->mesh.vertices.empty()) {
                m_ownerItem->active = false;
            } else {
                dst->mesh = src->mesh;
                dst->color = src->color;
                dst->castShadows = false;
                dst->mesh.normals.clear();
                // The prop sits in the palm (local +Y), a little toward the inner
                // end. It is smaller than the hand, and overlaps the palm just
                // enough to read as held.
                auto* handMr = m_ownerHand->GetComponent<MeshRenderer>();
                Vec3 hmn = handMr && !handMr->mesh.vertices.empty() ? handMr->mesh.vertices[0] : Vec3{-0.06f, -0.06f, -0.08f};
                Vec3 hmx = hmn;
                if (handMr)
                    for (const Vec3& v : handMr->mesh.vertices) {
                        hmn.x = std::min(hmn.x, v.x); hmn.y = std::min(hmn.y, v.y); hmn.z = std::min(hmn.z, v.z);
                        hmx.x = std::max(hmx.x, v.x); hmx.y = std::max(hmx.y, v.y); hmx.z = std::max(hmx.z, v.z);
                    }
                const Vec3 handExt = hmx - hmn;
                const float palm = std::min(handExt.x, handExt.z);
                Vec3 mn = src->mesh.vertices[0], mx = mn;
                for (const Vec3& v : src->mesh.vertices) {
                    mn.x = std::min(mn.x, v.x); mn.y = std::min(mn.y, v.y); mn.z = std::min(mn.z, v.z);
                    mx.x = std::max(mx.x, v.x); mx.y = std::max(mx.y, v.y); mx.z = std::max(mx.z, v.z);
                }
                const float maxExt = std::max(mx.x - mn.x, std::max(mx.y - mn.y, mx.z - mn.z));
                float uni = maxExt > 1e-4f ? (0.72f * palm / maxExt) : 1.0f;
                if (fpHands && fpHands->itemScale > 0.0f) uni *= fpHands->itemScale;
                Quat irot = Quat::Euler(-16.0f, -side * 18.0f, side * 8.0f);
                if (fpHands) irot = Quat::Euler(fpHands->itemRotation) * irot;
                Vec3 center{0.0f, 0.0f, 0.0f};
                float minY = 1.0e9f;
                const float invN = 1.0f / (float)src->mesh.vertices.size();
                for (const Vec3& v : src->mesh.vertices) {
                    const Vec3 p = irot * (v * uni);
                    center = center + p * invN;
                    minY = std::min(minY, p.y);
                }
                const float bite = 0.18f * handExt.y;
                Vec3 place{-center.x, hmx.y - bite - minY, -center.z + 0.12f * handExt.z};
                if (auto* ho = held->GetComponent<HoldableObject>()) {
                    const HoldableObject::Grip& grip = base >= B_RUPARM ? ho->leftGrip : ho->rightGrip;
                    if (grip.enabled) {
                        const Vec3 palmPoint{0.0f, hmx.y - bite, 0.12f * handExt.z};
                        place = palmPoint - irot * (grip.position * uni);
                    }
                }
                if (fpHands) place = place + fpHands->itemOffset;
                m_ownerItem->transform->localScale = Vec3{uni, uni, uni};
                m_ownerItem->transform->localRotation = irot;
                m_ownerItem->transform->localPosition = place;
                m_ownerItem->active = true;
                showItem = true;
            }
        }
    }

    Vec3 camOff = cam->transform->Rotation().Conjugate() * (m_ownerArm->transform->Position() - cam->transform->Position());
    // Every vertex, not the single extreme. A nearer corner projects further
    // than a far one, so the shift is the tightest bound across the cube.
    const float wantNx = side * (0.78f + 0.55f * slide - 0.40f * snap + 0.04f * bobC);
    const float wantNy = -0.72f - 0.45f * slide + 0.28f * snap + 0.055f * bobS;
    float shiftX = side > 0.0f ? 1.0e9f : -1.0e9f;
    float shiftY = -1.0e9f;
    bool any = false;
    auto bound = [&](GameObject* g) {
        if (!g) return;
        auto* mr = g->GetComponent<MeshRenderer>();
        if (!mr || !mr->enabled) return;
        for (const Vec3& v : mr->mesh.vertices) {
            const Vec3 c = cam->transform->InverseTransformPoint(g->transform->TransformPoint(v));
            const float nz = std::max(0.05f, -c.z);
            const float limX = wantNx * tanH * nz - c.x;
            const float limY = wantNy * tanV * nz - c.y;
            shiftX = side > 0.0f ? std::min(shiftX, limX) : std::max(shiftX, limX);
            shiftY = std::max(shiftY, limY);
            any = true;
        }
    };
    bound(m_ownerHand);
    if (showItem) bound(m_ownerItem);
    if (fpSide && fpSide->sleeve && !m_fpSecond) bound(m_ownerFore);
    if (any) { camOff.x += shiftX; camOff.y += shiftY; }
    if (fpSide && !m_fpSecond) camOff = camOff + fpSide->position;
    const float sway = fpHands ? fpHands->swayAmount : 1.0f;
    const Quat lag = Quat::Euler(m_fpTrailPitch * sway, m_fpTrailYaw * sway, 0.0f);
    camOff = lag * camOff;
    m_ownerArm->transform->SetPosition(cam->transform->Position() + cam->transform->Rotation() * camOff);
    m_ownerArm->transform->SetRotation(cam->transform->Rotation() * lag * camSpace);

    // Nothing may cross the near plane. Push along the look, away from the lens.
    const float keep = std::max(0.18f, near + 0.12f);
    float minAhead = 1.0e9f;
    auto consider = [&](GameObject* g) {
        if (!g || !g->transform) return;
        auto* mr = g->GetComponent<MeshRenderer>();
        if (!mr) return;
        for (const Vec3& v : mr->mesh.vertices) {
            const Vec3 local = cam->transform->InverseTransformPoint(g->transform->TransformPoint(v));
            minAhead = std::min(minAhead, -local.z);
        }
    };
    consider(m_ownerArm);
    consider(m_ownerHand);
    if (fpSide && fpSide->sleeve && !m_fpSecond) consider(m_ownerFore);
    if (showItem) consider(m_ownerItem);
    if (minAhead < keep) {
        const Vec3 fwd = basis * Vec3{0.0f, 0.0f, -1.0f};
        m_ownerArm->transform->SetPosition(m_ownerArm->transform->Position() + fwd * (keep - minAhead));
    }
    SetOwnerArmActive(true);
    if (m_ownerItem && !showItem) m_ownerItem->active = false;
    if (m_fpSecond) FinishTwoHands(cam, depth, snap, bobS, bobC);
    else DeactivateOwnerArm2();
}

void Character::FinishTwoHands(Camera* cam, float depth, float snap, float bobS, float bobC) {
    if (!cam || !cam->transform || !m_ownerArm || !m_ownerHand || !gameObject || !gameObject->transform) {
        DeactivateOwnerArm2();
        return;
    }
    const int baseR = B_LUPARM;
    const int baseL = B_RUPARM;
    if (m_ownerArmBase != baseR) EnsureOwnerArm(baseR);
    EnsureOwnerArm2(baseL);
    if (!m_ownerArm || !m_ownerHand || !m_ownerArm2 || !m_ownerHand2 || !m_ownerArm->transform || !m_ownerArm2->transform) {
        DeactivateOwnerArm2();
        return;
    }
    FirstPersonHands* settings = FirstPersonHands::FindOn(gameObject);
    if (settings && !settings->enabled) settings = nullptr;
    const FirstPersonHands::HandSide* right = settings ? &settings->right : nullptr;
    const FirstPersonHands::HandSide* left = settings ? &settings->left : nullptr;
    const float fovScale = FpFovScale(settings, cam);
    const Vec3 root{kFpHandScale * fovScale, kFpHandScale * fovScale, kFpHandScale * fovScale};
    auto prep = [&](GameObject* arm, GameObject* fore, GameObject* hand, int base, const FirstPersonHands::HandSide* side) {
        if (arm && arm->transform) arm->transform->localScale = root;
        if (fore && fore->transform) {
            fore->transform->localPosition = Vec3::Zero;
            fore->transform->localRotation = Quat::Identity;
            fore->transform->localScale = Vec3::One;
        }
        if (hand && hand->transform) {
            hand->transform->localPosition = Vec3::Zero;
            hand->transform->localRotation = Quat::Identity;
            hand->transform->localScale = FpHandScale(side);
        }
        if (base + 2 < (int)m_parts.size()) PaintFpHand(hand, m_parts[base + 2], side);
        PaintFpSleeve(fore, side);
    };
    prep(m_ownerArm, m_ownerFore, m_ownerHand, baseR, right);
    prep(m_ownerArm2, m_ownerFore2, m_ownerHand2, baseL, left);

    ProceduralObjectHolding* ph = gameObject->GetComponent<ProceduralObjectHolding>();
    GameObject* held = (ph && ph->IsHolding()) ? ph->Held() : nullptr;
    const bool have = held && held->transform && !held->IsDestroyed() && m_ownerItem && m_ownerItem->transform;
    const float sway = settings ? settings->swayAmount : 1.0f;
    const Quat lag = Quat::Euler(m_fpTrailPitch * sway, m_fpTrailYaw * sway, 0.0f);
    const float slide = 1.0f - m_fpShown;

    if (!have) {
        if (m_ownerItem) m_ownerItem->active = false;
        Vec3 off = cam->transform->Rotation().Conjugate() * (m_ownerArm->transform->Position() - cam->transform->Position());
        if (right) off = off + right->position;
        m_ownerArm->transform->SetPosition(cam->transform->Position() + cam->transform->Rotation() * off);
        if (right) m_ownerArm->transform->SetRotation(m_ownerArm->transform->Rotation() * Quat::Euler(right->rotation));
        Vec3 leftOff{-off.x, off.y, off.z};
        if (left) leftOff = leftOff + left->position;
        const Quat rel = cam->transform->Rotation().Conjugate() * m_ownerArm->transform->Rotation();
        Vec3 e = rel.ToEuler();
        e.y = -e.y;
        e.z = -e.z;
        if (left) e = e + left->rotation;
        m_ownerArm2->transform->SetPosition(cam->transform->Position() + cam->transform->Rotation() * leftOff);
        m_ownerArm2->transform->SetRotation(cam->transform->Rotation() * Quat::Euler(e));
        m_ownerArm2->active = true;
        if (m_ownerFore2) m_ownerFore2->active = true;
        if (m_ownerHand2) m_ownerHand2->active = true;
        SetOwnerArmActive(true);
        return;
    }

    MeshRenderer* src = held->GetComponent<MeshRenderer>();
    if (!src)
        for (Transform* c : held->transform->Children())
            if (c && c->gameObject)
                if (MeshRenderer* m = c->gameObject->GetComponent<MeshRenderer>()) { src = m; break; }
    auto* dst = m_ownerItem->GetComponent<MeshRenderer>();
    if (!src || !dst || src->mesh.vertices.empty()) {
        m_ownerItem->active = false;
        DeactivateOwnerArm2();
        return;
    }
    dst->mesh = src->mesh;
    dst->color = src->color;
    dst->castShadows = false;
    dst->mesh.normals.clear();
    Vec3 mn = src->mesh.vertices[0], mx = mn;
    for (const Vec3& v : src->mesh.vertices) {
        mn.x = std::min(mn.x, v.x); mn.y = std::min(mn.y, v.y); mn.z = std::min(mn.z, v.z);
        mx.x = std::max(mx.x, v.x); mx.y = std::max(mx.y, v.y); mx.z = std::max(mx.z, v.z);
    }
    const Vec3 centre = (mn + mx) * 0.5f;
    const Vec3 half = (mx - mn) * 0.5f;
    // The copy uses the prop's own proportions, fitted so the long side is a
    // held object in front of the lens rather than a wall. itemScale still applies.
    const Vec3 lossy = held->transform->LossyScale();
    const Vec3 ext{mx.x - mn.x, mx.y - mn.y, mx.z - mn.z};
    const float worldLong = std::max(std::fabs(lossy.x * ext.x),
                            std::max(std::fabs(lossy.y * ext.y), std::fabs(lossy.z * ext.z)));
    Vec3 scl = worldLong > 1e-4f ? lossy * (0.26f / worldLong) : Vec3{0.26f, 0.26f, 0.26f};
    if (settings && settings->itemScale > 0.0f) scl = scl * settings->itemScale;
    // Tilt the top toward the lens. A face square to the camera sits in shadow.
    Quat irot = Quat::Euler(-36.0f, 12.0f, 0.0f);
    if (settings) irot = Quat::Euler(settings->itemRotation) * irot;
    Vec3 camOff{0.0f, -0.04f - 0.22f * slide + 0.08f * snap + 0.02f * bobS, -std::max(0.58f, depth + 0.06f)};
    camOff.x += 0.02f * bobC;
    if (settings) camOff = camOff + settings->itemOffset;
    camOff = lag * camOff;
    m_ownerItem->transform->SetParent(gameObject->transform, false);
    m_ownerItem->transform->localScale = scl;
    m_ownerItem->transform->SetRotation(cam->transform->Rotation() * lag * irot);
    m_ownerItem->transform->SetPosition(cam->transform->Position() + cam->transform->Rotation() * camOff);
    // Lift the camera-facing side so a crate does not read as a black slab.
    dst->emissive = Color{src->color.r * 0.22f, src->color.g * 0.22f, src->color.b * 0.22f, 1.0f};
    m_ownerItem->active = true;
    held->ownerCameraSkip = gameObject;
    m_fpHiddenHeld = held;

    Vec3 imn{1.0e9f, 1.0e9f, 1.0e9f}, imx{-1.0e9f, -1.0e9f, -1.0e9f};
    for (const Vec3& v : dst->mesh.vertices) {
        const Vec3 c = cam->transform->InverseTransformPoint(m_ownerItem->transform->TransformPoint(v));
        imn.x = std::min(imn.x, c.x); imn.y = std::min(imn.y, c.y); imn.z = std::min(imn.z, c.z);
        imx.x = std::max(imx.x, c.x); imx.y = std::max(imx.y, c.y); imx.z = std::max(imx.z, c.z);
    }
    auto* ho = held->GetComponent<HoldableObject>();
    auto placeHand = [&](GameObject* arm, GameObject* hand, bool isLeft, const FirstPersonHands::HandSide* side) {
        // Grip height comes from the item. The hand itself sits on the near
        // left or right edge, outside the prop, broad face back at the lens.
        float gripY = 0.0f;
        if (ho) {
            const HoldableObject::Grip& g = isLeft ? ho->leftGrip : ho->rightGrip;
            const Vec3 gripLocal = g.enabled ? g.position : (centre + Vec3{isLeft ? -half.x : half.x, 0.0f, 0.0f});
            const Vec3 gripCam = cam->transform->InverseTransformPoint(m_ownerItem->transform->TransformPoint(gripLocal));
            gripY = gripCam.y;
        }
        if (gripY < imn.y) gripY = imn.y;
        if (gripY > imx.y) gripY = imx.y;
        float radius = 0.05f * root.x;
        if (hand) if (auto* mr = hand->GetComponent<MeshRenderer>()) if (!mr->mesh.vertices.empty()) {
            float e = 0.0f;
            for (const Vec3& v : mr->mesh.vertices) e = std::max(e, std::max(std::fabs(v.x), std::max(std::fabs(v.y), std::fabs(v.z))));
            const float sy = hand->transform ? std::fabs(hand->transform->localScale.y) : 1.0f;
            radius = e * sy * root.x;
        }
        const float x = isLeft ? (imn.x - radius * 0.65f) : (imx.x + radius * 0.65f);
        const float z = imx.z - 0.01f;   // the near face; camera looks down -Z
        Vec3 pos = cam->transform->TransformPoint(Vec3{x, gripY, z});
        const Vec3 inward = (cam->transform->Rotation() * Vec3{isLeft ? 0.85f : -0.85f, 0.0f, -0.25f}).Normalized();
        const Vec3 face = (cam->transform->Rotation() * Vec3{isLeft ? -0.2f : 0.2f, 0.78f, 0.50f}).Normalized();
        Quat rot = Quat::LookRotation(inward, face);
        if (side) {
            rot = rot * Quat::Euler(side->rotation);
            pos = pos + cam->transform->Rotation() * side->position;
        }
        arm->transform->SetRotation(rot);
        arm->transform->SetPosition(pos);
        arm->active = true;
        (void)snap;
    };
    placeHand(m_ownerArm, m_ownerHand, false, right);
    placeHand(m_ownerArm2, m_ownerHand2, true, left);
    if (m_ownerFore) m_ownerFore->active = true;
    if (m_ownerHand) m_ownerHand->active = true;
    if (m_ownerFore2) m_ownerFore2->active = true;
    if (m_ownerHand2) m_ownerHand2->active = true;
}

void Character::RememberPartRest() {
    m_partRest.assign(m_parts.size(), Vec3{0.0f, 0.0f, 0.0f});
    for (std::size_t i = 0; i < m_parts.size(); ++i)
        if (m_parts[i] && m_parts[i]->transform) m_partRest[i] = m_parts[i]->transform->localPosition;
}

void Character::RemoveParts() {
    ReleaseOwnerArm();   // the copy hangs off the player, not the rig, so the rig's destroy would miss it
    ReleaseOwnerArm2();
    if (GameObject* rig = FindRig())
        if (Scene* s = GetScene()) s->Destroy(rig);   // takes its children with it, copy included
    m_rigRoot = nullptr;
    m_parts.clear();
    m_partRest.clear();
    m_partsBuilt = false;
    if (gameObject) if (auto* mr = gameObject->GetComponent<MeshRenderer>()) mr->enabled = true;
}

void Character::EditorPreviewTick() {
    if (separateParts) {
        if (!m_partsBuilt) BuildParts();
        if (m_partsBuilt) {
            m_fpShown = (firstPersonArm && fpArmBase >= 0)
                ? (fpArmUp < 0.0f ? 0.0f : (fpArmUp > 1.0f ? 1.0f : fpArmUp)) : 0.0f;
            DriveParts();      // push the (preview/rest) pose onto the rig
        }
        return;
    }
    // Single mesh: rebuild from the previewed pose so the editor shows it live.
    auto* mr = gameObject ? gameObject->GetComponent<MeshRenderer>() : nullptr;
    if (!mr) return;
    EnsureRest();
    Mesh m = m_rest;
    Skin(m, m_bone, CurrentSourcePose());
    Vec3 so = StanceOffset();
    FaceMinusZ(m);
    for (Vec3& v : m.vertices) { v = v * height; v += so; }
    m.normals.clear(); mr->mesh = std::move(m); mr->doubleSided = false;
}

void Character::BuildParts() {
    Scene* s = GetScene();
    if (!s || !gameObject || !gameObject->transform) return;
    // A custom-rigged model owns this character: the blocky part rig would just
    // spawn the old default body on top of it. (Unrig to get the rig back.)
    if (m_hasCustomBind) { separateParts = false; return; }
    // Collapse to a SINGLE rig: adopt the first existing "Rig" child and destroy any
    // extras, so clicking "Separate Into Parts" (or replaying) never stacks up rigs.
    std::vector<GameObject*> rigs;
    for (Transform* c : gameObject->transform->Children())
        if (c && c->gameObject && c->gameObject->name == "Rig") rigs.push_back(c->gameObject);
    if (!rigs.empty()) {
        for (std::size_t i = 1; i < rigs.size(); ++i) s->Destroy(rigs[i]);
        AdoptParts(rigs[0]);
        return;
    }
    m_partsBuilt = false;   // no rig present — (re)build one even if the flag was stale
    EnsureRest();   // m_rest (pre-flip/height) + m_bone (per-vertex bone)
    auto sk = Skeleton();
    if ((int)sk.size() < B_COUNT) return;
    m_parts.assign(B_COUNT, nullptr);

    GameObject* root = s->CreateGameObject("Rig");
    root->transform->SetParent(gameObject->transform, false);
    root->transform->localRotation = Quat::Euler(0.0f, 180.0f, 0.0f);   // face -Z, like the baked mesh
    root->transform->localScale = Vec3{height, height, height};
    m_rigRoot = root;

    const bool cols = m_rest.HasFaceColors();
    const int faces = (int)m_rest.triangles.size() / 3;
    for (int bi = 0; bi < B_COUNT; ++bi) {
        Mesh part;
        for (int f = 0; f < faces; ++f) {
            int a = m_rest.triangles[f*3], b = m_rest.triangles[f*3+1], c = m_rest.triangles[f*3+2];
            if (a >= (int)m_bone.size() || b >= (int)m_bone.size() || c >= (int)m_bone.size()) continue;
            if (m_bone[a] != bi || m_bone[b] != bi || m_bone[c] != bi) continue;
            for (int idx : {a, b, c}) {
                part.triangles.push_back((int)part.vertices.size());
                part.vertices.push_back(m_rest.vertices[idx] - sk[bi].joint);   // re-centre to the joint
            }
            if (cols) part.triColors.push_back(m_rest.triColors[f]);
        }
        if (part.vertices.empty()) continue;
        GameObject* po = s->CreateGameObject(BoneName(bi));
        auto* mr = po->AddComponent<MeshRenderer>();
        // Outward winding (the rig's 180° yaw keeps it). Single-sided, so a
        // camera that pitches down sees the shell and not the open inside.
        mr->mesh = std::move(part); mr->doubleSided = false;
        m_parts[bi] = po;
    }
    // Parent per the skeleton so rotating a bone moves its children (real rig).
    for (int bi = 0; bi < B_COUNT; ++bi) {
        if (!m_parts[bi]) continue;
        int par = sk[bi].parent;
        Vec3 parentJoint = (par >= 0) ? sk[par].joint : Vec3{0.0f, 0.0f, 0.0f};
        GameObject* parentObj = (par >= 0 && m_parts[par]) ? m_parts[par] : root;
        m_parts[bi]->transform->SetParent(parentObj->transform, false);
        m_parts[bi]->transform->localPosition = sk[bi].joint - parentJoint;
    }
    if (auto* mr = gameObject->GetComponent<MeshRenderer>()) mr->enabled = false;   // hide the baked mesh
    RememberPartRest();
    m_partsBuilt = true;
    SplitNeckFromTorso();
}

void Character::SplitNeckFromTorso() {
    GameObject* torso = Part(B_TORSO);
    if (!torso || !torso->transform) return;
    for (Transform* c : torso->transform->Children())
        if (c && c->gameObject && c->gameObject->name == "Neck") return;
    auto* mr = torso->GetComponent<MeshRenderer>();
    Scene* s = GetScene();
    if (!mr || !s) return;
    Mesh& src = mr->mesh;
    const bool cols = src.HasFaceColors();
    Mesh neck, keep;
    const int faces = (int)src.triangles.size() / 3;
    for (int f = 0; f < faces; ++f) {
        int i0 = src.triangles[f * 3], i1 = src.triangles[f * 3 + 1], i2 = src.triangles[f * 3 + 2];
        const bool ok = i0 >= 0 && i1 >= 0 && i2 >= 0
            && i0 < (int)src.vertices.size() && i1 < (int)src.vertices.size() && i2 < (int)src.vertices.size();
        const bool neckFace = ok && NeckLocalVert(src.vertices[i0]) && NeckLocalVert(src.vertices[i1])
            && NeckLocalVert(src.vertices[i2]);
        Mesh& dst = neckFace ? neck : keep;
        if (!ok) continue;
        for (int idx : {i0, i1, i2}) {
            dst.triangles.push_back((int)dst.vertices.size());
            dst.vertices.push_back(src.vertices[idx]);
        }
        if (cols && f < (int)src.triColors.size()) dst.triColors.push_back(src.triColors[f]);
    }
    if (neck.vertices.empty()) return;
    mr->mesh = std::move(keep);
    mr->doubleSided = false;
    GameObject* n = s->CreateGameObject("Neck");
    auto* nm = n->AddComponent<MeshRenderer>();
    nm->mesh = std::move(neck);
    nm->doubleSided = false;
    nm->castShadows = true;
    n->transform->SetParent(torso->transform, false);
    n->transform->localPosition = Vec3{0.0f, 0.0f, 0.0f};
    n->active = true;
    n->firstPersonViewmodel = false;
}

void Character::ApplyOwnerVisibility(bool firstPerson) {
    if (!m_partsBuilt) return;
    for (int i = 0; i < B_COUNT && i < (int)m_parts.size(); ++i) {
        GameObject* p = m_parts[i];
        if (!p) continue;
        p->active = true;
        p->firstPersonViewmodel = firstPerson && ShowsToOwnerCamera(i);
        if (auto* mr = p->GetComponent<MeshRenderer>()) {
            mr->doubleSided = false;
            if (i == B_HEAD) mr->castShadows = true;
        }
    }
    GameObject* torso = Part(B_TORSO);
    if (!torso || !torso->transform) return;
    for (Transform* c : torso->transform->Children()) {
        if (!c || !c->gameObject || c->gameObject->name != "Neck") continue;
        c->gameObject->active = true;
        c->gameObject->firstPersonViewmodel = false;
        if (auto* mr = c->gameObject->GetComponent<MeshRenderer>()) {
            mr->doubleSided = false;
            mr->castShadows = true;
        }
    }
}

Vec3 Character::ClearFirstPersonEye(Vec3 eye) const {
    if (!m_partsBuilt || !gameObject || !gameObject->transform) return eye;
    const Vec3 fwd = gameObject->transform->Rotation() * Vec3{0.0f, 0.0f, -1.0f};
    const Vec3 up  = gameObject->transform->Rotation() * Vec3{0.0f, 1.0f, 0.0f};
    auto inside = [&](const Vec3& at) {
        for (int b = 0; b < B_COUNT && b < (int)m_parts.size(); ++b) {
            if (b == B_HEAD || (b >= B_LUPARM && b <= B_RHAND)) continue;
            GameObject* part = m_parts[b];
            if (!part || !part->transform) continue;
            auto* mr = part->GetComponent<MeshRenderer>();
            if (!mr || mr->mesh.vertices.empty()) continue;
            Vec3 lo, hi;
            mr->mesh.Bounds(lo, hi);
            const Vec3 local = part->transform->InverseTransformPoint(at);
            if (local.x > lo.x && local.x < hi.x && local.y > lo.y && local.y < hi.y
                && local.z > lo.z && local.z < hi.z)
                return true;
        }
        return false;
    };
    bool moved = false;
    for (int step = 0; step < 12 && inside(eye); ++step) {
        eye = eye + fwd * 0.05f + up * 0.04f;
        moved = true;
    }
    // Stop a little clear of the shell so the near plane does not slice it.
    if (moved) eye = eye + fwd * 0.04f + up * 0.03f;
    return eye;
}

std::vector<Vec3> Character::CurrentSourcePose() const {
    std::vector<Vec3> p;
    Vec3 root;
    if (m_editorPosing)                       { p = m_editorPose; p.resize(B_COUNT, Vec3{0, 0, 0}); }
    else if (m_activeClip && !m_stateDriven)  { p = m_activeClip->Sample(m_clipTime); p.resize(B_COUNT, Vec3{0, 0, 0}); }  // manual clip wins
    else if (AnimatorSample(p, root)) {       // the Animator crossfades itself
        ApplyLookAndLean(p);
        ApplyLayer(p);
        return p;
    }
    else if (m_blendTreeOn)                   p = SampleBlendTree();          // locomotion blend tree
    else if (m_activeClip)                    { p = m_activeClip->Sample(m_clipTime); p.resize(B_COUNT, Vec3{0, 0, 0}); }  // state clip
    else                                      p = PoseAt(animTime);
    // Crossfade from the state we left (live) or the pose captured at the last clip
    // switch (smoothstep weight).
    if (m_blendT < 1.0f && !p.empty()) {
        const bool live = m_blendFromAnim >= 0 && !m_editorPosing && !m_activeClip && !m_blendTreeOn;
        std::vector<Vec3> from = live ? PoseFor(m_blendFromAnim, animTime) : m_blendFrom;
        if (from.size() == p.size()) {
            float w = m_blendT * m_blendT * (3.0f - 2.0f * m_blendT);
            for (std::size_t i = 0; i < p.size(); ++i) p[i] = from[i] + (p[i] - from[i]) * w;
        }
    }
    ApplyLayer(p);   // overlay a partial-body layer clip (e.g. wave while walking) on top
    return p;
}

void Character::ApplyLayer(std::vector<Vec3>& pose) const {
    if (!m_layerClip || m_layerMask == 0 || m_layerWeight <= 0.0f) return;
    std::vector<Vec3> lp = m_layerClip->Sample(m_layerTime);
    if (lp.empty()) return;
    if ((int)pose.size() < B_COUNT) pose.resize(B_COUNT, Vec3{0, 0, 0});
    float w = m_layerWeight;
    for (int b = 0; b < B_COUNT && b < (int)lp.size(); ++b) {
        if (!(m_layerMask & (1u << b))) continue;
        if (m_layerAdditive) pose[b] = pose[b] + lp[b] * w;            // add (aim offset / recoil)
        else                 pose[b] = pose[b] + (lp[b] - pose[b]) * w; // weighted override
    }
}

void Character::AdvanceLayer(float dt) {
    if (!m_layerClip) return;
    m_layerTime += dt * animSpeed * m_layerClip->speed;
    float dur = m_layerClip->Duration();
    if (dur > 0.0f) {
        if (m_layerClip->loop) m_layerTime = std::fmod(m_layerTime, dur);
        else if (m_layerTime > dur) m_layerTime = dur;   // hold the final pose
    }
}

bool Character::PlayLayer(const std::string& clip, std::uint32_t boneMask, bool additive, float weight) {
    auto it = m_clips.find(clip);
    if (it == m_clips.end()) return false;
    m_layerClip = &it->second;
    m_layerName = clip;
    m_layerTime = 0.0f;
    m_layerMask = boneMask;
    m_layerAdditive = additive;
    m_layerWeight = weight < 0.0f ? 0.0f : (weight > 1.0f ? 1.0f : weight);
    return true;
}

void Character::MirrorPose(std::vector<Vec3>& pose) {
    if ((int)pose.size() < B_COUNT) pose.resize(B_COUNT, Vec3{0, 0, 0});
    // Swap the symmetric limb bones (and flip their yaw/roll so the motion mirrors).
    const int pairs[][2] = {
        {B_LUPARM, B_RUPARM}, {B_LFORE, B_RFORE}, {B_LHAND, B_RHAND},
        {B_LTHIGH, B_RTHIGH}, {B_LSHIN, B_RSHIN}, {B_LFOOT, B_RFOOT},
    };
    for (auto& pr : pairs) {
        Vec3 l = pose[pr[0]], r = pose[pr[1]];
        // mirror across the body's sagittal plane: keep pitch (x), flip yaw (y) & roll (z)
        pose[pr[0]] = Vec3{r.x, -r.y, -r.z};
        pose[pr[1]] = Vec3{l.x, -l.y, -l.z};
    }
    // Centre bones just flip yaw/roll in place.
    for (int b : {B_HIPS, B_TORSO, B_HEAD}) pose[b] = Vec3{pose[b].x, -pose[b].y, -pose[b].z};
}

void Character::StopLayer() {
    m_layerClip = nullptr;
    m_layerName.clear();
    m_layerTime = 0.0f;
    m_layerMask = 0;
}

void Character::SetBlendTree(const std::vector<BlendStop>& stops) {
    m_blendStops = stops;
    std::sort(m_blendStops.begin(), m_blendStops.end(),
              [](const BlendStop& a, const BlendStop& b) { return a.at < b.at; });
    m_blendTreeOn = !m_blendStops.empty();
    m_blendTreeTime = 0.0f;
}

void Character::AdvanceBlendTree(float dt) {
    if (m_blendTreeOn) m_blendTreeTime += dt * animSpeed;
}

std::vector<Vec3> Character::SampleBlendTree() const {
    // Sample one clip at the shared (looping) phase.
    auto sampleClip = [&](const std::string& name) -> std::vector<Vec3> {
        auto it = m_clips.find(name);
        if (it == m_clips.end()) return std::vector<Vec3>(B_COUNT, Vec3{0, 0, 0});
        float dur = it->second.Duration();
        float t = dur > 0.0f ? std::fmod(m_blendTreeTime, dur) : 0.0f;
        std::vector<Vec3> p = it->second.Sample(t);
        p.resize(B_COUNT, Vec3{0, 0, 0});
        return p;
    };
    if (m_blendStops.empty()) return std::vector<Vec3>(B_COUNT, Vec3{0, 0, 0});
    if (m_blendParam <= m_blendStops.front().at) return sampleClip(m_blendStops.front().clip);
    if (m_blendParam >= m_blendStops.back().at)  return sampleClip(m_blendStops.back().clip);
    for (std::size_t i = 0; i + 1 < m_blendStops.size(); ++i) {
        const BlendStop& a = m_blendStops[i];
        const BlendStop& b = m_blendStops[i + 1];
        if (m_blendParam < a.at || m_blendParam > b.at) continue;
        float span = b.at - a.at;
        float w = span > 1e-6f ? (m_blendParam - a.at) / span : 0.0f;
        std::vector<Vec3> pa = sampleClip(a.clip), pb = sampleClip(b.clip);
        std::vector<Vec3> out(B_COUNT, Vec3{0, 0, 0});
        for (int k = 0; k < B_COUNT; ++k) out[k] = pa[k] + (pb[k] - pa[k]) * w;
        return out;
    }
    return sampleClip(m_blendStops.back().clip);
}

std::uint32_t Character::ArmsMask() {
    return BoneBit(B_LUPARM) | BoneBit(B_LFORE) | BoneBit(B_LHAND) |
           BoneBit(B_RUPARM) | BoneBit(B_RFORE) | BoneBit(B_RHAND);
}
std::uint32_t Character::UpperBodyMask() {
    return ArmsMask() | BoneBit(B_TORSO) | BoneBit(B_HEAD);
}
std::vector<Vec3> Character::CurrentPose() const { return CurrentSourcePose(); }

void Character::EaseFirstPersonArm(float dt) {
    if (dt > 0.0f) m_fpDt = dt;
    const float want = (firstPersonArm && fpArmBase >= 0)
        ? (fpArmUp < 0.0f ? 0.0f : (fpArmUp > 1.0f ? 1.0f : fpArmUp)) : 0.0f;
    if (dt <= 0.0f) { m_fpShown = want; return; }
    const float k = 1.0f - std::exp(-dt * 8.0f);   // most of the way there in ~0.3 s
    m_fpShown += (want - m_fpShown) * k;
    if (std::fabs(m_fpShown - want) < 0.001f) m_fpShown = want;
}

void Character::DriveParts() {
    if (!m_partsBuilt) return;
    std::vector<Vec3> pose = CurrentSourcePose();
    // The punch stays on the real right arm, the swing third person and every
    // other machine already play. The owner's copy swings separately, below.
    std::vector<Vec3> shown = pose;
    if (m_punchT >= 0.0f && m_punchT < 1.0f && B_RUPARM + 1 < (int)shown.size()) {
        float arc = std::sin(m_punchT * 3.14159265f);
        Vec3 up = {-110.0f, 0.0f, 8.0f}, fore = {-18.0f, 0.0f, 0.0f};
        shown[B_RUPARM]     = shown[B_RUPARM]     + (up   - shown[B_RUPARM])     * arc;
        shown[B_RUPARM + 1] = shown[B_RUPARM + 1] + (fore - shown[B_RUPARM + 1]) * arc;
    }
    // The whole pose is rewritten every frame, joint offsets included: IK that moved
    // a joint last frame (foot IK on a step) starts again from the animation.
    // First person does not rewrite these bones. A look pitch lives on the copy.
    for (int bi = 0; bi < B_COUNT && bi < (int)shown.size(); ++bi)
        if (m_parts[bi] && m_parts[bi]->transform) {
            m_parts[bi]->transform->localRotation = Quat::Euler(shown[bi]);
            if (bi < (int)m_partRest.size()) m_parts[bi]->transform->localPosition = m_partRest[bi];
        }
    if (m_rigRoot && m_rigRoot->transform) {
        Vec3 so = StanceOffset();
        m_rigRoot->transform->localPosition = Vec3{0.0f, so.y, 0.0f};
        m_rigRoot->transform->localScale = Vec3{height, height, height};
    }
    PoseOwnerArm(pose);
}

std::string Character::ToText() const {
    std::ostringstream o;
    auto col = [&](const Color& c) { o << c.r << ' ' << c.g << ' ' << c.b << ' '; };
    o << height << ' ';
    col(skin); col(shirt); col(pants); col(shoes); col(hair); col(eyes);
    col(hat); col(pack); col(gloves); col(jacket); col(belt);
    o << (hasHair ? 1 : 0) << ' ' << hairStyle << ' ' << beardStyle << ' '
      << shirtStyle << ' ' << legStyle << ' ' << (hasBelt ? 1 : 0) << ' '
      << (hasJacket ? 1 : 0) << ' ' << (hasGloves ? 1 : 0) << ' '
      << (hasHat ? 1 : 0) << ' ' << hatStyle << ' ' << glassesStyle << ' '
      << (hasMask ? 1 : 0) << ' ' << (hasScarf ? 1 : 0) << ' ' << (hasShoulderPads ? 1 : 0) << ' '
      << (hasBackpack ? 1 : 0) << ' ' << (hasCape ? 1 : 0) << ' '
      << anim << ' ' << animSpeed << ' '
      // Custom-clip fields last so older saves (which lack them) still parse; "-"
      // stands in for an empty value (single tokens, no embedded spaces).
      << std::quoted(clipsFile.empty() ? std::string("-") : clipsFile) << ' '   // quoted: paths may have spaces
      << (autoPlayClip.empty() ? "-" : autoPlayClip) << ' '
      << (separateParts ? 1 : 0) << ' ' << (animateParts ? 1 : 0);
    // Authored animation clips (per-bone euler keyframes) so editor-made animations
    // persist + auto-play. Names are single tokens (no spaces); poses are by index.
    o << ' ' << m_clips.size();
    for (const auto& kv : m_clips) {
        const AnimClip& c = kv.second;
        o << ' ' << (c.name.empty() ? "-" : c.name) << ' ' << (c.loop ? 1 : 0) << ' ' << c.keys.size();
        for (const auto& k : c.keys) {
            o << ' ' << k.time << ' ' << k.pose.size();
            for (const auto& p : k.pose) o << ' ' << p.x << ' ' << p.y << ' ' << p.z;
        }
    }
    // State-clip bindings last (trailing, so older saves still parse). "-" = unbound.
    o << ' ' << (clipIdle.empty() ? "-" : clipIdle)
      << ' ' << (clipWalk.empty() ? "-" : clipWalk)
      << ' ' << (clipRun.empty()  ? "-" : clipRun)
      << ' ' << blendTime;
    // Animation events per clip (trailing + count-prefixed, so older saves parse as
    // "no events"). Only clips that actually have events are written.
    int withEvents = 0;
    for (const auto& kv : m_clips) if (!kv.second.events.empty()) ++withEvents;
    o << ' ' << withEvents;
    for (const auto& kv : m_clips) {
        const AnimClip& c = kv.second;
        if (c.events.empty()) continue;
        o << ' ' << (c.name.empty() ? "-" : c.name) << ' ' << c.events.size();
        for (const auto& e : c.events) o << ' ' << e.time << ' ' << (e.name.empty() ? "-" : e.name);
    }
    // Jump/fall/crouch bindings, the linked-file flag, then each clip's playback
    // settings (trailing again, so older saves read as defaults).
    o << ' ' << (clipJump.empty() ? "-" : clipJump)
      << ' ' << (clipFall.empty() ? "-" : clipFall)
      << ' ' << (clipCrouch.empty() ? "-" : clipCrouch)
      << ' ' << (clipsLinked ? 1 : 0) << ' ' << m_clips.size();
    for (const auto& kv : m_clips) {
        const AnimClip& c = kv.second;
        o << ' ' << (c.name.empty() ? "-" : c.name) << ' ' << (int)c.interp << ' ' << c.speed
          << ' ' << c.length << ' ' << (c.overrideMovement ? 1 : 0);
    }
    // Root tracks of the clips that have one (trailing, count-prefixed).
    int withRoot = 0;
    for (const auto& kv : m_clips) if (kv.second.hasRoot) ++withRoot;
    o << ' ' << withRoot;
    for (const auto& kv : m_clips) {
        const AnimClip& c = kv.second;
        if (!c.hasRoot) continue;
        o << ' ' << (c.name.empty() ? "-" : c.name) << ' ' << c.keys.size();
        for (const auto& k : c.keys) o << ' ' << k.root.x << ' ' << k.root.y << ' ' << k.root.z;
    }
    return o.str();
}

void Character::FromText(const std::string& text) {
    std::istringstream in(text);
    auto col = [&](Color& c) { in >> c.r >> c.g >> c.b; c.a = 1.0f; };
    in >> height;
    col(skin); col(shirt); col(pants); col(shoes); col(hair); col(eyes);
    col(hat); col(pack); col(gloves); col(jacket); col(belt);
    int hh, hbelt, hj, hg, hhat, hm, hs, hsp, hbp, hc;
    in >> hh >> hairStyle >> beardStyle >> shirtStyle >> legStyle >> hbelt
       >> hj >> hg >> hhat >> hatStyle >> glassesStyle >> hm >> hs >> hsp
       >> hbp >> hc >> anim >> animSpeed;
    if (in.fail()) return;
    hasHair = hh != 0; hasBelt = hbelt != 0; hasJacket = hj != 0; hasGloves = hg != 0;
    hasHat = hhat != 0; hasMask = hm != 0; hasScarf = hs != 0; hasShoulderPads = hsp != 0;
    hasBackpack = hbp != 0; hasCape = hc != 0;
    // Optional trailing custom-clip fields (absent in older saves -> left default).
    std::string cf, ap;
    if (in >> std::quoted(cf) && cf != "-") clipsFile = cf;   // quoted, or one bare token (older saves)
    if (in >> ap && ap != "-") autoPlayClip = ap;
    int sp = 0, an = 1;
    if (in >> sp) separateParts = (sp != 0);
    if (in >> an) animateParts = (an != 0);
    // Authored clips (optional; absent in older saves).
    int nc = 0;
    if (in >> nc) {
        for (int ci = 0; ci < nc; ++ci) {
            std::string cn; int lp = 1; int nk = 0;
            if (!(in >> cn >> lp >> nk)) break;
            AnimClip c; c.name = (cn == "-" ? "" : cn); c.loop = (lp != 0);
            for (int ki = 0; ki < nk; ++ki) {
                AnimKey key; int np = 0;
                if (!(in >> key.time >> np)) break;
                key.pose.resize(np);
                for (int pi = 0; pi < np; ++pi) in >> key.pose[pi].x >> key.pose[pi].y >> key.pose[pi].z;
                c.keys.push_back(std::move(key));
            }
            if (!c.name.empty()) AddClip(std::move(c));
        }
    }
    // Optional trailing state-clip bindings (absent in older saves -> unbound).
    std::string si, sw, sr;
    if (in >> si) clipIdle = (si == "-" ? "" : si);
    if (in >> sw) clipWalk = (sw == "-" ? "" : sw);
    if (in >> sr) clipRun  = (sr == "-" ? "" : sr);
    float bt = 0.15f;
    if (in >> bt) blendTime = bt;
    // Optional trailing per-clip animation events (absent in older saves).
    int we = 0;
    if (in >> we) {
        for (int i = 0; i < we; ++i) {
            std::string cn; int nev = 0;
            if (!(in >> cn >> nev)) break;
            std::vector<AnimEvent> evs;
            for (int j = 0; j < nev; ++j) {
                AnimEvent e; std::string en;
                if (!(in >> e.time >> en)) break;
                e.name = (en == "-" ? "" : en);
                evs.push_back(std::move(e));
            }
            std::string key = (cn == "-" ? "" : cn);
            auto it = m_clips.find(key);
            if (it != m_clips.end()) it->second.events = std::move(evs);
        }
    }
    // Optional trailing jump/fall/crouch bindings, linked flag and clip settings.
    std::string sj, sf, sc;
    if (in >> sj) clipJump = (sj == "-" ? "" : sj);
    if (in >> sf) clipFall = (sf == "-" ? "" : sf);
    if (in >> sc) clipCrouch = (sc == "-" ? "" : sc);
    int lk = 0;
    if (in >> lk) clipsLinked = (lk != 0);
    int nm = 0;
    if (in >> nm) {
        for (int i = 0; i < nm; ++i) {
            std::string cn; int ip = 0, ov = 0; float spd = 1.0f, len = 0.0f;
            if (!(in >> cn >> ip >> spd >> len >> ov)) break;
            auto it = m_clips.find(cn == "-" ? "" : cn);
            if (it == m_clips.end()) continue;
            it->second.interp = (ip == 1) ? AnimInterp::Smooth : (ip == 2) ? AnimInterp::Step : AnimInterp::Linear;
            it->second.speed = spd > 1e-4f ? spd : 1.0f;
            it->second.length = len > 0.0f ? len : 0.0f;
            it->second.overrideMovement = (ov != 0);
        }
    }
    // Optional trailing root tracks.
    int nr = 0;
    if (in >> nr) {
        for (int i = 0; i < nr; ++i) {
            std::string cn; std::size_t nk = 0;
            if (!(in >> cn >> nk)) break;
            auto it = m_clips.find(cn == "-" ? "" : cn);
            AnimClip* clip = (it != m_clips.end() && it->second.keys.size() == nk) ? &it->second : nullptr;
            for (std::size_t k = 0; k < nk && in; ++k) {
                Vec3 r; in >> r.x >> r.y >> r.z;
                if (clip) clip->keys[k].root = r;
            }
            if (clip && in) clip->hasRoot = true;
        }
    }
    m_built = false;
}

} // namespace okay
