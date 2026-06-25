#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <cstdio>
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

    // --- Bleeding drains a sibling HealthStat until bandaged --------------
    {
        Scene s("bleed");
        GameObject* p = s.CreateGameObject("P");
        auto* hp = p->AddComponent<HealthStat>(); hp->regenPerSecond = 0.0f;
        auto* bl = p->AddComponent<BleedingStat>();
        bl->clotPerSecond = 0.0f; bl->damagePerSecond = 20.0f;   // bleeds until bandaged
        s.Start();
        bl->Wound(100.0f);                                       // full bleed
        for (int i = 0; i < 60; ++i) s.Update(1.0f / 60.0f);
        CHECK(hp->health < 90.0f);                               // lost HP to bleeding
        CHECK(bl->bleeding);
        bl->Bandage();
        float after = hp->health;
        for (int i = 0; i < 60; ++i) s.Update(1.0f / 60.0f);
        CHECK(!bl->bleeding);
        CHECK_NEAR(hp->health, after, 1e-3f);                    // stopped after bandage
    }

    // --- Radiation builds in a zone and poisons SurvivalStats health -----
    {
        Scene s("rad");
        GameObject* p = s.CreateGameObject("P");
        auto* sv = p->AddComponent<SurvivalStats>(); sv->regenWhenFed = 0.0f;
        auto* rd = p->AddComponent<RadiationStat>();
        rd->gainPerSecond = 100.0f; rd->sickThreshold = 50.0f; rd->damagePerSecond = 10.0f;
        s.Start();
        rd->SetInRadiation(true);
        for (int i = 0; i < 120; ++i) s.Update(1.0f / 60.0f);    // 2s: ramps past threshold
        CHECK(rd->sick);
        CHECK(sv->health < 100.0f);                              // irradiated -> health damage
        rd->SetInRadiation(false);
        rd->TakeAntiRad(1000.0f);
        s.Update(1.0f / 60.0f);
        CHECK_NEAR(rd->radiation, 0.0f, 1e-3f);
        CHECK(!rd->sick);
    }

    // --- Poison fades on its own; Cure clears it -------------------------
    {
        Scene s("poison");
        GameObject* p = s.CreateGameObject("P");
        p->AddComponent<HealthStat>()->regenPerSecond = 0.0f;
        auto* po = p->AddComponent<PoisonStat>();
        po->decayPerSecond = 10.0f; po->damagePerSecond = 5.0f;
        s.Start();
        po->Poison(50.0f);
        float start = po->poison;
        for (int i = 0; i < 60; ++i) s.Update(1.0f / 60.0f);
        CHECK(po->poison < start);                               // metabolised over time
        po->CureAll();
        CHECK_NEAR(po->poison, 0.0f, 1e-3f);
        s.Update(1.0f / 60.0f);                                  // flag refreshes next tick
        CHECK(!po->poisoned);
    }

    // --- Wetness soaks/dries and chills a sibling SurvivalStats ----------
    {
        Scene s("wet");
        GameObject* p = s.CreateGameObject("P");
        auto* sv = p->AddComponent<SurvivalStats>();
        auto* we = p->AddComponent<WetnessStat>();
        we->soakPerSecond = 100.0f; we->chillPerSecond = 20.0f;
        s.Start();
        we->SetInWater(true);
        for (int i = 0; i < 60; ++i) s.Update(1.0f / 60.0f);
        CHECK(we->wetness > 50.0f);
        CHECK(sv->warmth < 100.0f);                              // wet -> chilled
        we->SetInWater(false);
        float wet = we->wetness;
        for (int i = 0; i < 60; ++i) s.Update(1.0f / 60.0f);
        CHECK(we->wetness < wet);                                // dries off
    }

    // --- Affliction dispatch + serialization -----------------------------
    {
        Scene s("affser");
        GameObject* p = s.CreateGameObject("P");
        auto* bl = p->AddComponent<BleedingStat>();
        p->AddComponent<RadiationStat>()->sickThreshold = 33.0f;
        s.Start();
        CHECK(InvokeNativeUIAction(p, "Wound", 40.0f));
        CHECK_NEAR(bl->bleed, 40.0f, 1e-3f);
        CHECK(InvokeNativeUIAction(p, "Bandage", 0.0f));
        CHECK_NEAR(bl->bleed, 0.0f, 1e-3f);
        std::string txt = SceneSerializer::SerializeObject(*p);
        CHECK(txt.find("stat_bleed ") != std::string::npos);
        CHECK(txt.find("stat_radiation ") != std::string::npos);
        Scene s2("affser2");
        GameObject* c2 = SceneSerializer::InstantiateFromText(s2, txt);
        CHECK(c2 && c2->GetComponent<BleedingStat>() != nullptr);
        CHECK_NEAR(c2->GetComponent<RadiationStat>()->sickThreshold, 33.0f, 1e-3f);
    }

    // --- Carry weight: over the limit drains stamina + slows -------------
    {
        Scene s("carry");
        GameObject* p = s.CreateGameObject("P");
        auto* st = p->AddComponent<StaminaStat>(); st->regenPerSecond = 0.0f;
        auto* cw = p->AddComponent<CarryWeightStat>();
        cw->maxLoad = 50.0f; cw->overStaminaDrain = 30.0f; cw->minSpeedFactor = 0.4f;
        s.Start();
        cw->AddLoad(40.0f);
        s.Update(1.0f / 60.0f);
        CHECK(!cw->OverLimit());
        CHECK_NEAR(cw->SpeedFactor(), 1.0f, 1e-3f);
        cw->AddLoad(40.0f);                                  // 80 > 50 -> encumbered
        for (int i = 0; i < 60; ++i) s.Update(1.0f / 60.0f);
        CHECK(cw->encumbered);
        CHECK(cw->SpeedFactor() < 1.0f);
        CHECK(st->stamina < 100.0f);                         // drained while overloaded
        cw->RemoveLoad(60.0f);                               // back to 20
        s.Update(1.0f / 60.0f);
        CHECK(!cw->encumbered);
    }

    // --- Status effects: timed debuff damages, then expires --------------
    {
        Scene s("status");
        GameObject* p = s.CreateGameObject("P");
        auto* hp = p->AddComponent<HealthStat>(); hp->regenPerSecond = 0.0f;
        auto* se = p->AddComponent<StatusEffectStat>();
        s.Start();
        se->Apply("burning", 1.0f, -20.0f);                  // 20 dmg/s for 1s
        CHECK(se->Has("burning"));
        CHECK(se->ActiveCount() == 1);
        for (int i = 0; i < 30; ++i) s.Update(1.0f / 60.0f); // 0.5s
        CHECK(hp->health < 100.0f);
        CHECK(se->Remaining("burning") > 0.0f);
        for (int i = 0; i < 40; ++i) s.Update(1.0f / 60.0f); // past 1s total
        CHECK(!se->Has("burning"));                          // expired
        CHECK(se->ActiveCount() == 0);
        // Heal-over-time buff
        hp->health = 50.0f;
        se->Apply("regen", 1.0f, 30.0f);
        for (int i = 0; i < 30; ++i) s.Update(1.0f / 60.0f);
        CHECK(hp->health > 55.0f);
    }

    // --- Save / Load round-trips live values across a fresh component ----
    {
        std::remove("test_survsave.okayprefs");
        {
            Scene s("save");
            GameObject* p = s.CreateGameObject("P");
            auto* sv = p->AddComponent<SurvivalStats>();
            auto* save = p->AddComponent<SurvivalSave>();
            save->saveKey = "test_survsave"; save->loadOnStart = false;
            s.Start();
            sv->health = 42.0f; sv->hunger = 17.0f; sv->thirst = 88.0f;
            save->Save();
        }
        {
            Scene s2("load");
            GameObject* p = s2.CreateGameObject("P");
            auto* sv = p->AddComponent<SurvivalStats>();
            auto* save = p->AddComponent<SurvivalSave>();
            save->saveKey = "test_survsave"; save->loadOnStart = true;
            s2.Start();                                      // fills to max
            CHECK_NEAR(sv->health, 100.0f, 1e-3f);           // not loaded yet (deferred)
            s2.Update(1.0f / 60.0f);                         // first frame -> Load()
            CHECK_NEAR(sv->health, 42.0f, 0.5f);
            CHECK_NEAR(sv->hunger, 17.0f, 0.5f);
            CHECK_NEAR(sv->thirst, 88.0f, 0.5f);
        }
        std::remove("test_survsave.okayprefs");
    }

    // --- Systems serialization round-trip --------------------------------
    {
        Scene s("sysser");
        GameObject* p = s.CreateGameObject("P");
        p->AddComponent<CarryWeightStat>()->maxLoad = 75.0f;
        p->AddComponent<StatusEffectStat>()->sendMessages = false;
        auto* save = p->AddComponent<SurvivalSave>();
        save->saveKey = "mygame"; save->saveContinuously = true;
        std::string txt = SceneSerializer::SerializeObject(*p);
        CHECK(txt.find("stat_carry ") != std::string::npos);
        CHECK(txt.find("statuseffect ") != std::string::npos);
        CHECK(txt.find("survivalsave ") != std::string::npos);
        Scene s2("sysser2");
        GameObject* c2 = SceneSerializer::InstantiateFromText(s2, txt);
        CHECK(c2 != nullptr);
        CHECK_NEAR(c2->GetComponent<CarryWeightStat>()->maxLoad, 75.0f, 1e-3f);
        CHECK(!c2->GetComponent<StatusEffectStat>()->sendMessages);
        CHECK(c2->GetComponent<SurvivalSave>()->saveKey == "mygame");
        CHECK(c2->GetComponent<SurvivalSave>()->saveContinuously);
    }

    TEST_MAIN_RESULT();
}
