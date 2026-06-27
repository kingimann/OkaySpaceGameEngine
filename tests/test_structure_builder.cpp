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
    sb->requireResources = false;   // creative mode for the geometry checks

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

    // --- Obstruction: can't build where a world object is in the way ---
    // A crate overlapping the foundation cell (but not the surface we aim at) blocks it.
    GameObject* crate = scene.CreateGameObject("Crate");
    crate->transform->localPosition = {10.4f, 1.0f, 9.0f};
    crate->transform->localScale = {1, 2, 1};      // spans x≈[9.9,10.9]
    crate->AddComponent<BoxCollider3D>();
    auto blockedByCrate = sb->Resolve(scene, Vec3{9.0f, 10.0f, 9.0f}, Vec3{0, -1, 0});
    CHECK(blockedByCrate.show && !blockedByCrate.valid);   // cell (x≈[7.5,10.5]) overlaps the crate
    // An empty patch of ground is still fine.
    auto clearCell = sb->Resolve(scene, Vec3{-9.0f, 10.0f, -9.0f}, Vec3{0, -1, 0});
    CHECK(clearCell.valid);

    // --- Inventory / tiers: building costs resources; upgrade + repair + refund ---
    {
        Scene rs("res");
        GameObject* gnd = rs.CreateGameObject("Ground");
        gnd->transform->localScale = {50, 1, 50};
        gnd->AddComponent<BoxCollider3D>();
        GameObject* player = rs.CreateGameObject("Player");
        auto* inv = player->AddComponent<Inventory>();
        auto* b = player->AddComponent<StructureBuilder>();
        b->reach = 50.0f; b->requireResources = true;
        b->costWood = 10; b->costStone = 15;

        // No wood yet → can't afford → not valid.
        b->piece = StructureBuilder::Piece::Foundation;
        auto noWood = b->Resolve(rs, Vec3{0, 10, 0}, Vec3{0, -1, 0});
        CHECK(noWood.show && !noWood.valid);

        // Give 25 wood → now valid; placing spends 10.
        inv->Add("Wood", 25);
        auto ok = b->Resolve(rs, Vec3{0, 10, 0}, Vec3{0, -1, 0});
        CHECK(ok.valid);
        GameObject* f = b->PlacePiece(rs, ok);
        CHECK(f != nullptr);
        CHECK(inv->Count("Wood") == 15);
        auto* bp = f->GetComponent<BuildPiece>();
        CHECK(bp != nullptr);
        CHECK(bp && bp->tier == 0);

        // Upgrade wood→stone costs 15 stone: fails without stone, works with it.
        CHECK(b->Upgrade(rs, Vec3{0, 10, 0}, Vec3{0, -1, 0}) == nullptr);
        inv->Add("Stone", 20);
        GameObject* up = b->Upgrade(rs, Vec3{0, 10, 0}, Vec3{0, -1, 0});
        CHECK(up == f);
        CHECK(bp && bp->tier == 1);
        CHECK(inv->Count("Stone") == 5);

        // Damage then repair (pays stone, restores to full).
        inv->Add("Stone", 10);   // enough to cover the repair
        bp->Damage(bp->maxHealth * 0.5f);
        CHECK(bp->health < bp->maxHealth);
        GameObject* rep = b->Repair(rs, Vec3{0, 10, 0}, Vec3{0, -1, 0});
        CHECK(rep == f);
        CHECK(bp->FullHealth());

        // Demolish refunds part of the (stone-tier) cost.
        int stoneBefore = inv->Count("Stone");
        GameObject* dem = b->Demolish(rs, Vec3{0, 10, 0}, Vec3{0, -1, 0});
        CHECK(dem != nullptr);
        rs.Update(0.0f);
        CHECK(inv->Count("Stone") > stoneBefore);   // got a refund
    }

    TEST_MAIN_RESULT();
}
