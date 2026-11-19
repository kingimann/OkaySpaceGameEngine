// The camera rig layer over simulated frames (docs/camera_upgrade.md): spring follow, dead and soft zones,
// look ahead, target groups, collision, fading blockers, blends and priority, effects, 2D, saving.
// Every case runs at 30, 60 and 144 frames a second where time matters. No window, no GPU.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <algorithm>
#include <cmath>
#include <functional>
#include <fstream>
#include <sstream>

using namespace okay;

static const float kRates[3] = {30.0f, 60.0f, 144.0f};

namespace {

struct Rig {
    Scene s{"Rig"};
    GameObject* player = nullptr;
    GameObject* camGO = nullptr;
    GameObject* vcGO = nullptr;
    Camera* cam = nullptr;
    CinemachineBrain* brain = nullptr;
    VirtualCamera* vc = nullptr;
    Rig() {
        s.physicsEnabled = false;
        player = s.CreateGameObject("Player");
        camGO = s.CreateGameObject("MainCamera");
        cam = camGO->AddComponent<Camera>();
        brain = camGO->AddComponent<CinemachineBrain>();
        brain->blendTime = 0.0f;
        vcGO = s.CreateGameObject("Rig");
        vc = vcGO->AddComponent<VirtualCamera>();
        vc->follow = "Player";
        vc->lookAt = "Player";
        vc->followOffset = {0, 2, -8};
        s.Start();
    }
    void Step(float dt) { s.Update(dt); }
    Vec3 CamPos() const { return camGO->transform->Position(); }
};

float QDot(const Quat& a, const Quat& b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
bool Finite3(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

} // namespace

int main() {
    RUN_SUITE("camera_rig");

    // ---- 0. The shot really looks at what it is told to look at (it aimed the opposite way before the upgrade) ----
    {
        Rig w;
        w.vc->positionDamping = 0.0f; w.vc->rotationDamping = 0.0f; w.vc->collision = false;
        w.player->transform->localPosition = {3, 1, 2};
        for (int i = 0; i < 5; ++i) w.Step(1.0f / 60.0f);
        Vec2 vp; float depth = 0;
        CHECK(w.cam->WorldToViewportPoint({3, 1, 2}, 1280, 720, vp, &depth));
        CHECK_NEAR(vp.x, 0.5f, 0.01f); CHECK_NEAR(vp.y, 0.5f, 0.01f);
        CHECK(depth > 5.0f);
        // Orbit (FreeLook) keeps the target in the middle too.
        Rig o;
        o.vc->freeLook = true; o.vc->orbitInput = false; o.vc->orbitYaw = 40.0f; o.vc->orbitPitch = 20.0f; o.vc->orbitHeight = 0.0f;
        o.vc->positionDamping = 0.0f; o.vc->rotationDamping = 0.0f; o.vc->collision = false;
        for (int i = 0; i < 5; ++i) o.Step(1.0f / 60.0f);
        CHECK(o.cam->WorldToViewportPoint({0, 0, 0}, 1280, 720, vp, &depth));
        CHECK_NEAR(vp.x, 0.5f, 0.01f); CHECK_NEAR(vp.y, 0.5f, 0.01f);
    }

    // ---- 1. Spring follow: the same place at 30, 60 and 144 fps, smooth, no overshoot ----
    {
        Vec3 at[3];
        for (int r = 0; r < 3; ++r) {
            Rig w;
            w.vc->dampingMode = VirtualCamera::DampingMode::Spring;
            w.vc->positionDamping = 4.0f;
            w.vc->collision = false;
            const float dt = 1.0f / kRates[r];
            for (int i = 0; i < 4; ++i) w.Step(dt);
            w.player->transform->localPosition = {10, 0, 0};      // the target jumps; the camera must glide
            float prevX = w.CamPos().x, prevStep = 0, maxX = prevX, maxJerk = 0;
            const int n = (int)std::round(kRates[r] * 2.0f);
            for (int i = 0; i < n; ++i) {
                w.Step(dt);
                const Vec3 p = w.CamPos();
                CHECK(Finite3(p));
                const float step = p.x - prevX; prevX = p.x;
                if (i > 1) maxJerk = std::max(maxJerk, std::fabs(step - prevStep) / dt);
                prevStep = step; maxX = std::max(maxX, p.x);
            }
            at[r] = w.CamPos();
            CHECK(maxX <= 10.001f);             // never past the target: no overshoot
            CHECK(maxJerk < 60.0f);             // gentle at every rate
        }
        CHECK((at[0] - at[2]).Magnitude() < 0.05f);
        CHECK((at[1] - at[2]).Magnitude() < 0.05f);
        CHECK(at[2].x > 8.0f && at[2].x < 10.0f);
    }

    // ---- 2. Follow a walking target: no jitter even when the target moves in 50 Hz steps ----
    for (int r = 0; r < 3; ++r) {
        Rig w;
        w.vc->dampingMode = VirtualCamera::DampingMode::Spring;
        w.vc->positionDamping = 6.0f;
        w.vc->collision = false;
        const float dt = 1.0f / kRates[r];
        float prev = 0, prevStep = 0, maxJerk = 0; bool have = false;
        const int n = (int)(kRates[r] * 5.0f);
        for (int i = 0; i < n; ++i) {
            const float t = i * dt;
            w.player->transform->localPosition = {std::floor(t * 50.0f) / 50.0f * 4.0f, 0, 0};   // a body moved by physics at 50 Hz
            w.Step(dt);
            const float x = w.CamPos().x;
            if (have && t > 2.0f) { const float step = x - prev; maxJerk = std::max(maxJerk, std::fabs(step - prevStep) / dt); prevStep = step; }
            else if (have) prevStep = x - prev;
            prev = x; have = true;
        }
        CHECK(maxJerk < 30.0f);   // m/s^2: a 4 m/s target stepped at 50 Hz still gives a smooth camera
        CHECK(w.CamPos().x > 12.0f);
    }

    // ---- 3. Dead zone and soft zone (aim) ----
    {
        Rig w;
        w.vc->useZones = true; w.vc->deadZoneW = 0.3f; w.vc->deadZoneH = 0.3f; w.vc->softZoneW = 0.8f; w.vc->softZoneH = 0.8f;
        w.vc->positionDamping = 0.0f; w.vc->collision = false; w.vc->followOffset = {0, 0, -10};
        w.vc->follow = ""; w.vc->lookAt = "Player";
        w.vcGO->transform->localPosition = {0, 0, -10};
        for (int i = 0; i < 5; ++i) w.Step(1.0f / 60.0f);
        const Quat before = w.vc->SolvedRotation();
        w.player->transform->localPosition = {0.5f, 0.3f, 0};          // well inside the dead zone
        for (int i = 0; i < 30; ++i) w.Step(1.0f / 60.0f);
        const Quat after = w.vc->SolvedRotation();
        CHECK(std::fabs(QDot(before, after)) > 0.99999f);          // the camera did not turn at all
        // Far to the side: past the soft zone the camera must keep the target on screen.
        w.player->transform->localPosition = {9.0f, 0, 0};
        for (int i = 0; i < 90; ++i) w.Step(1.0f / 60.0f);
        Vec2 vp; float depth = 0;
        CHECK(w.cam->WorldToViewportPoint({9.0f, 0, 0}, 1280, 720, vp, &depth));
        CHECK(vp.x > 0.0f && vp.x < 1.0f);
        CHECK(std::fabs(vp.x - 0.5f) <= 0.5f * 0.8f + 0.03f);          // inside the soft zone's edge
    }

    // ---- 4. Look ahead: the camera leans into the direction of travel, and not for a teleport ----
    {
        Rig w;
        w.vc->dampingMode = VirtualCamera::DampingMode::Spring; w.vc->positionDamping = 8.0f; w.vc->collision = false;
        w.vc->lookaheadTime = 0.5f; w.vc->lookaheadMax = 3.0f; w.vc->lookaheadSmooth = 6.0f;
        w.vc->lookAt = "";
        const float dt = 1.0f / 60.0f;
        for (int i = 0; i < 240; ++i) { w.player->transform->localPosition = {i * dt * 4.0f, 0, 0}; w.Step(dt); }
        const float lead = w.CamPos().x - w.player->transform->localPosition.x;
        CHECK(lead > 1.0f);                                             // ahead of the player (it also lags a little)
        CHECK(w.vc->LookaheadOffset().x > 1.5f && w.vc->LookaheadOffset().x <= 3.001f);
        w.player->transform->localPosition = {500.0f, 0, 0};            // a teleport must not throw the view
        w.Step(dt);
        CHECK(w.vc->LookaheadOffset().Magnitude() < 0.5f);
    }

    // ---- 5. Target group: everything stays in view, and the camera steps back to fit ----
    {
        Rig w;
        GameObject* a = w.s.CreateGameObject("A"); a->transform->localPosition = {-3, 0, 0};
        GameObject* b = w.s.CreateGameObject("B"); b->transform->localPosition = {3, 0, 0};
        w.vc->follow = ""; w.vc->lookAt = ""; w.vc->aimAtFollow = true;
        w.vc->groupTargets = {"A", "B"}; w.vc->groupPadding = 1.0f; w.vc->fieldOfView = 60.0f;
        w.vc->followOffset = {0, 1, -4}; w.vc->positionDamping = 0.0f; w.vc->collision = false;
        w.cam->fieldOfView = 60.0f;
        for (int i = 0; i < 5; ++i) w.Step(1.0f / 60.0f);
        const float d1 = (w.CamPos() - Vec3{0, 0, 0}).Magnitude();
        a->transform->localPosition = {-20, 0, 5}; b->transform->localPosition = {22, 0, -3};
        for (int i = 0; i < 5; ++i) w.Step(1.0f / 60.0f);
        const float d2 = (w.CamPos() - Vec3{1, 0, 1}).Magnitude();
        CHECK(d2 > d1 * 2.0f);                                          // stepped back
        CHECK(w.cam->IsPointVisible(a->transform->Position(), 1280, 720));
        CHECK(w.cam->IsPointVisible(b->transform->Position(), 1280, 720));
        w.vc->RemoveGroupTarget("B");                                   // the API for scripts
        CHECK(w.vc->groupTargets.size() == 1);
        w.vc->AddGroupTarget("B"); w.vc->AddGroupTarget("B");
        CHECK(w.vc->groupTargets.size() == 2);
    }

    // ---- 6. Collision: a wall pulls the camera in; glides back out; triggers and ignored layers do not ----
    {
        auto build = [](Rig& w, GameObject*& wall) {
            w.vc->collision = true; w.vc->dampingMode = VirtualCamera::DampingMode::Spring; w.vc->positionDamping = 50.0f;
            w.vc->followOffset = {0, 0, -8}; w.vc->lookAt = "Player"; w.vc->collisionRadius = 0.2f; w.vc->collisionRecover = 3.0f;
            wall = w.s.CreateGameObject("Wall");
            wall->transform->localPosition = {0, 0, -4};
            wall->AddComponent<BoxCollider3D>()->size = {10, 10, 0.5f};
        };
        Rig w; GameObject* wall = nullptr; build(w, wall);
        for (int i = 0; i < 10; ++i) w.Step(1.0f / 60.0f);
        CHECK(w.CamPos().z > -4.0f + 0.2f);                             // in front of the wall's near face (z = -3.75)
        CHECK(w.CamPos().z < -1.0f);
        wall->GetComponent<BoxCollider3D>()->enabled = false;           // the wall goes: glide back out, in small steps
        float prevZ = w.CamPos().z, maxStep = 0; bool monotonic = true;
        for (int i = 0; i < 240; ++i) {
            w.Step(1.0f / 60.0f);
            const float z = w.CamPos().z;
            maxStep = std::max(maxStep, std::fabs(z - prevZ));
            if (z > prevZ + 1e-4f) monotonic = false;   // moving out = z getting more negative
            prevZ = z;
        }
        CHECK(monotonic);
        CHECK(maxStep < 0.25f);                                         // a glide (at most about 0.2 a frame), no pop
        CHECK(w.CamPos().z < -7.0f);                                    // back out at its full distance
        {   // a trigger never blocks
            Rig t; GameObject* tw = nullptr; build(t, tw);
            tw->GetComponent<BoxCollider3D>()->isTrigger = true;
            for (int i = 0; i < 10; ++i) t.Step(1.0f / 60.0f);
            CHECK(t.CamPos().z < -7.5f);
        }
        {   // a layer the mask leaves out does not block
            Rig m; GameObject* mw = nullptr; build(m, mw);
            mw->GetComponent<BoxCollider3D>()->layer = 5;
            m.vc->collisionMask = ~(1u << 5);
            for (int i = 0; i < 10; ++i) m.Step(1.0f / 60.0f);
            CHECK(m.CamPos().z < -7.5f);
        }
        {   // the same distance at every frame rate
            float zs[3];
            for (int r = 0; r < 3; ++r) {
                Rig q; GameObject* qw = nullptr; build(q, qw);
                for (int i = 0; i < (int)kRates[r]; ++i) q.Step(1.0f / kRates[r]);
                zs[r] = q.CamPos().z;
            }
            CHECK_NEAR(zs[0], zs[2], 0.03f); CHECK_NEAR(zs[1], zs[2], 0.03f);
        }
    }

    // ---- 7. Fading blockers: see-through while in the way, back to normal after ----
    {
        Rig w;
        w.vc->collision = false; w.vc->fadeOccluders = true; w.vc->occlusionAlpha = 0.25f; w.vc->occlusionFadeSpeed = 10.0f;
        w.vc->positionDamping = 0.0f; w.vc->followOffset = {0, 0, -8};
        GameObject* wall = w.s.CreateGameObject("Wall");
        wall->transform->localPosition = {0, 0, -4};
        wall->AddComponent<BoxCollider3D>()->size = {6, 6, 0.5f};
        auto* mr = wall->AddComponent<MeshRenderer>();
        mr->color = Color{1, 1, 1, 1};
        for (int i = 0; i < 60; ++i) w.Step(1.0f / 60.0f);
        CHECK(mr->color.a < 0.3f && mr->color.a >= 0.25f - 1e-3f);
        wall->transform->localPosition = {40, 0, -4};                   // out of the way
        for (int i = 0; i < 120; ++i) w.Step(1.0f / 60.0f);
        CHECK_NEAR(mr->color.a, 1.0f, 1e-4f);
    }

    // ---- 8. Blends and priority ----
    {
        auto make = [](Rig& w, GameObject*& b, VirtualCamera*& vb) {
            w.vc->follow = ""; w.vc->lookAt = ""; w.vc->positionDamping = 0.0f; w.vc->collision = false;
            w.vcGO->transform->localPosition = {0, 0, 0};
            w.vc->priority = 10;
            b = w.s.CreateGameObject("ShotB");
            vb = b->AddComponent<VirtualCamera>();
            vb->follow = ""; vb->lookAt = ""; vb->positionDamping = 0.0f; vb->collision = false;
            vb->priority = 5; vb->fieldOfView = 60.0f;
            b->transform->localPosition = {10, 0, 0};
        };
        // Cut: all at once.
        {
            Rig w; GameObject* b; VirtualCamera* vb; make(w, b, vb);
            w.brain->blendTime = 1.0f; w.brain->blendStyle = cam::BlendStyle::Cut;
            for (int i = 0; i < 5; ++i) w.Step(1.0f / 60.0f);
            CHECK_NEAR(w.CamPos().x, 0.0f, 0.01f);
            vb->priority = 50;
            w.Step(1.0f / 60.0f);
            CHECK_NEAR(w.CamPos().x, 10.0f, 0.01f);
        }
        // Linear and ease styles at the halfway mark.
        const cam::BlendStyle styles[4] = {cam::BlendStyle::Linear, cam::BlendStyle::EaseInOut, cam::BlendStyle::EaseIn, cam::BlendStyle::EaseOut};
        float mid[4];
        for (int k = 0; k < 4; ++k) {
            Rig w; GameObject* b; VirtualCamera* vb; make(w, b, vb);
            w.brain->blendTime = 1.0f; w.brain->blendStyle = styles[k];
            for (int i = 0; i < 5; ++i) w.Step(1.0f / 60.0f);
            vb->priority = 50;
            for (int i = 0; i < 30; ++i) w.Step(1.0f / 60.0f);          // 0.5 s into a 1 s blend
            mid[k] = w.CamPos().x;
            for (int i = 0; i < 40; ++i) w.Step(1.0f / 60.0f);
            CHECK_NEAR(w.CamPos().x, 10.0f, 0.01f);                      // arrives exactly
            CHECK(!w.brain->IsBlending());
        }
        CHECK_NEAR(mid[0], 5.0f, 0.4f);        // linear: halfway
        CHECK_NEAR(mid[1], 5.0f, 0.4f);        // ease in and out is symmetric
        CHECK(mid[2] < mid[0]);                // ease in: slow start
        CHECK(mid[3] > mid[0]);                // ease out: fast start
        // Custom curve.
        {
            Rig w; GameObject* b; VirtualCamera* vb; make(w, b, vb);
            w.brain->blendTime = 1.0f; w.brain->blendStyle = cam::BlendStyle::Custom;
            w.brain->customCurve[0] = 0.0f; w.brain->customCurve[1] = 1.0f; w.brain->customCurve[2] = 0.2f; w.brain->customCurve[3] = 1.0f;   // very fast start
            for (int i = 0; i < 5; ++i) w.Step(1.0f / 60.0f);
            vb->priority = 50;
            for (int i = 0; i < 12; ++i) w.Step(1.0f / 60.0f);
            CHECK(w.CamPos().x > 6.0f);
        }
        // Same blend at every frame rate.
        {
            float xs[3];
            for (int r = 0; r < 3; ++r) {
                Rig w; GameObject* b; VirtualCamera* vb; make(w, b, vb);
                w.brain->blendTime = 1.0f; w.brain->blendStyle = cam::BlendStyle::EaseInOut;
                for (int i = 0; i < 5; ++i) w.Step(1.0f / kRates[r]);
                vb->priority = 50;
                for (int i = 0; i < (int)std::round(kRates[r] * 0.4f); ++i) w.Step(1.0f / kRates[r]);
                xs[r] = w.CamPos().x;
            }
            CHECK_NEAR(xs[0], xs[2], 0.25f); CHECK_NEAR(xs[1], xs[2], 0.25f);
        }
        // A shot with its own time and style; interrupting a blend never pops.
        {
            Rig w; GameObject* b; VirtualCamera* vb; make(w, b, vb);
            GameObject* c = w.s.CreateGameObject("ShotC");
            auto* vcC = c->AddComponent<VirtualCamera>();
            vcC->follow = ""; vcC->lookAt = ""; vcC->positionDamping = 0.0f; vcC->collision = false; vcC->priority = 1;
            c->transform->localPosition = {0, 20, 0};
            w.brain->blendTime = 2.0f; w.brain->blendStyle = cam::BlendStyle::Linear;
            for (int i = 0; i < 5; ++i) w.Step(1.0f / 60.0f);
            vb->priority = 50;                                           // blend toward B (2 s)
            for (int i = 0; i < 40; ++i) w.Step(1.0f / 60.0f);
            Vec3 prev = w.CamPos(); float maxStep = 0;
            vb->priority = 0; vcC->priority = 99;                        // change our mind half way: now toward C
            for (int i = 0; i < 200; ++i) {
                w.Step(1.0f / 60.0f);
                maxStep = std::max(maxStep, (w.CamPos() - prev).Magnitude()); prev = w.CamPos();
                CHECK(Finite3(prev));
            }
            CHECK(maxStep < 0.5f);                                       // never a jump (a 20 unit blend in 2 s moves 0.17 a frame)
            CHECK_NEAR(w.CamPos().y, 20.0f, 0.01f);
        }
        {   // blendIn: this shot blends quickly even though the brain is slow
            Rig w; GameObject* b; VirtualCamera* vb; make(w, b, vb);
            w.brain->blendTime = 5.0f; w.brain->blendStyle = cam::BlendStyle::Linear;
            vb->blendIn = 0.0f;                                          // a cut into B
            for (int i = 0; i < 5; ++i) w.Step(1.0f / 60.0f);
            vb->priority = 50;
            w.Step(1.0f / 60.0f);
            CHECK_NEAR(w.CamPos().x, 10.0f, 0.01f);
        }
        {   // ForceLive (what Camera.BlendTo does), then release
            Rig w; GameObject* b; VirtualCamera* vb; make(w, b, vb);
            w.brain->blendTime = 0.0f;
            for (int i = 0; i < 3; ++i) w.Step(1.0f / 60.0f);
            w.brain->ForceLive(vb, 0.0f);
            w.Step(1.0f / 60.0f);
            CHECK_NEAR(w.CamPos().x, 10.0f, 0.01f);
            CHECK(w.brain->LiveCamera() == vb);
            w.brain->ReleaseForced();
            w.Step(1.0f / 60.0f);
            CHECK_NEAR(w.CamPos().x, 0.0f, 0.01f);
        }
    }

    // ---- 9. Effects on the camera ----
    {
        // Shake presets: they move the view, then settle to exactly where it was.
        for (int p = 0; p < 3; ++p) {
            Scene s("fx"); s.physicsEnabled = false;
            GameObject* g = s.CreateGameObject("Cam"); Camera* c = g->AddComponent<Camera>();
            s.Start();
            const Vec3 home = c->RenderPosition();
            c->ShakePreset((cam::ShakePreset)p, 1.0f);
            float peak = 0;
            for (int i = 0; i < 300; ++i) { s.Update(1.0f / 60.0f); peak = std::max(peak, (c->RenderPosition() - home).Magnitude()); CHECK(Finite3(c->RenderPosition())); }
            CHECK(peak > 0.0f);
            CHECK(!c->IsShaking());
            CHECK((c->RenderPosition() - home).Magnitude() < 1e-6f);
        }
        {   // trauma shake and a preset together; the player's reduce-motion setting stops both
            Scene s("fx2"); s.physicsEnabled = false;
            GameObject* g = s.CreateGameObject("Cam"); Camera* c = g->AddComponent<Camera>();
            s.Start();
            Accessibility::SetReduceMotion(true);
            c->Shake(1.0f); c->ShakePreset(cam::ShakePreset::Explosion, 1.0f); c->KickFov(10.0f); c->SetHandheld(1.0f);
            for (int i = 0; i < 30; ++i) s.Update(1.0f / 60.0f);
            CHECK(!c->IsShaking());
            CHECK_NEAR(c->FovOffset(), 0.0f, 1e-6f);
            Accessibility::SetReduceMotion(false);
        }
        // Field-of-view kick: widens smoothly, peaks near the size asked for, settles back.
        for (int r = 0; r < 3; ++r) {
            Scene s("fov"); s.physicsEnabled = false;
            GameObject* g = s.CreateGameObject("Cam"); Camera* c = g->AddComponent<Camera>();
            c->fieldOfView = 60.0f;
            s.Start();
            const float dt = 1.0f / kRates[r];
            c->KickFov(8.0f, 0.5f);
            float peak = 0, prev = c->VerticalFovDegrees(1.78f), maxJump = 0;
            for (int i = 0; i < (int)(kRates[r] * 1.0f); ++i) {
                s.Update(dt);
                const float f = c->VerticalFovDegrees(1.78f);
                peak = std::max(peak, f - 60.0f); maxJump = std::max(maxJump, std::fabs(f - prev)); prev = f;
            }
            CHECK_NEAR(peak, 8.0f, 0.4f);
            if (r > 0) CHECK(maxJump < 0.6f * 8.0f);                      // at 60 and 144 fps no frame covers most of the rise
            CHECK(std::fabs(c->FovOffset()) < 0.5f);                     // back to normal after the time is up
        }
        {   // sprint FOV: held, eases in and out
            Scene s("sprint"); s.physicsEnabled = false;
            GameObject* g = s.CreateGameObject("Cam"); Camera* c = g->AddComponent<Camera>();
            s.Start();
            c->SetFovOffset(10.0f, 0.3f);
            for (int i = 0; i < 90; ++i) s.Update(1.0f / 60.0f);
            CHECK_NEAR(c->FovOffset(), 10.0f, 0.05f);
            c->SetFovOffset(0.0f, 0.3f);
            for (int i = 0; i < 90; ++i) s.Update(1.0f / 60.0f);
            CHECK_NEAR(c->FovOffset(), 0.0f, 0.05f);
        }
        {   // hit pulse: darkens the edges on top of the scene's vignette, then leaves it exactly as it was
            Scene s("pulse"); s.physicsEnabled = false;
            s.renderSettings.vignette = 0.2f;
            GameObject* g = s.CreateGameObject("Cam"); Camera* c = g->AddComponent<Camera>();
            s.Start();
            c->HitPulse(1.0f);
            s.Update(1.0f / 60.0f);
            CHECK(s.renderSettings.vignette > 0.4f);
            CHECK(c->VignettePulse() > 0.9f);
            for (int i = 0; i < 300; ++i) s.Update(1.0f / 60.0f);
            CHECK_NEAR(s.renderSettings.vignette, 0.2f, 1e-4f);
            CHECK_NEAR(c->VignettePulse(), 0.0f, 1e-6f);
        }
    }

    // ---- 10. 2D: zones, confine to the view, rooms, pixel snapping ----
    {
        Scene s("2d"); s.physicsEnabled = false;
        GameObject* player = s.CreateGameObject("Hero");
        GameObject* camGO = s.CreateGameObject("MainCamera");
        Camera* cam = camGO->AddComponent<Camera>();
        cam->projection = Camera::Projection::Orthographic; cam->orthographicSize = 5.0f;
        auto* brain = camGO->AddComponent<CinemachineBrain>(); brain->blendTime = 0.0f;
        GameObject* vg = s.CreateGameObject("Rig2D");
        auto* vc = vg->AddComponent<VirtualCamera>();
        vc->follow = "Hero"; vc->ortho2D = true; vc->followOffset = {0, 0, -10}; vc->orthoSize = 5.0f;
        vc->useZones = true; vc->deadZoneW = 0.2f; vc->deadZoneH = 0.2f; vc->softZoneW = 0.8f; vc->softZoneH = 0.8f; vc->zoneDamping = 6.0f;
        vc->confine = true; vc->confineToView = true; vc->confineMin = {0, 0, -1000}; vc->confineMax = {60, 30, 1000};
        s.Start();
        for (int i = 0; i < 3; ++i) s.Update(1.0f / 60.0f);
        const float aspect = cam->viewportAspect;
        // Inside the dead zone: the camera does not move.
        const Vec3 c0 = camGO->transform->Position();
        player->transform->localPosition = {c0.x + 0.5f, c0.y, 0};
        for (int i = 0; i < 30; ++i) s.Update(1.0f / 60.0f);
        CHECK_NEAR(camGO->transform->Position().x, c0.x, 1e-4f);
        // Run right and far: the picture's edges never leave the confine box, at every frame rate; the camera never turns.
        for (int r = 0; r < 3; ++r) {
            player->transform->localPosition = {30, 15, 0};
            s.Update(0.5f);   // settle
            const float dt = 1.0f / kRates[r];
            float maxStep = 0; Vec3 prev = camGO->transform->Position();
            for (int i = 0; i < (int)(kRates[r] * 12.0f); ++i) {
                player->transform->localPosition = {30.0f + i * dt * 6.0f, 15.0f + std::sin(i * dt) * 8.0f, 0};
                s.Update(dt);
                const Vec3 p = camGO->transform->Position();
                CHECK(Finite3(p));
                CHECK(p.x - 5.0f * aspect >= -1e-3f && p.x + 5.0f * aspect <= 60.0f + 1e-3f);
                CHECK(p.y - 5.0f >= -1e-3f && p.y + 5.0f <= 30.0f + 1e-3f);
                maxStep = std::max(maxStep, (p - prev).Magnitude()); prev = p;
            }
            CHECK(maxStep < 0.6f);   // 6 m/s target: the camera never jumps
            CHECK_NEAR(camGO->transform->Rotation().w, 1.0f, 1e-4f);
        }
    }
    {   // Rooms: the view is held in the room and slides to the next
        Scene s("rooms"); s.physicsEnabled = false;
        GameObject* hero = s.CreateGameObject("Hero");
        GameObject* camGO = s.CreateGameObject("MainCamera");
        Camera* cam = camGO->AddComponent<Camera>();
        cam->projection = Camera::Projection::Orthographic; cam->orthographicSize = 4.0f;
        auto* brain = camGO->AddComponent<CinemachineBrain>(); brain->blendTime = 0.0f;
        GameObject* r1 = s.CreateGameObject("RoomA"); r1->transform->localPosition = {0, 0, 0};
        r1->AddComponent<CameraRoom>()->size = {32, 18};
        GameObject* r2 = s.CreateGameObject("RoomB"); r2->transform->localPosition = {32, 0, 0};
        r2->AddComponent<CameraRoom>()->size = {32, 18};
        GameObject* vg = s.CreateGameObject("Rig2D");
        auto* vc = vg->AddComponent<VirtualCamera>();
        vc->follow = "Hero"; vc->ortho2D = true; vc->followOffset = {0, 0, -10}; vc->orthoSize = 4.0f; vc->positionDamping = 0.0f;
        vc->useRooms = true; vc->roomBlendTime = 0.6f;
        s.Start();
        for (int r = 0; r < 3; ++r) {
            hero->transform->localPosition = {0, 0, 0};
            for (int i = 0; i < 120; ++i) s.Update(1.0f / 60.0f);
            const float dt = 1.0f / kRates[r];
            float aspect = cam->viewportAspect, prevX = camGO->transform->Position().x, maxStep = 0;
            for (int i = 0; i < (int)(kRates[r] * 5.0f); ++i) {
                hero->transform->localPosition = {std::min(34.0f, i * dt * 8.0f), 0, 0};   // walk to the room edge and beyond it
                s.Update(dt);
                const float x = camGO->transform->Position().x;
                maxStep = std::max(maxStep, std::fabs(x - prevX)); prevX = x;
                CHECK(std::isfinite(x));
            }
            // Settled in room B: the view's right edge may not pass the room's right edge (48), and it is in B.
            const float x = camGO->transform->Position().x;
            CHECK(x + 4.0f * aspect <= 48.0f + 0.01f);
            CHECK(x - 4.0f * aspect >= 16.0f - 0.01f);
            CHECK(maxStep * kRates[r] < 200.0f);   // a slide (32 units in about 0.6 s: about 100 units a second at its fastest), never a snap
        }
    }
    {   // Pixel snapping: a smooth camera move lands on whole pixels and never jumps by more than one
        Scene s("px"); s.physicsEnabled = false;
        GameObject* g = s.CreateGameObject("Cam"); Camera* c = g->AddComponent<Camera>();
        c->projection = Camera::Projection::Orthographic; c->pixelPerfect = true; c->pixelsPerUnit = 16.0f;
        s.Start();
        float prev = c->RenderPosition().x; bool ok = true;
        for (int i = 0; i < 600; ++i) {
            g->transform->localPosition = {i * 0.0137f, 0, -10};
            s.Update(1.0f / 144.0f);
            const float x = c->RenderPosition().x;
            const float units = x * 16.0f;
            if (std::fabs(units - std::round(units)) > 1e-3f) ok = false;      // on the pixel grid
            if (x < prev - 1e-5f || x - prev > 1.0f / 16.0f + 1e-4f) ok = false; // never back, never more than a pixel
            prev = x;
        }
        CHECK(ok);
    }

    // ---- 11. Bad input never gives NaN ----
    {
        Rig w;
        w.vc->dampingMode = VirtualCamera::DampingMode::Spring; w.vc->useZones = true; w.vc->lookaheadTime = 0.5f; w.vc->collision = true;
        for (int i = 0; i < 10; ++i) w.Step(1.0f / 60.0f);
        const Vec3 good = w.CamPos();
        w.player->transform->localPosition = {NAN, 0, 0};
        for (int i = 0; i < 5; ++i) { w.Step(1.0f / 60.0f); CHECK(Finite3(w.CamPos())); }
        w.player->transform->localPosition = {0, 0, 0};
        for (int i = 0; i < 60; ++i) { w.Step(1.0f / 60.0f); CHECK(Finite3(w.CamPos())); }
        CHECK(Finite3(good));
        w.Step(0.0f); CHECK(Finite3(w.CamPos()));           // a paused frame
        w.Step(5.0f); CHECK(Finite3(w.CamPos()));           // a five second hitch
        w.vc->followOffset = {0, 0, 0};                    // camera exactly on its target
        for (int i = 0; i < 5; ++i) { w.Step(1.0f / 60.0f); CHECK(Finite3(w.CamPos())); }
    }

    // ---- 12. Saving and loading: every rig setting survives, old scenes still load ----
    {
        Scene a("save");
        GameObject* cg = a.CreateGameObject("MainCamera");
        auto* cam = cg->AddComponent<Camera>(); cam->hitPulseVignette = 0.35f;
        auto* br = cg->AddComponent<CinemachineBrain>();
        br->blendTime = 2.5f; br->blendStyle = cam::BlendStyle::Custom; br->customCurve[0] = 0.1f; br->customCurve[1] = 0.7f;
        br->customCurve[2] = 0.3f; br->customCurve[3] = 0.9f; br->yieldToCutscene = false;
        GameObject* vg = a.CreateGameObject("Shot");
        auto* vc = vg->AddComponent<VirtualCamera>();
        vc->follow = "Hero"; vc->dampingMode = VirtualCamera::DampingMode::Spring; vc->groupTargets = {"A", "B Boss"}; vc->groupPadding = 2.5f;
        vc->groupFrame = false; vc->lookaheadTime = 0.3f; vc->lookaheadMax = 1.5f; vc->lookaheadSmooth = 2.0f;
        vc->useZones = true; vc->deadZoneW = 0.11f; vc->deadZoneH = 0.12f; vc->softZoneW = 0.61f; vc->softZoneH = 0.62f; vc->zoneDamping = 3.0f;
        vc->ortho2D = true; vc->orthoSize = 7.0f; vc->confineToView = true; vc->useRooms = true; vc->roomBlendTime = 0.9f;
        vc->collision = true; vc->collisionRadius = 0.31f; vc->collisionSkin = 0.07f; vc->collisionRecover = 2.0f; vc->collisionPullIn = 9.0f;
        vc->collisionSnapIn = false; vc->collisionMask = 0xF0F0u; vc->fadeOccluders = true; vc->occlusionAlpha = 0.4f; vc->occlusionFadeSpeed = 5.0f;
        vc->blendIn = 0.75f; vc->blendInStyle = 3; vc->aimAtFollow = true;
        GameObject* rg = a.CreateGameObject("Room"); rg->AddComponent<CameraRoom>()->size = {21, 13};
        const std::string text = SceneSerializer::Serialize(a);
        Scene b("load"); std::string err;
        CHECK(SceneSerializer::Deserialize(b, text, &err, nullptr));
        auto* cam2 = b.Find("MainCamera")->GetComponent<Camera>();
        auto* br2 = b.Find("MainCamera")->GetComponent<CinemachineBrain>();
        auto* vc2 = b.Find("Shot")->GetComponent<VirtualCamera>();
        auto* rm2 = b.Find("Room")->GetComponent<CameraRoom>();
        CHECK(cam2 && br2 && vc2 && rm2);
        if (cam2 && br2 && vc2 && rm2) {
            CHECK_NEAR(cam2->hitPulseVignette, 0.35f, 1e-6f);
            CHECK(br2->blendStyle == cam::BlendStyle::Custom && !br2->yieldToCutscene);
            CHECK_NEAR(br2->blendTime, 2.5f, 1e-6f); CHECK_NEAR(br2->customCurve[1], 0.7f, 1e-6f); CHECK_NEAR(br2->customCurve[3], 0.9f, 1e-6f);
            CHECK(vc2->dampingMode == VirtualCamera::DampingMode::Spring);
            CHECK(vc2->groupTargets.size() == 2 && vc2->groupTargets[1] == "B Boss");
            CHECK_NEAR(vc2->groupPadding, 2.5f, 1e-6f); CHECK(!vc2->groupFrame);
            CHECK_NEAR(vc2->lookaheadTime, 0.3f, 1e-6f); CHECK_NEAR(vc2->lookaheadMax, 1.5f, 1e-6f); CHECK_NEAR(vc2->lookaheadSmooth, 2.0f, 1e-6f);
            CHECK(vc2->useZones); CHECK_NEAR(vc2->deadZoneW, 0.11f, 1e-6f); CHECK_NEAR(vc2->softZoneH, 0.62f, 1e-6f); CHECK_NEAR(vc2->zoneDamping, 3.0f, 1e-6f);
            CHECK(vc2->ortho2D && vc2->confineToView && vc2->useRooms); CHECK_NEAR(vc2->orthoSize, 7.0f, 1e-6f); CHECK_NEAR(vc2->roomBlendTime, 0.9f, 1e-6f);
            CHECK(vc2->collision && !vc2->collisionSnapIn); CHECK_NEAR(vc2->collisionRadius, 0.31f, 1e-6f); CHECK(vc2->collisionMask == 0xF0F0u);
            CHECK(vc2->fadeOccluders); CHECK_NEAR(vc2->occlusionAlpha, 0.4f, 1e-6f);
            CHECK_NEAR(vc2->blendIn, 0.75f, 1e-6f); CHECK(vc2->blendInStyle == 3); CHECK(vc2->aimAtFollow);
            CHECK_NEAR(rm2->size.x, 21.0f, 1e-6f); CHECK_NEAR(rm2->size.y, 13.0f, 1e-6f);
        }
        CHECK(SceneSerializer::Serialize(b) == text);       // saving it again gives the same text
        // An old file (no rig lines) still loads, with the old behaviour (classic damping, nothing new on).
        std::string oldText;
        {
            std::istringstream in(text); std::string line;
            while (std::getline(in, line))
                if (line.rfind("  vcamx", 0) != 0 && line.rfind("  cmbrainx", 0) != 0 && line.rfind("  camerax", 0) != 0 && line.rfind("  camroom", 0) != 0)
                    oldText += line + "\n";
        }
        Scene o("old"); std::string e2;
        const bool loaded = SceneSerializer::Deserialize(o, oldText, &e2, nullptr);
        CHECK(loaded);
        if (loaded) {
            auto* ob = o.Find("MainCamera")->GetComponent<CinemachineBrain>();
            auto* ov = o.Find("Shot")->GetComponent<VirtualCamera>();
            CHECK(ob && ob->blendStyle == cam::BlendStyle::EaseInOut && ob->yieldToCutscene);   // the old "ease" switch on = ease in and out
            CHECK(ov && ov->dampingMode == VirtualCamera::DampingMode::Smooth && !ov->useZones && !ov->collision && ov->groupTargets.empty());
        }
    }

    // ---- 13. ApplyStyle: the three "Camera Rig" styles give a working shot ----
    for (int style = 0; style < 3; ++style) {
        Rig w;
        w.vc->ApplyStyle(style);
        w.vc->orbitInput = false;
        for (int i = 0; i < 120; ++i) { w.player->transform->localPosition = {i * 0.03f, 0, 0}; w.Step(1.0f / 60.0f); CHECK(Finite3(w.CamPos())); }
        CHECK(w.vc->dampingMode == VirtualCamera::DampingMode::Spring);
        CHECK(w.cam->IsPointVisible(w.player->transform->Position(), 1280, 720));
    }

    // ---- 14. Script builtins: Camera.Shake / BlendTo / SetTarget / groups / FOV / hit pulse ----
    {
        Scene s("script"); s.physicsEnabled = false;
        GameObject* camGO = s.CreateGameObject("MainCamera");
        Camera* cam = camGO->AddComponent<Camera>();
        auto* brain = camGO->AddComponent<CinemachineBrain>(); brain->blendTime = 0.0f;
        GameObject* a = s.CreateGameObject("ShotA"); auto* va = a->AddComponent<VirtualCamera>(); va->priority = 10; va->positionDamping = 0.0f;
        GameObject* b = s.CreateGameObject("ShotB"); auto* vb = b->AddComponent<VirtualCamera>(); vb->priority = 1; vb->positionDamping = 0.0f;
        b->transform->localPosition = {7, 0, 0};
        s.CreateGameObject("Boss"); s.CreateGameObject("Minion");
        GameObject* d = s.CreateGameObject("Director");
        auto* sc = d->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "function start() {\n"
            "  Camera.Shake(\"explosion\", 1)\n"
            "  Camera.BlendTo(\"ShotB\", 0)\n"
            "  Camera.SetTarget(\"Boss\")\n"
            "  Camera.AddToGroup(\"Minion\")\n"
            "  Camera.AddToGroup(\"Minion\")\n"
            "  Camera.FovKick(8, 0.5)\n"
            "  Camera.SetFovOffset(5, 0.2)\n"
            "  Camera.HitPulse(1)\n"
            "  Camera.SetHandheld(1)\n"
            "  Camera.Shake(\"nonsense\", 1)\n"
            "}\n"
            "var n = 0\n"
            "function update(dt) {\n"
            "  n = n + 1\n"
            "  if (n == 6) { Camera.BlendTo(\"\") }\n"
            "}\n"));
        s.Start();
        for (int i = 0; i < 4; ++i) s.Update(1.0f / 60.0f);
        CHECK(cam->IsShaking());
        CHECK(brain->Forced() == vb);
        CHECK(brain->LiveCamera() == vb);
        CHECK_NEAR(camGO->transform->Position().z, -8.0f, 0.01f);   // ShotB now follows the Boss at its default offset
        CHECK(vb->follow == "Boss" && vb->lookAt == "Boss");
        CHECK(vb->groupTargets.size() == 1 && vb->groupTargets[0] == "Minion");
        CHECK(cam->FovOffset() != 0.0f);
        CHECK(cam->VignettePulse() > 0.5f);
        // Releasing: an empty name goes back to priority.
        for (int i = 0; i < 4; ++i) s.Update(1.0f / 60.0f);
        CHECK(brain->Forced() == nullptr);
        CHECK(brain->LiveCamera() == va);
    }

