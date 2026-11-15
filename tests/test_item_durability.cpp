// Item durability (okay/Inventory/Durability.hpp): settings from the item definition's custom fields (maxDurability,
// breakAction); Wear splits a stack so one item wears, breaks at 0 (kept or destroyed), fires on_item_broken; Repair
// to a value or to full, with materials taken all or nothing and never from the item itself; a broken item is
// refused by Use; a broken worn piece gives no armor or defense. Written for this engine.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/Equipment.hpp"
#include "okay/Inventory/Durability.hpp"
#include "okay/Inventory/InventoryOps.hpp"
#include "okay/Inventory/ItemDatabase.hpp"
#include <cstdio>

using namespace okay;

namespace {
void Def(const char* id, const char* maxDur, const char* breakAction, EquipSlot equip = EquipSlot::None,
         const char* armor = nullptr, const char* defense = nullptr) {
    ItemDefinition d;
    d.id = id; d.name = id;
    d.equip = equip;
    if (maxDur) d.custom.push_back({"maxDurability", maxDur});
    if (breakAction) d.custom.push_back({"breakAction", breakAction});
    if (armor) d.custom.push_back({"armor", armor});
    if (defense) d.custom.push_back({"defense", defense});
    ItemDatabase::Get().Put(d);
}
} // namespace

