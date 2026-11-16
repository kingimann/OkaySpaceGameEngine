#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <cstdio>
using namespace okay;

// Water buoyancy and the VehicleController push the body once per PHYSICS step, not once per
// rendered frame (issue #888, Findings 79 and 80). The same scene is run at 30, 60 and 144
// rendered frames per second with the same fixed 50 Hz physics step, and the results must agree.
// A car that is upside down or in the air gets no drive force from its wheels.
//
// Written for this engine from the bug reports; no outside code.

namespace {

// Smoothing off: the transform is then the physics pose itself, not a pose blended between steps.
void FixedStep(Scene& s) {
    s.UseFixedStepPhysics(true);
    s.SetPhysicsStepsPerSecond(50.0f);
    s.SetSmoothPhysics(false);
}

GameObject* Floor(Scene& s, float y = -0.5f) {
    GameObject* f = s.CreateGameObject("Floor");
    f->transform->SetPosition({0, y, 0});
    f->AddComponent<BoxCollider3D>()->size = {400, 1, 400};
    f->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
    return f;
}

// Run the scene for `seconds` of game time at `fps` rendered frames per second, and return how
// many physics steps ran. Adding up 1/144 in float can come out a hair short of a whole number of
// steps, so a last tiny frame (0.1 ms) finishes the step that rounding held back: every frame rate
// then compares the same number of physics steps (50 per second).
int Run(Scene& s, float fps, float seconds) {
    const int frames = (int)std::lround(seconds * fps);
    const int want = (int)std::lround(seconds * 50.0f);
    int steps = 0;
    for (int i = 0; i < frames; ++i) { s.Update(1.0f / fps); steps += s.PhysicsStepsLastFrame(); }
    for (int i = 0; i < 4 && steps < want; ++i) { s.Update(1e-4f); steps += s.PhysicsStepsLastFrame(); }
    return steps;
}

struct Result { float y = 0, vy = 0, z = 0, speed = 0; int steps = 0; };

// A 1 m box of the given mass dropped from y = 2 into still water (surface at 0, 10 m deep).
Result Float(float fps, float mass) {
    Scene s("float");
    FixedStep(s);
    GameObject* w = s.CreateGameObject("Water");
    auto* water = w->AddComponent<Water>();
    water->size = 40.0f;
    water->depth = 10.0f;
    water->waveHeight = 0.0f;
    water->fluidDensity = 1.0f;
    GameObject* b = s.CreateGameObject("Box");
    b->transform->SetPosition({0, 2, 0});
    b->AddComponent<BoxCollider3D>();
    auto* rb = b->AddComponent<Rigidbody3D>();
    rb->mass = mass;
    rb->freezeRotation = true;
    s.Start();
    const int steps = Run(s, fps, 5.0f);
    return {b->transform->Position().y, rb->velocity.y, 0, 0, steps};
}

// A car (2 x 1 x 4 box, gravity on) on a flat floor, full throttle for `seconds`.
Result Drive(float fps, bool suspension, float seconds) {
    Scene s("drive");
    FixedStep(s);
    Floor(s);
    GameObject* car = s.CreateGameObject("Car");
    car->transform->SetPosition({0, suspension ? 0.7f : 0.5f, 0});
    car->AddComponent<BoxCollider3D>()->size = {2, 1, 4};
    auto* rb = car->AddComponent<Rigidbody3D>();
    rb->freezeRotation = true;
    rb->friction = 0.0f;
    auto* v = car->AddComponent<VehicleController>();
    v->suspension = suspension;
    s.Start();
    Input::FeedKeys({'w'});
    const int steps = Run(s, fps, seconds);
    Input::FeedKeys({});
    return {car->transform->Position().y, rb->velocity.y, car->transform->Position().z, v->Speed(), steps};
}

} // namespace

