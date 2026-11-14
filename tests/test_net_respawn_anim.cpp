#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Animation/HumanoidLibrary.hpp"
#include "okay/Animation/HumanoidActions.hpp"
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include <chrono>
#include <cmath>
#include <string>
#include <thread>
#include <vector>

using namespace okay;

// Respawn replication, and dive / hit / vault / emote animations on other
// players (docs/multiplayer_audit.md, docs/unfinished_engine.md section 11).

namespace {
struct World {
    Scene ss{"Server"}, cs{"Client"};
    NetworkManager *srv = nullptr, *cli = nullptr;
    GameObject *hero = nullptr, *proxy = nullptr;
    Character *sChar = nullptr, *cChar = nullptr;
    HealthStat *sHs = nullptr, *cHs = nullptr;
    World() {
        ss.physicsEnabled = false; cs.physicsEnabled = false;
        srv = ss.CreateGameObject("Net")->AddComponent<NetworkManager>();
        srv->interpolationRate = 0.0f;
        CHECK(srv->StartServer(0));
        hero = ss.CreateGameObject("Hero");
        sChar = hero->AddComponent<Character>(); sChar->Apply();
        sHs = hero->AddComponent<HealthStat>();
        auto* ns = hero->AddComponent<NetworkSync>();
        ns->netId = "hero"; ns->authority = NetworkSync::Authority::Host;
        if (AnimStateMachine* sm = HumanoidLibrary::EnsureAnimator(hero)) HumanoidActions::MergeMissing(*sm, sChar, true);
        ss.Start();
        cli = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        cli->interpolationRate = 0.0f;
        CHECK(cli->StartClient("127.0.0.1", srv->ServerPort()));
        proxy = cs.CreateGameObject("Hero");
        cChar = proxy->AddComponent<Character>(); cChar->Apply();
        cHs = proxy->AddComponent<HealthStat>();
        auto* cns = proxy->AddComponent<NetworkSync>();
        cns->netId = "hero"; cns->authority = NetworkSync::Authority::Host;
        cs.Start();
        for (int i = 0; i < 300 && !cli->Joined(); ++i) Pump(1);
        CHECK(cli->Joined());
        Pump(15);
    }
    void Pump(int n, float dt = 0.02f) {
        for (int i = 0; i < n; ++i) {
            Time::Step(dt);
            ss.Update(dt); cs.Update(dt);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    // The state names any animator on `g` is in right now.
    static bool Playing(GameObject* g, const std::string& prefix) {
        for (AnimStateMachine* m : g->GetComponents<AnimStateMachine>())
            if (m->Current().rfind(prefix, 0) == 0) return true;
        return false;
    }
    // Pumps up to `n` frames and reports whether the proxy was seen in `prefix` at any frame.
    bool Sees(const std::string& prefix, int n = 120) {
        for (int i = 0; i < n; ++i) { Pump(1); if (Playing(proxy, prefix)) return true; }
        return false;
    }
};
} // namespace

int main() {
    RUN_SUITE("net_respawn_anim");
    auto sink = Log::sink;
    Log::sink = [](Log::Level, const std::string&) {};

    // ---- Respawn: the server's respawn reaches the other player's copy ----
    {
        World w;
        w.sHs->respawn = true; w.sHs->respawnDelay = 1.0f;
        w.cHs->respawn = true; w.cHs->respawnDelay = 0.2f;   // the same prefab, run on the client too
        w.sHs->maxHealth = w.cHs->maxHealth = 100.0f;
        w.sHs->health = w.cHs->health = 100.0f;
        w.hero->transform->SetPosition(Vec3(20.0f, 0.0f, 20.0f));
        w.Pump(10);
        // The hit that kills it reaches the client the way hits do.
        w.sHs->Damage(500.0f);
        w.cHs->SetHealthFromServer(0.0f);
        CHECK(w.sHs->IsDead() && w.cHs->IsDead());
        // The client's own respawn timer (0.2 s) must not bring it back before the server does.
        w.Pump(30);                                     // 0.6 s
        CHECK(w.sHs->IsDead());
        CHECK(w.cHs->IsDead());
        // The server respawns it (1 s): the client's copy follows, healthy and at the same spot.
        bool back = false;
        for (int i = 0; i < 150 && !back; ++i) { w.Pump(1); back = !w.cHs->IsDead(); }
        CHECK(!w.sHs->IsDead());
        CHECK(back);
        CHECK(std::fabs(w.cHs->health - 100.0f) < 0.01f);
        w.Pump(5);
        const Vec3 sp = w.hero->transform->Position(), cp = w.proxy->transform->Position();
        CHECK(std::fabs(sp.x) < 0.01f && std::fabs(sp.z) < 0.01f);   // the server moved it back to its start (it died at 20, 20)
        CHECK(std::fabs(cp.x - sp.x) < 0.05f && std::fabs(cp.z - sp.z) < 0.05f);   // and the copy snapped there, no glide
        // And it can die and come back again.
        w.sHs->Damage(500.0f);
        w.cHs->SetHealthFromServer(0.0f);
        CHECK(w.cHs->IsDead());
        bool again = false;
        for (int i = 0; i < 150 && !again; ++i) { w.Pump(1); again = !w.cHs->IsDead(); }
        CHECK(again);
    }

    // ---- A death no replicated hit caused (a script's die(), a kill volume) reaches the other player's copy ----
    {
        World w;
        w.sHs->respawn = true; w.sHs->respawnDelay = 1.0f;
        w.cHs->respawn = true; w.cHs->respawnDelay = 0.2f;   // the client's own timer must not bring it back
        w.sHs->maxHealth = w.cHs->maxHealth = 100.0f;
        w.sHs->health = w.cHs->health = 100.0f;
        w.Pump(10);
        CHECK(w.srv->DeathsSent() == 0);
        w.sHs->KillFromAction(false, Vec3{});                   // a script kills it
        CHECK(w.sHs->IsDead());
        bool died = false;
        for (int i = 0; i < 60 && !died; ++i) { w.Pump(1); died = w.cHs->IsDead(); }
        CHECK(died);                                             // no hit, no __dmg: only the death message says so
        CHECK(w.srv->DeathsSent() == 1);
        w.Pump(30);                                              // past the client's own 0.2 s timer
        CHECK(w.cHs->IsDead());
        bool back = false;
        for (int i = 0; i < 150 && !back; ++i) { w.Pump(1); back = !w.cHs->IsDead(); }
        CHECK(back && !w.sHs->IsDead());                         // the server's respawn brings it back
        // A kill volume: damage that is not a claimed hit, with the direction it came from.
        w.sHs->Damage(1e6f, Vec3(3.0f, 0.0f, -4.0f));
        died = false;
        for (int i = 0; i < 60 && !died; ++i) { w.Pump(1); died = w.cHs->IsDead(); }
        CHECK(died && w.srv->DeathsSent() == 2);
        Vec3 from;
        CHECK(w.cHs->LastHitFrom(from) && std::fabs(from.x - 3.0f) < 1e-3f && std::fabs(from.z + 4.0f) < 1e-3f);
    }

    // ---- A death message nobody should trust is ignored: from a client, bad numbers, unknown objects ----
    {
        World w;
        w.Pump(10);
        w.cli->Send("__death", "0|0|0|0|hero");                  // a client cannot kill another's copy
        w.cli->SendReliable("__death", "0|0|0|0|hero");
        w.Pump(30);
        CHECK(!w.sHs->IsDead() && !w.cHs->IsDead());
        for (const char* bad : {"garbage", "0|0|0|0|", "2|0|0|0|hero", "0|nan|0|0|hero", "1|1e9|0|0|hero", "0|0|0|0|nobody", "1|1|2|hero"}) {
            w.srv->SendReliable("__death", bad);
            w.Pump(8);
            CHECK(!w.cHs->IsDead());
        }
        w.srv->SendReliable("__death", "0|0|0|0|hero");           // and a well-formed one does kill it
        w.Pump(20);
        CHECK(w.cHs->IsDead());
    }

    // ---- A death a replicated hit caused travels with the hit, once (no __death on top) ----
    {
        Scene hs("Host"), cs("Shooter");
        hs.physicsEnabled = false; cs.physicsEnabled = false;
        auto* host = hs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        host->maxMessagesPerSecond = 0;
        host->anticheat.settings.hitTolerance = 0.5f;
        host->anticheat.settings.maxSyncSpeed = 10.0f;
        host->anticheat.settings.kickAfterViolations = 0;
        host->maxMoveSpeed = 1000.0f;   // a move check, so client hit claims are judged at all
        GameObject* dummy = hs.CreateGameObject("Dummy");
        dummy->transform->localPosition = Vec3(10.0f, 0.0f, -10.0f);
        dummy->AddComponent<NetworkSync>()->netId = "dummy";
        dummy->AddComponent<BoxCollider3D>()->size = Vec3(1.0f, 2.0f, 1.0f);
        auto* hp = dummy->AddComponent<HealthStat>();
        hp->health = hp->maxHealth = 5.0f;
        CHECK(host->StartServer(0));
        auto* cli = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        GameObject* copy = cs.CreateGameObject("Dummy");
        copy->transform->localPosition = Vec3(10.0f, 0.0f, -10.0f);
        copy->AddComponent<NetworkSync>()->netId = "dummy";
        auto* cp = copy->AddComponent<HealthStat>();
        cp->health = cp->maxHealth = 5.0f;
        CHECK(cli->StartClient("127.0.0.1", host->ServerPort()));
        hs.Start(); cs.Start();
        auto frame = [&] { Time::Step(0.02f); hs.Update(0.02f); cs.Update(0.02f); std::this_thread::sleep_for(std::chrono::milliseconds(2)); };
        for (int i = 0; i < 400 && !cli->Joined(); ++i) frame();
        CHECK(cli->Joined());
        for (int i = 0; i < 40; ++i) frame();
        const Vec3 at(0.0f, 0.0f, 0.0f), d = Vec3(10.0f, 0.0f, -10.0f) * (1.0f / Vec3(10.0f, 0.0f, -10.0f).Magnitude());
        cli->ClaimHit("dummy", "rifle", at, d);
        for (int i = 0; i < 150 && !cp->IsDead(); ++i) frame();
        CHECK(host->HitsApplied() == 1);
        CHECK(hp->IsDead() && cp->IsDead());                      // the hit's own message killed the copy
        CHECK(host->DeathsSent() == 0);                           // and no second message was sent for it
    }

    // ---- Revive() on the server (a manual respawn) is replicated too ----
    {
        World w;
        w.cHs->respawn = false; w.sHs->respawn = false;
        w.sHs->Damage(1e6f);
        w.cHs->SetHealthFromServer(0.0f);
        CHECK(w.cHs->IsDead());
        w.Pump(10);
        CHECK(w.cHs->IsDead());                         // nothing brings it back by itself
        w.sHs->Revive();
        bool back = false;
        for (int i = 0; i < 100 && !back; ++i) { w.Pump(1); back = !w.cHs->IsDead(); }
        CHECK(back);
        CHECK(w.cHs->health > 0.0f);
    }

    // ---- A client cannot forge a respawn; hostile payloads from the host change nothing ----
    {
        World w;
        w.sHs->Damage(1e6f);
        w.Pump(5);
        w.cli->Send("__respawn", "100|9|9|9|hero");     // a client claims the dead hero is alive
        w.Pump(40);
        CHECK(w.sHs->IsDead());
        CHECK(std::fabs(w.hero->transform->Position().x - 9.0f) > 0.1f);
        const float before = w.cHs->health;
        for (const char* bad : {"", "|", "nan|nan|nan|nan|hero", "inf|1|2|3|hero", "5|1|2|3|", "5|1|2|3|nobody",
                                "abc", "1|2|3", "-5|1|2|3|hero", "1e39|1e39|1e39|1e39|hero"})
            w.srv->SendReliable("__respawn", bad);
        w.Pump(40);
        CHECK(std::isfinite(w.cHs->health));
        CHECK(w.cHs->health <= before + 0.01f || w.cHs->health <= w.cHs->maxHealth + 0.01f);
        const Vec3 p = w.proxy->transform->Position();
        CHECK(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z));
    }

