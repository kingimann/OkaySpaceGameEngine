#include "okay/Components/Character.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Math/Mathf.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace okay {

enum { B_HIPS, B_TORSO, B_HEAD,
       B_LUPARM, B_LFORE, B_LHAND, B_RUPARM, B_RFORE, B_RHAND,
       B_LTHIGH, B_LSHIN, B_LFOOT, B_RTHIGH, B_RSHIN, B_RFOOT, B_COUNT };

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
// Rig conventions (measured on this skeleton): the rig faces +Z before the 180°
// display flip. On a bone hanging DOWN (thigh, shin, upper arm, forearm) +X swings
// its far end BACKWARD; on a bone pointing UP (torso, head) +X tips it FORWARD. So
// a knee or elbow bends naturally with shin +X / forearm -X, and +X on a foot
// points the toes down. Leg: hip joint 0.92 above the soles, knee 0.55, ankle 0.12.
namespace {
constexpr float kThigh = 0.37f, kShin = 0.43f, kHipToAnkle = 0.80f, kAnkleH = 0.12f;
constexpr float kPi = 3.14159265f, kRad2Deg = 57.2957795f;
float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
float frac(float v) { return v - std::floor(v); }
float smooth01(float u) { u = clampf(u, 0.0f, 1.0f); return u * u * (3.0f - 2.0f * u); }

// Two-bone leg IK in the hips' frame. The ankle goes `fwd` metres ahead of the hip
// joint, `down` below it and `side` out to the rig's +X; `pitch` is the foot's
// angle to the ground (deg, + = toes down). The knee always bends forward.
void LegIK(float fwd, float down, float side, float pitch, Vec3& thigh, Vec3& shin, Vec3& foot) {
    const float a = kThigh, b = kShin;
    float dz = std::sqrt(down * down + side * side);
    float abd = std::atan2(side, down);
    float d = clampf(std::sqrt(fwd * fwd + dz * dz), 0.12f, (a + b) * 0.9995f);
    float phi = std::atan2(fwd, dz);                                         // + = ankle ahead of the hip
    float alpha = std::acos(clampf((a * a + d * d - b * b) / (2.0f * a * d), -1.0f, 1.0f));
    float gamma = std::acos(clampf((a * a + b * b - d * d) / (2.0f * a * b), -1.0f, 1.0f));
    float tx = -(phi + alpha) * kRad2Deg;                                    // thigh forward (knee leads)
    float kx = (kPi - gamma) * kRad2Deg;                                     // knee flexion
    thigh = {tx, 0.0f, abd * kRad2Deg};
    shin  = {kx, 0.0f, 0.0f};
    foot  = {pitch - (tx + kx), 0.0f, 0.0f};                                 // world foot angle = pitch
}
// Ankle height above the ground for a foot pitched by `pitch` degrees with the
// toe (0.2 ahead) or heel (0.1 behind) as the contact point.
float AnkleLiftForPitch(float pitch) {
    float r = pitch * (kPi / 180.0f);
    return r >= 0.0f ? kAnkleH * std::cos(r) + 0.20f * std::sin(r) - kAnkleH
                     : kAnkleH * std::cos(r) - 0.10f * std::sin(r) - kAnkleH;
}
} // namespace

// One stride layout. Distances are in the rig's metres (feet at y = 0).
struct Character::Gait {
    float speed;     // ground speed the stride covers
    float freq;      // full cycles (two steps) per second
    float beta;      // fraction of the cycle each foot is planted (< 0.5 = flight: running)
    float reach;     // how far the planted foot travels under the hip (front to back)
    float lift;      // swing foot's peak height
    float baseDrop;  // constant hip lowering (knee softness / crouch)
    float sag;       // extra knee give at mid-stance
    bool  crouch;
};

Character::Gait Character::GaitFor(int state, float v) const {
    Gait g{};
    g.crouch = (state == 17);
    v = v < 0.0f ? 0.0f : v;
    g.speed = v;
    float maxReach;
    if (g.crouch) {
        g.freq = clampf(0.75f + 0.30f * v, 0.75f, 1.7f);
        maxReach = 0.66f;
        g.baseDrop = 0.17f;
        g.lift = clampf(0.05f + 0.025f * v, 0.05f, 0.12f);
        g.sag = 0.0f;
    } else {
        // Cadence rises with speed (people take quicker AND longer steps to go
        // faster); the reach a leg can cover caps the stance, so above a jog the
        // stance shortens below half the cycle and both feet leave the ground.
        // Walking quickens the steps fast; past a jog the cadence levels off and
        // the stride lengthens instead (people run faster by bounding farther).
        g.freq = clampf(std::fmin(0.95f + 0.30f * v, 1.45f + 0.08f * v), 0.95f, 2.2f);
        maxReach = 0.72f;
        float vc = v < 8.0f ? v : 8.0f;
        g.baseDrop = 0.012f + 0.006f * vc;
        g.lift = clampf(0.05f + 0.04f * v, 0.06f, 0.32f);
        g.sag = 0.008f * vc;
    }
    float stride = g.freq > 1e-4f ? v / g.freq : 0.0f;    // ground covered per cycle
    g.beta  = stride > 1e-4f ? clampf(maxReach / stride, 0.24f, 0.62f) : 0.62f;
    g.reach = stride * g.beta;
    return g;
}

