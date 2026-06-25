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

    TEST_MAIN_RESULT();
}
