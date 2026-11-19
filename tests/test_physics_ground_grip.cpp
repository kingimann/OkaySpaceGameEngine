// Characters (and vehicles) read the grip of the Physics Material under them: a running character stops quickly on plain
// ground and slides on ice.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/CharacterController3D.hpp"
#include "okay/Physics/GroundGrip.hpp"
#include <cmath>

using namespace okay;

static float SlideDistance(const char* floorMaterial) {
    Scene s("grip"); s.physics3D().quality = PhysicsQuality::Improved;
    GameObject* f = s.CreateGameObject("Floor"); f->transform->localPosition = {0, -0.5f, 0}; f->AddComponent<BoxCollider3D>()->size = {400, 1, 400};
    if (floorMaterial) AssignPhysicsMaterial(*f, floorMaterial);
    GameObject* p = s.CreateGameObject("Player"); p->transform->localPosition = {0, 1.0f, 0};
    auto* cap = p->AddComponent<CapsuleCollider3D>(); cap->radius = 0.4f; cap->height = 1.8f;
    auto* rb = p->AddComponent<Rigidbody3D>(); rb->freezeRotation = true;
    auto* cc = p->AddComponent<CharacterController3D>();
    (void)cc;
    s.Start();
    for (int i = 0; i < 40; ++i) s.physics3D().Step(s, 0.02f);          // settle on the floor
    rb->velocity.x = 6.0f;
    const float x0 = p->transform->Position().x;
    PlayerInput none;
    for (int i = 0; i < 150; ++i) { cc->Simulate(none, 0.02f); s.physics3D().Step(s, 0.02f); }
    return p->transform->Position().x - x0;
}

int main() {
    RUN_SUITE("physics_ground_grip");
    {
        Scene s("g"); s.physics3D().quality = PhysicsQuality::Improved;
        GameObject* f = s.CreateGameObject("Floor"); f->transform->localPosition = {0, -0.5f, 0}; f->AddComponent<BoxCollider3D>()->size = {40, 1, 40};
        GameObject* p = s.CreateGameObject("P"); p->transform->localPosition = {0, 0.9f, 0}; p->AddComponent<CapsuleCollider3D>();
        CHECK_NEAR(GroundGripScale(p), 1.0, 1e-5);                       // no material
        AssignPhysicsMaterial(*f, "Ice");  CHECK(GroundGripScale(p) < 0.1f);
        AssignPhysicsMaterial(*f, "Rubber"); CHECK(GroundGripScale(p) > 1.5f);
    }
    const float plain = SlideDistance(nullptr), ice = SlideDistance("Ice");
    std::cout << "  running at 6 m/s then letting go: slides " << plain << " m on plain ground, " << ice << " m on ice\n";
    CHECK(plain < 4.0f);
    CHECK(ice > plain * 3.0f);
    TEST_MAIN_RESULT();
}
