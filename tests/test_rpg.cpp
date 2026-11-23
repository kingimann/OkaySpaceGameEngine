#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("rpg");

    // --- Stats: damage (mitigated by defense, on the object's Health Stat), heal, XP levelling ---
    {
        Scene s("S"); s.physicsEnabled = false;
        GameObject* hero = s.CreateGameObject("Hero");
        auto* st = hero->AddComponent<Stats>();
        HealthStat* h = st->Health();                // added with Stats: the one health
        CHECK(h != nullptr);
        h->maxHealth = 100; h->health = 100; st->defense = 5;
        DealDamage(hero, 20);                        // 20 - 5 defense = 15
        CHECK_NEAR(h->health, 85.0f, 1e-4f);
        DealDamage(hero, 3);                         // below defense -> minimum 1
        CHECK_NEAR(h->health, 84.0f, 1e-4f);
        HealObject(hero, 1000);                      // clamps to max
        CHECK_NEAR(h->health, 100.0f, 1e-4f);
        CHECK(!IsObjectDead(hero));
        DealDamage(hero, 99999);
        CHECK(IsObjectDead(hero));

        // Level up: enough XP raises level + caps and refills.
        GameObject* hero2 = s.CreateGameObject("Hero2");
        auto* st2 = hero2->AddComponent<Stats>();
        st2->level = 1; st2->xp = 0; st2->xpToNext = 100; st2->Health()->maxHealth = 100;
        st2->AddXP(120);                          // crosses one level
        CHECK(st2->level == 2);
        CHECK(st2->Health()->maxHealth > 100.0f);
        CHECK_NEAR(st2->Health()->health, st2->Health()->maxHealth, 1e-4f);  // refilled on level up
    }

    // --- Inventory: stacking, remove, capacity ---
    {
        Scene s("I"); s.physicsEnabled = false;
        auto* inv = s.CreateGameObject("Bag")->AddComponent<Inventory>();
        inv->capacity = 2;
        CHECK(inv->Add("Potion", 3));
        CHECK(inv->Add("Potion", 2));             // stacks
        CHECK(inv->Count("Potion") == 5);
        CHECK(inv->SlotsUsed() == 1);
        CHECK(inv->Add("Sword", 1));              // second slot
        CHECK(!inv->Add("Shield", 1));            // full (capacity 2)
        CHECK(inv->Remove("Potion", 4));
        CHECK(inv->Count("Potion") == 1);
        CHECK(inv->Remove("Potion", 1));          // empties + frees the slot
        CHECK(inv->Count("Potion") == 0);
        CHECK(inv->SlotsUsed() == 1);
        CHECK(inv->Add("Shield", 1));             // now there's room again
    }

    // --- TurnManager: order, wrap, rounds, removal ---
    {
        Scene s("T"); s.physicsEnabled = false;
        auto* tm = s.CreateGameObject("Battle")->AddComponent<TurnManager>();
        tm->autoStart = false;
        tm->participants = {"A", "B", "C"};
        CHECK(tm->Current() == "A");
        tm->EndTurn(); CHECK(tm->Current() == "B");
        tm->EndTurn(); CHECK(tm->Current() == "C");
        CHECK(tm->round == 1);
        tm->EndTurn();                            // wraps -> new round
        CHECK(tm->Current() == "A");
        CHECK(tm->round == 2);
        tm->Remove("A");                          // A defeated
        CHECK(tm->Count() == 2);

        // Round-trips through the scene.
        Scene s2("x"); SceneSerializer::Deserialize(s2, SceneSerializer::Serialize(s));
        auto* tm2 = s2.Find("Battle") ? s2.Find("Battle")->GetComponent<TurnManager>() : nullptr;
        CHECK(tm2 != nullptr);
        if (tm2) { CHECK(tm2->Count() == 2); CHECK(tm2->round == 2); }
    }

    // --- Stats + Inventory round-trip ---
    {
        Scene s("RT"); s.physicsEnabled = false;
        GameObject* g = s.CreateGameObject("Hero");
        auto* st = g->AddComponent<Stats>(); st->level = 7; st->strength = 22; st->maxMana = 90;
        auto* inv = g->AddComponent<Inventory>(); inv->Add("Gold", 500); inv->Add("Key", 1);

        Scene s2("x"); SceneSerializer::Deserialize(s2, SceneSerializer::Serialize(s));
        GameObject* g2 = s2.Find("Hero");
        auto* st2 = g2 ? g2->GetComponent<Stats>() : nullptr;
        auto* in2 = g2 ? g2->GetComponent<Inventory>() : nullptr;
        CHECK(st2 && st2->level == 7 && st2->strength == 22);
        CHECK(in2 && in2->Count("Gold") == 500 && in2->Has("Key"));
    }

    TEST_MAIN_RESULT();
}
