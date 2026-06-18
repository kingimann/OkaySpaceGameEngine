#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("tilemap_collision");

    // --- A solid row merges into a single collider strip ---
    {
        Scene scene("Build");
        GameObject* ground = scene.CreateGameObject("Ground");
        auto* tm = ground->AddComponent<Tilemap>();
        tm->Resize(6, 2);
        for (int x = 0; x < 6; ++x) tm->SetTile(x, 0, 1); // solid bottom row
        auto* tc = ground->AddComponent<TilemapCollider2D>();

        scene.Start(); // Build() runs
        CHECK(tc->ColliderCount() == 1);              // 6 contiguous tiles -> 1 box
        CHECK(scene.Find("TileCollider") != nullptr); // collider object created
    }

    // --- Two separated runs in a row make two colliders ---
    {
        Scene scene("Gaps");
        GameObject* g = scene.CreateGameObject("G");
        auto* tm = g->AddComponent<Tilemap>();
        tm->Resize(5, 1);
        tm->SetTile(0, 0, 1); tm->SetTile(1, 0, 1); // run A
        // gap at x=2
        tm->SetTile(3, 0, 1); tm->SetTile(4, 0, 1); // run B
        auto* tc = g->AddComponent<TilemapCollider2D>();
        scene.Start();
        CHECK(tc->ColliderCount() == 2);
    }

    // --- A falling dynamic body lands on the tilemap instead of passing through ---
    {
        Scene scene("Land");
        GameObject* ground = scene.CreateGameObject("Ground");
        auto* tm = ground->AddComponent<Tilemap>();
        tm->tileSize = 1.0f;
        tm->Resize(10, 1);
        for (int x = 0; x < 10; ++x) tm->SetTile(x, 0, 1); // solid row, top at y=1
        ground->AddComponent<TilemapCollider2D>();

        GameObject* body = scene.CreateGameObject("Body");
        body->transform->localPosition = {5, 5, 0};
        auto* rb = body->AddComponent<Rigidbody2D>();
        rb->bodyType = Rigidbody2D::BodyType::Dynamic;
        auto* bc = body->AddComponent<BoxCollider2D>();
        bc->size = {1, 1};

        scene.Start();
        for (int i = 0; i < 240; ++i) scene.Update(1.0f / 60.0f); // ~4 seconds of falling
        // The tile row spans y in [0,1] (top = 1.0); the body (half-height 0.5)
        // should rest around y = 1.5 and never sink far below it.
        CHECK(body->transform->localPosition.y > 1.0f);
        CHECK(body->transform->localPosition.y < 2.5f);
    }

    // --- Tilemap + collider survive serialization (so built games keep them) ---
    {
        Scene scene("Ser");
        GameObject* g = scene.CreateGameObject("Level");
        auto* tm = g->AddComponent<Tilemap>();
        tm->tileSize = 0.5f;
        tm->Resize(3, 2);
        tm->SetTile(1, 1, 7);
        g->AddComponent<TilemapCollider2D>();

        std::string text = SceneSerializer::Serialize(scene);
        Scene loaded("L");
        std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));
        GameObject* r = loaded.Find("Level");
        auto* rtm = r->GetComponent<Tilemap>();
        CHECK(rtm != nullptr);
        CHECK(rtm->Width() == 3 && rtm->Height() == 2);
        CHECK_NEAR(rtm->tileSize, 0.5f, 0.001f);
        CHECK(rtm->GetTile(1, 1) == 7);
        CHECK(r->GetComponent<TilemapCollider2D>() != nullptr);
    }

    TEST_MAIN_RESULT();
}
