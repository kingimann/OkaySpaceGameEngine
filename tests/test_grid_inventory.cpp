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

    TEST_MAIN_RESULT();
}
