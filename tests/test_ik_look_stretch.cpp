#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/LookAtIK.hpp"
#include "okay/Components/LimbIK.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include <cmath>

using namespace okay;

static Vec3 Fwd(const Transform* t) { return (t->Rotation() * Vec3::Forward).Normalized(); }
static float Deg(const Vec3& a, const Vec3& b) {
    float d = Vec3::Dot(a.Normalized(), b.Normalized()); d = d > 1 ? 1 : (d < -1 ? -1 : d);
    return std::acos(d) * 57.29578f;
}
static float LocalTurn(const Transform* t) { return Quat::Angle(t->localRotation, Quat::Identity); }

// Spine > Neck > Head stacked up the Y axis, all facing +Z, under an NPC root.
struct Neck3 {
    Scene s{"look"};
    GameObject *npc, *spine, *neck, *head;
    LookAtIK* ik;
    Neck3() {
        s.physicsEnabled = false;
        npc = s.CreateGameObject("NPC");
        spine = s.CreateGameObject("Spine"); spine->transform->SetParent(npc->transform, false);  spine->transform->localPosition = {0, 1.2f, 0};
        neck  = s.CreateGameObject("Neck");  neck->transform->SetParent(spine->transform, false); neck->transform->localPosition = {0, 0.4f, 0};
        head  = s.CreateGameObject("Head");  head->transform->SetParent(neck->transform, false);  head->transform->localPosition = {0, 0.15f, 0};
        ik = npc->AddComponent<LookAtIK>();
        ik->chain = {spine->transform, neck->transform, head->transform};
        ik->maxAngle = 180.0f; ik->smoothing = 0.0f; ik->clampAngle = 0.0f;
    }
    void Tick(int n = 2) { for (int i = 0; i < n; ++i) s.Update(1.0f / 60.0f); }
};