float Character::GaitSpeed(int state) const {
    if (m_gaitSpeed >= 0.0f) return m_gaitSpeed;
    return state == 3 ? 4.8f : (state == 17 ? 1.0f : (state == 7 ? 0.8f : 1.5f));   // natural pace (matches NominalFreq)
}

// The cycle clock: animTime runs at this many cycles per second for a state, and
// Update speeds it up or slows it down to the real cadence for the reported speed,
// so changing speed mid-stride never jumps the phase.
float Character::NominalFreq(int state) const {
    return GaitFor(state, state == 3 ? 4.8f : (state == 17 ? 1.0f : (state == 7 ? 0.8f : 1.5f))).freq;
}

float Character::GaitPhase() const {
    if (anim != 2 && anim != 3 && anim != 17) return 0.0f;
    return frac(animTime * NominalFreq(anim));
}

namespace {
struct FootState { float fwd, lift, pitch; bool planted; float u; };
// Where one foot is at cycle phase p (0 = touchdown).
FootState FootAt(float p, float beta, float reach, float lift, float speed, bool crouch) {
    FootState f{};
    p = frac(p);
    if (p < beta) {                                     // planted: slides back under the hip
        float u = p / beta;
        f.planted = true; f.u = u;
        f.fwd = reach * (0.5f - u);
        float heelOff = crouch ? 0.0f : clampf((u - 0.65f) / 0.35f, 0.0f, 1.0f);
        // Walkers land heel first (toes up), runners on the forefoot (toes down).
        float strike = crouch ? 0.0f : clampf(1.0f - u / 0.2f, 0.0f, 1.0f);
        float strikePitch = speed < 3.0f ? -9.0f : 12.0f;
        f.pitch = (14.0f + 2.5f * clampf(speed, 0.0f, 6.0f)) * heelOff * heelOff + strikePitch * strike * strike;
        f.lift = AnkleLiftForPitch(f.pitch);
    } else {                                            // swinging forward to the next step
        float u = (p - beta) / (1.0f - beta);
        f.planted = false; f.u = u;
        // The heel folds up behind first (toward the seat when running), then the
        // knee brings the foot through and the leg reaches forward to land.
        float k = clampf((speed - 2.0f) / 4.0f, 0.0f, 1.0f);    // 0 walk .. 1 run
        f.fwd = reach * (smooth01(std::pow(u, 1.0f + 0.35f * k)) - 0.5f);
        f.lift = lift * std::sin(kPi * std::pow(u, 0.75f - 0.2f * k));
        float off = crouch ? 0.0f : (14.0f + 2.5f * clampf(speed, 0.0f, 6.0f));
        float land = crouch ? 0.0f : (speed < 3.0f ? -9.0f : 12.0f);
        f.pitch = off * (1.0f - u) * (1.0f - u) + land * u * u;
        f.lift += AnkleLiftForPitch(f.pitch);   // same rule as stance: no seam at lift-off / touch-down
    }
    return f;
}
} // namespace

float Character::GaitHipY(float t, int state) const {
    Gait g = GaitFor(state, GaitSpeed(state));
    float ph = frac(t * NominalFreq(state));
    const float maxLeg = kHipToAnkle * 0.985f;
    // How far the hips must come down for a planted foot to reach the ground.
    auto need = [&](const FootState& f) {
        float n = kHipToAnkle - std::sqrt(std::fmax(0.0f, maxLeg * maxLeg - f.fwd * f.fwd)) - f.lift;
        return n + g.sag * std::sin(kPi * f.u);
    };
    float drop = -1.0f;
    for (int leg = 0; leg < 2; ++leg) {
        FootState f = FootAt(ph + 0.5f * leg, g.beta, g.reach, g.lift, g.speed, g.crouch);
        if (f.planted) drop = std::fmax(drop, need(f));
    }
    if (drop < -0.5f) {
        // Both feet off the ground (running): a short ballistic arc that leaves
        // exactly where the last foot pushed off and lands exactly where the next
        // one touches down, so the hips never jump and the feet never dip.
        float s = frac(ph) - g.beta; if (s >= 0.5f) s -= 0.5f;     // cycles into this flight
        float flightCycles = 0.5f - g.beta;
        float w = clampf(s / flightCycles, 0.0f, 1.0f);
        // (Each foot in its own phase: push-off ends its stance at beta, touch-down starts one at 0.)
        FootState off = FootAt(g.beta - 1e-4f, g.beta, g.reach, g.lift, g.speed, g.crouch);
        FootState on  = FootAt(1e-4f, g.beta, g.reach, g.lift, g.speed, g.crouch);
        float a = need(off), b = need(on);
        float flight = flightCycles / g.freq;                        // seconds
        float rise = 0.5f * 9.81f * (0.25f * flight * flight) * 4.0f * w * (1.0f - w);
        drop = a + (b - a) * w - rise;
    }
    return -(g.baseDrop + drop);
}

