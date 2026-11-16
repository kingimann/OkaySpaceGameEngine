// Follow-camera fixes (#888 findings 66 and 67).
//
// 66: CameraFollow wrote a world position into localPosition, so a camera under a
//     moved, rotated or scaled parent ran away (smoothing) or sat off by the parent's
//     offset (snap). It now writes the world position through Transform::SetPosition.
// 67: one non-finite target position left VirtualCamera, CameraFollow and
//     FollowTarget2D at NaN for good. A non-finite target is now skipped for that
//     frame, and eased state that is not finite is reset, so they recover.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <limits>

using namespace okay;

namespace {

const float kNaN = std::numeric_limits<float>::quiet_NaN();

bool AllFinite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

} // namespace

int main() {
    RUN_SUITE("camera_follow_fixes");

    // 66: CameraFollow under an offset, rotated and scaled parent lands on the target
    // + offset in WORLD space, with smoothing and with snap.
    for (float smoothing : {5.0f, 0.0f}) {
        Scene s("Rig"); s.physicsEnabled = false;
        GameObject* target = s.CreateGameObject("Target");
        target->transform->localPosition = {0, 0, 0};
        GameObject* rig = s.CreateGameObject("Rig");
        rig->transform->localPosition = {100, 0, 0};
        rig->transform->localRotation = Quat::Euler(0.0f, 90.0f, 0.0f);
        rig->transform->localScale = {2, 2, 2};
        GameObject* cam = s.CreateGameObject("Cam");
        cam->transform->SetParent(rig->transform, false);
        auto* cf = cam->AddComponent<CameraFollow>();
        cf->targetName = "Target";
        cf->offset = {0, 2, -5};
        cf->smoothing = smoothing;
        s.Start();
        for (int i = 0; i < 600; ++i) s.Update(1.0f / 60.0f);   // 10 s
        const Vec3 w = cam->transform->Position();
        std::printf("  parented CameraFollow smoothing=%.0f: world (%.3f, %.3f, %.3f), expected (0, 2, -5)\n",
                    smoothing, w.x, w.y, w.z);
        CHECK_NEAR(w.x, 0.0f, 1e-3f);
        CHECK_NEAR(w.y, 2.0f, 1e-3f);
        CHECK_NEAR(w.z, -5.0f, 1e-3f);

        // The target moves: the camera follows it in world space.
        target->transform->localPosition = {-7, 1, 3};
        for (int i = 0; i < 600; ++i) s.Update(1.0f / 60.0f);
        const Vec3 w2 = cam->transform->Position();
        CHECK_NEAR(w2.x, -7.0f, 1e-3f);
        CHECK_NEAR(w2.y, 3.0f, 1e-3f);
        CHECK_NEAR(w2.z, -2.0f, 1e-3f);
    }

    // Unchanged finite behaviour at the scene root: the exact exponential ease the
    // component has always used, frame by frame.
    {
        Scene s("Root"); s.physicsEnabled = false;
        GameObject* target = s.CreateGameObject("Target");
        target->transform->localPosition = {10, 5, 0};
        GameObject* cam = s.CreateGameObject("Cam");
        cam->transform->localPosition = {1, 2, 3};
        auto* cf = cam->AddComponent<CameraFollow>();
        cf->targetName = "Target";
        cf->offset = {0, 1, -4};
        cf->smoothing = 3.0f;
        s.Start();
        Vec3 expect = {1, 2, 3};
        const Vec3 desired = {10, 6, -4};
        float worst = 0.0f;
        for (int i = 0; i < 120; ++i) {
            const float dt = (i % 3 == 0) ? 1.0f / 30.0f : 1.0f / 90.0f;
            s.Update(dt);
            const float t = 1.0f - std::exp(-3.0f * dt);
            expect = Vec3::Lerp(expect, desired, t);
            const Vec3 got = cam->transform->localPosition;
            worst = std::max(worst, (got - expect).Magnitude());
        }
        std::printf("  root CameraFollow vs reference ease: worst difference %.2e\n", worst);
        CHECK(worst < 1e-5f);
    }

    // 67: CameraFollow, one NaN target frame, then finite targets.
    {
        Scene s("CfNaN"); s.physicsEnabled = false;
        GameObject* target = s.CreateGameObject("Target");
        GameObject* cam = s.CreateGameObject("Cam");
        auto* cf = cam->AddComponent<CameraFollow>();
        cf->targetName = "Target";
        cf->offset = {0, 2, -5};
        cf->smoothing = 5.0f;
        s.Start();
        for (int i = 0; i < 30; ++i) s.Update(1.0f / 60.0f);
        target->transform->localPosition = {kNaN, 0, 0};
        s.Update(1.0f / 60.0f);
        CHECK(AllFinite(cam->transform->Position()));
        target->transform->localPosition = {1, 0, 0};
        bool allFinite = true;
        for (int i = 0; i < 600; ++i) { s.Update(1.0f / 60.0f); allFinite = allFinite && AllFinite(cam->transform->Position()); }
        const Vec3 w = cam->transform->Position();
        std::printf("  CameraFollow after a NaN frame, 10 s later: (%.3f, %.3f, %.3f)\n", w.x, w.y, w.z);
        CHECK(allFinite);
        CHECK_NEAR(w.x, 1.0f, 1e-3f);
        CHECK_NEAR(w.y, 2.0f, 1e-3f);
        CHECK_NEAR(w.z, -5.0f, 1e-3f);

        // Snap mode too.
        cf->smoothing = 0.0f;
        target->transform->localPosition = {kNaN, kNaN, kNaN};
        s.Update(1.0f / 60.0f);
        CHECK(AllFinite(cam->transform->Position()));
        target->transform->localPosition = {3, 0, 0};
        s.Update(1.0f / 60.0f);
        CHECK_NEAR(cam->transform->Position().x, 3.0f, 1e-4f);

        // A camera whose own position went non-finite snaps back to the target.
        cf->smoothing = 5.0f;
        cam->transform->localPosition = {kNaN, 0, 0};
        s.Update(1.0f / 60.0f);
        CHECK(AllFinite(cam->transform->Position()));
    }

    // 67: VirtualCamera (through the brain), one NaN follow frame, with damping.
    {
        Scene s("VcNaN"); s.physicsEnabled = false;
        GameObject* player = s.CreateGameObject("Player");
        player->transform->localPosition = {0, 0, 0};
        GameObject* camGO = s.CreateGameObject("MainCamera");
        camGO->AddComponent<Camera>();
        auto* brain = camGO->AddComponent<CinemachineBrain>();
        brain->blendTime = 0.0f;
        GameObject* vcGO = s.CreateGameObject("VCam");
        auto* vc = vcGO->AddComponent<VirtualCamera>();
        vc->follow = "Player";
        vc->lookAt = "Player";
        vc->followOffset = {0, 3, -8};
        vc->positionDamping = 4.0f;
        vc->rotationDamping = 4.0f;
        vc->priority = 10;
        s.Start();
        for (int i = 0; i < 30; ++i) s.Update(1.0f / 60.0f);
        player->transform->localPosition = {kNaN, 0, 0};
        s.Update(1.0f / 60.0f);
        CHECK(AllFinite(vc->SolvedPosition()));
        player->transform->localPosition = {1, 0, 0};
        bool allFinite = true;
        for (int i = 0; i < 600; ++i) {
            s.Update(1.0f / 60.0f);
            const Quat r = vc->SolvedRotation();
            allFinite = allFinite && AllFinite(vc->SolvedPosition()) && AllFinite(camGO->transform->Position()) &&
                        std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.z) && std::isfinite(r.w);
        }
        const Vec3 p = vc->SolvedPosition();
        std::printf("  VirtualCamera after a NaN frame, 10 s later: (%.3f, %.3f, %.3f)\n", p.x, p.y, p.z);
        CHECK(allFinite);
        CHECK_NEAR(p.x, 1.0f, 1e-3f);
        CHECK_NEAR(p.y, 3.0f, 1e-3f);
        CHECK_NEAR(p.z, -8.0f, 1e-3f);
        // The aim points at the target again.
        const Vec3 fwd = vc->SolvedRotation() * Vec3::Forward;
        const Vec3 want = (Vec3{1, 0, 0} - p).Normalized();
        CHECK(Vec3::Dot(fwd.Normalized(), want) > 0.999f);

        // A NaN target rotation (LockToTarget) does not poison the aim either.
        vc->bindingMode = VirtualCamera::BindingMode::LockToTarget;
        vc->lookAt.clear();
        player->transform->localRotation = Quat(kNaN, 0, 0, 1);
        s.Update(1.0f / 60.0f);
        CHECK(AllFinite(vc->SolvedPosition()));
        player->transform->localRotation = Quat::Identity;
        for (int i = 0; i < 300; ++i) s.Update(1.0f / 60.0f);
        const Quat r = vc->SolvedRotation();
        CHECK(std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.z) && std::isfinite(r.w));
        CHECK(AllFinite(vc->SolvedPosition()));
    }

    // 67: FollowTarget2D, one NaN target frame (Transform mover).
    {
        Scene s("FtNaN"); s.physicsEnabled = false;
        GameObject* player = s.CreateGameObject("Player");
        player->transform->localPosition = {5, 0, 0};
        GameObject* enemy = s.CreateGameObject("Enemy");
        auto* ft = enemy->AddComponent<FollowTarget2D>();
        ft->target = "Player"; ft->speed = 3.0f; ft->stopDistance = 0.0f;
        s.Start();
        s.Update(1.0f / 60.0f);
        player->transform->localPosition = {kNaN, 0, 0};
        s.Update(1.0f / 60.0f);
        CHECK(AllFinite(enemy->transform->Position()));
        player->transform->localPosition = {5, 0, 0};
        bool allFinite = true;
        for (int i = 0; i < 300; ++i) { s.Update(1.0f / 60.0f); allFinite = allFinite && AllFinite(enemy->transform->Position()); }
        const Vec3 e = enemy->transform->Position();
        std::printf("  FollowTarget2D after a NaN frame, 5 s later: (%.3f, %.3f, %.3f)\n", e.x, e.y, e.z);
        CHECK(allFinite);
        CHECK_NEAR(e.x, 5.0f, 0.06f);
        CHECK_NEAR(e.y, 0.0f, 1e-4f);
    }

    TEST_MAIN_RESULT();
}
