// One health per object (scene format 4): Survival Stats, RPG Stats and an NPC Controller keep no health of their
// own; each uses the object's Health Stat, which is added with them. Old scenes (format 3) that kept health in the
// `survival`, `stats` or `npc` records are moved into the Health Stat on load, and the script health functions act
// on the one health. Written from the engine's own design (docs/design/modular_gameplay_contracts.md 3.1 and 6).
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/Damage.hpp"
#include "okay/Components/NPCController.hpp"
#include "okay/Components/Stats.hpp"
#include "okay/Components/SurvivalStats.hpp"
#include "okay/Scene/SceneMigration.hpp"
#include <string>
#include <vector>

using namespace okay;

namespace {

bool Has(const std::string& s, const std::string& needle) { return s.find(needle) != std::string::npos; }
bool Any(const std::vector<std::string>& v, const std::string& needle) {
    for (const auto& w : v) if (Has(w, needle)) return true;
    return false;
}
// The one record line of `key` in a saved scene ("" if none).
std::string Line(const std::string& text, const std::string& key) {
    const std::size_t at = text.find("  " + key + " ");
    if (at == std::string::npos) return {};
    return text.substr(at + 2, text.find('\n', at) - at - 2);
}
// A format-3 scene with one object "O" holding the given records (what a build before one-health wrote).
std::string V3(const std::string& records) {
    return "okayscene 3\nname \"Old\"\ngameobject 0 \"O\"\n  active 1\n  parent -1\n" + records + "end\n";
}
// Record lines exactly as the format-3 writer produced them.
const char* kSurvivalV3 = "  survival 150 3 1 2 100 1.5 2 100 2 3 100 12 20 2 100 8 25 5 100 3 6 2 0 1 0 0.25 0 0.5\n";
const char* kSurvivalV3Plain = "  survival 120 0 1 5 100 1.5 2 100 2 3 100 12 20 2 100 8 25 5 100 3 6 2 1 1 1\n";  // before the mitigation fields
const char* kStatsV3 = "  stats 40 80 50 50 1 0 100 10 5 5 5\n";
const char* kNpcV3 = "  npc 1 2.5 10 8 1.6 8 1 1 \"Player\" 55 1 4.5 360 14 0 120 0 1.6 12 0 3 4 0 1 0 25 2 1 1 0 0.25 0 1 1 0 0 0.5\n";
const char* kNpcV3NoHealth = "  npc 1 2.5 10 8 1.6 8 1 1 \"Player\"\n";
const char* kHealthV3 = "  stat_health 200 7 0 0 25 1 1 1 0 0 0 0 0 0 0 0 3 0\n";

bool Load(Scene& s, const std::string& text, std::vector<std::string>* warnings = nullptr) {
    std::string err;
    const bool ok = SceneSerializer::Deserialize(s, text, &err, warnings);
    if (!ok) std::cerr << "  load failed: " << err << "\n";
    return ok;
}
HealthStat* TheHealth(Scene& s) {
    GameObject* o = s.Find("O");
    return o ? o->GetComponent<HealthStat>() : nullptr;
}
ActionList::Item Item(const std::string& op, std::vector<std::string> a = {}) {
    return ActionList::Item{op, std::move(a)};
}

} // namespace