void Character::GaitPose(std::vector<Vec3>& r, float t, int state) const {
    Gait g = GaitFor(state, GaitSpeed(state));
    float ph = frac(t * NominalFreq(state));
    float hipY = GaitHipY(t, state) - 0.12f * m_landK;
    float down = kHipToAnkle + hipY;                       // hip joint to ankle, at rest on the ground
    FootState fl = FootAt(ph, g.beta, g.reach, g.lift, g.speed, g.crouch);
    FootState fr = FootAt(ph + 0.5f, g.beta, g.reach, g.lift, g.speed, g.crouch);
    float half = g.reach > 1e-4f ? 0.5f * g.reach : 1.0f;
    float sl = clampf(fl.fwd / half, -1.0f, 1.0f), sr = clampf(fr.fwd / half, -1.0f, 1.0f);
    float v = g.speed;
    float twist = g.crouch ? 3.0f : clampf(3.0f + 1.0f * v, 3.0f, 9.0f);
    // Running feet land closer to the centre line.
    float narrow = g.crouch ? 0.0f : clampf((g.speed - 2.0f) * 0.012f, 0.0f, 0.05f);
    // Feet are placed in the travel frame, then brought into the twisting pelvis's
    // frame: the hip joints absorb the pelvis turn, so a planted foot stays put.
    float tw = twist * sl * (kPi / 180.0f), c = std::cos(tw), sn = std::sin(tw);
    // Past the hips' turn limit the rest of the travel angle is a side-step: the
    // stride runs along the real travel direction (+ = the body's right = rig -X).
    float rho = (m_travelYaw - m_lowerYaw) * (kPi / 180.0f), rs = std::sin(rho), rc = std::cos(rho);
    auto place = [&](const FootState& f, float hipX, float side, Vec3& th, Vec3& sh, Vec3& ft) {
        float x = hipX + side - f.fwd * rs, z = f.fwd * rc;  // foot, relative to the pelvis centre
        float lx = x * c - z * sn, lz = x * sn + z * c;      // in the pelvis frame
        LegIK(lz, down - f.lift, lx - hipX, f.pitch, th, sh, ft);
    };
    place(fl, -0.105f,  narrow, r[B_LTHIGH], r[B_LSHIN], r[B_LFOOT]);
    place(fr,  0.105f, -narrow, r[B_RTHIGH], r[B_RSHIN], r[B_RFOOT]);

    // Arms swing against the legs (left arm back while the left foot is forward),
    // wider and with more elbow bend the faster you go.
    float armA = g.crouch ? 8.0f + 5.0f * v : clampf(8.0f + 6.0f * v, 10.0f, 55.0f);
    float elbow = g.crouch ? 30.0f : clampf(12.0f + 10.0f * v, 12.0f, 95.0f);
    float lArm = armA * sl, rArm = armA * sr;
    float base = g.crouch ? -18.0f : 0.0f;                 // crouched: hands carried in front
    r[B_LUPARM] = {base + lArm, 0.0f,  6.0f};
    r[B_RUPARM] = {base + rArm, 0.0f, -6.0f};
    r[B_LFORE]  = {-(elbow + 0.45f * std::fmax(0.0f, -lArm)), 0.0f, 0.0f};
    r[B_RFORE]  = {-(elbow + 0.45f * std::fmax(0.0f, -rArm)), 0.0f, 0.0f};

    // Pelvis turns with the stride, shoulders counter-turn; the chest leans into speed
    // and the head stays level.
    float lean  = g.crouch ? 28.0f : clampf(2.0f + 1.5f * v, 2.0f, 15.0f);
    r[B_HIPS]  = {0.0f, twist * sl - m_lowerYaw, 0.0f};
    r[B_TORSO] = {lean + 9.0f * m_landK, -1.8f * twist * sl + m_lowerYaw, 0.0f};
    r[B_HEAD]  = {-0.65f * lean, 0.8f * twist * sl, 0.0f};
}

