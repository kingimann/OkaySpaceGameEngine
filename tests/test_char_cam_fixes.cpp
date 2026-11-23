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
        CHECK_NEAR(cartGo->transform->Position().x, dp->EvaluatePosition(0.75f).x, 1e-3f);   // where 0.75 is on the curve
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

    // ==== Features ================================================================================================

    // ---- Dolly path length and metres <-> position --------------------------------------------------------------
    {
        Scene s("dolly_len");
        GameObject* pathGo = s.CreateGameObject("Rail");
        auto* dp = pathGo->AddComponent<DollyPath>();
        // Bunched at the start: half the path (in t) is the first 2 m, the other half 8 m. (With {0, 1, 10} the curve
        // itself runs backwards past 0 near the start, so its length is not 10; this one only moves forward.)
        dp->waypoints = {{0, 0, 0}, {2, 0, 0}, {10, 0, 0}};
        CHECK_NEAR(dp->Length(), 10.0f, 0.05f);
        const float t = dp->PositionAtDistance(4.0f);
        CHECK_NEAR(dp->EvaluatePosition(t).x, 4.0f, 0.05f);
        CHECK_NEAR(dp->DistanceAt(t), 4.0f, 0.05f);
        CHECK_NEAR(dp->PositionAtDistance(-3.0f), 0.0f, 1e-6f);   // clamped on an open path
        CHECK_NEAR(dp->PositionAtDistance(30.0f), 1.0f, 1e-6f);
        const Vec3 tan = dp->EvaluateTangent(0.5f);
        CHECK_NEAR(tan.x, 1.0f, 1e-3f);
    }

    // ---- Dolly cart: metres per second keep one pace on uneven waypoints ----------------------------------------
    {
        Scene s("dolly_metres");
        s.physicsEnabled = false;
        GameObject* pathGo = s.CreateGameObject("Rail");
        pathGo->AddComponent<DollyPath>()->waypoints = {{0, 0, 0}, {2, 0, 0}, {10, 0, 0}};
        GameObject* cartGo = s.CreateGameObject("Cart");
        auto* dc = cartGo->AddComponent<DollyCart>();
        dc->path = "Rail"; dc->speed = 2.0f; dc->speedInMetres = true;
        s.Start();
        for (int i = 0; i < 60; ++i) s.Update(1.0f / 60.0f);     // 1 s at 2 m/s
        CHECK_NEAR(cartGo->transform->Position().x, 2.0f, 0.1f);
        for (int i = 0; i < 120; ++i) s.Update(1.0f / 60.0f);    // 2 s more: 6 m
        CHECK_NEAR(cartGo->transform->Position().x, 6.0f, 0.1f);
    }

    // ---- Dolly cart: ping-pong turns round at the ends; face along the path follows the travel -----------------
    {
        Scene s("dolly_pingpong");
        s.physicsEnabled = false;
        GameObject* pathGo = s.CreateGameObject("Rail");
        pathGo->AddComponent<DollyPath>()->waypoints = {{0, 0, 0}, {10, 0, 0}};
        GameObject* cartGo = s.CreateGameObject("Cart");
        auto* dc = cartGo->AddComponent<DollyCart>();
        dc->path = "Rail"; dc->speed = 0.5f; dc->pingPong = true; dc->orientToPath = true;
        s.Start();
        for (int i = 0; i < 60; ++i) s.Update(1.0f / 60.0f);     // 1 s: 0.5, going out
        Vec3 fwd = cartGo->transform->Rotation() * Vec3{0, 0, -1};
        CHECK_NEAR(fwd.x, 1.0f, 1e-2f);                           // faces +X, the way it travels
        CHECK(dc->Direction() > 0.0f);
        for (int i = 0; i < 120; ++i) s.Update(1.0f / 60.0f);    // 2 s more: out to 1 and back to 0.5
        CHECK_NEAR(dc->position, 0.5f, 1e-2f);
        CHECK(dc->Direction() < 0.0f);
        CHECK_NEAR(cartGo->transform->Position().x, 5.0f, 0.1f);
        fwd = cartGo->transform->Rotation() * Vec3{0, 0, -1};
        CHECK_NEAR(fwd.x, -1.0f, 1e-2f);                          // turned round
        for (int i = 0; i < 90; ++i) s.Update(1.0f / 60.0f);     // 1.5 s more: to 0 and out to 0.25
        CHECK_NEAR(dc->position, 0.25f, 1e-2f);
        CHECK(dc->Direction() > 0.0f);
    }

    // ---- Saves keep the new dolly cart and click-to-move settings ----------------------------------------------
    {
        Scene s("save_new");
        s.physicsEnabled = false;
        auto* dc = s.CreateGameObject("Cart")->AddComponent<DollyCart>();
        dc->path = "Rail"; dc->speedInMetres = true; dc->pingPong = true; dc->orientToPath = true;
        auto* cm = s.CreateGameObject("P")->AddComponent<ClickToMoveController>();
        cm->pickColliders = false; cm->giveUpWhenStuck = false; cm->stuckTime = 2.5f;
        const std::string txt = SceneSerializer::Serialize(s);
        Scene s2("load_new");
        SceneSerializer::Deserialize(s2, txt);
        auto* dc2 = s2.Find("Cart") ? s2.Find("Cart")->GetComponent<DollyCart>() : nullptr;
        auto* cm2 = s2.Find("P") ? s2.Find("P")->GetComponent<ClickToMoveController>() : nullptr;
        CHECK(dc2 && cm2);
        if (dc2) { CHECK(dc2->speedInMetres); CHECK(dc2->pingPong); CHECK(dc2->orientToPath); CHECK(dc2->path == "Rail"); }
        if (cm2) { CHECK(!cm2->pickColliders); CHECK(!cm2->giveUpWhenStuck); CHECK_NEAR(cm2->stuckTime, 2.5f, 1e-4f); }
        // An older record without the trailing fields keeps the defaults.
        // The record is cut back to its first four fields (path, position, speed, auto move).
        std::string oldTxt = txt;
        {
            const size_t at = oldTxt.find("  dollycart ");
            CHECK(at != std::string::npos);
            const size_t eol = oldTxt.find('\n', at);
            oldTxt.replace(at, eol - at, "  dollycart \"Rail\" 0.25 0.1 1");
        }
        Scene s3("old");
        SceneSerializer::Deserialize(s3, oldTxt);
        auto* dc3 = s3.Find("Cart") ? s3.Find("Cart")->GetComponent<DollyCart>() : nullptr;
        CHECK(dc3 != nullptr);
        if (dc3) { CHECK(!dc3->speedInMetres); CHECK(!dc3->pingPong); CHECK(!dc3->orientToPath); CHECK_NEAR(dc3->position, 0.25f, 1e-5f); }
    }

    // ---- Click to move: a click lands on the raised floor under the pointer -------------------------------------
    // A camera 10 m up looking straight down at x = 5; the screen centre's ray goes straight down onto a 1 m high
    // platform there. The flat plane at the player's feet would say y = 0; the platform top is y = 1.
    {
        auto clickCentre = [](bool pickColliders, bool playerUnderPointer) {
            Scene s("ctm_pick");
            s.physicsEnabled = true;
            GameObject* camGo = s.CreateGameObject("Main Camera");
            s.mainCamera = camGo->AddComponent<Camera>();
            camGo->transform->SetPosition({5, 10, 0});
            camGo->transform->localRotation = Quat::Euler(-90.0f, 0.0f, 0.0f);
            if (!playerUnderPointer) {
                GameObject* plat = s.CreateGameObject("Platform");
                plat->transform->SetPosition({5, 0.5f, 0});
                plat->AddComponent<BoxCollider3D>()->size = {2, 1, 2};
            }
            GameObject* p = s.CreateGameObject("Player");
            // Under the pointer: a wide body 0.4 m off the click point, so the ray hits its top and the walk is real.
            p->transform->SetPosition(playerUnderPointer ? Vec3{5.4f, 0, 0} : Vec3{-5, 0, 0});
            auto* body = p->AddComponent<BoxCollider3D>();
            body->size = {1.2f, 1.8f, 1.2f}; body->offset = {0, 0.9f, 0};
            auto* cm = p->AddComponent<ClickToMoveController>();
            cm->followCamera = false; cm->pickColliders = pickColliders; cm->clickMarker = false;
            s.Start();
            const Vec2 centre{UICanvas::Width() * 0.5f, UICanvas::Height() * 0.5f};
            Input::FeedMouse(centre, 0u);
            Input::FeedMouse(centre, 1u);                         // left button goes down this frame
            s.Update(1.0f / 60.0f);
            Input::FeedMouse(centre, 0u);
            return cm->HasDestination() || cm->GaveUp() ? cm->Destination() : Vec3{-99, -99, -99};
        };
        Vec3 d = clickCentre(true, false);
        CHECK_NEAR(d.y, 1.0f, 0.02f);
        CHECK_NEAR(d.x, 5.0f, 0.05f);
        d = clickCentre(false, false);                            // the old flat plane, when switched off
        CHECK_NEAR(d.y, 0.0f, 0.02f);
        d = clickCentre(true, true);                              // the player's own head is not ground
        CHECK_NEAR(d.y, 0.0f, 0.02f);
    }

    // ---- Click to move: walking into a wall gives up instead of running on the spot ------------------------------
    {
        // Open floor: arrives (8 m at 4 m/s, easing over the last 1.2 m), not a give-up.
        {
            float t = 0.0f;
            bool arrived = false, gaveUp = true;
            {
                Scene s("ctm_open");
                s.physicsEnabled = true;
                GameObject* p = s.CreateGameObject("Player");
                auto* cm = p->AddComponent<ClickToMoveController>();
                cm->followCamera = false; cm->usePathfinding = false;
                s.Start();
                cm->MoveTo({8, 0, 0});
                int i = 0;
                for (; i < 360 && cm->HasDestination(); ++i) s.Update(1.0f / 60.0f);
                t = i / 60.0f; arrived = !cm->HasDestination(); gaveUp = cm->GaveUp();
                CHECK_NEAR(p->transform->Position().x, 8.0f, 0.2f);
            }
            CHECK(arrived);
            CHECK(!gaveUp);
            CHECK(t < 3.5f);
        }
        // A wall across the way: the body is held at the wall and the walk ends about a second later.
        {
            Scene s("ctm_wall");
            s.physicsEnabled = true;
            GameObject* w = s.CreateGameObject("Wall");
            w->transform->SetPosition({3, 1, 0});
            w->AddComponent<BoxCollider3D>()->size = {1, 2, 20};
            GameObject* p = s.CreateGameObject("Player");
            auto* body = p->AddComponent<BoxCollider3D>();
            body->size = {0.6f, 1.8f, 0.6f}; body->offset = {0, 0.9f, 0};
            auto* cm = p->AddComponent<ClickToMoveController>();
            cm->followCamera = false; cm->usePathfinding = false;
            s.Start();
            cm->MoveTo({8, 0, 0});
            int i = 0;
            for (; i < 360 && cm->HasDestination(); ++i) s.Update(1.0f / 60.0f);
            std::printf("  wall: stopped after %.2f s at x = %.2f\n", i / 60.0f, p->transform->Position().x);
            CHECK(!cm->HasDestination());
            CHECK(cm->GaveUp());
            CHECK(p->transform->Position().x < 3.0f);               // never went through
            CHECK(i / 60.0f < 3.0f);
            // A new order clears the flag.
            cm->MoveTo({0, 0, 0});
            CHECK(!cm->GaveUp());
        }
        // Switched off: still pushing at the wall after 6 s (the old behaviour, kept as an option).
        {
            Scene s("ctm_wall_off");
            s.physicsEnabled = true;
            GameObject* w = s.CreateGameObject("Wall");
            w->transform->SetPosition({3, 1, 0});
            w->AddComponent<BoxCollider3D>()->size = {1, 2, 20};
            GameObject* p = s.CreateGameObject("Player");
            auto* body = p->AddComponent<BoxCollider3D>();
            body->size = {0.6f, 1.8f, 0.6f}; body->offset = {0, 0.9f, 0};
            auto* cm = p->AddComponent<ClickToMoveController>();
            cm->followCamera = false; cm->usePathfinding = false; cm->giveUpWhenStuck = false;
            s.Start();
            cm->MoveTo({8, 0, 0});
            for (int i = 0; i < 360; ++i) s.Update(1.0f / 60.0f);
            CHECK(cm->HasDestination());
            CHECK(!cm->GaveUp());
        }
    }

    TEST_MAIN_RESULT();
}
