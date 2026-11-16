// One NaN or infinite damage value must not make a character immortal (issue #888, Finding 57).
// Before the fix the `amount <= 0` guards let NaN through (every comparison with NaN is false), so health
// became NaN and `health <= 0` was never true again; infinity did the same through inf * 0 (100%
// resistance). Now every damage, heal and set-health call refuses a non-finite amount (health unchanged,
// one warning per component), a later normal lethal hit still kills, and finite hits give exactly the
// same numbers as before.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/SurvivalAfflictions.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <string>

using namespace okay;

namespace {

const float kNaN = std::numeric_limits<float>::quiet_NaN();
const float kInf = std::numeric_limits<float>::infinity();
const float kBad[] = {kNaN, kInf, -kInf};

int g_warnings = 0;
void CountWarnings() {
    Log::sink = [](Log::Level l, const std::string& m) {
        if (l == Log::Level::Warning && m.find("NaN or infinite") != std::string::npos) ++g_warnings;
    };
}

// Run `bad` with each non-finite value; health must stay finite and exactly what it was.
template <class Get, class Bad>
bool Unchanged(const char* what, Get get, Bad bad) {
    bool ok = true;
    for (float v : kBad) {
        const float before = get();
        bad(v);
        const float after = get();
        if (!std::isfinite(after) || after != before) {
            std::cout << "    " << what << "(" << v << "): health " << before << " -> " << after << "\n";
            ok = false;
        }
    }
    return ok;
}

}  // namespace

