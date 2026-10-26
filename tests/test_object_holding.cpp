#include "test_framework.hpp"
#include "rig_harness.hpp"
#include <Okay.hpp>
#include "okay/Components/ProceduralObjectHolding.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Scripting/OkayScriptVM.hpp"
#include <cmath>

using namespace okay;

namespace {

float Dist(const Vec3& a, const Vec3& b) { return (a - b).Magnitude(); }

GameObject* Ground(Scene& s) {
    GameObject* g = s.CreateGameObject("Ground");
    g->transform->localPosition = {0, -0.5f, 0};
    g->transform->localScale = {100, 1, 100};
    g->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
    g->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
    return g;
}

GameObject* Crate(Scene& s, const char* name, const Vec3& at, const Vec3& size, float mass) {
    GameObject* c = s.CreateGameObject(name);
    c->transform->localPosition = at;
    auto* mr = c->AddComponent<MeshRenderer>();
    mr->mesh = Mesh::Cube(1.0f);
    c->transform->localScale = size;
    c->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
    c->AddComponent<Rigidbody3D>()->mass = mass;
    return c;
}

// How far the hand bone is from the wrist goal the holding gave the IK (position
// in metres; rotation in degrees).
float WristError(FullBodyIK* ik, int l, Transform* hand) {
    Transform* t = ik->effectors[l].target;
    return t ? Dist(t->Position(), hand->Position()) : 1e9f;
}
float TurnError(FullBodyIK* ik, int l, Transform* hand) {
    Transform* t = ik->effectors[l].target;
    return t ? Quat::Angle(t->Rotation(), hand->Rotation()) : 1e9f;
}

struct Arm { Transform *up, *fore, *hand; };
Arm ArmOf(FullBodyIK* ik, int l) { Arm a{}; ik->LimbBones(l, a.up, a.fore, a.hand); return a; }

void Step(Scene& s, int frames, float dt = 1.0f / 60.0f) { for (int i = 0; i < frames; ++i) s.Update(dt); }

} // namespace

