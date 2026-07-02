// Headless test for the DayZ/Unturned GridInventory: multi-cell placement, fit
// testing, auto-add, move within and across containers, weight.
#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("grid_inventory");

    Scene s("gi");
    GameObject* g = s.CreateGameObject("Bag");
    auto* inv = g->AddComponent<GridInventory>();
    inv->cols = 4; inv->rows = 4;

    int a = inv->AddItem("Rifle", 2, 4, 1, 4.0f);   // fills x0..1, y0..3
    CHECK(a == 0);
    CHECK(inv->items[0].x == 0 && inv->items[0].y == 0);
    int b = inv->AddItem("Can", 1, 1, 2, 0.4f);      // first free → (2,0)
    CHECK(b == 1);
    CHECK(inv->items[1].x == 2 && inv->items[1].y == 0);

    CHECK(inv->ItemAtCell(0, 0) == 0);
    CHECK(inv->ItemAtCell(2, 0) == 1);
    CHECK(inv->ItemAtCell(3, 3) == -1);

    CHECK(!inv->CanPlace(0, 0, 1, 1));               // sits on the rifle
    CHECK(inv->CanPlace(2, 1, 2, 2));                // free corner
    CHECK(!inv->CanPlace(3, 3, 2, 2));               // out of bounds

    CHECK(inv->TotalWeight() > 4.79f && inv->TotalWeight() < 4.81f);  // 4 + 0.4*2

    CHECK(inv->AddItem("Huge", 4, 4, 1) == -1);      // nothing fits a 4×4

    CHECK(inv->PlaceAt(1, 3, 3));                     // move the can
    CHECK(inv->items[1].x == 3 && inv->items[1].y == 3);
    CHECK(!inv->PlaceAt(0, 3, 0));                    // rifle (2×4) won't fit at x=3

    // Move the rifle into a second container.
    GameObject* g2 = s.CreateGameObject("Bag2");
    auto* inv2 = g2->AddComponent<GridInventory>();
    inv2->cols = 4; inv2->rows = 4;
    CHECK(inv->MoveTo(0, *inv2, 0, 0));
    CHECK(inv2->items.size() == 1 && inv2->items[0].name == "Rifle");
    CHECK(inv->items.size() == 1 && inv->items[0].name == "Can");

    // ---- Unturned multi-container screen: equipped (worn) + nearby (ground) ----
    Scene s2("unturned");
    GameObject* player = s2.CreateGameObject("Player");
    auto* body = player->AddComponent<GridInventory>();   // the player's own grid
    body->cols = 5; body->rows = 3; body->category = "Hands";
    auto* screen = player->AddComponent<GridInventoryUI>();
    screen->multiContainer = true; screen->nearbyRange = 4.0f;

    // A worn backpack: a CHILD of the player with its own grid → counts as equipped.
    GameObject* pack = s2.CreateGameObject("Backpack");
    pack->transform->SetParent(player->transform, false);
    auto* packInv = pack->AddComponent<GridInventory>();
    packInv->cols = 6; packInv->rows = 6; packInv->category = "Backpack";

    // A nearby loot crate (worldItem) 3m away → within range.
    GameObject* crate = s2.CreateGameObject("Crate");
    crate->transform->localPosition = {3.0f, 0.0f, 0.0f};
    auto* crateInv = crate->AddComponent<GridInventory>();
    crateInv->worldItem = true; crateInv->cols = 4; crateInv->rows = 4;
    crateInv->AddItem("Loot", 1, 1, 5, 0.2f);

    // A far crate 20m away → out of range, must NOT show.
    GameObject* far = s2.CreateGameObject("FarCrate");
    far->transform->localPosition = {20.0f, 0.0f, 0.0f};
    auto* farInv = far->AddComponent<GridInventory>();
    farInv->worldItem = true;

    // Nest the whole rig under one tidy group: the player + the crate are SIBLINGS
    // under "Unturned" (the crate is NOT a child of the player). The screen keys off
    // the object it sits on, so worn bags (player's children) stay "equipped" and the
    // sibling crate stays "nearby" — exactly the clean, grouped layout we want.
    GameObject* group = s2.CreateGameObject("Unturned");
    player->transform->SetParent(group->transform, false);
    crate->transform->SetParent(group->transform, false);

    std::vector<GridInventory*> equipped, nearby;
    screen->CollectContainers(equipped, nearby);
    CHECK(equipped.size() == 2);            // body + worn backpack
    CHECK(equipped[0] == body);             // primary first
    CHECK(equipped[1] == packInv);
    CHECK(nearby.size() == 1);              // only the in-range crate
    CHECK(nearby[0] == crateInv);

    // Loot a nearby item straight into the worn backpack (cross-container).
    CHECK(crateInv->MoveTo(0, *packInv, 0, 0));
    CHECK(packInv->items.size() == 1 && packInv->items[0].name == "Loot");
    CHECK(crateInv->items.empty());

    TEST_MAIN_RESULT();
}