int main() {
    RUN_SUITE("ik_look_stretch");

    // ---- Default shares ramp up the chain: a 90 degree look splits 30 / 40 / 20
    // (a third of the turn, two thirds of what's left, the rest) and the head
    // still lands on the target.
    {
        Neck3 r;
        r.ik->target = {1000.0f, 1.75f, 0.0f};   // far to the right: every bone sees ~+X
        r.s.Start(); r.Tick();
        CHECK(std::fabs(LocalTurn(r.spine->transform) - 30.0f) < 1.0f);
        CHECK(std::fabs(LocalTurn(r.neck->transform) - 40.0f) < 1.0f);
        CHECK(std::fabs(LocalTurn(r.head->transform) - 20.0f) < 1.0f);
        CHECK(Fwd(r.head->transform).x > 0.999f);
    }

    // ---- Explicit shares: the spine takes half, the neck half of the rest, the
    // tip finishes. A share past the tip is ignored.
    {
        Neck3 r;
        r.ik->boneWeights = {0.5f, 0.5f, 0.1f};
        r.ik->target = {1000.0f, 1.75f, 0.0f};
        r.s.Start(); r.Tick();
        CHECK(std::fabs(LocalTurn(r.spine->transform) - 45.0f) < 1.0f);
        CHECK(std::fabs(LocalTurn(r.neck->transform) - 22.5f) < 1.0f);
        CHECK(std::fabs(LocalTurn(r.head->transform) - 22.5f) < 1.0f);
        CHECK(Fwd(r.head->transform).x > 0.999f);
        CHECK(LookAtIK::ShareOf({}, 2, 3) == 1.0f);
        CHECK(std::fabs(LookAtIK::ShareOf({}, 0, 3) - 1.0f / 3.0f) < 1e-5f);
        CHECK(LookAtIK::ShareOf({0.25f}, 0, 2) == 0.25f);
    }

    // ---- Clamp: a target 150 degrees round is watched from 100 degrees, on its
    // own side; off, the head turns all the way round.
    {
        Neck3 r;
        r.ik->clampAngle = 100.0f;
        r.ik->target = {500.0f, 1.75f, -866.0f};   // 150 degrees to the right-back
        r.s.Start(); r.Tick();
        Vec3 f = Fwd(r.head->transform);
        CHECK(std::fabs(Deg(f, Vec3::Forward) - 100.0f) < 1.5f);
        CHECK(f.x > 0.9f);
        // Straight behind: no nearer side, so the look keeps the side it had.
        r.ik->target = {0.0f, 1.75f, -1000.0f};
        r.Tick();
        f = Fwd(r.head->transform);
        CHECK(std::fabs(Deg(f, Vec3::Forward) - 100.0f) < 1.5f);
        CHECK(f.x > 0.9f);
        // Still level: the swing round goes about the up axis, not over the top.
        CHECK(std::fabs(f.y) < 0.05f);

        Neck3 r2;
        r2.ik->clampAngle = 0.0f;
        r2.ik->target = {500.0f, 1.75f, -866.0f};
        r2.s.Start(); r2.Tick();
        CHECK(std::fabs(Deg(Fwd(r2.head->transform), Vec3::Forward) - 150.0f) < 1.5f);
    }

    // ---- The clamp eases: a target crossing behind swings the look round the
    // front (through forward), not through the back.
    {
        Neck3 r;
        r.ik->clampAngle = 100.0f; r.ik->smoothing = 8.0f;
        r.ik->target = {500.0f, 1.75f, -866.0f};   // right-back
        r.s.Start(); r.Tick(60);
        CHECK(Fwd(r.head->transform).x > 0.9f);
        r.ik->target = {-500.0f, 1.75f, -866.0f};  // crosses to left-back
        float minZ = 1.0f;
        for (int i = 0; i < 90; ++i) { r.Tick(1); minZ = std::fmin(minZ, Fwd(r.head->transform).z); }
        // It never looked backwards (z stays above the clamped -0.17), and ends
        // up on the left at the clamp.
        CHECK(minZ > -0.3f);
        Vec3 f = Fwd(r.head->transform);
        CHECK(f.x < -0.9f && std::fabs(Deg(f, Vec3::Forward) - 100.0f) < 2.0f);
    }

    // ---- Eyes: aim straight at the target within eyeMaxAngle of the head; the
    // head itself (max angle 0) doesn't turn.
    {
        Scene s("eyes"); s.physicsEnabled = false;
        auto* npc  = s.CreateGameObject("NPC");
        auto* head = s.CreateGameObject("Head"); head->transform->SetParent(npc->transform, false); head->transform->localPosition = {0, 1.75f, 0};
        auto* eye  = s.CreateGameObject("LeftEye"); eye->transform->SetParent(head->transform, false); eye->transform->localPosition = {0.03f, 0.05f, 0.1f};
        auto* ik = npc->AddComponent<LookAtIK>();
        ik->chain = {head->transform}; ik->eyes = {eye->transform};
        ik->maxAngle = 0.0f; ik->smoothing = 0.0f; ik->eyeMaxAngle = 35.0f;
        ik->target = {std::sin(20.0f * 0.0174533f) * 100.0f, 1.8f, std::cos(20.0f * 0.0174533f) * 100.0f};
        s.Start(); s.Update(1.0f / 60.0f); s.Update(1.0f / 60.0f);
        CHECK(std::fabs(Deg(Fwd(eye->transform), Vec3::Forward) - 20.0f) < 1.0f);
        CHECK(LocalTurn(head->transform) < 0.01f);
        ik->target = {std::sin(80.0f * 0.0174533f) * 100.0f, 1.8f, std::cos(80.0f * 0.0174533f) * 100.0f};
        s.Update(1.0f / 60.0f);
        Vec3 f = Fwd(eye->transform);
        CHECK(std::fabs(Deg(f, Vec3::Forward) - 35.0f) < 1.0f);
        CHECK(f.x > 0.5f);
        ik->eyeWeight = 0.5f;
        s.Update(1.0f / 60.0f);
        CHECK(std::fabs(Deg(Fwd(eye->transform), Vec3::Forward) - 17.5f) < 1.0f);
        // Weight 0 restores the animated eye.
        ik->weight = 0.0f;
        s.Update(1.0f / 60.0f);
        CHECK(LocalTurn(eye->transform) < 0.01f);
    }

    // ---- Limb IK stretch: beyond reach the bones lengthen up to the limit, and
    // the animated lengths come back as soon as the target is reachable.
    {
        Scene s("stretch"); s.physicsEnabled = false;
        auto* rig  = s.CreateGameObject("Rig");
        auto* up   = s.CreateGameObject("UpperArm"); up->transform->SetParent(rig->transform, false);  up->transform->localPosition = {0, 1.4f, 0};
        auto* lo   = s.CreateGameObject("ForeArm");  lo->transform->SetParent(up->transform, false);   lo->transform->localPosition = {0.3f, 0, 0};
        auto* hand = s.CreateGameObject("Hand");     hand->transform->SetParent(lo->transform, false); hand->transform->localPosition = {0.3f, 0, 0};
        auto* ik = rig->AddComponent<LimbIK>();
        ik->upper = up->transform; ik->lower = lo->transform; ik->end = hand->transform;
        ik->poleMode = LimbIK::PoleMode::World; ik->pole = Vec3::Forward;
        ik->targetBlendTime = 0.0f;
        ik->target = {0.75f, 1.4f, 0.0f};   // 0.15 past the 0.6 reach
        s.Start(); s.Update(1.0f / 60.0f);
        auto boneLen = [&]() { return (lo->transform->Position() - up->transform->Position()).Magnitude(); };
        CHECK(std::fabs(hand->transform->Position().x - 0.6f) < 0.002f);   // no stretch: stops short
        CHECK(std::fabs(boneLen() - 0.3f) < 1e-4f);

        ik->stretch = 0.2f;                  // up to 20 %: 0.72 reach
        s.Update(1.0f / 60.0f);
        CHECK(std::fabs(hand->transform->Position().x - 0.72f) < 0.002f);
        CHECK(std::fabs(boneLen() - 0.36f) < 1e-3f);
        CHECK(std::fabs((hand->transform->Position() - lo->transform->Position()).Magnitude() - 0.36f) < 1e-3f);

        ik->target = {0.66f, 1.4f, 0.0f};    // only 10 % needed
        s.Update(1.0f / 60.0f);
        CHECK(std::fabs(hand->transform->Position().x - 0.66f) < 0.002f);
        CHECK(std::fabs(boneLen() - 0.33f) < 1e-3f);

        ik->weight = 0.5f;                   // the stretch blends by the weight like the turn
        ik->target = {0.75f, 1.4f, 0.0f};
        s.Update(1.0f / 60.0f);
        CHECK(std::fabs(boneLen() - 0.33f) < 1e-3f);
        ik->weight = 1.0f;

        ik->target = {0.5f, 1.4f, 0.0f};     // within reach: lengths restored, elbow bends
        s.Update(1.0f / 60.0f);
        CHECK(std::fabs(boneLen() - 0.3f) < 1e-4f);
        CHECK(std::fabs(hand->transform->Position().x - 0.5f) < 0.002f);
        CHECK(std::fabs(lo->transform->Position().z) > 0.1f);
        for (int i = 0; i < 30; ++i) s.Update(1.0f / 60.0f);   // and stays put: nothing accumulates
        CHECK(std::fabs(boneLen() - 0.3f) < 1e-4f);
        CHECK(std::fabs(hand->transform->Position().x - 0.5f) < 0.002f);
    }

    // ---- Scene file round trip, and files from before the clamp keep looking
    // all the way round (clamp 0).
    {
        Scene s("rt");
        auto* npc = s.CreateGameObject("NPC");
        auto* lk = npc->AddComponent<LookAtIK>();
        lk->chainNames = {"Spine", "Neck", "Head"}; lk->targetName = "Player";
        lk->boneWeights = {0.2f, 0.6f}; lk->clampAngle = 95.0f;
        lk->eyeNames = {"LeftEye", "RightEye"}; lk->eyeMaxAngle = 30.0f; lk->eyeWeight = 0.8f;
        auto* lb = npc->AddComponent<LimbIK>();
        lb->upperName = "A"; lb->lowerName = "B"; lb->endName = "C"; lb->stretch = 0.15f;
        const std::string text = SceneSerializer::Serialize(s);
        Scene s2("rt2");
        CHECK(SceneSerializer::Deserialize(s2, text));
        auto* l2 = s2.Find("NPC")->GetComponent<LookAtIK>();
        CHECK(l2 && l2->boneWeights.size() == 2 && std::fabs(l2->boneWeights[0] - 0.2f) < 1e-5f && std::fabs(l2->boneWeights[1] - 0.6f) < 1e-5f);
        CHECK(l2 && std::fabs(l2->clampAngle - 95.0f) < 1e-4f);
        CHECK(l2 && l2->eyeNames.size() == 2 && l2->eyeNames[1] == "RightEye");
        CHECK(l2 && std::fabs(l2->eyeMaxAngle - 30.0f) < 1e-4f && std::fabs(l2->eyeWeight - 0.8f) < 1e-5f);
        CHECK(l2 && l2->chainNames.size() == 3 && l2->chainNames[2] == "Head");
        auto* b2 = s2.Find("NPC")->GetComponent<LimbIK>();
        CHECK(b2 && std::fabs(b2->stretch - 0.15f) < 1e-5f);

        // An older record: the line ends at the smoothing value.
        std::string old = text;
        const std::size_t at = old.find(" clamp=");
        CHECK(at != std::string::npos);
        old.erase(at, old.find('\n', at) - at);
        Scene s3("rt3");
        CHECK(SceneSerializer::Deserialize(s3, old));
        auto* l3 = s3.Find("NPC")->GetComponent<LookAtIK>();
        CHECK(l3 && l3->clampAngle == 0.0f && l3->boneWeights.empty() && l3->eyeNames.empty());
        CHECK(l3 && l3->chainNames.size() == 3 && std::fabs(l3->smoothing - 10.0f) < 1e-4f);
    }

    TEST_MAIN_RESULT();
}
