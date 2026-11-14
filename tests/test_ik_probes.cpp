#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/FootIK.hpp"
#include "okay/Components/LookAtIK.hpp"
#include "okay/Components/FullBodyIK.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include <cmath>

using namespace okay;

static void Tick(Scene& s, int n) { for (int i = 0; i < n; ++i) s.Update(1.0f / 60.0f); }
static Vec3 Fwd(const Transform* t) { return (t->Rotation() * Vec3::Forward).Normalized(); }

// A parented leg (hip > knee > foot > toe) standing on flat ground, with a step
// starting 3 cm ahead of the ankle: the toes hang over it.
struct StepLeg {
    Scene s;
    GameObject *rig, *hip, *knee, *foot, *toe, *step;
    FootIK* ik;
    explicit StepLeg(float stepTop) {
        auto box = [&](const char* n, Vec3 pos, Vec3 scale) {
            GameObject* g = s.CreateGameObject(n);
            g->transform->localPosition = pos; g->transform->localScale = scale;
            g->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
            g->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
            return g;
        };
        box("Ground", {0, -0.5f, 0}, {10, 1, 10});                              // top at y = 0
        step = box("Step", {0, stepTop - 0.5f, 1.03f}, {4, 1, 2});              // top at stepTop, from z = 0.03
        rig  = s.CreateGameObject("Rig");
        hip  = s.CreateGameObject("LeftUpLeg"); hip->transform->SetParent(rig->transform, false);   hip->transform->localPosition = {0, 1.0f, 0};
        knee = s.CreateGameObject("LeftLeg");   knee->transform->SetParent(hip->transform, false);  knee->transform->localPosition = {0, -0.5f, 0.02f};
        foot = s.CreateGameObject("LeftFoot");  foot->transform->SetParent(knee->transform, false); foot->transform->localPosition = {0, -0.5f, -0.02f};
        toe  = s.CreateGameObject("LeftToe");   toe->transform->SetParent(foot->transform, false);  toe->transform->localPosition = {0, 0, 0.15f};
        ik = rig->AddComponent<FootIK>();
        ik->leftHipName = "LeftUpLeg"; ik->leftKneeName = "LeftLeg"; ik->leftFootName = "LeftFoot";
        ik->useRaycast = true; ik->footOffset = 0.0f; ik->smoothing = 0.0f;
    }
};

