// Unequipping into a bag that enforces its weight limit: the worn item's weight is already counted in the bag's
// wornWeight, so moving it into the bag must not count it twice (inventory.md "Not done": it was refused a little early
// near the limit). A move that really goes over is still refused, and the worn weight is left as it was.
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

    {   // 6 kg in the bag + 3 kg worn = 9 of 10: taking the mail off keeps 9, so it fits
        Rig r(6);
        CHECK(r.inv->wornWeight == 3.0f);
        const InvResult u = r.eq->Unequip(r.chest, *r.inv);
        std::printf("  unequip at 9 of 10: %s %s\n", u.Ok() ? "ok" : "refused", u.reason.c_str());
        CHECK(u.Ok());
        CHECK(r.inv->Count("mail") == 1);
        CHECK(r.eq->slots[r.chest].item.empty());
        CHECK(r.inv->wornWeight == 0.0f);
    }
    {   // exactly at the limit: 7 + 3 = 10 still fits
        Rig r(7);
        CHECK(r.eq->Unequip(r.chest, *r.inv).Ok());
    }
    {   // a bag that would really go over (8 + 3 = 11): refused, nothing changes
        Rig r(8);
        const InvResult u = r.eq->Unequip(r.chest, *r.inv);
        CHECK(!u.Ok());
        CHECK(r.eq->slots[r.chest].item == "mail");
        CHECK(r.inv->Count("mail") == 0);
        CHECK(r.inv->wornWeight == 3.0f);                                 // put back as it was
    }
    ItemDatabase::Get().Clear();
    TEST_MAIN_RESULT();
}
