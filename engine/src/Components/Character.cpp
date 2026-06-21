#include "okay/Components/Character.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Math/Quat.hpp"
#include <cmath>
#include <sstream>

namespace okay {

// Bone indices (15-bone humanoid).
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
    const bool round = (style == 2);
    const Color belt  = mul(shirt, 0.55f);
    const Color mouth = Color::FromBytes(150, 95, 90);
    const Color upperArmCol = (shirtStyle >= 1) ? shirt : skin;
    const Color foreArmCol  = (shirtStyle >= 2) ? shirt : skin;

    // Primitive helpers that tag every emitted vertex with a bone. `build` scales
    // girth (x/z); height is applied later, uniformly.
    auto add = [&](const Mesh& prim, Vec3 c, Vec3 s, Color col, int b) {
        int before = (int)m.vertices.size();
        m.Add(prim, c, s, &col);
        for (int i = before; i < (int)m.vertices.size(); ++i) bone.push_back(b);
    };
    auto box  = [&](Vec3 c, Vec3 s, Color col, int b) {
        add(Mesh::Cube(1.0f), c, {s.x * build, s.y, s.z * build}, col, b); };
    auto cyl  = [&](Vec3 c, float rx, float rz, float h, Color col, int b) {
        add(Mesh::Cylinder(0.5f, 1.0f, 12), c, {rx * 2 * build, h, rz * 2 * build}, col, b); };
    auto ball = [&](Vec3 c, Vec3 s, Color col, int b) {
        add(Mesh::Icosphere(0.5f, 2), c, {s.x * 2 * build, s.y * 2, s.z * 2 * build}, col, b); };
    // Style-agnostic blob: a box for the boxy styles, an ellipsoid for RuneScape.
    auto blob = [&](Vec3 c, Vec3 half, Color col, int b) {
        if (round) ball(c, half, col, b);
        else       box(c, {half.x * 2, half.y * 2, half.z * 2}, col, b); };

    const float hY = 1.62f;                 // head centre (build units)
    auto hpos = [&](Vec3 p) {               // head-part position, scaled about the neck
        return Vec3{p.x * headSize, 1.48f + (p.y - 1.48f) * headSize, p.z * headSize}; };
    auto hsz = [&](Vec3 s) { return Vec3{s.x * headSize, s.y * headSize, s.z * headSize}; };

