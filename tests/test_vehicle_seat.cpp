// Getting in and driving a vehicle (Seat on a VehicleController): the first Seat in the car's hierarchy is the driver's
// seat, the car is parked until a player sits there, E gets in and out, the move keys drive instead of standing the
// rider up, the rider rides along with their colliders off, and a car with no Seat drives at once as before. Written
// for this engine from its own Seat and VehicleController behaviour; no other project's code was used.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <cstdio>

using namespace okay;

namespace {
const float kDt = 1.0f / 60.0f;

void Tick(Scene& s, const std::vector<char>& keys, int frames) {
    for (int i = 0; i < frames; ++i) { Input::FeedKeys(keys); s.Update(kDt); }
}
float Flat(const Vec3& a, const Vec3& b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z)); }

struct Rig {
    Scene scene{"vehicle_seat"};
    GameObject* car = nullptr;
    VehicleController* vc = nullptr;
    Seat* driver = nullptr;
    Seat* passenger = nullptr;
    GameObject* player = nullptr;
    BoxCollider3D* playerBox = nullptr;
    Rig(bool seats, bool passengerSeat = false) {
        GameObject* floor = scene.CreateGameObject("Floor");
        floor->transform->localPosition = {0, -0.5f, 0};
        floor->AddComponent<BoxCollider3D>()->size = {200, 1, 200};
        floor->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
        car = scene.CreateGameObject("Car");
        car->transform->localPosition = {0, 0.6f, 0};
        car->AddComponent<Rigidbody3D>();
        car->AddComponent<BoxCollider3D>()->size = {1.8f, 1.0f, 4.0f};
        vc = car->AddComponent<VehicleController>();
        if (seats) {
            GameObject* d = scene.CreateGameObject("Driver seat");
            d->transform->SetParent(car->transform, false);
            d->transform->localPosition = {-0.4f, 0.1f, 0.2f};
            driver = d->AddComponent<Seat>();
            driver->exitPoint = {-1.6f, -0.1f, 0.0f};   // beside the driver's door
            driver->useRadius = 2.0f;
            if (passengerSeat) {
                GameObject* p = scene.CreateGameObject("Passenger seat");
                p->transform->SetParent(car->transform, false);
                p->transform->localPosition = {0.4f, 0.1f, 0.2f};
                passenger = p->AddComponent<Seat>();
                passenger->exitPoint = {1.6f, -0.1f, 0.0f};
                passenger->useRadius = 2.0f;
            }
        }
        player = scene.CreateGameObject("Player");
        player->transform->localPosition = {-2.2f, 0.1f, 0.2f};
        auto* ch = player->AddComponent<Character>();
        ch->Apply();
        if (AnimStateMachine* sm = HumanoidLibrary::EnsureAnimator(player)) HumanoidActions::MergeMissing(*sm, ch, true);
        player->AddComponent<Rigidbody3D>();
        playerBox = player->AddComponent<BoxCollider3D>();
        playerBox->size = {0.6f, 1.8f, 0.6f};
        playerBox->offset = {0, 0.9f, 0};
        auto* fp = player->AddComponent<FirstPersonController>();
        fp->lookSmoothing = 0.0f;
        fp->lockCursor = false;
        GameObject* eye = scene.CreateGameObject("Eye");
        eye->AddComponent<Camera>();
        eye->transform->SetParent(player->transform, false);
        eye->transform->localPosition = {0, 1.6f, 0};
        scene.Start();
        Tick(scene, {}, 30);
    }
    float CarSpeed() const { return std::fabs(vc->Speed()); }
};
} // namespace