int main() {
    RUN_SUITE("object_holding");

    // ---- Built-in Character: a big crate goes in both hands, on its sides.
    {
        Scene s("Hold");
        Ground(s);
        GameObject* player = Templates::BuildPlayerBody(s, {0, 0, 0}, "Player");
        auto* hold = player->AddComponent<ProceduralObjectHolding>();
        GameObject* crate = Crate(s, "Crate", {0, 0.25f, -1.0f}, {0.5f, 0.4f, 0.4f}, 3.0f);
        s.Start();
        Step(s, 5);
        auto* ch = player->GetComponent<Character>();
        CHECK(ch->PartsBuilt());
        const float upperRest = Dist(ch->Part(3)->transform->Position(), ch->Part(4)->transform->Position());
        CHECK(hold->Pickup(crate));
        CHECK(crate->GetComponent<HoldableObject>() != nullptr);
        CHECK(crate->GetComponent<Rigidbody3D>()->bodyType == Rigidbody3D::BodyType::Kinematic);
        Step(s, 90);
        FullBodyIK* ik = hold->IK();
        CHECK(ik != nullptr);
        CHECK(hold->HandInUse(true) && hold->HandInUse(false));
        for (int l = 0; l < 2; ++l) {
            Arm a = ArmOf(ik, l);
            CHECK(a.up && a.fore && a.hand);
            if (!a.hand) continue;
            const float err = WristError(ik, l, a.hand), turn = TurnError(ik, l, a.hand);
            CHECK(err < 0.01f && turn < 3.0f);
            // The wrist goal sits just behind the palm on the grip.
            CHECK(Dist(ik->effectors[l].target->Position(), hold->GripPoint(l == 0)) < 0.1f);
            // No stretching: bone lengths are the rig's own.
            CHECK_NEAR(Dist(a.up->Position(), a.fore->Position()), upperRest, 0.01f);
            // The elbow sits below the shoulder-wrist line (bends down, not up).
            const Vec3 sh = a.up->Position(), wr = a.hand->Position(), el = a.fore->Position();
            const float t = Vec3::Dot(el - sh, (wr - sh).Normalized());
            const Vec3 onLine = sh + (wr - sh).Normalized() * t;
            CHECK(el.y < onLine.y);
        }
        // The grips are on the crate's two sides, at its height, in front of the body.
        const Vec3 cp = crate->transform->Position();
        CHECK(Dist(hold->GripPoint(true), hold->GripPoint(false)) > 0.45f);
        CHECK(std::fabs(hold->GripPoint(true).y - cp.y) < 0.05f);
        CHECK(cp.z < -0.15f && cp.y > 0.7f && cp.y < 1.3f);
        // Walking carries it along, hands still on.
        player->transform->localPosition = {2, 0, 0};
        Step(s, 3);
        CHECK(std::fabs(crate->transform->Position().x - 2.0f) < 0.1f);
        // Drop: physics again, hands back to the animation.
        hold->Drop();
        CHECK(!hold->IsHolding());
        CHECK(crate->GetComponent<Rigidbody3D>()->bodyType == Rigidbody3D::BodyType::Dynamic);
        CHECK(!crate->GetComponent<BoxCollider3D>()->isTrigger);
        CHECK(crate->GetComponent<HoldableObject>()->Holder() == nullptr);
        Step(s, 60);
        CHECK(!ik->effectors[0].enabled && !ik->effectors[1].enabled);
        CHECK(ik->EffectiveWeight(0) == 0.0f && ik->effectors[0].target == nullptr);
    }

    // ---- A small light object goes in the primary hand only (left here).
    {
        Scene s("Hold");
        Ground(s);
        GameObject* player = Templates::BuildPlayerBody(s, {0, 0, 0}, "Player");
        auto* hold = player->AddComponent<ProceduralObjectHolding>();
        hold->primaryHand = ProceduralObjectHolding::Hand::Left;
        GameObject* cup = Crate(s, "Cup", {0, 0.05f, -0.6f}, {0.08f, 0.1f, 0.08f}, 0.3f);
        s.Start();
        Step(s, 2);
        CHECK(hold->Pickup(cup));
        Step(s, 60);
        CHECK(hold->HandInUse(true) && !hold->HandInUse(false));
        Arm a = ArmOf(hold->IK(), 0);
        CHECK(WristError(hold->IK(), 0, a.hand) < 0.01f && TurnError(hold->IK(), 0, a.hand) < 3.0f);
        // Someone else can't take it out of these hands.
        GameObject* other = Templates::BuildPlayerBody(s, {3, 0, 0}, "Other");
        auto* oh = other->AddComponent<ProceduralObjectHolding>();
        CHECK(!oh->Pickup(cup));
        CHECK(!hold->Pickup(player));
    }

    // ---- Custom grips far apart: the object comes in, the arms never stretch.
    {
        Scene s("Hold");
        Ground(s);
        GameObject* player = Templates::BuildPlayerBody(s, {0, 0, 0}, "Player");
        auto* hold = player->AddComponent<ProceduralObjectHolding>();
        GameObject* pole = Crate(s, "Pole", {0, 0.5f, -1.0f}, {1, 1, 1}, 2.0f);
        auto* h = pole->AddComponent<HoldableObject>();
        h->style = HoldableObject::Style::Custom;
        h->holdOffset = {0, 0, 2.0f};                       // asks for 2 m out in front
        h->leftGrip.enabled = h->rightGrip.enabled = true;
        h->leftGrip.position = {-0.2f, 0, 0};
        h->rightGrip.position = {0.2f, 0, 0};
        s.Start();
        Step(s, 2);
        auto* ch = player->GetComponent<Character>();
        const float limb = Dist(ch->Part(3)->transform->Position(), ch->Part(4)->transform->Position()) +
                           Dist(ch->Part(4)->transform->Position(), ch->Part(5)->transform->Position());
        CHECK(hold->Pickup(pole));
        Step(s, 90);
        for (int l = 0; l < 2; ++l) {
            Arm a = ArmOf(hold->IK(), l);
            CHECK(Dist(a.up->Position(), a.hand->Position()) <= limb + 1e-3f);
            CHECK(WristError(hold->IK(), l, a.hand) < 0.01f);   // and still on the grip
        }
        CHECK(Dist(pole->transform->Position(), player->transform->Position()) < 1.5f);
    }

    // ---- Imported humanoid: same hands on the same crate.
    {
        righarness::Rig r = righarness::PlayableFromFile("mixamo_t_noclips.glb");
        CHECK((bool)r);
        if (r) {
            Scene& s = *r.scene;
            auto* hold = r.player->AddComponent<ProceduralObjectHolding>();
            GameObject* crate = Crate(s, "Crate", {0, 0.25f, -1.0f}, {0.5f, 0.4f, 0.4f}, 3.0f);
            r.Step(1.0f / 60.0f, {}, 5);
            CHECK(hold->Pickup(crate));
            r.Step(1.0f / 60.0f, {}, 90);
            FullBodyIK* ik = hold->IK();
            CHECK(ik && hold->HandInUse(true) && hold->HandInUse(false));
            for (int l = 0; ik && l < 2; ++l) {
                Arm a = ArmOf(ik, l);
                CHECK(a.hand != nullptr);
                if (!a.hand) continue;
                const float err = WristError(ik, l, a.hand), turn = TurnError(ik, l, a.hand);
                CHECK(err < 0.01f && turn < 3.0f);
                CHECK(Dist(ik->effectors[l].target->Position(), hold->GripPoint(l == 0)) < 0.1f);
            }
        }
    }

    // ---- Scripts: hold_nearest only takes holdable objects in range; hold_drop lets go.
    {
        Scene s("Script");
        Ground(s);
        GameObject* player = Templates::BuildPlayerBody(s, {0, 0, 0}, "Player");
        auto* hold = player->AddComponent<ProceduralObjectHolding>();
        GameObject* far = Crate(s, "Far", {5, 0.2f, 0}, {0.3f, 0.3f, 0.3f}, 1.0f);
        far->AddComponent<HoldableObject>();
        Crate(s, "Plain", {0.5f, 0.2f, 0}, {0.3f, 0.3f, 0.3f}, 1.0f);   // not holdable
        s.Start();
        Step(s, 2);
        OkayScriptVM vm; ScriptHost host; host.gameObject = player; host.transform = player->transform;
        vm.Bind(&host);
        CHECK(vm.Load("let a = hold_nearest(2)\nlet n = hold_object()\n"));
        CHECK(!hold->IsHolding());
        GameObject* near = Crate(s, "Near", {0.6f, 0.2f, -0.3f}, {0.3f, 0.3f, 0.3f}, 1.0f);
        near->AddComponent<HoldableObject>();
        CHECK(vm.Load("hold_nearest(2)\n"));
        CHECK(hold->Held() == near);
        CHECK(vm.Load("hold_drop()\nhold_pickup(\"Far\")\n"));
        CHECK(hold->Held() == far);
    }

    // ---- Save/load keeps every setting; old scenes without the records load.
    {
        Scene s("Save");
        GameObject* p = s.CreateGameObject("P");
        auto* hold = p->AddComponent<ProceduralObjectHolding>();
        hold->primaryHand = ProceduralObjectHolding::Hand::Both;
        hold->holdingStyle = ProceduralObjectHolding::StyleMode::Custom;
        hold->armIK = false; hold->transitionTime = 0.5f;
        GameObject* o = s.CreateGameObject("O");
        auto* h = o->AddComponent<HoldableObject>();
        h->style = HoldableObject::Style::Custom;
        h->leftGrip = {true, {-0.1f, 0.2f, 0.3f}, {10, 20, 30}};
        h->rightGrip = {true, {0.1f, 0.2f, 0.3f}, {-10, -20, -30}};
        h->holdOffset = {0.1f, -0.2f, 0.4f}; h->holdRotation = {0, 90, 0};
        h->weight = 12.5f; h->socket = "Back Slot";
        Scene t("Load");
        CHECK(SceneSerializer::Deserialize(t, SceneSerializer::Serialize(s)));
        auto* ph = t.Find("P") ? t.Find("P")->GetComponent<ProceduralObjectHolding>() : nullptr;
        auto* oh = t.Find("O") ? t.Find("O")->GetComponent<HoldableObject>() : nullptr;
        CHECK(ph && oh);
        if (ph && oh) {
            CHECK(ph->primaryHand == ProceduralObjectHolding::Hand::Both);
            CHECK(ph->holdingStyle == ProceduralObjectHolding::StyleMode::Custom);
            CHECK(!ph->armIK); CHECK_NEAR(ph->transitionTime, 0.5f, 1e-5f);
            CHECK(oh->style == HoldableObject::Style::Custom);
            CHECK(oh->leftGrip.enabled && oh->rightGrip.enabled);
            CHECK_NEAR(oh->leftGrip.position.z, 0.3f, 1e-5f); CHECK_NEAR(oh->rightGrip.rotation.z, -30.0f, 1e-4f);
            CHECK_NEAR(oh->holdOffset.y, -0.2f, 1e-5f); CHECK_NEAR(oh->holdRotation.y, 90.0f, 1e-4f);
            CHECK_NEAR(oh->weight, 12.5f, 1e-5f); CHECK(oh->socket == "Back Slot");
        }
    }

    TEST_MAIN_RESULT();
}