int main() {
    RUN_SUITE("water_vehicle_step");
    const float rates[3] = {30.0f, 60.0f, 144.0f};

    // --- Buoyancy: same float height and speed at any frame rate --------------------------------
    for (float mass : {0.25f, 0.6f}) {
        Result r[3];
        for (int i = 0; i < 3; ++i) {
            r[i] = Float(rates[i], mass);
            std::printf("  float mass %.2f at %3.0f fps (%d steps): y = %+.4f  vy = %+.4f\n", mass, rates[i], r[i].steps,
                        r[i].y, r[i].vy);
        }
        for (int i = 1; i < 3; ++i) {
            CHECK_NEAR(r[i].y, r[0].y, 0.02f);
            CHECK_NEAR(r[i].vy, r[0].vy, 0.05f);
            CHECK(r[i].steps == r[0].steps);
        }
        // A 1 m box of density d floats with (1 - d) of it above the surface: centre at 0.5 - d
        // (the light box still bobs a little after 5 s).
        CHECK_NEAR(r[1].y, 0.5f - mass, 0.1f);
    }

    // --- Driving: same speed and distance at any frame rate --------------------------------------
    for (bool susp : {false, true}) {
        for (float secs : {1.0f, 3.0f}) {
            Result r[3];
            for (int i = 0; i < 3; ++i) {
                r[i] = Drive(rates[i], susp, secs);
                std::printf("  car %s %.0f s at %3.0f fps (%d steps): speed = %.4f  z = %.4f  y = %.4f\n",
                            susp ? "suspension" : "arcade    ", secs, rates[i], r[i].steps, r[i].speed, r[i].z, r[i].y);
            }
            for (int i = 1; i < 3; ++i) {
                CHECK_NEAR(r[i].speed, r[0].speed, 0.05f);
                CHECK_NEAR(r[i].z, r[0].z, 0.05f);
                CHECK_NEAR(r[i].y, r[0].y, 0.02f);
                CHECK(r[i].steps == r[0].steps);
            }
            CHECK(r[1].z > 5.0f);   // it really drove
            if (susp && secs > 2.0f) CHECK_NEAR(r[1].y, 0.7f, 0.05f);   // settled at rideHeight
        }
    }

    // --- Upside down: no drive force ------------------------------------------------------------
    for (bool susp : {false, true}) {
        Scene s("flipped");
        FixedStep(s);
        Floor(s);
        GameObject* car = s.CreateGameObject("Car");
        car->transform->SetPosition({0, 0.5f, 0});
        car->transform->SetRotation(Quat::Euler(0.0f, 0.0f, 180.0f));   // on its roof
        car->AddComponent<BoxCollider3D>()->size = {2, 1, 4};
        auto* rb = car->AddComponent<Rigidbody3D>();
        rb->freezeRotation = true;
        auto* v = car->AddComponent<VehicleController>();
        v->suspension = susp;
        s.Start();
        Input::FeedKeys({'w', 'd'});
        Run(s, 60.0f, 2.0f);
        Input::FeedKeys({});
        const Vec3 p = car->transform->Position();
        const float horiz = std::sqrt(rb->velocity.x * rb->velocity.x + rb->velocity.z * rb->velocity.z);
        std::printf("  flipped (%s): up.y = %+.2f  grounded = %d  speed = %.3f  moved = %.3f m\n",
                    susp ? "suspension" : "arcade", car->transform->Up().y, (int)v->IsGrounded(), horiz,
                    std::sqrt(p.x * p.x + p.z * p.z));
        if (!susp) {   // the suspension model keeps the body upright itself, so it cannot lie on its roof
            CHECK(car->transform->Up().y < -0.9f);
            CHECK(!v->IsGrounded());
            CHECK(horiz < 0.05f);
            CHECK(std::sqrt(p.x * p.x + p.z * p.z) < 0.05f);
        }
    }

    // --- In the air: no drive force, no steering --------------------------------------------------
    for (bool susp : {false, true}) {
        Scene s("air");
        FixedStep(s);
        Floor(s, -100.0f);
        GameObject* car = s.CreateGameObject("Car");
        car->transform->SetPosition({0, 50, 0});
        car->AddComponent<BoxCollider3D>()->size = {2, 1, 4};
        auto* rb = car->AddComponent<Rigidbody3D>();
        rb->freezeRotation = true;
        auto* v = car->AddComponent<VehicleController>();
        v->suspension = susp;
        s.Start();
        const Vec3 f0 = car->transform->Forward();
        Input::FeedKeys({'w', 'd'});
        Run(s, 60.0f, 1.0f);
        Input::FeedKeys({});
        const Vec3 f1 = car->transform->Forward();
        const float horiz = std::sqrt(rb->velocity.x * rb->velocity.x + rb->velocity.z * rb->velocity.z);
        std::printf("  airborne (%s): y = %.2f  grounded = %d  speed = %.3f  heading dot = %.4f\n",
                    susp ? "suspension" : "arcade", car->transform->Position().y, (int)v->IsGrounded(), horiz,
                    f0.x * f1.x + f0.z * f1.z);
        CHECK(!v->IsGrounded());
        CHECK(horiz < 0.05f);
        CHECK_NEAR(f0.x * f1.x + f0.z * f1.z, 1.0f, 1e-3f);
    }

    // --- Air control: the setting gives back that share of drive in the air -----------------------
    {
        Scene s("aircontrol");
        FixedStep(s);
        GameObject* car = s.CreateGameObject("Car");
        car->transform->SetPosition({0, 50, 0});
        auto* rb = car->AddComponent<Rigidbody3D>();
        rb->freezeRotation = true;
        auto* v = car->AddComponent<VehicleController>();
        CHECK_NEAR(v->airControl, 0.0f, 1e-6f);   // default: none
        v->airControl = 0.5f;
        s.Start();
        Input::FeedKeys({'w'});
        Run(s, 60.0f, 0.5f);
        Input::FeedKeys({});
        std::printf("  air control 0.5: speed = %.3f\n", v->Speed());
        CHECK(v->Speed() > 3.0f);   // half of 18 m/s^2 for 0.5 s = 4.5 m/s
        CHECK(v->Speed() < 5.0f);
    }

    // --- airControl is saved (only when set) and loaded ----------------------------------------------
    {
        Scene s("save");
        GameObject* car = s.CreateGameObject("Car");
        auto* v = car->AddComponent<VehicleController>();
        CHECK(SceneSerializer::SerializeObject(*car).find("vehicleair") == std::string::npos);   // default: not written
        v->airControl = 0.4f;
        const std::string txt = SceneSerializer::SerializeObject(*car);
        Scene s2("save2");
        GameObject* c2 = SceneSerializer::InstantiateFromText(s2, txt);
        auto* v2 = c2 ? c2->GetComponent<VehicleController>() : nullptr;
        CHECK(v2 != nullptr);
        if (v2) CHECK_NEAR(v2->airControl, 0.4f, 1e-4f);
    }

    TEST_MAIN_RESULT();
}
