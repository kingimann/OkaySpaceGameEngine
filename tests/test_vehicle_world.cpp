#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <cstdio>
#include <cstdlib>
using namespace okay;

// examples/driving/driving_world.okayscene is Templates::DrivingWorld saved to a file. Run with the environment variable
// OKAY_WRITE_DRIVING=<path> to rewrite it. Normally this test loads the shipped file and plays it: walk to a car, press E,
// drive, press E to get out.
int main() {
    RUN_SUITE("vehicle_world");
    const std::string path = std::string(OKAY_SOURCE_DIR) + "/examples/driving/driving_world.okayscene";
    if (const char* out = std::getenv("OKAY_WRITE_DRIVING")) {
        Scene s("gen"); Templates::DrivingWorld(s);
        CHECK(SceneSerializer::SaveToFile(s, out));
        std::printf("  wrote %s\n", out);
    }
    // Every vehicle type builds, has wheels, a driver seat and its preset.
    for (const auto& sh : VehicleBuilder::Shapes()) {
        Scene s("b"); GameObject* c = VehicleBuilder::Make(s, sh.name, {0, 0, 0}, 0, Color::FromBytes(200, 50, 50));
        CHECK(c != nullptr);
        auto* v = c ? c->GetComponent<VehicleController>() : nullptr;
        CHECK(v && v->openWorld && v->PresetName() == sh.name);
        CHECK(c && c->GetComponent<VehicleSeat>() && c->GetComponent<VehicleSeat>()->driver);
        s.Start(); s.Update(1.0f / 60.0f);
        CHECK(v && v->WheelCount() == 4);
    }
    CHECK(VehicleBuilder::Make(*std::make_unique<Scene>("x"), "Bicycle", {0, 0, 0}, 0, Color::FromBytes(1, 1, 1)) == nullptr);

    Scene s("world");
    std::string err;
    const bool loaded = SceneSerializer::LoadFromFile(s, path, &err, nullptr);
    if (!loaded) std::cerr << "    could not load " << path << ": " << err << "\n";
    CHECK(loaded);
    if (loaded) {
        GameObject* car = s.Find("Sedan");
        GameObject* player = s.Find("Player");
        CHECK(car && player);
        auto* v = car ? car->GetComponent<VehicleController>() : nullptr;
        auto* seat = car ? car->GetComponent<VehicleSeat>() : nullptr;
        CHECK(v && seat && v->openWorld);
        if (v && seat && player) {
            const float DT = 1.0f / 60.0f;
            auto run = [&](std::vector<char> keys, int n) { for (int i = 0; i < n; ++i) { Input::FeedKeys(keys); s.Update(DT); } };
            auto press = [&](char k) { run({}, 1); run({k}, 1); run({}, 1); };
            s.Start();
            run({}, 90);
            CHECK_NEAR(v->Speed(), 0.0f, 1e-3f);
            const Vec3 near = car->transform->TransformPoint({-2.0f, -0.3f, 0});
            player->transform->SetPosition({near.x, 1.0f, near.z});
            run({}, 30);
            press('e');
            CHECK(seat->Occupant() == player);
            run({}, 30);
            const Vec3 p0 = car->transform->Position(); const Vec3 fwd0 = car->transform->Forward();
            run({'w'}, 180);
            const float speed = v->Speed();
            std::printf("  Driving World: Sedan after 3 s of W: %.1f m/s, moved %.1f m, gear %d\n", speed, Vec3::Dot(car->transform->Position() - p0, fwd0), v->Gear());
            CHECK(speed > 5.0f); CHECK(std::isfinite(speed)); CHECK(Vec3::Dot(car->transform->Position() - p0, fwd0) > 5.0f);
            CHECK(v->Gear() >= 2);
            run({'w', 'd'}, 60); CHECK(Vec3::Dot(car->transform->Forward(), fwd0) < 0.99f);   // D turns right: the heading changes
            CHECK(car->transform->Forward().z < -0.05f);                       // facing +X, right is -Z
            run({'s'}, 240);                                                   // brake, then reverse is allowed
            CHECK(v->Speed() < 3.0f);
            run({}, 120);
            press('e');
            CHECK(seat->Occupant() == nullptr);
            CHECK(std::fabs(v->Speed()) < 6.0f);
            run({}, 60);
        }
    }
    Input::FeedKeys({});
    TEST_MAIN_RESULT();
}
