#include "okay/Components/CharacterBody.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include <cmath>
#include <sstream>

namespace okay {

// ---- Skeletal rig -----------------------------------------------------------
// A 17-bone humanoid skeleton (FK hierarchy) whose joints match the seamless
// body's geometry, plus linear-blend skinning so rotating a bone bends the mesh.
int CharacterBody::BoneCount() { return 17; }

const char* CharacterBody::BoneName(int i) {
    static const char* names[17] = {
        "Hips", "Spine", "Chest", "Neck", "Head",
        "L Upper Arm", "L Forearm", "L Hand",
        "R Upper Arm", "R Forearm", "R Hand",
        "L Thigh", "L Shin", "L Foot",
        "R Thigh", "R Shin", "R Foot",
    };
    return (i >= 0 && i < 17) ? names[i] : "?";
}

std::vector<CharacterBody::Bone> CharacterBody::BuildSkeleton(const HumanoidParams& p) const {
    const float H = p.height, up = 0.78f * H * (p.torsoLength - 1.0f);
    const float sw = 0.24f * p.shoulderWidth, hw = 0.20f * p.hipWidth;
    const float aL = p.armLength, lL = p.legLength;
    std::vector<Bone> b(17);
    b[0] = {-1, {0, 0.60f * H, 0}, {0, 0.80f * H, 0}};                 // Hips (root)
    b[1] = { 0, {0, 0.86f * H, 0}, {0, 1.05f * H, 0}};                 // Spine
    b[2] = { 1, {0, 1.10f * H + up * 0.5f, 0}, {0, 1.42f * H + up, 0}}; // Chest
    b[3] = { 2, {0, 1.46f * H + up, 0}, {0, 1.58f * H + up, 0}};        // Neck
    b[4] = { 3, {0, 1.62f * H + up, 0}, {0, 1.86f * H + up, 0}};        // Head
    for (int si = 0; si < 2; ++si) {
        int s = si == 0 ? -1 : 1;
        float aw = sw + p.armGap, lw = hw + p.legGap;
        float shoulderY = 1.46f * H + up, armLen = 0.82f * aL * H;
        Vec3 sh{(float)s * aw, shoulderY, 0};
        Quat q = Quat::Euler({(float)s * p.armSwing, 0, (float)s * p.armSpread});
        Vec3 elbow = sh + q * Vec3{0, -armLen * 0.5f, 0};
        Quat fq = q * Quat::Euler({16.0f, 0.0f, (float)s * -7.0f});
        Vec3 foreDir = fq * Vec3{0, -1, 0};
        Vec3 wrist = elbow + foreDir * (armLen * 0.5f);
        int ab = 5 + si * 3;
        b[ab + 0] = {2,        sh,    elbow};                          // Upper arm
        b[ab + 1] = {ab + 0,   elbow, wrist};                         // Forearm
        b[ab + 2] = {ab + 1,   wrist, wrist + foreDir * (0.18f * H)}; // Hand
        float legLen = 0.6f * H + 0.55f * lL * H;
        Vec3 hip{(float)s * lw, 0.60f * H, 0};
        Quat ql = Quat::Euler({(float)s * p.legSwing, 0, (float)s * p.legSpread});
        Vec3 knee = hip + ql * Vec3{0, -legLen * 0.5f, 0};
        Vec3 ankle = hip + ql * Vec3{0, -legLen, 0};
        int lb = 11 + si * 3;
        b[lb + 0] = {0,        hip,   knee};                           // Thigh
        b[lb + 1] = {lb + 0,   knee,  ankle};                         // Shin
        b[lb + 2] = {lb + 1,   ankle, ankle + Vec3{0, -0.05f * H, 0.18f}}; // Foot
    }
    return b;
}

void CharacterBody::ApplyPose(Mesh& m, const HumanoidParams& p) const {
    if ((int)pose.size() < BoneCount() || m.vertices.empty()) return;
    bool any = false;
    for (const Vec3& r : pose)
        if (std::fabs(r.x) + std::fabs(r.y) + std::fabs(r.z) > 1e-3f) { any = true; break; }
    if (!any) return;   // rest pose -> leave the mesh untouched

    std::vector<Bone> bones = BuildSkeleton(p);
    const int n = (int)bones.size();
    std::vector<Mat4> restW(n), poseW(n), skin(n);
    for (int i = 0; i < n; ++i) {
        Vec3 jp = bones[i].parent >= 0 ? bones[bones[i].parent].joint : Vec3{0, 0, 0};
        Mat4 local = Mat4::Translate(bones[i].joint - jp);
        Mat4 localPosed = local * Mat4::Rotate(Quat::Euler(pose[i]));
        if (bones[i].parent >= 0) {
            restW[i] = restW[bones[i].parent] * local;
            poseW[i] = poseW[bones[i].parent] * localPosed;
        } else { restW[i] = local; poseW[i] = localPosed; }
    }
    for (int i = 0; i < n; ++i) skin[i] = poseW[i] * restW[i].Inverse();

    auto segd = [](const Vec3& pt, const Vec3& a, const Vec3& bb) {
        Vec3 ab = bb - a, ap = pt - a; float dd = Vec3::Dot(ab, ab);
        float t = dd > 1e-6f ? Vec3::Dot(ap, ab) / dd : 0.0f;
        t = t < 0 ? 0 : (t > 1 ? 1 : t);
        return (pt - (a + ab * t)).Magnitude();
    };
    // Linear-blend skin to the two nearest bones (sharp inverse-distance weights
    // keep limbs rigid but blend smoothly across joints).
    for (Vec3& v : m.vertices) {
        int b1 = 0, b2 = 0; float d1 = 1e9f, d2 = 1e9f;
        for (int i = 0; i < n; ++i) {
            float d = segd(v, bones[i].joint, bones[i].tail);
            if (d < d1) { d2 = d1; b2 = b1; d1 = d; b1 = i; }
            else if (d < d2) { d2 = d; b2 = i; }
        }
        float w1 = 1.0f / std::pow(d1 + 1e-3f, 6.0f);
        float w2 = 1.0f / std::pow(d2 + 1e-3f, 6.0f);
        float sum = w1 + w2; w1 /= sum; w2 /= sum;
        v = skin[b1].MultiplyPoint(v) * w1 + skin[b2].MultiplyPoint(v) * w2;
    }
}

namespace {
std::string Quoted(const std::string& s) { return "\"" + s + "\""; }
std::string ReadQ(std::istream& in) {
    std::string t; in >> std::ws;
    if (in.peek() == '"') { in.get(); std::getline(in, t, '"'); }
    else in >> t;
    return t;
}
} // namespace

std::string CharacterBody::ToText() const {
    std::ostringstream o;
    const HumanoidParams& p = params;
    o << "okaychar 1\n";
    o << p.height << " " << p.build << " " << p.headSize << " " << p.shoulderWidth << " "
      << p.hipWidth << " " << p.armLength << " " << p.legLength << " " << p.neckLength << " "
      << p.handSize << " " << p.footSize << " " << p.armSpread << " " << p.legSpread << " "
      << p.torsoLength << " " << p.bodyDepth << " " << p.hairStyle << " " << p.eyeSpacing << " "
      << p.mouthWidth << " " << p.browAngle << " " << (p.ears ? 1 : 0) << " " << p.eyeSize << " "
      << p.noseSize << " " << p.armThickness << " " << p.legThickness << " " << p.waist << " "
      << p.belly << "\n";
    auto wc = [&](const Color& c) { o << c.r << " " << c.g << " " << c.b << " " << c.a << " "; };
    wc(color); wc(outfit); wc(pants); wc(shoes); wc(hair); wc(eye); wc(hat); wc(glasses); o << "\n";
    o << (hasHair ? 1 : 0) << " " << (hasFace ? 1 : 0) << " " << (hasHat ? 1 : 0) << " "
      << (hasGlasses ? 1 : 0) << " " << (beard ? 1 : 0) << " " << (mustache ? 1 : 0) << " "
      << subdivisions << " " << smoothAmount << " " << anim << " " << animSpeed << "\n";
    o << accessories.size() << "\n";
    for (const Accessory& a : accessories)
        o << Quoted(a.name) << " " << Quoted(a.shape) << " "
          << a.offset.x << " " << a.offset.y << " " << a.offset.z << " "
          << a.scale.x << " " << a.scale.y << " " << a.scale.z << " "
          << a.rotation.x << " " << a.rotation.y << " " << a.rotation.z << " "
          << a.color.r << " " << a.color.g << " " << a.color.b << " " << a.color.a << " "
          << a.attach << "\n";
    o << (rootMotion ? 1 : 0) << " " << (smoothBody ? 1 : 0) << " " << smoothRes << "\n";
    o << p.armGap << " " << p.legGap << "\n";
    o << pose.size();                       // skeletal pose (euler per bone)
    for (const Vec3& r : pose) o << " " << r.x << " " << r.y << " " << r.z;
    o << "\n";
    o << (lowPoly ? 1 : 0) << "\n";         // body style
    return o.str();
}

void CharacterBody::FromText(const std::string& text) {
    std::istringstream in(text);
    std::string tag; int ver = 0;
    in >> tag >> ver;
    if (tag != "okaychar") return;
    HumanoidParams& p = params;
    int ears = 1;
    in >> p.height >> p.build >> p.headSize >> p.shoulderWidth >> p.hipWidth >> p.armLength
       >> p.legLength >> p.neckLength >> p.handSize >> p.footSize >> p.armSpread >> p.legSpread
       >> p.torsoLength >> p.bodyDepth >> p.hairStyle >> p.eyeSpacing >> p.mouthWidth
       >> p.browAngle >> ears >> p.eyeSize >> p.noseSize >> p.armThickness >> p.legThickness
       >> p.waist >> p.belly;
    p.ears = (ears != 0);
    auto rc = [&](Color& c) { in >> c.r >> c.g >> c.b >> c.a; };
    rc(color); rc(outfit); rc(pants); rc(shoes); rc(hair); rc(eye); rc(hat); rc(glasses);
    int hh, hf, ht, hg, bd, ms;
    in >> hh >> hf >> ht >> hg >> bd >> ms >> subdivisions >> smoothAmount >> anim >> animSpeed;
    hasHair = hh; hasFace = hf; hasHat = ht; hasGlasses = hg; beard = bd; mustache = ms;
    std::size_t n = 0; in >> n;
    accessories.clear();
    for (std::size_t k = 0; k < n; ++k) {
        Accessory a;
        a.name = ReadQ(in); a.shape = ReadQ(in);
        in >> a.offset.x >> a.offset.y >> a.offset.z
           >> a.scale.x >> a.scale.y >> a.scale.z
           >> a.rotation.x >> a.rotation.y >> a.rotation.z
           >> a.color.r >> a.color.g >> a.color.b >> a.color.a >> a.attach;
        accessories.push_back(a);
    }
    int rm = 1; if (in >> rm) rootMotion = (rm != 0);   // optional (older presets lack it)
    int sb = 0; if (in >> sb) smoothBody = (sb != 0);
    int sr = 0; if (in >> sr) smoothRes = sr;
    float ag = 0.0f, lg = 0.0f;                          // limb spacing (optional)
    if (in >> ag) p.armGap = ag;
    if (in >> lg) p.legGap = lg;
    std::size_t np = 0;                                   // skeletal pose (optional)
    if (in >> np) {
        pose.assign(np, Vec3{0, 0, 0});
        for (std::size_t k = 0; k < np; ++k) in >> pose[k].x >> pose[k].y >> pose[k].z;
    }
    int lp = 1; if (in >> lp) lowPoly = (lp != 0);        // body style (optional)
}

void CharacterBody::Apply() {
    if (!gameObject) return;
    auto* mr = gameObject->GetComponent<MeshRenderer>();
    if (!mr) mr = gameObject->AddComponent<MeshRenderer>();
    mr->mesh = Build();
    mr->color = color;
    // The seamless body is a closed, outward-wound surface -> render single-sided
    // so the renderer can backface-cull (much cheaper with several characters).
    // The part-based body has mixed winding and must be drawn double-sided.
    mr->doubleSided = lowPoly || !smoothBody;
    // A soft specular sheen gives skin and clothing a lifelike highlight instead
    // of looking like flat matte clay.
    mr->specular = 0.22f;
    mr->shininess = 24.0f;
}

void CharacterBody::Update(float dt) {
    if (anim == 0 || smoothBody) return;   // seamless body is too heavy to rebuild per frame
    auto* mr = gameObject ? gameObject->GetComponent<MeshRenderer>() : nullptr;
    if (!mr) return;
    Transform* tr = gameObject ? gameObject->transform : nullptr;
    if (tr && !restYset) { restY = tr->localPosition.y; restYset = true; }
    animTime += dt * (animSpeed <= 0.0f ? 1.0f : animSpeed);
    const float t = animTime;
    HumanoidParams pp = params;        // animate a copy; authored params untouched
    switch (anim) {
        case 1: {  // idle: gentle counter-sway of the arms (subtle breathing)
            pp.armSwing = 4.0f * std::sin(t * 1.6f);
            break;
        }
        case 2: {  // walk: legs swing fore/aft, arms counter them
            float s = std::sin(t * 4.0f) * 24.0f;
            pp.legSwing = s; pp.armSwing = -s;
            break;
        }
        case 3: {  // run: faster, larger swing
            float s = std::sin(t * 7.0f) * 40.0f;
            pp.legSwing = s; pp.armSwing = -s;
            break;
        }
        case 4: {  // wave: raise the right arm and wave it
            pp.armSwing = 3.0f * std::sin(t * 1.5f);
            pp.rightArmRot = {18.0f * std::sin(t * 6.0f), 0.0f, -135.0f};
            break;
        }
        case 5: {  // jump: crouch-and-hop with arms up; bobs the body height
            float ph = std::sin(t * 3.0f);
            float h = ph > 0.0f ? ph : 0.0f;
            pp.legSwing = 0.0f; pp.armSpread = 20.0f + 80.0f * h;
            if (tr) tr->localPosition.y = restY + 0.8f * h;
            break;
        }
        case 6: {  // auto: pick idle/walk/run from how fast the object is moving
            float speed = 0.0f;
            if (tr) {
                Vec3 pos = tr->Position();
                if (lastPosSet && dt > 1e-4f) {
                    float dx = pos.x - lastPos.x, dz = pos.z - lastPos.z;
                    speed = std::sqrt(dx * dx + dz * dz) / dt;
                }
                lastPos = pos; lastPosSet = true;
            }
            if (speed > 2.5f)      { float s = std::sin(t * 7.0f) * 40.0f; pp.legSwing = s; pp.armSwing = -s; }
            else if (speed > 0.3f) { float s = std::sin(t * 4.0f) * 24.0f; pp.legSwing = s; pp.armSwing = -s; }
            else                   { pp.armSwing = 4.0f * std::sin(t * 1.6f); }
            break;
        }
        default: break;
    }
    // Root motion: Walk/Run travel forward along the object's facing.
    if (rootMotion && tr && (anim == 2 || anim == 3)) {
        float spd = (anim == 3 ? 3.0f : 1.2f) * (animSpeed <= 0.0f ? 1.0f : animSpeed);
        tr->localPosition = tr->localPosition + tr->Forward() * (spd * dt);
    }
    mr->mesh = Build(pp);
}

} // namespace okay