    if (style == 0) {
        // ---- Minecraft: a handful of boxes, big cubic head, flat shaded ----
        box({0, 0.95f, 0}, {0.40f, 0.20f, 0.22f}, pants, B_HIPS);
        box({0, 1.24f, 0}, {0.40f, 0.50f, 0.22f}, shirt, B_TORSO);
        for (int s = -1; s <= 1; s += 2) {
            int up = s < 0 ? B_LUPARM : B_RUPARM, fo = s < 0 ? B_LFORE : B_RFORE, ha = s < 0 ? B_LHAND : B_RHAND;
            box({s * 0.27f, 1.28f, 0}, {0.16f, 0.30f, 0.18f}, upperArmCol, up);
            box({s * 0.27f, 0.99f, 0}, {0.15f, 0.30f, 0.16f}, foreArmCol, fo);
            box({s * 0.27f, 0.80f, 0}, {0.15f, 0.12f, 0.16f}, skin, ha);
            int th = s < 0 ? B_LTHIGH : B_RTHIGH, sh = s < 0 ? B_LSHIN : B_RSHIN, ft = s < 0 ? B_LFOOT : B_RFOOT;
            box({s * 0.105f, 0.73f, 0}, {0.19f, 0.40f, 0.20f}, pants, th);
            box({s * 0.105f, 0.33f, 0}, {0.18f, 0.42f, 0.18f}, pants, sh);
            box({s * 0.105f, 0.06f, 0.03f}, {0.19f, 0.12f, 0.26f}, shoes, ft);
        }
        box(hpos({0, hY, 0}), hsz({0.44f, 0.44f, 0.42f}), skin, B_HEAD);
        for (int s = -1; s <= 1; s += 2)
            box(hpos({s * 0.10f, 1.64f, 0.21f}), hsz({0.07f, 0.08f, 0.03f}), eyes, B_HEAD);
    } else if (style == 1) {
        // ---- Unturned: boxy but more segments + smaller head, flat shaded ----
        box({0, 0.95f, 0}, {0.40f, 0.22f, 0.235f}, pants, B_HIPS);
        box({0, 1.25f, 0}, {0.44f, 0.50f, 0.25f}, shirt, B_TORSO);
        box({0, 1.00f, 0}, {0.41f, 0.06f, 0.255f}, belt, B_HIPS);
        for (int s = -1; s <= 1; s += 2) {
            int up = s < 0 ? B_LUPARM : B_RUPARM, fo = s < 0 ? B_LFORE : B_RFORE, ha = s < 0 ? B_LHAND : B_RHAND;
            box({s * 0.27f, 1.30f, 0}, {0.13f, 0.34f, 0.18f}, upperArmCol, up);
            box({s * 0.27f, 0.99f, 0}, {0.12f, 0.32f, 0.16f}, foreArmCol, fo);
            box({s * 0.27f, 0.80f, 0.02f}, {0.12f, 0.12f, 0.17f}, skin, ha);
            int th = s < 0 ? B_LTHIGH : B_RTHIGH, sh = s < 0 ? B_LSHIN : B_RSHIN, ft = s < 0 ? B_LFOOT : B_RFOOT;
            box({s * 0.105f, 0.74f, 0}, {0.18f, 0.42f, 0.20f}, pants, th);
            box({s * 0.105f, 0.34f, 0}, {0.16f, 0.44f, 0.18f}, pants, sh);
            box({s * 0.105f, 0.05f, 0.05f}, {0.17f, 0.10f, 0.30f}, shoes, ft);
        }
        box({0, 1.50f, 0}, {0.15f, 0.10f, 0.15f}, skin, B_TORSO);
        box(hpos({0, hY, 0}), hsz({0.30f, 0.32f, 0.30f}), skin, B_HEAD);
        for (int s = -1; s <= 1; s += 2)
            box(hpos({s * 0.07f, 1.64f, 0.16f}), hsz({0.05f, 0.06f, 0.03f}), eyes, B_HEAD);
    } else {
        // ---- RuneScape: rounded low-poly, smooth shaded, fingered hands ----
        cyl({0, 1.27f, 0}, 0.235f, 0.15f, 0.34f, shirt, B_TORSO);   // chest
        cyl({0, 1.05f, 0}, 0.195f, 0.135f, 0.20f, shirt, B_HIPS);   // waist
        box({0, 1.00f, 0}, {0.40f, 0.06f, 0.275f}, belt, B_HIPS);
        for (int s = -1; s <= 1; s += 2) ball({s * 0.20f, 1.38f, 0}, {0.10f, 0.10f, 0.125f}, shirt, B_TORSO);
        for (int s = -1; s <= 1; s += 2) {
            int up = s < 0 ? B_LUPARM : B_RUPARM, fo = s < 0 ? B_LFORE : B_RFORE, ha = s < 0 ? B_LHAND : B_RHAND;
            cyl({s * 0.255f, 1.27f, 0}, 0.072f, 0.072f, 0.30f, upperArmCol, up);
            ball({s * 0.27f, 1.11f, 0}, {0.068f, 0.07f, 0.068f}, foreArmCol, fo);
            cyl({s * 0.285f, 0.99f, 0}, 0.062f, 0.062f, 0.27f, foreArmCol, fo);
            // hand: palm + 4 fingers + thumb
            float hx = s * 0.285f;
            box({hx, 0.80f, 0}, {0.105f, 0.09f, 0.075f}, skin, ha);
            for (int f = 0; f < 4; ++f) cyl({hx + (-1.5f + f) * 0.028f, 0.715f, 0.012f}, 0.018f, 0.02f, 0.09f, skin, ha);
            cyl({hx - s * 0.062f, 0.78f, 0.03f}, 0.02f, 0.022f, 0.065f, skin, ha);
            int th = s < 0 ? B_LTHIGH : B_RTHIGH, sh = s < 0 ? B_LSHIN : B_RSHIN, ft = s < 0 ? B_LFOOT : B_RFOOT;
            cyl({s * 0.105f, 0.75f, 0}, 0.105f, 0.105f, 0.40f, pants, th);
            ball({s * 0.105f, 0.55f, 0}, {0.10f, 0.09f, 0.10f}, pants, sh);
            cyl({s * 0.105f, 0.335f, 0}, 0.085f, 0.085f, 0.45f, pants, sh);
            ball({s * 0.105f, 0.10f, 0}, {0.095f, 0.085f, 0.10f}, shoes, ft);
            box({s * 0.105f, 0.045f, 0.05f}, {0.17f, 0.09f, 0.27f}, shoes, ft);
        }
        cyl({0, 1.49f, 0}, 0.07f, 0.07f, 0.10f, skin, B_TORSO);
        ball(hpos({0, 1.66f, 0}), hsz({0.165f, 0.195f, 0.17f}), skin, B_HEAD);
        ball(hpos({0, 1.585f, 0.02f}), hsz({0.10f, 0.07f, 0.10f}), skin, B_HEAD);   // jaw
        box(hpos({0, 1.63f, 0.165f}), hsz({0.045f, 0.06f, 0.06f}), skin, B_HEAD);   // nose
        box(hpos({0, 1.585f, 0.155f}), hsz({0.06f, 0.022f, 0.04f}), mouth, B_HEAD); // mouth
        for (int s = -1; s <= 1; s += 2) ball(hpos({s * 0.165f, 1.66f, 0}), hsz({0.03f, 0.05f, 0.04f}), skin, B_HEAD);
        for (int s = -1; s <= 1; s += 2) ball(hpos({s * 0.058f, 1.67f, 0.15f}), hsz({0.026f, 0.034f, 0.02f}), eyes, B_HEAD);
    }

