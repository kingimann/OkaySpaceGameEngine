// Physics step rate is a setting, interpolation hides a slow step, and a parented
// body follows and detaches. The scene file is examples/physics/step_and_parent.okayscene.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Scene/SceneSerializer.hpp"

#include <cmath>
#include <string>

using namespace okay;

namespace {

Vec3 DropAt(float stepsPerSecond) {
    Scene s("drop");
    s.SetPhysicsStepsPerSecond(stepsPerSecond);
    s.SetSmoothPhysics(false);
    GameObject* floor = s.CreateGameObject("Floor");
    floor->transform->SetPosition({0.0f, -0.5f, 0.0f});
    floor->AddComponent<BoxCollider3D>()->size = {20.0f, 1.0f, 20.0f};
    GameObject* crate = s.CreateGameObject("Crate");
    crate->transform->SetPosition({0.0f, 4.0f, 0.0f});
    crate->AddComponent<BoxCollider3D>();
    Rigidbody3D* rb = crate->AddComponent<Rigidbody3D>();
    rb->velocity = {1.5f, 0.0f, 0.0f};
    rb->friction = 0.9f;
    s.Start();
    for (int i = 0; i < 180; ++i) s.Update(1.0f / 60.0f);
    return crate->transform->Position();
}

float FallY(bool parented) {
    Scene s("fall");
    s.fixedTimeStep = Scene::kDefaultFixedStep;
    s.interpolatePhysics = false;
    GameObject* parent = parented ? s.CreateGameObject("Parent") : nullptr;
    GameObject* body = s.CreateGameObject("Body");
    if (parent) body->transform->SetParent(parent->transform, false);
    body->AddComponent<Rigidbody3D>();
    s.Start();
    for (int i = 0; i < 50; ++i) s.Update(0.02f);
    return body->transform->Position().y;
}

} // namespace

