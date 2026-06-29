#include "okay/Components/Character.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Math/Mathf.hpp"
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
    if (anim == 6) return {0.0f, -0.12f, 0.0f};   // crouch: light knee bend, feet stay planted (forward-hunch sneak)
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
    m_activeClip = &it->second;
    m_activeClipName = name;
    m_clipTime = 0.0f;
    return true;
}

void Character::StopClip() {
    m_activeClip = nullptr;
    m_activeClipName.clear();
    m_clipTime = 0.0f;
}

void Character::Update(float dt) {
    // A custom clip, if one is playing, drives the whole body and overrides the
    // built-in animations.
    if (m_activeClip) {
        m_clipTime += dt * animSpeed;
        float dur = m_activeClip->Duration();
        if (dur > 0.0f) {
            if (m_activeClip->loop) m_clipTime = std::fmod(m_clipTime, dur);
            else if (m_clipTime > dur) m_clipTime = dur;
        }
        auto* mr = gameObject ? gameObject->GetComponent<MeshRenderer>() : nullptr;
        if (!mr) return;
        EnsureRest();
        Mesh m = m_rest;
        Skin(m, m_bone, m_activeClip->Sample(m_clipTime));
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
    std::vector<Vec3> pose = PoseAt(animTime);
    // First-person arm: freeze the body to rest (so walking never brings your torso
    // or legs into the first-person view) and raise your OWN arm into the camera.
    // The body mesh is mirror-built (v.x=-v.x below), so the L-bone arm lands on the
    // screen's RIGHT — your right hand, where Minecraft puts it.
    if (firstPersonArm && (int)pose.size() > B_RFORE) {
        for (Vec3& p : pose) p = Vec3{0.0f, 0.0f, 0.0f};
        float u = fpArmUp < 0.0f ? 0.0f : (fpArmUp > 1.0f ? 1.0f : fpArmUp);  // 0 = empty (low) .. 1 = holding (raised)
        // Empty-handed the arm rests low in the lower-right (this is the normally
        // shown, tunable pose); holding a weapon/item raises + presents it higher.
        Vec3 upRest  = {fpRaise, 0.0f, 6.0f},        foreRest  = {fpElbow, 0.0f, 0.0f};
        Vec3 upRaise = {fpRaise - 22.0f, 0.0f, 6.0f}, foreRaise = {fpElbow - 14.0f, 0.0f, 0.0f};
        pose[B_LUPARM] = upRest  + (upRaise  - upRest)  * u;
        pose[B_LFORE]  = foreRest + (foreRaise - foreRest) * u;
    }
    // Layer a punch arc over the arm (swings forward and back). In first-person mode
    // the swing drives the raised (screen-right) arm; otherwise the right arm.
    if (m_punchT >= 0.0f && m_punchT < 1.0f && (int)pose.size() > B_RFORE) {
        float arc = std::sin(m_punchT * 3.14159265f);   // 0..1..0
        if (firstPersonArm) {
            Vec3 up = {-140.0f, 0.0f, 8.0f}, fore = {36.0f, 0.0f, 0.0f};
            pose[B_LUPARM] = pose[B_LUPARM] + (up   - pose[B_LUPARM]) * arc;
            pose[B_LFORE]  = pose[B_LFORE]  + (fore - pose[B_LFORE])  * arc;
        } else {
            Vec3 up = {110.0f, 0.0f, -8.0f}, fore = {-18.0f, 0.0f, 0.0f};
            pose[B_RUPARM] = pose[B_RUPARM] + (up   - pose[B_RUPARM]) * arc;
            pose[B_RFORE]  = pose[B_RFORE]  + (fore - pose[B_RFORE])  * arc;
        }
    }
    Skin(m, m_bone, pose);
    Vec3 so = StanceOffset();
    for (Vec3& v : m.vertices) { v.y *= height; v.x = -v.x; v.z = -v.z; v += so; }   // face -Z + stance drop (see Apply)
    m.normals.clear();
    mr->mesh = std::move(m);
    mr->doubleSided = true;
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
      << (autoPlayClip.empty() ? "-" : autoPlayClip);
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
    m_built = false;
}

} // namespace okay