// Advance the built-in animation clock. Walk / run / crouch-walk run at the
// cadence their reported ground speed calls for (and backwards when the body
// backpedals), so a stride always covers exactly the ground the body moved.
void Character::AdvanceAnimClock(float dt) {
    const bool gait = (anim == 2 || anim == 3 || anim == 17);
    float target = -1.0f;
    if (moveSpeed >= 0.0f) {
        float sc = 1.0f;   // a scaled-up body takes proportionally longer strides
        if (gameObject && gameObject->transform) {
            Vec3 ls = gameObject->transform->LossyScale();
            sc = 0.5f * (std::fabs(ls.x) + std::fabs(ls.z));
            if (sc < 1e-3f) sc = 1.0f;
        }
        target = moveSpeed / sc;
    }
    if (target < 0.0f)          m_gaitSpeed = -1.0f;
    else if (m_gaitSpeed < 0.0f) m_gaitSpeed = target;
    else                         m_gaitSpeed += (target - m_gaitSpeed) * (1.0f - std::exp(-12.0f * dt));

    // Travel direction relative to the facing: the hips and legs turn toward it
    // (up to 75°, the chest stays forward); pointing behind, they step backwards.
    float a = std::fmod(moveAngle + 540.0f, 360.0f) - 180.0f;
    float limit = m_gaitDir < 0.0f ? 80.0f : 100.0f;            // hysteresis around the side
    m_gaitDir = std::fabs(a) > limit ? -1.0f : 1.0f;
    float yaw = m_gaitDir < 0.0f ? (a > 0.0f ? a - 180.0f : a + 180.0f) : a;
    if (!gait || m_gaitSpeed < 0.1f) yaw = 0.0f;
    float ek = 1.0f - std::exp(-10.0f * dt);
    m_travelYaw += (yaw - m_travelYaw) * ek;                    // where the feet actually step
    m_lowerYaw  += (clampf(yaw, -75.0f, 75.0f) - m_lowerYaw) * ek;   // how far the hips turn

    float rate = animSpeed;
    if (gait) {
        float nf = NominalFreq(anim);
        float f = GaitFor(anim, GaitSpeed(anim)).freq;
        rate *= (nf > 1e-4f ? f / nf : 1.0f) * m_gaitDir;
    } else if (anim == 7 && m_gaitSpeed >= 0.0f) {
        rate *= m_gaitSpeed / 0.8f;                              // crawl cycle follows the distance
    }
    animTime += dt * rate;
}

void Character::DriveLocomotion(const Locomotion& l) {
    // Measure what the body really did: a controller sets the velocity it WANTS
    // before physics runs, so pressed against a wall the velocity still says
    // "moving" while the body stands still. The position change is the truth
    // (one frame behind; a teleport/respawn jump is ignored).
    Vec3 vel = l.velocity;
    if (gameObject && gameObject->transform && l.dt > 1e-5f) {
        Vec3 pos = gameObject->transform->Position();
        if (m_havePos) {
            Vec3 d = pos - m_lastPos;
            const float al = l.ignoreAxis.Magnitude();
            if (al > 1e-5f) { Vec3 a = l.ignoreAxis * (1.0f / al); d = d - a * Vec3::Dot(d, a); }
            if (d.Magnitude() < 2.0f) vel = d * (1.0f / l.dt);
        }
        m_lastPos = pos; m_havePos = true;
    }
    // On the ground: a fresh contact, or solid ground just under the feet (contact
    // callbacks stop for a body at rest, and stepping off a curb shouldn't count).
    bool ground = l.onGround;
    if (!ground && !l.jumped && gameObject && gameObject->transform && gameObject->scene()) {
        Scene* sc = gameObject->scene();
        Vec3 o = gameObject->transform->Position();
        RaycastHit3D h = sc->physics3D().Raycast(*sc, o + Vec3{0.0f, 0.15f, 0.0f}, Vec3{0.0f, -1.0f, 0.0f}, 0.45f,
                                                 gameObject, Physics3D::kAllLayers, false);
        ground = h.hit && vel.y < 1.0f;
    }
    Vec3 v{vel.x, 0.0f, vel.z};
    float hs = std::sqrt(v.x * v.x + v.z * v.z);
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
    const bool wasMoving = anim == 2 || anim == 3 || anim == 17;
    const bool moving = hs > (wasMoving ? 0.2f : 0.35f);       // hysteresis: no idle/walk flicker
    if (l.prone)                   anim = 7;
    else if (l.jumped)             anim = 5;
    else if (m_airT > 0.12f)       anim = l.velocity.y > 0.4f ? 5 : 18;   // short drops keep the stride
    else if (l.crouch)             anim = moving ? 17 : 6;
    else if (!moving)              anim = 1;
    else {
        float runAt = l.walkSpeed * (anim == 3 ? 1.05f : 1.15f);
        anim = (hs > runAt || (l.sprint && hs > l.walkSpeed * 0.9f)) ? 3 : 2;
    }
}

