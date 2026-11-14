// Inventory equipment: dragging a helmet onto the head slot shows it on the character
// and raises defence; a worn backpack is a worn container whose contents travel with
// the item; equipment saves, loads and carries across scenes.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <okay/Components/Equipment.hpp>
#include <okay/Components/Sockets.hpp>
#include <okay/Inventory/InventoryInput.hpp>
#include <okay/Inventory/InventoryPersistence.hpp>
#include <cstdio>
#include <string>

using namespace okay;

static void Mouse(float x, float y, unsigned buttons) { Input::FeedMouse({x, y}, buttons); }

// A prefab file with one object (plus what `extra` adds to it).
template <class F>
static void MakePrefab(const std::string& path, const char* name, F extra) {
    Scene s("P");
    GameObject* go = s.CreateGameObject(name);
    extra(go);
    SceneSerializer::SaveObjectToFile(*go, path);
}

int main() {
    RUN_SUITE("inventory_equipment");
    MakePrefab("test_eq_helmet.okayprefab", "HelmetModel", [](GameObject* go) {
        go->AddComponent<BoxCollider3D>();
        go->AddComponent<Rigidbody3D>();
    });
    MakePrefab("test_eq_pack.okayprefab", "PackModel", [](GameObject* go) {
        auto* g = go->AddComponent<GridInventory>(); g->cols = 4; g->rows = 4; g->worldItem = true;
    });
    ItemDatabase& db = ItemDatabase::Get();
    db.Clear();
    db.LoadText("okayitems 1\n"
                "item \"helmet\"\n  name \"Iron Helmet\"\n  weight 2\n  equip head\n  prefab \"test_eq_helmet.okayprefab\"\n"
                "  custom \"armor\" \"4\"\n  custom \"defense\" \"3\"\nend\n"
                "item \"cap\"\n  name \"Cap\"\n  equip head\n  custom \"armor\" \"1\"\nend\n"
                "item \"sword\"\n  name \"Sword\"\n  weight 3\n  equip mainhand\nend\n"
                "item \"pack\"\n  name \"Backpack\"\n  equip back\n  prefab \"test_eq_pack.okayprefab\"\nend\n"
                "item \"apple\"\n  name \"Apple\"\n  stack 10\nend\n");

    // --- Sockets: named child, then a Character bone, else nothing ---
    {
        Scene s("T");
        GameObject* hero = s.CreateGameObject("Hero");
        GameObject* hand = s.CreateGameObject("HandSocket_R");
        hand->transform->SetParent(hero->transform, false);
        CHECK(FindSocket(hero, "HandSocket_R") == hand);
        CHECK(FindSocket(hero, "nope") == nullptr && FindSocket(nullptr, "head") == nullptr);
        GameObject* other = s.CreateGameObject("Other");
        GameObject* far = s.CreateGameObject("FarSocket");
        far->transform->SetParent(other->transform, false);
        CHECK(FindSocket(hero, "FarSocket") == nullptr);          // only inside this character
        auto* ch = hero->AddComponent<Character>();
        ch->BuildParts();
        GameObject* head = FindSocket(hero, "head");
        CHECK(head && head == ch->Part(Character::BoneIndex("head")));
    }

    // --- Equip a helmet from the bag: shown at the head, armor and defence go up ---
    {
        Scene s("T");
        GameObject* hero = s.CreateGameObject("Hero");
        GameObject* headSock = s.CreateGameObject("head");
        headSock->transform->SetParent(hero->transform, false);
        auto* inv = hero->AddComponent<Inventory>(); inv->capacity = 4; inv->weightLimit = 10.0f;
        auto* eq = hero->AddComponent<Equipment>();
        auto* hp = hero->AddComponent<HealthStat>();
        auto* st = hero->AddComponent<Stats>();
        auto* sc = hero->AddComponent<ScriptComponent>("okayscript");
        std::string err;
        CHECK(sc->LoadSource("var log = \"\";\n"
                             "function on_equip(slot, item) { log = log + \"+\" + slot + \":\" + item; }\n"
                             "function on_unequip(slot, item) { log = log + \"-\" + slot + \":\" + item; }\n", &err));
        s.Start();
        inv->Add("helmet", 1); inv->Add("sword", 1); inv->Add("cap", 1);
        const int head = eq->Find("Head"), hand = eq->Find("Main Hand");
        CHECK(head >= 0 && hand >= 0);
        CHECK(!eq->EquipFrom(*inv, 1, head).Ok());                       // a sword isn't a hat
        CHECK(inv->Count("sword") == 1 && eq->ItemIn(head).empty());
        CHECK(eq->EquipFrom(*inv, 0, head).Ok());
        CHECK(eq->ItemIn(head) == "helmet" && inv->Count("helmet") == 0);
        GameObject* shown = eq->Shown(head);
        CHECK(shown && shown->transform->Parent() == headSock->transform && shown->transient);
        CHECK(shown && !shown->GetComponent<Rigidbody3D>()->enabled && !shown->GetComponent<BoxCollider3D>()->enabled);
        CHECK(hp->equipArmor == 4.0f && st->equipDefense == 3);
        hp->Damage(10.0f);
        CHECK(hp->health == 94.0f);                                      // 10 - 4 armor
        CHECK(inv->wornWeight == 2.0f && inv->TotalWeight() == 2.0f + 3.0f);
        // Swapping in the cap puts the helmet back in the bag, in the cap's old slot.
        CHECK(eq->EquipFrom(*inv, 2, head).Ok());
        CHECK(eq->ItemIn(head) == "cap" && inv->slots[2].item == "helmet" && hp->equipArmor == 1.0f && st->equipDefense == 0);
        CHECK(eq->Shown(head) == nullptr);                               // the cap has no prefab
        // Unequip into the bag; a full bag refuses and nothing changes.
        inv->Add("apple", 10); inv->Add("apple", 10);                     // 4 slots: sword, apple, helmet, apple
        CHECK(inv->IsFull());
        CHECK(!eq->Unequip(head, *inv).Ok() && eq->ItemIn(head) == "cap");
        inv->Remove("apple", 10);
        CHECK(eq->Unequip(head, *inv).Ok() && eq->ItemIn(head).empty() && inv->Count("cap") == 1 && hp->equipArmor == 0.0f);
        const std::string log = sc->VM()->GetGlobal("log").AsString();
        CHECK(log == "+Head:helmet-Head:helmet+Head:cap-Head:cap");
        // Using a weapon equips it to the main hand.
        int sword = -1;
        for (int i = 0; i < (int)inv->slots.size(); ++i) if (inv->slots[i].item == "sword") sword = i;
        CHECK(InventoryOps::Use(inv, sword, hero).Ok() && eq->ItemIn(hand) == "sword" && inv->Count("sword") == 0);
        // The stat bonuses and shown objects go when the Equipment does.
        eq->EquipFrom(*inv, 2, head);
        CHECK(hp->equipArmor == 4.0f);
        hero->RemoveComponent(eq);
        CHECK(hp->equipArmor == 0.0f && st->equipDefense == 0 && inv->wornWeight == 0.0f);
    }

    // --- Drag from the bag onto the head slot, and back off it ---
    {
        Scene s("T");
        GameObject* hero = s.CreateGameObject("Hero");
        auto* inv = hero->AddComponent<Inventory>(); inv->capacity = 18;
        auto* eq = hero->AddComponent<Equipment>();
        auto* ui = hero->AddComponent<InventoryUI>(); ui->hotbarSlots = 9;
        s.Start();
        inv->Add("helmet", 1);
        ui->open = true;
        InventoryInput::HotbarLayout L = InventoryInput::LayoutHotbar(*ui, 1280, 720);
        CHECK(L.equip == eq && L.equipN == (int)eq->slots.size());
        const int head = eq->Find("Head");
        float hx, hy, sx, sy; L.EquipXY(head, hx, hy); L.SlotXY(0, sx, sy);
        CHECK(L.PickEquip(hx + 2, hy + 2) == head && L.Pick(sx + 2, sy + 2) == 0);
        Mouse(sx + 4, sy + 4, 1); InventoryInput::HandleHotbar(*ui, L, Input::MousePosition());   // lift the helmet
        Mouse(hx + 4, hy + 4, 1);
        CHECK(InventoryInput::HandleHotbar(*ui, L, Input::MousePosition()).equipHover == head);
        Mouse(hx + 4, hy + 4, 0); InventoryInput::HandleHotbar(*ui, L, Input::MousePosition());   // drop on Head
        CHECK(eq->ItemIn(head) == "helmet" && inv->Count("helmet") == 0 && ui->dragIndex == -1);
        Mouse(hx + 4, hy + 4, 1); InventoryInput::HandleHotbar(*ui, L, Input::MousePosition());   // pick it off
        CHECK(ui->equipDrag == head);
        float s4x, s4y; L.SlotXY(4, s4x, s4y);
        Mouse(s4x + 4, s4y + 4, 0); InventoryInput::HandleHotbar(*ui, L, Input::MousePosition());   // into slot 4
        CHECK(eq->ItemIn(head).empty() && inv->slots.size() > 4 && inv->slots[4].item == "helmet" && ui->equipDrag == -1);
        // The wrong slot refuses: the helmet stays in the bag.
        const int hand = eq->Find("Main Hand");
        float mx, my; L.EquipXY(hand, mx, my);
        Mouse(s4x + 4, s4y + 4, 1); InventoryInput::HandleHotbar(*ui, L, Input::MousePosition());
        Mouse(mx + 4, my + 4, 0); InventoryInput::HandleHotbar(*ui, L, Input::MousePosition());
        CHECK(eq->ItemIn(hand).empty() && inv->slots[4].item == "helmet");
    }

    // --- A worn backpack is a worn container; its contents travel with the item ---
    {
        Scene s("T");
        GameObject* hero = s.CreateGameObject("Hero");
        auto* inv = hero->AddComponent<Inventory>(); inv->capacity = 9;
        auto* eq = hero->AddComponent<Equipment>();
        auto* gui = hero->AddComponent<GridInventoryUI>();
        hero->AddComponent<GridInventory>();
        s.Start();
        inv->Add("pack", 1);
        const int back = eq->Find("Back");
        CHECK(eq->EquipFrom(*inv, 0, back).Ok());
        GameObject* packObj = eq->Shown(back);
        auto* bag = packObj ? packObj->GetComponent<GridInventory>() : nullptr;
        CHECK(bag && !bag->worldItem);
        std::vector<GridInventory*> worn, nearby;
        gui->CollectContainers(worn, nearby);
        CHECK(worn.size() == 2 && worn[1] == bag && nearby.empty());
        CHECK(bag && bag->AddItem("apple", 1, 1, 6) >= 0);
        CHECK(eq->Unequip(back, *inv).Ok() && inv->slots[0].item == "pack");
        bool hasBag = false;
        for (const auto& kv : inv->slots[0].data.values) if (kv.first == "bag") hasBag = true;
        CHECK(hasBag);
        s.Update(0.016f);                                                // the shown pack is gone
        CHECK(eq->Shown(back) == nullptr);
        CHECK(eq->EquipFrom(*inv, 0, back).Ok());
        auto* again = eq->Shown(back) ? eq->Shown(back)->GetComponent<GridInventory>() : nullptr;
        CHECK(again && again->items.size() == 1 && again->items[0].name == "apple" && again->items[0].count == 6);
        CHECK(eq->slots[back].data.values.empty());                      // the live bag holds them now

        // Saving the scene keeps what's worn, the bag's contents included.
        std::string text = SceneSerializer::Serialize(s), err;
        CHECK(text.find("PackModel") == std::string::npos);              // the shown pack isn't saved
        Scene back2("B");
        CHECK(SceneSerializer::Deserialize(back2, text, &err));
        GameObject* h2 = back2.Find("Hero");
        auto* eq2 = h2 ? h2->GetComponent<Equipment>() : nullptr;
        CHECK(eq2 && eq2->ItemIn(back) == "pack" && eq2->slots.size() == eq->slots.size());
        back2.Start();
        auto* bag2 = eq2 && eq2->Shown(back) ? eq2->Shown(back)->GetComponent<GridInventory>() : nullptr;
        CHECK(bag2 && bag2->items.size() == 1 && bag2->items[0].count == 6);

        // Save / load just the player's inventories, equipment included.
        const std::string saved = InventoryPersistence::SerializeObject(*hero);
        eq->Clear(back);
        s.Update(0.016f);
        CHECK(eq->ItemIn(back).empty());
        CHECK(InventoryPersistence::LoadObjectText(*hero, saved));
        s.Update(0.016f);
        auto* bag3 = eq->Shown(back) ? eq->Shown(back)->GetComponent<GridInventory>() : nullptr;
        CHECK(eq->ItemIn(back) == "pack" && bag3 && bag3->items.size() == 1);
    }

    // --- Equipment with keepOnSceneLoad goes to the next level ---
    {
        const std::string next = "test_eq_next.okayscene";
        {
            Scene n("Next");
            GameObject* p = n.CreateGameObject("Player");
            p->AddComponent<Equipment>();
            CHECK(SceneSerializer::SaveToFile(n, next));
        }
        Scene s("First");
        GameObject* p = s.CreateGameObject("Player");
        auto* eq = p->AddComponent<Equipment>(); eq->keepOnSceneLoad = true;
        s.Start();
        CHECK(eq->Set(eq->Find("Head"), "helmet"));
        s.RequestLoad(next);
        s.Update(0.016f);
        GameObject* np = s.Find("Player");
        auto* neq = np ? np->GetComponent<Equipment>() : nullptr;
        CHECK(neq && neq->ItemIn(neq->Find("Head")) == "helmet");
        std::remove(next.c_str());
    }

    std::remove("test_eq_helmet.okayprefab");
    std::remove("test_eq_pack.okayprefab");
    TEST_MAIN_RESULT();
}
