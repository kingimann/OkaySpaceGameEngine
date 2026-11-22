// The one health lookup and the one damage path (okay/Components/Damage.hpp) as the modules use them:
// HealthOf's order is the order DealDamage hurts in, and the UI owner keys read the same health; Hit Zones scale
// a hit inside DealDamage; a shot on a child reaches the parent that has the health; the hitzones record round-trips.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/Damage.hpp"
#include "okay/Components/HitZones.hpp"
#include "okay/Components/Projectile3D.hpp"
#include "okay/Components/UIOwnerData.hpp"

using namespace okay;

namespace {
// What the UI owner key "owner.health" shows for a piece of UI under `owner` (-1 when nothing).
float OwnerHealth(Scene& s, GameObject* owner) {
    GameObject* ui = s.CreateGameObject("Bar");
    ui->transform->SetParent(owner->transform);
    std::string text; float number = -1.0f; bool isNumber = false;
    if (!uiowner::Lookup(ui, "owner.health", text, number, isNumber) || !isNumber) return -1.0f;
    return number;
}
} // namespace

int main() {
    RUN_SUITE("health_contract");

    // ---- lookup order: Health Stat, then Survival Stats, then RPG Stats, then the NPC's own health
    {
        Scene s("T");
        GameObject* a = s.CreateGameObject("A"); auto* ah = a->AddComponent<HealthStat>(); ah->health = 11.0f;
        GameObject* b = s.CreateGameObject("B"); auto* bs = b->AddComponent<SurvivalStats>(); bs->health = 22.0f;
        GameObject* c = s.CreateGameObject("C"); auto* cs = c->AddComponent<Stats>(); cs->health = 33.0f;
        GameObject* d = s.CreateGameObject("D"); auto* dn = d->AddComponent<NPCController>(); dn->health = 44.0f;
        GameObject* e = s.CreateGameObject("E");
        // every pair of models on one object: the earlier one in the order wins
        GameObject* hs = s.CreateGameObject("HS"); hs->AddComponent<SurvivalStats>()->health = 2.0f; hs->AddComponent<HealthStat>()->health = 1.0f;
        GameObject* sr = s.CreateGameObject("SR"); sr->AddComponent<Stats>()->health = 3.0f;
        auto* srs = sr->AddComponent<SurvivalStats>(); srs->health = 2.0f; srs->armor = 0.0f;
        GameObject* rn = s.CreateGameObject("RN"); rn->AddComponent<NPCController>()->health = 4.0f; rn->AddComponent<Stats>()->health = 3.0f;
        GameObject* hn = s.CreateGameObject("HN"); hn->AddComponent<NPCController>()->health = 4.0f; hn->AddComponent<HealthStat>()->health = 1.0f;
        float h = -1.0f, m = -1.0f;
        CHECK(HealthOf(a, h, m) && h == 11.0f);
        CHECK(HealthOf(b, h, m) && h == 22.0f);
        CHECK(HealthOf(c, h, m) && h == 33.0f);
        CHECK(HealthOf(d, h, m) && h == 44.0f);
        CHECK(!HealthOf(e, h, m) && !HasHealth(e) && HasHealth(a) && HasHealth(d));
        CHECK(HealthOf(hs, h, m) && h == 1.0f);
        CHECK(HealthOf(sr, h, m) && h == 2.0f);
        CHECK(HealthOf(rn, h, m) && h == 3.0f);
        CHECK(HealthOf(hn, h, m) && h == 1.0f);
        // the UI owner keys read the same health (before, they put RPG Stats ahead of Survival Stats)
        CHECK(OwnerHealth(s, a) == 11.0f);
        CHECK(OwnerHealth(s, d) == 44.0f);
        CHECK(OwnerHealth(s, sr) == 2.0f);
        CHECK(OwnerHealth(s, hn) == 1.0f);
        CHECK(OwnerHealth(s, e) == -1.0f);
        // and a hit lands on the holder the lookup names
        s.Start();
        float before = 0.0f;
        HealthOf(sr, before, m);
        const float rpgBefore = sr->GetComponent<Stats>()->health;
        CHECK(DealDamage(sr, 1.0f));
        float after = 0.0f;
        HealthOf(sr, after, m);
        CHECK(after < before);
        CHECK(sr->GetComponent<Stats>()->health == rpgBefore);   // the RPG Stats behind it were not touched
    }

    // ---- hit zones and damage types, applied by DealDamage
    {
        Scene s("T");
        GameObject* g = s.CreateGameObject("Guard");
        auto* h = g->AddComponent<HealthStat>(); h->maxHealth = 1000.0f; h->health = 1000.0f; h->armor = 0.0f;
        auto* z = g->AddComponent<HitZones>();
        z->typeScale[(int)DamageType::Fire] = 0.5f;
        s.Start();
        DealDamage(g, DamageEvent::At(10.0f, Vec3{0, 1.7f, 0}, DamageType::Generic));   // high on the body: head x2
        CHECK_NEAR(h->health, 980.0f, 1e-3);
        DealDamage(g, DamageEvent::At(10.0f, Vec3{0, 1.0f, 0}, DamageType::Generic));   // body x1
        CHECK_NEAR(h->health, 970.0f, 1e-3);
        DealDamage(g, DamageEvent::At(10.0f, Vec3{0, 0.2f, 0}, DamageType::Generic));   // legs x0.75
        CHECK_NEAR(h->health, 962.5f, 1e-3);
        DealDamage(g, DamageEvent::From(10.0f, Vec3{0, 0.2f, 0}, DamageType::Generic)); // where it came from, not where it landed: no zone
        CHECK_NEAR(h->health, 952.5f, 1e-3);
        DealDamage(g, DamageEvent::Make(10.0f, DamageType::Fire));                      // fire x0.5
        CHECK_NEAR(h->health, 947.5f, 1e-3);
        z->typeScale[(int)DamageType::Cold] = 0.0f;
        CHECK(!DealDamage(g, DamageEvent::Make(10.0f, DamageType::Cold)));              // immune to this kind: nothing taken
        CHECK_NEAR(h->health, 947.5f, 1e-3);

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
            CHECK_NEAR(z2->typeScale[(int)DamageType::Cold], 0.0f, 1e-5);
        }
    }

    // ---- a shot on a child reaches the parent that has the health, of any kind
    {
        Scene s("T");
        GameObject* rpg = s.CreateGameObject("Rpg"); auto* st = rpg->AddComponent<Stats>();
        GameObject* child = s.CreateGameObject("Hand");
        child->transform->SetParent(rpg->transform);
        GameObject* wall = s.CreateGameObject("Wall");
        s.Start();
        const float before = st->health;
        CHECK(DamageGameObject(child, DamageEvent::At(20.0f, Vec3{0, 1, 0}, DamageType::Physical)));
        CHECK(st->health < before);
        CHECK(!DamageGameObject(wall, 20.0f));
    }
    TEST_MAIN_RESULT();
}