int main() {
    RUN_SUITE("one_health");
    Log::console = false;

    // ---- 1. Adding an owner adds the one Health Stat, with the owner's old defaults --------------------------
    {
        Scene s("add");
        GameObject* p = s.CreateGameObject("P");
        auto* sv = p->AddComponent<SurvivalStats>();
        HealthStat* h = p->GetComponent<HealthStat>();
        CHECK(sv && h);
        CHECK(p->GetComponents<HealthStat>().size() == 1);
        if (h) {
            CHECK_NEAR(h->maxHealth, 100.0f, 1e-6f);
            CHECK_NEAR(h->regenDelay, 5.0f, 1e-6f);                              // Survival Stats' old regen delay
            CHECK(h->GetDeathAction() == HealthStat::DeathAction::Disable);      // its death switched the player off
            CHECK(h->publishPrefs && h->sendMessages);                           // the player's {health} and `died`
        }
        CHECK(sv->Health() == h);
        CHECK(p->RequiredBy(h) == sv);                                           // the editor refuses to remove it

        GameObject* e = s.CreateGameObject("E");
        e->AddComponent<NPCController>();
        HealthStat* eh = e->GetComponent<HealthStat>();
        CHECK(eh != nullptr);
        if (eh) {
            CHECK_NEAR(eh->maxHealth, 30.0f, 1e-6f);                             // the NPC's old 30 HP
            CHECK(eh->destroyOnDeath && !eh->playDeathAnim);                    // gone at once, as before
            CHECK(!eh->publishPrefs && !eh->publishBar && !eh->sendMessages);   // not the player's {health}
        }

        GameObject* r = s.CreateGameObject("R");
        r->AddComponent<Stats>();
        HealthStat* rh = r->GetComponent<HealthStat>();
        CHECK(rh != nullptr);
        if (rh) {
            CHECK_NEAR(rh->minDamage, 1.0f, 1e-6f);                              // RPG: every hit deals at least 1
            CHECK(rh->GetDeathAction() == HealthStat::DeathAction::Nothing);
            CHECK(!rh->publishPrefs);
        }

        // An object that already has a Health Stat keeps it as it is, and gets no second one.
        GameObject* k = s.CreateGameObject("K");
        auto* kh = k->AddComponent<HealthStat>(); kh->maxHealth = 77.0f;
        k->AddComponent<SurvivalStats>(); k->AddComponent<Stats>(); k->AddComponent<NPCController>();
        CHECK(k->GetComponents<HealthStat>().size() == 1 && k->GetComponent<HealthStat>() == kh);
        CHECK_NEAR(kh->maxHealth, 77.0f, 1e-6f);
        CHECK_NEAR(kh->regenDelay, 0.0f, 1e-6f);
    }

    // ---- 2. Survival Stats: starving, regen and death all act on the Health Stat -----------------------------
    {
        Scene s("survive");
        GameObject* p = s.CreateGameObject("P");
        auto* sv = p->AddComponent<SurvivalStats>();
        sv->publishPrefs = false; sv->publishBars = false;
        HealthStat* h = p->GetComponent<HealthStat>();
        h->publishPrefs = false; h->publishBar = false;
        s.Start();
        sv->hunger = 0.0f; sv->starveDamage = 10.0f; sv->hungerDrain = 0.0f;
        for (int i = 0; i < 5; ++i) s.Update(0.1f);
        CHECK_NEAR(h->health, 95.0f, 0.01f);                                     // 10 HP/s for half a second
        CHECK(h->RegenPaused());                                                 // the hit holds regen back
        sv->Eat(50.0f);
        for (int i = 0; i < 70; ++i) s.Update(0.1f);                             // past the 5 s delay, fed and hydrated
        CHECK(h->health > 95.0f);                                                // "regen while fed" healed the Health Stat
        CHECK(h->health <= h->maxHealth);
        CHECK(DealDamage(p, 1000.0f));
        CHECK(h->IsDead() && IsObjectDead(p));
        CHECK(!p->active);                                                       // switched off, as Survival Stats did
        const float hunger = sv->hunger;
        s.Update(0.1f);
        CHECK_NEAR(sv->hunger, hunger, 1e-6f);                                   // a dead player's stats stop
    }

    // ---- 3. RPG Stats: defense is armor on the Health Stat, at least 1 per hit, level-ups raise its max --------
    {
        Scene s("rpg");
        GameObject* g = s.CreateGameObject("Hero");
        auto* st = g->AddComponent<Stats>(); st->defense = 5;
        HealthStat* h = st->Health();
        s.Start();
        CHECK(DealDamage(g, 20.0f));
        CHECK_NEAR(h->health, 85.0f, 1e-3f);                                     // 20 - 5 defense
        CHECK(DealDamage(g, 3.0f));
        CHECK_NEAR(h->health, 84.0f, 1e-3f);                                     // below defense: the minimum 1
        st->equipDefense = 10;
        CHECK(DealDamage(g, 20.0f));
        CHECK_NEAR(h->health, 79.0f, 1e-3f);                                     // worn defense counts too
        st->equipDefense = 0;
        st->xp = 0; st->xpToNext = 100;
        st->AddXP(120);
        CHECK(st->level == 2);
        CHECK_NEAR(h->maxHealth, 120.0f, 1e-3f);
        CHECK_NEAR(h->health, 120.0f, 1e-3f);                                    // refilled on level up
        CHECK(KillObject(g));
        CHECK(h->IsDead());
        CHECK(g->active);                                                        // RPG death changes nothing on the object
        st->Restore();
        CHECK(!h->IsDead() && std::fabs(h->health - h->maxHealth) < 1e-3f);
    }

    // ---- 4. NPC Controller: God Mode is the old Invulnerable, death broadcasts npc_died and removes it ---------
    {
        ActionList::ResetVars();
        Scene s("npc");
        GameObject* listener = s.CreateGameObject("L");
        auto* al = listener->AddComponent<ActionList>();
        al->trigger = ActionList::Trigger::OnMessage; al->triggerKey = "npc_died";
        al->instructions = { Item("set_var", {"npcdied", "1"}) };
        auto* died = s.CreateGameObject("D")->AddComponent<ActionList>();
        died->trigger = ActionList::Trigger::OnMessage; died->triggerKey = "died";
        died->instructions = { Item("set_var", {"playerdied", "1"}) };
        GameObject* npc = s.CreateGameObject("Wolf");
        auto* ai = npc->AddComponent<NPCController>(); ai->targetName = "nobody";
        HealthStat* h = ai->Health();
        s.Start();
        h->godMode = true;
        CHECK(!DealDamage(npc, 10.0f));
        CHECK_NEAR(h->health, 30.0f, 1e-6f);
        h->godMode = false;
        CHECK(DealDamage(npc, 10.0f));
        CHECK_NEAR(h->health, 20.0f, 1e-6f);
        CHECK(!ai->IsDead());
        CHECK(DealDamage(npc, 50.0f));
        CHECK(ai->IsDead());
        s.Update(0.016f);
        CHECK_NEAR(ActionList::Vars()["npcdied"], 1.0f, 1e-6f);
        CHECK(ActionList::Vars().count("playerdied") == 0 || ActionList::Vars()["playerdied"] == 0.0f);   // not the player's `died`
        CHECK(s.Find("Wolf") == nullptr);                                        // removed, as before
    }

    // ---- 5. An NPC's or an enemy's health does not overwrite the player's {health} -------------------------------
    {
        ActionList::ResetVars();
        Scene s("vars");
        GameObject* p = s.CreateGameObject("Player");
        auto* ph = p->AddComponent<HealthStat>(); ph->publishBar = false;
        GameObject* e = s.CreateGameObject("Orc");
        e->AddComponent<NPCController>()->targetName = "nobody";
        s.Start();
        DealDamage(p, 10.0f);
        DealDamage(e, 5.0f);
        s.Update(0.016f);
        CHECK_NEAR(ActionList::Vars()["health"], 90.0f, 1e-3f);
    }

    // ---- 6. Old scenes: each owner's health moves into the Health Stat -----------------------------------------
    {   // Survival Stats with the mitigation fields
        Scene s("m1"); std::vector<std::string> w;
        CHECK(Load(s, V3(kSurvivalV3), &w));
        HealthStat* h = TheHealth(s);
        CHECK(h != nullptr);
        if (h) {
            CHECK_NEAR(h->maxHealth, 150.0f, 1e-4f); CHECK_NEAR(h->health, 150.0f, 1e-4f);
            CHECK_NEAR(h->armor, 3.0f, 1e-4f); CHECK_NEAR(h->regenDelay, 2.0f, 1e-4f);
            CHECK_NEAR(h->resistance, 0.25f, 1e-4f); CHECK_NEAR(h->invincibleTime, 0.5f, 1e-4f);
            CHECK(!h->godMode);
            CHECK(!h->publishPrefs && h->publishBar && !h->sendMessages);        // Survival Stats' switches
            CHECK(h->GetDeathAction() == HealthStat::DeathAction::Disable);
        }
        auto* sv = s.Find("O")->GetComponent<SurvivalStats>();
        CHECK(sv && std::fabs(sv->regenWhenFed - 1.0f) < 1e-4f && std::fabs(sv->maxHunger - 100.0f) < 1e-4f);
        CHECK(sv && std::fabs(sv->freezeDamage - 2.0f) < 1e-4f && !sv->publishPrefs && sv->publishBars && !sv->sendMessages);
        CHECK(s.Find("O")->GetComponents<HealthStat>().size() == 1);
        CHECK(!Any(w, "skipped"));
    }
    {   // Survival Stats from before the mitigation fields
        Scene s("m1b");
        CHECK(Load(s, V3(kSurvivalV3Plain)));
        HealthStat* h = TheHealth(s);
        CHECK(h && std::fabs(h->maxHealth - 120.0f) < 1e-4f && std::fabs(h->regenDelay - 5.0f) < 1e-4f && h->publishPrefs);
    }
    {   // RPG Stats saved hurt: max and current are kept (current is the health at Play start)
        Scene s("m2");
        CHECK(Load(s, V3(kStatsV3)));
        HealthStat* h = TheHealth(s);
        CHECK(h != nullptr);
        if (h) {
            CHECK_NEAR(h->maxHealth, 80.0f, 1e-4f); CHECK_NEAR(h->health, 40.0f, 1e-4f);
            CHECK_NEAR(h->startHealth, 40.0f, 1e-4f); CHECK_NEAR(h->minDamage, 1.0f, 1e-4f);
        }
        auto* st = s.Find("O")->GetComponent<Stats>();
        CHECK(st && std::fabs(st->mana - 50.0f) < 1e-4f && st->level == 1 && st->xpToNext == 100 && st->strength == 10 && st->defense == 5);
        s.Start();
        CHECK(h && std::fabs(h->health - 40.0f) < 1e-4f);                        // still hurt when Play starts
    }
    {   // NPC Controller: max health and invulnerable; the AI fields after them still line up
        Scene s("m3");
        CHECK(Load(s, V3(kNpcV3)));
        HealthStat* h = TheHealth(s);
        CHECK(h && std::fabs(h->maxHealth - 55.0f) < 1e-4f && h->godMode && h->destroyOnDeath && !h->publishPrefs);
        auto* ai = s.Find("O")->GetComponent<NPCController>();
        CHECK(ai && std::fabs(ai->runSpeed - 4.5f) < 1e-4f && std::fabs(ai->fieldOfView - 120.0f) < 1e-4f);
        CHECK(ai && std::fabs(ai->fleeHealthPct - 0.25f) < 1e-4f && std::fabs(ai->repathInterval - 0.5f) < 1e-4f);
    }
    {   // an NPC record from before it had health: the 30 HP default
        Scene s("m3b");
        CHECK(Load(s, V3(kNpcV3NoHealth)));
        HealthStat* h = TheHealth(s);
        CHECK(h && std::fabs(h->maxHealth - 30.0f) < 1e-4f && !h->godMode);
    }

    // ---- 7. Old scenes with two healths: the one HealthOf already used wins ---------------------------------------
    {   // Survival Stats + Health Stat: the Health Stat's record wins, defaults where it saved nothing
        Scene s("c1");
        CHECK(Load(s, V3(std::string(kSurvivalV3) + kHealthV3)));
        HealthStat* h = TheHealth(s);
        CHECK(s.Find("O")->GetComponents<HealthStat>().size() == 1);
        CHECK(h && std::fabs(h->maxHealth - 200.0f) < 1e-4f && std::fabs(h->armor - 7.0f) < 1e-4f);
        CHECK(h && std::fabs(h->regenDelay - 0.0f) < 1e-4f && h->playDeathAnim);   // not Survival Stats' 2 s / Disable
    }
    {   // Survival Stats + RPG Stats: Survival Stats wins (it came first for HealthOf), with a warning
        Scene s("c2"); std::vector<std::string> w;
        CHECK(Load(s, V3(std::string(kSurvivalV3) + kStatsV3), &w));
        HealthStat* h = TheHealth(s);
        CHECK(h && std::fabs(h->maxHealth - 150.0f) < 1e-4f && std::fabs(h->armor - 3.0f) < 1e-4f && std::fabs(h->startHealth) < 1e-6f);
        CHECK(Any(w, "Stats health was dropped") && Any(w, "Survival Stats health is kept"));
    }
    {   // RPG Stats + NPC Controller (an RPG enemy): RPG Stats wins over the NPC
        Scene s("c3"); std::vector<std::string> w;
        CHECK(Load(s, V3(std::string(kNpcV3) + kStatsV3), &w));
        HealthStat* h = TheHealth(s);
        CHECK(h && std::fabs(h->maxHealth - 80.0f) < 1e-4f && std::fabs(h->health - 40.0f) < 1e-4f);
        CHECK(h && std::fabs(h->minDamage - 1.0f) < 1e-4f && !h->godMode && !h->destroyOnDeath);
        CHECK(Any(w, "NPC Controller health was dropped"));
    }
    {   // all four: the Health Stat's record wins outright
        Scene s("c4");
        CHECK(Load(s, V3(std::string(kSurvivalV3) + kHealthV3 + kNpcV3 + kStatsV3)));
        HealthStat* h = TheHealth(s);
        CHECK(s.Find("O")->GetComponents<HealthStat>().size() == 1);
        CHECK(h && std::fabs(h->maxHealth - 200.0f) < 1e-4f && !h->godMode && std::fabs(h->startHealth) < 1e-6f);
        GameObject* o = s.Find("O");
        CHECK(o->GetComponent<SurvivalStats>() && o->GetComponent<Stats>() && o->GetComponent<NPCController>());
    }

    // ---- 8. Saving writes format 4 with health only in stat_health; it loads back the same -----------------------
    {
        Scene s("rt");
        CHECK(Load(s, V3(std::string(kSurvivalV3) + kNpcV3 + kStatsV3)));
        const std::string text = SceneSerializer::Serialize(s);
        CHECK(text.compare(0, 12, "okayscene 4\n") == 0);
        CHECK(text.find("  stat_health ") != std::string::npos && text.find("  stat_health ") == text.rfind("  stat_health "));   // one
        CHECK(Line(text, "survival").compare(0, 11, "survival 1 ") == 0);      // starts with regenWhenFed now
        CHECK(Line(text, "stats").compare(0, 9, "stats 50 ") == 0);            // starts with mana now
        CHECK(!Has(Line(text, "npc"), "\"Player\" 55 "));                       // no max health after the target
        CHECK(text.find("  stat_health ") < text.find("  survival "));           // the health is read first
        Scene b("rt2"); std::vector<std::string> w;
        CHECK(Load(b, text, &w));
        CHECK(w.empty());
        CHECK(SceneSerializer::Serialize(b) == text);                            // stable
        HealthStat* h = TheHealth(b);
        CHECK(h && std::fabs(h->maxHealth - 150.0f) < 1e-4f && std::fabs(h->armor - 3.0f) < 1e-4f);
        auto* ai = b.Find("O")->GetComponent<NPCController>();
        CHECK(ai && std::fabs(ai->runSpeed - 4.5f) < 1e-4f && std::fabs(ai->repathInterval - 0.5f) < 1e-4f);
    }
    {   // a hurt RPG character's start health survives a save
        Scene s("rt3");
        CHECK(Load(s, V3(kStatsV3)));
        const std::string text = SceneSerializer::Serialize(s);
        Scene b("rt4");
        CHECK(Load(b, text));
        HealthStat* h = TheHealth(b);
        CHECK(h && std::fabs(h->startHealth - 40.0f) < 1e-4f && std::fabs(h->maxHealth - 80.0f) < 1e-4f);
    }
    {   // a new object (made in this build) round-trips with one Health Stat and keeps its values
        Scene a("new");
        GameObject* o = a.CreateGameObject("O");
        auto* sv = o->AddComponent<SurvivalStats>(); sv->regenWhenFed = 2.5f;
        HealthStat* h = o->GetComponent<HealthStat>(); h->maxHealth = 222.0f; h->godMode = true;
        const std::string text = SceneSerializer::Serialize(a);
        Scene b("new2");
        CHECK(Load(b, text));
        HealthStat* bh = TheHealth(b);
        CHECK(b.Find("O")->GetComponents<HealthStat>().size() == 1);
        CHECK(bh && std::fabs(bh->maxHealth - 222.0f) < 1e-4f && bh->godMode && std::fabs(bh->regenDelay - 5.0f) < 1e-4f);
        CHECK(std::fabs(b.Find("O")->GetComponent<SurvivalStats>()->regenWhenFed - 2.5f) < 1e-4f);
        CHECK(SceneSerializer::Serialize(b) == text);
    }

    // ---- 9. The migration chain reports the step; going through the engine gives format 4; a second run is a no-op
    {
        const std::string v3 = V3(std::string(kSurvivalV3) + kStatsV3);
        const auto t = SceneMigration::MigrateToCurrent(v3);
        CHECK(t.ok && t.needsEngine && t.toVersion == 3);                        // only the loader can move health
        const auto e = SceneMigration::MigrateThroughEngine(v3);
        CHECK(e.ok && e.complete && e.toVersion == 4 && e.changed);
        CHECK(Has(e.text, "stat_health 150 3 "));
        const auto again = SceneMigration::MigrateToCurrent(e.text);
        CHECK(again.ok && again.complete && !again.changed && again.log.empty());
        // a format-3 scene with none of these records needs no engine
        const auto plain = SceneMigration::MigrateToCurrent(V3("  tag \"x\"\n"));
        CHECK(plain.ok && plain.complete && !plain.needsEngine && plain.toVersion == 4);
    }

    // ---- 10. Scripts: hurt / heal / get_hp / get_max_hp / is_dead act on the one health ------------------------
    {
        Scene s("scripts");
        GameObject* hero = s.CreateGameObject("Hero");
        auto* st = s.CreateGameObject("Orc")->AddComponent<Stats>(); st->defense = 2;
        s.CreateGameObject("Tank")->AddComponent<SurvivalStats>()->publishPrefs = false;
        s.Find("Tank")->GetComponent<HealthStat>()->publishPrefs = false;
        s.CreateGameObject("Wolf")->AddComponent<NPCController>()->targetName = "nobody";
        auto* sc = hero->AddComponent<ScriptComponent>(); std::string err;
        const bool ok = sc->LoadSource(
            "var a = 0\nvar b = 0\nvar m = 0\nvar t = 0\nvar w = 0\nvar wd = 0\nvar od = 1\n"
            "func start() {\n"
            "  hurt(\"Orc\", 12)\n a = get_hp(\"Orc\")\n heal(\"Orc\", 4)\n b = get_hp(\"Orc\")\n m = get_max_hp(\"Orc\")\n"
            "  hurt(\"Tank\", 40, \"Fire\")\n t = get_hp(\"Tank\")\n"
            "  hurt(\"Wolf\", 25)\n w = get_hp(\"Wolf\")\n hurt(\"Wolf\", 25)\n wd = is_dead(\"Wolf\")\n od = is_dead(\"Orc\")\n}\n", &err);
        CHECK(ok);
        s.Start(); s.Update(1.0f / 60.0f);
        // a: 12 - 2 defense on the Health Stat; t: Survival Stats' Health Stat; w: the NPC's 30 HP
        const struct { const char* var; float want; } expect[] = {
            {"a", 90.0f}, {"b", 94.0f}, {"m", 100.0f}, {"t", 60.0f}, {"w", 5.0f}, {"wd", 1.0f}, {"od", 0.0f}};
        for (const auto& e : expect) {
            const float got = (ok && sc->VM()) ? sc->VM()->GetGlobal(e.var).AsFloat() : -1.0f;
            if (std::fabs(got - e.want) > 0.01f) std::cerr << "  script " << e.var << " = " << got << ", want " << e.want << "\n";
            CHECK(std::fabs(got - e.want) <= 0.01f);
        }
        CHECK_NEAR(st->Health()->health, 94.0f, 1e-3f);
    }

    // ---- 11. No-code buttons: Damage / Heal on a Survival Stats player reach the Health Stat once ----------------
    {
        Scene s("buttons");
        GameObject* p = s.CreateGameObject("P");
        p->AddComponent<SurvivalStats>();
        HealthStat* h = p->GetComponent<HealthStat>(); h->publishPrefs = false;
        s.Start();
        CHECK(InvokeNativeUIAction(p, "Damage", 10.0f));
        CHECK_NEAR(h->health, 90.0f, 1e-3f);                                     // one hit, not one per component
        CHECK(InvokeNativeUIAction(p, "Heal", 5.0f));
        CHECK_NEAR(h->health, 95.0f, 1e-3f);
        int damages = 0;
        for (const auto& n : NativeUIActionNames(p)) if (n == "Damage") ++damages;
        CHECK(damages == 1);                                                     // listed once
    }

    TEST_MAIN_RESULT();
}