std::vector<Vec3> Character::PoseAt(float t) const {
    std::vector<Vec3> r(B_COUNT, Vec3{0, 0, 0});
    if (anim == 0) {
        for (int i = 0; i < B_COUNT && i < (int)pose.size(); ++i) r[i] = pose[i];
        return r;
    }
    if (anim == 1) {                       // idle
        float s = std::sin(t * 1.8f);
        r[B_TORSO] = {1.5f * s, 0, 0};
        r[B_LUPARM] = {0, 0, 4 + 1.5f * s};
        r[B_RUPARM] = {0, 0, -4 - 1.5f * s};
        // Idle fidgets: a slow side-to-side weight shift (hips roll one way, the
        // torso counter-rolls to stay balanced) plus an occasional glance to one
        // side — alternating sides each ~9s cycle — so a standing character reads
        // as alive rather than a breathing statue.
        float sway = std::sin(t * 0.45f);
        r[B_HIPS].z  += 2.2f * sway;
        r[B_TORSO].z -= 1.4f * sway;
        float cyc = std::fmod(t, 9.0f);
        if (cyc > 6.0f && cyc < 8.0f) {
            float e = std::sin((cyc - 6.0f) * 0.5f * 3.14159265f);   // ease 0..1..0
            float side = std::fmod(t / 9.0f, 2.0f) < 1.0f ? 1.0f : -1.0f;
            r[B_HEAD].y += side * 22.0f * e;
            r[B_HEAD].x += 2.0f * e;                                 // slight tilt with it
        }
    } else if (anim == 2 || anim == 3) {   // walk / run: a stride laid out from the real ground speed
        GaitPose(r, t, anim);
    } else if (anim == 4) {                // wave
        r[B_RUPARM] = {0, 0, -150};
        r[B_RFORE]  = {0, 0, -15 + 28 * std::sin(t * 8.0f)};
        r[B_LUPARM] = {0, 0, 6};
    } else if (anim == 5) {                // jump: lead knee driven up, trail leg pushing off, arms swing up
        LegIK( 0.26f, 0.42f, 0.0f, 10.0f, r[B_LTHIGH], r[B_LSHIN], r[B_LFOOT]);   // lead leg tucked
        LegIK(-0.16f, 0.72f, 0.0f, 35.0f, r[B_RTHIGH], r[B_RSHIN], r[B_RFOOT]);   // trail leg, toes pointed
        r[B_RUPARM] = {-48, 0, -10};   r[B_RFORE] = {-62, 0, 0};    // opposite arm drives forward-up, elbow bent
        r[B_LUPARM] = {28, 0, 12};     r[B_LFORE] = {-18, 0, 0};    // the other swings back
        r[B_TORSO]  = {6, 4, 0};
        r[B_HEAD]   = {-6, 0, 0};
    } else if (anim == 18) {               // fall: legs reach down for the landing, arms out for balance
        LegIK( 0.10f, 0.66f, 0.03f, -4.0f, r[B_LTHIGH], r[B_LSHIN], r[B_LFOOT]);
        LegIK(-0.02f, 0.73f, -0.03f, 4.0f, r[B_RTHIGH], r[B_RSHIN], r[B_RFOOT]);
        r[B_LUPARM] = {-12, 0, 58};    r[B_RUPARM] = {-12, 0, -58};
        r[B_LFORE]  = {-22, 0, 0};     r[B_RFORE]  = {-22, 0, 0};
        r[B_TORSO]  = {4, 0, 0};
        r[B_HEAD]   = {8, 0, 0};       // eyes on the ground coming up
    } else if (anim == 19) {               // hang: arms straight up to the ledge, legs hanging loose
        r[B_LUPARM] = {-170, 0, 6};    r[B_RUPARM] = {-170, 0, -6};
        r[B_LFORE]  = {-8, 0, 0};      r[B_RFORE]  = {-8, 0, 0};
        float s = std::sin(t * 1.6f);  // a slight sway
        LegIK( 0.04f + 0.02f * s, 0.76f, 0.02f, 25.0f, r[B_LTHIGH], r[B_LSHIN], r[B_LFOOT]);
        LegIK(-0.02f + 0.02f * s, 0.77f, -0.02f, 25.0f, r[B_RTHIGH], r[B_RSHIN], r[B_RFOOT]);
        r[B_HEAD]   = {-14, 0, 0};     // looking up at the hands
    } else if (anim == 6) {                // crouch: hips back and down, knees forward over the toes, chest low
        float s = std::sin(t * 2.2f);      // breathing
        const float down = kHipToAnkle - 0.17f;
        LegIK( 0.12f, down, 0.03f, 0.0f, r[B_LTHIGH], r[B_LSHIN], r[B_LFOOT]);
        LegIK( 0.04f, down, -0.03f, 0.0f, r[B_RTHIGH], r[B_RSHIN], r[B_RFOOT]);   // one foot a little behind
        r[B_TORSO]  = {28 + 1.5f * s, 0, 0};
        r[B_HEAD]   = {-20, 0, 0};                           // eyes forward, not at the floor
        r[B_LUPARM] = {-20, 0, 8};   r[B_RUPARM] = {-20, 0, -8};   // hands carried in front
        r[B_LFORE]  = {-32, 0, 0};   r[B_RFORE]  = {-32, 0, 0};
    } else if (anim == 17) {               // crouch-walk: the same crouch, stepping heel-to-toe
        GaitPose(r, t, 17);
    } else if (anim == 7) {                // prone (whole body flat on the ground)
        // Rotate ONLY the hips 90° so the body lies flat: the torso/head swing
        // forward along the ground and the legs swing straight back — no counter
        // rotation on the legs (that was what stood it back up). StanceOffset()
        // then drops the whole body down onto the floor.
        r[B_HIPS]   = {90, 0, 0};
        r[B_HEAD]   = {-52, 0, 0};         // lift the head to look ahead
        // Lying down, the arms reach forward past the head (-X swings an arm
        // forward; -90 would point it at the ground) with the forearms flat on the
        // ground in front, elbows near the shoulders.
        r[B_LUPARM] = {-158, 0, 12};  r[B_RUPARM] = {-158, 0, -12};
        r[B_LFORE]  = {-20, 0, 0};    r[B_RFORE]  = {-20, 0, 0};
        r[B_LTHIGH] = {0, 0, -6};     r[B_RTHIGH] = {0, 0, 6};      // legs a little apart
        // Crawling: when the body moves, the elbows reach and pull in turn and the
        // opposite knee drags up along the ground (distance-driven like the walk).
        if (m_gaitSpeed > 0.15f) {
            float c = std::sin(t * NominalFreq(7) * 2.0f * kPi);
            float kl = std::fmax(0.0f, -c), kr = std::fmax(0.0f, c);
            r[B_LUPARM].x += 20.0f * c;   r[B_RUPARM].x -= 20.0f * c;
            r[B_LFORE].x  -= 25.0f * kr;  r[B_RFORE].x  -= 25.0f * kl;
            r[B_LTHIGH] = {0, 0, -(6.0f + 24.0f * kl)};   r[B_LSHIN] = {0, 0,  34.0f * kl};   // knee out, foot back in
            r[B_RTHIGH] = {0, 0,  (6.0f + 24.0f * kr)};   r[B_RSHIN] = {0, 0, -34.0f * kr};
            r[B_TORSO].y = 4.0f * c;
        }
    }
    // ---- Hand gestures (8-12) and emotions (13-16) -----------------------------
    // The rig is blocky with no face, so "emotions" read through body language
    // (posture, head tilt, bounce). +X on an upper arm swings it forward, +X on a
    // forearm bends the elbow up, +Z spreads the LEFT arm out (−Z the right);
    // arm Z is kept well under 90° so the hands never cross in front.
    else if (anim == 8) {                  // point forward (right arm)
        r[B_RUPARM] = {88, 0, -8};         // raise to horizontal, pointing ahead
        r[B_RFORE]  = {2, 0, 0};           // arm straight
        r[B_LUPARM] = {0, 0, 4};           // left arm rests
        r[B_TORSO]  = {0, -6, 0};          // slight turn into the point
    } else if (anim == 9) {                // clap (hands meet in front, repeating)
        float c = 18.0f * (0.5f + 0.5f * std::sin(t * 7.0f));   // 0..18 open/close
        r[B_LUPARM] = {62, 0, -22 + c};    r[B_RUPARM] = {62, 0, 22 - c};
        r[B_LFORE]  = {46, 0, 18};         r[B_RFORE]  = {46, 0, -18};
        r[B_HEAD]   = {-4, 0, 0};
    } else if (anim == 10) {               // thumbs up (right fist up at the side)
        r[B_RUPARM] = {6, 0, -18};
        r[B_RFORE]  = {128, 0, -6};        // forearm up, fist by the chest
        r[B_LUPARM] = {0, 0, 4};
    } else if (anim == 11) {               // salute (right hand to the brow)
        r[B_RUPARM] = {26, 0, -74};        // upper arm out to the side
        r[B_RFORE]  = {128, 0, -28};       // forearm folded up to the forehead
        r[B_HEAD]   = {-6, 0, 0};
        r[B_TORSO]  = {-2, 0, 0};          // stand tall
    } else if (anim == 12) {               // wave-both / surrender-style open hands
        float s2 = std::sin(t * 6.0f);
        r[B_LUPARM] = {0, 0, 120};         r[B_RUPARM] = {0, 0, -120};   // both arms up & out
        r[B_LFORE]  = {0, 0, 20 + 14 * s2}; r[B_RFORE] = {0, 0, -20 - 14 * s2};
    } else if (anim == 13) {               // happy / cheer (arms up, bouncing)
        float b = std::sin(t * 6.0f);
        r[B_LUPARM] = {-14, 0, 82};        r[B_RUPARM] = {-14, 0, -82};  // raised in a V
        r[B_LFORE]  = {18, 0, 0};          r[B_RFORE]  = {18, 0, 0};
        r[B_HEAD]   = {-12, 0, 0};         // chin up, upbeat
        r[B_TORSO]  = {-4 + 2.0f * b, 0, 0};
    } else if (anim == 14) {               // sad / dejected (slumped, head down)
        float b = std::sin(t * 1.4f);
        r[B_TORSO]  = {20 + 1.0f * b, 0, 0};   // slump forward
        r[B_HEAD]   = {26, 0, 0};              // look down
        r[B_LUPARM] = {12, 0, 2};          r[B_RUPARM] = {12, 0, -2};    // arms hang limp, slightly fwd
    } else if (anim == 15) {               // angry (lean in, fists up)
        float b = std::sin(t * 5.0f);
        r[B_TORSO]  = {14, 1.5f * b, 0};       // hunch + small aggressive sway
        r[B_HEAD]   = {12, 0, 0};              // head down/forward (glaring)
        r[B_LUPARM] = {28, 0, -14};        r[B_RUPARM] = {28, 0, 14};    // elbows in
        r[B_LFORE]  = {72, 0, 0};          r[B_RFORE]  = {72, 0, 0};     // fists up
    } else if (anim == 16) {               // think / idle-curious (hand near chin)
        r[B_RUPARM] = {30, 0, -26};
        r[B_RFORE]  = {118, 0, -10};       // hand up to the chin
        r[B_HEAD]   = {-4, 14, 0};         // slight quizzical tilt
        r[B_LUPARM] = {6, 0, 6};
    }

    // Landing recovery: a brief knee-bend absorb right after a jump lands — hips
    // drop, knees bend forward, chest dips, arms out for balance — springing back
    // over ~0.3s. Walk/run fold it into their own stride (GaitPose).
    if (m_landK > 0.0f && anim == 1) {
        float lk = m_landK;
        const float down = kHipToAnkle - 0.12f * lk;
        LegIK(0.03f, down, 0.0f, 0.0f, r[B_LTHIGH], r[B_LSHIN], r[B_LFOOT]);
        LegIK(0.03f, down, 0.0f, 0.0f, r[B_RTHIGH], r[B_RSHIN], r[B_RFOOT]);
        r[B_TORSO].x  += 12.0f * lk;
        r[B_LUPARM].z += 14.0f * lk;   r[B_RUPARM].z -= 14.0f * lk;   // arms out for balance
    }
    if (m_landK > 0.0f && (anim == 2 || anim == 3)) {
        r[B_LUPARM].z += 10.0f * m_landK;   r[B_RUPARM].z -= 10.0f * m_landK;
    }

    // Head look: layer the (eased) gaze on top of whatever the animation set, so the
    // head turns and tilts toward where the player is looking. Clamped so the neck
    // never breaks. (The body is flipped 180° about Y in Apply(), which negates the
    // pitch sense — hence the minus on X.)
    if (anim != 0) {
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
    return r;
}

Vec3 Character::StanceOffset() const {
    if (m_editorPosing) return m_editorOffset;        // a previewed pose carries its own
    Vec3 o = RawStanceOffset(animTime);
    if (m_blendT < 1.0f && !m_blendFrom.empty()) {   // ease the hip height with the pose crossfade
        float w = m_blendT * m_blendT * (3.0f - 2.0f * m_blendT);
        o.y = m_blendFromY + (o.y - m_blendFromY) * w;
    }
    return o;
}

Vec3 Character::RawStanceOffset(float t) const {
    // Walk / run / crouch-walk: the hips ride the stride (lower while a planted
    // foot is far ahead or behind, a small rise in a running flight) so the legs
    // reach the ground instead of the feet floating. Rig units scale with height.
    if (anim == 2 || anim == 3 || anim == 17)
        return {0.0f, (GaitHipY(t, anim) - 0.12f * m_landK) * height, 0.0f};
    if (anim == 6) return {0.0f, -0.17f * height, 0.0f};   // crouch: hips down, knees bent
    if (anim == 7) return {0.0f, -0.78f, 0.0f};   // prone: lay the body on the ground
    // Landing recovery: the flexed knees momentarily lower the whole body.
    if (m_landK > 0.0f && anim == 1)
        return {0.0f, -0.12f * m_landK * height, 0.0f};
    return {0.0f, 0.0f, 0.0f};
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
    for (Vec3& v : m.vertices) { v.y *= height; v.x = -v.x; v.z = -v.z; v += so; }
    m.normals.clear();                     // boxy -> flat shading
    mr->mesh = std::move(m);
    mr->doubleSided = true;
}

void Character::AddClip(AnimClip clip) {
    std::string name = clip.name;
    m_clips[name] = std::move(clip);
    if (m_activeClipName == name) m_activeClip = &m_clips[name];  // keep pointer valid on replace
}

int Character::LoadClips(const std::string& text, std::string* error) {
    auto clips = AnimClip::ParseAll(text, &Character::BoneIndex, B_COUNT, error);
    for (auto& c : clips) AddClip(std::move(c));
    return (int)clips.size();
}

bool Character::LoadClipsFromFile(const std::string& path, std::string* error) {
    std::ifstream f(path);
    if (!f) { if (error) *error = "cannot open " + path; return false; }
    std::stringstream ss; ss << f.rdbuf();
    return LoadClips(ss.str(), error) > 0;
}

bool Character::SaveClipsToFile(const std::string& path, std::string* error) const {
    std::vector<AnimClip> clips;
    clips.reserve(m_clips.size());
    for (const auto& kv : m_clips) clips.push_back(kv.second);
    std::string text = AnimClip::WriteAll(clips, [](int i) { return std::string(BoneName(i)); });
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
    // No-code: when the user has bound a clip to a movement state, make that clip the
    // active one (so YOUR animation plays instead of the built-in for that state).
    if (clipIdle.empty() && clipWalk.empty() && clipRun.empty()) return;
    if (m_blendTreeOn) return;    // the locomotion blend tree owns the base pose
    if (m_editorPosing) return;   // editor preview owns the pose
    const std::string* want = nullptr;
    if      (anim == 1 && !clipIdle.empty()) want = &clipIdle;
    else if (anim == 2 && !clipWalk.empty()) want = &clipWalk;
    else if (anim == 3 && !clipRun.empty())  want = &clipRun;
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

void Character::Update(float dt) {
    // Landing recovery: coming out of the jump pose into any grounded state plays a
    // brief knee-bend "absorb" (sin arc in PoseAt/StanceOffset) so a landing has
    // weight to it instead of the legs snapping straight the frame you touch down.
    if (anim != m_prevAnim) {
        if ((m_prevAnim == 5 || m_prevAnim == 18) && (anim == 1 || anim == 2 || anim == 3)) m_landT = 0.0f;
        // Crossfade between the built-in animations too (idle -> run -> crouch ...),
        // not just authored clips: snapshot the pose of the state we're leaving so
        // a controller switching states doesn't snap the whole body in one frame.
        if (!m_activeClip && !m_blendTreeOn && !m_editorPosing && blendTime > 1e-4f &&
            m_prevAnim != 0 && anim != 0) {
            const int now = anim;
            anim = m_prevAnim;
            m_blendFrom = CurrentSourcePose();
            m_blendFromY = RawStanceOffset(animTime).y;   // the hips ease too, or the feet dip through the floor
            anim = now;
            m_blendT = 0.0f;
        }
        m_prevAnim = anim;
    }
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
            if (animateParts || m_editorPosing || m_activeClip) DriveParts();   // preview / clip still drive
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
            for (Vec3& v : m.vertices) { v.y *= height; v.x = -v.x; v.z = -v.z; v += m_editorOffset; }
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
        for (Vec3& v : m.vertices) { v.y *= height; v.x = -v.x; v.z = -v.z; }  // face -Z (see Apply)
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
    if (anim == 0) return;
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
    auto* mr = gameObject ? gameObject->GetComponent<MeshRenderer>() : nullptr;
    if (!mr) return;
    EnsureRest();
    Mesh m = m_rest;
    std::vector<Vec3> pose = CurrentSourcePose();   // built-in anim, crossfade-aware
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
    Vec3 so = StanceOffset();
    for (Vec3& v : m.vertices) { v.y *= height; v.x = -v.x; v.z = -v.z; v += so; }   // face -Z + stance drop (see Apply)
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
        if (m_partsBuilt) DriveParts();      // push the (preview/rest) pose onto the rig
        return;
    }
    // Single mesh: rebuild from the previewed pose so the editor shows it live.
    auto* mr = gameObject ? gameObject->GetComponent<MeshRenderer>() : nullptr;
    if (!mr) return;
    EnsureRest();
    Mesh m = m_rest;
    Skin(m, m_bone, CurrentSourcePose());
    Vec3 so = StanceOffset();
    for (Vec3& v : m.vertices) { v.y *= height; v.x = -v.x; v.z = -v.z; v += so; }
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
    root->transform->localScale = Vec3{1.0f, height, 1.0f};
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
    if (m_editorPosing)                       { p = m_editorPose; p.resize(B_COUNT, Vec3{0, 0, 0}); }
    else if (m_activeClip && !m_stateDriven)  { p = m_activeClip->Sample(m_clipTime); p.resize(B_COUNT, Vec3{0, 0, 0}); }  // manual clip wins
    else if (m_blendTreeOn)                   p = SampleBlendTree();          // locomotion blend tree
    else if (m_activeClip)                    { p = m_activeClip->Sample(m_clipTime); p.resize(B_COUNT, Vec3{0, 0, 0}); }  // state clip
    else                                      p = PoseAt(animTime);
    // Crossfade from the pose captured at the last clip switch (smoothstep weight).
    if (m_blendT < 1.0f && m_blendFrom.size() == p.size() && !p.empty()) {
        float w = m_blendT * m_blendT * (3.0f - 2.0f * m_blendT);
        for (std::size_t i = 0; i < p.size(); ++i) p[i] = m_blendFrom[i] + (p[i] - m_blendFrom[i]) * w;
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
        // subtracting pitch tilts the arm the same way the camera tilts).
        pose[fpb]     = {fpRaise - fpPitch, 0.0f, 0.0f};   // upper arm: into frame + follow look
        pose[fpb + 1] = {fpElbow, 0.0f, 0.0f};             // forearm bend (hand height)
        pose[fpb + 2] = {0.0f, 0.0f, 0.0f};
    }
    // Punch swings the VISIBLE arm: the first-person arm if we're in first person,
    // otherwise the right arm. Forward (-Z, the body's facing) so it lands in front.
    int pb = (fpb >= 0) ? fpb : B_RUPARM;
    if (m_punchT >= 0.0f && m_punchT < 1.0f && pb + 1 < (int)pose.size()) {
        float arc = std::sin(m_punchT * 3.14159265f);
        Vec3 up = {-110.0f, 0.0f, 8.0f}, fore = {18.0f, 0.0f, 0.0f};
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
        // A steady first-person arm ignores the crouch/prone height drop so it stays in
        // view (the owner only sees the arm, so the body's true height is irrelevant).
        float y = (firstPersonArm && fpSteady) ? 0.0f : so.y;
        m_rigRoot->transform->localPosition = Vec3{0.0f, y, 0.0f};
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
      << (clipsFile.empty() ? "-" : clipsFile) << ' '
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
    if (in >> cf && cf != "-") clipsFile = cf;
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
    m_built = false;
}

} // namespace okay
