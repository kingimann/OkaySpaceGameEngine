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
    // ---- FullBodyIK: corrections never build up on an un-animated rig ----
    {
        struct Rig { Scene sc; FullBodyIK* fb = nullptr; Transform *ua = nullptr, *fa = nullptr, *hand = nullptr; };
        auto build = [](Rig& r) {
            GameObject* root = r.sc.CreateGameObject("Rig");
            auto mk = [&](const char* n, GameObject* parent, Vec3 local) {
                GameObject* o = r.sc.CreateGameObject(n);
                o->transform->SetParent(parent->transform, false);
                o->transform->localPosition = local;
                return o;
            };
            GameObject* hips = mk("mixamorig:Hips", root, {0, 1, 0});
            GameObject* chest = mk("mixamorig:Spine2", mk("mixamorig:Spine", hips, {0, 0.2f, 0}), {0, 0.3f, 0});
            GameObject* lua = mk("mixamorig:LeftArm", chest, {0.2f, 0.1f, 0});
            GameObject* lfa = mk("mixamorig:LeftForeArm", lua, {0.3f, 0, 0.02f});
            GameObject* lh = mk("mixamorig:LeftHand", lfa, {0.3f, 0, -0.02f});
            r.fb = root->AddComponent<FullBodyIK>();
            r.fb->blendSpeed = 0.0f; r.fb->softness = 0.0f; r.fb->bodyPull = 0.0f; r.fb->spineReach = 0.0f;
            r.ua = lua->transform; r.fa = lfa->transform; r.hand = lh->transform;
            r.sc.Start();
        };
        Rig r; build(r);
        const Vec3 rest = r.hand->Position();
        const Quat restUa = r.ua->localRotation, restFa = r.fa->localRotation;
        auto& e = r.fb->effectors[FullBodyIK::LeftHand];
        const Vec3 goal = r.ua->Position() + Vec3{0.25f, -0.3f, 0.2f};
        e.enabled = true; e.position = goal;

        // Half weight stays half, frame after frame (no creep to the full solve).
        r.fb->weight = 0.5f;
        r.sc.Update(1.0f / 60.0f);
        const Vec3 half = r.hand->Position();
        CHECK(Dist(half, rest) > 0.05f);
        CHECK(Dist(half, goal) > 0.05f);
        for (int i = 0; i < 60; ++i) r.sc.Update(1.0f / 60.0f);
        CHECK(Dist(r.hand->Position(), half) < 1e-4f);
        // A bone chain turns, it isn't stretched.
        CHECK_NEAR(Dist(r.ua->Position(), r.fa->Position()), Dist(Vec3{0, 0, 0}, Vec3{0.3f, 0, 0.02f}), 1e-4f);
        CHECK_NEAR(Dist(r.fa->Position(), r.hand->Position()), Dist(Vec3{0, 0, 0}, Vec3{0.3f, 0, -0.02f}), 1e-4f);

        // Goal released: the arm is back on its own pose the same frame.
        e.enabled = false;
        r.sc.Update(1.0f / 60.0f);
        CHECK(Dist(r.hand->Position(), rest) < 1e-4f);
        CHECK(std::fabs(Quat::Dot(r.ua->localRotation, restUa)) > 0.99999f);
        CHECK(std::fabs(Quat::Dot(r.fa->localRotation, restFa)) > 0.99999f);

        // A broken goal (NaN) leaves the arm on its pose; nothing goes NaN.
        e.enabled = true; r.fb->weight = 1.0f;
        e.position = Vec3{std::nanf(""), 0, 0};
        for (int i = 0; i < 3; ++i) r.sc.Update(1.0f / 60.0f);
        CHECK(Finite(r.hand->Position()));
        CHECK(Dist(r.hand->Position(), rest) < 1e-4f);
        e.position = goal;
        r.sc.Update(1.0f / 60.0f);
        CHECK(Dist(r.hand->Position(), goal) < 0.01f);

        // Goal smoothing looks the same at 30 and 144 fps.
        Rig a, b; build(a); build(b);
        for (Rig* x : {&a, &b}) {
            x->fb->goalSmoothing = 4.0f;
            auto& ex = x->fb->effectors[FullBodyIK::LeftHand];
            ex.enabled = true; ex.position = x->ua->Position() + Vec3{0.3f, -0.2f, 0.25f};
            x->sc.Update(1.0f / 60.0f);
            ex.position = x->ua->Position() + Vec3{0.1f, -0.4f, 0.3f};
        }
        for (int i = 0; i < 15; ++i) a.sc.Update(1.0f / 30.0f);
        for (int i = 0; i < 72; ++i) b.sc.Update(1.0f / 144.0f);
        CHECK(Dist(a.hand->Position(), b.hand->Position()) < 2e-3f);

        // Per-limb joint limits save and load.
        r.fb->effectors[FullBodyIK::RightFoot].lowerLimit = IKJointLimit::Knee();
        Scene loaded;
        CHECK(SceneSerializer::Deserialize(loaded, SceneSerializer::Serialize(r.sc)));
        GameObject* lr = loaded.Find("Rig");
        FullBodyIK* lf = lr ? lr->GetComponent<FullBodyIK>() : nullptr;
        CHECK(lf != nullptr);
        if (lf) {
            CHECK(lf->effectors[FullBodyIK::RightFoot].lowerLimit.type == IKJointLimit::Type::Hinge);
            CHECK(!lf->effectors[FullBodyIK::RightFoot].upperLimit.Active());
            CHECK(!lf->effectors[FullBodyIK::LeftHand].lowerLimit.Active());
        }
    }

    // ---- Solve order: feet, holding, body, limbs, then aim/look ----
    {
        Scene sc;
        GameObject* g = sc.CreateGameObject("Order");
        const int foot = g->AddComponent<FootIK>()->ExecutionOrder();
        const int hold = g->AddComponent<ProceduralObjectHolding>()->ExecutionOrder();
        const int body = g->AddComponent<FullBodyIK>()->ExecutionOrder();
        const int limb = g->AddComponent<LimbIK>()->ExecutionOrder();
        const int look = g->AddComponent<LookAtIK>()->ExecutionOrder();
        CHECK(foot < hold && hold < body && body < limb && limb < look);
        CHECK(g->AddComponent<ChainIK>()->ExecutionOrder() == limb);
        CHECK(g->AddComponent<AimIK>()->ExecutionOrder() == look);
    }

    // ---- LimbIK: a switched target swings over targetBlendTime; a teleport doesn't trail ----
    {
        Scene sc;
        GameObject* body = sc.CreateGameObject("Body");
        auto mk = [&](const char* n, GameObject* parent, Vec3 local) {
            GameObject* o = sc.CreateGameObject(n);
            o->transform->SetParent(parent->transform, false);
            o->transform->localPosition = local;
            return o;
        };
        GameObject* sh = mk("Shoulder", body, {0.2f, 1.5f, 0});
        GameObject* el = mk("Elbow", sh, {0.3f, 0, -0.05f});
        GameObject* hand = mk("Hand", el, {0.3f, 0, 0.05f});
        GameObject* t1 = mk("T1", body, {0.6f, 1.3f, 0});
        GameObject* t2 = mk("T2", body, {0.5f, 1.6f, 0.35f});
        auto* ik = body->AddComponent<LimbIK>();
        ik->upper = sh->transform; ik->lower = el->transform; ik->end = hand->transform;
        ik->targetObject = t1->transform;
        sc.Start();
        CHECK_NEAR(ik->targetBlendTime, 0.15f, 1e-6f);
        sc.Update(1.0f / 60.0f);
        CHECK(Dist(hand->transform->Position(), t1->transform->Position()) < 0.01f);   // first goal: no blend
        ik->targetObject = t2->transform;
        sc.Update(1.0f / 60.0f);
        CHECK(Dist(hand->transform->Position(), t2->transform->Position()) > 0.1f);    // swinging over
        for (int i = 0; i < 12; ++i) sc.Update(1.0f / 60.0f);
        CHECK(Dist(hand->transform->Position(), t2->transform->Position()) < 0.01f);   // there after ~0.2 s
        body->transform->SetPosition({10, 0, 5});                                      // respawn: no trail
        sc.Update(1.0f / 60.0f);
        CHECK(Dist(hand->transform->Position(), t2->transform->Position()) < 0.01f);
    }

    // ---- AimIK switched off: an un-animated bone gets its own rotation back ----
    {
        Scene sc;
        GameObject* head = sc.CreateGameObject("Head");
        auto* aim = head->AddComponent<AimIK>();
        aim->target = {5, 0, 0};
        sc.Start();
        const Quat before = head->transform->localRotation;
        sc.Update(1.0f / 60.0f);
        CHECK(std::fabs(Quat::Dot(head->transform->localRotation, before)) < 0.999f);
        aim->weight = 0.0f;
        sc.Update(1.0f / 60.0f);
        CHECK(std::fabs(Quat::Dot(head->transform->localRotation, before)) > 0.99999f);
    }

    // ---- FullBodyIK body pull re-plants FootIK's feet; nothing builds up ----
    {
        Scene sc;
        GameObject* root = sc.CreateGameObject("Rig");
        auto mk = [&](const char* n, GameObject* parent, Vec3 local) {
            GameObject* o = sc.CreateGameObject(n);
            o->transform->SetParent(parent->transform, false);
            o->transform->localPosition = local;
            return o;
        };
        GameObject* hips = mk("mixamorig:Hips", root, {0, 1, 0});
        GameObject* chest = mk("mixamorig:Spine2", mk("mixamorig:Spine", hips, {0, 0.2f, 0}), {0, 0.3f, 0});
        GameObject* lua = mk("mixamorig:LeftArm", chest, {0.2f, 0.1f, 0});
        mk("mixamorig:LeftHand", mk("mixamorig:LeftForeArm", lua, {0.3f, 0, 0.02f}), {0.3f, 0, -0.02f});
        GameObject* leg[2][3];
        for (int i = 0; i < 2; ++i) {
            const float x = i == 0 ? 0.1f : -0.1f;
            leg[i][0] = mk(i == 0 ? "mixamorig:LeftUpLeg" : "mixamorig:RightUpLeg", hips, {x, 0, 0});
            leg[i][1] = mk(i == 0 ? "mixamorig:LeftLeg" : "mixamorig:RightLeg", leg[i][0], {0, -0.42f, 0.1f});
            leg[i][2] = mk(i == 0 ? "mixamorig:LeftFoot" : "mixamorig:RightFoot", leg[i][1], {0, -0.42f, -0.1f});
        }
        auto* feet = root->AddComponent<FootIK>();
        feet->leftHip = leg[0][0]->transform;  feet->leftKnee = leg[0][1]->transform;  feet->leftFoot = leg[0][2]->transform;
        feet->rightHip = leg[1][0]->transform; feet->rightKnee = leg[1][1]->transform; feet->rightFoot = leg[1][2]->transform;
        feet->adjustPelvis = false; feet->footLock = false; feet->alignToGround = false;
        auto* fb = root->AddComponent<FullBodyIK>();
        fb->blendSpeed = 0.0f; fb->softness = 0.0f; fb->spineReach = 0.0f;
        fb->bodyPull = 1.0f; fb->maxBodyOffset = 0.15f;
        auto& eh = fb->effectors[FullBodyIK::LeftHand];
        eh.enabled = true; eh.position = {3, 1.6f, 0};
        sc.Start();
        const Vec3 hips0 = hips->transform->Position();
        const Vec3 foot0[2] = {leg[0][2]->transform->Position(), leg[1][2]->transform->Position()};
        for (int f = 0; f < 30; ++f) sc.Update(1.0f / 60.0f);
        const Vec3 moved = hips->transform->Position() - hips0;
        CHECK(moved.x > 0.1f);
        CHECK(moved.Magnitude() <= 0.1501f);                   // clamped, and not compounding frame on frame
        for (int i = 0; i < 2; ++i) {
            CHECK(Dist(leg[i][2]->transform->Position(), foot0[i]) < 2e-3f);   // feet stayed planted
            CHECK_NEAR(Dist(leg[i][0]->transform->Position(), leg[i][1]->transform->Position()),
                       Dist(Vec3{0, 0, 0}, Vec3{0, -0.42f, 0.1f}), 1e-4f);    // turned, not stretched
        }
        // Without a FootIK the feet ride along with the hips (the old behaviour).
        feet->weight = 0.0f;
        sc.Update(1.0f / 60.0f);
        CHECK(Dist(leg[0][2]->transform->Position(), foot0[0] + moved) < 2e-3f);
    }

    TEST_MAIN_RESULT();
}
