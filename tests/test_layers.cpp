#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

namespace {
struct Probe : Behaviour {
    int enter = 0;
    void OnCollisionEnter2D(const Collision2D&) override { ++enter; }
};
}

int main() {
    RUN_SUITE("physics_layers");

    Scene scene("L");

    GameObject* a = scene.CreateGameObject("A");
    a->AddComponent<BoxCollider2D>()->size = {1, 1};
    a->GetComponent<BoxCollider2D>()->layer = 1;
    a->AddComponent<Rigidbody2D>()->bodyType = Rigidbody2D::BodyType::Static;
    auto* pa = a->AddComponent<Probe>();

    GameObject* b = scene.CreateGameObject("B");
    b->AddComponent<BoxCollider2D>()->size = {1, 1};
    b->GetComponent<BoxCollider2D>()->layer = 2;
    b->AddComponent<Rigidbody2D>();

    // Default: layers 1 and 2 collide -> overlapping boxes report a contact.
    scene.Start();
    scene.Update(0.016f);
    CHECK(pa->enter >= 1);

    // Disable collisions between layers 1 and 2: no new contacts.
    Scene scene2("L2");
    scene2.physics().SetLayerCollision(1, 2, false);
    GameObject* a2 = scene2.CreateGameObject("A");
    a2->AddComponent<BoxCollider2D>()->size = {1, 1};
    a2->GetComponent<BoxCollider2D>()->layer = 1;
    auto* pa2 = a2->AddComponent<Probe>();
    GameObject* b2 = scene2.CreateGameObject("B");
    b2->AddComponent<BoxCollider2D>()->size = {1, 1};
    b2->GetComponent<BoxCollider2D>()->layer = 2;
    scene2.Start();
    for (int i = 0; i < 5; ++i) scene2.Update(0.016f);
    CHECK(pa2->enter == 0);

    CHECK(scene2.physics().LayersCollide(1, 2) == false);
    CHECK(scene2.physics().LayersCollide(3, 4) == true);

    TEST_MAIN_RESULT();
}
