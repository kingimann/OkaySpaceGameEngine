#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/SurvivalComponents.hpp"
#include <chrono>
#include <cmath>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace okay;

// A client that joins while an object is dead (or hurt) must see it dead (or hurt), not alive
// (docs/multiplayer_audit.md round 4, item 3). A respawn that follows a death can never be applied
// before it (one ordered stream).

namespace {

struct Actor { GameObject* go = nullptr; HealthStat* hs = nullptr; };

struct Peer {
    Scene scene;
    NetworkManager* nm = nullptr;
    Actor hero, npc, crate;
    explicit Peer(const char* name, bool server) : scene(name) {
        scene.physicsEnabled = false;
        nm = scene.CreateGameObject("Net")->AddComponent<NetworkManager>();
        nm->interpolationRate = 0.0f;
        auto make = [&](Actor& a, const char* id, float x, bool respawn, bool destroy) {
            a.go = scene.CreateGameObject(id);
            a.go->transform->localPosition = Vec3(x, 0.0f, 0.0f);
            auto* ns = a.go->AddComponent<NetworkSync>();
            ns->netId = id; ns->authority = NetworkSync::Authority::Host;
            a.hs = a.go->AddComponent<HealthStat>();
            a.hs->maxHealth = a.hs->health = 100.0f;
            a.hs->respawn = respawn; a.hs->respawnDelay = server ? 1.0f : 0.1f;
            a.hs->destroyOnDeath = destroy;
            a.hs->playDeathAnim = false;
        };
        make(hero, "hero", 0.0f, true, false);
        make(npc, "npc", 5.0f, false, false);
        make(crate, "crate", 9.0f, false, true);
    }
};

struct Rig {
    std::unique_ptr<Peer> srv;
    std::vector<std::unique_ptr<Peer>> cli;
    Rig() {
        srv = std::make_unique<Peer>("Server", true);
        CHECK(srv->nm->StartServer(0));
        srv->scene.Start();
    }
    Peer& Add() {
        cli.push_back(std::make_unique<Peer>("Client", false));
        Peer& p = *cli.back();
        CHECK(p.nm->StartClient("127.0.0.1", srv->nm->ServerPort()));
        p.scene.Start();
        return p;
    }
    bool WaitJoined(Peer& p) { return Until([&] { return p.nm->Joined(); }, 600); }
    void Step(float dt = 0.02f) {
        srv->scene.Update(dt);
        for (auto& c : cli) c->scene.Update(dt);
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }
    bool Until(const std::function<bool()>& done, int steps = 300) {
        for (int i = 0; i < steps && !done(); ++i) Step();
        return done();
    }
    void Pump(int n) { for (int i = 0; i < n; ++i) Step(); }
};

} // namespace

