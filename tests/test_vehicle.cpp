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

    // --- 2D top-down vehicle drives along facing + serializes -----------
    {
        Scene s("v2d");
        GameObject* car = s.CreateGameObject("Car2D");
        auto* rb = car->AddComponent<Rigidbody2D>(); rb->gravityScale = 0.0f;
        auto* v = car->AddComponent<VehicleController2D>();
        v->maxSpeed = 12.0f; v->acceleration = 40.0f;
        s.Start();
        Input::FeedKeys({'w'});
        for (int i = 0; i < 60; ++i) s.Update(1.0f / 60.0f);
        CHECK(v->Speed() > 9.0f);                         // accelerated
        CHECK(car->transform->Position().y > 3.0f);       // top-down faces +Y
        Input::FeedKeys({});
        std::string txt = SceneSerializer::SerializeObject(*car);
        CHECK(txt.find("vehicle2d ") != std::string::npos);
        Scene s2("v2d2");
        GameObject* c2 = SceneSerializer::InstantiateFromText(s2, txt);
        auto* v2 = c2 ? c2->GetComponent<VehicleController2D>() : nullptr;
        CHECK(v2 != nullptr);
        CHECK_NEAR(v2->maxSpeed, 12.0f, 1e-3f);
    }

    // --- 2D side-view car drives along X ------------------------------
    {
        Scene s("v2dside");
        GameObject* car = s.CreateGameObject("Car2D");
        auto* rb = car->AddComponent<Rigidbody2D>(); rb->gravityScale = 0.0f;
        auto* v = car->AddComponent<VehicleController2D>();
        v->sideView = true; v->maxSpeed = 10.0f; v->acceleration = 40.0f;
        s.Start();
        Input::FeedKeys({'d'});
        for (int i = 0; i < 60; ++i) s.Update(1.0f / 60.0f);
        CHECK(rb->velocity.x > 8.0f);                     // drives right along X
        Input::FeedKeys({});
    }

    // --- Raycast suspension: settles to ride height over a floor ---------
    {
        Scene s("susp");
        GameObject* floor = s.CreateGameObject("Floor");
        floor->transform->SetPosition({0, -0.5f, 0});      // top surface at y=0
        auto* fb = floor->AddComponent<BoxCollider3D>(); fb->size = {200, 1, 200};
        floor->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;

        GameObject* car = s.CreateGameObject("Car");
        car->transform->SetPosition({0, 4.0f, 0});         // dropped from above
        car->AddComponent<Rigidbody3D>();                  // gravity on
        car->AddComponent<BoxCollider3D>()->size = {1.6f, 0.5f, 2.4f};
        auto* v = car->AddComponent<VehicleController>();
        v->suspension = true; v->rideHeight = 0.8f; v->springStrength = 80.0f; v->springDamping = 12.0f;
        s.Start();
        Input::FeedKeys({});
        for (int i = 0; i < 240; ++i) s.Update(1.0f / 60.0f);   // ~4s to settle
        CHECK(v->IsGrounded());
        CHECK_NEAR(car->transform->Position().y, 0.8f, 0.2f);   // rests at ride height
    }

    // --- Suspension serialization round-trip ----------------------------
    {
        Scene s("suspser");
        GameObject* car = s.CreateGameObject("Car");
        auto* v = car->AddComponent<VehicleController>();
        v->suspension = true; v->rideHeight = 1.1f; v->springStrength = 72.0f;
        v->wheelBase = 3.0f; v->maxTilt = 22.0f;
        std::string txt = SceneSerializer::SerializeObject(*car);
        CHECK(txt.find("vehiclesusp ") != std::string::npos);
        Scene s2("suspser2");
        GameObject* c2 = SceneSerializer::InstantiateFromText(s2, txt);
        auto* v2 = c2 ? c2->GetComponent<VehicleController>() : nullptr;
        CHECK(v2 != nullptr);
        CHECK(v2->suspension);
        CHECK_NEAR(v2->rideHeight, 1.1f, 1e-3f);
        CHECK_NEAR(v2->wheelBase, 3.0f, 1e-3f);
        CHECK_NEAR(v2->maxTilt, 22.0f, 1e-3f);
    }

    TEST_MAIN_RESULT();
}