int main() {
    RUN_SUITE("item_durability");
    ItemDatabase::Get().Clear();
    Def("pick", "100", nullptr);
    Def("torch", "10", "destroy");
    Def("stone", nullptr, nullptr);
    Def("helmet", "50", nullptr, EquipSlot::Head, "5", "3");
    Def("scrap", nullptr, nullptr);

    {   // definitions and current values
        CHECK(durability::MaxOf("pick") == 100.0f);
        CHECK(durability::MaxOf("stone") == 0.0f);
        ItemInstanceData none;
        CHECK(durability::Tracked("pick", none) && !durability::Tracked("stone", none));
        CHECK(durability::Current("pick", none) == 100.0f);              // never worn: full
        CHECK(!durability::Broken("pick", none) && !durability::Broken("stone", none));
    }

    Scene s("dur");
    GameObject* player = s.CreateGameObject("Player");
    auto* inv = player->AddComponent<Inventory>();
    inv->capacity = 8;

    {   // wear: a stack of three splits so only one wears
        inv->slots.assign(8, Inventory::Slot{});
        inv->slots[0] = {"pick", 3, {}};
        const InvResult r = durability::Wear(inv, 0, 30.0f);
        CHECK(r.Ok());
        CHECK(inv->slots[0].count == 1 && inv->slots[0].data.durability == 70.0f);
        int rest = -1;
        for (int k = 1; k < (int)inv->slots.size(); ++k) if (inv->slots[k].item == "pick") rest = k;
        CHECK(rest > 0 && inv->slots[rest].count == 2 && inv->slots[rest].data.durability < 0.0f);   // the others untouched
        CHECK(!durability::Wear(inv, 0, 0.0f).Ok());
        CHECK(!durability::Wear(inv, 0, -5.0f).Ok());
        inv->slots[1] = {"stone", 1, {}};
        CHECK(durability::Wear(inv, 1, 5.0f).reason == "no durability");
        CHECK(!durability::Wear(inv, 6, 5.0f).Ok());                     // empty slot
    }
    {   // break and keep: at 0 it stays, Use refuses it, more wear is refused, repair brings it back
        inv->slots.assign(8, Inventory::Slot{});
        inv->slots[0] = {"pick", 1, {}};
        CHECK(durability::Wear(inv, 0, 250.0f).Ok());
        CHECK(inv->slots[0].item == "pick" && inv->slots[0].data.durability == 0.0f);
        CHECK(durability::Broken("pick", inv->slots[0].data));
        const InvResult u = InventoryOps::Use(inv, 0, player);
        CHECK(!u.Ok() && u.reason == "broken");
        CHECK(durability::Wear(inv, 0, 1.0f).reason == "broken");
        CHECK(durability::Repair(inv, 0, 40.0f).Ok());
        CHECK(inv->slots[0].data.durability == 40.0f);
        CHECK(!durability::Broken("pick", inv->slots[0].data));
        CHECK(InventoryOps::Use(inv, 0, player).reason != "broken");     // usable again (nothing here uses a pick)
        CHECK(durability::Repair(inv, 0).Ok());                          // to full
        CHECK(inv->slots[0].data.durability == 100.0f);
        CHECK(durability::Repair(inv, 0).reason == "not damaged");
        inv->slots[1] = {"stone", 1, {}};
        CHECK(durability::Repair(inv, 1).reason == "no durability");
    }
    {   // break and destroy
        inv->slots.assign(8, Inventory::Slot{});
        inv->slots[2] = {"torch", 1, {}};
        CHECK(durability::Wear(inv, 2, 4.0f).Ok());
        CHECK(inv->slots[2].data.durability == 6.0f);
        CHECK(durability::Wear(inv, 2, 6.0f).Ok());
        CHECK(Inventory::Empty(inv->slots[2]));
        CHECK(inv->Count("torch") == 0);
    }
    {   // repair with materials: all or nothing, and never paid with the item itself
        inv->slots.assign(8, Inventory::Slot{});
        inv->slots[0] = {"pick", 1, {}};
        inv->slots[1] = {"scrap", 2, {}};
        durability::Wear(inv, 0, 60.0f);
        CHECK(durability::Repair(inv, 0, 0.0f, {{"scrap", 3}}).reason == "missing materials");
        CHECK(inv->Count("scrap") == 2 && inv->slots[0].data.durability == 40.0f);   // nothing taken, nothing repaired
        CHECK(durability::Repair(inv, 0, 0.0f, {{"scrap", 2}}).Ok());
        CHECK(inv->Count("scrap") == 0 && inv->slots[0].data.durability == 100.0f);
        durability::Wear(inv, 0, 50.0f);
        CHECK(durability::Repair(inv, 0, 0.0f, {{"pick", 1}}).reason == "missing materials");   // only itself: refused
        CHECK(inv->slots[0].item == "pick" && inv->slots[0].count == 1);
        inv->slots[3] = {"pick", 1, {}};                                 // another pick can pay
        CHECK(durability::Repair(inv, 0, 0.0f, {{"pick", 1}}).Ok());
        CHECK(inv->slots[0].item == "pick" && inv->slots[0].data.durability == 100.0f);
        CHECK(Inventory::Empty(inv->slots[3]));
    }
    {   // worn armor: a broken helmet gives no armor or defense; repairing it brings them back
        auto* eq = player->AddComponent<Equipment>();
        const int head = eq->Find("Head");
        CHECK(head >= 0);
        CHECK(eq->Set(head, "helmet"));
        CHECK(eq->ArmorBonus() == 5.0f && eq->DefenseBonus() == 3);
        CHECK(durability::WearEquipped(eq, head, 20.0f).Ok());
        CHECK(eq->slots[head].data.durability == 30.0f && eq->ArmorBonus() == 5.0f);
        CHECK(durability::WearEquipped(eq, head, 30.0f).Ok());
        CHECK(durability::Broken("helmet", eq->slots[head].data));
        CHECK(eq->ArmorBonus() == 0.0f && eq->DefenseBonus() == 0);
        CHECK(durability::RepairEquipped(eq, head, 10.0f).Ok());
        CHECK(eq->ArmorBonus() == 5.0f && eq->DefenseBonus() == 3);
        CHECK(!durability::WearEquipped(eq, eq->Find("Feet"), 5.0f).Ok());   // nothing worn there
    }
    ItemDatabase::Get().Clear();
    TEST_MAIN_RESULT();
}