    // ---- Hair (style-aware shape) ----
    if (hasHair) {
        if (hairStyle == 2) {            // mohawk: a centre strip
            for (int i = 0; i < 3; ++i)
                blob(hpos({0, 1.80f + i * 0.0f, -0.04f + i * 0.04f}), hsz({0.05f, 0.12f - i * 0.02f, 0.12f}), hair, B_HEAD);
        } else {
            blob(hpos({0, 1.78f, -0.01f}), hsz({0.20f, 0.10f, 0.20f}), hair, B_HEAD);     // top cap
            blob(hpos({0, 1.70f, -0.12f}), hsz({0.20f, 0.13f, 0.07f}), hair, B_HEAD);     // back
            if (hairStyle == 1)          // long: down the back/neck
                box(hpos({0, 1.52f, -0.14f}), hsz({0.30f, 0.34f, 0.08f}), hair, B_HEAD);
            if (hairStyle == 3)          // bun
                blob(hpos({0, 1.86f, -0.10f}), hsz({0.09f, 0.09f, 0.09f}), hair, B_HEAD);
        }
    }
    if (hasBeard)
        blob(hpos({0, 1.56f, 0.12f}), hsz({0.16f, 0.10f, 0.10f}), hair, B_HEAD);

    // ---- Accessories ----
    if (hasHat) {
        if (hatStyle == 0) {                                  // cap
            blob(hpos({0, 1.83f, 0}), hsz({0.23f, 0.07f, 0.23f}), hat, B_HEAD);
            box(hpos({0, 1.82f, 0.20f}), hsz({0.30f, 0.04f, 0.20f}), hat, B_HEAD);   // brim
        } else if (hatStyle == 1) {                           // helmet
            blob(hpos({0, 1.74f, 0}), hsz({0.24f, 0.20f, 0.24f}), hat, B_HEAD);
        } else if (hatStyle == 2) {                           // top hat
            box(hpos({0, 1.84f, 0}), hsz({0.40f, 0.04f, 0.40f}), hat, B_HEAD);       // brim
            box(hpos({0, 1.98f, 0}), hsz({0.26f, 0.26f, 0.26f}), hat, B_HEAD);       // crown
        } else {                                              // wizard hat (cone)
            box(hpos({0, 1.84f, 0}), hsz({0.40f, 0.04f, 0.40f}), hat, B_HEAD);
            add(Mesh::Cone(0.5f, 1.0f, 12), hpos({0, 2.05f, 0}), hsz({0.34f, 0.42f, 0.34f}), hat, B_HEAD);
        }
    }
    if (hasGlasses) {
        Color gl = Color::FromBytes(25, 25, 30);
        for (int s = -1; s <= 1; s += 2)
            box(hpos({s * 0.058f, 1.67f, 0.17f}), hsz({0.07f, 0.07f, 0.02f}), gl, B_HEAD);
        box(hpos({0, 1.67f, 0.17f}), hsz({0.05f, 0.02f, 0.02f}), gl, B_HEAD);
    }
    if (hasBackpack)
        box({0, 1.22f, -0.20f}, {0.34f, 0.40f, 0.16f}, pack, B_TORSO);
    if (hasCape)
        box({0, 1.10f, -0.18f}, {0.40f, 0.70f, 0.04f}, mul(pack, 1.1f), B_TORSO);

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
    auto deg = [](float x) { return x; };
    if (anim == 0) {                       // manual pose
        for (int i = 0; i < B_COUNT && i < (int)pose.size(); ++i) r[i] = pose[i];
        return r;
    }
    if (anim == 1) {                       // idle: gentle breathing + arm sway
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
    } else if (anim == 4) {                // wave (right arm)
        r[B_RUPARM] = {0, 0, -150};
        r[B_RFORE]  = {0, 0, -15 + 28 * std::sin(t * 8.0f)};
        r[B_LUPARM] = {0, 0, 6};
    } else if (anim == 5) {                // jump pose
        r[B_LUPARM] = {0, 0, 140};   r[B_RUPARM] = {0, 0, -140};
        r[B_LTHIGH] = {-22, 0, 0};   r[B_RTHIGH] = {-22, 0, 0};
        r[B_LSHIN]  = {35, 0, 0};    r[B_RSHIN]  = {35, 0, 0};
    }
    (void)deg;
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
    for (Vec3& v : m.vertices) v.y *= height;
    if (style == 2) m.ComputeSmoothNormals(); else m.normals.clear();
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
    for (Vec3& v : m.vertices) v.y *= height;
    if (style == 2) m.ComputeSmoothNormals(); else m.normals.clear();
    mr->mesh = std::move(m);
    mr->doubleSided = true;
}

