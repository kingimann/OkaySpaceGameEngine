#include "okay/Components/CharacterBody.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include <cmath>
#include <sstream>

namespace okay {

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
    mr->doubleSided = !smoothBody;
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
