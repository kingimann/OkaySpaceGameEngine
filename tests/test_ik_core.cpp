#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Math/Fabrik.hpp"
#include "okay/Math/TwoBoneIK.hpp"
#include "okay/Math/IKFade.hpp"
#include "okay/Components/ChainIK.hpp"
#include "okay/Components/FullBodyIK.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Scripting/OkayScriptVM.hpp"
#include <cmath>
#include <memory>
#include <string>

using namespace okay;

static float Dist(const Vec3& a, const Vec3& b) { return (a - b).Magnitude(); }
static float BendDeg(const Vec3& a, const Vec3& b, const Vec3& c) {
    Vec3 u = (b - a).Normalized(), v = (c - b).Normalized();
    float d = Vec3::Dot(u, v); d = d > 1 ? 1 : (d < -1 ? -1 : d);
    return std::acos(d) * 57.29578f;
}

int main() {
    RUN_SUITE("ik_core");

    // ---- FABRIK pole: a 3-bone chain reaching a near target bends toward the pole.
    {
        std::vector<Vec3> j = {{0, 0, 0}, {0, 1, 0}, {0, 2, 0}, {0, 3, 0}};
        std::vector<float> len = {1, 1, 1};
        Vec3 pole{0, 1.5f, 5.0f};
        float err = SolveFabrikConstrained(j, len, {0, 1.5f, 0.5f}, 30, 1e-3f, &pole, 0.0f);
        CHECK(err < 0.01f);
        CHECK(j[1].z > 0.1f && j[2].z > 0.1f);                       // bent toward +Z (the pole)
        for (int i = 0; i < 3; ++i) CHECK_NEAR(Dist(j[i], j[i + 1]), 1.0f, 1e-3f);   // lengths kept
        // Pole on the other side flips the bend.
        std::vector<Vec3> k = {{0, 0, 0}, {0, 1, 0}, {0, 2, 0}, {0, 3, 0}};
        Vec3 back{0, 1.5f, -5.0f};
        SolveFabrikConstrained(k, len, {0, 1.5f, 0.5f}, 30, 1e-3f, &back, 0.0f);
        CHECK(k[1].z < -0.05f);
    }
    // ---- Bend limits: no joint bends more than the limit.
    {
        std::vector<Vec3> j = {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {3, 0, 0}, {4, 0, 0}};
        std::vector<float> len = {1, 1, 1, 1};
        SolveFabrikConstrained(j, len, {0.5f, 0.5f, 0}, 40, 1e-3f, nullptr, 45.0f);
        for (int i = 1; i < 4; ++i) CHECK(BendDeg(j[i - 1], j[i], j[i + 1]) <= 45.5f);
        for (int i = 0; i < 4; ++i) CHECK_NEAR(Dist(j[i], j[i + 1]), 1.0f, 1e-3f);
    }
    // ---- Soft IK: eases toward full reach instead of snapping straight.
    {
        CHECK_NEAR(SoftReachDistance(1.0f, 2.0f, 0.1f), 1.0f, 1e-6f);       // well inside: untouched
        float a = SoftReachDistance(1.95f, 2.0f, 0.1f), b = SoftReachDistance(2.5f, 2.0f, 0.1f);
        CHECK(a < 1.95f && a > 1.9f);
        CHECK(b < 2.0f && b > a);                                            // never fully straight
        Vec3 mid, end;
        SolveTwoBoneIKSoft({0, 0, 0}, 1, 1, {0, -3, 0}, {0, 0, 1}, 0.1f, mid, end);
        CHECK(Dist({0, 0, 0}, end) < 2.0f);
        CHECK(mid.z > 0.0f);                                                  // still slightly bent, toward the pole
    }
    // ---- IKFade eases in over fadeTime with a smoothstep.
    {
        IKFade f;
        CHECK(f.Step(false, 0.5f, 0.016f) == 0.0f);   // starts at the target state
        float v1 = f.Step(true, 0.5f, 0.25f), v2 = f.Step(true, 0.5f, 0.25f);
        CHECK_NEAR(v1, 0.5f, 1e-4f);
        CHECK_NEAR(v2, 1.0f, 1e-4f);
    }
    // ---- CCD keeps to the pole and the bend cap too (it used to ignore both).
    {
        std::vector<Vec3> j = {{0, 0, 0}, {0, 1, 0}, {0, 2, 0}, {0, 3, 0}};
        std::vector<float> len = {1, 1, 1};
        Vec3 pole{0, 1.5f, 5.0f};
        ChainConstraints c; c.pole = &pole;
        float err = SolveChainConstrained(j, len, {0, 1.5f, 0.5f}, 60, 1e-3f, c, true);
        CHECK(err < 0.05f);
        CHECK(j[1].z > 0.1f || j[2].z > 0.1f);                      // bent toward +Z (the pole)
        std::vector<Vec3> k = {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {3, 0, 0}, {4, 0, 0}};
        std::vector<float> l4 = {1, 1, 1, 1};
        ChainConstraints cap; cap.maxBendDeg = 45.0f;
        SolveChainConstrained(k, l4, {0.5f, 0.5f, 0}, 60, 1e-3f, cap, true);
        for (int i = 1; i < 4; ++i) CHECK(BendDeg(k[i - 1], k[i], k[i + 1]) <= 45.5f);
        for (int i = 0; i < 4; ++i) CHECK_NEAR(Dist(k[i], k[i + 1]), 1.0f, 1e-3f);
    }
    // ---- Per-joint limits: a swing cone on one joint, hinges that bend in one plane.
    {
        std::vector<Vec3> j = {{0, 0, 0}, {0, 1, 0}, {0, 2, 0}, {0, 3, 0}, {0, 4, 0}};
        std::vector<float> len = {1, 1, 1, 1};
        std::vector<Quat> rot(5, Quat::Identity);
        std::vector<JointLimit> lim(5);
        lim[2].maxSwing = 20.0f;
        ChainConstraints c; c.limits = &lim; c.rest = &j; c.restRot = &rot;
        std::vector<Vec3> rest = j, a = j;
        c.rest = &rest;
        SolveChainConstrained(a, len, {1.5f, 1.0f, 0.0f}, 60, 1e-3f, c);
        CHECK(BendDeg(a[1], a[2], a[3]) <= 20.5f);
        for (int i = 0; i < 4; ++i) CHECK_NEAR(Dist(a[i], a[i + 1]), 1.0f, 1e-3f);

        std::vector<JointLimit> hinge(5);
        for (int i = 1; i < 4; ++i) { hinge[i].hinge = true; hinge[i].axis = {1, 0, 0}; }   // bend about X only
        ChainConstraints h; h.limits = &hinge; h.rest = &rest; h.restRot = &rot;
        std::vector<Vec3> b = rest;
        SolveChainConstrained(b, len, {0.6f, 1.2f, 1.5f}, 60, 1e-3f, h);
        for (int i = 1; i < 4; ++i) {   // each hinged bend stays across its (turned) axis
            Vec3 axis = Quat::FromToRotation(rest[i] - rest[i - 1], b[i] - b[i - 1]) * Vec3{1, 0, 0};
            CHECK(std::fabs(Vec3::Dot((b[i + 1] - b[i]).Normalized(), axis.Normalized())) < 0.02f);
        }
        for (int i = 0; i < 4; ++i) CHECK_NEAR(Dist(b[i], b[i + 1]), 1.0f, 1e-3f);
        // A zero-length chain with the target on its root stays finite.
        std::vector<Vec3> z = {{1, 1, 1}, {1, 1, 1}};
        SolveFabrik(z, {0.0f}, {1, 1, 1});
        CHECK(std::isfinite(z[1].x) && std::isfinite(z[1].y) && std::isfinite(z[1].z));
    }
    // ---- ChainIK on a parented bone chain: the bones turn root to tip (lengths
    // kept, tip on target), a weight bends part of the way, a still rig doesn't
    // drift, and a NaN target leaves the pose.
    {
        struct Rig { std::unique_ptr<Scene> s; std::vector<GameObject*> b; ChainIK* ci; };
        auto build = [](Vec3 target, bool orient) {
            Rig r;
            r.s = std::make_unique<Scene>("chain");
            GameObject* parent = nullptr;
            for (int i = 0; i < 4; ++i) {
                GameObject* g = r.s->CreateGameObject("c" + std::to_string(i));
                if (parent) g->transform->SetParent(parent->transform, false);
                g->transform->SetPosition({0, (float)i, 0});
                r.b.push_back(parent = g);
            }
            r.ci = r.b[0]->AddComponent<ChainIK>();
            for (auto* x : r.b) r.ci->bones.push_back(x->transform);
            r.ci->target = target; r.ci->iterations = 30; r.ci->orient = orient;
            r.s->Start();
            return r;
        };
        auto P = [](GameObject* g) { return g->transform->Position(); };
        for (bool orient : {false, true}) {
            Rig r = build({1.5f, 1.5f, 0.5f}, orient);
            r.s->Update(1.0f / 60.0f);
            CHECK(Dist(P(r.b[3]), {1.5f, 1.5f, 0.5f}) < 0.02f);                 // tip on target, orient or not
            for (int i = 0; i < 3; ++i) CHECK_NEAR(Dist(P(r.b[i]), P(r.b[i + 1])), 1.0f, 1e-3f);
            const Vec3 tip = P(r.b[3]);
            for (int i = 0; i < 30; ++i) r.s->Update(1.0f / 60.0f);
            CHECK(Dist(P(r.b[3]), tip) < 1e-4f);                                 // no pile-up
        }
        Rig half = build({1.5f, 1.5f, 0.5f}, false);
        half.ci->weight = 0.5f;
        half.s->Update(1.0f / 60.0f);
        for (int i = 0; i < 3; ++i) CHECK_NEAR(Dist(P(half.b[i]), P(half.b[i + 1])), 1.0f, 1e-3f);
        CHECK(Dist(P(half.b[3]), {0, 3, 0}) > 0.3f && Dist(P(half.b[3]), {1.5f, 1.5f, 0.5f}) > 0.3f);   // part way
        Rig bad = build({std::nanf(""), 0, 0}, false);
        bad.s->Update(1.0f / 60.0f);
        CHECK(Dist(P(bad.b[3]), {0, 3, 0}) < 1e-5f);
        // Limits survive save / load.
        Rig ser = build({1, 1, 0}, false);
        ser.ci->limits.resize(3);
        ser.ci->limits[1].maxSwing = 30.0f; ser.ci->limits[2].hinge = true; ser.ci->limits[2].axis = {0, 0, 1};
        std::string text = SceneSerializer::Serialize(*ser.s);
        Scene s2;
        CHECK(SceneSerializer::Deserialize(s2, text));
        ChainIK* c2 = s2.Find("c0") ? s2.Find("c0")->GetComponent<ChainIK>() : nullptr;
        CHECK(c2 && c2->limits.size() == 3);
        if (c2 && c2->limits.size() == 3) {
            CHECK_NEAR(c2->limits[1].maxSwing, 30.0f, 1e-4f);
            CHECK(c2->limits[2].hinge && std::fabs(c2->limits[2].axis.z - 1.0f) < 1e-4f);
        }
    }
    // ---- ChainIK with a pole object via the scene.
    {
        Scene sc;
        std::vector<GameObject*> g;
        for (int i = 0; i < 4; ++i) { g.push_back(sc.CreateGameObject("b" + std::to_string(i))); g.back()->transform->SetPosition({0, (float)i, 0}); }
        GameObject* tgt = sc.CreateGameObject("tgt"); tgt->transform->SetPosition({0, 1.5f, 0.6f});
        GameObject* pl = sc.CreateGameObject("pole"); pl->transform->SetPosition({0, 1.5f, 4});
        auto* ci = g[0]->AddComponent<ChainIK>();
        for (auto* x : g) ci->bones.push_back(x->transform);
        ci->targetObject = tgt->transform; ci->poleObject = pl->transform; ci->iterations = 30;
        sc.Start(); sc.Update(1.0f / 60.0f);
        CHECK(ci->Error() < 0.02f);
        CHECK(g[1]->transform->Position().z > 0.05f);
    }
    // ---- FullBodyIK: auto-maps a Mixamo-style rig and plants a hand on a target.
    {
        CHECK(FullBodyIK::Classify("mixamorig:LeftArm") == FullBodyIK::LUpperArm);
        CHECK(FullBodyIK::Classify("mixamorig:RightForeArm") == FullBodyIK::RForeArm);
        CHECK(FullBodyIK::Classify("UpperArm.L") == FullBodyIK::LUpperArm);
        CHECK(FullBodyIK::Classify("thigh_r") == FullBodyIK::RThigh);
        CHECK(FullBodyIK::Classify("calf_l") == FullBodyIK::LShin);
        CHECK(FullBodyIK::Classify("L Hand") == FullBodyIK::LHand);
        CHECK(FullBodyIK::Classify("mixamorig:LeftHandIndex1") == -1);
        CHECK(FullBodyIK::Classify("mixamorig:Hips") == FullBodyIK::Hips);
        CHECK(FullBodyIK::Classify("mixamorig:Head") == FullBodyIK::Head);

        Scene sc;
        GameObject* root = sc.CreateGameObject("Rig");
        auto mk = [&](const char* n, GameObject* parent, Vec3 local) {
            GameObject* o = sc.CreateGameObject(n);
            o->transform->SetParent(parent->transform, false);
            o->transform->localPosition = local;
            return o;
        };
        GameObject* hips = mk("mixamorig:Hips", root, {0, 1, 0});
        GameObject* spine = mk("mixamorig:Spine", hips, {0, 0.2f, 0});
        GameObject* chest = mk("mixamorig:Spine2", spine, {0, 0.3f, 0});
        GameObject* lua = mk("mixamorig:LeftArm", chest, {0.2f, 0.1f, 0});
        GameObject* lfa = mk("mixamorig:LeftForeArm", lua, {0.3f, 0, 0});
        GameObject* lh  = mk("mixamorig:LeftHand", lfa, {0.3f, 0, 0});
        GameObject* lth = mk("mixamorig:LeftUpLeg", hips, {0.1f, 0, 0});
        GameObject* lsh = mk("mixamorig:LeftLeg", lth, {0, -0.45f, 0.01f});
        mk("mixamorig:LeftFoot", lsh, {0, -0.45f, -0.01f});
        mk("mixamorig:Head", chest, {0, 0.25f, 0});
        auto* fb = root->AddComponent<FullBodyIK>();
        fb->blendSpeed = 0.0f;   // goals apply at once (easing is tested below)
        sc.Start();
        CHECK(fb->bones[FullBodyIK::Hips] == hips->transform);
        CHECK(fb->bones[FullBodyIK::Chest] == chest->transform);
        CHECK(fb->bones[FullBodyIK::LHand] == lh->transform);
        CHECK(fb->bones[FullBodyIK::LShin] == lsh->transform);

        // Reachable hand target: the hand gets there, bone lengths unchanged.
        Vec3 shoulder = lua->transform->Position();
        Vec3 goal = shoulder + Vec3{0.3f, -0.2f, 0.25f};
        auto& eh = fb->effectors[FullBodyIK::LeftHand];
        eh.enabled = true; eh.position = goal;
        fb->softness = 0.0f; fb->bodyPull = 0.0f; fb->spineReach = 0.0f;
        sc.Update(1.0f / 60.0f);
        CHECK(Dist(lh->transform->Position(), goal) < 0.01f);
        CHECK_NEAR(Dist(lua->transform->Position(), lfa->transform->Position()), 0.3f, 1e-3f);
        CHECK_NEAR(Dist(lfa->transform->Position(), lh->transform->Position()), 0.3f, 1e-3f);
        // Looking aside turns the chest too, but before the arms solve: the hand
        // still lands on its goal.
        fb->lookEnabled = true; fb->lookPosition = shoulder + Vec3{3.0f, 0.5f, 2.0f}; fb->lookSmoothing = 0.0f;
        sc.Update(1.0f / 60.0f);
        CHECK(Dist(lh->transform->Position(), goal) < 0.01f);
        fb->lookEnabled = false;
        sc.Update(1.0f / 60.0f);

        // Far target: body pull moves the hips toward it (and stays clamped).
        Vec3 hips0 = hips->transform->Position();
        eh.position = shoulder + Vec3{2.0f, 0, 0};
        fb->bodyPull = 1.0f; fb->maxBodyOffset = 0.3f;
        sc.Update(1.0f / 60.0f);
        Vec3 moved = hips->transform->Position() - hips0;
        CHECK(moved.x > 0.05f);
        CHECK(moved.Magnitude() <= 0.3001f);
        // No animation re-poses the hips: the offset must not compound over frames.
        for (int i = 0; i < 30; ++i) sc.Update(1.0f / 60.0f);
        CHECK((hips->transform->Position() - hips0).Magnitude() <= 0.3001f);

        // Scripts drive goals: ik_hand / ik_look / ik_release.
        OkayScriptVM vm; ScriptHost host; host.gameObject = root; host.transform = root->transform;
        vm.Bind(&host);
        CHECK(vm.Load("ik_hand(\"right\", 1, 2, 3, 0.5)\nik_look(0, 1.7, 5)\n"));
        CHECK(fb->effectors[FullBodyIK::RightHand].enabled);
        CHECK_NEAR(fb->effectors[FullBodyIK::RightHand].position.z, 3.0f, 1e-5f);
        CHECK_NEAR(fb->effectors[FullBodyIK::RightHand].positionWeight, 0.5f, 1e-5f);
        CHECK(fb->lookEnabled && std::fabs(fb->lookPosition.z - 5.0f) < 1e-5f);

        // Round trip through the scene format.
        fb->effectors[FullBodyIK::LeftFoot].hintName = "KneeHint";
        fb->maxSpineAngle = 22.0f;
        Scene loaded;
        CHECK(SceneSerializer::Deserialize(loaded, SceneSerializer::Serialize(sc)));
        FullBodyIK* fb2 = nullptr;
        for (const auto& up : loaded.Objects()) if (auto* f = up->GetComponent<FullBodyIK>()) fb2 = f;
        CHECK(fb2 != nullptr);
        if (fb2) {
            CHECK(fb2->boneNames[FullBodyIK::LHand] == "mixamorig:LeftHand");
            CHECK(fb2->effectors[FullBodyIK::LeftFoot].hintName == "KneeHint");
            CHECK(fb2->effectors[FullBodyIK::RightHand].enabled);
            CHECK_NEAR(fb2->maxSpineAngle, 22.0f, 1e-5f);
            CHECK(fb2->lookEnabled);
            CHECK_NEAR(fb2->blendSpeed, 0.0f, 1e-5f);
            CHECK_NEAR(fb2->armMinBend, 20.0f, 1e-5f);
        }
    }

    // ---- FullBodyIK: goals ease in/out, bones keep their length mid-blend,
    //      elbows don't fold flat, the chest bend spreads over the spine.
    {
        Scene sc;
        GameObject* root = sc.CreateGameObject("Rig");
        auto mk = [&](const char* n, GameObject* parent, Vec3 local) {
            GameObject* o = sc.CreateGameObject(n);
            o->transform->SetParent(parent->transform, false);
            o->transform->localPosition = local;
            return o;
        };
        GameObject* hips = mk("Hips", root, {0, 1, 0});
        GameObject* spine = mk("Spine", hips, {0, 0.2f, 0});
        GameObject* chest = mk("Chest", spine, {0, 0.3f, 0});
        GameObject* lua = mk("LeftArm", chest, {0.2f, 0.1f, 0});
        GameObject* lfa = mk("LeftForeArm", lua, {0.3f, 0, 0});
        GameObject* lh  = mk("LeftHand", lfa, {0.3f, 0, 0});
        auto* fb = root->AddComponent<FullBodyIK>();
        fb->softness = 0.0f; fb->bodyPull = 0.0f; fb->spineReach = 0.0f;
        sc.Start();
        const Vec3 shoulder = lua->transform->Position();
        const Vec3 handRest = lh->transform->Position();
        auto& e = fb->effectors[FullBodyIK::LeftHand];
        e.enabled = true; e.position = shoulder + Vec3{0.3f, -0.25f, 0.25f};
        // Default blendSpeed: one frame applies only part of the goal...
        sc.Update(1.0f / 60.0f);
        CHECK(fb->EffectiveWeight(FullBodyIK::LeftHand) > 0.05f && fb->EffectiveWeight(FullBodyIK::LeftHand) < 0.5f);
        CHECK(Dist(lh->transform->Position(), e.position) > 0.02f);
        // ...with bone lengths intact mid-blend...
        CHECK_NEAR(Dist(lua->transform->Position(), lfa->transform->Position()), 0.3f, 1e-3f);
        CHECK_NEAR(Dist(lfa->transform->Position(), lh->transform->Position()), 0.3f, 1e-3f);
        // ...and gets there.
        for (int i = 0; i < 60; ++i) sc.Update(1.0f / 60.0f);
        CHECK(fb->Reached(FullBodyIK::LeftHand, 0.01f));
        // Releasing eases back out (no pop to the animated pose in one frame).
        e.enabled = false;
        sc.Update(1.0f / 60.0f);
        CHECK(!fb->Reached(FullBodyIK::LeftHand));
        CHECK(fb->EffectiveWeight(FullBodyIK::LeftHand) > 0.5f);
        for (int i = 0; i < 90; ++i) sc.Update(1.0f / 60.0f);
        CHECK(fb->EffectiveWeight(FullBodyIK::LeftHand) == 0.0f);

        // A goal at the shoulder can't fold the elbow tighter than armMinBend.
        fb->blendSpeed = 0.0f;
        e.enabled = true; e.position = shoulder + Vec3{0.001f, 0, 0};
        sc.Update(1.0f / 60.0f);
        Vec3 u = lua->transform->Position() - lfa->transform->Position(), v = lh->transform->Position() - lfa->transform->Position();
        float ang = std::acos(std::fmax(-1.0f, std::fmin(1.0f, Vec3::Dot(u.Normalized(), v.Normalized())))) * 57.29578f;
        CHECK(ang >= fb->armMinBend - 0.5f);

        // Out-of-reach hand: spine AND chest both bend toward it.
        fb->spineReach = 1.0f; fb->maxSpineAngle = 40.0f;
        e.position = shoulder + Vec3{0.2f, 1.2f, 0.0f};   // far overhead
        Quat sp0 = spine->transform->localRotation, ch0 = chest->transform->localRotation;
        sc.Update(1.0f / 60.0f);
        CHECK(Quat::Angle(sp0, spine->transform->localRotation) > 1.0f);
        CHECK(Quat::Angle(ch0, chest->transform->localRotation) > 1.0f);
        (void)handRest;
    }
    TEST_MAIN_RESULT();
}
