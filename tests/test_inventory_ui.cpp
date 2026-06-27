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

    TEST_MAIN_RESULT();
}
