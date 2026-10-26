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
        GameObject* pole = Crate(s, "Pole", {0, 0.5f, -1.0f}, {1.0f, 0.05f, 0.05f}, 2.0f);
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

    // ---- Automatic picks a style from the tag / name, then shape and weight.
    {
        Scene s("Resolve");
        using St = HoldableObject::Style;
        auto mk = [&](const char* name, Vec3 size, float mass) {
            return Crate(s, name, {0, 0, 0}, size, mass)->AddComponent<HoldableObject>();
        };
        CHECK(mk("Old Revolver", {0.05f, 0.15f, 0.25f}, 1)->ResolvedStyle() == St::Pistol);
        CHECK(mk("Shotgun", {0.05f, 0.15f, 0.25f}, 1)->ResolvedStyle() == St::Rifle);
        CHECK(mk("Coffee", {0.1f, 0.1f, 0.1f}, 1)->ResolvedStyle() == St::Cup);
        CHECK(mk("Thing1", {0.06f, 0.15f, 0.9f}, 4)->ResolvedStyle() == St::Rifle);      // long and weighty
        CHECK(mk("Thing2", {0.04f, 0.9f, 0.08f}, 1)->ResolvedStyle() == St::Sword);      // long and light
        CHECK(mk("Thing3", {0.05f, 0.05f, 0.3f}, 0.5f)->ResolvedStyle() == St::Flashlight);
        CHECK(mk("Thing4", {0.6f, 0.4f, 0.4f}, 30)->ResolvedStyle() == St::Heavy);
        CHECK(mk("Thing5", {0.5f, 0.4f, 0.4f}, 3)->ResolvedStyle() == St::Box);
        CHECK(mk("Thing6", {0.1f, 0.1f, 0.1f}, 0.3f)->ResolvedStyle() == St::Automatic); // small: one hand under
        auto* tagged = mk("Thing7", {0.1f, 0.1f, 0.1f}, 0.3f);
        tagged->gameObject->tag = "pistol";
        CHECK(tagged->ResolvedStyle() == St::Pistol);
        tagged->style = St::Sword;
        CHECK(tagged->ResolvedStyle() == St::Sword);
    }

    // ---- Each style: the hands it uses reach their grips, the object points the
    // way the style says.
    {
        using St = HoldableObject::Style;
        struct Case { const char* name; Vec3 size; St style; bool support; bool left, right; };
        const Case cases[] = {
            {"Pistol", {0.05f, 0.15f, 0.25f}, St::Pistol, false, false, true},
            {"Pistol2", {0.05f, 0.15f, 0.25f}, St::Pistol, true, true, true},
            {"Rifle", {0.06f, 0.15f, 0.9f}, St::Rifle, false, true, true},
            {"Flashlight", {0.05f, 0.05f, 0.3f}, St::Flashlight, false, false, true},
            {"Sword", {0.04f, 0.9f, 0.08f}, St::Sword, false, false, true},
            {"Greatsword", {0.04f, 1.1f, 0.08f}, St::Sword, true, true, true},
            {"Cup", {0.1f, 0.1f, 0.12f}, St::Cup, false, false, true},
            {"Box", {0.5f, 0.4f, 0.4f}, St::Box, false, true, true},
            {"Heavy", {0.5f, 0.4f, 0.4f}, St::Heavy, false, true, true},
        };
        float boxY = 0.0f, heavyY = 0.0f;
        for (const Case& cs : cases) {
            Scene s("Style");
            Ground(s);
            GameObject* player = Templates::BuildPlayerBody(s, {0, 0, 0}, "Player");
            auto* hold = player->AddComponent<ProceduralObjectHolding>();
            GameObject* obj = Crate(s, cs.name, {0, 0.3f, -0.8f}, cs.size, 1.0f);
            auto* h = obj->AddComponent<HoldableObject>();
            h->style = cs.style; h->supportHand = cs.support;
            s.Start();
            Step(s, 2);
            CHECK(hold->Pickup(obj));
            Step(s, 90);
            FullBodyIK* ik = hold->IK();
            const bool want[2] = {cs.left, cs.right};
            for (int l = 0; l < 2; ++l) {
                CHECK(hold->HandInUse(l == 0) == want[l]);
                if (!want[l]) continue;
                Arm a = ArmOf(ik, l);
                const float err = WristError(ik, l, a.hand);
                if (err >= 0.01f) std::cerr << "  " << cs.name << " hand " << l << " wrist error " << err << "\n";
                CHECK(err < 0.01f && TurnError(ik, l, a.hand) < 3.0f);
            }
            const Quat r = obj->transform->Rotation();
            const Vec3 fwd{0, 0, -1}, rightSide = (ArmOf(ik, 1).up->Position() - ArmOf(ik, 0).up->Position()).Normalized();
            if (cs.style == St::Pistol || cs.style == St::Rifle || cs.style == St::Flashlight)
                CHECK(Vec3::Dot(r * Vec3{0, 0, -1}, fwd) > 0.95f);          // barrel / beam forward
            if (cs.style == St::Sword) {
                CHECK((r * Vec3{0, 1, 0}).y > 0.7f);                          // blade up...
                CHECK(Vec3::Dot(r * Vec3{0, 1, 0}, fwd) > 0.3f);              // ...and forward
            }
            if (cs.style == St::Cup) CHECK(Vec3::Dot(r * Vec3{1, 0, 0}, rightSide) > 0.95f);   // handle to the hand
            if (cs.style == St::Rifle)                                        // support hand ahead of the trigger hand
                CHECK(Vec3::Dot(hold->GripPoint(true) - hold->GripPoint(false), fwd) > 0.2f);
            if (cs.style == St::Box) boxY = hold->GripPoint(false).y;
            if (cs.style == St::Heavy) heavyY = hold->GripPoint(false).y;
        }
        CHECK(heavyY < boxY - 0.05f);   // a heavy load hangs lower in the hands
    }

    // ---- Hold Rotation fixes a model built backwards (a gun facing +Z).
    {
        Scene s("Fix");
        Ground(s);
        GameObject* player = Templates::BuildPlayerBody(s, {0, 0, 0}, "Player");
        auto* hold = player->AddComponent<ProceduralObjectHolding>();
        GameObject* gun = Crate(s, "Pistol", {0, 0.3f, -0.8f}, {0.05f, 0.15f, 0.25f}, 1.0f);
        gun->AddComponent<HoldableObject>()->holdRotation = {0, 180, 0};
        s.Start();
        Step(s, 2);
        CHECK(hold->Pickup(gun));
        Step(s, 60);
        CHECK(Vec3::Dot(gun->transform->Rotation() * Vec3{0, 0, 1}, Vec3{0, 0, -1}) > 0.95f);
        Arm a = ArmOf(hold->IK(), 1);
        CHECK(WristError(hold->IK(), 1, a.hand) < 0.01f);
    }

    // ---- A rig with finger bones (no Character; bones found by name): fingers
    // close around a sword's hilt and open again after the drop.
    {
        Scene s("Fingers");
        GameObject* root = s.CreateGameObject("Rig");
        auto mk = [&](const char* n, GameObject* parent, Vec3 local) {
            GameObject* o = s.CreateGameObject(n);
            o->transform->SetParent(parent->transform, false);
            o->transform->localPosition = local;
            return o;
        };
        GameObject* hips = mk("Hips", root, {0, 1.0f, 0});
        GameObject* chest = mk("Chest", mk("Spine", hips, {0, 0.15f, 0}), {0, 0.25f, 0});
        mk("Head", chest, {0, 0.3f, 0});
        Transform* tip = nullptr;
        for (int sd = -1; sd <= 1; sd += 2) {
            const std::string L = sd < 0 ? "Left" : "Right";
            GameObject* up = mk((L + "Arm").c_str(), chest, {sd * 0.2f, 0.05f, 0});
            GameObject* fore = mk((L + "ForeArm").c_str(), up, {sd * 0.02f, -0.28f, 0});
            GameObject* hand = mk((L + "Hand").c_str(), fore, {sd * 0.02f, -0.26f, 0});
            GameObject* m1 = mk((L + "HandMiddle1").c_str(), hand, {0, -0.08f, 0});
            GameObject* m3 = mk((L + "HandMiddle3").c_str(), mk((L + "HandMiddle2").c_str(), m1, {0, -0.03f, 0}), {0, -0.03f, 0});
            mk((L + "HandIndex1").c_str(), hand, {0, -0.08f, -0.02f});
            mk((L + "HandThumb1").c_str(), hand, {-sd * 0.03f, -0.03f, -0.02f});
            if (sd > 0) tip = m3->transform;
        }
        auto* hold = root->AddComponent<ProceduralObjectHolding>();
        GameObject* sword = Crate(s, "Sword", {0, 0.5f, -0.8f}, {0.04f, 0.9f, 0.08f}, 1.0f);
        s.Start();
        Step(s, 2);
        Transform* hand = s.Find("RightHand")->transform;
        const float open0 = Dist(tip->Position(), hand->Position());
        const Quat rest = tip->localRotation;
        CHECK(hold->Pickup(sword));
        Step(s, 90);
        CHECK(hold->HandInUse(false));
        CHECK(WristError(hold->IK(), 1, hand) < 0.01f);
        const float closed = Dist(tip->Position(), hand->Position());
        CHECK(closed < open0 * 0.85f);
        hold->Drop();
        Step(s, 90);
        CHECK_NEAR(Dist(tip->Position(), hand->Position()), open0, 1e-3f);
        CHECK(std::fabs(Quat::Dot(tip->localRotation, rest)) > 0.99999f);
    }

    // ---- Body adjustment: a heavy load leans the torso back (through the
    // character's additive layer), slows the walk and tells the Animator; a light
    // one changes nothing; it all eases back after the drop.
    {
        Scene s("Body");
        Ground(s);
        GameObject* player = Templates::BuildPlayerBody(s, {0, 0, 0}, "Player");
        auto* sm = player->GetComponent<AnimStateMachine>();   // built-in players come with an Animator
        if (!sm) sm = player->AddComponent<AnimStateMachine>();
        auto* hold = player->AddComponent<ProceduralObjectHolding>();
        GameObject* heavy = Crate(s, "Load", {0, 0.3f, -0.8f}, {0.4f, 0.3f, 0.3f}, 30.0f);
        GameObject* cup = Crate(s, "Cup", {0.5f, 0.3f, -0.8f}, {0.08f, 0.1f, 0.08f}, 0.3f);
        s.Start();
        Step(s, 5);
        auto* ch = player->GetComponent<Character>();
        const float headZ0 = ch->Part(2)->transform->Position().z;
        CHECK(hold->Pickup(heavy));
        Step(s, 120);
        CHECK(hold->Heaviness() > 0.95f);
        CHECK_NEAR(hold->SpeedMultiplier(), 0.55f, 0.03f);
        CHECK(ch->LayerClip() == "Holding Lean" && ch->LayerWeight() > 0.9f);
        CHECK(ch->Part(2)->transform->Position().z > headZ0 + 0.03f);   // leaning back (facing -Z)
        CHECK(sm->GetBool("holding"));
        CHECK_NEAR(sm->GetFloat("carryWeight"), 30.0f, 1e-4f);
        CHECK((int)sm->GetFloat("carryStyle") == (int)HoldableObject::Style::Heavy);
        // Still on the grips with the lean.
        for (int l = 0; l < 2; ++l) CHECK(WristError(hold->IK(), l, ArmOf(hold->IK(), l).hand) < 0.01f);
        hold->Drop();
        Step(s, 120);
        CHECK(!ch->IsLayering());
        CHECK(!sm->GetBool("holding"));
        CHECK_NEAR(hold->SpeedMultiplier(), 1.0f, 1e-6f);
        CHECK(hold->Pickup(cup));
        Step(s, 60);
        CHECK(hold->Heaviness() == 0.0f && !ch->IsLayering());
        CHECK_NEAR(hold->SpeedMultiplier(), 1.0f, 1e-6f);
        hold->Drop();
        // Body adjustment off, or Custom carry animations: no built-in lean.
        hold->bodyAdjustment = false;
        CHECK(hold->Pickup(heavy));
        Step(s, 60);
        CHECK(!ch->IsLayering());
        CHECK_NEAR(hold->SpeedMultiplier(), 1.0f, 1e-6f);
        hold->Drop(); Step(s, 2);
        hold->bodyAdjustment = true;
        hold->carryAnimations = ProceduralObjectHolding::CarryAnim::Custom;
        CHECK(hold->Pickup(heavy));
        Step(s, 60);
        CHECK(!ch->IsLayering() && sm->GetBool("holding"));
        CHECK(hold->SpeedMultiplier() < 0.6f);
        hold->Drop(); Step(s, 2);
        // A layer a script is playing is left alone.
        hold->carryAnimations = ProceduralObjectHolding::CarryAnim::Automatic;
        AnimClip wave; wave.name = "Wave"; AnimKey k; k.pose.assign(Character::BoneCount(), Vec3{0, 0, 0}); wave.keys.push_back(k);
        ch->AddClip(wave);
        ch->PlayLayer("Wave", Character::BoneBit(6), false, 1.0f);
        CHECK(hold->Pickup(heavy));
        Step(s, 30);
        CHECK(ch->LayerClip() == "Wave");
    }

    // ---- A heavy load slows the player controllers down (MoveSpeedScale).
    {
        auto walked = [](bool carry) {
            Scene s("Walk");
            Ground(s);
            GameObject* player = Templates::AddThirdPersonPlayer(s, {0, 0, 0});
            auto* hold = player->AddComponent<ProceduralObjectHolding>();
            GameObject* load = Crate(s, "Load", {0, 0.3f, -0.8f}, {0.4f, 0.3f, 0.3f}, 30.0f);
            s.Start();
            for (int i = 0; i < 30; ++i) { Input::FeedKeys({}); s.Update(1.0f / 60.0f); }
            if (carry) hold->Pickup(load);
            for (int i = 0; i < 10; ++i) { Input::FeedKeys({}); s.Update(1.0f / 60.0f); }
            const Vec3 a = player->transform->Position();
            for (int i = 0; i < 90; ++i) { Input::FeedKeys({'w'}); s.Update(1.0f / 60.0f); }
            Input::FeedKeys({});
            const Vec3 d = player->transform->Position() - a;
            return std::sqrt(d.x * d.x + d.z * d.z);
        };
        const float free = walked(false), loaded = walked(true);
        CHECK(free > 3.0f);
        CHECK(loaded < free * 0.7f && loaded > free * 0.45f);   // ~0.55x
        CHECK_NEAR(MoveSpeedScale(nullptr), 1.0f, 1e-6f);
    }

    // ---- Crouched and prone (the stance core): the load is still in the hands,
    // the lean layer stacks on the stance pose, and the slow-down multiplies the
    // stance's own speed.
    for (auto st : {ThirdPersonController::Stance::Crouch, ThirdPersonController::Stance::Prone}) {
        Scene s("Stance");
        Ground(s);
        GameObject* player = Templates::AddThirdPersonPlayer(s, {0, 0, 0});
        auto* tpc = player->GetComponent<ThirdPersonController>();
        tpc->RestoreStance(st);
        auto* hold = player->AddComponent<ProceduralObjectHolding>();
        GameObject* load = Crate(s, "Load", {0, 0.3f, -0.8f}, {0.4f, 0.3f, 0.3f}, 30.0f);
        s.Start();
        for (int i = 0; i < 30; ++i) { Input::FeedKeys({}); s.Update(1.0f / 60.0f); }
        CHECK(tpc->stance() == st);
        CHECK(hold->Pickup(load));
        for (int i = 0; i < 120; ++i) { Input::FeedKeys({}); s.Update(1.0f / 60.0f); }
        auto* ch = player->GetComponent<Character>();
        CHECK(ch->LayerClip() == "Holding Lean" && ch->LayerWeight() > 0.9f);
        for (int l = 0; l < 2; ++l) {
            Arm a = ArmOf(hold->IK(), l);
            const float err = WristError(hold->IK(), l, a.hand);
            CHECK(std::isfinite(err) && err < 0.01f);
        }
        const Vec3 p = load->transform->Position();
        CHECK(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z));
        CHECK(p.y < (st == ThirdPersonController::Stance::Prone ? 0.9f : 1.2f));   // lower with the body
        CHECK_NEAR(hold->SpeedMultiplier(), 0.55f, 0.03f);
    }

    // ---- Kept out of the body: a box asked to sit in the chest ends up in front.
    {
        Scene s("Clip");
        Ground(s);
        GameObject* player = Templates::BuildPlayerBody(s, {0, 0, 0}, "Player");
        auto* hold = player->AddComponent<ProceduralObjectHolding>();
        GameObject* box = Crate(s, "Parcel", {0, 0.3f, -0.8f}, {0.3f, 0.3f, 0.3f}, 2.0f);
        auto* h = box->AddComponent<HoldableObject>();
        h->style = HoldableObject::Style::Custom;
        h->holdOffset = {0, -0.2f, 0};   // right inside the chest
        h->leftGrip = {true, {-0.5f, 0, 0}, {0, 0, -90}, 0.2f};
        h->rightGrip = {true, {0.5f, 0, 0}, {0, 0, 90}, 0.2f};
        s.Start();
        Step(s, 2);
        CHECK(hold->Pickup(box));
        Step(s, 60);
        auto* ch = player->GetComponent<Character>();
        const float chestZ = (ch->Part(3)->transform->Position().z + ch->Part(6)->transform->Position().z) * 0.5f;
        // The box's back face (toward the body, +Z) is in front of the torso front.
        CHECK(box->transform->Position().z + 0.15f < chestZ - 0.12f);
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
        hold->bodyAdjustment = false; hold->carryAnimations = ProceduralObjectHolding::CarryAnim::Custom;
        GameObject* o = s.CreateGameObject("O");
        auto* h = o->AddComponent<HoldableObject>();
        h->style = HoldableObject::Style::Custom;
        h->leftGrip = {true, {-0.1f, 0.2f, 0.3f}, {10, 20, 30}, 0.6f};
        h->rightGrip = {true, {0.1f, 0.2f, 0.3f}, {-10, -20, -30}, 0.6f};
        h->holdOffset = {0.1f, -0.2f, 0.4f}; h->holdRotation = {0, 90, 0};
        h->weight = 12.5f; h->socket = "Back Slot";
        h->supportHand = true; h->leftGrip.curl = 0.25f;
        Scene t("Load");
        CHECK(SceneSerializer::Deserialize(t, SceneSerializer::Serialize(s)));
        auto* ph = t.Find("P") ? t.Find("P")->GetComponent<ProceduralObjectHolding>() : nullptr;
        auto* oh = t.Find("O") ? t.Find("O")->GetComponent<HoldableObject>() : nullptr;
        CHECK(ph && oh);
        if (ph && oh) {
            CHECK(ph->primaryHand == ProceduralObjectHolding::Hand::Both);
            CHECK(ph->holdingStyle == ProceduralObjectHolding::StyleMode::Custom);
            CHECK(!ph->armIK); CHECK_NEAR(ph->transitionTime, 0.5f, 1e-5f);
            CHECK(!ph->bodyAdjustment); CHECK(ph->carryAnimations == ProceduralObjectHolding::CarryAnim::Custom);
            CHECK(oh->style == HoldableObject::Style::Custom);
            CHECK(oh->leftGrip.enabled && oh->rightGrip.enabled);
            CHECK_NEAR(oh->leftGrip.position.z, 0.3f, 1e-5f); CHECK_NEAR(oh->rightGrip.rotation.z, -30.0f, 1e-4f);
            CHECK_NEAR(oh->holdOffset.y, -0.2f, 1e-5f); CHECK_NEAR(oh->holdRotation.y, 90.0f, 1e-4f);
            CHECK_NEAR(oh->weight, 12.5f, 1e-5f); CHECK(oh->socket == "Back Slot");
            CHECK(oh->supportHand); CHECK_NEAR(oh->leftGrip.curl, 0.25f, 1e-5f); CHECK_NEAR(oh->rightGrip.curl, 0.6f, 1e-5f);
        }
    }

    TEST_MAIN_RESULT();
}