std::string Character::ToText() const {
    std::ostringstream o;
    o << style << ' ' << height << ' ' << build << ' ' << headSize << ' '
      << skin.r << ' ' << skin.g << ' ' << skin.b << ' '
      << shirt.r << ' ' << shirt.g << ' ' << shirt.b << ' '
      << pants.r << ' ' << pants.g << ' ' << pants.b << ' '
      << shoes.r << ' ' << shoes.g << ' ' << shoes.b << ' '
      << hair.r << ' ' << hair.g << ' ' << hair.b << ' '
      << eyes.r << ' ' << eyes.g << ' ' << eyes.b << ' '
      << hat.r << ' ' << hat.g << ' ' << hat.b << ' '
      << pack.r << ' ' << pack.g << ' ' << pack.b << ' '
      << (hasHair ? 1 : 0) << ' ' << hairStyle << ' ' << shirtStyle << ' '
      << (hasHat ? 1 : 0) << ' ' << hatStyle << ' ' << (hasGlasses ? 1 : 0) << ' '
      << (hasBackpack ? 1 : 0) << ' ' << (hasCape ? 1 : 0) << ' ' << (hasBeard ? 1 : 0) << ' '
      << anim << ' ' << animSpeed;
    return o.str();
}

void Character::FromText(const std::string& text) {
    std::istringstream in(text);
    int hh, hhat, hgl, hbp, hc, hb;
    in >> style >> height >> build >> headSize
       >> skin.r >> skin.g >> skin.b >> shirt.r >> shirt.g >> shirt.b
       >> pants.r >> pants.g >> pants.b >> shoes.r >> shoes.g >> shoes.b
       >> hair.r >> hair.g >> hair.b >> eyes.r >> eyes.g >> eyes.b
       >> hat.r >> hat.g >> hat.b >> pack.r >> pack.g >> pack.b
       >> hh >> hairStyle >> shirtStyle >> hhat >> hatStyle >> hgl >> hbp >> hc >> hb
       >> anim >> animSpeed;
    if (in.fail()) return;
    skin.a = shirt.a = pants.a = shoes.a = hair.a = eyes.a = hat.a = pack.a = 1.0f;
    hasHair = hh != 0; hasHat = hhat != 0; hasGlasses = hgl != 0;
    hasBackpack = hbp != 0; hasCape = hc != 0; hasBeard = hb != 0;
    m_built = false;
}

} // namespace okay
