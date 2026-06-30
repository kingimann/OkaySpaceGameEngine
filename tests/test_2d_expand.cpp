#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>

using namespace okay;

static bool V2(const Vec2& a, const Vec2& b, float eps = 1e-3f) {
    return std::fabs(a.x - b.x) < eps && std::fabs(a.y - b.y) < eps;
}

// 2D expansion: Vec2 math, contact friction, Joint2D constraints, sprite sorting layers.
int main() {
    RUN_SUITE("2d_expand");

    // ---- Vec2 math (GLM/Unity parity) ----
    CHECK(V2(Vec2::Reflect({1, -1}, {0, 1}), {1, 1}));               // bounce off the floor
    CHECK(V2(Vec2::Perpendicular({1, 0}), {0, 1}));                  // +90° CCW
    CHECK(V2(Vec2::Project({3, 4}, {1, 0}), {3, 0}));
    CHECK(V2(Vec2::ClampMagnitude({3, 4}, 2.5f), {1.5f, 2.0f}));     // len 5 -> 2.5
    CHECK(std::fabs(Vec2::Angle({1, 0}, {0, 1}) - 90.0f) < 1e-2f);
    CHECK(std::fabs(Vec2::SignedAngle({1, 0}, {0, 1}) - 90.0f) < 1e-2f);   // CCW positive
    CHECK(std::fabs(Vec2::SignedAngle({1, 0}, {0, -1}) + 90.0f) < 1e-2f);  // CW negative
    CHECK(std::fabs(Vec2::Cross({1, 0}, {0, 1}) - 1.0f) < 1e-3f);
    CHECK(V2(Vec2::Rotate({1, 0}, 90.0f), {0, 1}));
    CHECK(V2(Vec2::Scale({2, 3}, {5, 6}), {10, 18}));
    CHECK(V2(Vec2::Min({1, 5}, {4, 2}), {1, 2}));
    CHECK(V2(Vec2::Max({1, 5}, {4, 2}), {4, 5}));
    CHECK(V2(Vec2::Abs({-1, -3}), {1, 3}));
    CHECK(V2(Vec2::MoveTowards({0, 0}, {10, 0}, 3.0f), {3, 0}));

    // ---- Contact friction: a box sliding on the ground loses tangential speed ----
    {
        Scene s("fric2d");
        auto* ground = s.CreateGameObject("Ground");
        ground->transform->localPosition = {0, -0.5f, 0};
        { auto* rb = ground->AddComponent<Rigidbody2D>(); rb->bodyType = Rigidbody2D::BodyType::Static; }
        { auto* c = ground->AddComponent<BoxCollider2D>(); c->size = {50, 1}; }

        auto* box = s.CreateGameObject("Box");
        box->transform->localPosition = {0, 0.55f, 0};
        auto* rb = box->AddComponent<Rigidbody2D>();
        rb->friction = 0.8f;
        { auto* c = box->AddComponent<BoxCollider2D>(); c->size = {1, 1}; }
        rb->velocity = {6, 0};

        s.Start();
        for (int i = 0; i < 120; ++i) s.Update(1.0f / 60.0f);   // ~2s
        CHECK(std::fabs(rb->velocity.x) < 3.0f);                // friction bled off the slide

        // Frictionless on ice keeps more of its speed.
        Scene s2("ice2d");
        auto* g2 = s2.CreateGameObject("Ground");
        g2->transform->localPosition = {0, -0.5f, 0};
        { auto* r = g2->AddComponent<Rigidbody2D>(); r->bodyType = Rigidbody2D::BodyType::Static; }
        { auto* c = g2->AddComponent<BoxCollider2D>(); c->size = {50, 1}; }
        auto* ice = s2.CreateGameObject("Box");
        ice->transform->localPosition = {0, 0.55f, 0};
        auto* irb = ice->AddComponent<Rigidbody2D>(); irb->friction = 0.0f;
        { auto* c = ice->AddComponent<BoxCollider2D>(); c->size = {1, 1}; }
        irb->velocity = {6, 0};
        s2.Start();
        for (int i = 0; i < 120; ++i) s2.Update(1.0f / 60.0f);
        CHECK(std::fabs(irb->velocity.x) > std::fabs(rb->velocity.x));
    }

    // ---- Distance joint: body can't leave a fixed radius from a world anchor ----
    {
        Scene s("dist2d");
        auto* b = s.CreateGameObject("Bob");
        b->transform->localPosition = {2, 0, 0};
        auto* rb = b->AddComponent<Rigidbody2D>(); rb->gravityScale = 0.0f;
        auto* j = b->AddComponent<Joint2D>();
        j->mode = (int)Joint2D::Mode::Distance;
        j->anchor = {0, 0};
        j->autoConfigure = true;                 // rest length = 2
        rb->velocity = {5, 0};                    // try to fly outward
        s.Start();
        for (int i = 0; i < 120; ++i) s.Update(1.0f / 60.0f);
        Vec3 p = b->transform->localPosition;
        float r = std::sqrt(p.x * p.x + p.y * p.y);
        CHECK(std::fabs(r - 2.0f) < 0.2f);       // stays on the 2-unit circle
    }

    // ---- Spring joint: a stretched spring pulls the body back toward rest ----
    {
        Scene s("spring2d");
        auto* b = s.CreateGameObject("Bob");
        b->transform->localPosition = {5, 0, 0};
        auto* rb = b->AddComponent<Rigidbody2D>(); rb->gravityScale = 0.0f; rb->drag = 1.0f;
        auto* j = b->AddComponent<Joint2D>();
        j->mode = (int)Joint2D::Mode::Spring;
        j->anchor = {0, 0};
        j->autoConfigure = false; j->distance = 1.0f;   // rest 1, stretched to 5
        j->spring = 40.0f; j->damper = 3.0f;
        s.Start();
        for (int i = 0; i < 240; ++i) s.Update(1.0f / 60.0f);
        Vec3 p = b->transform->localPosition;
        float r = std::sqrt(p.x * p.x + p.y * p.y);
        CHECK(r < 4.0f);                          // pulled in from the stretched start
        CHECK(std::fabs(r - 1.0f) < 0.6f);        // settles near the rest length

        // Round-trips through the scene file.
        std::string text = SceneSerializer::Serialize(s);
        Scene b2("b2");
        CHECK(SceneSerializer::Deserialize(b2, text));
        auto* lj = b2.Find("Bob") ? b2.Find("Bob")->GetComponent<Joint2D>() : nullptr;
        CHECK(lj && lj->mode == (int)Joint2D::Mode::Spring);
        CHECK(lj && std::fabs(lj->spring - 40.0f) < 1e-3f);
    }

    // ---- Pin: weld a body to a fixed anchor (stays put under gravity) ----
    {
        Scene s("pin2d");
        auto* b = s.CreateGameObject("Hook");
        b->transform->localPosition = {0, 5, 0};
        auto* rb = b->AddComponent<Rigidbody2D>();   // gravity on
        auto* j = b->AddComponent<Joint2D>();
        j->mode = (int)Joint2D::Mode::Pin;
        j->anchor = {0, 5};
        s.Start();
        for (int i = 0; i < 120; ++i) s.Update(1.0f / 60.0f);
        Vec3 p = b->transform->localPosition;
        CHECK(std::fabs(p.y - 5.0f) < 0.1f);         // didn't fall
    }

    // ---- Sorting layers: a higher layer always draws on top, regardless of order ----
    {
        SpriteRenderer back, front;
        back.sortingLayer = 0;  back.sortOrder = 999;   // big order, low layer
        front.sortingLayer = 1; front.sortOrder = -999; // tiny order, high layer
        CHECK(front.SortKey() > back.SortKey());        // layer wins over order

        SpriteRenderer lo, hi;
        lo.sortingLayer = 2; lo.sortOrder = 1;
        hi.sortingLayer = 2; hi.sortOrder = 5;
        CHECK(hi.SortKey() > lo.SortKey());             // within a layer, order decides

        // sortingLayer survives a scene round-trip.
        Scene s("sortlayer");
        auto* go = s.CreateGameObject("Spr");
        auto* sr = go->AddComponent<SpriteRenderer>();
        sr->sortingLayer = 3; sr->sortOrder = 7; sr->flipX = true;
        std::string text = SceneSerializer::Serialize(s);
        Scene s2("s2");
        CHECK(SceneSerializer::Deserialize(s2, text));
        auto* l = s2.Find("Spr")->GetComponent<SpriteRenderer>();
        CHECK(l && l->sortingLayer == 3 && l->sortOrder == 7 && l->flipX);
    }

    // ---- Rigidbody2D friction round-trips ----
    {
        Scene s("rb2");
        auto* go = s.CreateGameObject("B");
        auto* rb = go->AddComponent<Rigidbody2D>();
        rb->friction = 0.73f; rb->bounciness = 0.25f;
        std::string text = SceneSerializer::Serialize(s);
        Scene s2("s2");
        CHECK(SceneSerializer::Deserialize(s2, text));
        auto* l = s2.Find("B")->GetComponent<Rigidbody2D>();
        CHECK(l && std::fabs(l->friction - 0.73f) < 1e-3f);
        CHECK(l && std::fabs(l->bounciness - 0.25f) < 1e-3f);
    }

    TEST_MAIN_RESULT();
}
