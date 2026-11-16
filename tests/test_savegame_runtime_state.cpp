// Save games keep the runtime state of components whose progress is not part of the
// authored scene (issue #888, findings 59, 60 and 61):
//   59  Spawner progress (spawned count, waves done, timers, running),
//   60  HealthStat (health, dead, lives left, respawn point) and the survival meters
//       (hunger, thirst, stamina, oxygen, warmth, sleep energy, sanity, SurvivalStats),
//   61  a respawning Collectible that was picked up (hidden, respawn timer running).
// Each case: set the state, SaveGame::Save, change it, SaveGame::LoadNow, compare.
// A save written before these records existed must still load as it did.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Core/SaveData.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace okay;
namespace fs = std::filesystem;

namespace {

int CountTagged(Scene& s, const std::string& tag) {
    int n = 0;
    for (const auto& o : s.Objects()) if (o && o->tag == tag) ++n;
    return n;
}

void Run(Scene& s, float seconds, float dt = 0.05f) {
    for (float t = 0.0f; t < seconds; t += dt) { s.Update(dt); s.FlushDestroyed(); }
}

// Spawner with a template "Enemy": interval 0.1, startDelay 0, maxAlive 0 (Grok's repro).
Spawner* BuildSpawner(Scene& s, int total, int waves, int count) {
    GameObject* tmpl = s.CreateGameObject("Enemy");
    tmpl->transform->SetPosition(Vec3{0, 0, 0});
    GameObject* g = s.CreateGameObject("Spawner");
    auto* sp = g->AddComponent<Spawner>();
    sp->templateName = "Enemy";
    sp->interval = 0.1f;
    sp->startDelay = 0.0f;
    sp->maxAlive = 0;
    sp->totalToSpawn = total;
    sp->waves = waves;
    sp->count = count;
    s.Start();
    return sp;
}

// Read a slot, drop every state line whose kind is in `kinds`, and write it back with a
// fresh checksum, so it looks like a save made before those records existed.
void StripRecords(const std::string& slot, std::initializer_list<const char*> kinds) {
    const std::string path = SaveGame::SlotPath(slot);
    std::string text;
    { std::ifstream f(path, std::ios::binary); std::ostringstream ss; ss << f.rdbuf(); text = ss.str(); }
    std::istringstream in(text);
    std::string line, out;
    while (std::getline(in, line)) {
        if (line.rfind("checksum\t", 0) == 0) break;
        bool drop = false;
        for (const char* k : kinds) if (line.rfind(std::string(k) + "\t", 0) == 0) drop = true;
        if (!drop) out += line + "\n";
    }
    out += "checksum\t" + savefmt::Hex16(savefmt::Fnv1a(out.data(), out.size())) + "\n";
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << out;
}

bool SlotHas(const std::string& slot, const std::string& prefix) {
    std::ifstream f(SaveGame::SlotPath(slot), std::ios::binary);
    std::string line;
    while (std::getline(f, line)) if (line.rfind(prefix, 0) == 0) return true;
    return false;
}

} // namespace

