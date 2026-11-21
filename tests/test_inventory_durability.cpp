// Durability wear and repair on slot bags: wear down, break at zero, repair up to the
// definition's maxDurability, and hostile inputs (NaN, negative, bad slot, stacks) change nothing.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <okay/Inventory/InventoryOps.hpp>
#include <cmath>
#include <limits>

using namespace okay;
using S = InvResult::Status;

int main() {
    RUN_SUITE("inventory_durability");
    ItemDatabase::Get().Clear();
    { ItemDefinition d; d.id = "sword"; d.name = "sword"; d.maxStack = 1; d.custom.push_back({"maxDurability", "100"}); ItemDatabase::Get().Put(d); }
    { ItemDefinition d; d.id = "wood"; d.name = "wood"; d.maxStack = 10; ItemDatabase::Get().Put(d); }
    { ItemDefinition d; d.id = "club"; d.name = "club"; d.maxStack = 1; ItemDatabase::Get().Put(d); }

    Inventory inv; inv.capacity = 6;
    inv.slots.resize(4);
    inv.slots[0].item = "sword"; inv.slots[0].count = 1; inv.slots[0].data.durability = 50.0f;
    inv.slots[1].item = "wood"; inv.slots[1].count = 5;
    inv.slots[2].item = "club"; inv.slots[2].count = 1; inv.slots[2].data.durability = 3.0f;

    // wear
    CHECK(InventoryOps::Wear(&inv, 0, 20.0f).status == S::Ok && inv.slots[0].data.durability == 30.0f);
    CHECK(InventoryOps::Wear(&inv, 0, 0.0f).Ok() && inv.slots[0].data.durability == 30.0f);
    CHECK(!InventoryOps::Wear(&inv, 0, -1.0f).Ok() && inv.slots[0].data.durability == 30.0f);
    CHECK(!InventoryOps::Wear(&inv, 0, std::numeric_limits<float>::quiet_NaN()).Ok());
    CHECK(!InventoryOps::Wear(&inv, 0, std::numeric_limits<float>::infinity()).Ok());
    CHECK(!InventoryOps::Wear(&inv, 1, 1.0f).Ok() && inv.slots[1].count == 5);   // wood does not wear
    CHECK(!InventoryOps::Wear(&inv, 3, 1.0f).Ok());                              // empty slot
    CHECK(!InventoryOps::Wear(&inv, 9, 1.0f).Ok() && !InventoryOps::Wear(&inv, -1, 1.0f).Ok() && !InventoryOps::Wear(nullptr, 0, 1.0f).Ok());
    inv.slots[0].count = 2;
    CHECK(!InventoryOps::Wear(&inv, 0, 1.0f).Ok() && !InventoryOps::Repair(&inv, 0, 1.0f).Ok());   // a stack does not wear
    inv.slots[0].count = 1;

    // repair
    CHECK(InventoryOps::Repair(&inv, 0, 30.0f).status == S::Ok && inv.slots[0].data.durability == 60.0f);
    CHECK(InventoryOps::Repair(&inv, 0, 500.0f).status == S::Partial && inv.slots[0].data.durability == 100.0f);
    CHECK(!InventoryOps::Repair(&inv, 0, 1.0f).Ok());                            // already full
    CHECK(!InventoryOps::Repair(&inv, 2, 1.0f).Ok() && inv.slots[2].data.durability == 3.0f);   // no maxDurability defined
    CHECK(!InventoryOps::Repair(&inv, 0, 0.0f).Ok() && !InventoryOps::Repair(&inv, 0, -5.0f).Ok());
    CHECK(!InventoryOps::Repair(&inv, 0, std::numeric_limits<float>::quiet_NaN()).Ok());

    // break at zero
    CHECK(InventoryOps::Wear(&inv, 2, 2.0f).moved == 0 && inv.slots[2].data.durability == 1.0f);
    const InvResult b = InventoryOps::Wear(&inv, 2, 5.0f);
    CHECK(b.Ok() && b.moved == 1 && Inventory::Empty(inv.slots[2]));
    CHECK(inv.Count("club") == 0 && inv.Count("sword") == 1);

    TEST_MAIN_RESULT();
}
