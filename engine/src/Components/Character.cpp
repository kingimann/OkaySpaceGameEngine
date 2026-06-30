#include "okay/Components/Character.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
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
    m_rest = BuildRig(m_bone);
    m_restPos = m_rest.vertices;
    m_built = true;
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
    } else if (anim == 2 || anim == 3) {   // walk / run
        float amp = (anim == 3) ? 42.0f : 26.0f;
        float w = t * (anim == 3 ? 9.0f : 6.5f);
        float s = std::sin(w);
        r[B_LTHIGH] = {amp * s, 0, 0};   r[B_RTHIGH] = {-amp * s, 0, 0};
        r[B_LSHIN]  = {std::fmax(0.0f, -1.2f * amp * s), 0, 0};
        r[B_RSHIN]  = {std::fmax(0.0f,  1.2f * amp * s), 0, 0};
        r[B_LUPARM] = {-0.8f * amp * s, 0, 5};   r[B_RUPARM] = {0.8f * amp * s, 0, -5};
        r[B_LFORE]  = {18, 0, 0};   r[B_RFORE] = {18, 0, 0};
        r[B_TORSO]  = {(anim == 3 ? 12.0f : 4.0f) + 2.0f * std::fabs(s), 0, 0};
    } else if (anim == 4) {                // wave
        r[B_RUPARM] = {0, 0, -150};
        r[B_RFORE]  = {0, 0, -15 + 28 * std::sin(t * 8.0f)};
        r[B_LUPARM] = {0, 0, 6};
    } else if (anim == 5) {                // jump (arms swing straight up — never crossing)
        // Raise both arms overhead by rotating ONLY about X (the forward/up swing).
        // Any Z rotation is what swung the hands across the centerline and made them
        // cross in front of the face — so the jump pose keeps Z = 0 on both arms,
        // which makes crossing geometrically impossible (each arm stays in its own
        // side plane). A bent elbow keeps it looking like a natural leap, not a
        // stiff "hands up". -110° on the upper arm reaches up-and-slightly-back.
        r[B_LUPARM] = {-110, 0, 0};   r[B_RUPARM] = {-110, 0, 0};
        r[B_LFORE]  = {28, 0, 0};     r[B_RFORE]  = {28, 0, 0};   // elbows bent up
        r[B_LTHIGH] = {-22, 0, 0};    r[B_RTHIGH] = {-22, 0, 0};
        r[B_LSHIN]  = {35, 0, 0};     r[B_RSHIN]  = {35, 0, 0};
    } else if (anim == 6) {                // crouch (Minecraft-style sneak: hunch forward, feet planted)
        float s = std::sin(t * 2.2f);      // gentle breathing
        // Minecraft's sneak isn't a deep squat — the upper body pivots forward at
        // the waist and the head dips while the legs stay nearly straight and the
        // feet stay planted. A small knee bend lowers the stance a touch; the strong
        // torso lean is the signature read.
        r[B_HIPS]   = {10, 0, 0};                            // tip the pelvis forward
        r[B_TORSO]  = {34 + 1.5f * s, 0, 0};                 // the forward hunch
        r[B_HEAD]   = {-24, 0, 0};                           // bring the head up to look ahead
        r[B_LTHIGH] = {18, 0, 0};    r[B_RTHIGH] = {18, 0, 0};   // slight knee crouch
        r[B_LSHIN]  = {-30, 0, 0};   r[B_RSHIN]  = {-30, 0, 0};
        r[B_LFOOT]  = {12, 0, 0};    r[B_RFOOT]  = {12, 0, 0};   // soles stay flat
        r[B_LUPARM] = {16, 0, 8};    r[B_RUPARM] = {16, 0, -8};  // arms hang slightly forward
        r[B_LFORE]  = {20, 0, 0};    r[B_RFORE]  = {20, 0, 0};
    } else if (anim == 17) {               // crouch-walk (sneak while moving: legs cycle)
        float amp = 16.0f;                 // a shorter, lower stride than the upright walk
        float ss = std::sin(t * 6.0f);
        r[B_HIPS]   = {10, 0, 0};                            // keep the crouch upper body
        r[B_TORSO]  = {34, 0, 0};
        r[B_HEAD]   = {-24, 0, 0};
        r[B_LTHIGH] = {18 + amp * ss, 0, 0};   r[B_RTHIGH] = {18 - amp * ss, 0, 0};   // legs swing
        r[B_LSHIN]  = {-30 + std::fmax(0.0f, -1.2f * amp * ss), 0, 0};
        r[B_RSHIN]  = {-30 + std::fmax(0.0f,  1.2f * amp * ss), 0, 0};
        r[B_LFOOT]  = {12, 0, 0};    r[B_RFOOT]  = {12, 0, 0};
        r[B_LUPARM] = {16 - 0.6f * amp * ss, 0, 8};   r[B_RUPARM] = {16 + 0.6f * amp * ss, 0, -8};
        r[B_LFORE]  = {20, 0, 0};    r[B_RFORE]  = {20, 0, 0};
    } else if (anim == 7) {                // prone (whole body flat on the ground)
        // Rotate ONLY the hips 90° so the body lies flat: the torso/head swing
        // forward along the ground and the legs swing straight back — no counter
        // rotation on the legs (that was what stood it back up). StanceOffset()
        // then drops the whole body down onto the floor.
        r[B_HIPS]   = {90, 0, 0};
        r[B_HEAD]   = {-52, 0, 0};         // lift the head to look ahead
        r[B_LUPARM] = {16, 0, 14};   r[B_RUPARM] = {16, 0, -14};   // arms slightly out front
        r[B_LFORE]  = {24, 0, 0};    r[B_RFORE]  = {24, 0, 0};
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
    if (anim == 6 || anim == 17) return {0.0f, -0.12f, 0.0f};   // crouch / crouch-walk: light knee bend
    if (anim == 7) return {0.0f, -0.78f, 0.0f};   // prone: lay the body on the ground
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
    SyncStateClips();
    // Advance any in-progress crossfade (covers every render path below).
    if (m_blendT < 1.0f) {
        m_blendT += (blendTime > 1e-4f ? dt / blendTime : 1.0f);
        if (m_blendT > 1.0f) m_blendT = 1.0f;
    }
    AdvanceLayer(dt);   // tick the partial-body layer clip (if any), every path below uses it
    AdvanceBlendTree(dt);   // tick the locomotion blend tree's shared clock
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
            else animTime += dt * animSpeed;
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
            for (Vec3& v : m.vertices) { v.y *= height; v.x = -v.x; v.z = -v.z; }
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
    animTime += dt * animSpeed;

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
    m_partsBuilt = true;
    return true;
}

void Character::RemoveParts() {
    if (GameObject* rig = FindRig())
        if (Scene* s = GetScene()) s->Destroy(rig);   // takes its children with it
    m_rigRoot = nullptr;
    m_parts.clear();
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
    for (Vec3& v : m.vertices) { v.y *= height; v.x = -v.x; v.z = -v.z; }
    m.normals.clear(); mr->mesh = std::move(m); mr->doubleSided = true;
}

void Character::BuildParts() {
    Scene* s = GetScene();
    if (!s || !gameObject || !gameObject->transform) return;
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
    for (int bi = 0; bi < B_COUNT && bi < (int)pose.size(); ++bi)
        if (m_parts[bi] && m_parts[bi]->transform)
            m_parts[bi]->transform->localRotation = Quat::Euler(pose[bi]);
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