int main() {
    RUN_SUITE("net_life_late");
    auto sink = Log::sink;
    Log::sink = [](Log::Level, const std::string&) {};

    // ---- Dead before the join: a script kill and a kill volume; hurt: damage ----
    {
        Rig w;
        w.srv->hero.hs->respawn = false;
        w.srv->hero.hs->KillFromAction(true, Vec3(3.0f, 0.0f, -4.0f));   // killed, with where the blow came from
        w.srv->npc.hs->Damage(60.0f);                                   // hurt, not dead
        CHECK(w.srv->hero.hs->IsDead() && !w.srv->npc.hs->IsDead());
        CHECK(w.srv->nm->DeathsSent() == 1);                             // sent to nobody (nobody was here)
        Peer& late = w.Add();
        CHECK(w.WaitJoined(late));
        CHECK(w.Until([&] { return late.hero.hs->IsDead(); }));
        CHECK(!late.npc.hs->IsDead());
        CHECK(w.Until([&] { return std::fabs(late.npc.hs->health - 40.0f) < 0.01f; }));
        Vec3 from;
        CHECK(late.hero.hs->LastHitFrom(from) && std::fabs(from.x - 3.0f) < 1e-3f && std::fabs(from.z + 4.0f) < 1e-3f);
        CHECK(!late.crate.hs->IsDead());                                  // what is fine stays fine
        CHECK(w.srv->nm->LateJoinLifeSent() == 2);
        // It stays dead: nothing on the joiner brings it back (it waits for the server's respawn).
        w.Pump(40);
        CHECK(late.hero.hs->IsDead());
    }

    // ---- A respawn before the join: the joiner sees it alive ----
    {
        Rig w;
        w.srv->hero.hs->KillFromAction(false, Vec3{});
        CHECK(w.srv->hero.hs->IsDead());
        w.srv->hero.hs->Revive();
        CHECK(!w.srv->hero.hs->IsDead());
        Peer& late = w.Add();
        CHECK(w.WaitJoined(late));
        w.Pump(40);
        CHECK(!late.hero.hs->IsDead());
        CHECK(w.srv->nm->LateJoinLifeSent() == 0);
    }

    // ---- Dead when it joins, respawns right after: the death is applied first, then the respawn ----
    {
        Rig w;
        w.srv->hero.hs->respawn = true;
        w.srv->hero.hs->respawnDelay = 0.5f;
        w.srv->hero.hs->KillFromAction(false, Vec3{});
        Peer& late = w.Add();
        late.hero.hs->respawn = false;
        CHECK(w.WaitJoined(late));
        CHECK(w.Until([&] { return late.hero.hs->IsDead(); }, 200) || !w.srv->hero.hs->IsDead());
        // The server respawns it (0.5 s): the copy must end alive, never stuck dead.
        CHECK(w.Until([&] { return !w.srv->hero.hs->IsDead(); }, 300));
        CHECK(w.Until([&] { return !late.hero.hs->IsDead() && late.hero.hs->health > 0.0f; }, 300));
        w.Pump(40);
        CHECK(!late.hero.hs->IsDead());
    }

    // ---- An object destroyed on death: the joiner's copy dies too ----
    {
        Rig w;
        Peer& early = w.Add();
        CHECK(w.WaitJoined(early));
        w.srv->crate.hs->KillFromAction(false, Vec3{});
        // The copy is dead or gone (it destroys itself on death), looked up by name each time.
        auto deadOrGone = [](Peer& p) { GameObject* g = p.scene.Find("crate"); return !g || g->IsDestroyed() || (g->GetComponent<HealthStat>() && g->GetComponent<HealthStat>()->IsDead()); };
        CHECK(w.Until([&] { return deadOrGone(early); }));
        w.Pump(60);
        Peer& late = w.Add();
        CHECK(w.WaitJoined(late));
        CHECK(w.Until([&] { return deadOrGone(late); }));
        CHECK(deadOrGone(early));
    }

    // ---- A death a hit caused is remembered for a joiner too ----
    {
        Rig w;
        w.srv->npc.hs->maxHealth = w.srv->npc.hs->health = 5.0f;
        w.srv->npc.hs->Damage(10.0f);                // the host's own hit path
        CHECK(w.srv->npc.hs->IsDead());
        Peer& late = w.Add();
        late.npc.hs->maxHealth = late.npc.hs->health = 5.0f;
        CHECK(w.WaitJoined(late));
        CHECK(w.Until([&] { return late.npc.hs->IsDead(); }));
    }

    // ---- A dropped client that resumes is told the truth as well ----
    {
        Rig w;
        Peer& a = w.Add();
        a.nm->autoReconnect = true;
        a.nm->reconnectDelaySeconds = 0.2f;
        CHECK(w.WaitJoined(a));
        w.Pump(20);
        const std::uint32_t id = a.nm->LocalId();
        a.nm->Stop();                                  // gone for a moment
        w.srv->npc.hs->KillFromAction(false, Vec3{});  // dies while it is away
        w.Pump(5);
        CHECK(a.nm->StartClient("127.0.0.1", w.srv->nm->ServerPort()));
        CHECK(w.WaitJoined(a));
        CHECK(w.Until([&] { return a.npc.hs->IsDead(); }));
        (void)id;
    }

    // ---- Hostile: a client cannot send life events; the host's bad ones change nothing ----
    {
        Rig w;
        Peer& a = w.Add();
        CHECK(w.WaitJoined(a));
        w.Pump(10);
        a.nm->SendOrdered("__life", "Dhero");
        a.nm->SendOrdered("__life", "D0|0|0|0|hero");
        a.nm->Send("__life", "D0|0|0|0|hero");
        a.nm->SendReliable("__life", "D0|0|0|0|hero");
        w.Pump(40);
        CHECK(!w.srv->hero.hs->IsDead() && !a.hero.hs->IsDead());
        const std::uint32_t id = a.nm->LocalId();
        for (const char* bad : {"", "D", "X0|0|0|0|hero", "D0|nan|0|0|hero", "D9|0|0|0|hero", "Rnan|0|0|0|hero", "R|||", "Hhero|nan|0|0", "H|1|2|3",
                                "Dnobody", "D0|0|0|0|nobody", "R1|1e39|0|0|hero", "\x01\x02"})
            w.srv->nm->SendOrderedTo(id, "__life", bad);
        w.Pump(40);
        CHECK(!a.hero.hs->IsDead());
        CHECK(std::isfinite(a.hero.hs->health));
        CHECK(std::isfinite(a.hero.go->transform->Position().x));
        // A good one still works afterwards.
        w.srv->nm->SendOrderedTo(id, "__life", "D0|0|0|0|hero");
        CHECK(w.Until([&] { return a.hero.hs->IsDead(); }));
    }

    Log::sink = sink;
    TEST_MAIN_RESULT();
}
