#include "okay/Components/Character.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Math/Quat.hpp"
#include <cmath>
#include <sstream>

namespace okay {

enum { B_HIPS, B_TORSO, B_HEAD,
       B_LUPARM, B_LFORE, B_LHAND, B_RUPARM, B_RFORE, B_RHAND,
       B_LTHIGH, B_LSHIN, B_LFOOT, B_RTHIGH, B_RSHIN, B_RFOOT, B_COUNT };

int Character::BoneCount() { return B_COUNT; }

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
    } else if (anim == 5) {                // jump
        r[B_LUPARM] = {0, 0, 140};   r[B_RUPARM] = {0, 0, -140};
        r[B_LTHIGH] = {-22, 0, 0};   r[B_RTHIGH] = {-22, 0, 0};
        r[B_LSHIN]  = {35, 0, 0};    r[B_RSHIN]  = {35, 0, 0};
    }
    return r;
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
    // is what stops the walk looking backwards (moonwalk).
    for (Vec3& v : m.vertices) { v.y *= height; v.x = -v.x; v.z = -v.z; }
    m.normals.clear();                     // boxy -> flat shading
    mr->mesh = std::move(m);
    mr->doubleSided = true;
}

void Character::Update(float dt) {
    if (anim == 0) return;
    animTime += dt * animSpeed;
    auto* mr = gameObject ? gameObject->GetComponent<MeshRenderer>() : nullptr;
    if (!mr) return;
    EnsureRest();
    Mesh m = m_rest;
    Skin(m, m_bone, PoseAt(animTime));
    for (Vec3& v : m.vertices) { v.y *= height; v.x = -v.x; v.z = -v.z; }   // face -Z (see Apply)
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
      << anim << ' ' << animSpeed;
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
    m_built = false;
}

} // namespace okay
