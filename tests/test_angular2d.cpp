#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>

using namespace okay;

// 2D angular dynamics: torque, off-center impulses, contact-driven spin, and the
// freeze-rotation default that keeps classic non-spinning behavior.
int main() {
    RUN_SUITE("angular2d");

    // ---- AddTorque spins a body that has rotation enabled ----
    {
        Scene s("torque");
        auto* go = s.CreateGameObject("Box");
        auto* rb = go->AddComponent<Rigidbody2D>();
        rb->gravityScale = 0.0f; rb->freezeRotation = false; rb->angularDrag = 0.0f;
        auto* c = go->AddComponent<BoxCollider2D>(); c->size = {1, 1};
        s.Start();
        rb->AddTorque(5.0f);
        s.Update(1.0f / 60.0f);
        CHECK(rb->angularVelocity > 0.0f);            // started spinning
        float before = go->transform->localRotation.ToEuler().z;
        for (int i = 0; i < 60; ++i) s.Update(1.0f / 60.0f);
        float after = go->transform->localRotation.ToEuler().z;
        CHECK(std::fabs(Mathf::DeltaAngle(before, after)) > 1.0f);   // rotation advanced
    }

    // ---- Frozen by default: torque does nothing, no rotation ----
    {
        Scene s("frozen");
        auto* go = s.CreateGameObject("Box");
        auto* rb = go->AddComponent<Rigidbody2D>();   // freezeRotation defaults TRUE
        rb->gravityScale = 0.0f;
        auto* c = go->AddComponent<BoxCollider2D>(); c->size = {1, 1};
        s.Start();
        rb->AddTorque(50.0f);
        for (int i = 0; i < 60; ++i) s.Update(1.0f / 60.0f);
        CHECK(rb->angularVelocity == 0.0f);
        CHECK(std::fabs(go->transform->localRotation.ToEuler().z) < 1e-3f);
    }

    // ---- Angular drag bleeds off spin over time ----
    {
        Scene s("angdrag");
        auto* go = s.CreateGameObject("Box");
        auto* rb = go->AddComponent<Rigidbody2D>();
        rb->gravityScale = 0.0f; rb->freezeRotation = false; rb->angularDrag = 2.0f;
        rb->angularVelocity = 200.0f;
        auto* c = go->AddComponent<BoxCollider2D>(); c->size = {1, 1};
        s.Start();
        for (int i = 0; i < 120; ++i) s.Update(1.0f / 60.0f);
        CHECK(rb->angularVelocity < 100.0f);          // damped well below the start
        CHECK(rb->angularVelocity > 0.0f);            // same sign, not reversed
    }

    // ---- AddForceAtPosition: an off-center push produces spin; centered doesn't ----
    {
        Scene s("offcenter");
        auto* go = s.CreateGameObject("Box");
        go->transform->localPosition = {0, 0, 0};
        auto* rb = go->AddComponent<Rigidbody2D>();
        rb->gravityScale = 0.0f; rb->freezeRotation = false; rb->angularDrag = 0.0f;
        auto* c = go->AddComponent<BoxCollider2D>(); c->size = {1, 1};
        s.Start();
        rb->AddForceAtPosition({0, 10}, {0.5f, 0});   // upward push at the right edge
        s.Update(1.0f / 60.0f);
        CHECK(rb->angularVelocity != 0.0f);           // torque from the lever arm
        CHECK(rb->velocity.y > 0.0f);                 // and it still moves up

        // Centered push: pure translation, no spin.
        Scene s2("centered");
        auto* g2 = s2.CreateGameObject("Box");
        auto* r2 = g2->AddComponent<Rigidbody2D>();
        r2->gravityScale = 0.0f; r2->freezeRotation = false; r2->angularDrag = 0.0f;
        auto* c2 = g2->AddComponent<BoxCollider2D>(); c2->size = {1, 1};
        s2.Start();
        r2->AddForceAtPosition({0, 10}, {0, 0});      // through the center
        s2.Update(1.0f / 60.0f);
        CHECK(std::fabs(r2->angularVelocity) < 1e-4f);
    }

    // ---- Contact-driven spin: a box landing off-center on a narrow pedestal
    //      is supported on only one side, so gravity topples it (real torque from
    //      an off-center contact — unlike a head-on hit, which stays square). ----
    {
        Scene s("tip");
        // A narrow static pedestal under the box's RIGHT side only.
        auto* ped = s.CreateGameObject("Pedestal");
        ped->transform->localPosition = {0.45f, 0, 0};
        { auto* r = ped->AddComponent<Rigidbody2D>(); r->bodyType = Rigidbody2D::BodyType::Static; }
        { auto* c = ped->AddComponent<BoxCollider2D>(); c->size = {0.4f, 0.4f}; }

        auto* box = s.CreateGameObject("Box");
        box->transform->localPosition = {0, 0.9f, 0};
        auto* rb = box->AddComponent<Rigidbody2D>();
        rb->freezeRotation = false; rb->angularDrag = 0.0f;   // gravity on (default scale 1)
        { auto* c = box->AddComponent<BoxCollider2D>(); c->size = {1, 1}; }
        s.Start();
        for (int i = 0; i < 180; ++i) s.Update(1.0f / 60.0f);
        CHECK(std::fabs(rb->angularVelocity) > 1.0f);   // tipped off the edge -> spinning
    }

    // ---- Angular fields round-trip through the scene file ----
    {
        Scene s("ser");
        auto* go = s.CreateGameObject("B");
        auto* rb = go->AddComponent<Rigidbody2D>();
        rb->freezeRotation = false; rb->angularDrag = 1.25f; rb->friction = 0.6f;
        std::string text = SceneSerializer::Serialize(s);
        Scene s2("s2");
        CHECK(SceneSerializer::Deserialize(s2, text));
        auto* l = s2.Find("B")->GetComponent<Rigidbody2D>();
        CHECK(l && !l->freezeRotation);
        CHECK(l && std::fabs(l->angularDrag - 1.25f) < 1e-3f);
        CHECK(l && std::fabs(l->friction - 0.6f) < 1e-3f);
    }

    TEST_MAIN_RESULT();
}
