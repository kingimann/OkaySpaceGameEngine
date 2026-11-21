// integration_player: the player, controller, animator, camera, interaction, inventory and vehicle
// working as ONE system. Headless: no window, fake key input, fixed-step physics. Each block is one
// promise of the contract in engine/include/okay/Player/PlayerContract.hpp. Nothing here was seen in a window.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Animation/PlayerSetup.hpp"
#include "okay/Player/PlayerContract.hpp"
#include <algorithm>
#include <cmath>
#include <vector>
using namespace okay;
namespace PC = okay::PlayerContract;

namespace {

struct World {
    Scene s{"integration"};
    float dt;
    GameObject* p = nullptr;
    GameObject* car = nullptr;
    VehicleSeat* seat = nullptr;
    VehicleController* veh = nullptr;
    GameObject* wall = nullptr;
    GameObject* apple = nullptr;

    explicit World(int fps, bool withScene = true) : dt(1.0f / (float)fps) {
        s.physicsEnabled = true;
        s.UseFixedStepPhysics(true);
        GameObject* f = s.CreateGameObject("Floor");
        f->transform->localPosition = {0, -0.5f, 0};
        f->AddComponent<BoxCollider3D>()->size = {400, 1, 400};
        if (!withScene) return;
        wall = s.CreateGameObject("Wall");                    // across the way, 12 m to the left (-X)
        wall->transform->localPosition = {-12.0f, 2.0f, 0};
        wall->AddComponent<BoxCollider3D>()->size = {1.0f, 4.0f, 20.0f};
        car = s.CreateGameObject("Car");                      // 6 m ahead (-Z), driver seat on its left
        car->transform->localPosition = {0, 0, -6.0f};
        car->AddComponent<Rigidbody3D>()->gravityScale = 0.0f;
        veh = car->AddComponent<VehicleController>();
        veh->maxSpeed = 15.0f; veh->acceleration = 40.0f; veh->brakeForce = 30.0f;
        seat = car->AddComponent<VehicleSeat>();
        seat->useRadius = 2.5f;
        apple = s.CreateGameObject("Apple");                  // 3 m to the right, on the floor
        apple->transform->localPosition = {3.0f, 0.3f, 0};
        auto* pk = apple->AddComponent<ItemPickup>();
        pk->item = "apple"; pk->pickupOnTouch = false; pk->interactKey = 'e'; pk->interactRange = 1.5f;
    }
    void Spawn(Vec3 at = {0, 0.02f, 0}) { p = CreatePlayerSystem(s, at); s.Start(); }
    void Frame(const std::vector<char>& keys) { Input::FeedKeys(keys); s.Update(dt); }
    void Frames(double seconds, const std::vector<char>& keys) {
        for (int i = 0, n = (int)std::lround(seconds / dt); i < n; ++i) Frame(keys);
    }
    void Tap(char k) { Frame({}); Frame({k}); Frame({}); }
    Vec3 Pos() const { return p->transform->Position(); }
    ~World() { Input::FeedKeys({}); Game::SetPaused(false); }
};

float Dist(Vec3 a, Vec3 b) { Vec3 d = a - b; return std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z); }

}  // namespace

