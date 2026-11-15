// Item durability (okay/Inventory/Durability.hpp): settings from the item definition's custom fields (maxDurability,
// breakAction); Wear splits a stack so one item wears, breaks at 0 (kept or destroyed), fires on_item_broken; Repair
// to a value or to full, with materials taken all or nothing and never from the item itself; a broken item is
// refused by Use; a broken worn piece gives no armor or defense. Written for this engine.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/Equipment.hpp"
#include "okay/Components/NetInventory.hpp"
#include "okay/Inventory/Durability.hpp"
#include "okay/Combat/WeaponDefinition.hpp"
#include "okay/Components/MeleeAttacker.hpp"
#include "okay/Components/RangedWeapon.hpp"
#include "okay/Inventory/InventoryOps.hpp"
#include "okay/Inventory/ItemDatabase.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Scripting/OkayScriptVM.hpp"
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
    {   // wear on hit: a HealthStat hit wears worn pieces that set wearPerHit; the others never wear on their own
        ItemDefinition plate; plate.id = plate.name = "plate"; plate.equip = EquipSlot::Chest;
        plate.custom = {{"maxDurability", "5"}, {"armor", "4"}, {"wearPerHit", "2"}};
        ItemDatabase::Get().Put(plate);
        Scene hs("hits");
        GameObject* knight = hs.CreateGameObject("Knight");
        auto* hp = knight->AddComponent<HealthStat>();
        hp->publishPrefs = false; hp->maxHealth = 100.0f;
        auto* eq = knight->AddComponent<Equipment>();
        const int head = eq->Find("Head"), chest = eq->Find("Chest");
        CHECK(eq->Set(head, "helmet") && eq->Set(chest, "plate"));
        hs.Start(); hs.Update(0.016f);
        CHECK(hp->equipArmor == 9.0f);                                    // 5 + 4
        hp->invincibleTime = 0.0f;
        hp->Damage(20.0f);
        CHECK(eq->slots[chest].data.durability == 3.0f);                  // plate wore by 2
        CHECK(eq->slots[head].data.durability < 0.0f);                    // the helmet has no wearPerHit: untouched
        hp->Damage(20.0f); hp->Damage(20.0f);                             // 3 -> 1 -> 0: broken
        CHECK(durability::Broken("plate", eq->slots[chest].data));
        hs.Update(0.016f);
        CHECK(hp->equipArmor == 5.0f);                                    // only the helmet protects now
        const float before = hp->health;
        hp->Damage(20.0f);
        CHECK(eq->slots[chest].data.durability == 0.0f);                  // a broken piece is not worn further
        CHECK(before - hp->health == 15.0f);                              // 20 - 5
    }
    {   // weapon wear: one wear per swing that lands (not on a miss); a broken weapon is not used as a weapon
        ItemDefinition sword; sword.id = sword.name = "sword"; sword.equip = EquipSlot::MainHand;
        sword.custom = {{"maxDurability", "2"}, {"wearPerStrike", "1"}};
        ItemDatabase::Get().Put(sword);
        WeaponDefinition wd; wd.id = "sword_w"; wd.name = "Sword"; wd.item = "sword"; wd.swingTime = 0.0f; wd.damage = 12.0f; wd.arc = 360.0f; wd.range = 3.0f;
        WeaponBook::Get().Put(wd);
        Scene ws("swing");
        GameObject* fighter = ws.CreateGameObject("Fighter");
        auto* eq = fighter->AddComponent<Equipment>();
        const int hand = eq->Find("Main Hand");
        CHECK(hand >= 0 && eq->Set(hand, "sword"));
        auto* melee = fighter->AddComponent<MeleeAttacker>();
        melee->cooldown = 0.0f; melee->attackKey = 0; melee->useMouse = false; melee->arc = 360.0f;
        GameObject* dummy = ws.CreateGameObject("Dummy");
        dummy->transform->localPosition = {0, 0, -1};
        auto* dh = dummy->AddComponent<HealthStat>(); dh->publishPrefs = false;
        ws.Start(); ws.Update(0.016f);
        CHECK(WeaponForHolder(fighter) != nullptr);
        const float hp0 = dh->health;
        melee->Swing(); ws.Update(0.016f);
        std::printf("  sword swing: dummy health %.1f -> %.1f, sword durability %.1f\n", hp0, dh->health, eq->slots[hand].data.durability);
        CHECK(dh->health < hp0);                                          // the swing landed
        CHECK(eq->slots[hand].data.durability == 1.0f);                   // one swing, one hit: worn by 1
        dummy->transform->localPosition = {0, 0, -50};                    // out of reach
        melee->Swing(); ws.Update(0.016f);
        CHECK(eq->slots[hand].data.durability == 1.0f);                   // a miss does not wear it
        dummy->transform->localPosition = {0, 0, -1};
        melee->Swing(); ws.Update(0.016f);
        CHECK(durability::Broken("sword", eq->slots[hand].data));
        CHECK(WeaponForHolder(fighter) == nullptr);                       // broken: not a weapon until repaired
        CHECK(durability::RepairEquipped(eq, hand).Ok());
        CHECK(WeaponForHolder(fighter) != nullptr);
        WeaponBook::Get().Clear();
    }
    {   // a gun wears per shot fired (RangedWeapon::Fire), hit or miss
        ItemDefinition gun; gun.id = gun.name = "gun"; gun.equip = EquipSlot::MainHand;
        gun.custom = {{"maxDurability", "3"}, {"wearPerShot", "1"}};
        ItemDatabase::Get().Put(gun);
        Scene gs("shots");
        GameObject* shooter = gs.CreateGameObject("Shooter");
        auto* eq = shooter->AddComponent<Equipment>();
        const int hand = eq->Find("Main Hand");
        CHECK(eq->Set(hand, "gun"));
        auto* rw = shooter->AddComponent<RangedWeapon>();
        rw->fireButton = -1; rw->magazine = 0;
        gs.Start(); gs.Update(0.016f);
        CHECK(rw->Fire());
        CHECK(eq->slots[hand].data.durability == 2.0f);
        gs.Update(1.0f);                                                  // past the fire-rate cooldown
        CHECK(rw->Fire());
        CHECK(eq->slots[hand].data.durability == 1.0f);
    }
    {   // the script functions: inv_durability, inv_broken, inv_wear, inv_repair (the visual-script Fn nodes too)
        Scene ss("script");
        GameObject* hero = ss.CreateGameObject("Hero");
        auto* hinv = hero->AddComponent<Inventory>();
        hinv->slots.assign(4, Inventory::Slot{});
        hinv->slots[0] = {"pick", 1, {}};
        hinv->slots[1] = {"stone", 1, {}};
        GameObject* chest = ss.CreateGameObject("Chest");
        auto* cinv = chest->AddComponent<Inventory>();
        cinv->slots.assign(2, Inventory::Slot{});
        cinv->slots[0] = {"pick", 1, {}};
        auto* sc = hero->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "var full = 0; var none = 0; var empty = 0; var w1 = 0; var after = 0; var w_stone = 0; var broke = 0;\n"
            "var is_broken = 0; var r1 = 0; var r_full = 0; var r_again = 0; var chest_w = 0; var chest_d = 0;\n"
            "function start() {\n"
            "  full = inv_durability(0); none = inv_durability(1); empty = inv_durability(3);\n"
            "  w1 = inv_wear(0, 30); after = inv_durability(0); w_stone = inv_wear(1, 5);\n"
            "  broke = inv_wear(0, 500); is_broken = inv_broken(0);\n"
            "  r1 = inv_repair(0, 25); r_full = inv_repair(0); r_again = inv_repair(0);\n"
            "  chest_w = inv_wear(0, 10, \"Chest\"); chest_d = inv_durability(0, \"Chest\");\n"
            "}\n"));
        ss.Start();
        auto G = [&](const char* n) { return sc->VM()->GetGlobal(n).AsFloat(); };
        CHECK(G("full") == 100.0f && G("none") == -1.0f && G("empty") == -1.0f);
        CHECK(G("w1") == 1.0f && G("after") == 70.0f && G("w_stone") == 0.0f);
        CHECK(G("broke") == 1.0f && G("is_broken") == 1.0f);
        CHECK(G("r1") == 1.0f && G("r_full") == 1.0f && G("r_again") == 0.0f);
        CHECK(hinv->slots[0].data.durability == 100.0f);
        CHECK(G("chest_w") == 1.0f && G("chest_d") == 90.0f);
        CHECK(cinv->slots[0].data.durability == 90.0f);
    }
    {   // review (a): an item whose definition has no maxDurability is never tracked or broken, even with a value of
        // 0 stored on it (an older scene or save). Before the fix it counted as broken: Use refused it, armor ignored.
        ItemDefinition cap; cap.id = cap.name = "old_cap"; cap.equip = EquipSlot::Head;
        cap.custom = {{"armor", "2"}};
        ItemDatabase::Get().Put(cap);
        ItemInstanceData zero; zero.durability = 0.0f;
        CHECK(!durability::Tracked("stone", zero));
        CHECK(!durability::Broken("stone", zero));
        Scene os("old");
        GameObject* p = os.CreateGameObject("P");
        auto* bag = p->AddComponent<Inventory>();
        bag->slots.assign(2, Inventory::Slot{});
        bag->slots[0] = {"stone", 1, zero};
        const InvResult u = InventoryOps::Use(bag, 0, p);
        CHECK(u.reason != "broken");
        CHECK(durability::Wear(bag, 0, 1.0f).reason == "no durability");
        auto* eq = p->AddComponent<Equipment>();
        const int head = eq->Find("Head");
        CHECK(eq->Set(head, "old_cap"));
        eq->slots[head].data = zero;
        eq->Refresh();
        CHECK(eq->ArmorBonus() == 2.0f);                                  // the stored 0 does not switch the armor off
    }
    {   // review (d): the armor piece that breaks on the hit that kills does not change that hit's damage
        ItemDefinition shell; shell.id = shell.name = "shell"; shell.equip = EquipSlot::Chest;
        shell.custom = {{"maxDurability", "1"}, {"armor", "6"}, {"wearPerHit", "5"}, {"breakAction", "destroy"}};
        ItemDatabase::Get().Put(shell);
        Scene ks("kill");
        GameObject* v = ks.CreateGameObject("Victim");
        auto* hp = v->AddComponent<HealthStat>();
        hp->publishPrefs = false; hp->maxHealth = 10.0f; hp->health = 10.0f; hp->invincibleTime = 0.0f;
        hp->respawn = false; hp->destroyOnDeath = false; hp->playDeathAnim = false;
        auto* eq = v->AddComponent<Equipment>();
        const int chest = eq->Find("Chest");
        CHECK(eq->Set(chest, "shell"));
        ks.Start(); ks.Update(0.016f);
        CHECK(hp->equipArmor == 6.0f);
        hp->health = 4.0f;
        hp->Damage(12.0f);                                                // 12 - 6 = 6 > 4: this hit kills, and breaks the shell
        CHECK(hp->dead);
        CHECK(hp->health == -2.0f || hp->health == 0.0f);                 // the damage used the armor of before the hit
        CHECK(eq->slots[chest].item.empty());                              // destroyed on break
        CHECK(hp->equipArmor == 0.0f);                                     // and its armor is gone afterwards
    }
    {   // review (c): every change fires on_durability_changed(item, value), for a bag and for a worn piece
        Scene es("events");
        GameObject* h = es.CreateGameObject("H");
        auto* bag = h->AddComponent<Inventory>();
        bag->slots.assign(2, Inventory::Slot{});
        bag->slots[0] = {"pick", 1, {}};
        auto* eq = h->AddComponent<Equipment>();
        const int head = eq->Find("Head");
        CHECK(eq->Set(head, "helmet"));
        auto* sc = h->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "var n = 0; var last = -1;\n"
            "function on_durability_changed(item, v) { n = n + 1; last = v; }\n"));
        es.Start();
        CHECK(durability::Wear(bag, 0, 10.0f).Ok());
        CHECK(sc->VM()->GetGlobal("n").AsFloat() == 1.0f && sc->VM()->GetGlobal("last").AsFloat() == 90.0f);
        CHECK(durability::Repair(bag, 0).Ok());
        CHECK(sc->VM()->GetGlobal("n").AsFloat() == 2.0f && sc->VM()->GetGlobal("last").AsFloat() == 100.0f);
        CHECK(durability::WearEquipped(eq, head, 20.0f).Ok());
        CHECK(sc->VM()->GetGlobal("n").AsFloat() == 3.0f && sc->VM()->GetGlobal("last").AsFloat() == 30.0f);
        CHECK(durability::RepairEquipped(eq, head).Ok());
        CHECK(sc->VM()->GetGlobal("n").AsFloat() == 4.0f && sc->VM()->GetGlobal("last").AsFloat() == 50.0f);
        // (b) offline nobody else decides: the host check is false without a connected NetworkManager
        CHECK(!NetInventory::HostDecides(h));
    }
    {   // the script in docs/try_it.md ("Item durability"): keep the two in step
        Def("test_pick", "3", nullptr);
        Scene ts("tryit");
        GameObject* p = ts.CreateGameObject("Player");
        auto* bag = p->AddComponent<Inventory>();
        bag->capacity = 8;
        auto* sc = p->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "var last = -1;\n"
            "function start() { inv_add(\"test_pick\", 1); }\n"
            "function wear_once() { inv_wear(0, 1); print(\"durability \" + inv_durability(0) + \", broken \" + inv_broken(0)); }\n"
            "function repair_all() { inv_repair(0); print(\"repaired: \" + inv_durability(0)); }\n"
            "function update(dt) {\n"
            "  if (key_pressed(\"f\")) { wear_once(); }\n"
            "  if (key_pressed(\"r\")) { repair_all(); }\n"
            "}\n"
            "function on_durability_changed(item, v) { last = v; }\n"));
        ts.Start(); ts.Update(0.016f);
        CHECK(!bag->slots.empty() && bag->slots[0].item == "test_pick");
        for (int i = 0; i < 3; ++i) sc->VM()->CallEventArgs("wear_once", {});
        CHECK(bag->slots[0].data.durability == 0.0f && durability::Broken("test_pick", bag->slots[0].data));
        CHECK(sc->VM()->GetGlobal("last").AsFloat() == 0.0f);
        sc->VM()->CallEventArgs("wear_once", {});                          // a broken item wears no further
        CHECK(bag->slots[0].data.durability == 0.0f);
        sc->VM()->CallEventArgs("repair_all", {});
        CHECK(bag->slots[0].data.durability == 3.0f && sc->VM()->GetGlobal("last").AsFloat() == 3.0f);
    }
    ItemDatabase::Get().Clear();
    TEST_MAIN_RESULT();
}
