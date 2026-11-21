// Using a tool item wears it (definition custom field "wearPerUse"), breaks it at zero, and
// reports what happened.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <okay/Inventory/InventoryOps.hpp>

using namespace okay;

int main() {
    RUN_SUITE("inventory_use");
    ItemDatabase::Get().Clear();
    { ItemDefinition d; d.id = "pick"; d.name = "pick"; d.maxStack = 1; d.custom.push_back({"wearPerUse", "4"}); ItemDatabase::Get().Put(d); }
    { ItemDefinition d; d.id = "rock"; d.name = "rock"; d.maxStack = 9; ItemDatabase::Get().Put(d); }
    { ItemDefinition d; d.id = "badpick"; d.name = "badpick"; d.maxStack = 1; d.custom.push_back({"wearPerUse", "-3"}); ItemDatabase::Get().Put(d); }

    Scene s("T");
    GameObject* hero = s.CreateGameObject("Hero");
    auto* inv = hero->AddComponent<Inventory>(); inv->capacity = 4;
    inv->slots.resize(4);
    inv->slots[0].item = "pick"; inv->slots[0].count = 1; inv->slots[0].data.durability = 10.0f;
    inv->slots[1].item = "rock"; inv->slots[1].count = 3;
    inv->slots[2].item = "badpick"; inv->slots[2].count = 1; inv->slots[2].data.durability = 10.0f;

    InvResult r = InventoryOps::Use(inv, 0, hero);
    CHECK(r.Ok() && !r.broke && inv->slots[0].data.durability == 6.0f);
    r = InventoryOps::Use(inv, 0, hero);
    CHECK(r.Ok() && !r.broke && inv->slots[0].data.durability == 2.0f);
    r = InventoryOps::Use(inv, 0, hero);   // 2 - 4 <= 0: breaks
    CHECK(r.Ok() && r.broke && Inventory::Empty(inv->slots[0]));
    CHECK(!InventoryOps::Use(inv, 0, hero).Ok());                                  // nothing left
    CHECK(!InventoryOps::Use(inv, 1, hero).Ok() && inv->slots[1].count == 3);      // no use, no wear field
    CHECK(!InventoryOps::Use(inv, 2, hero).Ok() && inv->slots[2].data.durability == 10.0f);   // bad wear value ignored
    CHECK(!InventoryOps::Use(inv, 0, nullptr).Ok());
    TEST_MAIN_RESULT();
}
