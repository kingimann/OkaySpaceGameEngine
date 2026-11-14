#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <vector>

using namespace okay;

namespace {

// Drop a body for `seconds` at `fps` and return where physics put it.
float FallAt(float fps, float seconds, bool fixedStep) {
    Scene s("fall");
    if (fixedStep) { s.fixedTimeStep = Scene::kDefaultFixedStep; s.interpolatePhysics = false; }
    GameObject* o = s.CreateGameObject("Body");
    o->AddComponent<Rigidbody3D>();
    s.Start();
    const int frames = (int)std::lround(seconds * fps);
    for (int i = 0; i < frames; ++i) s.Update(1.0f / fps);
    return o->transform->Position().y;
}

} // namespace

int main() {
    RUN_SUITE("fixed_step");

    // ---- The same fall at 30, 60, 144 and 240 FPS ----
    {
        // 1.01 s so each rate lands a little past the 50th step, not on its edge.
        const float y30 = FallAt(30.0f, 1.01f, true), y60 = FallAt(60.0f, 1.01f, true);
        const float y144 = FallAt(144.0f, 1.01f, true), y240 = FallAt(240.0f, 1.01f, true);
        CHECK(y30 < -4.0f);
        CHECK_NEAR(y60, y30, 1e-4);
        CHECK_NEAR(y144, y30, 1e-4);
        CHECK_NEAR(y240, y30, 1e-4);
        // Stepping once per frame (the old way) gives a different answer per rate.
        CHECK(std::fabs(FallAt(30.0f, 1.0f, false) - FallAt(240.0f, 1.0f, false)) > 0.05f);
    }

    // ---- Steps per frame, leftover time, and the catch-up limit ----
    {
        Scene s("steps");
        s.fixedTimeStep = Scene::kDefaultFixedStep;   // 0.02 s
        s.CreateGameObject("Body")->AddComponent<Rigidbody3D>();
        s.Start();
        s.Update(0.01f);
        CHECK(s.PhysicsStepsLastFrame() == 0);
        CHECK_NEAR(s.PhysicsAlpha(), 0.5f, 1e-4);
        s.Update(0.035f);                               // 0.045 banked: two steps, 0.005 left
        CHECK(s.PhysicsStepsLastFrame() == 2);
        CHECK_NEAR(s.PhysicsAlpha(), 0.25f, 1e-3);
        s.Update(5.0f);                                 // a long hitch catches up at most 1/3 s
        CHECK(s.PhysicsStepsLastFrame() <= 17);
        CHECK(s.PhysicsStepsLastFrame() >= 16);
        s.Update(0.0f);                                 // paused: no steps
        CHECK(s.PhysicsStepsLastFrame() == 0);
    }

    // ---- Interpolation: smooth on screen, exact underneath ----
    {
        Scene drawn("drawn"), plain("plain");
        drawn.UseFixedStepPhysics(true);
        plain.fixedTimeStep = Scene::kDefaultFixedStep;
        Rigidbody3D* bodies[2];
        int i = 0;
        for (Scene* s : {&drawn, &plain}) {
            auto* rb = s->CreateGameObject("Body")->AddComponent<Rigidbody3D>();
            rb->gravityScale = 0.0f;
            rb->velocity = Vec3{10.0f, 0.0f, 0.0f};
            bodies[i++] = rb;
            s->Start();
        }
        std::vector<float> shown;
        bool smooth = true;
        float prev = -1.0f;
        for (int f = 0; f < 144; ++f) {
            drawn.Update(1.0f / 144.0f);
            plain.Update(1.0f / 144.0f);
            const float x = bodies[0]->transform->Position().x;
            const float real = bodies[1]->transform->Position().x;
            if (f > 3) {
                // Every frame moves forward by about 10 m/s / 144 fps; no stalls or jumps.
                const float dx = x - prev;
                if (dx < 0.069f - 0.01f || dx > 0.069f + 0.01f) smooth = false;
                CHECK(x <= real + 1e-4f);               // never ahead of the physics
                CHECK(real - x <= 10.0f * 0.02f + 1e-3f);   // at most one step behind
            }
            prev = x;
        }
        CHECK(smooth);
        // Without interpolation the same body only moves on frames with a step.
        int still = 0;
        float last = bodies[1]->transform->Position().x;
        for (int f = 0; f < 20; ++f) {
            plain.Update(1.0f / 144.0f);
            const float x = bodies[1]->transform->Position().x;
            if (std::fabs(x - last) < 1e-6f) ++still;
            last = x;
        }
        CHECK(still >= 10);
    }

    // ---- Moving a body by hand: it goes there, no blend back ----
    {
        Scene s("teleport");
        s.UseFixedStepPhysics(true);
        GameObject* o = s.CreateGameObject("Body");
        auto* rb = o->AddComponent<Rigidbody3D>();
        rb->gravityScale = 0.0f;
        rb->velocity = Vec3{1.0f, 0.0f, 0.0f};
        s.Start();
        for (int f = 0; f < 10; ++f) s.Update(1.0f / 144.0f);
        o->transform->SetPosition(Vec3{100.0f, 5.0f, 0.0f});
        s.Update(1.0f / 144.0f);
        CHECK(o->transform->Position().x >= 100.0f);
        CHECK(o->transform->Position().x < 100.1f);
        CHECK_NEAR(o->transform->Position().y, 5.0f, 1e-4);
        for (int f = 0; f < 30; ++f) s.Update(1.0f / 144.0f);
        CHECK(o->transform->Position().x > 100.1f);   // carries on from there
    }

    // ---- 2D bodies are stepped and drawn the same way ----
    {
        Scene s("2d");
        s.UseFixedStepPhysics(true);
        GameObject* o = s.CreateGameObject("Body");
        auto* rb = o->AddComponent<Rigidbody2D>();
        rb->gravityScale = 0.0f;
        rb->velocity = Vec2{5.0f, 0.0f};
        s.Start();
        float prev = 0.0f;
        bool forward = true;
        for (int f = 0; f < 60; ++f) {
            s.Update(1.0f / 144.0f);
            const float x = o->transform->Position().x;
            if (f > 3 && x <= prev) forward = false;
            prev = x;
        }
        CHECK(forward);
        CHECK(prev > 1.5f);
    }

    TEST_MAIN_RESULT();
}
