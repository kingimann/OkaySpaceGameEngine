#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>

using namespace okay;

// Rigidbody3D::AddExplosionForce shoves a body away from a blast centre with linear
// falloff, and leaves bodies outside the radius untouched.
int main() {
    RUN_SUITE("physics_explode");

    Scene s("X");
    auto* go = s.CreateGameObject("Box");
    go->transform->localPosition = {2.0f, 0.0f, 0.0f};
    auto* rb = go->AddComponent<Rigidbody3D>();
    rb->mass = 1.0f;

    rb->AddExplosionForce(10.0f, {0, 0, 0}, 5.0f);   // dist 2, falloff 0.6 -> impulse 6 along +X
    CHECK(rb->velocity.x > 0.0f);                    // pushed away from the centre
    CHECK(std::fabs(rb->velocity.x - 6.0f) < 1e-3f);
    CHECK(std::fabs(rb->velocity.z) < 1e-5f);

    // A body beyond the radius feels nothing.
    auto* far = s.CreateGameObject("Far");
    far->transform->localPosition = {100.0f, 0.0f, 0.0f};
    auto* rbf = far->AddComponent<Rigidbody3D>();
    rbf->AddExplosionForce(10.0f, {0, 0, 0}, 5.0f);
    CHECK(rbf->velocity.x == 0.0f);

    // upModifier biases the blast upward.
    auto* up = s.CreateGameObject("Up");
    up->transform->localPosition = {1.0f, 0.0f, 0.0f};
    auto* rbu = up->AddComponent<Rigidbody3D>();
    rbu->AddExplosionForce(10.0f, {0, 0, 0}, 5.0f, /*upModifier=*/1.0f);
    CHECK(rbu->velocity.y > 0.0f);                   // popped up

    TEST_MAIN_RESULT();
}
