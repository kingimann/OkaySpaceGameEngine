#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

namespace {
struct CollisionProbe : Behaviour {
    int enter = 0, stay = 0, exit = 0;
    int tEnter = 0, tStay = 0, tExit = 0;
    void OnCollisionEnter2D(const Collision2D&) override { ++enter; }
    void OnCollisionStay2D(const Collision2D&) override { ++stay; }
    void OnCollisionExit2D(const Collision2D&) override { ++exit; }
    void OnTriggerEnter2D(Collider2D*) override { ++tEnter; }
    void OnTriggerStay2D(Collider2D*) override { ++tStay; }
    void OnTriggerExit2D(Collider2D*) override { ++tExit; }
};
}

int main() {
    RUN_SUITE("physics");

    // --- Gravity integration ---
    {
        Scene scene("Gravity");
        GameObject* go = scene.CreateGameObject("FallingBody");
        auto* rb = go->AddComponent<Rigidbody2D>();
        rb->gravityScale = 1.0f;
        scene.Start();
        for (int i = 0; i < 60; ++i) scene.Update(1.0f / 60.0f); // ~1 second
        CHECK(go->transform->localPosition.y < -4.0f); // ~ -4.9
        CHECK(rb->velocity.y < 0.0f);
    }

    // --- A falling box rests on a static floor (collision resolution) ---
    {
        Scene scene("Resting");
        GameObject* floor = scene.CreateGameObject("Floor");
        auto* fc = floor->AddComponent<BoxCollider2D>();
        fc->size = {20.0f, 1.0f}; // top at y = 0.5

        GameObject* box = scene.CreateGameObject("Box");
        box->transform->localPosition = {0.0f, 5.0f, 0.0f};
        box->AddComponent<BoxCollider2D>()->size = {1.0f, 1.0f};
        auto* rb = box->AddComponent<Rigidbody2D>();
        rb->gravityScale = 1.0f;
        auto* probe = box->AddComponent<CollisionProbe>();

        scene.Start();
        for (int i = 0; i < 600; ++i) scene.Update(1.0f / 60.0f); // 10 seconds

        // Box half-height 0.5 resting on floor top 0.5 => center near y = 1.0.
        CHECK(box->transform->localPosition.y > 0.9f);
        CHECK(box->transform->localPosition.y < 1.2f);
        CHECK(probe->enter >= 1);  // it landed
        CHECK(probe->stay  >= 1);  // and kept resting
    }

    // --- Trigger enter/exit as a body passes through ---
    {
        Scene scene("Trigger");
        GameObject* zone = scene.CreateGameObject("Zone");
        auto* zc = zone->AddComponent<BoxCollider2D>();
        zc->size = {2.0f, 2.0f};
        zc->isTrigger = true;
        auto* probe = zone->AddComponent<CollisionProbe>();

        GameObject* mover = scene.CreateGameObject("Mover");
        mover->transform->localPosition = {-5.0f, 0.0f, 0.0f};
        mover->AddComponent<BoxCollider2D>()->size = {1.0f, 1.0f};
        auto* rb = mover->AddComponent<Rigidbody2D>();
        rb->bodyType = Rigidbody2D::BodyType::Kinematic;
        rb->velocity = {4.0f, 0.0f}; // moves right through the zone

        scene.Start();
        for (int i = 0; i < 180; ++i) scene.Update(1.0f / 60.0f); // 3 seconds, x: -5 -> +7

        CHECK(probe->tEnter == 1);
        CHECK(probe->tExit  == 1);
        CHECK(probe->tStay  >= 1);
        // Triggers must not push the mover off its path.
        CHECK(mover->transform->localPosition.y == 0.0f);
    }

    TEST_MAIN_RESULT();
}
