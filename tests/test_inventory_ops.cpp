// The shared inventory layer: InventoryOps (every UI change goes through it, with an
// ok / partial / rejected result) and InventoryInput (layout + mouse handling used by
// both the built player and the editor Play view), driven here with fed mouse input.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <okay/Inventory/InventoryInput.hpp>
#include <okay/Inventory/InventoryOps.hpp>
#include <string>

using namespace okay;

// One frame of mouse input (button mask: bit 0 = left, bit 1 = right).
static void Mouse(float x, float y, unsigned buttons) { Input::FeedMouse({x, y}, buttons); }

int main() {
    RUN_SUITE("inventory_ops");
    using S = InvResult::Status;

    // --- Slot ops: move / merge / swap / partial / bounds ---
    {
        Inventory a; a.capacity = 4; a.maxStack = 10;
        Inventory b; b.capacity = 4; b.maxStack = 10;
        a.Add("wood", 8); a.Add("stone", 3);         // a: [wood 8][stone 3]
        b.Add("wood", 6);                            // b: [wood 6]
        InvResult r = InventoryOps::Move(&a, 0, &b, 0);
        CHECK(r.status == S::Partial && r.moved == 4);   // b's stack fills to 10, 4 stay
        CHECK(a.slots[0].count == 4 && b.slots[0].count == 10);
        r = InventoryOps::Move(&a, 0, &b, 0);
        CHECK(r.status == S::Rejected && r.reason == "stack full");
        r = InventoryOps::Move(&a, 1, &b, 0);        // stone onto wood: swap
        CHECK(r.status == S::Ok && b.slots[0].item == "stone" && a.slots[1].item == "wood");
        r = InventoryOps::Move(&a, 0, &a, 1, 2);     // 2 of a's wood onto a different item: rejected
        CHECK(!r.Ok() && a.slots[0].count == 4);
        r = InventoryOps::Move(&a, 0, &a, 3, 2);     // part-stack into an empty slot
        CHECK(r.status == S::Ok && a.slots[0].count == 2 && a.slots[3].count == 2);
        CHECK(!InventoryOps::Move(&a, 0, &a, 4).Ok());   // past capacity
        CHECK(a.Count("wood") + b.Count("wood") == 14 && a.Count("stone") + b.Count("stone") == 3);

        CHECK(InventoryOps::Split(&b, 0, 1).status == S::Ok && b.Count("stone") == 3);
        CHECK(!InventoryOps::Split(&b, 0, 5).Ok());       // can't split all of it
        CHECK(InventoryOps::Drop(&b, 0, 1).moved == 1 && b.Count("stone") == 2);

        Inventory full; full.capacity = 1; full.maxStack = 5; full.Add("wood", 4);
        Inventory src; src.Add("wood", 3);
        r = InventoryOps::Transfer(&src, 0, &full);
        CHECK(r.status == S::Partial && r.moved == 1 && src.slots[0].count == 2 && full.Count("wood") == 5);

        Inventory bag; bag.capacity = 1; bag.Add("wood", 2);
        CHECK(!InventoryOps::Craft(&bag, {{"wood", 3}}, {{"plank", 1}}).Ok());
        CHECK(InventoryOps::Craft(&bag, {{"wood", 2}}, {{"plank", 4}}).moved == 4 && bag.Count("plank") == 4);
    }

    // --- Grid ops ---
    {
        GridInventory g; g.cols = 2; g.rows = 1; g.maxStack = 30;
        g.AddItem("ammo", 1, 1, 20);
        GridInventory h; h.maxStack = 30; h.AddItem("ammo", 1, 1, 25);
        InvResult r = InventoryOps::Move(&h, 0, &g, 0, 0);
        CHECK(r.status == S::Partial && r.moved == 10 && g.items[0].count == 30 && h.items[0].count == 15);
        CHECK(InventoryOps::Split(&g, 0, 10).Ok() && g.items.size() == 2);
        CHECK(!InventoryOps::Split(&g, 0, 5).Ok());       // no free cell
    }

    // --- Hotbar layout + input: the same path the player and editor use ---
    {
        Scene s("T");
        GameObject* p = s.CreateGameObject("P");
        auto* inv = p->AddComponent<Inventory>();
        inv->capacity = 18; inv->maxStack = 64;
        auto* ui = p->AddComponent<InventoryUI>();
        ui->hotbarSlots = 9; ui->scaleToScreen = false;
        inv->Add("wood", 10);
        const auto L = InventoryInput::LayoutHotbar(*ui, 800, 600);
        CHECK(L.total == 18 && L.rows == 1);
        float x0, y0, x5, y5, xb, yb;
        L.SlotXY(0, x0, y0); L.SlotXY(5, x5, y5); L.SlotXY(9, xb, yb);
        CHECK(L.Pick(x0 + 1, y0 + 1) == 0 && L.Pick(x5 + L.sz - 1, y5 + 1) == 5 && L.Pick(xb + 2, yb + 2) == 9);
        CHECK(yb < y0);                                       // the backpack sits above a bottom hotbar
        CHECK(L.Pick(-500, -500) == -1);

        ui->open = true;
        Mouse(x0 + 4, y0 + 4, 1); InventoryInput::HandleHotbar(*ui, L, Input::MousePosition());   // press on wood
        CHECK(ui->dragIndex == 0);
        Mouse(x5 + 4, y5 + 4, 1);
        auto fr = InventoryInput::HandleHotbar(*ui, L, Input::MousePosition());
        CHECK(fr.target == 5);
        Mouse(x5 + 4, y5 + 4, 0); InventoryInput::HandleHotbar(*ui, L, Input::MousePosition());   // release
        CHECK(ui->dragIndex == -1 && inv->slots.size() == 6 && inv->slots[5].item == "wood" && inv->slots[5].count == 10);

        // Right-click lifts half; dropping it in the backpack splits the stack.
        Mouse(x5 + 4, y5 + 4, 2); InventoryInput::HandleHotbar(*ui, L, Input::MousePosition());
        CHECK(ui->dragIndex == 5 && ui->dragAmount == 5);
        Mouse(xb + 4, yb + 4, 0); InventoryInput::HandleHotbar(*ui, L, Input::MousePosition());
        CHECK(inv->slots[5].count == 5 && inv->slots[9].count == 5 && inv->Count("wood") == 10);

        ui->open = false;                                     // closing clears any drag
        ui->dragIndex = 3;
        InventoryInput::HandleHotbar(*ui, L, Input::MousePosition());
        CHECK(ui->dragIndex == -1);
    }

    // --- Chest layout + input ---
    {
        Scene s("T");
        GameObject* hero = s.CreateGameObject("Hero"); hero->tag = "Player";
        auto* pinv = hero->AddComponent<Inventory>(); pinv->capacity = 9;
        GameObject* box = s.CreateGameObject("Box");
        auto* cinv = box->AddComponent<Inventory>(); cinv->capacity = 6;
        cinv->Add("gold", 3);
        auto* ch = box->AddComponent<ChestInventory>();
        ch->open = true;
        const auto L = InventoryInput::LayoutChest(*ch, 800, 600);
        CHECK(L.chest == cinv && L.player == pinv && L.chestN == 6 && L.playN == 9);
        float cx, cy, px, py;
        L.SlotXY(0, 0, cx, cy); L.SlotXY(1, 4, px, py);
        int side = -1, idx = -1;
        CHECK(L.Pick(px + 2, py + 2, side, idx) && side == 1 && idx == 4);
        Mouse(cx + 2, cy + 2, 1); InventoryInput::HandleChest(*ch, L, Input::MousePosition());
        CHECK(ch->dragSide == 0 && ch->dragIndex == 0);
        Mouse(px + 2, py + 2, 0); InventoryInput::HandleChest(*ch, L, Input::MousePosition());
        CHECK(ch->dragSide == -1 && pinv->slots.size() == 5 && pinv->slots[4].item == "gold" && cinv->Count("gold") == 0);
    }

    // --- Grid layout + input ---
    {
        Scene s("T");
        GameObject* p = s.CreateGameObject("P");
        auto* bag = p->AddComponent<GridInventory>();
        bag->cols = 4; bag->rows = 2; bag->open = true;
        bag->AddItem("rifle", 1, 2);
        auto* ui = p->AddComponent<GridInventoryUI>();
        const auto L = InventoryInput::LayoutGrid(*ui, *bag, 1024, 768);
        CHECK(L.panels.size() == 1 && L.panels[0].inv == bag);
        const auto& P = L.panels[0];
        auto cell = [&](int x, int y) { return Vec2{P.ox + x * (L.cs + L.gp) + 3, P.oy + y * (L.cs + L.gp) + 3}; };
        Vec2 c = cell(0, 0);
        Mouse(c.x, c.y, 1); InventoryInput::HandleGrid(*ui, L, InventoryInput::HoverGrid(L, Input::MousePosition()));
        CHECK(ui->dragInv == bag && ui->dragIndex == 0);
        c = cell(3, 0);
        Mouse(c.x, c.y, 0); InventoryInput::HandleGrid(*ui, L, InventoryInput::HoverGrid(L, Input::MousePosition()));
        CHECK(!ui->dragInv && bag->items[0].x == 3 && bag->items[0].y == 0);
    }

    TEST_MAIN_RESULT();
}
