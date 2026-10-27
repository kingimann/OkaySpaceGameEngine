#include "okay/Components/Character.hpp"
#include "okay/Animation/HumanoidMotion.hpp"
#include "okay/Components/MeshRenderer.hpp"
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
        box({s * 0.27f, 0.99f, 0}, {0.12f, 0.32f, 0.16f}, foreArmCol, fo);
        // One plain block at the wrist, offset out of the sleeve so the first-person
        // arm shows a palm instead of the forearm end. No thumb and no finger
        // segments. It ends about where a grip is placed, so it meets a held
        // object instead of stabbing through it.
        box({s * 0.34f, 0.80f, 0.05f}, {0.11f, 0.10f, 0.11f}, handCol, ha);
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
    for (Vec3& v : m.vertices) { v = Vec3{-v.x, v.y, -v.z} * height; v += so; }
    m.normals.clear();                     // boxy -> flat shading
    mr->mesh = std::move(m);
    mr->doubleSided = true;
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
            for (Vec3& v : m.vertices) { v = Vec3{-v.x, v.y, -v.z} * height; v += m_editorOffset; }
            m.normals.clear(); mr->mesh = std::move(m); mr->doubleSided = true;
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
        for (Vec3& v : m.vertices) { v = Vec3{-v.x, v.y, -v.z} * height; }  // face -Z (see Apply)
        m.normals.clear();
        mr->mesh = std::move(m);
        mr->doubleSided = true;
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
    // First-person view: collapse the head (and the small neck box on the torso)
    // onto the neck joint so the owner's eyes don't see the inside of the skull.
    // The chest, arms and legs stay, which is what you see looking down.
    if (hideOwnHead && m_bone.size() == m.vertices.size()) {
        const Vec3 sink{0.0f, 1.48f, 0.0f};
        for (std::size_t i = 0; i < m.vertices.size(); ++i) {
            bool head = m_bone[i] == B_HEAD;
            bool neck = false;
            if (!head && m_bone[i] == B_TORSO && i < m_restPos.size()) {
                const Vec3& r = m_restPos[i];
                neck = r.y > 1.445f && r.y < 1.56f && std::fabs(r.x) < 0.11f && std::fabs(r.z) < 0.11f;
            }
            if (head || neck) m.vertices[i] = sink;
        }
    }
    Vec3 so = StanceOffset();
    for (Vec3& v : m.vertices) { v = Vec3{-v.x, v.y, -v.z} * height; v += so; }   // face -Z + stance drop (see Apply)
    m.normals.clear();
    if (firstPersonArm) { BuildFpArm(m); m_fpArmReady = true; } else m_fpArmReady = false;
    mr->mesh = std::move(m);
    mr->doubleSided = true;
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
    return true;
}

void Character::RememberPartRest() {
    m_partRest.assign(m_parts.size(), Vec3{0.0f, 0.0f, 0.0f});
    for (std::size_t i = 0; i < m_parts.size(); ++i)
        if (m_parts[i] && m_parts[i]->transform) m_partRest[i] = m_parts[i]->transform->localPosition;
}

void Character::RemoveParts() {
    if (GameObject* rig = FindRig())
        if (Scene* s = GetScene()) s->Destroy(rig);   // takes its children with it
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
    for (Vec3& v : m.vertices) { v = Vec3{-v.x, v.y, -v.z} * height; v += so; }
    m.normals.clear(); mr->mesh = std::move(m); mr->doubleSided = true;
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
        mr->mesh = std::move(part); mr->doubleSided = true;
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
    const float want = (firstPersonArm && fpArmBase >= 0)
        ? (fpArmUp < 0.0f ? 0.0f : (fpArmUp > 1.0f ? 1.0f : fpArmUp)) : 0.0f;
    if (dt <= 0.0f) { m_fpShown = want; return; }
    const float k = 1.0f - std::exp(-dt * 10.0f);   // most of the way there in ~0.25 s
    m_fpShown += (want - m_fpShown) * k;
    if (std::fabs(m_fpShown - want) < 0.001f) m_fpShown = want;
}

