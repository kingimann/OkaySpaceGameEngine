#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("physics_queries");

    Scene scene("Q");
    scene.physicsEnabled = false;

    // A box at the origin (2x2 -> spans [-1,1]) and a circle at x=6 (r=1).
    GameObject* box = scene.CreateGameObject("Box");
    box->AddComponent<BoxCollider2D>()->size = {2.0f, 2.0f};

    GameObject* circ = scene.CreateGameObject("Circle");
    circ->transform->localPosition = {6.0f, 0.0f, 0.0f};
    circ->AddComponent<CircleCollider2D>()->radius = 1.0f;

    scene.Start();
    Physics2D& phys = scene.physics();

    // Raycast from the left hits the box first at x = -1 (distance 4 from x=-5).
    RaycastHit2D hit = phys.Raycast(scene, {-5.0f, 0.0f}, {1.0f, 0.0f}, 100.0f);
    CHECK(hit);
    CHECK(hit.gameObject == box);
    CHECK_NEAR(hit.point.x, -1.0f, 0.05f);
    CHECK_NEAR(hit.distance, 4.0f, 0.05f);
    CHECK(hit.normal.x < 0.0f); // facing the ray

    // A ray that passes above everything misses.
    CHECK(!phys.Raycast(scene, {-5.0f, 50.0f}, {1.0f, 0.0f}, 100.0f).hit);

    // OverlapPoint (returns the Collider2D under the point).
    Collider2D* atOrigin = phys.OverlapPoint(scene, {0.0f, 0.0f});
    CHECK(atOrigin && atOrigin->gameObject == box);
    Collider2D* atCircle = phys.OverlapPoint(scene, {6.0f, 0.0f});
    CHECK(atCircle && atCircle->gameObject == circ);
    CHECK(phys.OverlapPoint(scene, {3.0f, 0.0f}) == nullptr);

    // OverlapCircle around the origin with radius 1.5 hits only the box.
    auto near = phys.OverlapCircle(scene, {0.0f, 0.0f}, 1.5f);
    CHECK(near.size() == 1);

    // A big overlap box covering both.
    auto both = phys.OverlapBox(scene, {3.0f, 0.0f}, {5.0f, 2.0f});
    CHECK(both.size() == 2);

    TEST_MAIN_RESULT();
}
