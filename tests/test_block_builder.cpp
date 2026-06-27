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

    // Grid snap maps a point to the centre of the cell that contains it (cells are
    // [n, n+1), centres at half-steps) so blocks sit flush on integer-height surfaces.
    Vec3 sn = bb->Snap(Vec3{1.4f, -0.2f, 2.6f});
    CHECK_NEAR(sn.x, 1.5f, 1e-4f);
    CHECK_NEAR(sn.y, -0.5f, 1e-4f);
    CHECK_NEAR(sn.z, 2.5f, 1e-4f);

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

    // Builder on a CHILD camera must ignore the whole player body (its collider is
    // on the root), so a forward shot lands on the wall ahead — never on yourself.
    {
        Scene hs("hier");
        GameObject* player = hs.CreateGameObject("Player");
        player->transform->localPosition = {0, 0, 0};
        auto* pcol = player->AddComponent<BoxCollider3D>();
        pcol->size = {1.0f, 2.0f, 1.0f};            // body wraps the origin
        GameObject* camObj = hs.CreateGameObject("Cam");
        camObj->transform->SetParent(player->transform, false);
        auto* bb2 = camObj->AddComponent<BlockBuilder>();
        bb2->blockSize = 1.0f; bb2->reach = 10.0f;
        GameObject* wall = hs.CreateGameObject("Wall");
        wall->transform->localPosition = {0, 0, -5};
        wall->transform->localScale = {4, 4, 1};
        wall->AddComponent<BoxCollider3D>();
        // Cast from inside the player body, straight ahead (-Z) at the wall.
        GameObject* placed = bb2->Build(hs, Vec3{0, 0, 0}, Vec3{0, 0, -1}, true, false);
        CHECK(placed != nullptr);
        CHECK(placed && placed->transform->localPosition.z < -1.0f);  // near the wall, not on the player
    }

    // Update() must aim where the camera LOOKS (down its local -Z), not behind it.
    // Identity-rotation camera looks toward -Z; the ghost must land on the -Z wall.
    {
        Scene cs("cam");
        GameObject* cg = cs.CreateGameObject("Cam");
        cg->transform->localPosition = {0, 0, 0};   // identity rotation → looks -Z
        auto* cam = cg->AddComponent<Camera>();
        cs.mainCamera = cam;
        auto* cbb = cg->AddComponent<BlockBuilder>();
        cbb->blockSize = 1.0f; cbb->reach = 20.0f;
        GameObject* wall = cs.CreateGameObject("Wall");
        wall->transform->localPosition = {0, 0, -6};
        wall->transform->localScale = {6, 6, 1};
        wall->AddComponent<BoxCollider3D>();
        cs.Update(0.0f);   // drives BlockBuilder::Update → builds the ghost from cam aim
        GameObject* ghost = nullptr;
        for (const auto& up : cs.Objects()) if (up && up->tag == "BlockPreview") ghost = up.get();
        CHECK(ghost != nullptr);
        CHECK(ghost && ghost->transform->localPosition.z < 0.0f);  // in front (-Z), not behind
    }

    // --- Obstruction: can't place a block where a world object already is ---
    {
        Scene os("obstruct");
        GameObject* bgo = os.CreateGameObject("Builder");
        auto* ob = bgo->AddComponent<BlockBuilder>();
        ob->blockSize = 1.0f; ob->reach = 4.0f; ob->placeInAir = true;
        // A crate overlapping the cell an air-placement would land in (the aim ray
        // passes beside it, so the crate is an obstruction, not the support surface).
        GameObject* box = os.CreateGameObject("Crate");
        box->transform->localPosition = {0.9f, 0.5f, -3.5f};
        box->transform->localScale = {0.6f, 1.0f, 0.6f};
        box->AddComponent<BoxCollider3D>();
        GameObject* refused = ob->Build(os, Vec3{0, 0, 0}, Vec3{0, 0, -1}, true, false);
        CHECK(refused == nullptr);                       // cell overlaps the crate
        GameObject* placed = ob->Build(os, Vec3{0, 0, 0}, Vec3{0, 0, 1}, true, false);
        CHECK(placed != nullptr);                        // opposite way is clear
    }

    TEST_MAIN_RESULT();
}
