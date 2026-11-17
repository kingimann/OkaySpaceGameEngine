#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Combat/WeaponDefinition.hpp"

using namespace okay;

int main() {
    RUN_SUITE("health_contract");

    // ---- the modules registered their adapters (finder per model, in priority order)
    {
        Scene s("T");
        GameObject* a = s.CreateGameObject("A"); a->AddComponent<HealthStat>();
        GameObject* b = s.CreateGameObject("B"); b->AddComponent<SurvivalStats>();
        GameObject* c = s.CreateGameObject("C"); c->AddComponent<Stats>();
        GameObject* d = s.CreateGameObject("D"); d->AddComponent<NPCController>();
        GameObject* e = s.CreateGameObject("E");
        GameObject* both = s.CreateGameObject("Both"); both->AddComponent<SurvivalStats>(); both->AddComponent<HealthStat>();
        CHECK(std::string(health::Find(a)->Model()) == "HealthStat");
        CHECK(std::string(health::Find(b)->Model()) == "SurvivalStats");
        CHECK(std::string(health::Find(c)->Model()) == "Stats");
        CHECK(std::string(health::Find(d)->Model()) == "NPCController");
        CHECK(health::Find(e) == nullptr);
        CHECK(std::string(health::Find(both)->Model()) == "HealthStat");   // the HealthStat is the object's own health
        CHECK(!health::Damage(e, 5.0f).handled);                          // nothing to hurt
        CHECK(health::Heal(e, 5.0f) == 0.0f);
    }

    // ---- same numbers as the old direct call (armor, resistance, minimum damage)
    {
        Scene s("T");
        auto make = [&](const char* n) {
            GameObject* g = s.CreateGameObject(n);
            auto* h = g->AddComponent<HealthStat>();
            h->maxHealth = 100.0f; h->armor = 5.0f; h->resistance = 0.2f;
            return h;
        };
        HealthStat* direct = make("Direct"); HealthStat* viaContract = make("Via");
        s.Start();
        direct->Damage(30.0f);
        DamageResult r = health::Damage(viaContract->gameObject, 30.0f);
        CHECK_NEAR(direct->health, viaContract->health, 1e-5);
        CHECK(r.handled && !r.blocked && !r.killed);
        CHECK_NEAR(r.applied, 100.0f - viaContract->health, 1e-4);
        auto ih = health::Find(viaContract->gameObject);
        CHECK_NEAR(ih->Current(), viaContract->health, 1e-6);
        CHECK_NEAR(ih->Armor(), 5.0f, 1e-6);
        CHECK_NEAR(ih->Heal(10.0f), 10.0f, 1e-5);
        // god mode blocks
        viaContract->godMode = true;
        r = health::Damage(viaContract->gameObject, 50.0f);
        CHECK(r.handled && r.blocked && r.applied == 0.0f);
        viaContract->godMode = false;
        // a killing blow
        r = health::Damage(viaContract->gameObject, 5000.0f);
        CHECK(r.killed && viaContract->IsDead());
        CHECK(health::Damage(viaContract->gameObject, 5.0f).blocked);     // already dead
        CHECK(ih->IsDead());
    }

    // ---- hit zones and damage types
    {
        Scene s("T");
        GameObject* g = s.CreateGameObject("Guard");
        auto* h = g->AddComponent<HealthStat>(); h->maxHealth = 1000.0f; h->health = 1000.0f;
        auto* z = g->AddComponent<HitZones>();
        z->typeScale[(int)DamageType::Fire] = 0.5f;
        s.Start();
        DamageEvent e = DamageEvent::Of(10.0f);
        e.zone = "head";
        health::Damage(g, e);
        CHECK_NEAR(h->health, 980.0f, 1e-4);                               // head x2
        e = DamageEvent::Of(10.0f); e.point = Vec3{0, 1.7f, 0}; e.hasPoint = true;
        health::Damage(g, e);                                              // no zone name: the point is high, so head
        CHECK_NEAR(h->health, 960.0f, 1e-4);
        e = DamageEvent::Of(10.0f); e.point = Vec3{0, 1.0f, 0}; e.hasPoint = true;
        health::Damage(g, e);                                              // body x1
        CHECK_NEAR(h->health, 950.0f, 1e-4);
        e = DamageEvent::Of(10.0f); e.point = Vec3{0, 0.2f, 0}; e.hasPoint = true;
        health::Damage(g, e);                                              // legs x0.75
        CHECK_NEAR(h->health, 942.5f, 1e-4);
        e = DamageEvent::Of(10.0f, DamageType::Fire);
        health::Damage(g, e);                                              // fire x0.5
        CHECK_NEAR(h->health, 937.5f, 1e-4);

        DamageType t;
        CHECK(DamageTypeFromName("bullet", t) && t == DamageType::Bullet && !DamageTypeFromName("nope", t));
        CHECK(std::string(DamageTypeName(DamageType::Drowning)) == "drowning");

        // the scene record round-trips
        z->zones.push_back({"tail", 3.0f});
        z->headFromHeight = 1.8f;
        std::string text = SceneSerializer::Serialize(s);
        CHECK(text.find("hitzones") != std::string::npos);
        Scene back("B");
        std::string err;
        CHECK(SceneSerializer::Deserialize(back, text, &err));
        auto* z2 = back.Find("Guard") ? back.Find("Guard")->GetComponent<HitZones>() : nullptr;
        CHECK(z2 != nullptr);
        if (z2) {
            CHECK_NEAR(z2->ZoneScale("head"), 2.0f, 1e-5);
            CHECK_NEAR(z2->ZoneScale("tail"), 3.0f, 1e-5);
            CHECK_NEAR(z2->headFromHeight, 1.8f, 1e-5);
            CHECK_NEAR(z2->typeScale[(int)DamageType::Fire], 0.5f, 1e-5);
        }
    }

    // ---- one damage path: shots and blows now hurt every kind of health (before, only HealthStat took a shot)
    {
        Scene s("T");
        GameObject* npc = s.CreateGameObject("Npc"); auto* n = npc->AddComponent<NPCController>(); n->maxHealth = 30.0f;
        GameObject* rpg = s.CreateGameObject("Rpg"); auto* st = rpg->AddComponent<Stats>();
        GameObject* sv = s.CreateGameObject("Surv"); auto* ss = sv->AddComponent<SurvivalStats>();
        s.Start();
        CHECK(ApplyCombatDamage(rpg, 40.0f, Vec3{0, 0, 0}, Vec3{0, 0, 0}, DamageType::Bullet));
        CHECK(st->health < 100.0f);
        CHECK(ApplyCombatDamage(sv, 40.0f, Vec3{0, 0, 0}, Vec3{0, 0, 0}, DamageType::Melee));
        CHECK(ss->health < 100.0f);
        // a hit on a child counts for the parent that has the health
        GameObject* child = s.CreateGameObject("Hand");
        child->transform->SetParent(rpg->transform);
        float before = st->health;
        CHECK(health::Damage(child, 20.0f, true).handled);
        CHECK(st->health < before);
        CHECK(!health::Damage(child, 20.0f, false).handled);
        // heal and kill through the contract
        auto h = health::Find(rpg);
        h->Kill();
        CHECK(st->IsDead());
        CHECK(h->Revive() && !st->IsDead());
        auto nh = health::Find(npc);
        CHECK(!nh->Revive());                                              // an NPC cannot be revived this way
        DamageResult r = nh->ApplyDamage(DamageEvent::Of(500.0f));
        CHECK(r.killed);
    }
    TEST_MAIN_RESULT();
}
