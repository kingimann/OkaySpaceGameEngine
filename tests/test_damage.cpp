#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <limits>
#include <string>
#include <vector>
using namespace okay;

// One damage path: DamageEvent, per-type resistances, death choices, events, regeneration at several
// frame rates, i-frames, NaN/inf handling, old saved files, and every holder (Health Stat, Survival
// Stats, RPG Stats, NPC Controller) reached through DealDamage.
static void Run(Scene& s, float seconds, float fps) {
    const int n = (int)std::lround(seconds * fps);
    for (int i = 0; i < n; ++i) s.Update(1.0f / fps);
}

int main() {
    RUN_SUITE("damage");
    ClearDamageListeners();

    // --- type names -------------------------------------------------------
    {
        DamageType t = DamageType::Generic;
        CHECK(DamageTypeFromName("fire", t) && t == DamageType::Fire);
        CHECK(DamageTypeFromName("POISON", t) && t == DamageType::Poison);
        CHECK(!DamageTypeFromName("nope", t));
        CHECK(std::string(DamageTypeName(DamageType::Explosion)) == "Explosion");
    }

    // --- resistances per type, armor and general resistance still apply ------
    {
        Scene s("res");
        GameObject* g = s.CreateGameObject("G");
        auto* h = g->AddComponent<HealthStat>();
        h->SetResistance(DamageType::Fire, 0.5f);
        h->SetResistance(DamageType::Poison, 1.0f);
        h->SetResistance(DamageType::Cold, -1.0f);
        s.Start();
        CHECK(h->Damage(DamageEvent::Make(20.0f, DamageType::Fire)));
        CHECK_NEAR(h->health, 90.0f, 1e-3f);                    // half of 20
        h->Damage(DamageEvent::Make(50.0f, DamageType::Poison));
        CHECK_NEAR(h->health, 90.0f, 1e-3f);                    // immune
        h->Damage(DamageEvent::Make(10.0f, DamageType::Cold));
        CHECK_NEAR(h->health, 70.0f, 1e-3f);                    // weak: double
        h->Damage(DamageEvent::Make(10.0f, DamageType::Physical));
        CHECK_NEAR(h->health, 60.0f, 1e-3f);                    // no resistance to Physical
        h->SetResistance(DamageType::Fire, std::nanf(""));      // ignored
        CHECK_NEAR(h->ResistanceTo(DamageType::Fire), 0.5f, 1e-6f);
        h->SetResistance(DamageType::Fire, 5.0f);               // clamped
        CHECK_NEAR(h->ResistanceTo(DamageType::Fire), 1.0f, 1e-6f);
    }

    // --- NaN / inf / negative amounts ---------------------------------------
    {
        Scene s("sane");
        GameObject* g = s.CreateGameObject("G");
        auto* h = g->AddComponent<HealthStat>();
        s.Start();
        h->Damage(std::nanf("")); h->Damage(-30.0f); h->Damage(0.0f);
        CHECK_NEAR(h->health, 100.0f, 1e-6f);
        h->Damage(40.0f);
        h->Heal(std::nanf("")); h->Heal(-10.0f);
        CHECK_NEAR(h->health, 60.0f, 1e-6f);
        h->Heal(std::numeric_limits<float>::infinity());        // heals to full, no more
        CHECK_NEAR(h->health, 100.0f, 1e-6f);
        h->Damage(std::numeric_limits<float>::infinity());      // kills, health stays finite
        CHECK(h->IsDead());
        CHECK(std::isfinite(h->health));
        CHECK(!DealDamage(g, 10.0f));                           // dead: nothing to hit
    }

    // --- events: order, content ---------------------------------------------
    {
        ClearDamageListeners();
        std::vector<std::string> log;
        DamageEvent last;
        AddDamagedListener([&](const DamageEvent& e) { log.push_back("damaged"); last = e; });
        AddDeathListener([&](const DamageEvent&) { log.push_back("death"); });
        AddHealedListener([&](GameObject*, float a) { log.push_back("healed" + std::to_string((int)a)); });
        Scene s("ev");
        GameObject* src = s.CreateGameObject("Src");
        GameObject* g = s.CreateGameObject("G");
        auto* h = g->AddComponent<HealthStat>();
        s.Start();
        DamageEvent e = DamageEvent::From(30.0f, Vec3{1, 2, 3}, DamageType::Explosion, src);
        CHECK(DealDamage(g, e));
        CHECK(last.type == DamageType::Explosion && last.source == src && last.target == g);
        CHECK_NEAR(last.dealt, 30.0f, 1e-3f);
        CHECK(!last.killed);
        CHECK(last.hasHitPoint && last.hitPoint.y == 2.0f);
        h->Heal(10.0f);
        h->Damage(500.0f);                                      // lethal
        CHECK(log.size() == 4);
        CHECK(log[0] == "damaged" && log[1] == "healed10" && log[2] == "damaged" && log[3] == "death");
        CHECK(last.killed);
        CHECK_NEAR(last.dealt, 80.0f, 1e-3f);                   // only the health that was there
        ClearDamageListeners();
    }

    // --- Kill / Revive ------------------------------------------------------
    {
        ClearDamageListeners();
        int deaths = 0;
        AddDeathListener([&](const DamageEvent&) { ++deaths; });
        Scene s("kill");
        GameObject* g = s.CreateGameObject("G");
        auto* h = g->AddComponent<HealthStat>();
        h->godMode = true;
        s.Start();
        h->Damage(10.0f);
        CHECK_NEAR(h->health, 100.0f, 1e-6f);                   // god mode blocks a hit
        CHECK(KillObject(g));                                   // but not a kill
        CHECK(h->IsDead() && deaths == 1);
        CHECK(!KillObject(g) && deaths == 1);                   // second kill does nothing
        h->Revive();
        CHECK(!h->IsDead() && h->health == 100.0f);
        ClearDamageListeners();
    }

    // --- death choices ------------------------------------------------------
    {
        Scene s("death");
        GameObject* a = s.CreateGameObject("Nothing");
        GameObject* b = s.CreateGameObject("Disable");
        GameObject* c = s.CreateGameObject("Destroy");
        auto* ha = a->AddComponent<HealthStat>(); ha->SetDeathAction(HealthStat::DeathAction::Nothing);
        auto* hb = b->AddComponent<HealthStat>(); hb->SetDeathAction(HealthStat::DeathAction::Disable);
        auto* hc = c->AddComponent<HealthStat>(); hc->SetDeathAction(HealthStat::DeathAction::Destroy);
        hc->playDeathAnim = false;
        CHECK(ha->GetDeathAction() == HealthStat::DeathAction::Nothing);
        CHECK(hb->GetDeathAction() == HealthStat::DeathAction::Disable);
        CHECK(hc->GetDeathAction() == HealthStat::DeathAction::Destroy);
        s.Start();
        ha->Kill(); hb->Kill(); hc->Kill();
        CHECK(a->active);
        CHECK(!b->active);
        s.Update(0.016f);                                       // destroys are applied at the end of a frame
        CHECK(s.Find("Destroy") == nullptr);
        GameObject* r = s.CreateGameObject("R");
        auto* hr = r->AddComponent<HealthStat>();
        hr->SetDeathAction(HealthStat::DeathAction::Respawn); hr->respawnDelay = 0.5f;
        CHECK(hr->GetDeathAction() == HealthStat::DeathAction::Respawn);
        s.Start();
        hr->Kill();
        CHECK(hr->IsDead());
        Run(s, 0.6f, 60);
        CHECK(!hr->IsDead());
        // default is the animation choice (as before this change)
        GameObject* d = s.CreateGameObject("D");
        CHECK(d->AddComponent<HealthStat>()->GetDeathAction() == HealthStat::DeathAction::PlayAnimation);
    }

    // --- regeneration is frame-rate independent -------------------------------
    for (float fps : {30.0f, 60.0f, 144.0f}) {
        Scene s("regen");
        GameObject* g = s.CreateGameObject("G");
        auto* h = g->AddComponent<HealthStat>();
        h->regenPerSecond = 10.0f; h->regenDelay = 1.0f;
        s.Start();
        h->Damage(50.0f);
        Run(s, 1.0f, fps);                                      // inside the delay: no regen
        CHECK_NEAR(h->health, 50.0f, 0.5f);
        Run(s, 2.0f, fps);                                      // 2 s of regen: +20
        CHECK_NEAR(h->health, 70.0f, 1.0f);
    }

    // --- invulnerability window --------------------------------------------
    {
        Scene s("iframes");
        GameObject* g = s.CreateGameObject("G");
        auto* h = g->AddComponent<HealthStat>();
        h->invincibleTime = 0.5f;
        s.Start();
        CHECK(DealDamage(g, 10.0f));
        CHECK(!DealDamage(g, 10.0f));                           // inside the window
        CHECK_NEAR(h->health, 90.0f, 1e-3f);
        Run(s, 0.6f, 60);
        CHECK(DealDamage(g, 10.0f));
        CHECK_NEAR(h->health, 80.0f, 1e-3f);
    }

    // --- validation in plain words ------------------------------------------
    {
        Scene s("val");
        GameObject* g = s.CreateGameObject("G");
        auto* h = g->AddComponent<HealthStat>();
        CHECK(h->Validate().empty());                           // defaults work with zero edits
        h->maxHealth = 0.0f;
        auto w = h->Validate();
        CHECK(!w.empty() && w[0].find("Max Health") != std::string::npos);
    }

    // --- every holder through the one path -----------------------------------
    {
        ClearDamageListeners();
        std::vector<GameObject*> hit; std::vector<GameObject*> died; std::vector<DamageType> types;
        AddDamagedListener([&](const DamageEvent& e) { hit.push_back(e.target); types.push_back(e.type); });
        AddDeathListener([&](const DamageEvent& e) { died.push_back(e.target); });
        Scene s("holders");
        GameObject* a = s.CreateGameObject("Hs");   a->AddComponent<HealthStat>();
        GameObject* b = s.CreateGameObject("Sv");   auto* sv = b->AddComponent<SurvivalStats>(); sv->armor = 5.0f;
        GameObject* c = s.CreateGameObject("Rpg");  auto* st = c->AddComponent<Stats>(); st->defense = 5;
        GameObject* d = s.CreateGameObject("Npc");  auto* npc = d->AddComponent<NPCController>(); npc->maxHealth = 30;
        s.Start();
        CHECK(DealDamage(a, 10.0f, DamageType::Fire));
        CHECK(DealDamage(b, 20.0f, DamageType::Fire));
        CHECK_NEAR(sv->health, 85.0f, 1e-3f);                   // 20 - 5 armor, as before
        CHECK(DealDamage(c, 3.0f));
        CHECK_NEAR(st->health, st->maxHealth - 1.0f, 1e-3f);    // RPG rule kept: a hit is always at least 1
        CHECK(DealDamage(d, 12.0f));
        CHECK_NEAR(npc->health, 18.0f, 1e-3f);
        CHECK(hit.size() == 4 && types[0] == DamageType::Fire);
        float hh = 0, mm = 0;
        CHECK(HealthOf(b, hh, mm) && hh == sv->health && mm == sv->maxHealth);
        CHECK(HealthOf(d, hh, mm) && mm == 30.0f);
        CHECK(HealObject(b, 100.0f) > 0.0f);
        CHECK_NEAR(sv->health, sv->maxHealth, 1e-3f);
        CHECK(KillObject(b) && sv->dead && IsObjectDead(b));
        CHECK(KillObject(c) && IsObjectDead(c));
        CHECK(died.size() == 2);
        CHECK(!DealDamage(s.CreateGameObject("Bare"), 5.0f)); // nothing to hurt
        ClearDamageListeners();
    }

    // --- survival drain goes through damage events ---------------------------
    {
        ClearDamageListeners();
        std::vector<DamageType> types;
        AddDamagedListener([&](const DamageEvent& e) { types.push_back(e.type); });
        Scene s("surv");
        GameObject* p = s.CreateGameObject("P");
        auto* hp = p->AddComponent<HealthStat>(); hp->regenPerSecond = 0.0f;
        auto* hu = p->AddComponent<HungerStat>();
        hu->drainPerSecond = 200.0f; hu->starveDamage = 20.0f;
        auto* sv2 = s.CreateGameObject("Q")->AddComponent<SurvivalStats>();
        sv2->hungerDrain = 200.0f; sv2->starveDamage = 10.0f; sv2->regenWhenFed = 0.0f;
        s.Start();
        Run(s, 2.0f, 60);
        CHECK(!types.empty());
        bool allHunger = true; for (auto t : types) allHunger &= (t == DamageType::Hunger);
        CHECK(allHunger);
        CHECK(hp->health < 100.0f && sv2->health < 100.0f);
        ClearDamageListeners();
    }

    // --- projectile and melee reach the one path ------------------------------
    {
        ClearDamageListeners();
        DamageEvent last; int n = 0;
        AddDamagedListener([&](const DamageEvent& e) { last = e; ++n; });
        Scene s("calls");
        GameObject* shooter = s.CreateGameObject("Shooter");
        GameObject* parent = s.CreateGameObject("Body");
        auto* hp = parent->AddComponent<HealthStat>();
        GameObject* limb = s.CreateGameObject("Limb");
        limb->transform->SetParent(parent->transform);
        s.Start();
        CHECK(DamageGameObject(limb, 15.0f, shooter));          // a shot hits a child: the body takes it
        CHECK(n == 1 && last.source == shooter && last.type == DamageType::Physical && last.target == parent);
        CHECK_NEAR(hp->health, 85.0f, 1e-3f);
        CHECK(ApplyCombatDamage(parent, 10.0f, Vec3{0, 0, -3}, Vec3{0, 0, 0}));   // melee / gun
        CHECK(n == 2 && last.type == DamageType::Physical && last.hasHitPoint);
        CHECK_NEAR(hp->health, 75.0f, 1e-3f);
        ClearDamageListeners();
    }

    // --- save and load round trip; an old saved line still loads ---------------
    {
        Scene s("ser");
        GameObject* p = s.CreateGameObject("P");
        auto* h = p->AddComponent<HealthStat>();
        h->maxHealth = 150.0f;
        h->SetResistance(DamageType::Fire, 0.25f); h->SetResistance(DamageType::Fall, 1.0f);
        h->SetDeathAction(HealthStat::DeathAction::Nothing);
        std::string txt = SceneSerializer::SerializeObject(*p);
        Scene s2("ser2");
        GameObject* c = SceneSerializer::InstantiateFromText(s2, txt);
        CHECK(c != nullptr);
        auto* h2 = c ? c->GetComponent<HealthStat>() : nullptr;
        CHECK(h2 != nullptr);
        if (h2) {
            CHECK_NEAR(h2->maxHealth, 150.0f, 1e-3f);
            CHECK_NEAR(h2->ResistanceTo(DamageType::Fire), 0.25f, 1e-6f);
            CHECK_NEAR(h2->ResistanceTo(DamageType::Fall), 1.0f, 1e-6f);
            CHECK_NEAR(h2->ResistanceTo(DamageType::Cold), 0.0f, 1e-6f);
            CHECK(h2->GetDeathAction() == HealthStat::DeathAction::Nothing);
        }
        // An old file: stat_health line ends after the hit-flinch flag, no resistances.
        std::string full = txt;
        size_t a = full.find("  stat_health ");
        size_t b = a == std::string::npos ? a : full.find('\n', a);
        if (a != std::string::npos && b != std::string::npos) {
            std::string line = "  stat_health 80 2 1 3 20 1 1 1 0.1 0 0 0 0 0 0 0 0 3 0 1 0";
            full.replace(a, b - a, line);
            Scene s3("ser3");
            GameObject* o = SceneSerializer::InstantiateFromText(s3, full);
            auto* h3 = o ? o->GetComponent<HealthStat>() : nullptr;
            CHECK(h3 != nullptr);
            if (h3) {
                CHECK_NEAR(h3->maxHealth, 80.0f, 1e-3f);
                CHECK_NEAR(h3->armor, 2.0f, 1e-3f);
                CHECK_NEAR(h3->ResistanceTo(DamageType::Fire), 0.0f, 1e-6f);   // no resistances in the old file
                CHECK(!h3->keepActiveOnDeath);
            }
        }
        // Survival Stats, RPG Stats and NPC Controller still round-trip with their old fields
        GameObject* q = s.CreateGameObject("Q");
        auto* sv = q->AddComponent<SurvivalStats>(); sv->armor = 4.0f; sv->resistance = 0.2f; sv->invincibleTime = 0.3f;
        Scene s4("ser4");
        GameObject* q2 = SceneSerializer::InstantiateFromText(s4, SceneSerializer::SerializeObject(*q));
        auto* sv2 = q2 ? q2->GetComponent<SurvivalStats>() : nullptr;
        CHECK(sv2 != nullptr);
        if (sv2) { CHECK_NEAR(sv2->armor, 4.0f, 1e-4f); CHECK_NEAR(sv2->resistance, 0.2f, 1e-4f); CHECK_NEAR(sv2->invincibleTime, 0.3f, 1e-4f); }
    }

    TEST_MAIN_RESULT();
}
