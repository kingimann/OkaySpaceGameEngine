#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>

using namespace okay;

// Friction stops sliding; Joint3D constrains a body to an anchor (distance/spring/pin).
int main() {
    RUN_SUITE("physics_joints");

    // ---- Friction: a sliding box on the ground loses tangential speed ----
    {
        Scene s("fric");
        // Ground: a big static box at y=0.
        auto* ground = s.CreateGameObject("Ground");
        ground->transform->localPosition = {0, -0.5f, 0};
        { auto* rb = ground->AddComponent<Rigidbody3D>(); rb->bodyType = Rigidbody3D::BodyType::Static; }
        { auto* c = ground->AddComponent<BoxCollider3D>(); c->size = {50, 1, 50}; }

        // A box resting on it, given a sideways velocity.
        auto* box = s.CreateGameObject("Box");
        box->transform->localPosition = {0, 0.55f, 0};
        auto* rb = box->AddComponent<Rigidbody3D>();
        rb->friction = 0.8f;
        { auto* c = box->AddComponent<BoxCollider3D>(); c->size = {1, 1, 1}; }
        rb->velocity = {6, 0, 0};

        s.Start();
        for (int i = 0; i < 120; ++i) s.Update(1.0f / 60.0f);   // ~2s
        // Friction should have bled off most of the 6 u/s slide.
        CHECK(std::fabs(rb->velocity.x) < 3.0f);

        // Same setup but frictionless slides much further / faster.
        Scene s2("ice");
        auto* g2 = s2.CreateGameObject("Ground");
        g2->transform->localPosition = {0, -0.5f, 0};
        { auto* r = g2->AddComponent<Rigidbody3D>(); r->bodyType = Rigidbody3D::BodyType::Static; }
        { auto* c = g2->AddComponent<BoxCollider3D>(); c->size = {50, 1, 50}; }
        auto* ice = s2.CreateGameObject("Box");
        ice->transform->localPosition = {0, 0.55f, 0};
        auto* irb = ice->AddComponent<Rigidbody3D>(); irb->friction = 0.0f;
        { auto* c = ice->AddComponent<BoxCollider3D>(); c->size = {1, 1, 1}; }
        irb->velocity = {6, 0, 0};
        s2.Start();
        for (int i = 0; i < 120; ++i) s2.Update(1.0f / 60.0f);
        CHECK(std::fabs(irb->velocity.x) > std::fabs(rb->velocity.x));   // ice keeps more speed
    }

    // ---- Distance joint: a body can't leave a fixed radius from a world anchor ----
    {
        Scene s("dist");
        auto* b = s.CreateGameObject("Bob");
        b->transform->localPosition = {2, 0, 0};
        auto* rb = b->AddComponent<Rigidbody3D>(); rb->gravityScale = 0.0f;
        auto* j = b->AddComponent<Joint3D>();
        j->mode = (int)Joint3D::Mode::Distance;
        j->anchor = {0, 0, 0};
        j->autoConfigure = true;                 // rest length = 2 (current distance)
        rb->velocity = {5, 0, 0};                // try to fly outward
        s.Start();
        for (int i = 0; i < 120; ++i) s.Update(1.0f / 60.0f);
        Vec3 p = b->transform->localPosition;
        float r = std::sqrt(p.x*p.x + p.y*p.y + p.z*p.z);
        CHECK(std::fabs(r - 2.0f) < 0.2f);       // stays on the 2-unit sphere
    }

    // ---- Spring joint: a stretched spring pulls the body back toward rest ----
    {
        Scene s("spring");
        auto* b = s.CreateGameObject("Bob");
        b->transform->localPosition = {5, 0, 0};
        auto* rb = b->AddComponent<Rigidbody3D>(); rb->gravityScale = 0.0f; rb->drag = 1.0f;
        auto* j = b->AddComponent<Joint3D>();
        j->mode = (int)Joint3D::Mode::Spring;
        j->anchor = {0, 0, 0};
        j->autoConfigure = false; j->distance = 1.0f;   // rest length 1, currently stretched to 5
        j->spring = 40.0f; j->damper = 3.0f;
        s.Start();
        for (int i = 0; i < 240; ++i) s.Update(1.0f / 60.0f);   // ~4s to settle
        Vec3 p = b->transform->localPosition;
        float r = std::sqrt(p.x*p.x + p.y*p.y + p.z*p.z);   // distance from the anchor
        CHECK(r < 4.0f);                          // pulled in from the stretched start (5)
        CHECK(std::fabs(r - 1.0f) < 0.6f);        // settles near the rest length

        // Round-trips through the scene file.
        std::string text = SceneSerializer::Serialize(s);
        Scene b2("b2");
        CHECK(SceneSerializer::Deserialize(b2, text));
        auto* lj = b2.Find("Bob") ? b2.Find("Bob")->GetComponent<Joint3D>() : nullptr;
        CHECK(lj && lj->mode == (int)Joint3D::Mode::Spring);
        CHECK(lj && std::fabs(lj->spring - 40.0f) < 1e-3f);
    }

    // ---- Pin: weld a body to a fixed anchor (it stays put under gravity) ----
    {
        Scene s("pin");
        auto* b = s.CreateGameObject("Hook");
        b->transform->localPosition = {0, 5, 0};
        auto* rb = b->AddComponent<Rigidbody3D>();   // gravity on
        auto* j = b->AddComponent<Joint3D>();
        j->mode = (int)Joint3D::Mode::Pin;
        j->anchor = {0, 5, 0};                       // weld where it sits
        s.Start();
        for (int i = 0; i < 120; ++i) s.Update(1.0f / 60.0f);
        Vec3 p = b->transform->localPosition;
        CHECK(std::fabs(p.y - 5.0f) < 0.1f);         // didn't fall
    }

    TEST_MAIN_RESULT();
}
