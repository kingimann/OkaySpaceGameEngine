// Keeping inventories across scenes + saving them on their own, one crafting system
// (shared Recipe, .okayrecipes books, craft time queue, bench tags, unlocks, craft
// max), and sorting by the item definitions.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <okay/Inventory/InventoryPersistence.hpp>
#include <okay/Inventory/Recipe.hpp>
#include <cstdio>
#include <fstream>
#include <string>

using namespace okay;

int main() {
    RUN_SUITE("inventory_persist_craft");
    ItemDatabase& db = ItemDatabase::Get();
    db.Clear();
    db.LoadText("okayitems 1\n"
                "item \"wood\"\n  name \"Wood\"\n  category \"Material\"\n  weight 0.5\n  custom \"value\" \"1\"\nend\n"
                "item \"plank\"\n  name \"Plank\"\n  category \"Material\"\n  weight 0.2\n  custom \"value\" \"3\"\nend\n"
                "item \"sword\"\n  name \"A Sword\"\n  category \"Weapon\"\n  weight 3\n  custom \"value\" \"50\"\nend\n"
                "item \"apple\"\n  name \"Apple\"\n  category \"Food\"\n  weight 0.2\n  custom \"value\" \"2\"\nend\n");

    // --- The player's bag survives load_scene; other objects don't carry anything ---
    {
        const std::string next = "test_persist_next.okayscene";
        {
            Scene n("Next");
            GameObject* p = n.CreateGameObject("Player");
            auto* inv = p->AddComponent<Inventory>(); inv->capacity = 9; inv->Add("apple", 1);   // the new scene's own default
            auto* g = p->AddComponent<GridInventory>(); g->cols = 4; g->rows = 4;
            GameObject* box = n.CreateGameObject("Box");
            box->AddComponent<Inventory>()->Add("plank", 2);
            CHECK(SceneSerializer::SaveToFile(n, next));
        }
        Scene s("First");
        GameObject* p = s.CreateGameObject("Player");
        auto* inv = p->AddComponent<Inventory>(); inv->capacity = 9; inv->keepOnSceneLoad = true;
        inv->Add("sword", 1); inv->Add("wood", 7);
        inv->slots[0].data.durability = 0.5f;
        auto* g = p->AddComponent<GridInventory>(); g->keepOnSceneLoad = true;
        g->AddItem("wood", 1, 2, 3, 0.5f);
        GameObject* box = s.CreateGameObject("Box");
        box->AddComponent<Inventory>()->Add("wood", 99);              // not marked: doesn't carry
        s.Start();
        s.RequestLoad(next);
        s.Update(0.016f);
        GameObject* np = s.Find("Player");
        auto* ninv = np ? np->GetComponent<Inventory>() : nullptr;
        CHECK(ninv && ninv->Count("sword") == 1 && ninv->Count("wood") == 7 && ninv->Count("apple") == 0);
        CHECK(ninv && ninv->slots[0].data.durability == 0.5f);
        auto* ng = np ? np->GetComponent<GridInventory>() : nullptr;
        CHECK(ng && ng->items.size() == 1 && ng->items[0].count == 3);
        GameObject* nb = s.Find("Box");
        CHECK(nb && nb->GetComponent<Inventory>()->Count("plank") == 2 && nb->GetComponent<Inventory>()->Count("wood") == 0);
        std::remove(next.c_str());
    }

    // --- InventoryPersistence::SaveObject / LoadObject without a scene snapshot ---
    {
        Scene s("T");
        GameObject* p = s.CreateGameObject("Player");
        auto* inv = p->AddComponent<Inventory>(); inv->capacity = 9;
        inv->Add("sword", 1); inv->Add("apple", 4);
        inv->slots[0].data.uid = 42; inv->slots[0].data.Set("name", "Old \"Bitey\"");
        auto* g = p->AddComponent<GridInventory>();
        g->AddItem("plank", 2, 1, 5, 0.2f);
        const std::string path = "test_persist_inv.okayinv";
        CHECK(InventoryPersistence::SaveObject(*p, path));
        inv->Clear(); g->items.clear();
        CHECK(InventoryPersistence::LoadObject(*p, path));
        CHECK(inv->Count("sword") == 1 && inv->Count("apple") == 4 && inv->slots[0].data.uid == 42 &&
              inv->slots[0].data.Get("name") == "Old \"Bitey\"");
        CHECK(g->items.size() == 1 && g->items[0].count == 5 && g->items[0].w == 2);
        CHECK(!InventoryPersistence::LoadObjectText(*p, "garbage"));
        CHECK(inv->Count("sword") == 1);                               // a bad file changes nothing
        std::remove(path.c_str());
    }

    // --- One Recipe type: both old brace orders still build it ---
    {
        Crafting::Recipe a{"plank", 4, {{"wood", 2}}};
        CraftingStation::Recipe b{{{"wood", 2}}, "plank", 4};
        CHECK(a.output == b.output && a.outputCount == b.outputCount && a.inputs.size() == b.inputs.size());
        static_assert(std::is_same<Crafting::Recipe, CraftingStation::Recipe>::value, "one recipe type");
    }

    // --- Recipe book, craft time queue, bench tags, unlocks, craft max ---
    {
        RecipeBook book;
        std::string err;
        CHECK(book.LoadText("okayrecipes 1\n"
                            "recipe \"Plank\" 4\n  in \"Wood\" 2\n  out \"wood\" 0\nend\n"            // byproduct with count 0 is dropped
                            "recipe \"sword\" 1\n  in \"plank\" 3\n  time 2\n  station \"forge\"\nend\n"
                            "recipe \"apple\" 1\n  in \"wood\" 1\n  unlock \"knows_apples\"\nend\n"
                            "recipe \"broken\" 1\nend\n", &err));
        CHECK(book.recipes.size() == 3 && book.recipes[0].output == "plank" && book.recipes[0].inputs[0].item == "wood");
        RecipeBook copy;
        CHECK(copy.LoadText(book.Serialize(), &err) && copy.Serialize() == book.Serialize());
        const std::string path = "test_persist_book.okayrecipes";
        { std::ofstream(path) << book.Serialize(); }

        Scene s("T");
        GameObject* p = s.CreateGameObject("Player");
        auto* inv = p->AddComponent<Inventory>(); inv->capacity = 9;
        inv->Add("wood", 10);
        auto* cr = p->AddComponent<Crafting>();
        cr->recipeBook = path;
        s.Start();
        CHECK(cr->AllRecipes().size() == 3);
        CHECK(cr->Craft("plank") && inv->Count("plank") == 4 && inv->Count("wood") == 8);   // instant
        CHECK(!cr->CanCraft("apple"));                                  // locked
        ActionList::Vars()["knows_apples"] = 1.0f;
        CHECK(cr->CanCraft("apple"));
        ActionList::Vars().erase("knows_apples");

        inv->Add("plank", 5);                                           // 9 planks: 3 swords' worth
        CHECK(!cr->CanCraft("sword"));                                  // needs an open forge
        GameObject* forge = s.CreateGameObject("Forge");
        auto* fs = forge->AddComponent<CraftingStation>();
        fs->stationTag = "forge"; fs->recipeBook = path; fs->open = true; fs->range = 0.0f;
        CHECK(fs->Recipes().size() == 1 && fs->Recipes()[0].output == "sword");   // only the forge's recipes
        CHECK(cr->CanCraft("sword"));
        Recipe sword; for (const auto& r : cr->AllRecipes()) if (r.output == "sword") sword = r;
        CHECK(cr->CraftMax(sword));
        CHECK(inv->Count("plank") == 0 && cr->Queue().Jobs().size() == 3);   // inputs taken when queued
        s.Update(1.0f);
        CHECK(cr->Queue().Progress() > 0.45f && cr->Queue().Progress() < 0.55f && inv->Count("sword") == 0);
        s.Update(1.5f);
        CHECK(inv->Count("sword") == 1 && cr->Queue().Jobs().size() == 2);
        cr->CancelQueue();                                              // the rest is refunded
        CHECK(cr->Queue().Jobs().empty() && inv->Count("plank") == 6 && inv->Count("sword") == 1);

        // A finished craft that no longer fits drops next to the crafter instead of vanishing.
        Inventory& small = *inv;
        small.Clear(); small.capacity = 1; small.Add("plank", 3);
        CHECK(cr->Craft(sword));
        small.slots[0] = Inventory::Slot{"apple", 5};   // the bag fills up meanwhile
        s.Update(3.0f);
        int dropped = 0;
        for (auto* pk : s.FindObjectsOfType<ItemPickup>()) if (pk->item == "sword") dropped += pk->count;
        CHECK(dropped == 1);
        std::remove(path.c_str());
    }

    // --- Station recipes save their new fields ---
    {
        Scene s("T");
        GameObject* b = s.CreateGameObject("Bench");
        auto* cs = b->AddComponent<CraftingStation>();
        cs->stationTag = "workbench"; cs->recipeBook = "recipes/main.okayrecipes";
        Recipe r{{{"wood", 2}}, "plank", 4};
        r.craftTime = 1.5f; r.station = "workbench"; r.unlockFlag = "tier1"; r.byproducts.push_back({"wood", 1});
        cs->recipes.push_back(r);
        auto* cr = b->AddComponent<Crafting>();
        cr->recipeBook = "recipes/menu.okayrecipes";
        cr->recipes.push_back(r);
        std::string err;
        Scene back("R");
        CHECK(SceneSerializer::Deserialize(back, SceneSerializer::Serialize(s), &err));
        auto* rb = back.Find("Bench");
        auto* rcs = rb ? rb->GetComponent<CraftingStation>() : nullptr;
        auto* rcr = rb ? rb->GetComponent<Crafting>() : nullptr;
        CHECK(rcs && rcs->stationTag == "workbench" && rcs->recipeBook == "recipes/main.okayrecipes" && rcs->recipes.size() == 1);
        CHECK(rcs && rcs->recipes[0].craftTime == 1.5f && rcs->recipes[0].unlockFlag == "tier1" && rcs->recipes[0].byproducts.size() == 1);
        CHECK(rcr && rcr->recipeBook == "recipes/menu.okayrecipes" && rcr->recipes.size() == 1 && rcr->recipes[0].station == "workbench");
    }

    // --- Sorting by the item definitions ---
    {
        Scene s("T");
        GameObject* p = s.CreateGameObject("P");
        auto* inv = p->AddComponent<Inventory>(); inv->capacity = 9;
        inv->Add("wood", 10); inv->Add("sword", 1); inv->Add("apple", 3); inv->Add("plank", 1);
        auto* ui = p->AddComponent<InventoryUI>();
        auto order = [&] { std::string o; for (const auto& sl : inv->slots) if (!sl.item.empty()) o += sl.item + ","; return o; };
        ui->sortMode = InventoryUI::SortMode::Name;     ui->Sort(); CHECK(order() == "sword,apple,plank,wood,");   // "A Sword" first
        ui->sortMode = InventoryUI::SortMode::Category; ui->Sort(); CHECK(order() == "apple,plank,wood,sword,");   // Food, Material, Weapon
        ui->sortMode = InventoryUI::SortMode::Weight;   ui->Sort(); CHECK(order() == "wood,sword,apple,plank,");   // 5, 3, 0.6, 0.2
        ui->sortMode = InventoryUI::SortMode::Value;    ui->Sort(); CHECK(order() == "sword,wood,apple,plank,");   // 50, 10, 6, 3
    }

    TEST_MAIN_RESULT();
}
