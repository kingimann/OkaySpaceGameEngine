// Character push-out, joints linked by object, shape casts that report every hit, closest point.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Physics/PlayerCollision.hpp"
#include <cmath>

using namespace okay;

namespace {
void Floor(Scene& s) {
    auto* g = s.CreateGameObject("Ground");
    g->transform->localPosition = {0, -0.5f, 0};
    g->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
    g->AddComponent<BoxCollider3D>()->size = {40, 1, 40};
}
} // namespace

int main() {
    // A tall character (a box 0.6 x 1.8 x 0.6) under a low ceiling. One sphere around the waist
    // never reaches the ceiling, so the head stays inside it. A capsule fitted to the box does not.
    RUN_SUITE("character capsule push-out");
    {
        Scene s("ceiling");
        auto* player = s.CreateGameObject("Player");
        player->transform->localPosition = {0, 0.9f, 0};
        player->AddComponent<BoxCollider3D>()->size = {0.6f, 1.8f, 0.6f};
        auto* ceil = s.CreateGameObject("Ceiling");
        ceil->transform->localPosition = {0, 1.7f, 0};
        ceil->AddComponent<BoxCollider3D>()->size = {4, 0.4f, 4};   // bottom at y = 1.5, head was at 1.8
        const float before = player->transform->Position().y;
        ResolvePlayerBody(s, player);
        CHECK(player->transform->Position().y < before - 0.15f);
        CHECK(player->transform->Position().y < 0.75f);

        // A cube still uses the waist sphere and is pushed out of a wall it overlaps.
        Scene w("cube");
        auto* cube = w.CreateGameObject("Cube");
        cube->transform->localPosition = {0.4f, 0.5f, 0};
        cube->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
        auto* wall = w.CreateGameObject("Wall");
        wall->transform->localPosition = {1.0f, 0.5f, 0};
        wall->AddComponent<BoxCollider3D>()->size = {1, 2, 2};
        ResolvePlayerBody(w, cube);
        CHECK(cube->transform->Position().x < 0.4f);
    }

    // Two objects share a name. The joint is tied to the second one by object, not to the first
    // one the name would find, and not to the world origin.
    RUN_SUITE("joint linked by object");
    {
        Scene s("link");
        auto* first = s.CreateGameObject("Post");
        first->transform->localPosition = {0, 0, 0};
        first->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
        auto* second = s.CreateGameObject("Post");
        second->transform->localPosition = {4, 0, 0};
        second->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
        auto* weight = s.CreateGameObject("Weight");
        weight->transform->localPosition = {4, 2, 0};
        auto* rb = weight->AddComponent<Rigidbody3D>();
        rb->gravityScale = 0.0f;
        rb->velocity = {-3, 0, 0};
        auto* j = weight->AddComponent<Joint3D>();
        j->mode = (int)Joint3D::Mode::Distance;
        j->autoConfigure = false;
        j->distance = 0.5f;
        j->anchor = {0, 0, 0};
        j->ConnectTo(second);
        s.Start();
        for (int i = 0; i < 120; ++i) s.Update(1.0f / 60.0f);
        CHECK(weight->transform->Position().x > 3.0f);
    }

    // A sphere cast through two walls reports both, the nearer one first.
    RUN_SUITE("shape cast all hits");
    {
        Scene s("casts");
        auto* near = s.CreateGameObject("Near");
        near->transform->localPosition = {2, 0, 0};
        near->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
        auto* far = s.CreateGameObject("Far");
        far->transform->localPosition = {6, 0, 0};
        far->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
        std::vector<RaycastHit3D> hits = s.physics3D().SphereCastAll(s, {0, 0, 0}, {1, 0, 0}, 0.2f, 20.0f);
        CHECK(hits.size() == 2);
        CHECK(hits[0].hit && hits[0].gameObject == near);
        CHECK(hits[1].gameObject == far);
        CHECK(hits[0].distance < hits[1].distance);
        RaycastHit3D only = s.physics3D().SphereCast(s, {0, 0, 0}, {1, 0, 0}, 0.2f, 20.0f);
        CHECK(only.gameObject == near);

        std::vector<RaycastHit3D> boxes = s.physics3D().BoxCastAll(s, {0, 0, 0}, {0.2f, 0.2f, 0.2f}, {1, 0, 0});
        CHECK(boxes.size() == 2 && boxes[0].gameObject == near);
        std::vector<RaycastHit3D> caps = s.physics3D().CapsuleCastAll(s, {-0.1f, 0, 0}, {0.1f, 0, 0}, 0.2f, {1, 0, 0}, 20.0f);
        CHECK(caps.size() == 2 && caps[0].gameObject == near);
    }

    // Closest point on a box sits on the face, not at the centre.
    RUN_SUITE("closest point");
    {
        Scene s("nearpt");
        auto* box = s.CreateGameObject("Box");
        box->AddComponent<BoxCollider3D>()->size = {2, 2, 2};
        NearestSolid3D q = s.physics3D().ClosestPoint(s, {3, 0, 0});
        CHECK(q.hit);
        CHECK(q.gameObject == box);
        CHECK_NEAR(q.point.x, 1.0f, 0.05f);
        CHECK_NEAR(q.point.y, 0.0f, 0.05f);
        CHECK_NEAR(q.distance, 2.0f, 0.05f);
        NearestSolid3D inside = s.physics3D().ClosestPoint(s, {0.2f, 0, 0});
        CHECK(inside.hit && inside.distance < 0.001f);
        auto* trigger = s.CreateGameObject("Trig");
        trigger->transform->localPosition = {3, 0, 0};
        auto* tc = trigger->AddComponent<SphereCollider3D>();
        tc->radius = 0.2f;
        tc->isTrigger = true;
        NearestSolid3D skip = s.physics3D().ClosestPoint(s, {3, 0, 0}, 0.5f);
        CHECK(!skip.hit);   // the trigger is ignored and the box face is 2 units away
    }

    // A 2D circle cast through two boxes reports both, the nearer one first.
    RUN_SUITE("2d shape cast all hits");
    {
        Scene s("cast2d");
        auto* near = s.CreateGameObject("Near");
        near->transform->localPosition = {2, 0, 0};
        near->AddComponent<BoxCollider2D>()->size = {1, 1};
        auto* far = s.CreateGameObject("Far");
        far->transform->localPosition = {6, 0, 0};
        far->AddComponent<BoxCollider2D>()->size = {1, 1};
        std::vector<RaycastHit2D> hits = s.physics().CircleCastAll(s, {0, 0}, {1, 0}, 0.2f, 20.0f);
        CHECK(hits.size() == 2);
        CHECK(hits[0].gameObject == near && hits[1].gameObject == far);
        CHECK(hits[0].distance < hits[1].distance);
        RaycastHit2D only = s.physics().CircleCast(s, {0, 0}, {1, 0}, 0.2f, 20.0f);
        CHECK(only.gameObject == near);
        std::vector<RaycastHit2D> boxes = s.physics().BoxCastAll(s, {0, 0}, {0.2f, 0.2f}, {1, 0});
        CHECK(boxes.size() == 2 && boxes[0].gameObject == near);
    }

    TEST_MAIN_RESULT();
}
