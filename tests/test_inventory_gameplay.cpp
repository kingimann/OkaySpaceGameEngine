// Inventory gameplay: world pickups and drops (a dropped item can be picked up again),
// using the held item, encumbrance, and loot tables rolled on start / open / death.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <okay/Components/ItemPickup.hpp>
#include <okay/Components/LootSource.hpp>
#include <okay/Inventory/InventoryInput.hpp>
#include <okay/Inventory/LootTable.hpp>
#include <cstdio>
#include <fstream>
#include <string>

using namespace okay;

static int Pickups(Scene& s, const std::string& item) {
    int n = 0;
    for (auto* p : s.FindObjectsOfType<ItemPickup>()) if (p->item == item) n += p->count;
    return n;
}

int main() {
    RUN_SUITE("inventory_gameplay");
    ItemDatabase& db = ItemDatabase::Get();
    db.Clear();
    db.LoadText("okayitems 1\n"
                "item \"sword\"\n  name \"Sword\"\n  weight 3\n  equip mainhand\nend\n"
                "item \"apple\"\n  name \"Apple\"\n  stack 10\n  weight 0.2\n  use \"Eat\" 15\nend\n"
                "item \"plank\"\n  name \"Plank\"\n  weight 1\nend\n");

    // --- Drop a sword into the world, then pick it up again ---
    {
        Scene s("T");
        GameObject* hero = s.CreateGameObject("Hero"); hero->tag = "Player";
        auto* inv = hero->AddComponent<Inventory>(); inv->capacity = 9;
        inv->Add("sword", 1);
        auto* ui = hero->AddComponent<InventoryUI>();
        ui->selected = 0;
        CHECK(ui->DropSelectedItem());
        CHECK(inv->Count("sword") == 0 && Pickups(s, "sword") == 1);
        auto* pk = s.FindObjectsOfType<ItemPickup>()[0];
        CHECK(pk->collectorTag == "Player");
        GameObject* npc = s.CreateGameObject("Goblin");
        npc->AddComponent<Inventory>();
        CHECK(pk->GiveTo(npc) == 0);                          // not a collector
        CHECK(pk->GiveTo(hero) == 1);
        s.Update(0.016f);
        CHECK(inv->Count("sword") == 1 && Pickups(s, "sword") == 0);
    }

    // --- A pickup only takes what fits; the rest stays on the ground. Radius + magnet ---
    {
        Scene s("T");
        GameObject* hero = s.CreateGameObject("Hero"); hero->tag = "Player";
        auto* inv = hero->AddComponent<Inventory>(); inv->capacity = 1;
        GameObject* g = ItemPickup::Spawn(s, "apple", 14, ItemInstanceData{}, {5, 0, 0});
        auto* pk = g->GetComponent<ItemPickup>();
        CHECK(pk->GiveTo(hero) == 10);                        // apple stacks to 10, one slot
        CHECK(pk->count == 4 && inv->Count("apple") == 10);
        s.Update(0.016f);
        CHECK(Pickups(s, "apple") == 4);                      // still there
        inv->Clear();
        pk->autoRadius = 1.0f; pk->magnetRadius = 10.0f; pk->magnetSpeed = 1000.0f;
        s.Update(0.016f);                                     // pulled in (the hero is 5 m away)...
        s.Update(0.016f);                                     // ...and collected
        CHECK(inv->Count("apple") == 4 && Pickups(s, "apple") == 0);
    }

    // --- Dragging a stack off the slots drops it in the world ---
    {
        Scene s("T");
        GameObject* hero = s.CreateGameObject("Hero"); hero->tag = "Player";
        auto* inv = hero->AddComponent<Inventory>(); inv->capacity = 9;
        inv->Add("plank", 4);
        auto* ui = hero->AddComponent<InventoryUI>();
        ui->open = true; ui->scaleToScreen = false;
        const auto L = InventoryInput::LayoutHotbar(*ui, 800, 600);
        float x, y; L.SlotXY(0, x, y);
        Input::FeedMouse({x + 3, y + 3}, 1); InventoryInput::HandleHotbar(*ui, L, Input::MousePosition());
        Input::FeedMouse({400, 50}, 0);      InventoryInput::HandleHotbar(*ui, L, Input::MousePosition());   // released far away
        CHECK(inv->Count("plank") == 0 && Pickups(s, "plank") == 4);
    }

    // --- Using the held item: eat it (its definition), or pick its build piece ---
    {
        Scene s("T");
        GameObject* hero = s.CreateGameObject("Hero");
        auto* inv = hero->AddComponent<Inventory>(); inv->capacity = 9;
        inv->Add("apple", 2); inv->Add("plank", 3);
        hero->AddComponent<Consumables>();
        auto* hunger = hero->AddComponent<HungerStat>(); hunger->hunger = 10.0f;
        auto* ui = hero->AddComponent<InventoryUI>();
        ui->selected = 0;
        CHECK(ui->UseSelected() && inv->Count("apple") == 1 && hunger->hunger > 10.0f);
        ui->selected = 1;
        CHECK(!ui->UseSelected());                           // nothing uses a plank yet
        auto* bm = hero->AddComponent<BuildMode>();
        bm->palette.push_back({"Wall", "wall.okayprefab", ""});
        bm->palette.push_back({"Plank", "plank.okayprefab", ""});
        CHECK(ui->UseSelected() && bm->Active() && bm->SelectedSlot() == 1);
        CHECK(inv->Count("plank") == 3);                      // selecting a piece doesn't spend it
    }

    // --- Encumbrance from the definitions' weights ---
    {
        Scene s("T");
        GameObject* hero = s.CreateGameObject("Hero");
        auto* inv = hero->AddComponent<Inventory>(); inv->capacity = 9;
        inv->weightLimit = 5.0f; inv->slowWhenOverWeight = true; inv->overWeightSpeed = 0.4f;
        auto* sc = hero->AddComponent<ScriptComponent>("okayscript");
        std::string err;
        CHECK(sc->LoadSource("var over = -1;\nfunction on_encumbered(o) { if (o) { over = 1; } else { over = 0; } }\n", &err));
        s.Start();
        inv->Add("sword", 1);
        CHECK(inv->TotalWeight() == 3.0f && !inv->OverWeight() && Inventory::SpeedFactorOf(hero) == 1.0f);
        inv->Add("plank", 3);
        CHECK(inv->OverWeight() && Inventory::SpeedFactorOf(hero) == 0.4f);
        CHECK((int)sc->VM()->GetGlobal("over").AsFloat() == 1);
        inv->Remove("plank", 3);
        CHECK((int)sc->VM()->GetGlobal("over").AsFloat() == 0 && Inventory::SpeedFactorOf(hero) == 1.0f);
    }

    // --- Loot tables ---
    {
        LootTable t;
        std::string err;
        CHECK(t.LoadText("okayloot 1\nrolls 2 4\nentry \"apple\" 3 1 3 1\nentry \"Sword\" 1 1 1 1\nentry \"plank\" 0 1 1 1\n", &err));
        CHECK(t.entries.size() == 2 && t.entries[1].item == "sword");   // display name → id; weight 0 skipped
        Random a(42), b(42);
        auto r1 = t.Roll(a), r2 = t.Roll(b);
        CHECK(!r1.empty() && r1.size() == r2.size());
        for (std::size_t i = 0; i < r1.size() && i < r2.size(); ++i) CHECK(r1[i].item == r2[i].item && r1[i].count == r2[i].count);
        int total = 0;
        for (const auto& st : r1) total += st.count;
        CHECK(total >= 2 && total <= 12);
        LootTable never;
        CHECK(never.LoadText("okayloot 1\nrolls 5 5\nentry \"apple\" 1 1 1 0\n", &err) && never.Roll(a).empty());

        const std::string path = "test_loot_tmp.okayloot";
        { std::ofstream(path) << "okayloot 1\nrolls 1 1\nentry \"apple\" 1 2 2 1\n"; }

        Scene s("T");
        GameObject* crate = s.CreateGameObject("Crate");
        auto* cinv = crate->AddComponent<Inventory>(); cinv->capacity = 6;
        auto* ls = crate->AddComponent<LootSource>();
        ls->table = path; ls->seed = 7; ls->refill = 1.0f;
        s.Start();
        CHECK(cinv->Count("apple") == 2);                     // filled on Start
        cinv->Clear();
        s.Update(0.6f); s.Update(0.6f);
        CHECK(cinv->Count("apple") == 2);                     // refilled once emptied

        GameObject* box = s.CreateGameObject("Box");
        auto* binv = box->AddComponent<Inventory>(); binv->capacity = 6;
        auto* ch = box->AddComponent<ChestInventory>(); ch->range = 0.0f;
        auto* bls = box->AddComponent<LootSource>(); bls->table = path; bls->when = LootSource::When::Open;
        s.Update(0.016f);
        CHECK(binv->Count("apple") == 0);                     // not yet opened
        ch->open = true; s.Update(0.016f);
        CHECK(binv->Count("apple") == 2);
        ch->open = false; s.Update(0.016f); ch->open = true; s.Update(0.016f);
        CHECK(binv->Count("apple") == 2);                     // only the first open rolls

        GameObject* wolf = s.CreateGameObject("Wolf");
        auto* hp = wolf->AddComponent<HealthStat>();
        auto* wls = wolf->AddComponent<LootSource>(); wls->table = path; wls->when = LootSource::When::Death;
        s.Update(0.016f);
        CHECK(Pickups(s, "apple") == 0);
        hp->Damage(1000.0f);
        s.Update(0.016f); s.Update(0.016f);
        CHECK(Pickups(s, "apple") == 2);                      // dropped once, not every frame

        GameObject* goblin = s.CreateGameObject("Goblin");
        auto* npc = goblin->AddComponent<NPCController>();
        auto* gls = goblin->AddComponent<LootSource>(); gls->table = path; gls->when = LootSource::When::Death;
        s.Update(0.016f);
        npc->Damage(1e9f);                                    // removes itself the moment it dies
        s.Update(0.016f);
        CHECK(Pickups(s, "apple") == 4);
        std::remove(path.c_str());
    }

    // --- Save / load: pickups (with instance data), loot sources, inventory weight + UI use key ---
    {
        Scene s("T");
        ItemInstanceData d; d.durability = 0.75f; d.uid = 9; d.Set("engraving", "for \"Ana\"");
        ItemPickup::Spawn(s, "sword", 1, d, {1, 2, 3});
        GameObject* c = s.CreateGameObject("Crate");
        auto* ls = c->AddComponent<LootSource>(); ls->table = "loot/crate.okayloot"; ls->when = LootSource::When::Open; ls->refill = 30; ls->seed = 5;
        GameObject* h = s.CreateGameObject("Hero");
        auto* inv = h->AddComponent<Inventory>(); inv->weightLimit = 20; inv->slowWhenOverWeight = true; inv->overWeightSpeed = 0.3f;
        auto* ui = h->AddComponent<InventoryUI>(); ui->useKey = 'q'; ui->clickToUse = true;
        std::string err;
        Scene r("R");
        CHECK(SceneSerializer::Deserialize(r, SceneSerializer::Serialize(s), &err));
        auto pks = r.FindObjectsOfType<ItemPickup>();
        CHECK(pks.size() == 1 && pks[0]->item == "sword" && pks[0]->data.uid == 9 && pks[0]->data.durability == 0.75f &&
              pks[0]->data.Get("engraving") == "for \"Ana\"");
        auto* rl = r.Find("Crate") ? r.Find("Crate")->GetComponent<LootSource>() : nullptr;
        CHECK(rl && rl->table == "loot/crate.okayloot" && rl->when == LootSource::When::Open && rl->refill == 30.0f && rl->seed == 5);
        auto* rh = r.Find("Hero");
        CHECK(rh && rh->GetComponent<Inventory>()->weightLimit == 20.0f && rh->GetComponent<Inventory>()->slowWhenOverWeight &&
              rh->GetComponent<Inventory>()->overWeightSpeed == 0.3f);
        CHECK(rh && rh->GetComponent<InventoryUI>()->useKey == 'q' && rh->GetComponent<InventoryUI>()->clickToUse);
    }

    TEST_MAIN_RESULT();
}
