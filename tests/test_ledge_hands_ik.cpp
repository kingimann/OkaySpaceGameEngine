#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Math/LedgeHands.hpp"
#include "okay/Animation/HumanoidActions.hpp"
#include <cmath>
#include <cstdio>

using namespace okay;

static GameObject* Bone(Scene& sc, const char* name, GameObject* parent, Vec3 local) {
    GameObject* o = sc.CreateGameObject(name);
    o->transform->SetParent(parent->transform, false);
    o->transform->localPosition = local;
    return o;
}


// The controllers tick the traverse move each frame (ThirdPersonController / FirstPersonController);
// this stands in for them so the scene's own update order runs the grab.
struct TraverseTicker : Component {
    int ExecutionOrder() const override { return -10; }
    void Update(float dt) override { TickTraverse(gameObject, dt); }
};

// A hero with two arm chains under a chest 1.4 m up. The LimbIK arms go on `armHost`
// (the hero itself when null), so a test can put them on a child object it destroys.
struct Rig {
    GameObject* hero = nullptr;
    GameObject* lHand = nullptr;
    GameObject* rHand = nullptr;
    LimbIK* la = nullptr;
    LimbIK* ra = nullptr;
};
static Rig MakeRig(Scene& sc, Vec3 at, bool addArms, GameObject* lHost = nullptr, GameObject* rHost = nullptr) {
    Rig r;
    r.hero = sc.CreateGameObject("Hero");
    r.hero->transform->SetPosition(at);
    GameObject* chest = Bone(sc, "Chest", r.hero, {0, 1.4f, 0});
    GameObject* lArm = Bone(sc, "LeftArm", chest, {-0.2f, 0, 0});
    GameObject* lFore = Bone(sc, "LeftForeArm", lArm, {0, 0.3f, 0});
    r.lHand = Bone(sc, "LeftHand", lFore, {0, 0.3f, 0});
    GameObject* rArm = Bone(sc, "RightArm", chest, {0.2f, 0, 0});
    GameObject* rFore = Bone(sc, "RightForeArm", rArm, {0, 0.3f, 0});
    r.rHand = Bone(sc, "RightHand", rFore, {0, 0.3f, 0});
    if (!addArms) return r;
    auto mk = [&](GameObject* host, GameObject* u, GameObject* lo, GameObject* e) {
        auto* a = (host ? host : r.hero)->AddComponent<LimbIK>();
        a->upper = u->transform; a->lower = lo->transform; a->end = e->transform;
        a->ikActive = false; a->stretch = 0.15f;
        a->poleMode = LimbIK::PoleMode::World; a->pole = Vec3{0, 0, 1};
        return a;
    };
    r.la = mk(lHost, lArm, lFore, r.lHand);
    r.ra = mk(rHost, rArm, rFore, r.rHand);
    return r;
}
static void LedgeGeometry(Scene& s, float wallH) {
    GameObject* ground = s.CreateGameObject("Ground");
    ground->transform->SetPosition(Vec3(0, -0.5f, 0));
    ground->AddComponent<BoxCollider3D>()->size = Vec3(30, 1, 30);
    GameObject* wall = s.CreateGameObject("Wall");   // its face toward the hero is at z = -0.4
    wall->transform->SetPosition(Vec3(0, wallH * 0.5f, -1.4f));
    wall->AddComponent<BoxCollider3D>()->size = Vec3(4, wallH, 2.0f);
}
static void Frames(Scene& s, int n) { for (int i = 0; i < n; ++i) s.Update(1.0f / 60.0f); }

