#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <sstream>
using namespace okay;

// VehicleSeat: get in, drive with the keys only while seated, get out, one driver per
// vehicle, blocked-exit fallback, destroyed driver / vehicle, save and load.
// Time step is 1/60 s. Car: maxSpeed 20, acceleration 60, brakeForce 36, no gravity.

namespace {
// These cases test the seat with the older E key and key fields; the scheme path (F key, tap and hold) has its own cases below.
static VehicleController* LegacyVC(GameObject* g) { auto* v = g->AddComponent<VehicleController>(); v->useScheme = false; return v; }
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
        g->AddComponent<ThirdPersonController>();     // a player controller (IPlayerSim): what a player is
        return g;
    }
    World(bool start = true) {
        car = s.CreateGameObject("Car");
        car->AddComponent<Rigidbody3D>()->gravityScale = 0.0f;
        v = LegacyVC(car);
        v->maxSpeed = 20.0f; v->acceleration = 60.0f; v->brakeForce = 36.0f;
        seat = car->AddComponent<VehicleSeat>();     // sit (0,0.6,0), exit (-1.8,0,0), radius 2
        seat->smoothEnter = false;                   // these cases look at the seat position at once; the glide has its own case
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
        CHECK_NEAR(pp.x, 0.0f, 1e-3f); CHECK_NEAR(pp.y, 0.6f - 0.57f / 1.8f * 0.2f, 1e-3f);   // the seated hip height follows the 2.0 m body (sitPoint is for 1.8 m) CHECK_NEAR(pp.z, 0.0f, 1e-3f);
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
    // Each case is its own world, with the blockers in place before anyone moves, so the
    // standing player never overlaps a blocker and nothing leaks between cases.
    {
        auto run = [](int blockers) {
            World w;
            w.seat->exitPoint = {-1.8f, 0.0f, 0.5f};
            const float bx[4] = {-1.8f, 1.8f, -1.8f, 1.8}, bz[4] = {0.5f, 0.5f, -0.5f, -0.5f};
            for (int i = 0; i < blockers; ++i) {
                GameObject* b = w.s.CreateGameObject("Block");
                b->transform->SetPosition({bx[i], 0.9f, bz[i]});
                b->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
                b->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
            }
            w.s.Start();
            Input::FeedKeys({});
            w.s.Update(DT);
            CHECK(w.seat->Enter(w.p));
            w.seat->Exit();
            return w.p->transform->Position();
        };
        Vec3 a = run(0);                              // free: the exit point itself
        CHECK_NEAR(a.x, -1.8f, 1e-3f); CHECK_NEAR(a.z, 0.5f, 1e-3f);
        Vec3 b = run(1);                              // left blocked: only possible if Blocked() said true
        CHECK_NEAR(b.x, 1.8f, 1e-3f); CHECK_NEAR(b.z, 0.5f, 1e-3f);
        Vec3 b2 = run(3);                             // three blocked: the last mirror (+1.8, -0.5)
        CHECK_NEAR(b2.x, 1.8f, 1e-3f); CHECK_NEAR(b2.z, -0.5f, 1e-3f);
        Vec3 c = run(4);                              // all four blocked: the search goes further out (the roof is only the last resort)
        CHECK_NEAR(c.x, -2.6f, 1e-3f); CHECK_NEAR(c.y, 0.0f, 1e-3f); CHECK_NEAR(c.z, 1.3f, 1e-3f);   // the four old spots blocked: the next free spot beside the door (-1.8,.5),(-1.8,1.3),(-1.8,-.3),(-2.2,*) are all within 0.4 of a blocker; (-2.6,1.3) is 0.42 from the corner
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

    // --- Who can board: a first-person-style player yes, an NPC with a Character no ----
    {
        World w;
        GameObject* npc = w.s.CreateGameObject("Npc");
        npc->transform->SetPosition({0.5f, 0.0f, 0.0f});          // nearer than the player
        npc->AddComponent<Character>();
        npc->AddComponent<NPCController>();
        w.Press('e');
        CHECK(w.seat->Occupant() == w.p);             // the player, not the nearer NPC
        CHECK(npc->GetComponent<Character>() != nullptr);
        World w2;                                     // only an NPC around
        w2.p->transform->SetPosition({50, 0, 0});
        GameObject* n2 = w2.s.CreateGameObject("Npc");
        n2->transform->SetPosition({0.5f, 0.0f, 0.0f});
        n2->AddComponent<Character>();
        n2->AddComponent<NPCController>();
        w2.Press('e');
        CHECK(!w2.seat->occupied);
    }

    // --- Saving while seated writes the player as they were; the seat takes them again ---
    {
        World w;
        w.Press('e');
        CHECK(w.seat->occupied);
        const std::string text = SceneSerializer::Serialize(w.s);
        CHECK(!w.fpc->enabled);                       // still seated afterwards
        CHECK(!w.col->enabled);
        CHECK_NEAR(w.prb->gravityScale, 0.0f, 1e-6f);
        const std::string objText = SceneSerializer::SerializeObject(*w.p);   // a copy / prefab of the player
        CHECK(!w.fpc->enabled);
        for (const std::string* t : {&text, &objText}) {
            Scene s2("loaded");
            if (t == &text) CHECK(SceneSerializer::Deserialize(s2, *t, nullptr, nullptr));
            else CHECK(SceneSerializer::InstantiateFromText(s2, *t) != nullptr);
            GameObject* p2 = s2.Find("P1");
            CHECK(p2 != nullptr);
            if (!p2) continue;
            for (Behaviour* b : p2->GetComponents<Behaviour>()) CHECK(b->enabled);   // no frozen controller
            auto* c2 = p2->GetComponent<CapsuleCollider3D>();
            CHECK(c2 && c2->enabled);
            auto* r2 = p2->GetComponent<Rigidbody3D>();
            CHECK(r2 && std::fabs(r2->gravityScale - 1.0f) < 1e-6f);
        }
        w.seat->Exit();
        CHECK(w.fpc->enabled);                        // and the exit still restores everything
    }

    // --- Two seats on one car, one E press: out of one seat, not into the other ---
    {
        World w;
        GameObject* sideObj = w.s.CreateGameObject("PassSeat");
        sideObj->transform->SetParent(w.car->transform, false);
        auto* pass = sideObj->AddComponent<VehicleSeat>();
        pass->driver = false;
        w.s.Start();
        w.Press('e');
        CHECK(w.seat->occupied != pass->occupied);    // in exactly one seat
        w.Press('e');                                 // one press: out, and stays out
        CHECK(!w.seat->occupied);
        CHECK(!pass->occupied);
        CHECK(w.fpc->enabled);
    }

    // --- A dead occupant is put out ---------------------------------------
    {
        World w;
        auto* hp = w.p->AddComponent<HealthStat>();
        w.Press('e');
        CHECK(w.seat->occupied);
        hp->Damage(1000.0f);
        CHECK(hp->IsDead());
        w.Run({}, 2);
        CHECK(!w.seat->occupied);
        CHECK(w.v->Driver() == nullptr);
        CHECK(!w.seat->Enter(w.p));                   // and a dead body cannot get in
    }

    // --- The car stays driveable when its driver seat goes away or stops being a driver seat ---
    {
        World w;
        w.p->transform->SetPosition({50, 0, 0});
        w.Run({'w'}, 10);
        CHECK_NEAR(w.v->Speed(), 0.0f, 1e-4f);        // driver seat, empty: keys ignored
        w.seat->driver = false;                       // now a passenger seat only
        w.Run({'w'}, 60);
        CHECK(w.v->Speed() > 15.0f);                  // old behaviour: reads the keys
        w.seat->driver = true;
        w.Run({}, 120);                               // brakes to a stop again
        CHECK_NEAR(w.v->Speed(), 0.0f, 1e-3f);
        w.car->RemoveComponent(w.seat);
        w.Run({'w'}, 60);
        CHECK(w.v->Speed() > 15.0f);                  // seat removed: drives
        Input::FeedKeys({});
    }

    // --- Hierarchy: not your own object or parent; child hitboxes off; exit upright ---
    {
        World w;
        CHECK(!w.seat->Enter(w.car));                 // the seat's own object
        GameObject* kid = w.s.CreateGameObject("KidCar");
        kid->transform->SetParent(w.p->transform, false);
        LegacyVC(kid);
        auto* kseat = kid->AddComponent<VehicleSeat>();
        CHECK(!kseat->Enter(w.p));                    // the car's parent cannot sit in it
        GameObject* hit = w.s.CreateGameObject("Hitbox");
        hit->transform->SetParent(w.p->transform, false);
        auto* hb = hit->AddComponent<BoxCollider3D>();
        w.s.Start();
        w.car->transform->SetRotation(Quat::Euler(25.0f, 40.0f, 15.0f));   // pitched, rolled, yawed car
        CHECK(w.seat->Enter(w.p));
        CHECK(!hb->enabled);                          // child collider off too
        w.s.Update(DT);
        w.seat->Exit();
        CHECK(hb->enabled);                           // and back
        Vec3 up = w.p->transform->Up();
        CHECK_NEAR(up.y, 1.0f, 1e-3f);                // upright: no pitch or roll left over
    }

    // --- Chair Seat and vehicle seat do not mix ---------------------------
    {
        World w;
        GameObject* chair = w.s.CreateGameObject("Chair");
        chair->transform->SetPosition({5, 0, 5});
        auto* cs = chair->AddComponent<Seat>();
        w.s.Start();
        cs->Claim(w.p);                               // sitting on the chair
        CHECK(!w.seat->Enter(w.p));
        cs->Release();
        CHECK(w.seat->Enter(w.p));                    // free again
        CHECK(!HumanoidActions::RequestSit(w.p, chair));   // in a car: cannot sit on the chair
    }

    // --- A car without a driver seat is unchanged: it reads the keys itself --
    {
        Scene s("plain");
        GameObject* car = s.CreateGameObject("Car");
        car->AddComponent<Rigidbody3D>()->gravityScale = 0.0f;
        auto* v = LegacyVC(car);
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
        w.p->RemoveComponent<ThirdPersonController>();
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

    // --- Wheels: they roll with the speed and the front ones steer --------------------
    {
        World w(false);
        w.v->maxSpeed = 2.0f; w.v->acceleration = 100.0f;
        const char* names[4] = {"Wheel Front Left", "Wheel Front Right", "Wheel Rear Left", "Wheel Rear Right"};
        GameObject* roots[4];
        for (int i = 0; i < 4; ++i) {
            roots[i] = w.s.CreateGameObject(names[i]);
            roots[i]->transform->SetParent(w.car->transform, false);
            GameObject* tire = w.s.CreateGameObject("Tire");
            tire->transform->SetParent(roots[i]->transform, false);
            tire->transform->localScale = {0.7f, 0.25f, 0.7f};      // radius 0.35
            tire->transform->localRotation = Quat::Euler(0.0f, 0.0f, 90.0f);
        }
        w.s.Start();
        w.Press('e');                                       // sit in the driver seat (no animator: carried upright)
        CHECK(w.seat->Occupant() == w.p);
        CHECK(!w.seat->PoseApplied());                      // a body with no humanoid animator has no sit pose
        w.Run({}, 5);
        CHECK(w.v->WheelCount() == 4);
        CHECK_NEAR(w.v->WheelSpin(), 0.0f, 1e-3f);          // stopped: not turning
        const float a0 = w.v->WheelSpin();
        w.Run({'w'}, 30);                                   // 2 m/s for about half a second: 2 / 0.35 rad/s
        const float a1 = w.v->WheelSpin();
        CHECK(a1 - a0 > 110.0f && a1 - a0 < 200.0f);
        w.Run({'w'}, 10);
        const float fast = w.v->WheelSpin() - a1;
        w.v->maxSpeed = 1.0f;
        w.Run({'w'}, 40);                                   // slow down to 1 m/s
        const float b1 = w.v->WheelSpin();
        w.Run({'w'}, 10);
        const float slow = w.v->WheelSpin() - b1;
        CHECK(slow > 0.0f && slow < fast * 0.7f && slow > fast * 0.3f);   // half the speed, about half the roll
        // Forward roll: the top of the tyre moves toward the car's front (+Z).
        CHECK((Quat::AngleAxis(10.0f, Vec3{1, 0, 0}) * Vec3{0, 1, 0}).z > 0.1f);
        // Steering: front wheels turn with the key, rear wheels never.
        const Vec3 head0 = w.car->transform->Forward();
        w.Run({'w', 'd'}, 20);
        CHECK(Vec3::Cross(head0, w.car->transform->Forward()).y < -0.01f);   // D = a right turn: clockwise seen from above
        CHECK(w.v->SteerAngle() > 20.0f && w.v->SteerAngle() <= VehicleController::kMaxSteerAngle + 0.01f);
        CHECK_NEAR(roots[0]->transform->LocalEulerAngles().y, -w.v->SteerAngle(), 0.5f);   // right turn = negative yaw
        CHECK_NEAR(roots[1]->transform->LocalEulerAngles().y, -w.v->SteerAngle(), 0.5f);
        CHECK_NEAR(roots[2]->transform->LocalEulerAngles().y, 0.0f, 0.1f);
        CHECK_NEAR(roots[3]->transform->LocalEulerAngles().y, 0.0f, 0.1f);
        w.Run({'w', 'a'}, 40);
        CHECK(w.v->SteerAngle() < -20.0f);                  // the other way
        w.Run({'w'}, 40);
        CHECK_NEAR(w.v->SteerAngle(), 0.0f, 1.0f);          // straight again
    }

    // --- A seat moved to another car (re-parented) and `driver` unticked while seated ------------
    {
        struct Two {
            Scene s{"reparent"};
            GameObject *a = nullptr, *b = nullptr, *seatObj = nullptr, *seat2Obj = nullptr, *p = nullptr;
            VehicleController *va = nullptr, *vb = nullptr;
            VehicleSeat *seat = nullptr;
            GameObject* MakeCar(const char* name, Vec3 pos) {
                GameObject* c = s.CreateGameObject(name);
                c->transform->SetPosition(pos);
                c->AddComponent<Rigidbody3D>()->gravityScale = 0.0f;
                auto* v = LegacyVC(c);
                v->maxSpeed = 20.0f; v->acceleration = 60.0f;
                return c;
            }
            Two(bool secondSeatOnA) {
                a = MakeCar("A", {0, 0, 0}); b = MakeCar("B", {100, 0, 0});
                va = a->GetComponent<VehicleController>(); vb = b->GetComponent<VehicleController>();
                seatObj = s.CreateGameObject("Seat");
                seatObj->transform->SetParent(a->transform, false);
                seat = seatObj->AddComponent<VehicleSeat>();
                if (secondSeatOnA) {
                    seat2Obj = s.CreateGameObject("Seat2");
                    seat2Obj->transform->SetParent(a->transform, false);
                    seat2Obj->AddComponent<VehicleSeat>();
                }
                p = s.CreateGameObject("P");
                p->transform->SetPosition({-1.0f, 0.0f, 0.0f});
                p->AddComponent<ThirdPersonController>();
                s.Start();
            }
            void Run(std::vector<char> k, int n) { for (int i = 0; i < n; ++i) { Input::FeedKeys(k); s.Update(DT); } }
            void Press(char k) { Run({}, 1); Run({k}, 1); Run({}, 1); }
        };
        {   // (5a) nobody seated: the old car stops counting the seat, the new one counts it
            Two t(false);
            t.Run({}, 3);
            CHECK(t.va->HasDriverSeat() && !t.vb->HasDriverSeat());
            t.p->transform->SetPosition({500, 0, 0});          // keep the player out of the way
            t.seatObj->transform->SetParent(t.b->transform, false);
            t.Run({}, 3);
            CHECK(!t.va->HasDriverSeat());                     // A has no driver seat any more
            CHECK(t.vb->HasDriverSeat());
            t.Run({'w'}, 30);
            CHECK(t.va->Speed() > 5.0f);                       // and A, with no seat, reads the keys again
            CHECK_NEAR(t.vb->Speed(), 0.0f, 1e-3f);            // B waits for a driver
        }
        {   // (5b) seated, then re-parented, then E: nobody is left as A's driver; A does not drive empty
            Two t(true);                                        // A keeps a second driver seat
            t.Run({}, 3);
            t.Press('e');
            CHECK(t.seat->Occupant() == t.p);
            CHECK(t.va->Driver() == t.p);
            t.seatObj->transform->SetParent(t.b->transform, false);
            t.Run({}, 3);
            t.Press('e');                                       // get out
            CHECK(!t.seat->occupied);
            CHECK(t.va->Driver() == nullptr);                  // the car the person drove is freed
            CHECK(t.vb->Driver() == nullptr);
            CHECK(t.va->HasDriverSeat());                      // seat 2 still counts on A
            t.p->transform->SetPosition({-300, 0, 0});          // away from every seat, so W cannot board
            t.Run({'w'}, 60);
            CHECK_NEAR(t.va->Speed(), 0.0f, 1e-2f);            // nobody inside: A does not drive
            CHECK_NEAR(t.vb->Speed(), 0.0f, 1e-2f);
        }
        {   // (6) untick `driver` while seated: exit still frees the car's driver place
            Two t(true);
            t.Run({}, 3);
            t.Press('e');
            CHECK(t.va->Driver() == t.p);
            t.seat->driver = false;
            t.Run({}, 2);
            t.Press('e');                                       // get out
            CHECK(!t.seat->occupied);
            CHECK(t.va->Driver() == nullptr);
            CHECK(t.va->HasDriverSeat());                      // seat 2
            VehicleSeat* s2 = t.seat2Obj->GetComponent<VehicleSeat>();
            t.p->transform->SetPosition({-300, 0, 0});
            t.Run({'w'}, 60);
            CHECK_NEAR(t.va->Speed(), 0.0f, 1e-2f);            // empty car does not drive
            CHECK(s2->Enter(t.p));                              // and seat 2 can be taken
            CHECK(t.va->Driver() == t.p);
        }
    }

    // --- Camera flag follows the car it was set on; a removed VehicleController leaves no stale pointer; wheels re-scan ----
    {
        // (a) seated with the player's camera, seat re-parented to car B, E: car A gets its own camera back
        Scene s("camcar");
        auto mkCar = [&](const char* n, Vec3 pos) {
            GameObject* c = s.CreateGameObject(n);
            c->transform->SetPosition(pos);
            c->AddComponent<Rigidbody3D>()->gravityScale = 0.0f;
            LegacyVC(c)->followCamera = true;
            return c;
        };
        GameObject* a = mkCar("A", {0, 0, 0}); GameObject* b = mkCar("B", {100, 0, 0});
        GameObject* so = s.CreateGameObject("Seat");
        so->transform->SetParent(a->transform, false);
        so->AddComponent<VehicleSeat>();
        GameObject* p = Templates::AddThirdPersonPlayer(s, {-1.0f, 0.0f, 0.0f});
        s.Start();
        auto run = [&](std::vector<char> k, int n) { for (int i = 0; i < n; ++i) { Input::FeedKeys(k); s.Update(DT); } };
        auto press = [&](char k) { run({}, 1); run({k}, 1); run({}, 1); };
        auto* va = a->GetComponent<VehicleController>(); auto* vb = b->GetComponent<VehicleController>();
        run({}, 5);
        press('e');
        CHECK(va->CameraDrivenBySeat());
        so->transform->SetParent(b->transform, false);
        run({}, 3);
        press('e');                                        // out
        CHECK(!va->CameraDrivenBySeat());                  // A is not stuck with its chase camera off
        CHECK(!vb->CameraDrivenBySeat());
        (void)p;
    }
    {
        // (b) remove just the VehicleController component while someone is seated, then add a new one
        Scene s("rmvc");
        GameObject* car = s.CreateGameObject("Car");
        car->AddComponent<Rigidbody3D>()->gravityScale = 0.0f;
        auto* v = LegacyVC(car);
        auto* seat = car->AddComponent<VehicleSeat>();
        GameObject* p = s.CreateGameObject("P");
        p->transform->SetPosition({-1.0f, 0.0f, 0.0f});
        p->AddComponent<ThirdPersonController>();
        s.Start();
        auto run = [&](std::vector<char> k, int n) { for (int i = 0; i < n; ++i) { Input::FeedKeys(k); s.Update(DT); } };
        auto press = [&](char k) { run({}, 1); run({k}, 1); run({}, 1); };
        run({}, 3);
        press('e');
        CHECK(seat->Occupant() == p);
        CHECK(car->RemoveComponent(v));
        run({}, 3);
        auto* v2 = LegacyVC(car);   // may reuse the freed address
        run({}, 3);
        CHECK(v2->HasDriverSeat());                          // the seat counts on the NEW controller, not the dead one
        CHECK(v2->Driver() == nullptr || v2->Driver() == p);
        press('e');                                          // get out: must not touch the freed controller
        CHECK(!seat->occupied);
        CHECK(v2->Driver() == nullptr);
        // and a seat that is only registered (nobody seated)
        auto* v3 = LegacyVC(car);
        run({}, 3);
        CHECK(car->RemoveComponent(v3));
        run({}, 3);
        CHECK(v2->HasDriverSeat());
    }
    {
        // (c) wheels added after the first frame are found (re-scan), and nothing else is driven
        Scene s("late");
        GameObject* car = s.CreateGameObject("Car");
        car->AddComponent<Rigidbody3D>()->gravityScale = 0.0f;
        auto* v = LegacyVC(car);
        v->maxSpeed = 2.0f; v->acceleration = 100.0f;
        s.Start();
        for (int i = 0; i < 5; ++i) { Input::FeedKeys({'w'}); s.Update(DT); }
        CHECK(v->WheelCount() == 0);
        GameObject* w = s.CreateGameObject("Wheel Front Left");
        w->transform->SetParent(car->transform, false);
        GameObject* t = s.CreateGameObject("Tire");
        t->transform->SetParent(w->transform, false);
        t->transform->localScale = {0.7f, 0.25f, 0.7f};
        for (int i = 0; i < 60; ++i) { Input::FeedKeys({'w'}); s.Update(DT); }
        Input::FeedKeys({});
        CHECK(v->WheelCount() == 1);
    }

    // --- Sit clips: the pose height is pinned (a drop from the standing hip height, 0.92 m) ----
    {
        const float kRest = 0.92f;
        struct { const char* name; float hip; } clips[] = {{"Sit_Chair", 0.57f}, {"SitDown_Chair", 0.57f},
                                                           {"Sit_Ground", 0.22f}, {"Sit_Ground_Knees", 0.32f}};
        for (auto& c : clips) {
            const AnimClip* clip = HumanoidLibrary::Find(c.name);
            CHECK(clip != nullptr);
            if (clip && !clip->keys.empty()) CHECK_NEAR(clip->keys.back().root.y, c.hip - kRest, 1e-3f);
        }
        const AnimClip* chair = HumanoidLibrary::Find("Sit_Chair");
        if (chair && !chair->keys.empty()) CHECK(chair->keys.back().root.y < -0.3f);   // lowered, never raised
    }

    // --- A body WITH the humanoid animator: sit pose, death while seated, camera, wheels ----
    struct Hum {
        Scene s{"hum"};
        GameObject* car = nullptr; GameObject* p = nullptr;
        VehicleController* v = nullptr; VehicleSeat* seat = nullptr;
        Hum() {
            car = s.CreateGameObject("Car");
            car->AddComponent<Rigidbody3D>()->gravityScale = 0.0f;
            v = LegacyVC(car);
            seat = car->AddComponent<VehicleSeat>();
            p = Templates::AddThirdPersonPlayer(s, {-1.0f, 0.0f, 0.0f});
            s.Start();
        }
        void Run(std::vector<char> k, int n) { for (int i = 0; i < n; ++i) { Input::FeedKeys(k); s.Update(DT); } }
        void Press(char k) { Run({}, 1); Run({k}, 1); Run({}, 1); }
        Camera* Cam() { return s.mainCamera; }
    };
    {   // dead while seated: stays dead, is not pulled into StandUp
        Hum h;
        h.Run({}, 5);
        h.Press('e');
        CHECK(h.seat->Occupant() == h.p);
        h.Run({}, 30);
        CHECK(h.seat->PoseApplied());
        AnimStateMachine* sm = h.p->GetComponent<AnimStateMachine>();
        CHECK(sm != nullptr);
        if (auto* hp = h.p->GetComponent<HealthStat>()) hp->Damage(1.0e6f);
        HumanoidActions::PlayDeath(h.p, nullptr);
        h.Run({}, 3);
        CHECK(HumanoidActions::IsDead(h.p));
        CHECK(!h.seat->occupied);                          // put out
        h.Run({}, 40);
        CHECK(HumanoidActions::IsDead(h.p));
        if (sm) CHECK(sm->Current() != "StandUp" && sm->Current() != "Sit" && sm->Current() != "SitDown");
    }
    {   // first person: eyes at the seat, restored on exit
        Hum h;
        if (auto* pc = PlayerController::Of(h.p)) if (auto* pv = pc->toggle()) { pv->snapSwitch = true; pv->SetView(0); }
        h.Run({}, 30);
        Camera* cam = h.Cam();
        CHECK(cam != nullptr);
        h.Press('e');
        h.Run({}, 5);
        CHECK(h.seat->CameraTarget() == h.car);
        if (cam && cam->gameObject) {
            const Vec3 eye = cam->gameObject->transform->Position();
            const Vec3 want = h.seat->SitWorld() + Vec3{0.0f, 1.2f, 0.12f};
            CHECK((eye - want).Magnitude() < 0.05f);
            const Vec3 look = cam->gameObject->transform->Rotation() * Vec3{0, 0, -1};
            CHECK(Vec3::Dot(look, h.car->transform->Forward()) > 0.99f);
        }
        h.Press('e');
        CHECK(h.seat->CameraTarget() == nullptr);
        h.Run({}, 5);
        if (cam && cam->gameObject)
            CHECK((cam->gameObject->transform->Position() - h.p->transform->Position()).Magnitude() < 3.0f);
    }
    {   // third person: saving while seated hands the camera pose back; the seat destroyed frees the camera
        Hum h;
        h.Run({}, 30);
        Camera* cam = h.Cam();
        CHECK(cam != nullptr);
        const Vec3 lp0 = cam && cam->gameObject ? cam->gameObject->transform->localPosition : Vec3{};
        h.Press('e');
        h.Run({}, 90);
        CHECK(h.seat->CameraTarget() == h.car);
        if (cam && cam->gameObject) {
            Transform* ct = cam->gameObject->transform;
            VehicleSeat::SuspendAllForSave();
            CHECK((ct->localPosition - lp0).Magnitude() < 0.3f);           // back at the orbit pose it had before boarding (a few metres from the chase pose)
            VehicleSeat::ResumeAllAfterSave();
            h.Run({}, 40);
            CHECK((ct->localPosition - lp0).Magnitude() > 1.0f);           // chase again
        }
        h.s.Destroy(h.car);                                                // the car and its seat go while the camera is taken
        h.Run({}, 5);
        CHECK(!h.p->IsDestroyed());
    }
    {   // no wheels: still counts the roll; the 'Wheel' rule is a prefix; only Front wheels steer
        Scene s("nowheel");
        GameObject* car = s.CreateGameObject("Car");
        car->AddComponent<Rigidbody3D>()->gravityScale = 0.0f;
        auto* v = LegacyVC(car);
        v->maxSpeed = 2.0f; v->acceleration = 100.0f;
        GameObject* fake = s.CreateGameObject("Rewheel");             // contains "wheel" but does not start with "Wheel"
        fake->transform->SetParent(car->transform, false);
        GameObject* ftire = s.CreateGameObject("Tire");
        ftire->transform->SetParent(fake->transform, false);
        ftire->transform->localScale = {0.7f, 0.25f, 0.7f};
        const Quat fakeRot = ftire->transform->localRotation;
        GameObject* side = s.CreateGameObject("Wheel Rear Only");
        side->transform->SetParent(car->transform, false);
        GameObject* stire = s.CreateGameObject("Tire");
        stire->transform->SetParent(side->transform, false);
        stire->transform->localScale = {0.7f, 0.25f, 0.7f};
        s.Start();
        for (int i = 0; i < 40; ++i) { Input::FeedKeys({'w', 'd'}); s.Update(DT); }
        Input::FeedKeys({});
        CHECK(v->WheelCount() == 1);                                   // only the real wheel
        CHECK(v->WheelSpin() > 10.0f);
        CHECK(v->SteerAngle() > 10.0f);                                // the angle exists...
        CHECK_NEAR(side->transform->LocalEulerAngles().y, 0.0f, 0.1f); // ...but a wheel with no "Front" does not turn
        CHECK(ftire->transform->localRotation.w == fakeRot.w && ftire->transform->localRotation.x == fakeRot.x);   // 'Rewheel' untouched
        Scene s2("bare");                                              // a car with no wheel objects at all
        GameObject* c2 = s2.CreateGameObject("Car");
        c2->AddComponent<Rigidbody3D>()->gravityScale = 0.0f;
        auto* v2 = LegacyVC(c2);
        v2->maxSpeed = 2.0f; v2->acceleration = 100.0f;
        s2.Start();
        for (int i = 0; i < 30; ++i) { Input::FeedKeys({'w'}); s2.Update(DT); }
        Input::FeedKeys({});
        CHECK(v2->WheelCount() == 0);
        CHECK(v2->WheelSpin() > 10.0f);
    }

    // --- THE MAIN PROOF: load the real sample scene file from disk and play it --------
    // examples/vehicle/vehicle_test.okayscene is a hand-kept copy of Templates::VehicleSeats3D (also the "Vehicle (get in and drive)" template in the editor chooser; shipped in the Windows zip and installer as examples/vehicle/).
    {
        Scene s("sample");
        std::string err;
        const std::string path = std::string(OKAY_SOURCE_DIR) + "/examples/vehicle/vehicle_test.okayscene";
        const bool loaded = SceneSerializer::LoadFromFile(s, path, &err, nullptr);
        if (!loaded) std::cerr << "    could not load " << path << ": " << err << "\n";
        CHECK(loaded);
        GameObject* car = s.Find("Car");
        GameObject* player = s.Find("Player");
        CHECK(car != nullptr && player != nullptr);
        if (loaded && car && player) {
            auto* v = car->GetComponent<VehicleController>();
            auto* driverSeat = car->GetComponent<VehicleSeat>();
            GameObject* pObj = s.Find("Passenger Seat");
            auto* passSeat = pObj ? pObj->GetComponent<VehicleSeat>() : nullptr;
            CHECK(v && driverSeat && passSeat);
            CHECK(driverSeat && driverSeat->driver);
            CHECK(passSeat && !passSeat->driver);
            if (v && driverSeat && passSeat) {
                s.Start();
                auto run = [&](std::vector<char> keys, int n) { for (int i = 0; i < n; ++i) { Input::FeedKeys(keys); s.Update(DT); } };
                auto press = [&](char k) { run({}, 1); run({k}, 1); run({}, 1); };
                run({}, 90);                                    // the player drops to the ground
                const float car0z = car->transform->Position().z;
                run({'w'}, 30);
                CHECK_NEAR(car->transform->Position().z, car0z, 0.05f);   // nobody inside: the keys do nothing
                CHECK_NEAR(v->Speed(), 0.0f, 1e-3f);
                player->transform->SetPosition({car->transform->Position().x - 2.45f, 1.0f, 0.0f});     // W also walked the player: back to the door
                run({}, 60);

                Camera* cam = s.mainCamera;
                CHECK(cam != nullptr);
                CHECK(!HumanoidActions::IsSitting(player));     // standing
                press('e');                                     // get in (driver door side)
                CHECK(driverSeat->Occupant() == player);
                CHECK(v->Driver() == player);
                CHECK(!passSeat->occupied);
                // Sitting: the player plays the chair sit clip (the humanoid animator's sitting state).
                run({}, 40);
                CHECK(driverSeat->PoseApplied());
                CHECK(HumanoidActions::IsSitting(player));
                // The body faces the way the car drives (humanoids face -Z, the car drives +Z).
                CHECK(Vec3::Dot(player->transform->Rotation() * Vec3{0, 0, -1}, car->transform->Forward()) > 0.99f);
                // Camera: the player's own camera now follows the car.
                CHECK(driverSeat->CameraTarget() == car);
                run({'w'}, 80);                                 // drive; the chase camera catches up
                if (cam && cam->gameObject) {
                    const Vec3 cpos = cam->gameObject->transform->Position();
                    const Vec3 carp = car->transform->Position();
                    CHECK(cpos.z < carp.z - 3.0f && cpos.z > carp.z - 12.0f);     // behind the car
                    CHECK(cpos.y > carp.y + 0.8f);                                // and above it
                    const Vec3 fwd = cam->gameObject->transform->Rotation() * Vec3{0, 0, -1};   // a camera looks down -Z
                    CHECK(Vec3::Dot(fwd, (carp - cpos).Normalized()) > 0.9f);     // looking at the car
                }
                // Wheels roll with the speed, the front ones are straight with no steering.
                CHECK(v->WheelCount() == 4);
                const float spin0 = v->WheelSpin();
                run({'w'}, 3);
                CHECK(std::fabs(v->WheelSpin() - spin0) > 1.0f);
                CHECK_NEAR(v->SteerAngle(), 0.0f, 0.5f);
                run({'w', 'd'}, 15);                            // steer right while driving
                CHECK(v->SteerAngle() > 15.0f);
                if (GameObject* fl = s.Find("Wheel Front Left")) CHECK(std::fabs(fl->transform->LocalEulerAngles().y + v->SteerAngle()) < 1.0f);
                if (GameObject* rl = s.Find("Wheel Rear Left")) CHECK(std::fabs(rl->transform->LocalEulerAngles().y) < 0.5f);
                run({'w', 'a'}, 15);                            // steer back the same amount
                run({'w'}, 30);                                 // about 2.4 s of throttle in all: 22 m/s top speed
                const Vec3 cp = car->transform->Position();
                CHECK(cp.z > car0z + 15.0f);                    // drove forward
                CHECK(v->Speed() > 20.0f);
                const Vec3 pp = player->transform->Position();
                const Vec3 seatLocal = driverSeat->sitPoint;
                CHECK_NEAR(pp.z, cp.z + seatLocal.z, 0.6f);     // at the sit point (the car turned a little while steering)
                CHECK_NEAR(pp.x, cp.x + seatLocal.x, 0.6f);
                CHECK_NEAR(pp.y, cp.y + seatLocal.y, 0.1f);
                CHECK(pp.y > cp.y);

                press('e');                                     // get out
                CHECK(!driverSeat->occupied);
                CHECK(v->Driver() == nullptr);
                CHECK(!driverSeat->PoseApplied());              // the pose flag is cleared
                CHECK(driverSeat->CameraTarget() == nullptr);   // and the camera is the player's again
                const Vec3 cp2 = car->transform->Position();
                const Vec3 pp2 = player->transform->Position();
                const Vec3 away{pp2.x - cp2.x, 0.0f, pp2.z - cp2.z};
                CHECK(away.Magnitude() > 1.0f);   // beside the car, on the driver side
                run({}, 60);                                    // the stand-up clip plays out
                CHECK(!HumanoidActions::IsSitting(player));     // back on their feet
                if (cam && cam->gameObject)
                    CHECK((cam->gameObject->transform->Position() - player->transform->Position()).Magnitude() < 9.0f);   // orbit behind the player
                run({'w'}, 90);                                 // W is held but nobody drives
                CHECK_NEAR(v->Speed(), 0.0f, 1e-2f);
                const float stopZ = car->transform->Position().z;
                run({'w'}, 30);
                CHECK_NEAR(car->transform->Position().z, stopZ, 0.01f);

                // Passenger: walk to the right side, get in; the car does not drive for them.
                player->transform->SetPosition({car->transform->Position().x + 2.2f, 1.0f, car->transform->Position().z - 0.2f});
                run({}, 60);
                press('e');
                CHECK(passSeat->Occupant() == player);
                CHECK(v->Driver() == nullptr);
                run({}, 40);
                CHECK(HumanoidActions::IsSitting(player));      // passengers sit too
                CHECK(passSeat->CameraTarget() == car);
                run({'w'}, 60);
                CHECK_NEAR(v->Speed(), 0.0f, 1e-3f);
                press('e');
                CHECK(!passSeat->occupied);
                CHECK(!passSeat->PoseApplied());
                run({}, 60);
                CHECK(!HumanoidActions::IsSitting(player));
                Input::FeedKeys({});
            }
        }
    }

    // --- Control scheme on the car: F taps in as driver, a short hold goes in as passenger, a press gets out --------------------
    {
        World w(false);
        GameObject* pg = w.s.CreateGameObject("PassSeat");
        pg->transform->SetParent(w.car->transform, false);
        auto* pass = pg->AddComponent<VehicleSeat>(); pass->driver = false; pass->sitPoint = {0.6f, 0.6f, 0.0f}; pass->smoothEnter = false;
        w.seat->smoothEnter = false;
        w.v->useScheme = true;
        w.s.Start();
        w.Run({}, 5);
        w.Run({'e'}, 3); w.Run({}, 5);                          // E is not the key of the Street scheme
        CHECK(!w.seat->occupied && !pass->occupied);
        w.Run({'f'}, 4); w.Run({}, 5);                          // a tap (4 frames = 0.07 s): driver
        CHECK(w.seat->occupied && w.seat->Occupant() == w.p); CHECK(!pass->occupied); CHECK(w.v->Driver() == w.p);
        w.Run({}, 10);
        w.Run({'f'}, 4); w.Run({}, 8);                          // a press gets out ...
        CHECK(!w.seat->occupied); CHECK(w.v->Driver() == nullptr);
        w.Run({}, 10);
        CHECK(!w.seat->occupied && !pass->occupied);            // ... and its release is not read as a tap
        w.p->transform->SetPosition({-1.0f, 0.0f, 0.0f}); w.Run({}, 5);
        w.Run({'f'}, 40);                                       // held 0.67 s: passenger seat, not the driver's
        CHECK(pass->occupied && pass->Occupant() == w.p); CHECK(!w.seat->occupied); CHECK(w.v->Driver() == nullptr);
        w.Run({}, 8);
        CHECK(pass->occupied);                                  // the release of a hold does nothing
        w.Run({'f'}, 4); w.Run({}, 8);
        CHECK(!pass->occupied);                                 // a press gets out of the passenger seat
        Vec3 out = w.p->transform->Position();
        CHECK(std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z));
    }

    // --- Fit: glide to the seat, hips on the cushion for the body's height, scale under a low roof, restore on exit --------------
    {
        World w(false); w.seat->smoothEnter = true; w.seat->roofHeight = 1.2f;
        w.v->useScheme = true; w.s.Start(); w.Run({}, 5);
        CHECK(w.seat->Enter(w.p));
        const Vec3 p0 = w.p->transform->Position();
        CHECK_NEAR(p0.x, -1.0f, 1e-3f);                         // first frame: still where it was
        w.Run({}, 3);
        const Vec3 p1 = w.p->transform->Position();
        CHECK(p1.x > -1.0f && p1.x < 0.0f);                     // gliding
        w.Run({}, 60);
        const Vec3 p2 = w.p->transform->Position();
        // body 2.0 m tall (default capsule): roof 1.2 -> room 1.2 - 0.05 - 0.45 = 0.70; wants 0.7292, floor 0.8 -> scale 0.8
        // hip height seated = 0.57/1.8 * 1.6 = 0.5067; feet = 0.6 + 0.45 - ... equals sitPoint.y + 0.31667*(1.8 - 2.0*0.8) = 0.6 + 0.0633
        CHECK_NEAR(p2.x, 0.0f, 1e-3f); CHECK_NEAR(p2.y, 0.6f + 0.57f / 1.8f * (1.8f - 1.6f), 2e-3f); CHECK_NEAR(p2.z, 0.0f, 1e-3f);
        CHECK_NEAR(w.p->transform->localScale.y, 0.8f, 1e-4f);
        w.seat->Exit();
        CHECK_NEAR(w.p->transform->localScale.y, 1.0f, 1e-5f);  // size given back
    }
    {   // no roof: the body keeps its size, only the hip height follows the body (2.0 m: 0.3167 * (1.8 - 2.0) = -0.0633)
        World w(false); w.seat->smoothEnter = false; w.v->useScheme = true; w.s.Start(); w.Run({}, 5);
        CHECK(w.seat->Enter(w.p)); w.Run({}, 3);
        CHECK_NEAR(w.p->transform->Position().y, 0.6f - 0.57f / 1.8f * 0.2f, 2e-3f); CHECK_NEAR(w.p->transform->localScale.y, 1.0f, 1e-6f);
        w.seat->Exit();
    }
    // --- Seat yaw and clip box ---------------------------------------------------------------------------------------------------------------
    {
        World w(false); w.seat->smoothEnter = false; w.seat->seatYaw = 90.0f; w.seat->useClip = true;
        w.seat->clipCentre = {0.0f, 1.6f, 0.0f}; w.seat->clipHalf = {0.3f, 0.3f, 0.3f};
        GameObject* hat = w.s.CreateGameObject("Hat"); hat->transform->SetParent(w.p->transform, false); hat->transform->localPosition = {0.0f, 1.0f, 0.0f};
        auto* hr = hat->AddComponent<MeshRenderer>();
        GameObject* shoe = w.s.CreateGameObject("Shoe"); shoe->transform->SetParent(w.p->transform, false);
        auto* sr = shoe->AddComponent<MeshRenderer>();
        w.s.Start(); w.Run({}, 5);
        CHECK(w.seat->Enter(w.p)); w.Run({}, 3);
        CHECK(!hr->enabled); CHECK(sr->enabled);                // the part at head height is hidden, the one at the feet is not
        const Vec3 f = w.p->transform->Forward();
        CHECK_NEAR(std::fabs(f.x), 1.0f, 1e-3f);               // turned 90 degrees about up: faces along X
        w.seat->Exit();
        CHECK(hr->enabled);                                     // shown again
    }
    TEST_MAIN_RESULT();
}
