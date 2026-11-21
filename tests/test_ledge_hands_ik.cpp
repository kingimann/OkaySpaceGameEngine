#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Math/LedgeHands.hpp"
#include <cmath>

using namespace okay;

static GameObject* Bone(Scene& sc, const char* name, GameObject* parent, Vec3 local) {
    GameObject* o = sc.CreateGameObject(name);
    o->transform->SetParent(parent->transform, false);
    o->transform->localPosition = local;
    return o;
}

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

    TEST_MAIN_RESULT();
}
