// Script functions inv_to_grid and inv_from_grid (and their Fn nodes): a script moves items between its slot bag and a
// grid bag (its own GridInventory or a named object's) through InventoryOps::ToGrid / FromGrid. inventory.md listed
// this as not done.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/GridInventory.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Inventory/ItemDatabase.hpp"
#include "okay/Scripting/OkayScriptVM.hpp"
#include <cstdio>

using namespace okay;

int main() {
    RUN_SUITE("inv_grid_script");
    ItemDatabase::Get().Clear();
    { ItemDefinition d; d.id = d.name = "rock"; d.maxStack = 10; ItemDatabase::Get().Put(d); }
    { ItemDefinition d; d.id = d.name = "log"; d.gridW = 2; d.gridH = 1; ItemDatabase::Get().Put(d); }

    Scene s("grid script");
    GameObject* hero = s.CreateGameObject("Hero");
    auto* inv = hero->AddComponent<Inventory>();
    inv->capacity = 6;
    inv->slots.assign(6, Inventory::Slot{});
    inv->slots[0] = {"rock", 5, {}};
    inv->slots[1] = {"log", 1, {}};
    auto* grid = hero->AddComponent<GridInventory>();
    grid->cols = 4; grid->rows = 3;
    GameObject* chest = s.CreateGameObject("Chest");
    auto* cgrid = chest->AddComponent<GridInventory>();
    cgrid->cols = 2; cgrid->rows = 2;

    auto* sc = hero->AddComponent<ScriptComponent>("okayscript");
    CHECK(sc->LoadSource(
        "var a = 0; var b = 0; var c = 0; var d = 0; var e = 0; var f = 0; var g = 0; var h = 0;\n"
        "function start() {\n"
        "  a = inv_to_grid(0, 0, 0, 0, 2);\n"          // 2 rocks to cell (0,0) of my grid
        "  b = inv_to_grid(1, 1, 1);\n"                // the log (2x1) to cell (1,1)
        "  c = inv_to_grid(0, 1, 1);\n"                // onto the log: refused
        "  d = inv_to_grid(0, 0, 0, 0, 0, \"Chest\");\n" // the other 3 rocks to the chest's grid
        "  e = inv_from_grid(0);\n"                    // my grid's first item (2 rocks) back, any slot
        "  f = inv_from_grid(0, 4, 0, \"Chest\");\n"   // the chest's rocks back into slot 4
        "  g = inv_from_grid(9);\n"                    // no such item
        "  h = inv_to_grid(5, 0, 0);\n"                // empty slot
        "}\n"));
    s.Start();
    auto G = [&](const char* n) { return sc->VM()->GetGlobal(n).AsFloat(); };
    std::printf("  a %.0f b %.0f c %.0f d %.0f e %.0f f %.0f g %.0f h %.0f\n", G("a"), G("b"), G("c"), G("d"), G("e"), G("f"), G("g"), G("h"));
    CHECK(G("a") == 2.0f);
    CHECK(G("b") == 1.0f);
    CHECK(G("c") == 0.0f);
    CHECK(G("d") == 3.0f);
    CHECK(G("e") == 2.0f);
    CHECK(G("f") == 3.0f);
    CHECK(G("g") == 0.0f && G("h") == 0.0f);
    // where everything ended up
    CHECK(grid->items.size() == 1 && grid->items[0].name == "log" && grid->items[0].x == 1 && grid->items[0].y == 1);
    CHECK(cgrid->items.empty());
    CHECK(inv->Count("rock") == 5 && inv->Count("log") == 0);
    CHECK(inv->slots[4].item == "rock" && inv->slots[4].count == 3);
    {   // review: "any slot" skips a same-name stack the item cannot join (a unique item, different instance data) and
        // takes an empty slot, instead of failing with "different item there"
        Scene u("unique");
        GameObject* p = u.CreateGameObject("P");
        auto* bag = p->AddComponent<Inventory>();
        bag->capacity = 4;
        bag->slots.assign(4, Inventory::Slot{});
        bag->slots[0] = {"rock", 3, {}};
        auto* g = p->AddComponent<GridInventory>();
        g->cols = 2; g->rows = 2;
        GridItem gi; gi.name = "rock"; gi.count = 1; gi.data.uid = 77;    // a unique rock: never stacks with plain ones
        g->items.push_back(gi);
        auto* usc = p->AddComponent<ScriptComponent>("okayscript");
        CHECK(usc->LoadSource("var m = -1;\nfunction start() { m = inv_from_grid(0); }\n"));
        u.Start();
        CHECK(usc->VM()->GetGlobal("m").AsFloat() == 1.0f);
        CHECK(bag->slots[0].count == 3);                                  // the plain stack is untouched
        CHECK(bag->slots[1].item == "rock" && bag->slots[1].data.uid == 77);
    }
    ItemDatabase::Get().Clear();
    TEST_MAIN_RESULT();
}
