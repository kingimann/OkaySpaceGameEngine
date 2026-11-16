// The organic Guided human: proportions, body types, sliders, triangle budgets per Detail level, validity (closed, finite parts) and
// the new designs. Checks numbers on the generated parts (heights, widths, ratios); it does NOT judge how good the person looks
// (that is what the headless pictures in docs/images/guided_modeling/human_* are for).
#include "test_framework.hpp"
#include "okay/Modeling/Guided.hpp"
#include <cmath>
#include <cstdio>
#include <set>

using namespace okay;
using namespace okay::modeling;
using namespace okay::modeling::guided;

namespace {
struct Box { Vec3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f}; float W() const { return hi.x - lo.x; } float H() const { return hi.y - lo.y; } float D() const { return hi.z - lo.z; } bool Empty() const { return lo.x > hi.x; } };
Box BoundsOf(const std::vector<GeneratedPart>& parts, const std::function<bool(const GeneratedPart&)>& pick) {
    Box b;
    for (const GeneratedPart& p : parts) if (pick(p)) for (const Vec3& v : p.mesh.verts) {
        b.lo = {std::fmin(b.lo.x, v.x), std::fmin(b.lo.y, v.y), std::fmin(b.lo.z, v.z)};
        b.hi = {std::fmax(b.hi.x, v.x), std::fmax(b.hi.y, v.y), std::fmax(b.hi.z, v.z)};
    }
    return b;
}
Recipe Human(const char* start, double detail) { RegisterBuiltinTemplates(); Recipe r = DefaultRecipe(*Registry::Instance().Find("human"), start); r.params["detail"] = detail; return r; }
int Tris(const Recipe& r) { return TriangleCount(Create(r)); }
bool Is(const GeneratedPart& p, const char* prefix) { return p.id.rfind(prefix, 0) == 0; }
}