    // ---- Emote, hit, dive and vault play on the other player's copy ----
    {
        World w;
        w.Pump(10);
        CHECK(HumanoidActions::PlayEmote(w.hero, 2));
        CHECK(w.Sees("Emote_Shrug"));
        w.Pump(80);
        CHECK(HumanoidActions::PlayEmote(w.hero, "nod"));
        CHECK(w.Sees("Emote_Nod"));
        w.Pump(80);
        CHECK(HumanoidActions::HitReact(w.hero, 2, false));
        CHECK(w.Sees("Hit"));
        w.Pump(80);
        CHECK(HumanoidActions::HitReact(w.hero, 0, true));
        CHECK(w.Sees("HitHeavy"));
        w.Pump(120);
        CHECK(HumanoidActions::BeginDive(w.hero));
        CHECK(w.Sees("Dive"));
        w.Pump(120);
        CHECK(HumanoidActions::PlayTraverse(w.hero, HumanoidActions::TraverseClip::Vault));
        CHECK(w.Sees("Vault"));
        w.Pump(120);
        CHECK(HumanoidActions::PlayTraverse(w.hero, HumanoidActions::TraverseClip::MantleHigh));
        CHECK(w.Sees("MantleHigh"));
    }

    // ---- A one-shot plays once: the owner's repeated reports do not replay it ----
    {
        World w;
        w.Pump(10);
        int starts = 0; bool was = false;
        CHECK(HumanoidActions::PlayEmote(w.hero, 0));
        for (int i = 0; i < 300; ++i) {
            w.Pump(1);
            const bool now = World::Playing(w.proxy, "Emote_Nod");
            if (now && !was) ++starts;
            was = now;
        }
        CHECK(starts == 1);
    }

