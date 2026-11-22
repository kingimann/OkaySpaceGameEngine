// Area 4 (characters and cameras) fixes and features: the click-to-move follow camera below the origin, remote
// copies of a player leaving the main camera alone, and the dolly cart's position, ends, facing and metre speed.
// Written against this engine's own components; the expected numbers are worked out in the comments.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>

using namespace okay;

namespace {

// A scene with a main camera parked at a known spot.
Camera* AddParkedCamera(Scene& s, const Vec3& at) {
    GameObject* camGo = s.CreateGameObject("Main Camera");
    auto* cam = camGo->AddComponent<Camera>();
    camGo->transform->SetPosition(at);
    s.mainCamera = cam;
    return cam;
}

// A player that this peer does NOT own: a NetworkManager exists and the NetworkSync is Manual, owned = false.
GameObject* AddRemotePlayer(Scene& s) {
    s.CreateGameObject("Net")->AddComponent<NetworkManager>();
    GameObject* p = s.CreateGameObject("Remote Player");
    auto* ns = p->AddComponent<NetworkSync>();
    ns->authority = NetworkSync::Authority::Manual;
    ns->owned = false;
    p->transform->SetPosition({20.0f, 0.0f, 20.0f});
    return p;
}

bool Same(const Vec3& a, const Vec3& b) {
    return std::fabs(a.x - b.x) < 1e-5f && std::fabs(a.y - b.y) < 1e-5f && std::fabs(a.z - b.z) < 1e-5f;
}

} // namespace

