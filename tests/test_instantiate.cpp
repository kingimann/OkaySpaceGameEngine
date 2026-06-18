#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("instantiate");

    // --- Serializer now covers 3D + physics components ---
    {
        Scene scene("S");
        GameObject* go = scene.CreateGameObject("Crate");
        auto* mr = go->AddComponent<MeshRenderer>();
        mr->mesh = Mesh::Pyramid();
        mr->color = Color::Red;
        auto* rb = go->AddComponent<Rigidbody2D>();
        rb->bodyType = Rigidbody2D::BodyType::Kinematic;
        rb->gravityScale = 0.5f;
        go->AddComponent<BoxCollider2D>()->size = {3.0f, 2.0f};

        std::string text = SceneSerializer::Serialize(scene);
        Scene loaded("L");
        CHECK(SceneSerializer::Deserialize(loaded, text));
        GameObject* lc = loaded.Find("Crate");
        CHECK(lc != nullptr);
        if (lc) {
            auto* lmr = lc->GetComponent<MeshRenderer>();
            CHECK(lmr && lmr->mesh.name == "Pyramid");
            CHECK(lmr && lmr->mesh.TriangleCount() == Mesh::Pyramid().TriangleCount());
            auto* lrb = lc->GetComponent<Rigidbody2D>();
            CHECK(lrb && lrb->bodyType == Rigidbody2D::BodyType::Kinematic);
            CHECK(lrb && Mathf::Approximately(lrb->gravityScale, 0.5f));
            auto* lbc = lc->GetComponent<BoxCollider2D>();
            CHECK(lbc && Mathf::Approximately(lbc->size.x, 3.0f));
        }
    }

    // --- Instantiate (prefab clone, including a child hierarchy) ---
    {
        Scene scene("World");
        GameObject* prefab = scene.CreateGameObject("Enemy");
        prefab->AddComponent<SpriteRenderer>()->color = Color::Magenta;
        GameObject* gun = scene.CreateGameObject("Gun");
        gun->transform->SetParent(prefab->transform);
        gun->transform->localPosition = {1, 0, 0};
        gun->AddComponent<SpriteRenderer>()->glyph = 'x';

        std::size_t before = scene.Objects().size();
        GameObject* clone = scene.Instantiate(*prefab, {5, 5, 0});
        CHECK(clone != nullptr);
        CHECK(scene.Objects().size() == before + 2); // enemy + gun cloned

        if (clone) {
            CHECK(clone->name == "Enemy");
            CHECK(clone != prefab);
            CHECK(clone->GetComponent<SpriteRenderer>() != nullptr);
            CHECK(clone->transform->localPosition == Vec3(5, 5, 0));
            CHECK(clone->transform->ChildCount() == 1);
            if (clone->transform->ChildCount() == 1) {
                GameObject* childClone = clone->transform->Children()[0]->gameObject;
                CHECK(childClone->name == "Gun");
                CHECK(childClone->transform->localPosition == Vec3(1, 0, 0));
            }
        }
    }

    TEST_MAIN_RESULT();
}