int main() {
    RUN_SUITE("guided human");
    RegisterBuiltinTemplates();
    const Template& T = *Registry::Instance().Find("human");

    // ---- triangle budgets: about 2-3 thousand at Low, 8 thousand at Medium, 20 thousand at High, more at Ultra ----------------------
    {
        int t[4];
        for (int lv = 0; lv < 4; ++lv) t[lv] = Tris(Human("average", lv));
        std::printf("  average human triangles: Low %d, Medium %d, High %d, Ultra %d\n", t[0], t[1], t[2], t[3]);
        CHECK(t[0] >= 1500 && t[0] <= 4500);
        CHECK(t[1] >= 5000 && t[1] <= 12000);
        CHECK(t[2] >= 14000 && t[2] <= 32000);
        CHECK(t[3] > t[2] * 1.3 && t[3] <= 80000);
    }

    // ---- proportions: an adult is about 7.5 heads tall, shoulders wider than hips, arms reach mid thigh ----------------------------------
    for (int lv = 0; lv < 4; ++lv) {
        const Recipe r = Human("average", lv);
        const std::vector<GeneratedPart> parts = Generate(r);
        const Box body = BoundsOf(parts, [](const GeneratedPart& p) { return Is(p, "torso") || Is(p, "head") || Is(p, "leg") || Is(p, "foot") || Is(p, "neck"); });
        const Box head = BoundsOf(parts, [](const GeneratedPart& p) { return Is(p, "head"); });
        const Box torso = BoundsOf(parts, [](const GeneratedPart& p) { return Is(p, "torso"); });
        const Box legs = BoundsOf(parts, [](const GeneratedPart& p) { return Is(p, "leg") || Is(p, "foot"); });
        const Box hand = BoundsOf(parts, [](const GeneratedPart& p) { return p.id == "hand/1"; });
        const Box foot = BoundsOf(parts, [](const GeneratedPart& p) { return p.id == "foot/1"; });
        CHECK(!body.Empty() && !head.Empty() && !torso.Empty() && !hand.Empty());
        if (body.Empty() || head.Empty() || torso.Empty() || hand.Empty()) continue;
        const float H = body.H();
        std::printf("  level %d: height %.3f, head %.3f (1/%.2f), torso width %.3f, hand %.3f, foot %.3f\n", lv, H, head.H(), H / head.H(), torso.W(), hand.H(), foot.D());
        CHECK(H > 1.70f && H < 1.82f);                                 // the 1.75 m slider (a little taller with hair, no)
        CHECK(H / head.H() > 6.8f && H / head.H() < 8.4f);             // seven and a half heads, give or take
        CHECK(torso.W() > legs.W() * 0.55f);                          // shoulders are wider than one leg pair's half
        CHECK(hand.H() > 0.13f && hand.H() < 0.24f);                  // a hand is about 0.18 m long
        CHECK(foot.D() > 0.2f && foot.D() < 0.32f);                   // a foot is about 0.26 m long
        CHECK(std::fabs(head.lo.y - (body.hi.y - head.H())) < 0.05f);  // the head sits on top
        CHECK(body.lo.y > -0.02f && body.lo.y < 0.03f);               // standing on the ground
        // the figure is symmetric: left and right arm and leg boxes mirror each other
        for (const char* pre : {"arm/", "leg/", "hand/", "foot/"}) {
            const Box a = BoundsOf(parts, [&](const GeneratedPart& p) { return Is(p, pre) && p.id.find("0") != std::string::npos; });
            const Box b = BoundsOf(parts, [&](const GeneratedPart& p) { return Is(p, pre) && p.id.find("1") != std::string::npos; });
            CHECK(std::fabs(a.lo.x + b.hi.x) < 0.04f && std::fabs(a.hi.x + b.lo.x) < 0.04f);
            CHECK(std::fabs(a.H() - b.H()) < 0.03f);
        }
    }

    // ---- sliders and body types change the body the way their names say ---------------------------------------------------------
    {
        auto torsoBox = [&](Recipe r) { return BoundsOf(Generate(r), [](const GeneratedPart& p) { return Is(p, "torso"); }); };
        auto whole = [&](Recipe r) { return BoundsOf(Generate(r), [](const GeneratedPart& p) { return Is(p, "torso") || Is(p, "head") || Is(p, "leg") || Is(p, "foot"); }); };
        Recipe base = Human("average", 1);
        Recipe slim = base; slim.params["build"] = 0.0;
        Recipe heavy = base; heavy.params["build"] = 1.0;
        CHECK(torsoBox(heavy).D() > torsoBox(slim).D() * 1.15f);       // build: the heavy body is deeper
        CHECK(torsoBox(heavy).W() > torsoBox(slim).W());
        Recipe tall = base; tall.params["height"] = 2.0;
        CHECK(whole(tall).H() > whole(base).H() * 1.12f);              // height
        Recipe wide = base; wide.params["shoulderWidth"] = 0.5;
        CHECK(torsoBox(wide).W() > torsoBox(base).W() * 1.1f);         // shoulder width
        Recipe male = base; male.params["bodyType"] = 1;
        Recipe female = base; female.params["bodyType"] = 2;
        CHECK(torsoBox(male).W() > torsoBox(female).W());              // men have wider shoulders (the torso box is the widest ring)
        auto armW = [&](Recipe r) { return BoundsOf(Generate(r), [](const GeneratedPart& p) { return p.id == "arm/1/upper"; }).D(); };
        Recipe buff = base; buff.params["muscle"] = 1.0; Recipe soft = base; soft.params["muscle"] = 0.0;
        CHECK(armW(buff) > armW(soft));                                // muscle: thicker arms
        CHECK(armW(heavy) > armW(slim));
        Recipe child = Human("child", 1);
        CHECK(whole(child).H() < 1.25f);
        const Box ch = BoundsOf(Generate(child), [](const GeneratedPart& p) { return Is(p, "head"); });
        CHECK(whole(child).H() / ch.H() < 6.3f);                       // a child's head is a bigger share of the height
    }

    // ---- every start design: valid parts at every level, the new designs, hands with fingers ----------------------------------------
    {
        CHECK(T.starts.size() >= 13);
        std::set<std::string> ids;
        for (const StartDesign& st : T.starts) ids.insert(st.id);
        for (const char* want : {"average", "slim", "athletic", "heavy", "child", "elder", "woman", "man"}) CHECK(ids.count(want) == 1);
        for (const StartDesign& st : T.starts)
            for (int lv = 0; lv < 4; ++lv) {
                Recipe r = Human(st.id.c_str(), lv);
                const std::vector<GeneratedPart> parts = Generate(r);
                const ValidationReport rep = Validate(parts, lv < 3);
                if (!rep.Ok() && !rep.problems.empty()) std::printf("  %s level %d: %s\n", st.id.c_str(), lv, rep.problems[0].c_str());
                CHECK(rep.Ok());
            }
        // fingers: the finger design has more triangles than the mitten and the block
        auto handTris = [&](const char* design) { Recipe r = Human("average", 2); r.partDesign["hand/1"] = design; return TriangleCount(Assemble(Generate(r))); };
        CHECK(handTris("fingers") > handTris("mitten"));
        CHECK(handTris("mitten") > handTris("block"));
        // every hair style, hat and glasses design builds something that is valid
        for (const char* h : {"short", "long", "mohawk", "bun", "spiky", "buzz", "bob", "ponytail", "afro", "curly", "sidepart", "braid"}) {
            Recipe r = Human("average", 2); r.slotDesign["hair"] = h;
            const std::vector<GeneratedPart> parts = Generate(r);
            bool has = false; for (const GeneratedPart& p : parts) if (p.slot == "hair" && !p.mesh.faces.empty()) has = true;
            CHECK(has);
            CHECK(Validate(parts).Ok());
        }
        for (const char* h : {"cap", "tophat", "helmet", "beanie"}) { Recipe r = Human("average", 2); r.slotDesign["hat"] = h; CHECK(Validate(Generate(r)).Ok()); }
        for (const char* g : {"round", "square", "visor"}) { Recipe r = Human("average", 2); r.slotDesign["glasses"] = g; CHECK(Validate(Generate(r)).Ok()); }
    }

    // ---- the same recipe gives the same mesh, and a joint has real rings around it (elbows, knees: at least 4 rings in each limb part) -------
    {
        const Recipe r = Human("average", 2);
        const PolyMesh a = Create(r), b = Create(r);
        CHECK(a.verts.size() == b.verts.size() && a.faces.size() == b.faces.size());
        bool same = a.verts.size() == b.verts.size();
        for (size_t i = 0; same && i < a.verts.size(); ++i) same = a.verts[i].x == b.verts[i].x && a.verts[i].y == b.verts[i].y && a.verts[i].z == b.verts[i].z;
        CHECK(same);
        const std::vector<GeneratedPart> parts = Generate(r);
        for (const char* id : {"arm/1/upper", "arm/1/fore", "leg/1/upper", "leg/1/lower"}) {
            std::set<int> ys;
            for (const GeneratedPart& p : parts) if (p.id == id) for (const Vec3& v : p.mesh.verts) ys.insert((int)std::lround(v.y * 1000.0f));
            CHECK((int)ys.size() >= 8);   // many distinct heights: a limb cut into rings, so it bends smoothly
        }
    }

    // ---- the head's posture: the Frankfurt plane (ear to the bottom of the eye) is level within 5 degrees, the nose tip is below the eye line,
    // ---- the chin is not more than 2 cm behind the lower lip and the nose does not stick out more than 5 cm ----------------------------------
    for (int lv = 0; lv < 4; ++lv)
        for (const char* start : {"average", "woman", "man", "child", "elder"}) {
            const std::vector<GeneratedPart> parts = Generate(Human(start, lv));
            auto box = [&](const char* id) { return BoundsOf(parts, [&](const GeneratedPart& p) { return p.id == id; }); };
            const Box ear = box("ears/0"), eye = box("eyewhite/0"), nose = box("nose/0"), mouth = box("mouth/0"), head = box("head/0");
            CHECK(!ear.Empty() && !eye.Empty() && !nose.Empty() && !mouth.Empty() && !head.Empty());
            if (ear.Empty() || eye.Empty() || nose.Empty() || mouth.Empty() || head.Empty()) continue;
            const float earY = (ear.lo.y + ear.hi.y) * 0.5f, earZ = (ear.lo.z + ear.hi.z) * 0.5f;
            const float eyeLowY = eye.lo.y, eyeZ = (eye.lo.z + eye.hi.z) * 0.5f;   // the lower rim of the eye
            const float deg = std::atan2(eyeLowY - earY, eyeZ - earZ) * 57.29578f;
            if (lv == 2) std::printf("  head posture %s: Frankfurt plane %.1f deg (0 = level, positive = face up)\n", start, deg);
            CHECK(std::fabs(deg) < 5.0f);
            CHECK(nose.lo.y < eye.lo.y);                                   // the tip of the nose is below the eye line
            CHECK(nose.hi.z - eye.hi.z < 0.05f * (head.H() / 0.247f));     // a nose that does not stick out like a beak
            float chinZ = -9, lowY = head.lo.y;
            for (const GeneratedPart& p : parts) if (p.id == "head/0") for (const Vec3& v : p.mesh.verts) if (v.y < lowY + 0.03f * head.H() / 0.247f) chinZ = std::fmax(chinZ, v.z);
            CHECK(chinZ > mouth.hi.z - 0.025f * (head.H() / 0.247f));      // the chin is not far behind the lips
        }
    TEST_MAIN_RESULT();
}