void Character::DriveParts() {
    if (!m_partsBuilt) return;
    std::vector<Vec3> pose = CurrentSourcePose();
    // First-person: raise the visible arm into the lower corner of the view like a
    // Minecraft hand (overriding the walk pose for those three bones).
    int fpb = (firstPersonArm && fpArmBase >= 0 && fpArmBase + 2 < (int)pose.size()) ? fpArmBase : -1;
    if (fpb >= 0) {
        // The arm hangs off the hips/torso, which also carry the walk bob and the
        // crouch/prone pose. We can only steady the arm by zeroing those SHARED bones,
        // which would distort the BODY if it's visible — so we do it ONLY for the opt-in
        // "Steady arm" / "no bob" cases (meant for first person, where the body is culled
        // from the owner's own camera). The body's own crouch/prone pose is left intact.
        if (fpSteady || !fpArmBob) { pose[B_HIPS] = {0.0f, 0.0f, 0.0f}; pose[B_TORSO] = {0.0f, 0.0f, 0.0f}; }
        // Raise the arm; subtract the camera pitch so the arm follows the view up/down
        // (the rig's 180° flip maps the bone's local X to a world rotation such that
        // subtracting pitch tilts the arm the same way the camera tilts). Positive
        // fpElbow is a bend: this skeleton folds the elbow on -X, and +X hyperextends
        // the wrist. m_fpShown eases the three bones off the animated pose.
        const float w = m_fpShown < 0.0f ? 0.0f : (m_fpShown > 1.0f ? 1.0f : m_fpShown);
        // Crouch hunches the chest and prone lays the hips flat. Either swing
        // carries the only arm the owner can see around behind the sleeve.
        // The body is hidden, so drop that pitch. Hip height still follows
        // the stance, and the arm stays next to the lowered eye.
        if (w > 0.0f && (int)pose.size() > B_TORSO) {
            pose[B_HIPS].x *= (1.0f - w);
            pose[B_TORSO].x *= (1.0f - w);
        }
        const Vec3 raised{fpRaise - fpPitch, 0.0f, 0.0f};
        const Vec3 elbow{-fpElbow, 0.0f, 0.0f};
        pose[fpb]     = pose[fpb]     + (raised - pose[fpb])     * w;
        pose[fpb + 1] = pose[fpb + 1] + (elbow  - pose[fpb + 1]) * w;
        pose[fpb + 2] = pose[fpb + 2] * (1.0f - w);
    }
    // Punch swings the VISIBLE arm: the first-person arm if we're in first person,
    // otherwise the right arm. Forward (-X on a hanging bone) so it lands in front.
    // The forearm stays bent (-X); +X at the peak used to flip the wrist back.
    int pb = (fpb >= 0) ? fpb : B_RUPARM;
    if (m_punchT >= 0.0f && m_punchT < 1.0f && pb + 1 < (int)pose.size()) {
        float arc = std::sin(m_punchT * 3.14159265f);
        Vec3 up = {-110.0f, 0.0f, 8.0f}, fore = {-18.0f, 0.0f, 0.0f};
        pose[pb]     = pose[pb]     + (up   - pose[pb])     * arc;
        pose[pb + 1] = pose[pb + 1] + (fore - pose[pb + 1]) * arc;
    }
    // The whole pose is rewritten every frame, joint offsets included: IK that moved
    // a joint last frame (foot IK on a step) starts again from the animation.
    for (int bi = 0; bi < B_COUNT && bi < (int)pose.size(); ++bi)
        if (m_parts[bi] && m_parts[bi]->transform) {
            m_parts[bi]->transform->localRotation = Quat::Euler(pose[bi]);
            if (bi < (int)m_partRest.size()) m_parts[bi]->transform->localPosition = m_partRest[bi];
        }
    if (m_rigRoot && m_rigRoot->transform) {
        Vec3 so = StanceOffset();
        // The crouch eye drops further than the crouch hips. The body is hidden,
        // so drop the viewmodel arm with the eye or it rides up out of the view.
        if (fpb >= 0 && m_fpShown > 0.5f && !fpSteady && gameObject && gameObject->transform)
            for (Transform* c : gameObject->transform->Children()) {
                if (!c || !c->gameObject || !c->gameObject->GetComponent<Camera>()) continue;
                const float eye = c->localPosition.y;
                if (eye < 0.35f) break;
                if (eye > m_fpEyeRest) m_fpEyeRest = eye;
                so.y = eye - m_fpEyeRest;
                break;
            }
        // A steady first-person arm ignores the crouch/prone height drop so it stays in
        // view (the owner only sees the arm, so the body's true height is irrelevant).
        float y = (firstPersonArm && fpSteady) ? 0.0f : so.y;
        m_rigRoot->transform->localPosition = Vec3{0.0f, y, 0.0f};
        m_rigRoot->transform->localScale = Vec3{height, height, height};
    }
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