int main() {
    RUN_SUITE("char_cam_fixes");

    // ---- Bug: the click-to-move follow camera could not go below y = 0.5 -------------------------------------
    // With "Use Player Height" on, the floor clamp used y = 0 instead of the player's own floor, so a player in a
    // basement at y = -20 had the camera pinned at y = 0.5, twenty metres above. Expected camera height:
    // player y + cameraHeight + sin(pitch) * distance = -20 + 1 + sin(50 deg) * 12 = -9.81.
    {
        Scene s("ctm_low");
        s.physicsEnabled = false;
        Camera* cam = AddParkedCamera(s, {0, 5, 0});
        GameObject* p = s.CreateGameObject("Player");
        p->transform->SetPosition({0.0f, -20.0f, 0.0f});
        auto* cm = p->AddComponent<ClickToMoveController>();
        cm->cameraHeight = 1.0f; cm->cameraDistance = 12.0f; cm->cameraPitch = 50.0f; cm->cameraDamping = 0.0f;
        s.Start();
        s.Update(1.0f / 60.0f);
        const float want = -20.0f + 1.0f + std::sin(50.0f * Mathf::Deg2Rad) * 12.0f;
        CHECK_NEAR(cam->transform->Position().y, want, 0.05f);
        // The clearance still holds relative to the player's floor: a near-flat pitch keeps the lens 0.5 above it.
        cm->minPitch = -30.0f; cm->cameraPitch = -30.0f; cm->cameraHeight = 0.0f;
        s.Update(1.0f / 60.0f);
        CHECK(cam->transform->Position().y >= -20.0f + 0.5f - 1e-3f);
        CHECK(cam->transform->Position().y < -19.0f);
    }

    // ---- Bug: a remote copy of a player moved this machine's main camera ------------------------------------
    // TopDownController, ClickToMoveController and ThirdPersonShooterController skip input on a copy this peer does
    // not own, but their camera code in LateUpdate still ran, so the main camera jumped to the other player.
    {
        Scene s("td_remote");
        s.physicsEnabled = false;
        const Vec3 park{1, 2, 3};
        Camera* cam = AddParkedCamera(s, park);
        GameObject* p = AddRemotePlayer(s);
        p->AddComponent<TopDownController>();
        s.Start();
        for (int i = 0; i < 5; ++i) s.Update(1.0f / 60.0f);
        CHECK(Same(cam->transform->Position(), park));
    }
    {
        Scene s("ctm_remote");
        s.physicsEnabled = false;
        const Vec3 park{1, 2, 3};
        Camera* cam = AddParkedCamera(s, park);
        GameObject* p = AddRemotePlayer(s);
        p->AddComponent<ClickToMoveController>();
        s.Start();
        for (int i = 0; i < 5; ++i) s.Update(1.0f / 60.0f);
        CHECK(Same(cam->transform->Position(), park));
    }
    {
        Scene s("tps_remote");
        s.physicsEnabled = false;
        const Vec3 park{1, 2, 3};
        Camera* cam = AddParkedCamera(s, park);
        GameObject* p = AddRemotePlayer(s);
        p->AddComponent<ThirdPersonShooterController>();
        s.Start();
        for (int i = 0; i < 5; ++i) s.Update(1.0f / 60.0f);
        CHECK(Same(cam->transform->Position(), park));
    }
    // The same controller on a player this peer owns still drives the camera (single player: no NetworkSync).
    {
        Scene s("td_local");
        s.physicsEnabled = false;
        const Vec3 park{1, 2, 3};
        Camera* cam = AddParkedCamera(s, park);
        GameObject* p = s.CreateGameObject("Player");
        p->transform->SetPosition({20.0f, 0.0f, 20.0f});
        p->AddComponent<TopDownController>()->cameraDamping = 0.0f;
        s.Start();
        s.Update(1.0f / 60.0f);
        CHECK(!Same(cam->transform->Position(), park));
    }

    // ---- Bug: the dolly cart's position ran past the end of an open path --------------------------------------
    // The cart added speed * dt forever; the path clamped only when it was read, so after 4 s at 0.5/s the cart
    // stored 2.0 and reversing the speed spent two seconds "coming back" to the end before it moved at all.
    {
        Scene s("dolly_end");
        s.physicsEnabled = false;
        GameObject* pathGo = s.CreateGameObject("Rail");
        auto* dp = pathGo->AddComponent<DollyPath>();
        dp->waypoints = {{0, 0, 0}, {10, 0, 0}};
        GameObject* cartGo = s.CreateGameObject("Cart");
        auto* dc = cartGo->AddComponent<DollyCart>();
        dc->path = "Rail"; dc->speed = 0.5f;
        s.Start();
        for (int i = 0; i < 240; ++i) s.Update(1.0f / 60.0f);   // 4 s
        CHECK_NEAR(dc->position, 1.0f, 1e-4f);
        dc->speed = -0.5f;
        for (int i = 0; i < 30; ++i) s.Update(1.0f / 60.0f);    // 0.5 s back: 1 - 0.25
        CHECK_NEAR(dc->position, 0.75f, 1e-3f);
        CHECK_NEAR(cartGo->transform->Position().x, 7.5f, 0.05f);
    }
    // A looped path keeps the stored position in [0, 1).
    {
        Scene s("dolly_loop");
        s.physicsEnabled = false;
        GameObject* pathGo = s.CreateGameObject("Ring");
        auto* dp = pathGo->AddComponent<DollyPath>();
        dp->waypoints = {{0, 0, 0}, {10, 0, 0}, {10, 0, 10}, {0, 0, 10}};
        dp->looped = true;
        GameObject* cartGo = s.CreateGameObject("Cart");
        auto* dc = cartGo->AddComponent<DollyCart>();
        dc->path = "Ring"; dc->speed = 0.7f;
        s.Start();
        for (int i = 0; i < 300; ++i) s.Update(1.0f / 60.0f);   // 5 s: 3.5 laps -> 0.5
        CHECK(dc->position >= 0.0f && dc->position < 1.0f);
        CHECK_NEAR(dc->position, 0.5f, 1e-3f);
    }

    // ---- Bug: the free-roam fly camera turned to the mirror of where it was placed -----------------------------
    // Start() seeds yaw from the object's facing with atan2(f.x, -f.z); a turn of +Y degrees gives
    // f = (-sin Y, 0, -cos Y), so that read back -Y and the first frame snapped a camera facing left to face right.
    for (float yawDeg : {90.0f, 35.0f, -120.0f}) {
        Scene s("free_roam_seed");
        s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Fly Cam");
        go->transform->localRotation = Quat::Euler(-20.0f, yawDeg, 0.0f);
        const Vec3 before = go->transform->localRotation * Vec3{0, 0, -1};
        go->AddComponent<FreeRoamController>();
        Input::FeedKeys({});
        s.Start();
        s.Update(1.0f / 60.0f);
        const Vec3 after = go->transform->localRotation * Vec3{0, 0, -1};
        CHECK(Same(before, after) || (before - after).Magnitude() < 1e-3f);
    }

    // ---- Bug: three controllers ignored the load / encumbrance slow-down ---------------------------------------
    // MoveSpeedScale (MoveSpeed.hpp) is the one factor every player controller multiplies its speed by; the
    // Character Controller 3D, the Top Down Shooter and the 2.5D Platformer never read it, so an over-weight
    // inventory (speed 0.4) did not slow them. Each runs 1.5 s holding D, once light and once over the limit.
    {
        auto run = [](int kind, bool heavy) {
            Scene s("load");
            s.physicsEnabled = true;
            GameObject* g = s.CreateGameObject("Ground");
            g->transform->SetPosition({0, -0.5f, 0});
            g->AddComponent<BoxCollider3D>()->size = {400, 1, 40};
            g->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
            GameObject* p = s.CreateGameObject("Player");
            p->transform->SetPosition({0, 0.02f, 0});
            p->AddComponent<Rigidbody3D>();
            auto* col = p->AddComponent<BoxCollider3D>();
            col->size = {0.6f, 1.8f, 0.6f};
            col->offset = {0.0f, 0.9f, 0.0f};
            if (kind == 0) p->AddComponent<CharacterController3D>();
            if (kind == 1) { auto* t = p->AddComponent<TopDownShooterController>(); t->aimMode = (int)TopDownShooterController::AimMode::MoveDirection; }
            if (kind == 2) p->AddComponent<Platformer25DController>();
            if (heavy) {
                auto* inv = p->AddComponent<Inventory>();
                inv->weightLimit = 5.0f; inv->wornWeight = 10.0f;
                inv->slowWhenOverWeight = true; inv->overWeightSpeed = 0.4f;
            }
            s.Start();
            Input::FeedKeys({});
            for (int i = 0; i < 30; ++i) s.Update(1.0f / 60.0f);   // settle on the ground
            const float x0 = p->transform->Position().x;
            for (int i = 0; i < 90; ++i) { Input::FeedKeys({'d'}); s.Update(1.0f / 60.0f); }
            Input::FeedKeys({});
            return std::fabs(p->transform->Position().x - x0);
        };
        const char* names[] = {"CharacterController3D", "TopDownShooterController", "Platformer25DController"};
        for (int k = 0; k < 3; ++k) {
            const float light = run(k, false), heavy = run(k, true);
            std::printf("  %s: light %.2f m, over-weight %.2f m\n", names[k], light, heavy);
            CHECK(light > 2.0f);
            CHECK(heavy < light * 0.6f);
            CHECK(heavy > light * 0.25f);
        }
    }

    TEST_MAIN_RESULT();
}
