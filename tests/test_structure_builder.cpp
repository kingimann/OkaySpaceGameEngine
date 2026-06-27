// Headless test for the Rust-style StructureBuilder: grid/edge/top snapping of
// foundations, walls, floors, pillars + place/demolish.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>

using namespace okay;

static int CountTag(Scene& s, const std::string& tag) {
    int n = 0;
    for (const auto& up : s.Objects()) if (up && up->tag == tag) ++n;
    return n;
}

int main() {
    RUN_SUITE("structure_builder");

    Scene scene("build");
    // Ground with a collider so foundations have something to sit on.
    GameObject* ground = scene.CreateGameObject("Ground");
    ground->transform->localPosition = {0, 0, 0};
    ground->transform->localScale = {50, 1, 50};
    ground->AddComponent<BoxCollider3D>();   // top at y = 0.5

    GameObject* go = scene.CreateGameObject("Builder");
    auto* sb = go->AddComponent<StructureBuilder>();
    sb->cellSize = 3.0f; sb->wallHeight = 3.0f; sb->slabThickness = 0.3f;
    sb->reach = 50.0f;

    // --- Foundation: snaps to the world grid and sits on the ground ---
    sb->piece = StructureBuilder::Piece::Foundation;
    auto fp = sb->Resolve(scene, Vec3{1.2f, 10.0f, 0.4f}, Vec3{0, -1, 0});
    CHECK(fp.show && fp.valid);
    CHECK_NEAR(fp.position.x, 0.0f, 1e-3f);   // 1.2 snaps to grid 0
    CHECK_NEAR(fp.position.z, 0.0f, 1e-3f);
    CHECK_NEAR(fp.scale.x, 3.0f, 1e-3f);
    GameObject* found = sb->PlacePiece(scene, fp);
    CHECK(found != nullptr);
    CHECK(CountTag(scene, "Structure") == 1);
    CHECK(found->GetComponent<BoxCollider3D>() != nullptr);
    float foundTop = found->transform->localPosition.y + found->transform->localScale.y * 0.5f;

    // Re-resolving the same cell is now occupied → not valid (no duplicate).
    auto dup = sb->Resolve(scene, Vec3{0.5f, 10.0f, -0.5f}, Vec3{0, -1, 0});
    CHECK(dup.show && !dup.valid);

    // --- Wall: snaps to the east edge of the foundation, sits on its top ---
    sb->piece = StructureBuilder::Piece::Wall;
    // Aim at the +X side of the foundation top.
    auto wp = sb->Resolve(scene, Vec3{1.3f, 10.0f, 0.0f}, Vec3{0, -1, 0});
    CHECK(wp.show && wp.valid);
    CHECK_NEAR(wp.position.x, 1.5f, 1e-3f);          // foundation half-width 1.5
    CHECK_NEAR(wp.scale.z, 3.0f, 1e-3f);             // runs along the edge (Z)
    CHECK_NEAR(wp.scale.x, sb->wallThickness, 1e-3f);
    CHECK_NEAR(wp.position.y, foundTop + sb->wallHeight * 0.5f, 1e-3f);
    CHECK(sb->PlacePiece(scene, wp) != nullptr);
    CHECK(CountTag(scene, "Structure") == 2);

    // --- Floor: stacks a storey above the foundation ---
    sb->piece = StructureBuilder::Piece::Floor;
    auto flp = sb->Resolve(scene, Vec3{0.0f, 10.0f, 0.0f}, Vec3{0, -1, 0});
    CHECK(flp.show && flp.valid);
    CHECK(flp.position.y > foundTop + sb->wallHeight - 0.5f);   // up one storey
    CHECK(sb->PlacePiece(scene, flp) != nullptr);

    // --- Demolish: remove a structure piece we aimed at ---
    int before = CountTag(scene, "Structure");
    GameObject* gone = sb->Demolish(scene, Vec3{0.0f, 10.0f, 0.0f}, Vec3{0, -1, 0}, 50.0f);
    CHECK(gone != nullptr);
    scene.Update(0.0f);   // flush queued destroy
    CHECK(CountTag(scene, "Structure") == before - 1);

    // --- Foundation needs a surface: aiming at empty sky places nothing ---
    sb->piece = StructureBuilder::Piece::Foundation;
    auto miss = sb->Resolve(scene, Vec3{0.0f, 10.0f, 0.0f}, Vec3{0, 1, 0});
    CHECK(!miss.show && !miss.valid);

    TEST_MAIN_RESULT();
}