int main() {
    RUN_SUITE("vehicle_seat");

    {   // No Seat on the car: it drives from the keys at once, as before.
        Rig r(false);
        CHECK(r.vc->DriverSeat() == nullptr);
        CHECK(r.vc->HasDriver());
        Tick(r.scene, {'w'}, 60);
        std::printf("  no seat: speed %.2f\n", r.CarSpeed());
        CHECK(r.CarSpeed() > 5.0f);
    }

    {   // With a driver's seat: parked until someone gets in, then driven, then parked again.
        Rig r(true);
        CHECK(r.vc->DriverSeat() == r.driver);
        CHECK(r.driver->OnVehicle() && r.driver->IsDriverSeat());
        CHECK(!r.vc->HasDriver());
        const Vec3 carStart = r.car->transform->Position();
        Tick(r.scene, {'w'}, 40);                                 // W with nobody in the car: it stays parked
        std::printf("  empty car after W: speed %.2f, moved %.2f\n", r.CarSpeed(), Flat(r.car->transform->Position(), carStart));
        CHECK(r.CarSpeed() < 0.05f);
        CHECK(Flat(r.car->transform->Position(), carStart) < 0.05f);
        // The player (who walked a little with W) steps back near the door and presses E.
        r.player->transform->SetPosition({-2.2f, 0.1f, 0.2f});
        Tick(r.scene, {}, 5);
        CHECK(r.driver->playerInRange);
        Tick(r.scene, {'e'}, 1);
        Tick(r.scene, {}, 1);
        CHECK(r.driver->occupied && r.driver->Occupant() == r.player);
        CHECK(HumanoidActions::InVehicleSeat(r.player));
        CHECK(r.vc->HasDriver());
        CHECK(!r.playerBox->enabled);                             // the rider's body does not push the car
        Tick(r.scene, {}, 40);                                    // the sit-down blend
        // Drive: W moves the car, and the rider stays in the seat instead of standing up.
        Tick(r.scene, {'w'}, 90);
        const float driveSpeed = r.CarSpeed();
        const float riderGap = Flat(r.player->transform->Position(), r.driver->SitWorld());
        std::printf("  driving: speed %.2f, car moved %.2f, rider %.3f from the seat\n", driveSpeed,
                    Flat(r.car->transform->Position(), carStart), riderGap);
        CHECK(driveSpeed > 5.0f);
        CHECK(Flat(r.car->transform->Position(), carStart) > 5.0f);
        CHECK(r.driver->occupied);
        CHECK(riderGap < 0.6f);                                   // rides along (at most a frame behind)
        // E gets out: beside the car, colliders back on, and the car brakes to a stop.
        Tick(r.scene, {}, 2);
        Tick(r.scene, {'e'}, 1);
        Tick(r.scene, {}, 1);
        CHECK(!r.driver->occupied);
        CHECK(!HumanoidActions::InVehicleSeat(r.player));
        CHECK(r.playerBox->enabled);
        CHECK(!r.vc->HasDriver());
        Tick(r.scene, {'w'}, 120);
        std::printf("  after getting out: speed %.2f, seat occupied %d\n", r.CarSpeed(), (int)r.driver->occupied);
        CHECK(r.CarSpeed() < 0.05f);
        CHECK(!r.driver->occupied);                               // the press that got out did not get back in
    }

    {   // A passenger seat does not drive.
        Rig r(true, true);
        CHECK(r.vc->DriverSeat() == r.driver);
        CHECK(r.passenger->OnVehicle() && !r.passenger->IsDriverSeat());
        CHECK(HumanoidActions::RequestSit(r.player, r.passenger->gameObject, 0));
        Tick(r.scene, {}, 40);
        CHECK(r.passenger->occupied);
        CHECK(!r.vc->HasDriver());
        Tick(r.scene, {'w'}, 60);
        CHECK(r.CarSpeed() < 0.05f);
        CHECK(r.passenger->occupied);                             // W did not stand the passenger up
    }

    {   // An ordinary chair (no vehicle): moving still stands up, and colliders stay on.
        Rig r(false);
        GameObject* chair = r.scene.CreateGameObject("Chair");
        chair->transform->localPosition = {-2.2f, 0.0f, 3.0f};
        Seat* s = chair->AddComponent<Seat>();
        CHECK(!s->OnVehicle() && !s->IsDriverSeat());
        CHECK(HumanoidActions::RequestSit(r.player, chair, 0));
        Tick(r.scene, {}, 40);
        CHECK(s->occupied);
        CHECK(r.playerBox->enabled);
        CHECK(!HumanoidActions::InVehicleSeat(r.player));
        Tick(r.scene, {'w'}, 2);
        CHECK(!s->occupied);
    }

    TEST_MAIN_RESULT();
}