    // ---- 15. The shipped example scenes play: the hero is in view, keys switch shots, the group shot frames both ----
    {
        const std::string dir = std::string(OKAY_SOURCE_DIR) + "/examples/camera/";
        Scene s("ex"); std::string err;
        std::ifstream in(dir + "camera_rigs.okayscene");
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CHECK(!text.empty());
        CHECK(SceneSerializer::Deserialize(s, text, &err, nullptr));
        s.Start();
        Camera* cam = s.mainCamera;
        CHECK(cam != nullptr);
        GameObject* hero = s.Find("Hero"); GameObject* buddy = s.Find("Buddy");
        CHECK(hero && buddy && cam);
        if (hero && buddy && cam) {
            Input::FeedKeys({});
            for (int i = 0; i < 120; ++i) s.Update(1.0f / 60.0f);
            CHECK(cam->IsPointVisible(hero->transform->Position(), 1280, 720));
            Input::FeedKeys({'4'});  s.Update(1.0f / 60.0f);          // the Group shot
            Input::FeedKeys({});
            for (int i = 0; i < 240; ++i) s.Update(1.0f / 60.0f);
            CHECK(cam->IsPointVisible(hero->transform->Position(), 1280, 720));
            CHECK(cam->IsPointVisible(buddy->transform->Position(), 1280, 720));
            Input::FeedKeys({'x'});  s.Update(1.0f / 60.0f);          // an explosion shake
            Input::FeedKeys({});
            s.Update(1.0f / 60.0f);
            CHECK(cam->IsShaking());
        }
        Scene s2("ex2");
        std::ifstream in2(dir + "camera_2d_rooms.okayscene");
        std::string text2((std::istreambuf_iterator<char>(in2)), std::istreambuf_iterator<char>());
        CHECK(SceneSerializer::Deserialize(s2, text2, &err, nullptr));
        s2.Start();
        GameObject* h2 = s2.Find("Hero");
        if (h2 && s2.mainCamera) {
            Input::FeedKeys({'d'});
            for (int i = 0; i < 600; ++i) s2.Update(1.0f / 60.0f);    // ten seconds walking right: out of Room A into Room B
            Input::FeedKeys({});
            for (int i = 0; i < 120; ++i) s2.Update(1.0f / 60.0f);
            CHECK(h2->transform->Position().x > 16.0f);
            const float cx = s2.mainCamera->gameObject->transform->Position().x;
            CHECK(cx > 16.0f && cx < 46.0f);                          // the view is in Room B (x from 16 to 44)
        }
        Input::FeedKeys({});
    }

