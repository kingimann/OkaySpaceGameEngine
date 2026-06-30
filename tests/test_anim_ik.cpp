#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>

using namespace okay;

static bool V3(const Vec3& a, const Vec3& b, float eps = 1e-2f) {
    return std::fabs(a.x-b.x) < eps && std::fabs(a.y-b.y) < eps && std::fabs(a.z-b.z) < eps;
}

// Two-bone IK solver, FootIK ground planting, and RootMotion extraction.
int main() {
    RUN_SUITE("anim_ik");

    // ---- Two-bone IK: reaches the target and preserves bone lengths ----
    {
        Vec3 root{0, 2, 0};
        float up = 1.0f, lo = 1.0f;
        Vec3 mid, end;
        // Target straight below, within reach (dist 1.5 < 2.0).
        SolveTwoBoneIK(root, up, lo, {0, 0.5f, 0}, {0, 0, 1}, mid, end);
        CHECK(std::fabs((mid - root).Magnitude() - up) < 1e-3f);   // upper length kept
        CHECK(std::fabs((end - mid).Magnitude() - lo) < 1e-3f);    // lower length kept
        CHECK(V3(end, {0, 0.5f, 0}));                              // reached the target
        CHECK(mid.z > 0.01f);                                      // knee bent toward +Z pole

        // Out-of-reach target is clamped to max reach (no stretch), chain ~straight.
        SolveTwoBoneIK(root, up, lo, {0, -10, 0}, {0, 0, 1}, mid, end);
        CHECK(std::fabs((mid - root).Magnitude() - up) < 1e-3f);
        CHECK(std::fabs((end - mid).Magnitude() - lo) < 1e-3f);
        CHECK((end - root).Magnitude() < up + lo + 1e-2f);         // never longer than reach
    }

    // ---- FootIK: lifts a foot up onto ground that's above the animated pose ----
    {
        Scene s("footik");
        auto* body = s.CreateGameObject("Body");          // faces +Z by default
        auto* hipO  = s.CreateGameObject("Hip");   hipO->transform->SetPosition({0, 1.0f, 0});
        auto* kneeO = s.CreateGameObject("Knee");  kneeO->transform->SetPosition({0, 0.55f, 0});
        auto* footO = s.CreateGameObject("Foot");  footO->transform->SetPosition({0, 0.1f, 0});

        auto* ik = body->AddComponent<FootIK>();
        ik->leftHip = hipO->transform; ik->leftKnee = kneeO->transform; ik->leftFoot = footO->transform;
        ik->useRaycast = false; ik->groundY = 0.3f; ik->footOffset = 0.05f; ik->weight = 1.0f;
        float upLen = 0.45f, loLen = 0.45f;
        (void)upLen; (void)loLen;
        s.Start();
        s.Update(1.0f / 60.0f);
        // Foot planted at ground + offset (0.35), lifted from its animated 0.1.
        CHECK(std::fabs(footO->transform->Position().y - 0.35f) < 1e-2f);
        // Lengths preserved and knee bent forward.
        float a = (kneeO->transform->Position() - hipO->transform->Position()).Magnitude();
        float b = (footO->transform->Position() - kneeO->transform->Position()).Magnitude();
        CHECK(std::fabs(a - 0.45f) < 2e-2f);
        CHECK(std::fabs(b - 0.45f) < 2e-2f);
        CHECK(kneeO->transform->Position().z > 0.0f);

        // Ground below the foot: leave the animation alone (no sinking).
        Scene s2("footik2");
        auto* b2 = s2.CreateGameObject("Body");
        auto* hp = s2.CreateGameObject("Hip");  hp->transform->SetPosition({0, 1.0f, 0});
        auto* kn = s2.CreateGameObject("Knee"); kn->transform->SetPosition({0, 0.55f, 0});
        auto* ft = s2.CreateGameObject("Foot"); ft->transform->SetPosition({0, 0.5f, 0});
        auto* ik2 = b2->AddComponent<FootIK>();
        ik2->leftHip = hp->transform; ik2->leftKnee = kn->transform; ik2->leftFoot = ft->transform;
        ik2->useRaycast = false; ik2->groundY = 0.0f; ik2->weight = 1.0f;
        s2.Start();
        s2.Update(1.0f / 60.0f);
        CHECK(std::fabs(ft->transform->Position().y - 0.5f) < 1e-3f);   // untouched
    }

    // ---- RootMotion: the bone's motion drives the body; the bone re-centers ----
    {
        Scene s("rootmotion");
        auto* body = s.CreateGameObject("Body");           // at origin, facing +Z
        auto* hips = s.CreateGameObject("Hips");
        auto* rm = body->AddComponent<RootMotion>();
        rm->rootNode = hips->transform;
        rm->lockHeight = true;
        s.Start();

        // Frame 0 establishes the baseline.
        hips->transform->localPosition = {0, 0, 0};
        s.Update(1.0f / 60.0f);
        // Simulate an animation walking the hips forward (+Z) by 0.1 each frame.
        for (int i = 1; i <= 10; ++i) {
            hips->transform->localPosition = {0, 0, 0.1f * i};
            s.Update(1.0f / 60.0f);
        }
        // Body advanced ~1.0 in +Z (forward), and the hips bone was re-centered.
        CHECK(body->transform->Position().z > 0.8f);
        CHECK(std::fabs(body->transform->Position().y) < 1e-3f);   // height locked
        CHECK(std::fabs(hips->transform->localPosition.z) < 0.2f); // bone pinned back

        // Disabled mode leaves the body where it is.
        Scene s2("rm_off");
        auto* b2 = s2.CreateGameObject("Body");
        auto* h2 = s2.CreateGameObject("Hips");
        auto* rm2 = b2->AddComponent<RootMotion>();
        rm2->rootNode = h2->transform; rm2->mode = (int)RootMotion::Mode::Disabled;
        s2.Start();
        for (int i = 0; i <= 5; ++i) { h2->transform->localPosition = {0, 0, 0.1f * i}; s2.Update(1.0f/60.0f); }
        CHECK(std::fabs(b2->transform->Position().z) < 1e-3f);     // body didn't move
    }

    // ---- Look-At IK: the tip bone's forward ends up pointing at the target ----
    {
        Scene s("lookat");
        auto* npc  = s.CreateGameObject("NPC");
        auto* neck = s.CreateGameObject("Neck"); neck->transform->SetPosition({0, 1.5f, 0});
        auto* head = s.CreateGameObject("Head"); head->transform->SetPosition({0, 1.7f, 0});
        auto* ik = npc->AddComponent<LookAtIK>();
        ik->chain = {neck->transform, head->transform};
        ik->target = {5, 1.7f, 0};                 // straight to the right (+X)
        ik->weight = 1.0f; ik->maxAngle = 180.0f;  // allow the full turn for the test
        s.Start();
        for (int i = 0; i < 5; ++i) s.Update(1.0f / 60.0f);   // ease in over a few frames
        Vec3 fwd = (head->transform->Rotation() * Vec3::Forward).Normalized();
        Vec3 want = ((Vec3{5, 1.7f, 0}) - head->transform->Position()).Normalized();
        CHECK(Vec3::Dot(fwd, want) > 0.99f);       // head looks at the target

        // Weight 0 leaves the bone unrotated.
        Scene s2("lookoff");
        auto* n2 = s2.CreateGameObject("NPC");
        auto* h2 = s2.CreateGameObject("Head"); h2->transform->SetPosition({0, 1.7f, 0});
        auto* ik2 = n2->AddComponent<LookAtIK>();
        ik2->chain = {h2->transform}; ik2->target = {5, 0, 0}; ik2->weight = 0.0f;
        s2.Start(); s2.Update(1.0f / 60.0f);
        CHECK(Quat::Angle(h2->transform->Rotation(), Quat::Identity) < 0.01f);
    }

    // ---- FABRIK: a long chain reaches the target, root pinned, lengths kept ----
    {
        std::vector<Vec3> joints = {{0,0,0}, {0,1,0}, {0,2,0}, {0,3,0}};   // 3 unit bones up
        std::vector<float> len = {1, 1, 1};
        SolveFabrik(joints, len, {2.0f, 1.0f, 0.0f}, 20);
        CHECK(V3(joints[0], {0, 0, 0}));                       // root pinned
        CHECK(V3(joints[3], {2.0f, 1.0f, 0.0f}, 2e-2f));       // tip reached target
        for (int i = 0; i < 3; ++i)
            CHECK(std::fabs((joints[i+1] - joints[i]).Magnitude() - 1.0f) < 1e-2f);  // lengths kept

        // Out of reach (target 10 away, reach 3): chain straightens toward it.
        SolveFabrik(joints, len, {10, 0, 0}, 20);
        CHECK(V3(joints[0], {0, 0, 0}));
        CHECK(std::fabs((joints[3] - joints[0]).Magnitude() - 3.0f) < 1e-2f);  // fully extended
        CHECK(joints[3].x > 2.9f);                             // pointing at the target
    }

    // ---- ChainIK component: drives a bone chain's tip to a target via FABRIK ----
    {
        Scene s("chainik");
        auto* root = s.CreateGameObject("Root");
        auto* b0 = s.CreateGameObject("B0"); b0->transform->SetPosition({0, 0, 0});
        auto* b1 = s.CreateGameObject("B1"); b1->transform->SetPosition({0, 1, 0});
        auto* b2 = s.CreateGameObject("B2"); b2->transform->SetPosition({0, 2, 0});
        auto* b3 = s.CreateGameObject("B3"); b3->transform->SetPosition({0, 3, 0});
        auto* ik = root->AddComponent<ChainIK>();
        ik->bones = {b0->transform, b1->transform, b2->transform, b3->transform};
        ik->target = {2.0f, 1.0f, 0.0f}; ik->weight = 1.0f; ik->iterations = 20;
        s.Start();
        s.Update(1.0f / 60.0f);
        CHECK(V3(b0->transform->Position(), {0, 0, 0}));               // root stays
        CHECK(V3(b3->transform->Position(), {2.0f, 1.0f, 0.0f}, 3e-2f)); // tip reaches
    }

    // ---- CCD: reaches the target, root fixed, bone lengths preserved ----
    {
        std::vector<Vec3> joints = {{0,0,0}, {0,1,0}, {0,2,0}, {0,3,0}};
        SolveCCD(joints, {2.0f, 1.0f, 0.0f}, 40);
        CHECK(V3(joints[0], {0, 0, 0}));                       // root never moves in CCD
        CHECK(V3(joints[3], {2.0f, 1.0f, 0.0f}, 5e-2f));       // tip reached
        for (int i = 0; i < 3; ++i)
            CHECK(std::fabs((joints[i+1] - joints[i]).Magnitude() - 1.0f) < 1e-3f);  // rigid bones

        // ChainIK with the CCD solver selected drives a real chain.
        Scene s("ccd");
        auto* root = s.CreateGameObject("R");
        auto* b0 = s.CreateGameObject("B0"); b0->transform->SetPosition({0, 0, 0});
        auto* b1 = s.CreateGameObject("B1"); b1->transform->SetPosition({0, 1, 0});
        auto* b2 = s.CreateGameObject("B2"); b2->transform->SetPosition({0, 2, 0});
        auto* ik = root->AddComponent<ChainIK>();
        ik->bones = {b0->transform, b1->transform, b2->transform};
        ik->target = {1.5f, 0.5f, 0.0f}; ik->iterations = 40;
        ik->solver = (int)ChainIK::Solver::CCD;
        s.Start(); s.Update(1.0f / 60.0f);
        CHECK(V3(b0->transform->Position(), {0, 0, 0}));
        CHECK(V3(b2->transform->Position(), {1.5f, 0.5f, 0.0f}, 6e-2f));
    }

    // ---- AimIK: a bone's aim axis points at the target ----
    {
        Scene s("aim");
        auto* turret = s.CreateGameObject("Turret");
        turret->transform->SetPosition({0, 1, 0});
        auto* ik = turret->AddComponent<AimIK>();
        ik->bone = turret->transform;
        ik->aimAxis = Vec3::Forward;             // +Z barrel
        ik->target = {10, 1, 0};                 // dead to the right
        ik->weight = 1.0f; ik->maxAngle = 180.0f;
        s.Start();
        for (int i = 0; i < 5; ++i) s.Update(1.0f / 60.0f);
        Vec3 aim = (turret->transform->Rotation() * Vec3::Forward).Normalized();
        Vec3 want = (Vec3{10, 1, 0} - turret->transform->Position()).Normalized();
        CHECK(Vec3::Dot(aim, want) > 0.99f);
    }

    // ---- FootIK pelvis adjustment: hips drop so a foot on lower ground reaches ----
    {
        Scene s("pelvis");
        auto* body  = s.CreateGameObject("Body");
        // A real leg hierarchy: hips -> hip -> knee -> foot, so lowering the pelvis
        // lowers the leg roots (and feet) with it.
        auto* hips  = s.CreateGameObject("Hips"); hips->transform->SetPosition({0, 1.0f, 0});
        auto* lHip  = s.CreateGameObject("LHip");  lHip->transform->SetPosition({-0.2f, 1.0f, 0});
        auto* lKnee = s.CreateGameObject("LKnee"); lKnee->transform->SetPosition({-0.2f, 0.55f, 0});
        auto* lFoot = s.CreateGameObject("LFoot"); lFoot->transform->SetPosition({-0.2f, 0.1f, 0});
        auto* rHip  = s.CreateGameObject("RHip");  rHip->transform->SetPosition({0.2f, 1.0f, 0});
        auto* rKnee = s.CreateGameObject("RKnee"); rKnee->transform->SetPosition({0.2f, 0.55f, 0});
        auto* rFoot = s.CreateGameObject("RFoot"); rFoot->transform->SetPosition({0.2f, 0.1f, 0});
        lHip->transform->SetParent(hips->transform, true);  lKnee->transform->SetParent(lHip->transform, true);  lFoot->transform->SetParent(lKnee->transform, true);
        rHip->transform->SetParent(hips->transform, true);  rKnee->transform->SetParent(rHip->transform, true);  rFoot->transform->SetParent(rKnee->transform, true);

        auto* ik = body->AddComponent<FootIK>();
        ik->leftHip = lHip->transform;  ik->leftKnee = lKnee->transform;  ik->leftFoot = lFoot->transform;
        ik->rightHip = rHip->transform; ik->rightKnee = rKnee->transform; ik->rightFoot = rFoot->transform;
        ik->useRaycast = false; ik->groundY = -0.3f;   // ground BELOW the animated feet
        ik->footOffset = 0.0f; ik->weight = 1.0f;
        ik->pelvis = hips->transform; ik->adjustPelvis = true; ik->plantDown = true;
        ik->maxRayDown = 2.0f;
        s.Start();
        s.Update(1.0f / 60.0f);
        CHECK(hips->transform->Position().y < 1.0f);          // pelvis lowered toward the ground
        CHECK(std::fabs(lFoot->transform->Position().y - (-0.3f)) < 0.1f);   // feet planted on it
    }

    // ---- Two-bone bend limit: a max knee angle stops the limb hyperextending ----
    {
        Vec3 root{0, 2, 0};
        float up = 1.0f, lo = 1.0f;
        Vec3 mid, end;
        // Target far below (would fully straighten the limb to reach 2.0 down).
        // Limit the knee to 90° interior: reach is sqrt(1+1)=~1.414, not 2.0.
        SolveTwoBoneIK(root, up, lo, {0, -1.0f, 0}, {0, 0, 1}, mid, end, 0.0f, 90.0f);
        float reach = (end - root).Magnitude();
        CHECK(reach < 1.6f);                                   // didn't straighten out
        CHECK(std::fabs(reach - 1.4142f) < 0.05f);             // capped at the 90° distance
        CHECK(std::fabs((mid - root).Magnitude() - up) < 1e-3f);   // lengths still exact
        CHECK(std::fabs((end - mid).Magnitude() - lo) < 1e-3f);

        // No limit (default): same target reaches farther (closer to straight).
        Vec3 mid2, end2;
        SolveTwoBoneIK(root, up, lo, {0, -1.0f, 0}, {0, 0, 1}, mid2, end2);
        CHECK((end2 - root).Magnitude() > reach + 0.3f);
    }

    // ---- FootIK ground-normal tilt: the planted foot rotates to match a slope ----
    {
        Scene s("foottilt");
        auto* body = s.CreateGameObject("Body");
        auto* hip  = s.CreateGameObject("Hip");  hip->transform->SetPosition({0, 1.0f, 0});
        auto* knee = s.CreateGameObject("Knee"); knee->transform->SetPosition({0, 0.55f, 0});
        auto* foot = s.CreateGameObject("Foot"); foot->transform->SetPosition({0, 0.2f, 0});
        auto* ik = body->AddComponent<FootIK>();
        ik->leftHip = hip->transform; ik->leftKnee = knee->transform; ik->leftFoot = foot->transform;
        ik->useRaycast = false; ik->groundY = 0.3f; ik->footOffset = 0.0f; ik->weight = 1.0f;
        // (No raycast -> normal defaults to Up, so the foot stays level here.)
        ik->alignToGround = true; ik->footUpAxis = Vec3::Up;
        s.Start(); s.Update(1.0f / 60.0f);
        Vec3 footUp = (foot->transform->Rotation() * Vec3::Up).Normalized();
        CHECK(Vec3::Dot(footUp, Vec3::Up) > 0.99f);            // level ground -> foot stays flat

        // The solver itself tilts the foot toward a given slope normal.
        Quat r = Quat::Identity;
        Vec3 up0 = r * Vec3::Up;
        Vec3 slope = Vec3{0.3f, 1.0f, 0.0f}.Normalized();
        Quat tilt = Quat::FromToRotation(up0, slope);
        Vec3 tilted = (tilt * up0).Normalized();
        CHECK(Vec3::Dot(tilted, slope) > 0.99f);               // FromToRotation aligns up to the slope
    }

    TEST_MAIN_RESULT();
}
