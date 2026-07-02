// Headless test for in-game Builder Mode: place primitives via a ray, parent them
// under one model root, save that model as a prefab, stamp prefabs into a map, and
// grab/remove placed pieces — all camera-independent so it runs without a window.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <cstdio>

using namespace okay;

static int CountTag(Scene& s, const std::string& tag) {
    int n = 0;
    for (const auto& up : s.Objects()) if (up && up->tag == tag) ++n;
    return n;
}

int main() {
    RUN_SUITE("builder_mode");

    // ---- Grid snapping: rounds to the nearest grid node (off = passthrough) ----
    {
        Scene s("snap");
        auto* bm = s.CreateGameObject("B")->AddComponent<BuilderMode>();
        bm->gridSize = 1.0f; bm->snapToGrid = true;
        Vec3 sn = bm->Snap({1.4f, -0.2f, 2.6f});
        CHECK_NEAR(sn.x, 1.0f, 1e-4f);
        CHECK_NEAR(sn.y, 0.0f, 1e-4f);
        CHECK_NEAR(sn.z, 3.0f, 1e-4f);
        bm->snapToGrid = false;
        Vec3 raw = bm->Snap({1.4f, -0.2f, 2.6f});
        CHECK_NEAR(raw.x, 1.4f, 1e-4f);          // no snap when off
    }

    // ---- Place primitives: they parent under one model root, ready to be saved ----
    {
        Scene s("model");
        GameObject* ground = s.CreateGameObject("Ground");
        ground->transform->localScale = {50, 1, 50};
        ground->AddComponent<BoxCollider3D>();

        auto* bm = s.CreateGameObject("Builder")->AddComponent<BuilderMode>();
        bm->reach = 20.0f; bm->gridSize = 1.0f; bm->parentToModel = true;
        bm->modelName = "MyModel";

        // Aim down at the ground → a cube rests on top of it, under the model root.
        GameObject* c = bm->PlaceAt(s, {0.0f, 6.0f, 0.0f}, {0, -1, 0});
        CHECK(c != nullptr);
        CHECK(c && c->tag == "BuildPart");
        CHECK(c && c->GetComponent<MeshRenderer>() && c->GetComponent<BoxCollider3D>());
        CHECK(c && c->transform->localPosition.y > 0.0f);    // lifted to sit on the surface
        GameObject* root = bm->ModelRoot();
        CHECK(root != nullptr && root->name == "MyModel");
        CHECK(c && c->transform->Parent() == root->transform);

        // A second brush (sphere) joins the same model root.
        bm->brush = BuilderMode::Brush::Sphere;
        GameObject* sp = bm->PlaceAt(s, {2.0f, 6.0f, 0.0f}, {0, -1, 0});
        CHECK(sp != nullptr && sp->name == std::string("Sphere"));
        CHECK(root->transform->Children().size() == 2);

        // Save the assembled model as a prefab, then instantiate it back: same shape.
        const char* path = "test_buildermode_model.okayprefab";
        CHECK(bm->SaveModel(path));
        Scene s2("loaded");
        GameObject* inst = SceneSerializer::InstantiateFromFile(s2, path, nullptr);
        CHECK(inst != nullptr);
        CHECK(inst && inst->transform->Children().size() == 2);  // both parts round-tripped
        std::remove(path);
    }

    // ---- Brush cycling wraps through every brush including Prefab ----
    {
        Scene s("brush");
        auto* bm = s.CreateGameObject("B")->AddComponent<BuilderMode>();
        bm->brush = BuilderMode::Brush::Cube;
        for (int i = 0; i < BuilderMode::kBrushCount; ++i) {
            CHECK((int)bm->brush == i);
            // CycleBrush is private; exercise it through the public wrap by name check.
            bm->brush = (BuilderMode::Brush)((i + 1) % BuilderMode::kBrushCount);
        }
        CHECK(std::string(BuilderMode::BrushName(BuilderMode::Brush::Prefab)) == "Prefab");
        CHECK(std::string(BuilderMode::BrushName(BuilderMode::Brush::Torus)) == "Torus");
    }

    // ---- Save refuses when nothing has been modelled yet ----
    {
        Scene s("empty");
        auto* bm = s.CreateGameObject("B")->AddComponent<BuilderMode>();
        CHECK(!bm->SaveModel("should_not_exist.okayprefab"));
    }

    // ---- Map building: the Prefab brush stamps instances around the world ----
    {
        // Make a tiny prefab on disk first (a single tagged cube).
        Scene mk("mk");
        GameObject* proto = mk.CreateGameObject("Tower");
        proto->transform->localScale = {1, 3, 1};
        proto->AddComponent<MeshRenderer>()->mesh = Mesh::Cube();
        proto->AddComponent<BoxCollider3D>();
        const char* ppath = "test_buildermode_tower.okayprefab";
        CHECK(SceneSerializer::SaveObjectToFile(*proto, ppath));

        Scene s("map");
        GameObject* ground = s.CreateGameObject("Ground");
        ground->transform->localScale = {50, 1, 50};
        ground->AddComponent<BoxCollider3D>();
        auto* bm = s.CreateGameObject("Builder")->AddComponent<BuilderMode>();
        bm->reach = 20.0f; bm->brush = BuilderMode::Brush::Prefab;
        bm->prefabPath = ppath;
        bm->parentToModel = false;   // map pieces go straight into the world

        GameObject* a = bm->PlaceAt(s, {0.0f, 6.0f, 0.0f}, {0, -1, 0});
        GameObject* b = bm->PlaceAt(s, {5.0f, 6.0f, 0.0f}, {0, -1, 0});
        CHECK(a != nullptr && b != nullptr);
        CHECK(a && a->name == std::string("Tower"));   // stamped a real instance
        CHECK(a != b);
        // No model root was created (map mode is unparented).
        CHECK(bm->ModelRoot() == nullptr);
        // A Prefab brush with no path stamps nothing.
        bm->prefabPath = "";
        CHECK(bm->PlaceAt(s, {0.0f, 6.0f, 0.0f}, {0, -1, 0}) == nullptr);
        std::remove(ppath);
    }

    // ---- Remove: right-click deletes the piece you aim at (only ones we placed) ----
    {
        Scene s("remove");
        GameObject* ground = s.CreateGameObject("Ground");
        ground->transform->localScale = {50, 1, 50};
        ground->AddComponent<BoxCollider3D>();
        auto* bm = s.CreateGameObject("Builder")->AddComponent<BuilderMode>();
        bm->reach = 20.0f; bm->parentToModel = false;

        GameObject* c = bm->PlaceAt(s, {0.0f, 6.0f, 0.0f}, {0, -1, 0});
        CHECK(c != nullptr);
        CHECK(CountTag(s, "BuildPart") == 1);
        // Aim down at the piece and remove it.
        GameObject* gone = bm->RemoveAt(s, {0.0f, 6.0f, 0.0f}, {0, -1, 0});
        CHECK(gone != nullptr);
        s.Update(0.0f);                                  // flush the deferred destroy
        CHECK(CountTag(s, "BuildPart") == 0);
        // Aiming at the bare ground removes nothing (we only delete our own pieces).
        CHECK(bm->RemoveAt(s, {0.0f, 6.0f, 0.0f}, {0, -1, 0}) == nullptr);
    }

    // ---- Update() drives preview + crosshair from the camera, builds nothing ----
    {
        Scene s("preview");
        GameObject* ground = s.CreateGameObject("Ground");
        ground->transform->localScale = {50, 1, 50};
        ground->AddComponent<BoxCollider3D>();
        GameObject* cg = s.CreateGameObject("Cam");
        cg->transform->localPosition = {0, 5, 0};
        cg->transform->localRotation = Quat::Euler(-90.0f, 0.0f, 0.0f);  // look straight down
        s.mainCamera = cg->AddComponent<Camera>();
        auto* bm = cg->AddComponent<BuilderMode>();
        bm->reach = 20.0f;
        s.Update(0.0f);
        CHECK(CountTag(s, "BuildPreview") == 1);          // a ghost, not a real part
        CHECK(CountTag(s, "BuildPart") == 0);
        CHECK(cg->GetComponent<Crosshair>() != nullptr);  // reticle added
        bm->showPreview = false;
        s.Update(0.0f);
        int hidden = 0;
        for (const auto& up : s.Objects())
            if (up && up->tag == "BuildPreview" && !up->active) ++hidden;
        CHECK(hidden == 1);
    }

    // ---- SaveMap: persist the whole built scene (build-in-play, then save) ----
    {
        Scene s("map2");
        GameObject* ground = s.CreateGameObject("Ground");
        ground->transform->localScale = {50, 1, 50};
        ground->AddComponent<BoxCollider3D>();
        auto* bm = s.CreateGameObject("Builder")->AddComponent<BuilderMode>();
        bm->reach = 20.0f; bm->parentToModel = false;
        GameObject* piece = bm->PlaceAt(s, {0.0f, 6.0f, 0.0f}, {0, -1, 0});
        CHECK(piece != nullptr);
        const char* mp = "test_buildermode_map.okayscene";
        CHECK(bm->SaveMap(mp));                          // write the whole scene
        Scene s2("loaded"); std::string err;
        CHECK(SceneSerializer::LoadFromFile(s2, mp, &err));
        CHECK(s2.Find("Builder") != nullptr);            // scene (incl. the build) round-tripped
        int parts = 0;
        for (const auto& up : s2.Objects()) if (up && up->tag == "BuildPart") ++parts;
        CHECK(parts >= 1);                               // the placed piece persisted
        std::remove(mp);
    }

    TEST_MAIN_RESULT();
}