int main() {
    RUN_SUITE("savegame_runtime_state");
    const fs::path dir = fs::temp_directory_path() / "okay_test_savegame_runtime_state";
    fs::remove_all(dir);
    fs::create_directories(dir);
    Save::SetBaseDir(dir.string());
    std::string err;

    // ---- 59: a finished spawner (totalToSpawn) stays finished after a load ----
    {
        Scene s("Arena");
        Spawner* sp = BuildSpawner(s, 3, 0, 1);
        Run(s, 1.0f);
        CHECK(sp->Spawned() == 3);
        CHECK(CountTagged(s, "spawn:Spawner") == 3);
        CHECK(SaveGame::Save(s, "sp_total", "", &err));
        CHECK(SaveGame::LoadNow(s, "sp_total", &err));
        Run(s, 2.0f);
        Spawner* after = s.Find("Spawner")->GetComponent<Spawner>();
        std::cout << "  59 total: after load + 2 s: " << CountTagged(s, "spawn:Spawner")
                  << " copies, Spawned() = " << after->Spawned() << "\n";
        CHECK(CountTagged(s, "spawn:Spawner") == 3);
        CHECK(after->Spawned() == 3);
    }
    // ---- 59: a finished wave run (waves = 1, count = 3) stays finished ----
    {
        Scene s("Arena");
        Spawner* sp = BuildSpawner(s, 0, 1, 3);
        Run(s, 1.0f);
        CHECK(sp->Spawned() == 3);
        CHECK(sp->WavesDone() == 1);
        CHECK(!sp->Running());
        CHECK(SaveGame::Save(s, "sp_waves", "", &err));
        CHECK(SaveGame::LoadNow(s, "sp_waves", &err));
        Run(s, 2.0f);
        Spawner* after = s.Find("Spawner")->GetComponent<Spawner>();
        std::cout << "  59 waves: after load + 2 s: " << CountTagged(s, "spawn:Spawner")
                  << " copies, WavesDone() = " << after->WavesDone() << "\n";
        CHECK(CountTagged(s, "spawn:Spawner") == 3);
        CHECK(after->WavesDone() == 1);
        CHECK(!after->Running());
    }
    // ---- 59: a spawner part-way through carries on from where it was ----
    {
        Scene s("Arena");
        Spawner* sp = BuildSpawner(s, 5, 0, 1);
        sp->interval = 1.0f;
        Run(s, 1.5f);   // spawns at t=0 and t=1
        const int before = sp->Spawned();
        CHECK(before == 2);
        CHECK(SaveGame::Save(s, "sp_mid", "", &err));
        CHECK(SaveGame::LoadNow(s, "sp_mid", &err));
        Spawner* after = s.Find("Spawner")->GetComponent<Spawner>();
        CHECK(after->Spawned() == before);
        Run(s, 6.0f);
        CHECK(after->Spawned() == 5);
        CHECK(CountTagged(s, "spawn:Spawner") == 5);
    }
    // ---- 59: a spawner stopped by a script stays stopped ----
    {
        Scene s("Arena");
        Spawner* sp = BuildSpawner(s, 0, 0, 1);
        sp->interval = 1.0f;
        Run(s, 0.2f);
        sp->StopWaves();
        const int before = sp->Spawned();
        CHECK(SaveGame::Save(s, "sp_stop", "", &err));
        CHECK(SaveGame::LoadNow(s, "sp_stop", &err));
        Run(s, 3.0f);
        Spawner* after = s.Find("Spawner")->GetComponent<Spawner>();
        CHECK(!after->Running());
        CHECK(after->Spawned() == before);
    }

    // ---- 60: health and the survival meters come back as they were ----
    {
        Scene s("Survival");
        GameObject* p = s.CreateGameObject("Player");
        auto* hp = p->AddComponent<HealthStat>();
        hp->maxHealth = 100.0f;
        auto* hu = p->AddComponent<HungerStat>();
        auto* th = p->AddComponent<ThirstStat>();
        auto* st = p->AddComponent<StaminaStat>();
        auto* ox = p->AddComponent<OxygenStat>();
        auto* te = p->AddComponent<TemperatureStat>();
        auto* sl = p->AddComponent<SleepStat>();
        auto* sa = p->AddComponent<SanityStat>();
        hu->drainPerSecond = 0.0f; th->drainPerSecond = 0.0f; st->regenPerSecond = 0.0f;
        ox->refillPerSecond = 0.0f; te->warmRegen = 0.0f; sl->drainPerSecond = 0.0f; sa->regenInLight = 0.0f;
        s.Start();
        hp->Damage(70.0f);
        hu->hunger = 12.0f; th->thirst = 23.0f; st->stamina = 34.0f; ox->oxygen = 45.0f;
        te->warmth = 56.0f; sl->energy = 67.0f; sa->sanity = 78.0f;
        CHECK_NEAR(hp->health, 30.0f, 1e-4);
        CHECK(SaveGame::Save(s, "stats", "", &err));
        hp->Heal(100.0f); hu->hunger = 100.0f;
        CHECK(SaveGame::LoadNow(s, "stats", &err));
        GameObject* q = s.Find("Player");
        std::cout << "  60: after load health " << q->GetComponent<HealthStat>()->health
                  << " hunger " << q->GetComponent<HungerStat>()->hunger << "\n";
        CHECK_NEAR(q->GetComponent<HealthStat>()->health, 30.0f, 1e-3);
        CHECK_NEAR(q->GetComponent<HungerStat>()->hunger, 12.0f, 1e-3);
        CHECK_NEAR(q->GetComponent<ThirstStat>()->thirst, 23.0f, 1e-3);
        CHECK_NEAR(q->GetComponent<StaminaStat>()->stamina, 34.0f, 1e-3);
        CHECK_NEAR(q->GetComponent<OxygenStat>()->oxygen, 45.0f, 1e-3);
        CHECK_NEAR(q->GetComponent<TemperatureStat>()->warmth, 56.0f, 1e-3);
        CHECK_NEAR(q->GetComponent<SleepStat>()->energy, 67.0f, 1e-3);
        CHECK_NEAR(q->GetComponent<SanityStat>()->sanity, 78.0f, 1e-3);
        // The shared variable the HUD reads is the loaded value, not the full one.
        CHECK_NEAR(ActionList::Vars()["health"], 30.0f, 1e-3);
        s.Update(0.0f);
        CHECK_NEAR(q->GetComponent<HealthStat>()->health, 30.0f, 1e-3);
    }
    // ---- 60: lives left and the respawn point are kept ----
    {
        Scene s("Survival");
        GameObject* p = s.CreateGameObject("Player");
        p->transform->SetPosition(Vec3{1.0f, 0.0f, 0.0f});
        auto* hp = p->AddComponent<HealthStat>();
        hp->respawn = true; hp->respawnDelay = 0.1f; hp->lives = 2; hp->playDeathAnim = false;
        s.Start();
        hp->Damage(1000.0f);   // one life used
        Run(s, 0.3f);
        CHECK(!hp->IsDead());
        p->transform->SetPosition(Vec3{9.0f, 0.0f, 0.0f});   // walked away before saving
        CHECK(SaveGame::Save(s, "lives", "", &err));
        CHECK(SaveGame::LoadNow(s, "lives", &err));
        GameObject* q = s.Find("Player");
        auto* h2 = q->GetComponent<HealthStat>();
        h2->Damage(1000.0f);   // the last life
        Run(s, 0.3f);
        CHECK(!h2->IsDead());
        std::cout << "  60: respawned at x = " << q->transform->Position().x << " (start was 1)\n";
        CHECK_NEAR(q->transform->Position().x, 1.0f, 1e-3);
        h2->Damage(1000.0f);   // no lives left: stays dead
        Run(s, 0.3f);
        CHECK(h2->IsDead());
    }
    // ---- 60: SurvivalStats (the all-in-one component) ----
    {
        Scene s("Survival");
        GameObject* p = s.CreateGameObject("Player");
        auto* ss = p->AddComponent<SurvivalStats>();
        s.Start();
        ss->health = 40.0f; ss->hunger = 11.0f; ss->thirst = 22.0f;
        ss->stamina = 33.0f; ss->oxygen = 44.0f; ss->warmth = 55.0f;
        CHECK(SaveGame::Save(s, "allin", "", &err));
        CHECK(SaveGame::LoadNow(s, "allin", &err));
        auto* s2 = s.Find("Player")->GetComponent<SurvivalStats>();
        CHECK_NEAR(s2->health, 40.0f, 1e-3);
        CHECK_NEAR(s2->hunger, 11.0f, 1e-3);
        CHECK_NEAR(s2->thirst, 22.0f, 1e-3);
        CHECK_NEAR(s2->stamina, 33.0f, 1e-3);
        CHECK_NEAR(s2->oxygen, 44.0f, 1e-3);
        CHECK_NEAR(s2->warmth, 55.0f, 1e-3);
    }

    // ---- 61: a picked-up respawning Collectible comes back after a load ----
    {
        Scene s("Coins");
        GameObject* coin = s.CreateGameObject("Coin");
        auto* sr = coin->AddComponent<SpriteRenderer>();
        auto* bc = coin->AddComponent<BoxCollider2D>();
        bc->isTrigger = true;
        auto* c = coin->AddComponent<Collectible>();
        c->respawn = true; c->respawnDelay = 1.0f;
        GameObject* player = s.CreateGameObject("Player");
        s.Start();
        c->Collect(player);
        CHECK(!sr->enabled);
        CHECK(!bc->enabled);
        CHECK(SaveGame::Save(s, "coin", "", &err));
        CHECK(SaveGame::LoadNow(s, "coin", &err));
        GameObject* c2 = s.Find("Coin");
        Run(s, 0.5f);
        CHECK(!c2->GetComponent<SpriteRenderer>()->enabled);   // still hidden: timer half done
        Run(s, 4.5f);
        std::cout << "  61: save+load, then 5 s: sprite=" << c2->GetComponent<SpriteRenderer>()->enabled
                  << " collider=" << c2->GetComponent<BoxCollider2D>()->enabled << "\n";
        CHECK(c2->GetComponent<SpriteRenderer>()->enabled);
        CHECK(c2->GetComponent<BoxCollider2D>()->enabled);
        // And it can be collected again.
        const float score = ActionList::Vars()["score"];
        c2->GetComponent<Collectible>()->Collect(s.Find("Player"));
        CHECK_NEAR(ActionList::Vars()["score"], score + 1.0f, 1e-4);
    }

    // ---- An old save, without the new records, loads as before ----
    {
        Scene s("Old");
        GameObject* p = s.CreateGameObject("Player");
        auto* hp = p->AddComponent<HealthStat>();
        GameObject* coin = s.CreateGameObject("Coin");
        auto* cc = coin->AddComponent<Collectible>();
        cc->respawn = true;
        GameObject* tmpl = s.CreateGameObject("Enemy");
        (void)tmpl;
        auto* sp = s.CreateGameObject("Spawner")->AddComponent<Spawner>();
        sp->templateName = "Enemy"; sp->interval = 0.1f; sp->startDelay = 0.0f; sp->maxAlive = 0; sp->totalToSpawn = 2;
        s.Start();
        Run(s, 0.5f);
        hp->Damage(40.0f);
        CHECK(SaveGame::Save(s, "old", "", &err));
        StripRecords("old", {"spawner", "health", "meter", "survival", "collect"});
        CHECK(!SlotHas("old", "health\t"));
        err.clear();
        CHECK(SaveGame::LoadNow(s, "old", &err));
        CHECK(err.empty());
        // The authored values, as before: full health, a fresh spawner.
        CHECK_NEAR(s.Find("Player")->GetComponent<HealthStat>()->health, 100.0f, 1e-3);
        CHECK(s.Find("Spawner")->GetComponent<Spawner>()->Spawned() == 0);
    }

    fs::remove_all(dir);
    TEST_MAIN_RESULT();
}
