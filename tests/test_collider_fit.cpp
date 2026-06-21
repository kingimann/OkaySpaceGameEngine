#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("collider_fit");

    // 2D: a box collider fits the sprite's size.
    {
        Scene s("Fit2D");
        GameObject* o = s.CreateGameObject("Spr");
        o->AddComponent<SpriteRenderer>()->size = {3.0f, 2.0f};
        auto* bc = o->AddComponent<BoxCollider2D>();   // default 1x1
        FitColliders(o);
        CHECK_NEAR(bc->size.x, 3.0f, 1e-4f);
        CHECK_NEAR(bc->size.y, 2.0f, 1e-4f);
    }

    // 2D circle fits to the larger half-extent.
    {
        Scene s("FitCircle");
        GameObject* o = s.CreateGameObject("Spr");
        o->AddComponent<SpriteRenderer>()->size = {4.0f, 2.0f};
        auto* cc = o->AddComponent<CircleCollider2D>();
        FitColliders(o);
        CHECK_NEAR(cc->radius, 2.0f, 1e-4f);           // max(4,2)/2
    }

    // 3D: a box collider fits the cube mesh bounds (1x1x1).
    {
        Scene s("Fit3D");
        GameObject* o = s.CreateGameObject("Cube");
        o->AddComponent<MeshRenderer>()->mesh = Mesh::Cube();
        auto* bc = o->AddComponent<BoxCollider3D>();
        FitColliders(o);
        CHECK_NEAR(bc->size.x, 1.0f, 1e-3f);
        CHECK_NEAR(bc->size.y, 1.0f, 1e-3f);
        CHECK_NEAR(bc->size.z, 1.0f, 1e-3f);
    }

    // autoFit re-fits during Scene::Update when the sprite changes size.
    {
        Scene s("AutoFit");
        GameObject* o = s.CreateGameObject("Spr");
        auto* sr = o->AddComponent<SpriteRenderer>(); sr->size = {1.0f, 1.0f};
        auto* bc = o->AddComponent<BoxCollider2D>();
        bc->autoFit = true;
        s.Start();
        sr->size = {5.0f, 3.0f};
        s.Update(0.016f);                               // auto-fit pass runs
        CHECK_NEAR(bc->size.x, 5.0f, 1e-4f);
        CHECK_NEAR(bc->size.y, 3.0f, 1e-4f);
    }

    // autoFit survives a serialize round-trip.
    {
        Scene s("Persist");
        GameObject* o = s.CreateGameObject("Spr");
        o->AddComponent<SpriteRenderer>()->size = {2.0f, 2.0f};
        o->AddComponent<BoxCollider2D>()->autoFit = true;
        std::string txt = SceneSerializer::Serialize(s);
        Scene s2("x"); SceneSerializer::Deserialize(s2, txt);
        auto* bc = s2.Find("Spr") ? s2.Find("Spr")->GetComponent<BoxCollider2D>() : nullptr;
        CHECK(bc != nullptr);
        if (bc) CHECK(bc->autoFit == true);
    }

    TEST_MAIN_RESULT();
}
