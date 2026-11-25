#include "okay/Core/Profiler.hpp"
#include "CharacterFirstPersonInternal.hpp"
#include "CharacterInternal.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/NetworkSync.hpp"
#include "okay/Animation/HumanoidMotion.hpp"
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/ProceduralObjectHolding.hpp"
#include "okay/Components/FirstPersonHands.hpp"
#include "okay/Components/HoldableObject.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/PlayerView.hpp"
#include "okay/Components/FirstPersonController.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Input/GameInput.hpp"
#include "okay/Core/Game.hpp"
#include "okay/Components/FootIK.hpp"
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
using character_fp::OwnerCamera;
using namespace character_rig;

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

namespace character_rig {
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
} // namespace character_rig

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


void Character::Update(float dt) {
    OKAY_PROFILE("Anim.Character");
    TickFeel(dt);
    NoteStateChange();
    if (m_landT >= 0.0f) {
        m_landT += dt / 0.28f;                       // ~0.28s dip-and-spring
        if (m_landT >= 1.0f) { m_landT = -1.0f; m_landK = 0.0f; }
        else m_landK = std::sin(3.14159265f * m_landT);
    }
    SyncStateClips();
    // Advance any in-progress crossfade (covers every render path below).
    if (m_blendT < 1.0f) {
        const float dur = m_blendDur > 1e-4f ? m_blendDur : blendTime;
        m_blendT += dur > 1e-4f ? dt / dur : 1.0f;
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
    // A remote copy already received the owner's look. Tracking the local camera
    // would fight that and jitter the head.
    bool remoteLook = false;
    for (Transform* t = gameObject ? gameObject->transform : nullptr; t && !remoteLook; t = t->Parent())
        if (t->gameObject)
            if (auto* ns = t->gameObject->GetComponent<NetworkSync>())
                remoteLook = ns->syncAnimation && !ns->IsLocallyOwned();
    if ((lookAtCamera || !lookAtTarget.empty()) && !remoteLook && gameObject && gameObject->transform) {
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
    OKAY_PROFILE("Anim.Character");
    // Turning the Animator component back on does not restart it, so the stale
    // clip would pop on. The first time we see it stays instant (play start).
    // This runs after every Update, so it doesn't matter which component ticked first.
    if (m_poseSource) {
        const bool on = m_poseSource->AnimatorOn();
        if (!m_animSeen) { m_animSeen = true; m_animOn = on; }
        else if (on && !m_animOn) m_resumeAnim = true;
        m_animOn = on;
    } else m_animSeen = false;
    if (m_resumeAnim && m_poseSource) {
        Vec3 root = RawStanceOffset(animTime);
        if (height > 1e-3f) root = root * (1.0f / height);
        m_poseSource->ResumeFrom(PoseAt(animTime), root, CrossfadeSeconds(1, 2));
        m_resumeAnim = false;
    }
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

void Character::RememberPartRest() {
    const std::vector<Bone> sk = Skeleton();
    m_partRest.assign(m_parts.size(), Vec3{0.0f, 0.0f, 0.0f});
    for (std::size_t i = 0; i < m_parts.size(); ++i) {
        if (!m_parts[i] || !m_parts[i]->transform) continue;
        const Vec3 live = m_parts[i]->transform->localPosition;
        Vec3 bind = live;
        if ((int)i < B_COUNT && (int)i < (int)sk.size()) {
            const int par = sk[i].parent;
            const Vec3 pj = par >= 0 ? sk[par].joint : Vec3{0.0f, 0.0f, 0.0f};
            bind = sk[i].joint - pj;
        }
        // A scene saved mid-play stores the animated bone. The joint offset of
        // the rig is the skeleton's rest, not a foot planted on a step. A part
        // still sitting at its rest rotation keeps an edit-mode position tweak.
        const Quat& q = m_parts[i]->transform->localRotation;
        const bool posed = std::fabs(q.w) < 0.9995f;
        m_partRest[i] = posed ? bind : live;
    }
}

void Character::RemoveParts() {
    ReleaseOwnerArm();   // the copy hangs off the player, not the rig, so the rig's destroy would miss it
    ReleaseOwnerArm2();
    if (m_viewAnchor && !m_viewAnchor->IsDestroyed())
        if (Scene* s = GetScene()) s->Destroy(m_viewAnchor);
    m_viewAnchor = nullptr;
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

void Character::PoseBuiltParts() {
    if (m_partsBuilt && animateParts) DriveParts();
}

void Character::SetViewmodelBlend(float weight) {
    fpViewWeight = weight;
    if (weight < 0.0f) return;
    float arm = 0.0f;
    if (firstPersonArm && fpArmBase >= 0)
        arm = fpArmUp < 0.0f ? 0.0f : (fpArmUp > 1.0f ? 1.0f : fpArmUp);
    if (weight > 1.0f) weight = 1.0f;
    m_fpShown = arm * weight;
}

GameObject* Character::EnsureViewAnchor() {
    if (m_viewAnchor && m_viewAnchor->IsDestroyed()) m_viewAnchor = nullptr;
    if (m_viewAnchor) return m_viewAnchor;
    Scene* s = GetScene();
    if (!s || !gameObject || !gameObject->transform) return nullptr;
    m_viewAnchor = s->CreateGameObject("FP_ViewAnchor");
    m_viewAnchor->transient = true;
    m_viewAnchor->transform->SetParent(gameObject->transform, false);
    m_viewAnchor->transform->localPosition = Vec3::Zero;
    m_viewAnchor->transform->localRotation = Quat::Identity;
    m_viewAnchor->transform->localScale = Vec3::One;
    return m_viewAnchor;
}

GameObject* Character::OwnerHideTarget() const {
    Camera* cam = OwnerCamera(gameObject);
    if (!cam || !cam->ignoreObject) return nullptr;
    GameObject* ig = cam->ignoreObject;
    if (ig == gameObject) return ig;
    if (m_viewAnchor && ig == m_viewAnchor) return ig;
    return nullptr;
}

void Character::BlendHeldTowardGrip() {
    // The copy is already in the first-person placement. While the blend is
    // open, slide that world pose back toward the real grip so the item the
    // owner sees does not teleport. At either end the caller shows only one.
    if (fpViewWeight < 0.0f || m_fpShown >= 0.999f) return;
    if (!m_ownerItem || !m_ownerItem->active || !m_ownerItem->transform) return;
    if (!m_fpHiddenHeld || m_fpHiddenHeld->IsDestroyed() || !m_fpHiddenHeld->transform) return;
    const float w = m_fpShown < 0.0f ? 0.0f : m_fpShown;
    const Vec3 fpPos = m_ownerItem->transform->Position();
    const Quat fpRot = m_ownerItem->transform->Rotation();
    const Vec3 fpScale = m_ownerItem->transform->LossyScale();
    const Vec3 gripPos = m_fpHiddenHeld->transform->Position();
    const Quat gripRot = m_fpHiddenHeld->transform->Rotation();
    const Vec3 gripScale = m_fpHiddenHeld->transform->LossyScale();
    Transform* parent = nullptr;
    if (GameObject* anchor = EnsureViewAnchor()) parent = anchor->transform;
    else if (gameObject) parent = gameObject->transform;
    if (parent) m_ownerItem->transform->SetParent(parent, true);
    m_ownerItem->transform->SetPosition(Vec3::Lerp(gripPos, fpPos, w));
    m_ownerItem->transform->SetRotation(Quat::Slerp(gripRot, fpRot, w));
    const Vec3 parentScale = parent ? parent->LossyScale() : Vec3::One;
    const Vec3 scl = Vec3::Lerp(gripScale, fpScale, w);
    auto div = [](float world, float by) { return std::fabs(by) < 1e-5f ? world : world / by; };
    m_ownerItem->transform->localScale = Vec3{div(scl.x, parentScale.x), div(scl.y, parentScale.y), div(scl.z, parentScale.z)};
}

void Character::EaseFirstPersonArm(float dt) {
    if (dt > 0.0f) m_fpDt = dt;
    const float want = (firstPersonArm && fpArmBase >= 0)
        ? (fpArmUp < 0.0f ? 0.0f : (fpArmUp > 1.0f ? 1.0f : fpArmUp)) : 0.0f;
    if (fpViewWeight >= 0.0f) {
        float w = fpViewWeight > 1.0f ? 1.0f : fpViewWeight;
        m_fpShown = want * w;
        return;
    }
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
    // How fast the stride follows the ground (trailing, so older saves keep the default).
    o << ' ' << gaitSpeedSmoothing;
    o << ' ' << jumpCrouchTime << ' ' << jumpCrouchBend
      << ' ' << turnStepAngle << ' ' << turnStepTime << ' ' << turnStepLift
      << ' ' << turnStillSpeed << ' ' << motionLean << ' ' << motionLeanMax;
    o << ' ' << (clipDive.empty() ? "-" : clipDive);
    // Per-key easing, for the clips that set it on any key (trailing, count-prefixed).
    auto hasKeyEase = [](const AnimClip& c) {
        for (const auto& k : c.keys) if (k.ease != AnimKeyEase::Clip) return true;
        return false;
    };
    int withEase = 0;
    for (const auto& kv : m_clips) if (hasKeyEase(kv.second)) ++withEase;
    o << ' ' << withEase;
    for (const auto& kv : m_clips) {
        const AnimClip& c = kv.second;
        if (!hasKeyEase(c)) continue;
        o << ' ' << (c.name.empty() ? "-" : c.name) << ' ' << c.keys.size();
        for (const auto& k : c.keys) o << ' ' << (int)k.ease;
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
            it->second.interp = (ip == 1) ? AnimInterp::Smooth : (ip == 2) ? AnimInterp::Step
                              : (ip == 3) ? AnimInterp::Cubic  : AnimInterp::Linear;
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
    float gs = gaitSpeedSmoothing;
    if (in >> gs) gaitSpeedSmoothing = gs > 0.0f ? gs : 0.0f;
    float jct = jumpCrouchTime, jcb = jumpCrouchBend, tsa = turnStepAngle, tst = turnStepTime;
    float tsl = turnStepLift, tss = turnStillSpeed, ml = motionLean, mlm = motionLeanMax;
    if (in >> jct) jumpCrouchTime = std::max(0.0f, jct);
    if (in >> jcb) jumpCrouchBend = std::max(0.0f, jcb);
    if (in >> tsa) turnStepAngle = std::max(0.0f, tsa);
    if (in >> tst) turnStepTime = std::max(0.05f, tst);
    if (in >> tsl) turnStepLift = std::max(0.0f, tsl);
    if (in >> tss) turnStillSpeed = std::max(0.0f, tss);
    if (in >> ml)  motionLean = std::max(0.0f, ml);
    if (in >> mlm) motionLeanMax = std::max(0.0f, mlm);
    // Optional trailing dive binding (absent in older saves).
    std::string sdive;
    if (in >> sdive) clipDive = (sdive == "-" ? "" : sdive);
    // Optional trailing per-key easing.
    int ne = 0;
    if (in >> ne) {
        for (int i = 0; i < ne; ++i) {
            std::string cn; std::size_t nk = 0;
            if (!(in >> cn >> nk)) break;
            auto it = m_clips.find(cn == "-" ? "" : cn);
            AnimClip* clip = (it != m_clips.end() && it->second.keys.size() == nk) ? &it->second : nullptr;
            for (std::size_t k = 0; k < nk && in; ++k) {
                int e = 0; in >> e;
                if (clip && e >= 0 && e <= (int)AnimKeyEase::EaseOut) clip->keys[k].ease = (AnimKeyEase)e;
            }
        }
    }
    m_built = false;
}

} // namespace okay