int main() {
    RUN_SUITE("ik_probes");

    // ---- One ray under the ankle: the foot stays on the low ground, toes in the step.
    {
        StepLeg L(0.1f);
        L.s.Start(); Tick(L.s, 60);
        CHECK(std::fabs(L.foot->transform->Position().y) < 0.01f);
    }
    // ---- Toe and heel rays: the foot rests on the step and the toes tilt up over
    // the edge (with Align To Slope); without it the foot only rises.
    {
        StepLeg L(0.1f);
        L.ik->stepProbes = true; L.ik->alignToGround = true;
        L.s.Start(); Tick(L.s, 60);
        CHECK(std::fabs(L.foot->transform->Position().y - 0.1f) < 0.015f);
        CHECK(Fwd(L.foot->transform).y > 0.3f);
        CHECK(L.knee->transform->Position().z > 0.05f);   // the knee bent forward to shorten the leg

        StepLeg L2(0.1f);
        L2.ik->stepProbes = true; L2.ik->alignToGround = false;
        L2.s.Start(); Tick(L2.s, 60);
        CHECK(std::fabs(L2.foot->transform->Position().y - 0.1f) < 0.015f);
        CHECK(std::fabs(Fwd(L2.foot->transform).y) < 0.05f);
    }
    // ---- A wall under the toes (50 cm up) is not a step: ignored.
    {
        StepLeg L(0.5f);
        L.ik->stepProbes = true; L.ik->alignToGround = true;
        L.s.Start(); Tick(L.s, 60);
        CHECK(std::fabs(L.foot->transform->Position().y) < 0.01f);
        CHECK(std::fabs(Fwd(L.foot->transform).y) < 0.05f);
    }
    // ---- Probes off again: nothing accumulates, the foot is back on the ground.
    {
        StepLeg L(0.1f);
        L.ik->stepProbes = true; L.ik->alignToGround = true;
        L.s.Start(); Tick(L.s, 60);
        L.ik->stepProbes = false; L.ik->alignToGround = false;
        Tick(L.s, 60);
        CHECK(std::fabs(L.foot->transform->Position().y) < 0.01f);
        CHECK(std::fabs(Fwd(L.foot->transform).y) < 0.02f);
    }

    // ---- Look-At IK distance: full look inside the range, none beyond it, half
    // way through the fade band.
    {
        Scene s("range"); s.physicsEnabled = false;
        auto* npc  = s.CreateGameObject("NPC");
        auto* head = s.CreateGameObject("Head"); head->transform->SetParent(npc->transform, false); head->transform->localPosition = {0, 1.7f, 0};
        auto* ik = npc->AddComponent<LookAtIK>();
        ik->chain = {head->transform}; ik->maxAngle = 180.0f; ik->smoothing = 0.0f; ik->clampAngle = 0.0f;
        ik->maxDistance = 5.0f; ik->fadeDistance = 1.0f;
        auto turn = [&]() { return Quat::Angle(head->transform->localRotation, Quat::Identity); };
        ik->target = {3.0f, 1.7f, 0.0f};
        s.Start(); Tick(s, 2);
        CHECK(std::fabs(turn() - 90.0f) < 1.0f);
        ik->target = {10.0f, 1.7f, 0.0f};
        Tick(s, 2);
        CHECK(turn() < 0.01f);
        ik->target = {4.5f, 1.7f, 0.0f};
        Tick(s, 2);
        CHECK(std::fabs(turn() - 45.0f) < 1.5f);
        ik->maxDistance = 0.0f;   // off: any distance
        ik->target = {10.0f, 1.7f, 0.0f};
        Tick(s, 2);
        CHECK(std::fabs(turn() - 90.0f) < 1.0f);
    }

    // ---- Full Body IK stretch: a hand goal beyond the arm lengthens it, up to the
    // limit; a reachable goal brings the animated lengths back.
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
        fb->blendSpeed = 0.0f; fb->softness = 0.0f; fb->bodyPull = 0.0f; fb->spineReach = 0.0f; fb->armMinBend = 0.0f;
        for (auto& e : fb->effectors) e.targetBlendTime = 0.0f;
        sc.Start();
        CHECK(fb->bones[FullBodyIK::LHand] == lh->transform);
        const Vec3 shoulder = lua->transform->Position();
        auto& eh = fb->effectors[FullBodyIK::LeftHand];
        eh.enabled = true; eh.position = shoulder + Vec3{0.7f, 0.0f, 0.0f};   // 0.1 past the 0.6 reach
        auto upper = [&]() { return (lfa->transform->Position() - lua->transform->Position()).Magnitude(); };
        auto handX = [&]() { return lh->transform->Position().x - shoulder.x; };
        Tick(sc, 2);
        CHECK(std::fabs(handX() - 0.6f) < 0.005f);         // no stretch: stops short
        CHECK(std::fabs(upper() - 0.3f) < 1e-4f);
        fb->stretch = 0.2f;
        Tick(sc, 1);
        CHECK(std::fabs(handX() - 0.7f) < 0.005f);
        CHECK(std::fabs(upper() - 0.35f) < 1e-3f);
        eh.position = shoulder + Vec3{0.9f, 0.0f, 0.0f};   // past the limit: 0.72 at most
        Tick(sc, 1);
        CHECK(std::fabs(handX() - 0.72f) < 0.005f);
        eh.position = shoulder + Vec3{0.4f, -0.1f, 0.2f};  // reachable: lengths restored
        Tick(sc, 1);
        CHECK(std::fabs(upper() - 0.3f) < 1e-4f);
        CHECK((lh->transform->Position() - eh.position).Magnitude() < 0.01f);
        Tick(sc, 30);
        CHECK(std::fabs(upper() - 0.3f) < 1e-4f);

        // Round trip.
        const std::string text = SceneSerializer::Serialize(sc);
        Scene s2("s2");
        CHECK(SceneSerializer::Deserialize(s2, text));
        auto* fb2 = s2.Find("Rig")->GetComponent<FullBodyIK>();
        CHECK(fb2 && std::fabs(fb2->stretch - 0.2f) < 1e-5f);
    }

    // ---- Foot IK probes and Look-At range ride along in the scene file.
    {
        Scene s("rt");
        auto* g = s.CreateGameObject("G");
        auto* f = g->AddComponent<FootIK>();
        f->leftHipName = "a"; f->leftKneeName = "b"; f->leftFootName = "c";
        f->stepProbes = true; f->toeReach = 0.2f; f->heelReach = 0.07f;
        auto* l = g->AddComponent<LookAtIK>();
        l->chainNames = {"Head"}; l->maxDistance = 6.0f; l->fadeDistance = 1.5f;
        const std::string text = SceneSerializer::Serialize(s);
        Scene s2("rt2");
        CHECK(SceneSerializer::Deserialize(s2, text));
        auto* f2 = s2.Find("G")->GetComponent<FootIK>();
        CHECK(f2 && f2->stepProbes && std::fabs(f2->toeReach - 0.2f) < 1e-5f && std::fabs(f2->heelReach - 0.07f) < 1e-5f);
        auto* l2 = s2.Find("G")->GetComponent<LookAtIK>();
        CHECK(l2 && std::fabs(l2->maxDistance - 6.0f) < 1e-5f && std::fabs(l2->fadeDistance - 1.5f) < 1e-5f);
    }

    TEST_MAIN_RESULT();
}
