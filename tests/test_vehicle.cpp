#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
using namespace okay;

// The arcade VehicleController: throttle drives along facing, steering yaws the
// body (scaled by speed), grip kills sideways slide, and it round-trips through
// serialization.
int main() {
    RUN_SUITE("vehicle");

    // --- Throttle accelerates forward (along +Z facing) ----------------
    {
        Scene s("drive");
        GameObject* car = s.CreateGameObject("Car");
        auto* rb = car->AddComponent<Rigidbody3D>(); rb->gravityScale = 0.0f;
        auto* v = car->AddComponent<VehicleController>();
        v->maxSpeed = 20.0f; v->acceleration = 60.0f; v->grip = 7.0f;
        s.Start();
        Input::FeedKeys({'w'});
        for (int i = 0; i < 60; ++i) s.Update(1.0f / 60.0f);
        CHECK(v->Speed() > 15.0f);                       // ramped up toward maxSpeed
        CHECK_NEAR(v->Speed(), 20.0f, 0.5f);
        CHECK(car->transform->Position().z > 5.0f);      // moved forward (+Z)
        CHECK(std::fabs(rb->velocity.x) < 0.5f);         // no sideways drift
    }

    // --- Steering turns the body while moving --------------------------
    {
        Scene s("steer");
        GameObject* car = s.CreateGameObject("Car");
        auto* rb = car->AddComponent<Rigidbody3D>(); rb->gravityScale = 0.0f;
        auto* v = car->AddComponent<VehicleController>();
        v->maxSpeed = 20.0f; v->acceleration = 60.0f; v->turnSpeed = 120.0f;
        s.Start();
        Input::FeedKeys({'w'});
        for (int i = 0; i < 30; ++i) s.Update(1.0f / 60.0f);   // get moving
        Vec3 before = car->transform->Forward();
        Input::FeedKeys({'w', 'd'});
        for (int i = 0; i < 30; ++i) s.Update(1.0f / 60.0f);   // steer right
        Vec3 after = car->transform->Forward();
        float dot = before.x * after.x + before.z * after.z;
        CHECK(dot < 0.999f);                              // heading changed
    }

    // --- Stationary car doesn't steer (needs speed) --------------------
    {
        Scene s("nosteer");
        GameObject* car = s.CreateGameObject("Car");
        car->AddComponent<Rigidbody3D>()->gravityScale = 0.0f;
        auto* v = car->AddComponent<VehicleController>();
        v->turnSpeed = 120.0f;
        s.Start();
        Vec3 before = car->transform->Forward();
        Input::FeedKeys({'d'});                          // steer only, no gas
        for (int i = 0; i < 30; ++i) s.Update(1.0f / 60.0f);
        Vec3 after = car->transform->Forward();
        float dot = before.x * after.x + before.z * after.z;
        CHECK_NEAR(dot, 1.0f, 1e-3f);                     // didn't turn in place
        Input::FeedKeys({});
    }

    // --- Serialization round-trip --------------------------------------
    {
        Scene s("ser");
        GameObject* car = s.CreateGameObject("Car");
        auto* v = car->AddComponent<VehicleController>();
        v->maxSpeed = 33.0f; v->turnSpeed = 95.0f; v->grip = 9.5f;
        v->followCamera = true; v->camDistance = 7.0f; v->handbrakeKey = 'b';
        std::string txt = SceneSerializer::SerializeObject(*car);
        CHECK(txt.find("vehicle ") != std::string::npos);
        Scene s2("ser2");
        GameObject* c2 = SceneSerializer::InstantiateFromText(s2, txt);
        CHECK(c2 != nullptr);
        auto* v2 = c2->GetComponent<VehicleController>();
        CHECK(v2 != nullptr);
        CHECK_NEAR(v2->maxSpeed, 33.0f, 1e-3f);
        CHECK_NEAR(v2->turnSpeed, 95.0f, 1e-3f);
        CHECK_NEAR(v2->grip, 9.5f, 1e-3f);
        CHECK(v2->followCamera);
        CHECK_NEAR(v2->camDistance, 7.0f, 1e-3f);
        CHECK(v2->handbrakeKey == 'b');
    }

    TEST_MAIN_RESULT();
}
