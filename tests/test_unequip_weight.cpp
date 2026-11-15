// Unequipping into a bag that enforces its weight limit. A worn item's weight is already counted in the bag's
// wornWeight, so taking it off does not change the carried total, and Equipment::Unequip never refuses it for weight
// (inventory.md used to list an early refusal here; checking showed Unequip does not test the weight at all, which is
// right). The total stays the same before and after, near the limit and over it.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/Equipment.hpp"
#include "okay/Inventory/ItemDatabase.hpp"
#include <cstdio>

using namespace okay;

namespace {
void Def(const char* id, float weight, EquipSlot equip = EquipSlot::None) {
    ItemDefinition d; d.id = d.name = id; d.weight = weight; d.equip = equip;
    ItemDatabase::Get().Put(d);
}
struct Rig {
    Scene s{"w"};
    Inventory* inv = nullptr;
    Equipment* eq = nullptr;
    int chest = -1;
    Rig(int rocks) {
        GameObject* p = s.CreateGameObject("Player");
        inv = p->AddComponent<Inventory>();
        inv->capacity = 8; inv->weightLimit = 10.0f; inv->enforceWeight = true;
        inv->slots.assign(8, Inventory::Slot{});
        if (rocks > 0) inv->slots[0] = {"rock", rocks, {}};
        eq = p->AddComponent<Equipment>();
        chest = eq->Find("Chest");
        CHECK(eq->Set(chest, "mail"));
        s.Start(); s.Update(0.016f);
    }
};
} // namespace

int main() {
    RUN_SUITE("unequip_weight");
    ItemDatabase::Get().Clear();
    Def("rock", 1.0f);
    Def("mail", 3.0f, EquipSlot::Chest);

    for (int rocks : {6, 7, 8}) {   // 9, 10 and 11 of 10 carried in all (11: already over before taking it off)
        Rig r(rocks);
        CHECK(r.inv->wornWeight == 3.0f);
        const float before = r.inv->TotalWeight();
        const InvResult u = r.eq->Unequip(r.chest, *r.inv);
        std::printf("  %d rocks + 3 worn: unequip %s, total %.1f -> %.1f\n", rocks, u.Ok() ? "ok" : "refused", before, r.inv->TotalWeight());
        CHECK(u.Ok());
        CHECK(r.inv->Count("mail") == 1);
        CHECK(r.eq->slots[r.chest].item.empty());
        CHECK(r.inv->wornWeight == 0.0f);
        CHECK(r.inv->TotalWeight() == before);                            // the same total: nothing was added
    }
    ItemDatabase::Get().Clear();
    TEST_MAIN_RESULT();
}