    // ---- Garbage in the event tail is dropped, not applied ----
    {
        World w;
        w.Pump(10);
        w.sChar->netEventSeq += 1; w.sChar->netEventId = 99; w.sChar->netEventParam = 3.0f;      // unknown id
        w.Pump(20);
        w.sChar->netEventSeq += 1; w.sChar->netEventId = 8; w.sChar->netEventParam = NAN;         // not a number
        w.Pump(20);
        w.sChar->netEventSeq += 1; w.sChar->netEventId = 8; w.sChar->netEventParam = 1e9f;        // out of range: clamped
        w.Pump(20);
        CHECK(!World::Playing(w.proxy, "Hit"));
        CHECK(!World::Playing(w.proxy, "Dive"));
    }

    // ---- Two one-shots inside one sync interval both play, in order (the older is not dropped) ----
    {
        World w;
        w.Pump(15);
        std::vector<std::string> seen;   // emote clips the proxy started, in order
        auto watch = [&](int frames) {
            std::string last;
            for (int i = 0; i < frames; ++i) {
                w.Pump(1);
                std::string now;
                for (const char* n : {"Emote_Nod", "Emote_Shake", "Emote_Shrug"})
                    if (World::Playing(w.proxy, n)) now = n;
                if (!now.empty() && now != last) seen.push_back(now);
                if (!now.empty() || last.empty()) last = now;
            }
        };
        // Started in the same frame: no sync message goes out between them.
        w.sChar->RecordNetEvent(8, 0.0f);   // nod
        w.sChar->RecordNetEvent(8, 2.0f);   // shrug
        watch(300);
        CHECK(seen.size() == 2 && seen[0] == "Emote_Nod" && seen[1] == "Emote_Shrug");
        // And it does not replay while the owner keeps reporting the same state.
        seen.clear();
        watch(150);
        CHECK(seen.empty());
    }