int main() {
    RUN_SUITE("rigidbody_step_parent");

    // A bare scene, and UseFixedStepPhysics with no project rate, stay as they were.
    {
        Scene bare("bare");
        CHECK(bare.fixedTimeStep == 0.0f);
        CHECK(!bare.interpolatePhysics);
        bare.UseFixedStepPhysics(true);
        CHECK_NEAR(bare.fixedTimeStep, Scene::kDefaultFixedStep, 1e-8f);
        CHECK(bare.interpolatePhysics);
        CHECK_NEAR(bare.PhysicsStepsPerSecond(), 50.0f, 1e-3f);
        bare.UseFixedStepPhysics(false);
        CHECK(bare.fixedTimeStep == 0.0f);
        CHECK(!bare.interpolatePhysics);
    }

    // The same drop, two step rates, one place.
    {
        const Vec3 slow = DropAt(30.0f);
        const Vec3 fast = DropAt(120.0f);
        std::printf("  drop 30 Hz (%.3f, %.3f)  120 Hz (%.3f, %.3f)\n", slow.x, slow.y, fast.x, fast.y);
        CHECK(slow.y > 0.2f && slow.y < 0.8f);          // resting on the floor, not through it
        CHECK(std::fabs(slow.x - fast.x) < 0.25f);
        CHECK(std::fabs(slow.y - fast.y) < 0.08f);
        CHECK(std::fabs(slow.z - fast.z) < 0.02f);
    }

    // An identity parent matches an un-parented fall. Old scenes have no parent.
    {
        const float free = FallY(false);
        const float held = FallY(true);
        std::printf("  fall free %.4f held %.4f\n", free, held);
        CHECK(free < -4.0f);
        CHECK_NEAR(held, free, 1e-3f);
    }

    // Interpolation removes the stair-step at a slow physics rate.
    {
        auto run = [](bool smooth, int& still, float& biggest) {
            Scene s("smooth");
            s.SetPhysicsStepsPerSecond(10.0f);
            s.SetSmoothPhysics(smooth);
            Rigidbody3D* rb = s.CreateGameObject("Body")->AddComponent<Rigidbody3D>();
            rb->gravityScale = 0.0f;
            rb->velocity = {10.0f, 0.0f, 0.0f};
            s.Start();
            still = 0;
            biggest = 0.0f;
            float prev = 0.0f;
            for (int f = 0; f < 144; ++f) {
                s.Update(1.0f / 144.0f);
                const float x = rb->transform->Position().x;
                if (f > 20) {   // the first step is at 0.1 s; before that nothing has moved yet
                    const float dx = x - prev;
                    if (dx < 1e-4f) ++still;
                    if (dx > biggest) biggest = dx;
                }
                prev = x;
            }
        };
        int stillSmooth = 0, stillRaw = 0;
        float bigSmooth = 0.0f, bigRaw = 0.0f;
        run(true, stillSmooth, bigSmooth);
        run(false, stillRaw, bigRaw);
        std::printf("  smooth still %d jump %.3f; raw still %d jump %.3f\n",
                    stillSmooth, bigSmooth, stillRaw, bigRaw);
        CHECK(stillSmooth <= 2);
        CHECK(bigSmooth < 0.2f);     // one frame at 10 m/s is about 0.07 m, not a 1 m step
        CHECK(stillRaw >= 40);
    }

    // A moving parent carries the body. A turned parent does not tilt gravity.
    {
        Scene s("carry");
        s.SetPhysicsStepsPerSecond(50.0f);
        s.SetSmoothPhysics(true);
        GameObject* platform = s.CreateGameObject("Platform");
        GameObject* rider = s.CreateGameObject("Rider");
        rider->transform->SetParent(platform->transform, false);
        rider->transform->localPosition = {2.0f, 0.0f, 0.0f};
        Rigidbody3D* rb = rider->AddComponent<Rigidbody3D>();
        rb->bodyType = Rigidbody3D::BodyType::Kinematic;
        rb->gravityScale = 0.0f;
        s.Start();
        bool followed = true;
        for (int f = 0; f < 60; ++f) {
            platform->transform->SetPosition({0.05f * (float)(f + 1), 0.0f, 0.0f});
            s.Update(1.0f / 60.0f);
            const Vec3 expect = platform->transform->Position() + Vec3{2.0f, 0.0f, 0.0f};
            if ((rider->transform->Position() - expect).SqrMagnitude() > 0.02f * 0.02f) followed = false;
        }
        CHECK(followed);

        platform->transform->SetRotation(Quat::AngleAxis(90.0f, Vec3::Up));
        s.Update(1.0f / 60.0f);
        const Vec3 turned = rider->transform->Position() - platform->transform->Position();
        std::printf("  turned offset (%.3f, %.3f, %.3f)\n", turned.x, turned.y, turned.z);
        CHECK(std::fabs(turned.x) < 0.15f);
        CHECK(std::fabs(turned.z) > 1.5f);

        // Gravity on a parent rolled onto its side still pulls down, not along the parent.
        rb->bodyType = Rigidbody3D::BodyType::Dynamic;
        rb->gravityScale = 1.0f;
        rb->velocity = Vec3::Zero;
        platform->transform->SetRotation(Quat::AngleAxis(90.0f, Vec3::Forward));
        const float y0 = rider->transform->Position().y;
        const float x0 = rider->transform->Position().x;
        for (int f = 0; f < 20; ++f) s.Update(1.0f / 60.0f);
        CHECK(rider->transform->Position().y < y0 - 0.15f);
        CHECK(std::fabs(rider->transform->Position().x - x0) < 0.2f);
    }

    // A scaled parent does not stretch the step.
    {
        Scene s("scale");
        s.SetPhysicsStepsPerSecond(50.0f);
        s.SetSmoothPhysics(false);
        GameObject* parent = s.CreateGameObject("Parent");
        parent->transform->localScale = {2.0f, 2.0f, 2.0f};
        GameObject* body = s.CreateGameObject("Body");
        body->transform->SetParent(parent->transform, false);
        Rigidbody3D* rb = body->AddComponent<Rigidbody3D>();
        rb->gravityScale = 0.0f;
        rb->bodyType = Rigidbody3D::BodyType::Kinematic;
        rb->velocity = {1.0f, 0.0f, 0.0f};
        s.Start();
        for (int i = 0; i < 50; ++i) s.Update(0.02f);
        std::printf("  scaled step x %.3f\n", body->transform->Position().x);
        CHECK_NEAR(body->transform->Position().x, 1.0f, 0.05f);
    }

    // Unparenting keeps the speed the parent was giving the body.
    {
        Scene s("detach");
        s.SetPhysicsStepsPerSecond(50.0f);
        s.SetSmoothPhysics(false);
        GameObject* platform = s.CreateGameObject("Platform");
        GameObject* rider = s.CreateGameObject("Rider");
        rider->transform->SetParent(platform->transform, false);
        Rigidbody3D* rb = rider->AddComponent<Rigidbody3D>();
        rb->bodyType = Rigidbody3D::BodyType::Kinematic;
        rb->gravityScale = 0.0f;
        s.Start();
        float x = 0.0f;
        const float dt = 1.0f / 60.0f;
        for (int f = 0; f < 30; ++f) {
            x += 4.0f * dt;
            platform->transform->SetPosition({x, 1.0f, 0.0f});
            s.Update(dt);
        }
        x += 4.0f * dt;
        platform->transform->SetPosition({x, 1.0f, 0.0f});
        rider->transform->SetParent(nullptr, true);
        s.Update(dt);
        const float atDetach = rider->transform->Position().x;
        for (int f = 0; f < 30; ++f) s.Update(dt);
        const float gained = rider->transform->Position().x - atDetach;
        std::printf("  detach gained %.3f (want about 2)\n", gained);
        CHECK(gained > 1.4f);
        CHECK(gained < 2.6f);
    }

    // The scene file: the rider follows the platform, the crate from the file lands.
    {
        Scene slow("file-slow"), fast("file-fast");
        std::string err;
        const std::string path = std::string(OKAY_SOURCE_DIR) + "/examples/physics/step_and_parent.okayscene";
        CHECK(SceneSerializer::LoadFromFile(slow, path, &err));
        CHECK(SceneSerializer::LoadFromFile(fast, path, &err));
        auto land = [](Scene& s, float hz) {
            s.SetPhysicsStepsPerSecond(hz);
            s.SetSmoothPhysics(false);
            GameObject* crate = s.Find("Crate");
            GameObject* platform = s.Find("Platform");
            GameObject* rider = s.Find("Rider");
            CHECK(crate && platform && rider);
            s.Start();
            for (int f = 0; f < 30; ++f) {
                platform->transform->SetPosition({0.1f * (float)f, 2.0f, 0.0f});
                s.Update(1.0f / 60.0f);
            }
            const Vec3 off = rider->transform->Position() - platform->transform->Position();
            CHECK(std::fabs(off.x - 2.0f) < 0.05f);
            CHECK(std::fabs(off.y) < 0.05f);
            for (int f = 0; f < 150; ++f) s.Update(1.0f / 60.0f);
            return crate->transform->Position();
        };
        const Vec3 a = land(slow, 30.0f);
        const Vec3 b = land(fast, 120.0f);
        std::printf("  scene crate 30 Hz y %.3f, 120 Hz y %.3f\n", a.y, b.y);
        CHECK(std::fabs(a.y - b.y) < 0.08f);
        CHECK(a.y > 0.2f && a.y < 0.8f);
    }

    // Scripts and a visual-script node read and set the rate.
    {
        Scene s("script");
        s.UseFixedStepPhysics(true);
        GameObject* go = s.CreateGameObject("Mgr");
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource("function start() { set_physics_steps(30); set_physics_smooth(0); }"));
        s.Start();
        CHECK_NEAR(s.PhysicsStepsPerSecond(), 30.0f, 0.05f);
        CHECK(!s.SmoothPhysics());

        Scene g("graph");
        g.UseFixedStepPhysics(true);
        GameObject* host = g.CreateGameObject("Host");
        auto* vc = host->AddComponent<VisualScriptComponent>();
        std::string err;
        CHECK(vc->LoadFromText(
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 Fn.set_physics_steps\ndefault 2 0 40\nexec 1 0 2\n", &err));
        g.Start();
        CHECK_NEAR(g.PhysicsStepsPerSecond(), 40.0f, 0.05f);
        CHECK(g.SmoothPhysics());   // setting the rate does not turn smoothing off
    }

    TEST_MAIN_RESULT();
}
