// Camera robustness: the simple chase camera (CameraFollow), bad numbers into the Camera component, and the
// orbit (FreeLook) angles. Headless, no window. Each case failed before the fix in the same commit.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>

using namespace okay;

static bool Finite3(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

int main() {
    RUN_SUITE("camera_robust");
    const float kNaN = std::nanf("");

    {   // CameraFollow on a camera that has a parent: the world position must reach target + offset
        Scene s("S"); s.physicsEnabled = false;
        GameObject* tgt = s.CreateGameObject("T"); tgt->transform->localPosition = {10, 0, 0};
        GameObject* parent = s.CreateGameObject("Rail"); parent->transform->localPosition = {100, 50, 0};
        GameObject* cg = s.CreateGameObject("Cam"); cg->transform->SetParent(parent->transform);
        cg->AddComponent<Camera>();
        CameraFollow* f = cg->AddComponent<CameraFollow>();
        f->targetName = "T"; f->offset = {0, 2, -5}; f->smoothing = 8.0f;
        s.Start();
        for (int i = 0; i < 240; ++i) s.Update(1.0f / 60.0f);
        const Vec3 p = cg->transform->Position();
        CHECK_NEAR(p.x, 10.0f, 0.05f); CHECK_NEAR(p.y, 2.0f, 0.05f); CHECK_NEAR(p.z, -5.0f, 0.05f);
    }
    {   // CameraFollow starts on the target, it does not fly in from far away on the first frame
        Scene s("S"); s.physicsEnabled = false;
        GameObject* tgt = s.CreateGameObject("T"); tgt->transform->localPosition = {300, 0, 0};
        GameObject* cg = s.CreateGameObject("Cam"); cg->AddComponent<Camera>();
        CameraFollow* f = cg->AddComponent<CameraFollow>(); f->targetName = "T"; f->smoothing = 5.0f;
        (void)tgt;
        s.Start();
        s.Update(1.0f / 60.0f);
        CHECK((cg->transform->Position() - Vec3{300, 0, 0}).Magnitude() < 1.0f);
    }
    {   // CameraFollow: same place after 1 s at 30, 60 and 144 fps (target moving steadily)
        const float rates[3] = {30.0f, 60.0f, 144.0f};
        Vec3 at[3];
        for (int r = 0; r < 3; ++r) {
            Scene s("S"); s.physicsEnabled = false;
            GameObject* tgt = s.CreateGameObject("T");
            GameObject* cg = s.CreateGameObject("Cam"); cg->AddComponent<Camera>();
            CameraFollow* f = cg->AddComponent<CameraFollow>(); f->targetName = "T"; f->smoothing = 4.0f;
            s.Start();
            const float dt = 1.0f / rates[r]; const int n = (int)std::lround(rates[r]);
            for (int i = 0; i < n; ++i) { tgt->transform->localPosition = {6.0f * dt * (float)(i + 1), 0, 0}; s.Update(dt); }
            at[r] = cg->transform->Position();
        }
        CHECK_NEAR(at[0].x, at[1].x, 0.06f); CHECK_NEAR(at[2].x, at[1].x, 0.06f);
    }
    {   // CameraFollow: NaN step, NaN target and a destroyed target never make the camera NaN
        Scene s("S"); s.physicsEnabled = false;
        GameObject* tgt = s.CreateGameObject("T");
        GameObject* cg = s.CreateGameObject("Cam"); cg->AddComponent<Camera>();
        CameraFollow* f = cg->AddComponent<CameraFollow>(); f->targetName = "T"; f->smoothing = 5.0f;
        s.Start();
        for (int i = 0; i < 10; ++i) s.Update(1.0f / 60.0f);
        f->LateUpdate(kNaN);
        CHECK(Finite3(cg->transform->Position()));
        tgt->transform->localPosition = {kNaN, 0, 0};
        for (int i = 0; i < 5; ++i) s.Update(1.0f / 60.0f);
        CHECK(Finite3(cg->transform->Position()));
        tgt->transform->localPosition = {1, 0, 0};
        for (int i = 0; i < 60; ++i) s.Update(1.0f / 60.0f);
        CHECK_NEAR(cg->transform->Position().x, 1.0f, 0.1f);
        s.DestroyImmediate(tgt);
        for (int i = 0; i < 5; ++i) s.Update(1.0f / 60.0f);
        CHECK(Finite3(cg->transform->Position()));
    }
    {   // One owner: a camera with a brain and a live shot is placed by the shot, not dragged back by CameraFollow
        Scene s("S"); s.physicsEnabled = false;
        s.CreateGameObject("T");
        GameObject* cg = s.CreateGameObject("Cam"); cg->AddComponent<Camera>();
        CinemachineBrain* br = cg->AddComponent<CinemachineBrain>(); br->blendTime = 0.0f;
        CameraFollow* f = cg->AddComponent<CameraFollow>(); f->targetName = "T"; f->offset = {0, 0, 50}; f->smoothing = 0.0f;
        GameObject* vg = s.CreateGameObject("Shot"); vg->transform->localPosition = {0, 5, -10};
        VirtualCamera* vc = vg->AddComponent<VirtualCamera>(); vc->follow = ""; vc->lookAt = ""; vc->positionDamping = 0.0f;
        s.Start();
        for (int i = 0; i < 30; ++i) s.Update(1.0f / 60.0f);
        CHECK(br->LiveCamera() == vc);
        CHECK(cg->transform->Position().z < 0.0f);   // at the shot, not at the follow offset (z = 50)
    }
    {   // Camera: a NaN shake or zoom must not poison the camera for good
        Scene s("S"); s.physicsEnabled = false;
        GameObject* g = s.CreateGameObject("Cam"); Camera* c = g->AddComponent<Camera>();
        s.Start();
        c->Shake(kNaN);
        c->Shake(0.8f);
        c->ZoomTo(2.0f, kNaN);
        for (int i = 0; i < 5; ++i) s.Update(1.0f / 60.0f);
        CHECK(std::isfinite(c->ShakeTrauma()));
        CHECK(c->ShakeTrauma() > 0.0f);
        CHECK(std::isfinite(c->zoom));
        c->LateUpdate(kNaN);
        c->ZoomTo(3.0f, 1.0f);
        c->LateUpdate(kNaN);
        CHECK(std::isfinite(c->zoom));
        for (int i = 0; i < 120; ++i) s.Update(1.0f / 60.0f);
        CHECK_NEAR(c->zoom, 3.0f, 0.01f);
        CHECK(Finite3(c->RenderPosition()));
        c->KickFov(kNaN); c->SetFovOffset(kNaN); c->HitPulse(kNaN);
        CHECK(std::isfinite(c->FovOffset()));
        CHECK(std::isfinite(c->VerticalFovDegrees(1.78f)));
    }
    {   // Orbit (FreeLook): a NaN angle, and a minimum pitch above the maximum, still give a finite camera
        Scene s("S"); s.physicsEnabled = false;
        s.CreateGameObject("Player");
        GameObject* cg = s.CreateGameObject("Cam"); cg->AddComponent<Camera>();
        GameObject* vg = s.CreateGameObject("Rig"); VirtualCamera* vc = vg->AddComponent<VirtualCamera>();
        vc->follow = "Player"; vc->freeLook = true; vc->orbitInput = false; vc->collision = false;
        vc->positionDamping = 0.0f; vc->rotationDamping = 0.0f;
        s.Start();
        vc->orbitYaw = kNaN; vc->orbitPitch = kNaN;
        for (int i = 0; i < 5; ++i) s.Update(1.0f / 60.0f);
        CHECK(std::isfinite(vc->orbitYaw)); CHECK(std::isfinite(vc->orbitPitch));
        CHECK(Finite3(vg->transform->Position()));
        vc->orbitMinPitch = 60.0f; vc->orbitMaxPitch = -20.0f; vc->orbitPitch = 10.0f;
        for (int i = 0; i < 5; ++i) s.Update(1.0f / 60.0f);
        CHECK(vc->orbitPitch >= -20.0f - 1e-3f && vc->orbitPitch <= 60.0f + 1e-3f);
        // Yaw kept in -180..180 so long play never loses precision; no jump across 359 -> 0
        vc->orbitMinPitch = -20.0f; vc->orbitMaxPitch = 70.0f; vc->orbitPitch = 15.0f; vc->orbitYaw = 359.0f;
        s.Update(1.0f / 60.0f);
        const Vec3 a = vg->transform->Position();
        vc->orbitYaw = 361.0f; s.Update(1.0f / 60.0f);
        const Vec3 b = vg->transform->Position();
        CHECK((a - b).Magnitude() < 1.0f);
        CHECK(vc->orbitYaw <= 180.0f && vc->orbitYaw >= -180.0f);
    }

    TEST_MAIN_RESULT();
}
