// Visual-script inventory nodes: Add / Remove / Has / Count / Equip / Craft / Loot Roll /
// Spawn Pickup run in a small graph against a real Inventory.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <okay/Components/Equipment.hpp>
#include <okay/Components/ItemPickup.hpp>
#include <fstream>
#include <string>

using namespace okay;

int main() {
    RUN_SUITE("vs_inventory");
    ItemDatabase& db = ItemDatabase::Get();
    db.Clear();
    db.LoadText("okayitems 1\n"
                "item \"wood\"\n  name \"Wood\"\nend\n"
                "item \"plank\"\n  name \"Plank\"\nend\n"
                "item \"apple\"\n  name \"Apple\"\nend\n"
                "item \"helmet\"\n  name \"Helmet\"\n  equip head\nend\n");
    { std::ofstream("test_vs_inv_loot.okayloot") << "okayloot 1\nrolls 1 1\nentry \"wood\" 1 2 2 1\n"; }

    Scene s("T");
    GameObject* hero = s.CreateGameObject("Hero");
    auto* inv = hero->AddComponent<Inventory>(); inv->capacity = 4;
    hero->AddComponent<Equipment>();
    auto* cr = hero->AddComponent<Crafting>();
    cr->recipes.push_back(Recipe({{"wood", 2}}, "plank", 1));
    auto* vc = hero->AddComponent<VisualScriptComponent>();
    std::string err;
    CHECK(vc->LoadFromText(
        "node 1 OnStart\nentry OnStart 1\n"
        "node 2 AddItem wood\ndefault 2 0 5\nexec 1 0 2\n"
        "node 3 SetVar left\ndata 3 0 2 0\nexec 2 0 3\n"
        "node 4 RemoveItem wood\ndefault 4 0 2\nexec 3 0 4\n"
        "node 5 SetVar removed\ndata 5 0 4 0\nexec 4 0 5\n"
        "node 6 RemoveItem Wood\ndefault 6 0 99\nexec 5 0 6\n"                 // display names resolve; too many: nothing happens
        "node 7 SetVar removedNo\ndata 7 0 6 0\nexec 6 0 7\n"
        "node 8 HasItem wood\ndefault 8 0 3\nnode 9 SetVar hasYes\ndata 9 0 8 0\nexec 7 0 9\n"
        "node 10 HasItem wood\ndefault 10 0 4\nnode 11 SetVar hasNo\ndata 11 0 10 0\nexec 9 0 11\n"
        "node 12 ItemCount wood\nnode 13 SetVar count\ndata 13 0 12 0\nexec 11 0 13\n"
        "node 14 AddItem helmet\nexec 13 0 14\n"                               // Count left at its default of 1
        "node 15 EquipItem helmet\ndefault 15 0 Head\nexec 14 0 15\n"
        "node 16 SetVar equipped\ndata 16 0 15 0\nexec 15 0 16\n"
        "node 17 CraftItem plank\nexec 16 0 17\n"
        "node 18 SetVar crafted\ndata 18 0 17 0\nexec 17 0 18\n"
        "node 19 LootRoll test_vs_inv_loot.okayloot\nexec 18 0 19\n"
        "node 20 SetVar rolled\ndata 20 0 19 0\nexec 19 0 20\n"
        "node 21 SpawnPickup apple\ndefault 21 0 2\ndefault 21 1 7\nexec 20 0 21\n"
        "node 22 SetVar pickup\ndata 22 0 21 0\nexec 21 0 22\n", &err));
    CHECK(err.empty());
    s.Start();
    s.Update(0.016f);

    CHECK(vc->GetVariable("left").AsFloat() == 0.0f);
    CHECK(vc->GetVariable("removed").AsBool() && !vc->GetVariable("removedNo").AsBool());
    CHECK(vc->GetVariable("hasYes").AsBool() && !vc->GetVariable("hasNo").AsBool());
    CHECK(vc->GetVariable("count").AsFloat() == 3.0f);
    CHECK(vc->GetVariable("equipped").AsBool());
    CHECK(Equipment::Of(hero)->ItemIn(Equipment::Of(hero)->Find("Head")) == "helmet" && inv->Count("helmet") == 0);
    CHECK(vc->GetVariable("crafted").AsBool() && inv->Count("plank") == 1);
    CHECK(vc->GetVariable("rolled").AsFloat() == 2.0f);
    CHECK(inv->Count("wood") == 3);                                           // 5 - 2 - 2 (crafted) + 2 (loot)
    GameObject* pk = s.Find(vc->GetVariable("pickup").AsString());
    CHECK(pk != nullptr && pk->GetComponent<ItemPickup>() != nullptr);
    CHECK(pk && pk->transform && pk->transform->Position().x > 6.9f && pk->transform->Position().x < 7.1f);

    // An overfull bag: Left is what didn't fit.
    {
        Scene s2("T2");
        GameObject* h = s2.CreateGameObject("Hero");
        auto* i2 = h->AddComponent<Inventory>(); i2->capacity = 1;
        i2->Add("apple", 1);
        auto* v2 = h->AddComponent<VisualScriptComponent>();
        CHECK(v2->LoadFromText("node 1 OnStart\nentry OnStart 1\nnode 2 AddItem wood\ndefault 2 0 3\nexec 1 0 2\n"
                               "node 3 SetVar left\ndata 3 0 2 0\nexec 2 0 3\n", &err));
        s2.Start(); s2.Update(0.016f);
        CHECK(v2->GetVariable("left").AsFloat() == 3.0f && i2->Count("wood") == 0);
    }

    TEST_MAIN_RESULT();
}
