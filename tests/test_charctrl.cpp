#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("charctrl");

    // Top-down: pressing 'd' drives a sibling Rigidbody2D's velocity right.
    {
        Scene s("T"); s.physicsEnabled = true;
        GameObject* o = s.CreateGameObject("Player");
        auto* rb = o->AddComponent<Rigidbody2D>();
        rb->bodyType = Rigidbody2D::BodyType::Kinematic; // move purely by velocity
        rb->gravityScale = 0.0f;
        auto* cc = o->AddComponent<CharacterController2D>();
        cc->mode = CharacterController2D::Mode::TopDown; cc->speed = 4.0f;
        s.Start();
        Input::FeedKeys({'d'});
        for (int i = 0; i < 30; ++i) s.Update(1.0f / 60.0f); // 0.5s
        CHECK(o->transform->localPosition.x > 1.5f);
        CHECK_NEAR(rb->velocity.x, 4.0f, 1e-3f);
        Input::FeedKeys({});
    }

    // Platformer: a grounded body jumps when space is pressed.
    {
        Scene s("P"); s.physicsEnabled = true;
        GameObject* o = s.CreateGameObject("Hero");
        auto* rb = o->AddComponent<Rigidbody2D>();
        rb->gravityScale = 0.0f;          // keep vy ~0 so the jump condition holds
        auto* cc = o->AddComponent<CharacterController2D>();
        cc->mode = CharacterController2D::Mode::Platformer; cc->jumpForce = 7.0f;
        s.Start();
        Input::FeedKeys({' '});           // space-down edge
        s.Update(1.0f / 60.0f);
        CHECK_NEAR(rb->velocity.y, 7.0f, 1e-3f);
        Input::FeedKeys({});
    }

    // Serialization round-trips the mode + speed + jump force.
    {
        Scene s("S"); s.physicsEnabled = false;
        GameObject* o = s.CreateGameObject("C");
        auto* cc = o->AddComponent<CharacterController2D>();
        cc->mode = CharacterController2D::Mode::Platformer; cc->speed = 8.5f; cc->jumpForce = 12.0f;
        cc->runSpeed = 11.0f; cc->maxJumps = 2; cc->coyoteTime = 0.12f; cc->airControl = 0.5f;
        std::string txt = SceneSerializer::Serialize(s);
        Scene s2("x"); SceneSerializer::Deserialize(s2, txt);
        auto* c2 = s2.Find("C") ? s2.Find("C")->GetComponent<CharacterController2D>() : nullptr;
        CHECK(c2 != nullptr);
        if (c2) {
            CHECK(c2->mode == CharacterController2D::Mode::Platformer);
            CHECK_NEAR(c2->speed, 8.5f, 1e-4f);
            CHECK_NEAR(c2->jumpForce, 12.0f, 1e-4f);
            CHECK_NEAR(c2->runSpeed, 11.0f, 1e-4f);   // extended fields round-trip
            CHECK(c2->maxJumps == 2);
            CHECK_NEAR(c2->coyoteTime, 0.12f, 1e-4f);
            CHECK_NEAR(c2->airControl, 0.5f, 1e-4f);
        }
    }

    // 3D controller: 'w' drives forward (+Z) velocity; space jumps when grounded.
    {
        Scene s("3"); s.physicsEnabled = true;
        GameObject* o = s.CreateGameObject("P3");
        auto* rb = o->AddComponent<Rigidbody3D>();
        rb->gravityScale = 0.0f;
        auto* cc = o->AddComponent<CharacterController3D>();
        cc->speed = 5.0f; cc->jumpForce = 6.0f;
        s.Start();
        Input::FeedKeys({'w'});
        for (int i = 0; i < 30; ++i) s.Update(1.0f / 60.0f);   // momentum ramps up to speed
        CHECK_NEAR(rb->velocity.z, 5.0f, 1e-2f);
        Input::FeedKeys({'w', ' '});
        s.Update(1.0f / 60.0f);
        CHECK_NEAR(rb->velocity.y, 6.0f, 1e-3f);
        Input::FeedKeys({});

        std::string txt = SceneSerializer::Serialize(s);
        Scene s2("y"); SceneSerializer::Deserialize(s2, txt);
        auto* c2 = s2.Find("P3") ? s2.Find("P3")->GetComponent<CharacterController3D>() : nullptr;
        CHECK(c2 != nullptr);
        if (c2) CHECK_NEAR(c2->jumpForce, 6.0f, 1e-4f);
    }

    // Rotating platform: a rider is carried round the pivot and told how far it turned.
    {
        Scene s("R"); s.physicsEnabled = true;
        GameObject* plat = s.CreateGameObject("Turntable");
        plat->AddComponent<BoxCollider3D>()->size = {10.0f, 1.0f, 10.0f};
        auto* prb = plat->AddComponent<Rigidbody3D>();
        prb->bodyType = Rigidbody3D::BodyType::Kinematic;
        GameObject* rider = s.CreateGameObject("Rider");
        rider->transform->SetPosition({2.0f, 0.5f, 0.0f});
        s.Start();
        PlatformRide ride;
        RideMovingPlatform(s, rider, ride, true);            // latch on
        CHECK(ride.platform == plat);
        plat->transform->SetRotation(Quat::AngleAxis(90.0f, Vec3::Up));
        RideMovingPlatform(s, rider, ride, true);
        Vec3 p = rider->transform->Position();
        CHECK_NEAR(ride.appliedYaw, 90.0f, 0.01f);
        CHECK_NEAR(p.x, 0.0f, 0.01f);
        CHECK_NEAR(std::fabs(p.z), 2.0f, 0.01f);             // same radius, a quarter turn on
        RideMovingPlatform(s, rider, ride, true);            // no further turn
        CHECK_NEAR(ride.appliedYaw, 0.0f, 1e-3f);
        CHECK_NEAR(TwistYawDeg(Quat{0.0f, -0.0087265f, 0.0f, -0.99996f}), 1.0f, 0.01f);   // -q is the same turn: 1 degree, not 359
    }

    TEST_MAIN_RESULT();
}
