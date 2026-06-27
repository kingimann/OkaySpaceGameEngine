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

    TEST_MAIN_RESULT();
}