    // ---- 16. Camera zones: the field of view, offset and damping blend in across the edge, at every frame rate ----
    {
        for (int r = 0; r < 3; ++r) {
            Rig w;
            w.vc->collision = false; w.vc->positionDamping = 0.0f; w.vc->fieldOfView = 60.0f; w.vc->followOffset = {0, 2, -8};
            GameObject* zg = w.s.CreateGameObject("Corridor");
            zg->transform->localPosition = {20, 0, 0};
            auto* z = zg->AddComponent<CameraZone>();
            z->size = {10, 10, 10}; z->blend = 0.5f; z->fieldOfView = 90.0f; z->followOffset = {0, 0, 4}; z->positionDamping = 3.0f;
            const float dt = 1.0f / kRates[r];
            for (int i = 0; i < 5; ++i) w.Step(dt);
            CHECK_NEAR(w.vc->SolvedFov(), 60.0f, 1e-3f);               // outside: untouched
            CHECK_NEAR(w.cam->fieldOfView, 60.0f, 0.01f);
            CHECK_NEAR(z->WeightAt({30, 0, 0}), 0.0f, 0.0f);          // on the outer face: nothing
            CHECK_NEAR(z->WeightAt({20, 0, 0}), 1.0f, 1e-5f);         // in the middle: full
            CHECK(z->WeightAt({24.5f, 0, 0}) > 0.0f && z->WeightAt({24.5f, 0, 0}) < 1.0f);
            float prevFov = 60.0f, maxJump = 0.0f; bool monotonic = true;
            for (int i = 0; i < (int)(kRates[r] * 4.0f); ++i) {          // walk in, 5 units a second
                w.player->transform->localPosition = {i * dt * 5.0f, 0, 0};
                w.Step(dt);
                const float f = w.cam->fieldOfView;
                maxJump = std::max(maxJump, std::fabs(f - prevFov));
                if (f < prevFov - 1e-4f) monotonic = false;
                prevFov = f;
                CHECK(Finite3(w.CamPos()));
            }
            CHECK(monotonic);
            CHECK(maxJump < 3.5f);                                       // eased over the edge (about 2 degrees a frame at 30 fps), no jump
            for (int i = 0; i < (int)(kRates[r] * 3.0f); ++i) w.Step(dt);   // let the damping settle
            CHECK_NEAR(w.cam->fieldOfView, 90.0f, 0.5f);                  // inside: the zone's field of view
            CHECK_NEAR(w.CamPos().z, -4.0f, 0.05f);                       // offset -8 + 4
            // A zone for another shot does nothing; a disabled one does nothing.
            z->shot = "Other"; w.Step(dt);
            CHECK_NEAR(w.vc->SolvedFov(), 60.0f, 1e-3f);
            z->shot = ""; z->enabled = false; w.Step(dt);
            CHECK_NEAR(w.vc->SolvedFov(), 60.0f, 1e-3f);
        }
        {   // higher priority wins where two overlap; save and load keeps every field
            Scene a("zsave");
            GameObject* g = a.CreateGameObject("Z");
            auto* z = g->AddComponent<CameraZone>();
            z->size = {3, 4, 5}; z->offset = {1, 2, 3}; z->blend = 0.2f; z->priority = 7; z->fieldOfView = 44.0f;
            z->followOffset = {0, 1, 2}; z->positionDamping = 2.5f; z->shot = "Shot A";
            const std::string text = SceneSerializer::Serialize(a);
            Scene b("zload"); std::string err;
            CHECK(SceneSerializer::Deserialize(b, text, &err, nullptr));
            auto* z2 = b.Find("Z") ? b.Find("Z")->GetComponent<CameraZone>() : nullptr;
            CHECK(z2 != nullptr);
            if (z2) {
                CHECK_NEAR(z2->size.z, 5.0f, 1e-6f); CHECK_NEAR(z2->offset.y, 2.0f, 1e-6f); CHECK_NEAR(z2->blend, 0.2f, 1e-6f);
                CHECK(z2->priority == 7 && z2->shot == "Shot A"); CHECK_NEAR(z2->fieldOfView, 44.0f, 1e-6f);
                CHECK_NEAR(z2->followOffset.z, 2.0f, 1e-6f); CHECK_NEAR(z2->positionDamping, 2.5f, 1e-6f);
            }
            CHECK(SceneSerializer::Serialize(b) == text);
        }
        {   // priority
            Rig w;
            w.vc->collision = false; w.vc->positionDamping = 0.0f;
            GameObject* g1 = w.s.CreateGameObject("Low"); auto* z1 = g1->AddComponent<CameraZone>(); z1->blend = 0.0f; z1->fieldOfView = 40.0f; z1->priority = 0;
            GameObject* g2 = w.s.CreateGameObject("High"); auto* z2 = g2->AddComponent<CameraZone>(); z2->blend = 0.0f; z2->fieldOfView = 100.0f; z2->priority = 5;
            for (int i = 0; i < 3; ++i) w.Step(1.0f / 60.0f);
            CHECK_NEAR(w.vc->SolvedFov(), 100.0f, 1e-3f);
        }
    }

    // ---- 17. Camera presets: each one gives a working shot with the target in view ----
    for (int p = 0; p < VirtualCamera::kPresetCount; ++p) {
        Rig w;
        w.vc->ApplyPreset(p);
        if (p == 0) { w.cam->projection = Camera::Projection::Orthographic; w.cam->orthographicSize = 6.0f; }
        w.vc->orbitInput = false;
        const float dt = 1.0f / 60.0f;
        for (int i = 0; i < 240; ++i) { w.player->transform->localPosition = {std::sin(i * 0.02f) * 3.0f, 0.0f, i * 0.02f}; w.Step(dt); CHECK(Finite3(w.CamPos())); }
        CHECK(w.cam->IsPointVisible(w.player->transform->Position(), 1280, 720));
        CHECK(std::string(VirtualCamera::PresetName(p)).size() > 3);
    }
    {
        Rig a, b;
        a.vc->ApplyPreset(3); b.vc->ApplyPreset(5);
        CHECK(a.vc->fieldOfView > b.vc->fieldOfView);                    // first person is wide, cinematic is long-lens
        CHECK(a.vc->positionDamping == 0.0f && b.vc->positionDamping > 1.0f);
    }

    TEST_MAIN_RESULT();
}
