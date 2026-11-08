// Building must be paid for in full or not at all, a demolish refund that does not fit
// in the bag drops as a pickup, and item names resolve ignoring case and whitespace.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <okay/Inventory/ItemDatabase.hpp>

using namespace okay;

static int CountTag(Scene& s, const std::string& tag) {
    int n = 0;
    for (const auto& up : s.Objects()) if (up && up->tag == tag) ++n;
    return n;
}

int main() {
    RUN_SUITE("inventory_build_charge");

    // --- An unaffordable build takes nothing and places nothing ---
    {
        Scene s("charge");
        GameObject* gnd = s.CreateGameObject("Ground");
        gnd->transform->localScale = {50, 1, 50};
        gnd->AddComponent<BoxCollider3D>();
        GameObject* player = s.CreateGameObject("Player");
        auto* inv = player->AddComponent<Inventory>();
        auto* b = player->AddComponent<StructureBuilder>();
        b->reach = 50.0f; b->requireResources = true; b->costWood = 10;

        StructureBuilder::Placement p;   // forced valid, so PlacePiece's own payment check is what is tested
        p.valid = true; p.piece = StructureBuilder::Piece::Foundation;
        p.position = {0, 1, 0}; p.scale = {3, 0.3f, 3};

        CHECK(b->PlacePiece(s, p) == nullptr);                 // no wood at all
        CHECK(CountTag(s, "Structure") == 0);

        inv->Add("Wood", 4);                                   // 4 of 10: Remove would drain them and still fail
        CHECK(b->PlacePiece(s, p) == nullptr);
        CHECK(inv->Count("Wood") == 4);
        CHECK(CountTag(s, "Structure") == 0);

        inv->Add("Wood", 6);                                   // exactly enough
        GameObject* f = b->PlacePiece(s, p);
        CHECK(f != nullptr);
        CHECK(inv->Count("Wood") == 0);

        // Upgrade and repair are all-or-nothing too.
        b->costStone = 15;
        inv->Add("Stone", 9);
        CHECK(b->Upgrade(s, Vec3{0, 10, 0}, Vec3{0, -1, 0}) == nullptr);
        CHECK(inv->Count("Stone") == 9);
        auto* bp = f->GetComponent<BuildPiece>();
        bp->Damage(bp->maxHealth * 0.5f);
        inv->Remove("Stone", 9);
        inv->Add("Wood", 1);
        CHECK(b->Repair(s, Vec3{0, 10, 0}, Vec3{0, -1, 0}) == nullptr);   // costs more than 1 wood
        CHECK(inv->Count("Wood") == 1);
    }

    // --- A full bag: the demolish refund spawns a pickup instead of vanishing ---
    {
        Scene s("refund");
        GameObject* gnd = s.CreateGameObject("Ground");
        gnd->transform->localScale = {50, 1, 50};
        gnd->AddComponent<BoxCollider3D>();
        GameObject* player = s.CreateGameObject("Player");
        auto* inv = player->AddComponent<Inventory>();
        inv->capacity = 1;
        auto* b = player->AddComponent<StructureBuilder>();
        b->reach = 50.0f; b->requireResources = true; b->costWood = 10; b->refundFraction = 0.5f;

        inv->Add("Wood", 10);
        auto pl = b->Resolve(s, Vec3{0, 10, 0}, Vec3{0, -1, 0});
        CHECK(pl.valid);
        CHECK(b->PlacePiece(s, pl) != nullptr);                // spends all 10; the single slot is now empty
        inv->Add("Rock", 1);                                   // fill it with something else
        CHECK(inv->IsFull());

        CHECK(b->Demolish(s, Vec3{0, 10, 0}, Vec3{0, -1, 0}) != nullptr);
        s.Update(0.0f);
        CHECK(inv->Count("Wood") == 0);
        int dropped = 0;
        for (auto* pk : s.FindObjectsOfType<ItemPickup>()) if (pk->item == "Wood") dropped += pk->count;
        CHECK(dropped == 5);
    }

    // --- Item names ignore case and surrounding whitespace ---
    {
        ItemDatabase& db = ItemDatabase::Get();
        db.Clear();
        std::string err;
        CHECK(db.LoadText("okayitems 1\nitem \"wood\"\n  name \"Wood\"\nend\n", &err));
        CHECK(db.Resolve("Wood") == "wood");
        CHECK(db.Resolve("wood ") == "wood");
        CHECK(db.Resolve("  WOOD\t") == "wood");
        CHECK(db.Resolve("Stone ") == "Stone");                // unknown: trimmed, kept loose
        CHECK(db.CaseConflicts().empty());

        ItemDefinition dup; dup.id = "WOOD";
        db.Put(dup);
        CHECK(db.CaseConflicts().size() == 1);
        CHECK(db.Resolve("Wood") == "wood");                   // exact name still wins
        CHECK(db.Resolve("woOd") == "woOd");                   // ambiguous by case: no guess
        db.Clear();
    }

    TEST_MAIN_RESULT();
}
