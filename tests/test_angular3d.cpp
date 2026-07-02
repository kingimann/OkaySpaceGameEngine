#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>

using namespace okay;

// 3D angular dynamics: torque, off-center impulses, contact-driven spin, and the
// freeze-rotation default that keeps classic non-spinning behavior.
int main() {
    RUN_SUITE("angular3d");

    // ---- AddTorque spins a body that has rotation enabled ----
    {
        Scene s("torque");
        auto* go = s.CreateGameObject("Box");
        auto* rb = go->AddComponent<Rigidbody3D>();
        rb->gravityScale = 0.0f; rb->freezeRotation = false; rb->angularDrag = 0.0f;
        auto* c = go->AddComponent<BoxCollider3D>(); c->size = {1, 1, 1};
        s.Start();
        rb->AddTorque({0, 2, 0});                       // spin about Y
        s.Update(1.0f / 60.0f);
        CHECK(rb->angularVelocity.y > 0.0f);
        Quat before = go->transform->localRotation;
        for (int i = 0; i < 60; ++i) s.Update(1.0f / 60.0f);
        CHECK(Quat::Angle(before, go->transform->localRotation) > 1.0f);   // orientation moved
    }

    // ---- Frozen by default: torque does nothing, no rotation ----
    {
        Scene s("frozen");
        auto* go = s.CreateGameObject("Box");
        auto* rb = go->AddComponent<Rigidbody3D>();     // freezeRotation defaults TRUE
        rb->gravityScale = 0.0f;
        auto* c = go->AddComponent<BoxCollider3D>(); c->size = {1, 1, 1};
        s.Start();
        rb->AddTorque({0, 50, 0});
        for (int i = 0; i < 60; ++i) s.Update(1.0f / 60.0f);
        CHECK(rb->angularVelocity.SqrMagnitude() == 0.0f);
        CHECK(Quat::Angle(Quat::Identity, go->transform->localRotation) < 0.01f);
    }

    // ---- Angular drag bleeds off spin over time ----
    {
        Scene s("angdrag");
        auto* go = s.CreateGameObject("Box");
        auto* rb = go->AddComponent<Rigidbody3D>();
        rb->gravityScale = 0.0f; rb->freezeRotation = false; rb->angularDrag = 2.0f;
        rb->angularVelocity = {0, 10, 0};
        auto* c = go->AddComponent<BoxCollider3D>(); c->size = {1, 1, 1};
        s.Start();
        for (int i = 0; i < 120; ++i) s.Update(1.0f / 60.0f);
        CHECK(rb->angularVelocity.y < 5.0f && rb->angularVelocity.y > 0.0f);   // damped, same sign
    }

    // ---- AddForceAtPosition: an off-center push produces spin; centered doesn't ----
    {
        Scene s("offcenter");
        auto* go = s.CreateGameObject("Box");
        auto* rb = go->AddComponent<Rigidbody3D>();
        rb->gravityScale = 0.0f; rb->freezeRotation = false; rb->angularDrag = 0.0f;
        auto* c = go->AddComponent<BoxCollider3D>(); c->size = {1, 1, 1};
        s.Start();
        rb->AddForceAtPosition({0, 10, 0}, {0.5f, 0, 0});   // up at the +X edge
        s.Update(1.0f / 60.0f);
        CHECK(rb->angularVelocity.SqrMagnitude() > 1e-6f);
        CHECK(rb->velocity.y > 0.0f);

        Scene s2("centered");
        auto* g2 = s2.CreateGameObject("Box");
        auto* r2 = g2->AddComponent<Rigidbody3D>();
        r2->gravityScale = 0.0f; r2->freezeRotation = false; r2->angularDrag = 0.0f;
        auto* c2 = g2->AddComponent<BoxCollider3D>(); c2->size = {1, 1, 1};
        s2.Start();
        r2->AddForceAtPosition({0, 10, 0}, {0, 0, 0});      // through the center
        s2.Update(1.0f / 60.0f);
        CHECK(r2->angularVelocity.SqrMagnitude() < 1e-8f);
    }

    // ---- Contact-driven spin: a box striking a wall off-center starts rotating ----
    {
        Scene s("tip");
        auto* wall = s.CreateGameObject("Wall");
        wall->transform->localPosition = {3, 0, 0};
        { auto* r = wall->AddComponent<Rigidbody3D>(); r->bodyType = Rigidbody3D::BodyType::Static; }
        { auto* c = wall->AddComponent<BoxCollider3D>(); c->size = {1, 6, 6}; }

        auto* box = s.CreateGameObject("Box");
        box->transform->localPosition = {0, 1.5f, 0};
        auto* rb = box->AddComponent<Rigidbody3D>();
        rb->gravityScale = 0.0f; rb->freezeRotation = false; rb->angularDrag = 0.0f;
        rb->velocity = {6, 0, 0};
        { auto* c = box->AddComponent<BoxCollider3D>(); c->size = {1, 1, 1}; }
        s.Start();
        for (int i = 0; i < 120; ++i) s.Update(1.0f / 60.0f);
        CHECK(rb->angularVelocity.SqrMagnitude() > 0.01f);   // the impact set it spinning

        // A centered identical hit should NOT spin (sanity that it's the offset, not noise).
        Scene s2("flat");
        auto* w2 = s2.CreateGameObject("Wall");
        w2->transform->localPosition = {3, 0, 0};
        { auto* r = w2->AddComponent<Rigidbody3D>(); r->bodyType = Rigidbody3D::BodyType::Static; }
        { auto* c = w2->AddComponent<BoxCollider3D>(); c->size = {1, 6, 6}; }
        auto* b2 = s2.CreateGameObject("Box");
        b2->transform->localPosition = {0, 0, 0};            // centered on the wall
        auto* rb2 = b2->AddComponent<Rigidbody3D>();
        rb2->gravityScale = 0.0f; rb2->freezeRotation = false; rb2->angularDrag = 0.0f;
        rb2->velocity = {6, 0, 0};
        { auto* c = b2->AddComponent<BoxCollider3D>(); c->size = {1, 1, 1}; }
        s2.Start();
        for (int i = 0; i < 120; ++i) s2.Update(1.0f / 60.0f);
        CHECK(rb2->angularVelocity.SqrMagnitude() < 0.01f);  // head-on: little to no spin
    }

    // ---- Angular fields round-trip through the scene file ----
    {
        Scene s("ser");
        auto* go = s.CreateGameObject("B");
        auto* rb = go->AddComponent<Rigidbody3D>();
        rb->freezeRotation = false; rb->angularDrag = 1.25f; rb->friction = 0.6f; rb->maxFallSpeed = 30.0f;
        std::string text = SceneSerializer::Serialize(s);
        Scene s2("s2");
        CHECK(SceneSerializer::Deserialize(s2, text));
        auto* l = s2.Find("B")->GetComponent<Rigidbody3D>();
        CHECK(l && !l->freezeRotation);
        CHECK(l && std::fabs(l->angularDrag - 1.25f) < 1e-3f);
        CHECK(l && std::fabs(l->friction - 0.6f) < 1e-3f);
        CHECK(l && std::fabs(l->maxFallSpeed - 30.0f) < 1e-3f);
    }

    TEST_MAIN_RESULT();
}