    // ---- The queue is bounded: past four in one interval the oldest are dropped, the rest play in order ----
    {
        World w;
        w.Pump(15);
        std::vector<std::string> seen;
        for (int i = 0; i < 6; ++i) w.sChar->RecordNetEvent(8, (float)(i % 3));   // nod, shake, shrug, nod, shake, shrug
        CHECK((int)w.sChar->netEventEarlier.size() == Character::kNetEventHistory - 1);
        std::string last;
        for (int i = 0; i < 600; ++i) {
            w.Pump(1);
            std::string now;
            for (const char* n : {"Emote_Nod", "Emote_Shake", "Emote_Shrug"})
                if (World::Playing(w.proxy, n)) now = n;
            if (!now.empty() && now != last) seen.push_back(now);
            if (!now.empty() || last.empty()) last = now;
        }
        const std::vector<std::string> want = {"Emote_Shrug", "Emote_Nod", "Emote_Shake", "Emote_Shrug"};
        CHECK(seen == want);
    }

    // ---- A single event still plays at once (no added delay when nothing is queued) ----
    {
        World w;
        w.Pump(15);
        w.sChar->RecordNetEvent(8, 0.0f);
        CHECK(w.Sees("Emote_Nod", 25));
    }

    // ---- The earlier-events group: round trip, limits and garbage ----
    {
        Character c;
        c.RecordNetEvent(8, 1.0f); c.RecordNetEvent(6, 5.0f); c.RecordNetEvent(5, 0.0f);
        CHECK(c.netEventSeq == 3 && c.netEventId == 5 && c.netEventEarlier.size() == 2);
        CHECK(c.netEventEarlier[0].seq == 1 && c.netEventEarlier[1].seq == 2 && c.netEventEarlier[1].id == 6);
        for (int i = 0; i < 10; ++i) c.RecordNetEvent(8, 0.0f);
        CHECK((int)c.netEventEarlier.size() == Character::kNetEventHistory - 1);
        CHECK(c.netEventEarlier.back().seq == c.netEventSeq - 1);
        AnimSyncView v;
        CHECK(ParseAnimSync("2 0 1.5 30 7 2 -10 4 3 9 8 2 2 7 6 5 8 8 1", v));
        CHECK(v.haveEvent && v.eventSeq == 9 && v.earlier.size() == 2);
        CHECK(v.earlier[0].seq == 7 && v.earlier[0].id == 6 && std::fabs(v.earlier[0].param - 5.0f) < 1e-4f);
        CHECK(v.earlier[1].seq == 8 && v.earlier[1].id == 8);
        AnimSyncView none;
        CHECK(ParseAnimSync("2 0 1.5 30 7 2 -10 4 3 9 8 2", none));          // an older sender: no group
        CHECK(none.haveEvent && none.earlier.empty());
        AnimSyncView cut;
        CHECK(ParseAnimSync("2 0 1.5 30 7 2 -10 4 3 9 8 2 2 7 6 5 8 8", cut));   // cut inside an entry: the group is dropped
        CHECK(cut.haveEvent && cut.earlier.empty());
        AnimSyncView big;
        CHECK(ParseAnimSync("2 0 1.5 30 7 2 -10 4 3 9 8 2 99 7 6 5", big));      // a count over the limit
        CHECK(big.haveEvent && big.earlier.empty());
        AnimSyncView nan;
        CHECK(ParseAnimSync("2 0 1.5 30 7 2 -10 4 3 9 8 2 1 7 6 nan", nan));
        CHECK(nan.haveEvent && nan.earlier.empty());
    }

    // ---- The event tail is optional: an older sender's message still parses ----
    {
        AnimSyncView v;
        CHECK(ParseAnimSync("2 0 1.5 30 7 2 -10 4 3", v));
        CHECK(v.haveParam && !v.haveEvent);
        CHECK(ParseAnimSync("2 0 1.5 30 7 2 -10 4 3 5 8 2", v));
        CHECK(v.haveEvent && v.eventSeq == 5 && v.eventId == 8 && std::fabs(v.eventParam - 2.0f) < 1e-4f);
        AnimSyncView cut;
        CHECK(ParseAnimSync("2 0 1.5 30 7 2 -10 4 3 5 8", cut));   // a cut tail
        CHECK(!cut.haveEvent);
    }

    Log::sink = sink;
    TEST_MAIN_RESULT();
}
