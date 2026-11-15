#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <sstream>
using namespace okay;

// VehicleSeat: get in, drive with the keys only while seated, get out, one driver per
// vehicle, blocked-exit fallback, destroyed driver / vehicle, save and load.
// Time step is 1/60 s. Car: maxSpeed 20, acceleration 60, brakeForce 36, no gravity.

namespace {
const float DT = 1.0f / 60.0f;

struct World {
    Scene s{"seat"};
    GameObject* car = nullptr;
    GameObject* p = nullptr;
    VehicleController* v = nullptr;
    VehicleSeat* seat = nullptr;
    FirstPersonController* fpc = nullptr;
    CapsuleCollider3D* col = nullptr;
    Rigidbody3D* prb = nullptr;

    GameObject* MakePlayer(const char* name, Vec3 pos) {
        GameObject* g = s.CreateGameObject(name);
        g->transform->SetPosition(pos);
        g->AddComponent<Character>();
        return g;
    }
    World(bool start = true) {
        car = s.CreateGameObject("Car");
        car->AddComponent<Rigidbody3D>()->gravityScale = 0.0f;
        v = car->AddComponent<VehicleController>();
        v->maxSpeed = 20.0f; v->acceleration = 60.0f; v->brakeForce = 36.0f;
        seat = car->AddComponent<VehicleSeat>();     // sit (0,0.6,0), exit (-1.8,0,0), radius 2
        p = MakePlayer("P1", {-1.0f, 0.0f, 0.0f});   // flat distance to sit point = 1 < 2
        prb = p->AddComponent<Rigidbody3D>();        // gravity 1
        col = p->AddComponent<CapsuleCollider3D>();
        fpc = p->AddComponent<FirstPersonController>();
        if (start) s.Start();
    }
    void Run(std::vector<char> keys, int frames) {
        for (int i = 0; i < frames; ++i) { Input::FeedKeys(keys); s.Update(DT); }
    }
    void Press(char k) { Input::FeedKeys({}); s.Update(DT); Input::FeedKeys({k}); s.Update(DT); Input::FeedKeys({}); }
};
}