int main() {
    RUN_SUITE("integration_player");
    Log::console = false;
    ItemDatabase& db = ItemDatabase::Get();
    db.Clear();
    db.LoadText("okayitems 1\nitem \"apple\"\n  name \"Apple\"\n  stack 10\n  weight 0.2\n  use \"Eat\" 15\nend\n");

    // ---- Contract table: every component's real order agrees with PlayerContract::Order ----
    {
        World w(60);
        w.Spawn();
        GameObject* g = w.s.CreateGameObject("Probe");
        auto* fik = g->AddComponent<FootIK>();
        auto* lik = g->AddComponent<LookAtIK>();
        auto* fb = g->AddComponent<FullBodyIK>();
        auto* limb = g->AddComponent<LimbIK>();
        auto* aim = g->AddComponent<AimIK>();
        CHECK(w.p->GetComponent<ThirdPersonController>()->ExecutionOrder() == PC::Order::Controller);
        CHECK(w.p->GetComponent<FirstPersonController>()->ExecutionOrder() == PC::Order::Controller);
        CHECK(w.p->GetComponent<PlayerView>()->ExecutionOrder() == PC::Order::View);
        CHECK(fik->ExecutionOrder() == PC::Order::FootIK);
        CHECK(fb->ExecutionOrder() == PC::Order::FullBodyIK);
        CHECK(limb->ExecutionOrder() == PC::Order::LimbIK);
        CHECK(aim->ExecutionOrder() == PC::Order::AimIK);
        CHECK(lik->ExecutionOrder() == PC::Order::AimIK);
        CHECK(w.seat->ExecutionOrder() == PC::Order::Interaction);
        CHECK(w.apple->GetComponent<ItemPickup>()->ExecutionOrder() == PC::Order::Interaction);
        CHECK(w.p->GetComponent<Character>()->ExecutionOrder() == PC::Order::Animation);
        // The chain itself: controller < interaction/animation < every IK pass < skinning.
        CHECK(PC::Order::Controller < PC::Order::Interaction);
        CHECK(PC::Order::Animation < PC::Order::FootIK);
        CHECK(PC::Order::FootIK < PC::Order::Holding && PC::Order::Holding < PC::Order::FullBodyIK);
        CHECK(PC::Order::FullBodyIK < PC::Order::LimbIK && PC::Order::LimbIK < PC::Order::AimIK);
        CHECK(PC::Order::AimIK < PC::Order::Skinning);
    }

    // ---- Input ownership table ----
    {
        PC::Situation s;
        s.vehicleSeatInRange = true; s.pickupInRange = true;
        CHECK(PC::UseKeyOwner(s) == PC::Owner::World);          // a pickup beats a car
        s.pickupInRange = false;
        CHECK(PC::UseKeyOwner(s) == PC::Owner::Vehicle);
        s.vehicleSeatInRange = false;
        CHECK(PC::UseKeyOwner(s) == PC::Owner::Controller);     // lean
        s.seated = true; CHECK(PC::UseKeyOwner(s) == PC::Owner::Vehicle);
        CHECK(PC::MoveKeysOwner(s) == PC::Owner::Vehicle);
        s.uiOpen = true; CHECK(PC::UseKeyOwner(s) == PC::Owner::Menu);
        CHECK(PC::MoveKeysOwner(s) == PC::Owner::None);
        CHECK(PC::WheelOwner(s) == PC::Owner::Menu);
        s.paused = true; CHECK(PC::UseKeyOwner(s) == PC::Owner::None);
        CHECK(PC::MouseLookOwner(s) == PC::Owner::None);
    }

    // ---- Moves and stops, the same at 30, 60 and 144 fps ----
    float walked[3] = {0, 0, 0}, stopped[3] = {0, 0, 0};
    {
        const int fpsList[3] = {30, 60, 144};
        for (int i = 0; i < 3; ++i) {
            World w(fpsList[i], false);
            w.Spawn();
            w.Frames(0.5, {});                                    // settle on the floor
            const Vec3 a = w.Pos();
            w.Frames(2.0, {'w'});
            const Vec3 b = w.Pos();
            walked[i] = Dist(a, b);
            w.Frames(1.5, {});
            const Vec3 c = w.Pos();
            stopped[i] = Dist(b, c);
            CHECK(walked[i] > 4.0f);                              // it moved
            CHECK(stopped[i] < 1.0f);                             // and came to a stop (no skating on)
            auto* rb = w.p->GetComponent<Rigidbody3D>();
            CHECK(std::hypot(rb->velocity.x, rb->velocity.z) < 0.05f);
        }
        CHECK_NEAR(walked[0], walked[1], walked[1] * 0.06f);
        CHECK_NEAR(walked[2], walked[1], walked[1] * 0.06f);
        CHECK_NEAR(stopped[0], stopped[1], 0.25f);
        CHECK_NEAR(stopped[2], stopped[1], 0.25f);
    }

    // ---- The animator follows the movement: idle, walk or run, jump, land ----
    {
        World w(60, false);
        w.Spawn();
        Character* ch = w.p->GetComponent<Character>();
        w.Frames(1.0, {});
        CHECK(ch->anim == 1);                                     // idle
        w.Frames(1.0, {'w'});
        CHECK(ch->anim == 2 || ch->anim == 3);                    // walk or run
        w.Frames(1.5, {});
        CHECK(ch->anim == 1);                                     // back to idle
        bool jumpSeen = false;
        for (int i = 0; i < 90; ++i) { w.Frame(i < 3 ? std::vector<char>{' '} : std::vector<char>{}); if (ch->anim == 5) jumpSeen = true; }
        CHECK(jumpSeen);
        w.Frames(1.0, {});
        CHECK(ch->anim == 1);                                     // landed, idle again
    }

    // ---- The camera follows without lag or jitter, and never ends up inside the wall ----
    {
        World w(60);
        w.Spawn();
        w.Frames(1.0, {});
        Camera* cam = w.s.mainCamera;
        CHECK(cam != nullptr);
        if (cam) {
            std::vector<float> off;
            w.Frames(0.5, {'w'});                                 // get up to speed before measuring
            for (int i = 0; i < 90; ++i) {
                w.Frame({'w'});
                off.push_back(Dist(cam->transform->Position(), w.Pos()));
            }
            float lo = *std::min_element(off.begin(), off.end()), hi = *std::max_element(off.begin(), off.end());
            CHECK(hi - lo < 0.15f);                               // steady distance behind the player
            float maxStep = 0;                                    // per-frame offset change
            for (size_t i = 1; i < off.size(); ++i) maxStep = std::max(maxStep, std::fabs(off[i] - off[i - 1]));
            CHECK(maxStep < 0.05f);
            // Walk left (-X) into the wall at x = -11.5 and keep pushing: the lens never enters the wall box.
            w.p->transform->SetPosition({-9.0f, 0.02f, 8.0f});
            bool inside = false;
            for (int i = 0; i < 240; ++i) {
                w.Frame({'a'});
                const Vec3 c = cam->transform->Position();
                if (c.x < -11.5f && c.x > -12.5f && std::fabs(c.z) < 10.0f && c.y < 4.0f) inside = true;
            }
            CHECK(!inside);
        }
    }

    // ---- Pick an item up into the inventory, then use it ----
    {
        World w(60);
        w.Spawn({2.0f, 0.02f, 0});                                // within 1.5 m of the apple, 6 m from the car
        w.Frames(0.5, {});
        Inventory* inv = w.p->GetComponent<Inventory>();
        CHECK(inv && inv->Count("apple") == 0);
        w.Tap('e');
        CHECK(inv && inv->Count("apple") == 1);
        auto* co = w.p->GetComponent<Consumables>();
        if (co) {
            co->AddRecipe("apple", "Eat", 20.0f);
            w.p->AddComponent<HungerStat>()->hunger = 10.0f;
            CHECK(co->Use("apple"));
            CHECK(inv->Count("apple") == 0);
        }
    }

    // ---- One use key, one action: an apple and a car door both in reach, E picks the apple up and does not board ----
    {
        World w(60);
        w.Spawn({-1.0f, 0.02f, -6.0f});                           // at the driver door
        w.apple->transform->SetPosition({-1.0f, 0.3f, -6.8f});    // and the apple is within 1.5 m too
        w.Frames(0.3, {});
        w.Tap('e');
        Inventory* inv = w.p->GetComponent<Inventory>();
        const bool picked = inv && inv->Count("apple") == 1;
        CHECK(picked != w.seat->occupied);                        // exactly one of the two
        CHECK(picked);                                            // the contract: the pickup wins
    }

    // ---- Walk to the car, get in, drive, get out, everything switches back ----
    {
        World w(60);
        w.Spawn({0, 0.02f, -3.0f});                               // 3 m behind the car (within use radius 2.5 after one step)
        w.Frames(0.5, {'w'});
        w.Frames(0.5, {});
        Camera* cam = w.s.mainCamera;
        auto* tp = w.p->GetComponent<ThirdPersonController>();
        auto* fp = w.p->GetComponent<FirstPersonController>();
        w.p->transform->SetPosition({-1.0f, 0.02f, -6.0f});       // standing at the driver door
        w.Frames(0.2, {});
        w.Tap('e');
        CHECK(w.seat->occupied);
        CHECK(w.seat->Occupant() == w.p);
        CHECK(!tp->enabled && !fp->enabled);                      // controllers off
        CHECK(w.seat->PoseApplied());                             // sitting pose
        CHECK(w.seat->CameraTarget() == w.car);                   // the vehicle camera has the camera
        CHECK(w.s.mainCamera == cam);                             // one camera, one writer
        const Vec3 carBefore = w.car->transform->Position();
        const Vec3 pBefore = w.Pos();
        w.Frames(2.0, {'w'});
        CHECK(Dist(w.car->transform->Position(), carBefore) > 3.0f);   // input drives the car
        const Vec3 rel = w.Pos() - w.car->transform->Position();
        CHECK(Dist(rel, pBefore - carBefore) < 0.3f);             // and the body rides along, not walks away
        // The camera follows the car without a big jump from one frame to the next.
        Vec3 prevCam = cam->transform->Position();
        float maxJump = 0;
        for (int i = 0; i < 60; ++i) {
            w.Frame({'w'});
            maxJump = std::max(maxJump, Dist(cam->transform->Position(), prevCam));
            prevCam = cam->transform->Position();
        }
        CHECK(maxJump < w.veh->maxSpeed * w.dt * 1.5f + 0.05f);
        w.Frames(3.0, {});                                        // coast / stop
        w.Tap('e');
        CHECK(!w.seat->occupied);
        CHECK(w.veh->Driver() == nullptr);
        CHECK(tp->enabled || fp->enabled);                        // controller back
        CHECK(w.seat->CameraTarget() == nullptr);
        // The exit spot is free: nothing solid at the body.
        const Vec3 ex = w.Pos();
        CHECK(std::fabs(ex.y) < 2.0f);
        w.Frames(1.0, {});
        CHECK(w.Pos().y > -0.5f && w.Pos().y < 1.0f);             // stands on the floor, did not fall through or sink
        // Movement keys go to the player again, not the car.
        const Vec3 car2 = w.car->transform->Position();
        w.Frames(1.0, {'w'});
        CHECK(Dist(w.car->transform->Position(), car2) < 0.5f);
        CHECK(Dist(w.Pos(), ex) > 1.0f);
    }

    // ---- Save while seated, load: a sane state ----
    {
        World w(60);
        w.Spawn({-1.0f, 0.02f, -6.0f});
        w.Frames(0.3, {});
        w.Tap('e');
        CHECK(w.seat->occupied);
        const std::string text = SceneSerializer::Serialize(w.s);
        Scene s2("loaded");
        CHECK(SceneSerializer::Deserialize(s2, text));
        s2.Start();
        GameObject* p2 = s2.Find("Player");
        CHECK(p2 != nullptr);
        if (p2) {
            for (int i = 0; i < 30; ++i) { Input::FeedKeys({}); s2.Update(1.0f / 60.0f); }
            auto* tp = p2->GetComponent<ThirdPersonController>();
            CHECK(tp && tp->enabled);                             // the saved player can move again
            for (auto* c : p2->GetComponentsInChildren<Collider3D>(true)) CHECK(c->enabled);
            CHECK(!VehicleSeat::IsSeated(p2));                    // seating is runtime only
            auto* rb = p2->GetComponent<Rigidbody3D>();
            CHECK(rb && rb->gravityScale > 0.5f);
            CHECK(p2->transform->Position().y > -0.5f);
        }
    }

    // ---- Pause freezes everything: movement, animator, camera, the use key ----
    {
        World w(60);
        w.Spawn({-1.0f, 0.02f, -6.0f});
        w.Frames(0.5, {});
        Camera* cam = w.s.mainCamera;
        const Vec3 p0 = w.Pos(), c0 = cam ? cam->transform->Position() : Vec3{};
        Game::SetPaused(true);
        for (int i = 0; i < 60; ++i) { Input::FeedKeys({'w', 'e'}); w.s.Update(0.0f); }
        CHECK(Dist(w.Pos(), p0) < 0.01f);
        if (cam) CHECK(Dist(cam->transform->Position(), c0) < 0.01f);
        CHECK(!w.seat->occupied);                                 // E while paused does not board
        Game::SetPaused(false);
        w.Frame({});
    }

    TEST_MAIN_RESULT();
}
