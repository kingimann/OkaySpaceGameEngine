// Headless test for the Minecraft-style InventoryUI: it reads from a sibling/owner
// Inventory and reports the selected (in-hand) item.
#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("inventory_ui");

    Scene s("iu");
    GameObject* player = s.CreateGameObject("Player");
    auto* inv = player->AddComponent<Inventory>();
    inv->capacity = 36;
    inv->Add("Dirt", 64);
    inv->Add("Stone", 10);

    auto* ui = player->AddComponent<InventoryUI>();
    CHECK(ui->Inv() == inv);                       // finds the sibling Inventory
    CHECK(ui->SelectedItem() == "Dirt");           // slot 0 selected by default
    CHECK(ui->SelectedCount() == 64);

    ui->selected = 1;
    CHECK(ui->SelectedItem() == "Stone");
    CHECK(ui->SelectedCount() == 10);

    ui->selected = 5;                              // empty slot
    CHECK(ui->SelectedItem() == "");
    CHECK(ui->SelectedCount() == 0);

    // A UI on a child object resolves the Inventory on the player root.
    GameObject* cam = s.CreateGameObject("Cam");
    cam->transform->SetParent(player->transform, false);
    auto* ui2 = cam->AddComponent<InventoryUI>();
    CHECK(ui2->Inv() == inv);

    // Drag-and-drop: MoveSlot swaps two stacks, merges identical ones.
    ui->MoveSlot(0, 1);                            // swap Dirt <-> Stone
    CHECK(inv->slots[0].item == "Stone" && inv->slots[1].item == "Dirt");
    inv->slots.push_back({"Stone", 5});            // a second Stone stack at the end
    int last = (int)inv->slots.size() - 1;
    CHECK(inv->slots[last].item == "Stone");
    ui->MoveSlot(last, 0);                         // drop Stone onto Stone → merge
    CHECK(inv->slots[0].item == "Stone");
    CHECK(inv->slots[0].count == 15);              // 10 + 5 merged
    CHECK(inv->Count("Stone") == 15);

    // Drop onto an EMPTY slot: the item lands at that slot index, leaving a gap —
    // this is the case that previously refused the drop.
    {
        Scene s3("iu3");
        GameObject* p3 = s3.CreateGameObject("P");
        auto* inv3 = p3->AddComponent<Inventory>(); inv3->capacity = 36;
        inv3->Add("Dirt", 10);                      // slot 0
        auto* u3 = p3->AddComponent<InventoryUI>();
        u3->MoveSlot(0, 5);                          // drop onto empty slot 5
        CHECK((int)inv3->slots.size() == 6);
        CHECK(inv3->slots[5].item == "Dirt");
        CHECK(Inventory::Empty(inv3->slots[0]));    // gap left behind
        CHECK(inv3->Count("Dirt") == 10);
        inv3->Add("Stone", 3);                      // new loot reuses the hole at slot 0
        CHECK(inv3->slots[0].item == "Stone");
    }

    // Sort modes: Name (A→Z), Count (biggest first), Rarity (rarities-list order).
    {
        Scene s4("iu4");
        GameObject* p4 = s4.CreateGameObject("P");
        auto* inv4 = p4->AddComponent<Inventory>(); inv4->capacity = 36;
        inv4->Add("Zucchini", 2);
        inv4->Add("Apple", 5);
        inv4->Add("Mango", 9);
        auto* u4 = p4->AddComponent<InventoryUI>();

        u4->sortMode = InventoryUI::SortMode::Name;
        u4->Sort();
        CHECK(inv4->slots[0].item == "Apple");
        CHECK(inv4->slots[1].item == "Mango");
        CHECK(inv4->slots[2].item == "Zucchini");

        u4->sortMode = InventoryUI::SortMode::Count;
        u4->Sort();
        CHECK(inv4->slots[0].item == "Mango");      // 9 first
        CHECK(inv4->slots[0].count == 9);
        CHECK(inv4->slots[2].count == 2);           // Zucchini (2) last

        u4->sortMode = InventoryUI::SortMode::Rarity;
        u4->rarities.push_back({"Mango", Color::White});
        u4->rarities.push_back({"Zucchini", Color::White});
        u4->Sort();
        CHECK(inv4->slots[0].item == "Mango");      // listed first = rarest
        CHECK(inv4->slots[1].item == "Zucchini");
        CHECK(inv4->slots[2].item == "Apple");      // unranked sorts last

        // DropSelected removes one of the selected item and returns its name.
        u4->selected = 0;
        int before = inv4->Count("Mango");
        std::string dropped = u4->DropSelected();
        CHECK(dropped == "Mango");
        CHECK(inv4->Count("Mango") == before - 1);
    }

    // Sort mode + drop key round-trip through serialization.
    {
        Scene s5("iu5");
        GameObject* p5 = s5.CreateGameObject("P");
        p5->AddComponent<Inventory>();
        auto* u5 = p5->AddComponent<InventoryUI>();
        u5->sortMode = InventoryUI::SortMode::Rarity;
        u5->sortOnClose = true;
        u5->dropKey = 'q';
        Scene loaded("L");
        CHECK(SceneSerializer::Deserialize(loaded, SceneSerializer::Serialize(s5)));
        auto* r5 = loaded.Find("P")->GetComponent<InventoryUI>();
        CHECK(r5 != nullptr);
        CHECK(r5->sortMode == InventoryUI::SortMode::Rarity);
        CHECK(r5->sortOnClose);
        CHECK(r5->dropKey == 'q');
    }

    TEST_MAIN_RESULT();
}