int main() {
    RUN_SUITE("vehicle_seat");
    Log::console = false;

    // --- Nobody inside: the keys do nothing ----------------------------
    {
        World w;
        w.p->transform->SetPosition({50, 0, 0});     // far away, so no one boards
        w.Run({'w'}, 30);
        CHECK_NEAR(w.v->Speed(), 0.0f, 1e-4f);
        CHECK_NEAR(w.car->transform->Position().z, 0.0f, 1e-4f);
        CHECK(w.v->HasDriverSeat());
        CHECK(w.v->Driver() == nullptr);
    }

    // --- Too far away: E does nothing; in range: E boards ---------------
    {
        World w;
        w.p->transform->SetPosition({-5.0f, 0.0f, 0.0f});
        w.Press('e');
        CHECK(!w.seat->occupied);
        w.p->transform->SetPosition({-1.0f, 0.0f, 0.0f});
        w.Press('e');
        CHECK(w.seat->occupied);
        CHECK(w.seat->Occupant() == w.p);
        CHECK(w.v->Driver() == w.p);
        CHECK(!w.fpc->enabled);                       // player controller off
        CHECK(!w.col->enabled);                       // body collider off
        CHECK_NEAR(w.prb->gravityScale, 0.0f, 1e-6f); // does not fall out of the seat
        Vec3 pp = w.p->transform->Position();
        CHECK_NEAR(pp.x, 0.0f, 1e-3f); CHECK_NEAR(pp.y, 0.6f, 1e-3f); CHECK_NEAR(pp.z, 0.0f, 1e-3f);
    }

    // --- Seated: keys drive the car and the driver rides along; E gets out; empty car brakes
    {
        World w;
        w.Press('e');
        CHECK(w.seat->occupied);
        w.Run({'w'}, 60);                             // 1 s: 20 m/s is reached after 1/3 s
        CHECK_NEAR(w.v->Speed(), 20.0f, 0.3f);
        const float cz = w.car->transform->Position().z;
        CHECK(cz > 12.0f);                            // about 20*(1-1/6) = 16.7 m
        CHECK_NEAR(w.p->transform->Position().z, cz, 0.05f);   // carried with the car
        // Get out while still holding W.
        Input::FeedKeys({'w'}); w.s.Update(DT);
        Input::FeedKeys({'w', 'e'}); w.s.Update(DT);
        CHECK(!w.seat->occupied);
        CHECK(w.v->Driver() == nullptr);
        CHECK(w.fpc->enabled);                        // controls come back
        CHECK(w.col->enabled);
        CHECK_NEAR(w.prb->gravityScale, 1.0f, 1e-6f);
        const float exitZ = w.car->transform->Position().z;
        Vec3 pp = w.p->transform->Position();
        CHECK_NEAR(pp.x, -1.8f, 1e-3f);               // exit point is 1.8 m to the left
        CHECK_NEAR(pp.y, 0.0f, 1e-3f);
        CHECK_NEAR(pp.z, exitZ, 0.4f);
        // Empty car brakes at 36 m/s^2: stopped within 0.6 s even with W held.
        w.Run({'w'}, 60);
        CHECK_NEAR(w.v->Speed(), 0.0f, 1e-3f);
        const float stopZ = w.car->transform->Position().z;
        w.Run({'w'}, 30);
        CHECK_NEAR(w.car->transform->Position().z, stopZ, 1e-3f);
    }

    // --- One driver per vehicle; one seat per person; passenger seats ----
    {
        World w;
        GameObject* p2 = w.MakePlayer("P2", {1.0f, 0.0f, 0.0f});
        CHECK(w.seat->Enter(w.p));
        CHECK(!w.seat->Enter(p2));                    // seat taken
        GameObject* sideObj = w.s.CreateGameObject("PassSeat");
        sideObj->transform->SetParent(w.car->transform, false);
        auto* pass = sideObj->AddComponent<VehicleSeat>();
        pass->driver = false;
        auto* second = w.car->AddComponent<VehicleSeat>();   // a second driver seat on the same car
        CHECK(!second->Enter(p2));                    // still one driver
        CHECK(second->Occupant() == nullptr);
        CHECK(!pass->Enter(w.p));                     // P1 already sits somewhere
        CHECK(pass->Enter(p2));                       // a passenger is fine
        CHECK(w.v->Driver() == w.p);                  // and does not take the wheel
        pass->Exit();
        CHECK(w.v->Driver() == w.p);                  // passenger leaving keeps the driver
        w.seat->Exit();
        CHECK(w.v->Driver() == nullptr);
        CHECK(second->Enter(p2));                     // now the other seat can take the wheel
        CHECK(w.v->Driver() == p2);
    }

    // --- Exit placement: free, one side blocked, all blocked -------------
    {
        World w;
        w.seat->exitPoint = {-1.8f, 0.0f, 0.5f};
        auto blocker = [&](float x, float z) {
            GameObject* b = w.s.CreateGameObject("Block");
            b->transform->SetPosition({x, 0.9f, z});
            b->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
            b->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
        };
        w.s.Start();
        CHECK(w.seat->Enter(w.p));
        w.seat->Exit();
        Vec3 a = w.p->transform->Position();          // free: the exit point itself
        CHECK_NEAR(a.x, -1.8f, 1e-3f); CHECK_NEAR(a.z, 0.5f, 1e-3f);

        blocker(-1.8f, 0.5f);                         // left side now blocked
        w.s.Update(DT);
        CHECK(w.seat->Enter(w.p));
        w.seat->Exit();
        Vec3 b = w.p->transform->Position();         // mirrored: the right side
        CHECK_NEAR(b.x, 1.8f, 1e-3f); CHECK_NEAR(b.z, 0.5f, 1e-3f);

        blocker(1.8f, 0.5f); blocker(-1.8f, -0.5f); blocker(1.8f, -0.5f);
        w.s.Update(DT);
        CHECK(w.seat->Enter(w.p));
        w.seat->Exit();
        Vec3 c = w.p->transform->Position();         // all four blocked: the roof, sit + 2.5 up
        CHECK_NEAR(c.x, 0.0f, 1e-3f); CHECK_NEAR(c.y, 3.1f, 1e-3f); CHECK_NEAR(c.z, 0.0f, 1e-3f);
    }

    // --- Driver destroyed while seated: seat freed, no stale pointer ------
    {
        World w;
        w.Press('e');
        CHECK(w.v->Driver() == w.p);
        w.s.Destroy(w.p);
        w.Run({'w'}, 3);
        CHECK(w.v->Driver() == nullptr);
        CHECK(w.seat->Occupant() == nullptr);
        CHECK(!w.seat->occupied);
        w.Run({'w'}, 30);                             // car keeps running with nobody at the wheel
        CHECK_NEAR(w.v->Speed(), 0.0f, 1e-3f);
        GameObject* p2 = w.MakePlayer("P2", {-1.0f, 0.0f, 0.0f});
        CHECK(w.seat->Enter(p2));                     // seat is usable again
    }

    // --- Vehicle destroyed while driving: the player is put back on foot ---
    {
        World w;
        w.Press('e');
        CHECK(!w.fpc->enabled);
        w.s.Destroy(w.car);
        w.Run({}, 3);
        CHECK(w.fpc->enabled);
        CHECK(w.col->enabled);
        CHECK_NEAR(w.prb->gravityScale, 1.0f, 1e-6f);
        CHECK_NEAR(w.p->transform->Position().x, -1.8f, 0.05f);
    }

    // --- A car without a driver seat is unchanged: it reads the keys itself --
    {
        Scene s("plain");
        GameObject* car = s.CreateGameObject("Car");
        car->AddComponent<Rigidbody3D>()->gravityScale = 0.0f;
        auto* v = car->AddComponent<VehicleController>();
        v->maxSpeed = 20.0f; v->acceleration = 60.0f;
        s.Start();
        Input::FeedKeys({'w'});
        for (int i = 0; i < 60; ++i) s.Update(DT);
        CHECK_NEAR(v->Speed(), 20.0f, 0.3f);
        CHECK(!v->HasDriverSeat());
        Input::FeedKeys({});
    }

    // --- Save and load: the seat fields and the link to the vehicle -------
    {
        World w(false);
        w.p->RemoveComponent<FirstPersonController>();   // its own save format is converted on load, not our concern here
        w.seat->driver = false;
        w.seat->sitPoint = {0.3f, 0.7f, -0.2f};
        w.seat->exitPoint = {2.5f, 0.1f, 1.5f};
        w.seat->useRadius = 3.25f;
        w.seat->enterKey = 'f';
        w.seat->prompt = "Hop in";
        GameObject* kid = w.s.CreateGameObject("DriverSeat");
        kid->transform->SetParent(w.car->transform, false);
        kid->AddComponent<VehicleSeat>();
        w.s.Start();
        w.seat->Enter(w.p);                           // runtime state must not be saved
        const std::string text = SceneSerializer::Serialize(w.s);
        Scene s2("loaded");
        std::string err;
        CHECK(SceneSerializer::Deserialize(s2, text, &err, nullptr));
        GameObject* car2 = s2.Find("Car");
        auto* seat2 = car2 ? car2->GetComponent<VehicleSeat>() : nullptr;
        CHECK(seat2 != nullptr);
        if (seat2) {
            CHECK(!seat2->driver);
            CHECK_NEAR(seat2->sitPoint.x, 0.3f, 1e-4f); CHECK_NEAR(seat2->sitPoint.y, 0.7f, 1e-4f);
            CHECK_NEAR(seat2->sitPoint.z, -0.2f, 1e-4f);
            CHECK_NEAR(seat2->exitPoint.x, 2.5f, 1e-4f); CHECK_NEAR(seat2->exitPoint.z, 1.5f, 1e-4f);
            CHECK_NEAR(seat2->useRadius, 3.25f, 1e-4f);
            CHECK(seat2->enterKey == 'f');
            CHECK(seat2->prompt == "Hop in");
            CHECK(!seat2->occupied);
            CHECK(seat2->Occupant() == nullptr);
        }
        GameObject* kid2 = s2.Find("DriverSeat");
        auto* ks = kid2 ? kid2->GetComponent<VehicleSeat>() : nullptr;
        CHECK(ks != nullptr);
        CHECK(ks && ks->Vehicle() == car2->GetComponent<VehicleController>());   // found through the parent
        CHECK(SceneSerializer::Serialize(s2) == text);                            // stable
        // Scene copy of the object: seat survives and still links to its own copy of the car.
        Scene s3("copy");
        GameObject* c3 = SceneSerializer::InstantiateFromText(s3, SceneSerializer::SerializeObject(*w.car));
        auto* seat3 = c3 ? c3->GetComponent<VehicleSeat>() : nullptr;
        CHECK(seat3 && seat3->Vehicle() == c3->GetComponent<VehicleController>());
        CHECK(seat3 && seat3->Occupant() == nullptr);
    }

    TEST_MAIN_RESULT();
}