int main() {
    RUN_SUITE("ledge_hands_ik");

    // ---- pure maths
    {
        // Body at the origin, lip 0.75 m ahead along -Z at height 2.
        const LedgeGrip g = LedgeFromLip({0, 0, 0}, {0, 2, -0.75f}, 0.25f);
        CHECK(g.valid);
        CHECK_NEAR(g.edge.x, 0.0f, 1e-5); CHECK_NEAR(g.edge.y, 2.0f, 1e-5); CHECK_NEAR(g.edge.z, -0.25f, 1e-5);
        CHECK_NEAR(g.normal.z, 1.0f, 1e-5);   // points from the wall to the body
        CHECK_NEAR(g.normal.y, 0.0f, 1e-6);
        Vec3 l, r;
        CHECK(LedgeHandTargets(g, 0.2f, 0.05f, l, r));
        CHECK_NEAR(l.y, 2.0f, 1e-5); CHECK_NEAR(r.y, 2.0f, 1e-5);
        CHECK_NEAR(l.z, -0.30f, 1e-5); CHECK_NEAR(r.z, -0.30f, 1e-5);   // over the edge
        CHECK_NEAR(l.x, -0.2f, 1e-5); CHECK_NEAR(r.x, 0.2f, 1e-5);      // facing -Z: right is +X
        // Rotated wall: facing +X. Right-hand side is then +Z.
        const LedgeGrip g2 = LedgeFromLip({1, 0, 1}, {1.75f, 3, 1}, 0.25f);
        LedgeHandTargets(g2, 0.2f, 0.0f, l, r);
        CHECK_NEAR(l.x, 1.25f, 1e-5); CHECK_NEAR(r.x, 1.25f, 1e-5);
        CHECK_NEAR(r.z - l.z, 0.4f, 1e-5); CHECK(r.z > l.z);
        // Bad input: lip straight above the body (no direction), NaN height, negative setback.
        CHECK(!LedgeFromLip({0, 0, 0}, {0, 2, 0}, 0.25f).valid);
        CHECK(!LedgeFromLip({0, 0, 0}, {0, NAN, -1}, 0.25f).valid);
        CHECK(!LedgeFromLip({0, 0, 0}, {0, 2, -1}, -1.0f).valid);
        CHECK(!LedgeHandTargets(LedgeGrip{}, 0.2f, 0.0f, l, r));
        // Clamp: inside reach stays; beyond reach lands on the sphere, same direction.
        const Vec3 in = ClampToReach({0, 0, 0}, {0.3f, 0, 0}, 0.6f);
        CHECK_NEAR(in.x, 0.3f, 1e-6);
        const Vec3 out = ClampToReach({0, 1, 0}, {0, 4, 0}, 0.6f);
        CHECK_NEAR(out.y, 1.6f, 1e-5); CHECK_NEAR(out.x, 0.0f, 1e-6);
        CHECK_NEAR(ClampToReach({1, 1, 1}, {1, 1, 1}, 0.5f).x, 1.0f, 1e-6);
        // From the probe's edge: the point is kept, the normal is flattened and normalised.
        const LedgeGrip ge = LedgeFromEdge({1, 2, -0.4f}, {0, 0.5f, 2.0f});
        CHECK(ge.valid);
        CHECK_NEAR(ge.edge.z, -0.4f, 1e-6); CHECK_NEAR(ge.normal.z, 1.0f, 1e-5); CHECK_NEAR(ge.normal.y, 0.0f, 1e-6);
        CHECK(!LedgeFromEdge({0, 2, 0}, {0, 1, 0}).valid);      // no horizontal direction
        CHECK(!LedgeFromEdge({0, NAN, 0}, {0, 0, 1}).valid);
    }

    // ---- component: two arms, hands go onto the ledge while a lip is held, then fade off
    {
        Scene sc;
        GameObject* hero = sc.CreateGameObject("Hero");
        hero->transform->SetPosition({0, 0, 0});
        GameObject* chest = Bone(sc, "Chest", hero, {0, 1.4f, 0});
        GameObject* lArm = Bone(sc, "LeftArm", chest, {-0.2f, 0, 0});
        GameObject* lFore = Bone(sc, "LeftForeArm", lArm, {0, 0.3f, 0});
        GameObject* lHand = Bone(sc, "LeftHand", lFore, {0, 0.3f, 0});
        GameObject* rArm = Bone(sc, "RightArm", chest, {0.2f, 0, 0});
        GameObject* rFore = Bone(sc, "RightForeArm", rArm, {0, 0.3f, 0});
        GameObject* rHand = Bone(sc, "RightHand", rFore, {0, 0.3f, 0});
        auto mk = [&](GameObject* u, GameObject* lo, GameObject* e) {
            auto* a = hero->AddComponent<LimbIK>();
            a->upper = u->transform; a->lower = lo->transform; a->end = e->transform;
            a->ikActive = false; a->stretch = 0.15f;
            a->poleMode = LimbIK::PoleMode::World; a->pole = Vec3{0, 0, 1};
            return a;
        };
        LimbIK* la = mk(lArm, lFore, lHand);
        LimbIK* ra = mk(rArm, rFore, rHand);
        auto* lh = hero->AddComponent<LedgeHandsIK>();
        lh->autoWire = false; lh->leftArm = la; lh->rightArm = ra;
        lh->fadeTime = 0.1f; lh->handSpread = 0.2f; lh->overEdge = 0.0f; lh->setback = 0.25f;

        // No hang: nothing moves, arms stay off.
        lh->LateUpdate(0.016f);
        CHECK(!lh->Holding());
        CHECK(!la->ikActive && !ra->ikActive);
        const Vec3 rest = lHand->transform->Position();

        // Hang: the ledge top is at y 1.9, the wall is ahead along -Z; the lip is 0.75 m ahead.
        HangPoints()[hero] = Vec3{0, 1.9f, -0.75f};
        for (int i = 0; i < 20; ++i) {
            lh->LateUpdate(0.016f);
            la->Update(0.016f); ra->Update(0.016f);
            la->LateUpdate(0.016f); ra->LateUpdate(0.016f);
        }
        CHECK(lh->Holding());
        CHECK(la->ikActive && ra->ikActive);
        const Vec3 wantL{-0.2f, 1.9f, -0.25f}, wantR{0.2f, 1.9f, -0.25f};
        CHECK((lHand->transform->Position() - wantL).Magnitude() < 0.03f);
        CHECK((rHand->transform->Position() - wantR).Magnitude() < 0.03f);
        CHECK((lHand->transform->Position() - rest).Magnitude() > 0.1f);

        // Let go: the arms switch off and fade back to the animated pose.
        HangPoints().erase(hero);
        lh->LateUpdate(0.016f);
        CHECK(!lh->Holding() && !la->ikActive);
        for (int i = 0; i < 30; ++i) {
            la->Update(0.016f); ra->Update(0.016f);
            lh->LateUpdate(0.016f);
            la->LateUpdate(0.016f); ra->LateUpdate(0.016f);
        }
        CHECK((lHand->transform->Position() - rest).Magnitude() < 0.01f);

        // A grab in progress (traverse job with a remembered lip) counts; one without does not.
        TraverseJob job; job.rememberLip = false; job.lip = Vec3{0, 1.9f, -0.75f};
        TraverseJobs()[hero] = job;
        lh->LateUpdate(0.016f);
        CHECK(!lh->Holding());
        TraverseJobs()[hero].rememberLip = true;
        lh->LateUpdate(0.016f);
        CHECK(lh->Holding());
        TraverseJobs().erase(hero);

        // Missing arm: no crash, no hold.
        lh->rightArm = nullptr;
        lh->LateUpdate(0.016f);
        CHECK(!lh->Holding());
    }

    // ---- autoWire on a non-humanoid adds nothing
    {
        Scene sc;
        GameObject* box = sc.CreateGameObject("Box");
        auto* lh = box->AddComponent<LedgeHandsIK>();
        lh->Start();
        CHECK(lh->leftArm == nullptr && lh->rightArm == nullptr);
        CHECK(box->GetComponents<LimbIK>().empty());
        lh->LateUpdate(0.016f);
        CHECK(!lh->Holding());
    }


    // ---- real grab through the scene: the hands land on the true ledge edge, not on the top behind it
    {
        TraverseJobs().clear(); HangPoints().clear(); HangGravity().clear();
        Scene s; s.physicsEnabled = true;
        LedgeGeometry(s, 3.5f);                       // top at y 3.5, face at z -0.4
        Rig r = MakeRig(s, {0, 2.0f, 0}, true);       // in the air in front of the wall, facing -Z
        r.hero->AddComponent<TraverseTicker>();
        auto* lh = r.hero->AddComponent<LedgeHandsIK>();
        lh->autoWire = false; lh->leftArm = r.la; lh->rightArm = r.ra;
        lh->fadeTime = 0.1f; lh->handSpread = 0.2f; lh->overEdge = 0.0f;
        s.Start();
        Frames(s, 1);
        CHECK(HumanoidActions::RequestLedgeGrab(r.hero));
        Frames(s, 60);
        CHECK(HangPoints().count(r.hero) == 1);
        CHECK(lh->Holding());
        const Vec3 edgeL{-0.2f, 3.5f, -0.4f}, edgeR{0.2f, 3.5f, -0.4f};
        const float dl = (r.lHand->transform->Position() - edgeL).Magnitude();
        const float dr = (r.rHand->transform->Position() - edgeR).Magnitude();
        std::printf("  hands from the true edge: left %.3f m, right %.3f m\n", dl, dr);
        CHECK(dl < 0.04f);
        CHECK(dr < 0.04f);

        // Disabled mid-hang: LateUpdate stops running, so OnDisable must let go of the arms.
        lh->enabled = false;
        Frames(s, 1);
        CHECK(!r.la->ikActive && !r.ra->ikActive);
        lh->enabled = true;
        Frames(s, 2);
        CHECK(lh->Holding() && r.la->ikActive && r.ra->ikActive);
        // Removed mid-hang: OnDestroy lets go too.
        r.hero->RemoveComponent(lh);
        Frames(s, 1);
        CHECK(!r.la->ikActive && !r.ra->ikActive);
        TraverseJobs().clear(); HangPoints().clear(); HangGravity().clear();
    }

    // ---- arms shared with another user: left alone while not hanging, given back after the hang
    {
        TraverseJobs().clear(); HangPoints().clear(); HangGravity().clear();
        Scene s; s.physicsEnabled = false;
        Rig r = MakeRig(s, {0, 0, 0}, true);
        GameObject* cup = s.CreateGameObject("Cup");      // what a held-object setup aims the left hand at
        cup->transform->SetPosition({-0.3f, 1.2f, -0.4f});
        GameObject* cup2 = s.CreateGameObject("Cup2");    // the right hand's idle target, destroyed mid-hang
        cup2->transform->SetPosition({0.3f, 1.2f, -0.4f});
        r.la->ikActive = true;  r.la->targetObject = cup->transform;  r.la->fadeTime = 0.3f;
        r.ra->ikActive = false; r.ra->targetObject = cup2->transform; r.ra->fadeTime = 0.5f;
        auto* lh = r.hero->AddComponent<LedgeHandsIK>();
        lh->autoWire = false; lh->leftArm = r.la; lh->rightArm = r.ra; lh->fadeTime = 0.1f;
        s.Start();
        Frames(s, 10);
        CHECK(!lh->Holding());
        CHECK(r.la->ikActive && r.la->targetObject == cup->transform && std::fabs(r.la->fadeTime - 0.3f) < 1e-6f);
        CHECK(!r.ra->ikActive && r.ra->targetObject == cup2->transform && std::fabs(r.ra->fadeTime - 0.5f) < 1e-6f);

        HangPoints()[r.hero] = Vec3{0, 1.9f, -0.75f};
        Frames(s, 5);
        CHECK(lh->Holding());
        CHECK(r.la->ikActive && r.ra->ikActive && r.la->targetObject == nullptr);
        s.Destroy(cup2);
        Frames(s, 2);

        HangPoints().erase(r.hero);
        Frames(s, 30);
        CHECK(!lh->Holding());
        CHECK(r.la->ikActive && r.la->targetObject == cup->transform && std::fabs(r.la->fadeTime - 0.3f) < 1e-6f);
        CHECK(!r.ra->ikActive && r.ra->targetObject == nullptr && std::fabs(r.ra->fadeTime - 0.5f) < 1e-6f);
        TraverseJobs().clear(); HangPoints().clear(); HangGravity().clear();
    }

    // ---- auto-wire reuses a LimbIK already on a hand: never two solvers on one arm
    {
        Scene s; s.physicsEnabled = false;
        Rig r = MakeRig(s, {0, 0, 0}, false);
        auto* existing = r.hero->AddComponent<LimbIK>();
        existing->upper = r.lHand->transform->Parent()->Parent();
        existing->lower = r.lHand->transform->Parent();
        existing->end = r.lHand->transform;
        auto* lh = r.hero->AddComponent<LedgeHandsIK>();
        s.Start();
        Frames(s, 1);
        CHECK(lh->leftArm == existing);
        CHECK(lh->rightArm != nullptr && lh->rightArm != existing);
        CHECK(r.hero->GetComponentsInChildren<LimbIK>().size() == 2);
    }

    // ---- an arm's GameObject destroyed mid-hang: the pointer is dropped, frames keep running
    {
        TraverseJobs().clear(); HangPoints().clear(); HangGravity().clear();
        Scene s; s.physicsEnabled = false;
        GameObject* lHost = s.CreateGameObject("LeftArmIK");
        GameObject* rHost = s.CreateGameObject("RightArmIK");
        Rig r = MakeRig(s, {0, 0, 0}, true, lHost, rHost);
        lHost->transform->SetParent(r.hero->transform, false);
        rHost->transform->SetParent(r.hero->transform, false);
        auto* lh = r.hero->AddComponent<LedgeHandsIK>();
        lh->autoWire = false; lh->leftArm = r.la; lh->rightArm = r.ra; lh->fadeTime = 0.1f;
        s.Start();
        HangPoints()[r.hero] = Vec3{0, 1.9f, -0.75f};
        Frames(s, 5);
        CHECK(lh->Holding());
        s.Destroy(lHost);
        Frames(s, 10);
        CHECK(lh->leftArm == nullptr);
        CHECK(!lh->Holding());
        CHECK(!r.ra->ikActive);   // the surviving arm is given back (it was off before the hang)
        HangPoints().erase(r.hero);
        Frames(s, 5);
        TraverseJobs().clear(); HangPoints().clear(); HangGravity().clear();
    }

    TEST_MAIN_RESULT();
}
