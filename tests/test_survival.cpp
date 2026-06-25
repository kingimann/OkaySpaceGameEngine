#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
using namespace okay;

// Native SurvivalStats: stats drain over time, empty stats damage health directly
// (no script VM / message round-trip), death deactivates the object, refills clamp,
// it fills same-named progress bars, and it round-trips through serialization.
int main() {
    RUN_SUITE("survival");

    // --- Hunger/thirst drain, then damage health directly ----------------
    {
        Scene s("drain");
        GameObject* p = s.CreateGameObject("Player");
        auto* sv = p->AddComponent<SurvivalStats>();
        sv->hungerDrain = 50.0f; sv->thirstDrain = 50.0f;    // empty within ~2s
        sv->starveDamage = 10.0f; sv->dehydrateDamage = 10.0f;
        sv->regenWhenFed = 0.0f;
        s.Start();
        CHECK_NEAR(sv->hunger, 100.0f, 1e-3f);               // Start() filled it
        for (int i = 0; i < 60; ++i) s.Update(1.0f / 60.0f); // 1s: drains, not yet empty
        CHECK(sv->hunger < 60.0f);
        CHECK_NEAR(sv->health, 100.0f, 1e-3f);               // still fed enough -> no dmg
        for (int i = 0; i < 180; ++i) s.Update(1.0f / 60.0f);// 3s more: hunger/thirst hit 0
        CHECK_NEAR(sv->hunger, 0.0f, 1e-3f);
        CHECK(sv->health < 100.0f);                          // empty stats bit health directly
    }

    // --- Armor soaks damage; Heal clamps to max --------------------------
    {
        Scene s("dmg");
        GameObject* p = s.CreateGameObject("Player");
        auto* sv = p->AddComponent<SurvivalStats>();
        sv->armor = 5.0f;
        s.Start();
        sv->Damage(20.0f);                                   // 20 - 5 armor = 15
        CHECK_NEAR(sv->health, 85.0f, 1e-3f);
        sv->Damage(2.0f);                                    // fully soaked by armor
        CHECK_NEAR(sv->health, 85.0f, 1e-3f);
        sv->Heal(999.0f);
        CHECK_NEAR(sv->health, 100.0f, 1e-3f);               // clamped
    }

    // --- Death deactivates the object + broadcasts "died" ----------------
    {
        Scene s("death");
        GameObject* gm = s.CreateGameObject("GM");
        auto* al = gm->AddComponent<ActionList>();
        al->trigger = ActionList::Trigger::OnMessage; al->triggerKey = "died";
        GameObject* p = s.CreateGameObject("Player");
        auto* sv = p->AddComponent<SurvivalStats>();
        s.Start();
        CHECK(p->active);
        sv->Damage(1000.0f);
        CHECK(sv->IsDead());
        CHECK(!p->active);                                   // deactivated on death
        sv->Revive();
        CHECK(!sv->IsDead());
        CHECK(p->active);
        CHECK_NEAR(sv->health, 100.0f, 1e-3f);
    }

    // --- Sprinting burns stamina and drains hunger faster ----------------
    {
        Scene s("sprint");
        GameObject* p = s.CreateGameObject("Player");
        auto* sv = p->AddComponent<SurvivalStats>();
        sv->hungerDrain = 4.0f; sv->sprintDrainMult = 3.0f; sv->sprintCost = 30.0f;
        s.Start();
        sv->SetSprinting(true);
        for (int i = 0; i < 60; ++i) s.Update(1.0f / 60.0f); // 1s sprint
        CHECK(sv->stamina < 80.0f);                          // stamina spent
        CHECK(sv->hunger < 90.0f);                           // ~12 hunger (4 * 3x)
    }

    // --- Fills a same-named HealthBar progress widget --------------------
    {
        Scene s("bar");
        GameObject* bar = s.CreateGameObject("HealthBar");
        auto* pb = bar->AddComponent<UIProgressBar>();
        GameObject* p = s.CreateGameObject("Player");
        auto* sv = p->AddComponent<SurvivalStats>();
        sv->maxHealth = 200.0f;
        s.Start();
        sv->Damage(100.0f);          // health 100/200 -> 0.5
        s.Update(1.0f / 60.0f);
        CHECK_NEAR(pb->value, 0.5f, 0.02f);
    }

    // --- Serialization round-trip ---------------------------------------
    {
        Scene s("ser");
        GameObject* p = s.CreateGameObject("Player");
        auto* sv = p->AddComponent<SurvivalStats>();
        sv->maxHealth = 150.0f; sv->armor = 7.5f; sv->hungerDrain = 2.5f;
        sv->drownDamage = 9.0f; sv->sendMessages = false;
        std::string txt = SceneSerializer::SerializeObject(*p);
        CHECK(txt.find("survival ") != std::string::npos);
        Scene s2("ser2");
        GameObject* c2 = SceneSerializer::InstantiateFromText(s2, txt);
        auto* v2 = c2 ? c2->GetComponent<SurvivalStats>() : nullptr;
        CHECK(v2 != nullptr);
        CHECK_NEAR(v2->maxHealth, 150.0f, 1e-3f);
        CHECK_NEAR(v2->armor, 7.5f, 1e-3f);
        CHECK_NEAR(v2->hungerDrain, 2.5f, 1e-3f);
        CHECK_NEAR(v2->drownDamage, 9.0f, 1e-3f);
        CHECK(!v2->sendMessages);
    }

    // --- Individual HealthStat: regen after delay, death deactivates -----
    {
        Scene s("ihealth");
        GameObject* p = s.CreateGameObject("P");
        auto* h = p->AddComponent<HealthStat>();
        h->regenPerSecond = 10.0f; h->regenDelay = 0.0f;
        s.Start();
        h->Damage(40.0f);
        CHECK_NEAR(h->health, 60.0f, 1e-3f);
        for (int i = 0; i < 60; ++i) s.Update(1.0f / 60.0f);  // ~+10 over 1s
        CHECK(h->health > 65.0f);
        h->Damage(1000.0f);
        CHECK(h->IsDead());
        CHECK(!p->active);
    }

    // --- Individual stats are independent (Hunger doesn't touch Health) --
    {
        Scene s("indep");
        GameObject* p = s.CreateGameObject("P");
        auto* hp = p->AddComponent<HealthStat>();
        auto* hu = p->AddComponent<HungerStat>();
        hu->drainPerSecond = 80.0f;                 // empties fast
        s.Start();
        for (int i = 0; i < 180; ++i) s.Update(1.0f / 60.0f);
        CHECK_NEAR(hu->hunger, 0.0f, 1e-3f);
        CHECK(hu->starving);
        CHECK_NEAR(hp->health, 100.0f, 1e-3f);      // standalone: no cross-talk
    }

    // --- StaminaStat: sprint drains, exhaustion locks until recovered ----
    {
        Scene s("istam");
        GameObject* p = s.CreateGameObject("P");
        auto* st = p->AddComponent<StaminaStat>();
        st->sprintCost = 200.0f; st->exhaustedUntil = 50.0f; st->regenPerSecond = 40.0f; st->regenDelay = 0.0f;
        s.Start();
        st->SetSprinting(true);
        for (int i = 0; i < 60; ++i) s.Update(1.0f / 60.0f);
        CHECK(st->stamina < 30.0f);                 // drained to exhaustion (then idle-regens)
        CHECK(st->exhausted);
        CHECK(!st->CanSprint());
        st->SetSprinting(false);
        for (int i = 0; i < 120; ++i) s.Update(1.0f / 60.0f); // recover past threshold
        CHECK(!st->exhausted);
        CHECK(st->stamina >= 50.0f);
    }

    // --- OxygenStat drains submerged, refills surfaced -------------------
    {
        Scene s("iox");
        GameObject* p = s.CreateGameObject("P");
        auto* ox = p->AddComponent<OxygenStat>();
        ox->drainPerSecond = 50.0f; ox->refillPerSecond = 50.0f;
        s.Start();
        ox->SetSubmerged(true);
        for (int i = 0; i < 60; ++i) s.Update(1.0f / 60.0f);
        CHECK(ox->oxygen < 60.0f);
        float low = ox->oxygen;
        ox->SetSubmerged(false);
        for (int i = 0; i < 30; ++i) s.Update(1.0f / 60.0f);
        CHECK(ox->oxygen > low);                    // refilled
    }

    // --- Individual stat serialization round-trip ------------------------
    {
        Scene s("iser");
        GameObject* p = s.CreateGameObject("P");
        p->AddComponent<HealthStat>()->maxHealth = 250.0f;
        auto* st = p->AddComponent<StaminaStat>(); st->jumpCost = 33.0f; st->sendMessages = false;
        p->AddComponent<SanityStat>()->drainInDark = 7.0f;
        std::string txt = SceneSerializer::SerializeObject(*p);
        CHECK(txt.find("stat_health ") != std::string::npos);
        CHECK(txt.find("stat_stamina ") != std::string::npos);
        CHECK(txt.find("stat_sanity ") != std::string::npos);
        Scene s2("iser2");
        GameObject* c2 = SceneSerializer::InstantiateFromText(s2, txt);
        CHECK(c2 != nullptr);
        CHECK_NEAR(c2->GetComponent<HealthStat>()->maxHealth, 250.0f, 1e-3f);
        CHECK_NEAR(c2->GetComponent<StaminaStat>()->jumpCost, 33.0f, 1e-3f);
        CHECK(!c2->GetComponent<StaminaStat>()->sendMessages);
        CHECK_NEAR(c2->GetComponent<SanityStat>()->drainInDark, 7.0f, 1e-3f);
    }

    // --- Native On Click dispatch (button -> SurvivalStats method) -------
    {
        Scene s("invoke");
        GameObject* p = s.CreateGameObject("Player");
        auto* sv = p->AddComponent<SurvivalStats>();
        s.Start();
        sv->thirst = 40.0f;
        CHECK(InvokeNativeUIAction(p, "Drink", 30.0f));    // handled
        CHECK_NEAR(sv->thirst, 70.0f, 1e-3f);
        sv->health = 50.0f;
        CHECK(InvokeNativeUIAction(p, "Heal", 25.0f));
        CHECK_NEAR(sv->health, 75.0f, 1e-3f);
        CHECK(!InvokeNativeUIAction(p, "NotAMethod", 1.0f));   // unknown -> false
        auto names = NativeUIActionNames(p);
        CHECK(std::find(names.begin(), names.end(), "Eat") != names.end());
    }

    // --- Dispatch to an individual stat component ------------------------
    {
        Scene s("invoke2");
        GameObject* p = s.CreateGameObject("P");
        auto* hu = p->AddComponent<HungerStat>();
        s.Start();
        hu->hunger = 20.0f;
        CHECK(InvokeNativeUIAction(p, "Eat", 50.0f));
        CHECK_NEAR(hu->hunger, 70.0f, 1e-3f);
        CHECK(!InvokeNativeUIAction(p, "Drink", 10.0f));   // HungerStat has no Drink
    }

    // --- Button clickArg round-trips through serialization ---------------
    {
        Scene s("btnser");
        GameObject* b = s.CreateGameObject("EatBtn");
        auto* btn = b->AddComponent<UIButton>();
        btn->clickTarget = "Player"; btn->clickFunction = "Eat"; btn->clickArg = 35.0f;
        std::string txt = SceneSerializer::SerializeObject(*b);
        Scene s2("btnser2");
        GameObject* c2 = SceneSerializer::InstantiateFromText(s2, txt);
        auto* b2 = c2 ? c2->GetComponent<UIButton>() : nullptr;
        CHECK(b2 != nullptr);
        CHECK(b2->clickFunction == "Eat");
        CHECK(b2->clickTarget == "Player");
        CHECK_NEAR(b2->clickArg, 35.0f, 1e-3f);
    }

    TEST_MAIN_RESULT();
}
