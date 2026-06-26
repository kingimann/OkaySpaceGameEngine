// Headless test for the voxel BlockBuilder: grid snapping + place/remove via a ray.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>

using namespace okay;

static int CountBlocks(Scene& s, const std::string& tag) {
    int n = 0;
    for (const auto& up : s.Objects()) if (up && up->tag == tag) ++n;
    return n;
}

int main() {
    RUN_SUITE("block_builder");

    Scene scene("build");
    // A big flat ground with a box collider so the builder ray has something to hit.
    GameObject* ground = scene.CreateGameObject("Ground");
    ground->transform->localPosition = {0, 0, 0};
    ground->transform->localScale = {50, 1, 50};
    ground->AddComponent<BoxCollider3D>();

    GameObject* builderGO = scene.CreateGameObject("Builder");
    auto* bb = builderGO->AddComponent<BlockBuilder>();
    bb->blockSize = 1.0f;
    bb->reach = 20.0f;

    // Grid snap rounds to the nearest cell centre.
    Vec3 sn = bb->Snap(Vec3{1.4f, -0.2f, 2.6f});
    CHECK_NEAR(sn.x, 1.0f, 1e-4f);
    CHECK_NEAR(sn.y, 0.0f, 1e-4f);
    CHECK_NEAR(sn.z, 3.0f, 1e-4f);

    // Default (Minecraft-style): aiming at empty sky places nothing.
    GameObject* refused = bb->Build(scene, Vec3{0.0f, 6.0f, 0.0f}, Vec3{0, 1, 0}, /*place=*/true, false);
    CHECK(refused == nullptr);
    CHECK(CountBlocks(scene, "Block") == 0);

    // With placeInAir on, the same shot lands a block at arm's length, snapped.
    bb->placeInAir = true;
    GameObject* sky = bb->Build(scene, Vec3{0.0f, 6.0f, 0.0f}, Vec3{0, 1, 0}, /*place=*/true, false);
    CHECK(sky != nullptr);
    CHECK(sky && sky->tag == "Block");
    CHECK(CountBlocks(scene, "Block") == 1);
    if (sky) {
        CHECK(sky->GetComponent<MeshRenderer>() != nullptr);
        CHECK(sky->GetComponent<BoxCollider3D>() != nullptr);
    }
    bb->placeInAir = false;

    // Aim down at the ground → a block lands on top of it (positive Y).
    GameObject* onGround = bb->Build(scene, Vec3{0.0f, 6.0f, 0.0f}, Vec3{0, -1, 0}, true, false);
    CHECK(onGround != nullptr);
    CHECK(onGround && onGround->transform->localPosition.y > 0.0f);
    CHECK(CountBlocks(scene, "Block") == 2);

    // Right-click down removes that block (destroy is queued; flush via Update).
    GameObject* removed = bb->Build(scene, Vec3{0.0f, 6.0f, 0.0f}, Vec3{0, -1, 0}, false, /*remove=*/true);
    CHECK(removed != nullptr);
    scene.Update(0.0f);
    CHECK(CountBlocks(scene, "Block") == 1);

    // Now aiming down hits only the ground (not a "Block") — removal is refused.
    GameObject* none = bb->Build(scene, Vec3{0.0f, 6.0f, 0.0f}, Vec3{0, -1, 0}, false, true);
    CHECK(none == nullptr);
    CHECK(CountBlocks(scene, "Block") == 1);

    // Preview + crosshair: Update() (with a main camera) spawns a non-removable
    // ghost outline and adds an aim reticle, without creating a real block.
    GameObject* camGO = scene.CreateGameObject("Camera");
    auto* cam = camGO->AddComponent<Camera>();
    camGO->transform->localPosition = {0.0f, 6.0f, 0.0f};
    scene.mainCamera = cam;
    bb->placeInAir = true;   // so the ghost shows regardless of the camera's aim in this test
    int before = CountBlocks(scene, "Block");
    scene.Update(0.0f);
    CHECK(CountBlocks(scene, "Block") == before);           // preview is not a block
    CHECK(CountBlocks(scene, "BlockPreview") == 1);          // exactly one ghost
    CHECK(builderGO->GetComponent<Crosshair>() != nullptr); // reticle added
    // Toggling the preview off hides (deactivates) the ghost on the next tick.
    bb->showPreview = false;
    scene.Update(0.0f);
    int hidden = 0;
    for (const auto& up : scene.Objects())
        if (up && up->tag == "BlockPreview" && !up->active) ++hidden;
    CHECK(hidden == 1);

    TEST_MAIN_RESULT();
}
