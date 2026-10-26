// Inventory core correctness (inventory plan, PR 1): no duplication, no item loss,
// stacks never exceed maxStack, crafting / eating are all-or-nothing, the drop key
// never deletes items silently.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <climits>

using namespace okay;

static int Total(const Inventory& a, const Inventory& b, const char* item) { return a.Count(item) + b.Count(item); }
static bool UnderCap(const Inventory& inv) {
    for (const auto& s : inv.slots) if (inv.maxStack > 0 && s.count > inv.maxStack) return false;
    return true;
}

int main() {
    RUN_SUITE("inventory_core");

    // ---- Add is all-or-nothing; AddPartial reports the leftover ----
    {
        Scene s("a"); auto* inv = s.CreateGameObject("P")->AddComponent<Inventory>();
        inv->capacity = 2; inv->maxStack = 10;
        CHECK(inv->CanAdd("wood", 25) == 20);
        CHECK(!inv->Add("wood", 25));
        CHECK(inv->Count("wood") == 0);                 // nothing added on failure
        CHECK(inv->AddPartial("wood", 25) == 5);         // 20 fit, 5 left
        CHECK(inv->Count("wood") == 20);
        // Unlimited stacks can't overflow int.
        Inventory* u = s.CreateGameObject("U")->AddComponent<Inventory>();
        u->capacity = 1; u->maxStack = 0;
        CHECK(u->Add("gold", INT_MAX - 5));
        CHECK(!u->Add("gold", 10));
        CHECK(u->AddPartial("gold", 10) == 5);
        CHECK(u->Count("gold") == INT_MAX);
    }

    // ---- Chest -> almost-full bag: only what fits moves, the rest stays ----
    {
        Scene s("c");
        GameObject* player = s.CreateGameObject("Player");
        auto* bag = player->AddComponent<Inventory>();
        bag->capacity = 2; bag->maxStack = 64;
        bag->Add("stone", 64); bag->Add("wood", 60);
        player->AddComponent<InventoryUI>();
        GameObject* chestGo = s.CreateGameObject("Chest");
        auto* box = chestGo->AddComponent<Inventory>();
        box->maxStack = 64; box->Add("wood", 64);
        auto* chest = chestGo->AddComponent<ChestInventory>();
        const int before = Total(*bag, *box, "wood");
        CHECK(chest->TakeToPlayer(0));
        CHECK(bag->Count("wood") == 64);
        CHECK(box->Count("wood") == 60);                 // the remainder stayed in the chest
        CHECK(Total(*bag, *box, "wood") == before);      // nothing duplicated or lost
        CHECK(UnderCap(*bag));
        // Drag-merge between grids respects the DESTINATION's maxStack.
        box->slots[0] = {"wood", 50};
        bag->slots[1] = {"wood", 40};
        ChestInventory::MoveStack(box, 0, bag, 1);
        CHECK(bag->slots[1].count == 64);
        CHECK(box->slots[0].count == 26);
        CHECK(UnderCap(*bag));
    }

    // ---- Sort / QuickMove / MoveSlot never exceed maxStack ----
    {
        Scene s("s"); GameObject* p = s.CreateGameObject("P");
        auto* inv = p->AddComponent<Inventory>(); inv->capacity = 9; inv->maxStack = 64;
        auto* ui = p->AddComponent<InventoryUI>(); ui->hotbarSlots = 3; ui->backpackRows = 2;
        inv->slots = {{"wood", 50}, {"wood", 50}, {"wood", 50}};
        ui->Sort();
        CHECK(UnderCap(*inv));
        CHECK(inv->Count("wood") == 150);
        // QuickMove: the first matching stack in the backpack is full -> use the next / an empty slot.
        inv->slots = {{"ore", 10}, {}, {}, {"ore", 64}, {"ore", 60}, {}};
        CHECK(ui->QuickMove(0));
        CHECK(inv->slots[3].count == 64 && inv->slots[4].count == 64);
        CHECK(inv->Count("ore") == 134);
        CHECK(UnderCap(*inv));
        // Sort that can't fit after re-splitting keeps the layout untouched.
        Inventory* tight = s.CreateGameObject("T")->AddComponent<Inventory>();
        tight->capacity = 2; tight->maxStack = 10;
        auto* tui = tight->gameObject->AddComponent<InventoryUI>();
        tight->slots = {{"a", 15}, {"b", 5}};             // (an over-full stack from old data)
        tui->Sort();                                      // would need a10,a5,b5 = 3 slots: no-op
        CHECK(tight->slots.size() == 2 && tight->slots[0].count == 15 && tight->slots[1].item == "b");
    }

    // ---- Crafting with a full bag fails and keeps the ingredients ----
    {
        Scene s("k"); GameObject* p = s.CreateGameObject("P");
        auto* inv = p->AddComponent<Inventory>(); inv->capacity = 2; inv->maxStack = 64;
        inv->Add("wood", 64); inv->Add("rock", 64);
        auto* cr = p->AddComponent<Crafting>();
        cr->AddRecipe("torch", 1, {{"wood", 1}});         // output needs a new slot; none free
        CHECK(!cr->CraftIndex(0));
        CHECK(inv->Count("wood") == 64 && inv->Count("torch") == 0);
        auto* st = p->AddComponent<CraftingStation>();
        st->recipes.push_back({{{"wood", 1}}, "torch", 1});
        CHECK(!st->Craft(0));
        CHECK(inv->Count("wood") == 64 && inv->Count("torch") == 0);
        // Duplicate ingredient lines are summed: {wood,1},{wood,1} needs 2 wood.
        Scene s2("k2"); GameObject* q = s2.CreateGameObject("Q");
        auto* i2 = q->AddComponent<Inventory>(); i2->Add("wood", 1);
        auto* c2 = q->AddComponent<Crafting>();
        c2->AddRecipe("plank", 1, {{"wood", 1}, {"wood", 1}});
        CHECK(!c2->CanCraft("plank"));
        CHECK(!c2->CraftIndex(0));
        CHECK(i2->Count("wood") == 1 && i2->Count("plank") == 0);
        i2->Add("wood", 1);
        CHECK(c2->CraftIndex(0));
        CHECK(i2->Count("wood") == 0 && i2->Count("plank") == 1);
    }

    // ---- Eating with no survival stats keeps the apple ----
    {
        Scene s("e"); GameObject* p = s.CreateGameObject("P");
        auto* inv = p->AddComponent<Inventory>(); inv->Add("apple", 2);
        auto* co = p->AddComponent<Consumables>(); co->AddRecipe("apple", "Eat", 20.0f);
        CHECK(!co->Use("apple"));
        CHECK(inv->Count("apple") == 2);
        auto* hunger = p->AddComponent<HungerStat>(); hunger->hunger = 10.0f;
        CHECK(co->Use("apple"));
        CHECK(inv->Count("apple") == 1);
        // The inventory may live on the player root, not on this object.
        GameObject* child = s.CreateGameObject("Hand");
        child->transform->SetParent(p->transform, false);
        auto* co2 = child->AddComponent<Consumables>(); co2->AddRecipe("apple", "Eat", 5.0f);
        CHECK(co2->Inv() == inv);
    }

    // ---- The drop key never deletes an item when nothing handles the drop ----
    {
        Scene s("d"); GameObject* p = s.CreateGameObject("P");
        auto* inv = p->AddComponent<Inventory>(); inv->Add("sword", 1);
        auto* ui = p->AddComponent<InventoryUI>(); ui->selected = 0;
        CHECK(!ui->DropSelectedToScript());
        CHECK(inv->Count("sword") == 1);
    }

    // ---- Every UI move fires on_inventory_changed; a handled drop removes the item ----
    {
        Scene s("ev"); GameObject* p = s.CreateGameObject("P");
        auto* inv = p->AddComponent<Inventory>(); inv->capacity = 9; inv->maxStack = 64;
        auto* ui = p->AddComponent<InventoryUI>(); ui->hotbarSlots = 3; ui->backpackRows = 2;
        auto* sc = p->AddComponent<ScriptComponent>("okayscript");
        std::string err;
        CHECK(sc->LoadSource(
            "var changes = 0;\n"
            "var dropped = \"\";\n"
            "function on_inventory_changed() { changes = changes + 1; }\n"
            "function on_item_dropped(item) { dropped = item; }\n", &err));
        s.Start();
        inv->slots = {{"wood", 10}, {}, {}};
        auto changes = [&] { return (int)sc->VM()->GetGlobal("changes").AsFloat(); };
        int c0 = changes();
        ui->MoveSlot(0, 1);                CHECK(changes() == c0 + 1);
        ui->dragAmount = 4; ui->ApplyDrag(1, 2); ui->dragAmount = 0;
                                           CHECK(changes() == c0 + 2);
        CHECK(ui->SplitSlot(1));           CHECK(changes() == c0 + 3);
        CHECK(ui->QuickMove(1));           CHECK(changes() == c0 + 4);
        ui->Sort();                        CHECK(changes() == c0 + 5);
        inv->slots = {{"sword", 1}};
        ui->selected = 0;
        CHECK(ui->DropSelectedToScript());
        CHECK(inv->Count("sword") == 0);
        CHECK(sc->VM()->GetGlobal("dropped").AsString() == "sword");
    }

    TEST_MAIN_RESULT();
}
