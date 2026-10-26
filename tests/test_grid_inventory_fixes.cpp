// Grid inventory correctness: rotating a held item only sticks if the drop lands,
// a drag from a container that went away is cancelled, enforced weight limits,
// stack merge / split, and Validate() repairing bad contents from a file.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <okay/Components/GridInventory.hpp>
#include <okay/Components/GridInventoryUI.hpp>
#include <memory>
#include <string>

using namespace okay;

static bool NoOverlaps(const GridInventory& g) {
    for (int i = 0; i < (int)g.items.size(); ++i) {
        const GridItem& it = g.items[i];
        if (!g.CanPlace(it.x, it.y, it.w, it.h, i)) return false;
    }
    return true;
}

static int Count(const GridInventory& g, const std::string& name) {
    int n = 0;
    for (const GridItem& it : g.items) if (it.name == name) n += it.count;
    return n;
}

int main() {
    RUN_SUITE("grid_inventory_fixes");

    // --- A rotated drag that fails to drop leaves the item as it was ---
    {
        GridInventory bag; bag.cols = 4; bag.rows = 4;
        bag.AddItem("rifle", 1, 3);          // (0,0) 1x3
        bag.AddItem("can", 1, 1);            // (1,0)
        GridInventoryUI ui;
        ui.BeginDrag(&bag, 0, 0, 0);
        ui.RotateDrag();                     // held as 3x1, turned clockwise: the grabbed top cell is now its right end
        CHECK(ui.grabX == 2 && ui.grabY == 0);
        CHECK(ui.HeldItem().w == 3 && ui.HeldItem().h == 1);
        CHECK(bag.items[0].w == 1 && bag.items[0].h == 3);   // the stored item is untouched while held
        CHECK(!ui.Drop(nullptr, 0, 0));      // released outside every grid
        CHECK(bag.items[0].w == 1 && bag.items[0].h == 3);
        CHECK(NoOverlaps(bag));
        CHECK(ui.dragInv == nullptr && !ui.dragRotated);

        ui.BeginDrag(&bag, 0, 0, 0);
        ui.RotateDrag();
        CHECK(!ui.CanDrop(&bag, 2, 0));      // 3x1 at (0,0) hits the can
        CHECK(!ui.Drop(&bag, 2, 0));
        CHECK(bag.items[0].w == 1 && bag.items[0].h == 3 && bag.items[0].x == 0);
        CHECK(NoOverlaps(bag));

        ui.BeginDrag(&bag, 0, 0, 0);
        ui.RotateDrag();
        CHECK(ui.CanDrop(&bag, 2, 3));
        CHECK(ui.Drop(&bag, 2, 3));          // lands on the free bottom row, turned
        CHECK(bag.items[0].w == 3 && bag.items[0].h == 1 && bag.items[0].y == 3);
        CHECK(NoOverlaps(bag));
    }

    // --- Rotating keeps the grabbed cell under the cursor ---
    {
        GridInventory bag; bag.cols = 6; bag.rows = 6;
        bag.AddItem("rifle", 1, 3);
        GridInventoryUI ui;
        ui.BeginDrag(&bag, 0, 0, 2);         // grabbed by its bottom cell
        ui.RotateDrag();
        CHECK(ui.grabX == 0 && ui.grabY == 0);
        ui.RotateDrag();                     // 180°: the grabbed bottom cell is now on top
        CHECK(!ui.dragRotated && ui.grabX == 0 && ui.grabY == 0);
        ui.RotateDrag(); ui.RotateDrag();    // full turn
        CHECK(!ui.dragRotated && ui.grabX == 0 && ui.grabY == 2);
    }

    // --- A drag from a container that's gone is cancelled (no stale pointer use) ---
    {
        Scene s("T");
        GameObject* crate = s.CreateGameObject("crate");
        auto* loot = crate->AddComponent<GridInventory>();
        loot->AddItem("ammo", 1, 1, 30);
        GridInventory bag;
        GridInventoryUI ui;
        ui.BeginDrag(loot, 0, 0, 0);
        CHECK(ui.CheckDrag({&bag, loot}));
        s.Destroy(crate); s.Update(0.0f);     // the crate (and its grid) is destroyed mid-drag
        CHECK(!ui.CheckDrag({&bag}));
        CHECK(ui.dragInv == nullptr && ui.dragIndex == -1);
        CHECK(!ui.Drop(&bag, 0, 0));
        CHECK(bag.items.empty());

        GridInventory other; other.AddItem("x", 1, 1);
        ui.BeginDrag(&other, 0, 0, 0);
        other.items.clear();                  // the item itself vanished
        CHECK(!ui.CheckDrag({&other}));
    }

    // --- Enforced weight limit ---
    {
        GridInventory pack; pack.weightLimit = 10.0f;
        CHECK(pack.enforceWeight);                              // on for new components
        CHECK(pack.AddItem("brick", 1, 1, 4, 2.0f) >= 0);     // 8
        CHECK(pack.AddItem("brick2", 1, 1, 2, 2.0f) < 0);     // would be 12
        CHECK(pack.TotalWeight() <= 10.0f);
        GridInventory ground;
        ground.AddItem("anvil", 1, 1, 1, 5.0f);
        CHECK(!ground.MoveTo(0, pack, 3, 3));
        CHECK(ground.items.size() == 1 && pack.TotalWeight() <= 10.0f);
        pack.enforceWeight = false;                             // informational only: old behaviour
        CHECK(ground.MoveTo(0, pack, 3, 3));
        CHECK(pack.OverWeight());
    }

    // --- Stacking: drop on a matching stack merges up to maxStack; the rest stays ---
    {
        GridInventory a; a.maxStack = 30;
        GridInventory b; b.maxStack = 30;
        a.AddItem("ammo", 1, 1, 25);                          // (0,0)
        b.AddItem("ammo", 1, 1, 20);
        CHECK(b.MoveTo(0, a, 0, 0));
        CHECK(a.items.size() == 1 && a.items[0].count == 30);
        CHECK(b.items.size() == 1 && b.items[0].count == 15);
        CHECK(Count(a, "ammo") + Count(b, "ammo") == 45);
        CHECK(!b.MoveTo(0, a, 0, 0));                         // full stack: nothing moves
        CHECK(Count(a, "ammo") + Count(b, "ammo") == 45);

        // Within one grid.
        GridInventory g; g.maxStack = 10;
        g.AddItem("nail", 1, 1, 4);
        int second = (int)g.items.size(); g.items.push_back(GridItem{"nail", 1, 1, 3, 0, 5, 0.0f});
        CHECK(g.PlaceAt(second, 0, 0));
        CHECK(g.items.size() == 1 && g.items[0].count == 9);

        // AddItem tops up a stack before taking a new cell, and splits by maxStack.
        CHECK(g.AddItem("nail", 1, 1, 1) == 0 && g.items[0].count == 10);
        CHECK(g.AddItem("nail", 1, 1, 1) == 1);
        CHECK(g.AddItem("nail", 1, 1, 14) == 2);               // 9 tops up item 1, 5 more in a new stack
        CHECK(g.items[1].count == 10 && g.items[2].count == 5 && Count(g, "nail") == 25);

        // All or nothing: a partial fit changes nothing.
        GridInventory tiny; tiny.cols = 1; tiny.rows = 1; tiny.maxStack = 10;
        tiny.AddItem("nail", 1, 1, 8);
        CHECK(tiny.AddItem("nail", 1, 1, 5) == -1);
        CHECK(tiny.items.size() == 1 && tiny.items[0].count == 8);

        // A long item that only fits turned goes in turned.
        GridInventory wide; wide.cols = 4; wide.rows = 1;
        CHECK(wide.AddItem("rifle", 1, 4) == 0 && wide.items[0].w == 4 && wide.items[0].h == 1);

        // Merging respects an enforced weight limit on the destination.
        GridInventory light; light.maxStack = 50; light.weightLimit = 3.0f; light.enforceWeight = true;
        light.AddItem("bolt", 1, 1, 2, 1.0f);
        GridInventory heap; heap.AddItem("bolt", 1, 1, 5, 1.0f);
        CHECK(heap.MoveTo(0, light, 0, 0));
        CHECK(light.items[0].count == 3 && heap.items[0].count == 4);

        // maxStack 0 (the default) never merges — same as before.
        GridInventory plain; plain.AddItem("ammo", 1, 1, 5);
        GridInventory src;   src.AddItem("ammo", 1, 1, 5);
        CHECK(!src.MoveTo(0, plain, 0, 0));
        CHECK(plain.items[0].count == 5 && src.items[0].count == 5);
    }

    // --- Split ---
    {
        GridInventory g; g.cols = 2; g.rows = 1;
        g.AddItem("ammo", 1, 1, 9);
        int k = g.Split(0, 4);
        CHECK(k == 1 && g.items[0].count == 5 && g.items[1].count == 4 && g.items[1].x == 1);
        CHECK(g.Split(0, 2) == -1);                           // no free cell: nothing changes
        CHECK(Count(g, "ammo") == 9);
        CHECK(g.Split(1, 0) == -1 && g.Split(1, 4) == -1);    // must leave at least one behind
        CHECK(NoOverlaps(g));
    }

    // --- Validate() repairs bad contents without losing items ---
    {
        GridInventory g; g.cols = 3; g.rows = 2;
        g.items.push_back(GridItem{"a", 2, 2, 0, 0, 1, 1.0f});
        g.items.push_back(GridItem{"b", 2, 2, 0, 0, 1, 1.0f});   // overlaps a, no room anywhere
        g.items.push_back(GridItem{"c", 1, 1, 9, 9, 0, -3.0f});  // out of bounds, bad count/weight
        g.items.push_back(GridItem{"d", 0, -2, 2, 1, 2, 0.5f});  // bad size
        CHECK(!NoOverlaps(g));
        CHECK(g.Validate() == 3);
        CHECK(g.items.size() == 3 && g.overflow.size() == 1 && g.overflow[0].name == "b");
        CHECK(NoOverlaps(g));
        for (const GridItem& it : g.items) {
            CHECK(it.w >= 1 && it.h >= 1 && it.count >= 1 && it.weight >= 0.0f);
            CHECK(it.x >= 0 && it.y >= 0 && it.x + it.w <= g.cols && it.y + it.h <= g.rows);
        }
        CHECK(g.Validate() == 0);                                // idempotent
        g.rows = 4;                                              // room again: b comes back
        g.Validate();
        CHECK(g.items.size() == 4 && g.overflow.empty() && NoOverlaps(g));
    }

    // --- Shrinking the grid (inspector) never overlaps or deletes ---
    {
        GridInventory g; g.cols = 4; g.rows = 4;
        for (int i = 0; i < 16; ++i) g.AddItem("can" + std::to_string(i), 1, 1);
        g.cols = 2; g.rows = 2;
        g.Validate();
        CHECK(NoOverlaps(g) && g.items.size() == 4 && g.overflow.size() == 12);
        g.cols = 4; g.rows = 4;
        g.Validate();
        CHECK(NoOverlaps(g) && g.items.size() == 16 && g.overflow.empty());
    }

    // --- Scenes: old lines load (and get repaired); new fields round-trip ---
    {
        const char* kOld =
            "okayscene 1\n"
            "gameobject 0 \"Bag\"\n  transform 0 0 0 0 0 0 1 1 1 1\n"
            "  gridinventory 4 2 105 \"Bag\" 2 \"rifle\" 1 3 0 0 1 4 \"can\" 1 1 0 0 1 0.5 20 \"Backpack\" 0\nend\n";
        Scene s("T");
        std::string err;
        CHECK(SceneSerializer::Deserialize(s, kOld, &err));
        GameObject* bag = s.Find("Bag");
        auto* gi = bag ? bag->GetComponent<GridInventory>() : nullptr;
        CHECK(gi != nullptr);
        if (gi) {
            CHECK(gi->items.size() == 2 && gi->weightLimit == 20.0f && gi->category == "Backpack");
            CHECK(!gi->enforceWeight && gi->maxStack == 0);
            CHECK(NoOverlaps(*gi));                       // 1x3 rifle fixed (rotated into the 4x2 grid), can moved off it
            gi->enforceWeight = true; gi->maxStack = 12;
            gi->overflow.push_back(GridItem{"crowbar", 1, 5, 0, 0, 1, 2.0f});
            std::string text = SceneSerializer::Serialize(s);
            Scene s2("T2");
            CHECK(SceneSerializer::Deserialize(s2, text, &err));
            auto* g2 = s2.Find("Bag") ? s2.Find("Bag")->GetComponent<GridInventory>() : nullptr;
            CHECK(g2 && g2->enforceWeight && g2->maxStack == 12 && g2->category == "Backpack" && g2->items.size() == 2);
            CHECK(g2 && g2->overflow.size() == 1 && g2->overflow[0].name == "crowbar" && g2->overflow[0].count == 1);
        }
    }

    TEST_MAIN_RESULT();
}