int main() {
    RUN_SUITE("nan_damage");
    Log::console = false;
    CountWarnings();

    // ---- HealthStat: Damage, Damage(from), ApplyDamage, Heal, AddArmor, the server's set-health ----
    {
        Scene s("H");
        GameObject* go = s.CreateGameObject("Enemy");
        auto* h = go->AddComponent<HealthStat>();
        s.Start();
        h->Damage(10.0f);
        CHECK(h->health == 90.0f);
        auto hp = [&] { return h->health; };
        const int w0 = g_warnings;
        CHECK(Unchanged("HealthStat.Damage", hp, [&](float v) { h->Damage(v); }));
        CHECK(g_warnings == w0 + 1);   // once per component, not once per call
        CHECK(Unchanged("HealthStat.Damage(from)", hp, [&](float v) { h->Damage(v, Vec3{1, 0, 0}); }));
        CHECK(Unchanged("HealthStat.ApplyDamage", hp, [&](float v) { h->ApplyDamage(v); }));
        CHECK(Unchanged("HealthStat.Heal", hp, [&](float v) { h->Heal(v); }));
        CHECK(Unchanged("HealthStat.AddArmor", hp, [&](float v) { h->AddArmor(v); }));
        CHECK(std::isfinite(h->armor) && h->armor == 0.0f);
        CHECK(Unchanged("HealthStat.SetHealthFromServer", hp, [&](float v) { h->SetHealthFromServer(v); }));
        CHECK(Unchanged("HealthStat.RespawnFromServer", hp, [&](float v) { h->RespawnFromServer(v); }));
        CHECK(g_warnings == w0 + 1);
        CHECK(!h->IsDead());
        h->resistance = 1.0f;   // the finding's inf * (1 - 1) = NaN case
        CHECK(Unchanged("HealthStat.Damage (resistance 1)", hp, [&](float v) { h->Damage(v); }));
        h->resistance = 0.0f;
        h->Damage(1000.0f);     // a normal lethal hit still kills
        CHECK(h->IsDead());
        CHECK(h->health == 0.0f);
        std::cout << "  HealthStat after NaN/inf hits then 1000: health=" << h->health << " dead=" << h->IsDead() << "\n";
    }
    // Finite hits are exactly as before: armor, resistance and minDamage.
    {
        Scene s("F");
        auto* h = s.CreateGameObject("A")->AddComponent<HealthStat>();
        s.Start();
        h->armor = 2.0f; h->resistance = 0.25f; h->minDamage = 1.0f;
        h->Damage(10.0f);
        CHECK(h->health == 100.0f - (10.0f - 2.0f) * (1.0f - 0.25f));
        h->Damage(2.5f);                            // mitigated to 0.375, raised to minDamage 1
        CHECK(h->health == 100.0f - 6.0f - 1.0f);
        h->Heal(3.0f);
        CHECK(h->health == 96.0f);
        h->Damage(0.0f); h->Damage(-5.0f);          // zero and negative are still ignored, without a warning
        CHECK(h->health == 96.0f);
    }

    // ---- Stats: TakeDamage, Heal ----
    {
        Scene s("S");
        auto* st = s.CreateGameObject("Rpg")->AddComponent<Stats>();
        s.Start();
        st->TakeDamage(20.0f);                      // 20 - defense 5
        CHECK(st->health == 85.0f);
        auto hp = [&] { return st->health; };
        CHECK(Unchanged("Stats.TakeDamage", hp, [&](float v) { st->TakeDamage(v); }));
        CHECK(Unchanged("Stats.Heal", hp, [&](float v) { st->Heal(v); }));
        CHECK(!st->IsDead());
        st->TakeDamage(1e9f);
        CHECK(st->IsDead());
        std::cout << "  Stats after NaN/inf then 1e9: health=" << st->health << " IsDead=" << st->IsDead() << "\n";
        st->Restore();
        st->Heal(-10.0f);                           // finite: unchanged behaviour (a negative heal still hurts)
        CHECK(st->health == 90.0f);
    }

    // ---- SurvivalStats: Damage, Heal, AddArmor ----
    {
        Scene s("V");
        auto* sv = s.CreateGameObject("Survivor")->AddComponent<SurvivalStats>();
        s.Start();
        sv->Damage(10.0f);
        const float h0 = sv->health;
        CHECK(std::isfinite(h0) && h0 < sv->maxHealth);
        auto hp = [&] { return sv->health; };
        CHECK(Unchanged("SurvivalStats.Damage", hp, [&](float v) { sv->Damage(v); }));
        CHECK(Unchanged("SurvivalStats.Heal", hp, [&](float v) { sv->Heal(v); }));
        CHECK(Unchanged("SurvivalStats.AddArmor", hp, [&](float v) { sv->AddArmor(v); }));
        CHECK(std::isfinite(sv->armor));
        sv->resistance = 1.0f;
        CHECK(Unchanged("SurvivalStats.Damage (resistance 1)", hp, [&](float v) { sv->Damage(v); }));
        sv->resistance = 0.0f;
        sv->Damage(1e6f);
        CHECK(sv->dead);
    }

    // ---- NPCController: Damage ----
    {
        Scene s("N");
        auto* npc = s.CreateGameObject("Guard")->AddComponent<NPCController>();
        s.Start();
        npc->Damage(5.0f);
        CHECK(npc->health == npc->maxHealth - 5.0f);
        auto hp = [&] { return npc->health; };
        CHECK(Unchanged("NPCController.Damage", hp, [&](float v) { npc->Damage(v); }));
        CHECK(!npc->IsDead());
        npc->Damage(1000.0f);
        CHECK(npc->IsDead());
    }

    // ---- BuildPiece (destructible building pieces): Damage, Heal ----
    {
        Scene s("B");
        auto* bp = s.CreateGameObject("Wall")->AddComponent<BuildPiece>();
        CHECK(!bp->Damage(50.0f));
        CHECK(bp->health == 150.0f);
        auto hp = [&] { return bp->health; };
        bool destroyed = false;
        CHECK(Unchanged("BuildPiece.Damage", hp, [&](float v) { destroyed |= bp->Damage(v); }));
        CHECK(!destroyed);
        CHECK(Unchanged("BuildPiece.Heal", hp, [&](float v) { bp->Heal(v); }));
        CHECK(bp->Damage(1000.0f));                 // still breaks
        CHECK(bp->health == 0.0f);
    }

    // ---- The shared helpers: DamageHealthOn / HealOn (afflictions) and ApplyCombatDamage (weapons) ----
    {
        Scene s("C");
        GameObject* go = s.CreateGameObject("Target");
        auto* h = go->AddComponent<HealthStat>();
        s.Start();
        auto hp = [&] { return h->health; };
        CHECK(Unchanged("DamageHealthOn", hp, [&](float v) { DamageHealthOn(go, v); }));
        CHECK(Unchanged("HealOn", hp, [&](float v) { HealOn(go, v); }));
        bool hit = false;
        CHECK(Unchanged("ApplyCombatDamage", hp, [&](float v) { hit |= ApplyCombatDamage(go, v, Vec3{}, Vec3{}); }));
        CHECK(!hit);                                // a refused hit is not reported as a hit
        CHECK(ApplyCombatDamage(go, 30.0f, Vec3{}, Vec3{}));
        CHECK(h->health == 70.0f);
        CHECK(ApplyCombatDamage(go, 500.0f, Vec3{}, Vec3{}));
        CHECK(h->IsDead());
    }

    // ---- Equipment: an item whose "armor" reads as NaN counts as no armor ----
    {
        ItemDatabase& db = ItemDatabase::Get();
        db.Clear();
        db.LoadText("okayitems 1\nitem \"cursed\"\n  name \"Cursed Helm\"\n  equip head\n  custom \"armor\" \"nan\"\nend\n");
        Scene s("E");
        GameObject* hero = s.CreateGameObject("Hero");
        auto* inv = hero->AddComponent<Inventory>(); inv->capacity = 4;
        auto* eq = hero->AddComponent<Equipment>();
        auto* h = hero->AddComponent<HealthStat>();
        s.Start();
        inv->Add("cursed", 1);
        const int head = eq->Find("Head");
        CHECK(head >= 0 && eq->EquipFrom(*inv, 0, head).Ok());
        s.Update(0.0f);
        CHECK(std::isfinite(h->equipArmor) && h->equipArmor == 0.0f);
        h->Damage(10.0f);
        std::cout << "  cursed helm (armor nan), hit for 10: health=" << h->health << "\n";
        CHECK(h->health == 90.0f);
        db.Clear();
    }

    // ---- From a script, the way docs/try_it.md shows it: pow(-1, 0.5) is NaN, pow(10, 100) is infinite ----
    // (`log` is not the math log in OkayScript: it is an alias of print.)
    {
        Scene s("Script");
        GameObject* enemy = s.CreateGameObject("Enemy");
        auto* st = enemy->AddComponent<Stats>();
        auto* sc = s.CreateGameObject("Tester")->AddComponent<ScriptComponent>("okayscript");
        std::string err;
        CHECK(sc->LoadSource("var hp_after_bad = -1;\n"
                             "function start() {\n"
                             "  hurt(\"Enemy\", pow(-1, 0.5));\n"     // NaN
                             "  hurt(\"Enemy\", pow(10, 100));\n"     // +infinity
                             "  heal(\"Enemy\", pow(-1, 0.5));\n"
                             "  hp_after_bad = get_hp(\"Enemy\");\n"
                             "  hurt(\"Enemy\", 500);\n"              // a normal lethal hit
                             "}\n", &err));
        s.Start();
        s.Update(0.0f);
        const float hpAfterBad = sc->VM() ? sc->VM()->GetGlobal("hp_after_bad").AsFloat() : -2.0f;
        std::cout << "  script: after NaN/inf hurt and heal hp=" << hpAfterBad << ", after hurt 500 hp=" << st->health
                  << " dead=" << st->IsDead() << "\n";
        CHECK(hpAfterBad == 100.0f);
        CHECK(st->IsDead());
    }

    std::cout << "  warnings logged: " << g_warnings << "\n";
    Log::sink = nullptr;
    TEST_MAIN_RESULT();
}
