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
        std::string txt = SceneSerializer::Serialize(s);
        Scene s2("x"); SceneSerializer::Deserialize(s2, txt);
        auto* c2 = s2.Find("C") ? s2.Find("C")->GetComponent<CharacterController2D>() : nullptr;
        CHECK(c2 != nullptr);
        if (c2) {
            CHECK(c2->mode == CharacterController2D::Mode::Platformer);
            CHECK_NEAR(c2->speed, 8.5f, 1e-4f);
            CHECK_NEAR(c2->jumpForce, 12.0f, 1e-4f);
        }
    }

    TEST_MAIN_RESULT();
}
