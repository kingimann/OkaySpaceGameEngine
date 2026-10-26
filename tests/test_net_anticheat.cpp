#include "test_framework.hpp"
#include <Okay.hpp>

#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

using namespace okay;

// Anti-cheat (opt-in, server side): a client can't move or despawn another
// peer's networked object, can't spawn prefabs off the game's allowlist or with
// another peer's SpawnOwned id, can't write another object's SyncVars or flood
// them, and a speed-hacking client is flagged and kicked. With it off the same
// messages behave exactly as before.
namespace {
struct Room {
    Scene ss{"Server"};
    Scene ca{"A"}, cb{"B"};
    NetworkManager* srv = nullptr;
    NetworkManager* a = nullptr;   // an honest client
    NetworkManager* b = nullptr;   // the cheater
    std::vector<CheatReport> reports;

    void Pump(int frames, float bDt = 0.02f) {
        for (int i = 0; i < frames; ++i) {
            ss.Update(0.02f); ca.Update(0.02f); cb.Update(bDt);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    bool Open(bool anticheatOn, const std::string& okPrefab) {
        srv = ss.CreateGameObject("Net")->AddComponent<NetworkManager>();
        srv->interpolationRate = 0.0f;   // remote objects snap to what's received
        srv->maxMessagesPerSecond = 0;
        if (anticheatOn) {
            AntiCheatSettings s;
            s.enabled = true;
            s.spawnAllowlist = {okPrefab};
            s.maxSyncSpeed = 5.0f;
            s.maxSyncVarsPerSecond = 20;
            srv->anticheat.settings = s;
        }
        srv->SetCheatHandler([this](const CheatReport& r) { reports.push_back(r); });
        if (!srv->StartServer(0)) return false;
        ss.Start();
        a = ca.CreateGameObject("Net")->AddComponent<NetworkManager>();
        b = cb.CreateGameObject("Net")->AddComponent<NetworkManager>();
        a->interpolationRate = b->interpolationRate = 0.0f;
        if (!a->StartClient("127.0.0.1", srv->ServerPort()) || !b->StartClient("127.0.0.1", srv->ServerPort())) return false;
        ca.Start(); cb.Start();
        for (int i = 0; i < 300 && !(a->Joined() && b->Joined()); ++i) Pump(1);
        Pump(10);
        return a->Joined() && b->Joined();
    }
    int Reports(std::uint32_t peer, CheatKind k) const {
        int n = 0;
        for (const auto& r : reports) n += r.peer == peer && r.kind == k;
        return n;
    }
};

Vec3 PosOf(Scene& s, const std::string& netId) {
    for (const auto& g : s.Objects())
        if (auto* ns = g->GetComponent<NetworkSync>(); ns && ns->netId == netId && g->transform)
            return g->transform->localPosition;
    return Vec3(-999.0f, -999.0f, -999.0f);
}
int CountNamed(Scene& s, const char* name) {
    int n = 0;
    for (const auto& g : s.Objects()) n += g->name == name;
    return n;
}
} // namespace

int main() {
    RUN_SUITE("net_anticheat");
    auto sink = Log::sink;
    Log::sink = [](Log::Level, const std::string&) {};

    // Two prefabs: one the game lets clients spawn, one it doesn't.
    const std::string ok = "/tmp/okay_ac_crate.okayprefab", bad = "/tmp/okay_ac_nuke.okayprefab";
    {
        Scene t("T");
        CHECK(SceneSerializer::SaveObjectToFile(*t.CreateGameObject("Crate"), ok));
        CHECK(SceneSerializer::SaveObjectToFile(*t.CreateGameObject("Nuke"), bad));
    }

    // ---- .okayac settings round-trip and reject junk ----------------------------------
    {
        AntiCheatSettings s;
        std::string err;
        CHECK(AntiCheatSettings::Parse("# game rules\nenabled true\nspawn prefabs\\crate.okayprefab\n"
                                       "maxSyncSpeed 12.5\nmaxSyncVarsPerSecond 30\nkickAfterViolations 4\n"
                                       "ownerOnlySyncVars false\n", s, &err));
        CHECK(s.enabled && s.spawnAllowlist.size() == 1 && s.spawnAllowlist[0] == "prefabs/crate.okayprefab");
        CHECK(s.maxSyncSpeed == 12.5f && s.maxSyncVarsPerSecond == 30 && s.kickAfterViolations == 4 && !s.ownerOnlySyncVars);
        AntiCheatSettings back;
        CHECK(AntiCheatSettings::Parse(s.ToText(), back) && back.ToText() == s.ToText());
        AntiCheatSettings keep = s;
        CHECK(!AntiCheatSettings::Parse("enabled true\nmaxSyncSpeed fast\n", keep, &err));
        CHECK(err.find("line 2") != std::string::npos);
        CHECK(keep.ToText() == s.ToText());                        // a bad file changes nothing
        CHECK(!AntiCheatSettings::Parse("maxSyncSpeed nan\n", keep) && !AntiCheatSettings::Parse("kickAfterViolations -1\n", keep));
        CHECK(!AntiCheatSettings::Parse("bogus 1\n", keep));
        CHECK(!AntiCheatSettings::Parse("maxSyncSpeed 5abc\n", keep));   // the whole value, not its start
        CHECK(!AntiCheatSettings().enabled);                       // off by default
    }

    // ---- SyncVar keys nested under a plain object id are that object's owner's ----
    {
        AntiCheat ac;
        ac.settings.enabled = true;
        ac.exists = [](const std::string& id) { return id == "car"; };
        ac.SetOwner("car", 1);
        CHECK(ac.ValidateSyncVar(1, "car.stats.hp"));
        CHECK(!ac.ValidateSyncVar(2, "car.hp"));
        CHECK(!ac.ValidateSyncVar(2, "car.stats.hp"));               // was allowed: only "car.stats" was checked
        CHECK(!ac.ValidateSyncVar(2, "car/stats/hp"));
        CHECK(ac.ValidateSyncVar(2, "round"));
        CHECK(ac.ValidateSyncVar(2, "scores.car"));                  // "scores" isn't an object
    }

    // ---- With anti-cheat on -------------------------------------------------------------
    {
        Room r;
        CHECK(r.Open(true, ok));
        const std::uint32_t A = r.a->LocalId(), B = r.b->LocalId();

        // A spawns an allowlisted crate it owns and moves it at a normal speed.
        GameObject* crate = r.a->SpawnOwned(ok, {1.0f, 0.0f, 0.0f});
        CHECK(crate != nullptr);
        const std::string aId = crate ? crate->GetComponent<NetworkSync>()->netId : "";
        r.Pump(30);
        CHECK(CountNamed(r.ss, "Crate") == 1 && CountNamed(r.cb, "Crate") == 1);   // allowed, replicated
        for (int i = 0; i < 50; ++i) { crate->transform->localPosition.x += 0.08f; r.Pump(1); }   // 4 u/s
        r.Pump(10);
        CHECK(r.srv->anticheat.Violations(A) == 0);
        CHECK(std::fabs(PosOf(r.ss, aId).x - crate->transform->localPosition.x) < 0.01f);

        // A forged __xform for A's crate from B is dropped: not applied, not relayed.
        // (A pauses its own broadcasts so they can't paper over a forged one.)
        crate->GetComponent<NetworkSync>()->SetOwned(false);
        r.Pump(5);
        const float x0 = PosOf(r.ss, aId).x;
        r.b->Send("__xform", aId + "|100 0 0 0 0 0 1");
        r.Pump(10);
        CHECK(std::fabs(PosOf(r.ss, aId).x - x0) < 0.01f);
        CHECK(PosOf(r.cb, aId).x < 50.0f);
        CHECK(r.Reports(B, CheatKind::ForgedTransform) == 1);
        CHECK(r.srv->anticheat.Violations(A) == 0);

        // ...and so is a forged despawn, and a SyncVar on A's crate.
        r.b->Send("__despawn", aId);
        r.b->SetVar(aId + ".hp", "0");
        r.a->SetVar(aId + ".hp", "100");
        r.b->SetVar("chat_topic", "anything");                  // a game-wide key stays writable
        r.Pump(20);
        CHECK(CountNamed(r.ss, "Crate") == 1);
        CHECK(r.Reports(B, CheatKind::ForgedDespawn) == 1);
        CHECK(r.srv->GetVar(aId + ".hp") == "100");
        CHECK(r.Reports(B, CheatKind::ForgedSyncVar) == 1);
        CHECK(r.srv->GetVar("chat_topic") == "anything");

        // A prefab that isn't on the allowlist never appears anywhere, and a
        // SpawnOwned id naming another peer is refused.
        r.b->Spawn(bad, {0.0f, 0.0f, 0.0f});
        r.b->Send("__spawn", ok + "|0|0|0|sp/" + std::to_string(A) + "/77");
        r.Pump(20);
        CHECK(CountNamed(r.ss, "Nuke") == 0 && CountNamed(r.ca, "Nuke") == 0);
        CHECK(r.Reports(B, CheatKind::SpawnNotAllowed) == 1);
        CHECK(r.Reports(B, CheatKind::ForgedSpawnId) == 1);
        CHECK(CountNamed(r.ss, "Crate") == 1);

        // SyncVar flood: past 20 a second the rest are refused.
        r.Pump(60);
        for (int i = 0; i < 40; ++i) r.b->SetVar("score_b", std::to_string(i));
        r.Pump(10);
        CHECK(r.Reports(B, CheatKind::SyncVarFlood) > 0);
        CHECK(r.srv->GetVar("score_b") != "39");

        // A speed-hacking client (its game runs 4x fast: 20 u/s against a 5 u/s
        // limit) is flagged, its crate stays put on the server, and it's kicked.
        r.srv->anticheat.settings.kickAfterViolations = 10;
        GameObject* mine = r.b->SpawnOwned(ok, {0.0f, 5.0f, 0.0f});
        CHECK(mine != nullptr);
        const std::string bId = mine ? mine->GetComponent<NetworkSync>()->netId : "";
        r.Pump(30);
        CHECK(std::fabs(PosOf(r.ss, bId).y - 5.0f) < 0.01f);    // its own crate arrived
        for (int i = 0; i < 300 && !r.b->WasKicked(); ++i) {
            mine->transform->localPosition.x += 5.0f * 0.08f;    // 5 u/s x a 4x clock
            r.Pump(1, 0.08f);
        }
        CHECK(r.Reports(B, CheatKind::SpeedHack) > 0);
        CHECK(r.b->WasKicked());
        CHECK(PosOf(r.ss, bId).x < 5.0f);                      // never got far on the server
        CHECK(r.srv->anticheat.Violations(A) == 0);           // the honest client is untouched
        CHECK(r.srv->anticheat.TotalReports() == (int)r.reports.size());
        // A was never reported for anything.
        bool aClean = true;
        for (const auto& rep : r.reports) aClean = aClean && rep.peer != A;
        CHECK(aClean);
    }

    // ---- Review follow-ups: the spawn parser, recovery, other paths, cleanup ----------
    {
        Room r;
        CHECK(r.Open(true, ok));
        const std::uint32_t A = r.a->LocalId(), B = r.b->LocalId();
        GameObject* crate = r.a->SpawnOwned(ok, {0.0f, 0.0f, 0.0f});
        const std::string aId = crate ? crate->GetComponent<NetworkSync>()->netId : "";
        GameObject* bCrate = r.b->SpawnOwned(ok, {0.0f, 9.0f, 0.0f});
        const std::string bId = bCrate ? bCrate->GetComponent<NetworkSync>()->netId : "";
        r.Pump(30);
        CHECK(r.srv->HasSync(aId) && r.srv->HasSync(bId));

        // A spawn whose id the receiver reads differently from a naive check:
        // odd separators, or a second id after a newline. Both name A's id.
        r.b->Send("__spawn", ok + "|0;0;0|" + aId);
        r.b->Send("__spawn", ok + "|0|0|0|" + aId + "\n|" + bId);
        r.Pump(20);
        CHECK(r.Reports(B, CheatKind::ForgedSpawnId) == 2);
        CHECK(CountNamed(r.ss, "Crate") == 2);                  // nothing spawned over A's crate
        for (int i = 0; i < 10; ++i) { crate->transform->localPosition.x += 0.05f; r.Pump(1); }
        r.Pump(5);
        CHECK(std::fabs(PosOf(r.ss, aId).x - crate->transform->localPosition.x) < 0.01f);   // still replicating

        // One overshoot (a lag burst or a respawn jump) is one violation, not a lockout.
        crate->transform->localPosition.x += 10.0f;
        r.Pump(10);                                              // one sync interval or two
        const int afterJump = r.srv->anticheat.Violations(A);
        CHECK(afterJump >= 1);
        for (int i = 0; i < 50; ++i) { crate->transform->localPosition.x += 0.08f; r.Pump(1); }   // 4 u/s
        r.Pump(5);
        CHECK(r.srv->anticheat.Violations(A) == afterJump);
        CHECK(std::fabs(PosOf(r.ss, aId).x - crate->transform->localPosition.x) < 0.01f);

        // Forged transforms over the reliable and direct paths are refused too.
        const int forged = r.Reports(B, CheatKind::ForgedTransform);
        r.b->SendReliable("__xform", aId + "|500 0 0 0 0 0 1");
        r.b->SendTo(0, "__xform", aId + "|600 0 0 0 0 0 1");
        r.Pump(20);
        CHECK(r.Reports(B, CheatKind::ForgedTransform) == forged + 2);
        CHECK(PosOf(r.ss, aId).x < 100.0f);

        // Ids the server doesn't have are dropped without claiming anything or
        // leaving state behind: an object named ahead of time, or a thousand
        // made-up SpawnOwned ids.
        const std::size_t tracked = r.srv->anticheat.TrackedObjects();
        r.b->Send("__xform", std::string("door|1 2 3 0 0 0 1"));
        for (int i = 0; i < 1000; ++i)
            r.b->Send("__xform", "sp/" + std::to_string(B) + "/" + std::to_string(1000 + i) + "|1 2 3 0 0 0 1");
        r.Pump(30);
        CHECK(r.srv->anticheat.OwnerOf("door") == 0);
        CHECK(r.srv->anticheat.TrackedObjects() == tracked);

        // A nested key on A's object is still A's.
        r.b->SetVar(aId + ".stats.hp", "0");
        r.Pump(10);
        CHECK(r.Reports(B, CheatKind::ForgedSyncVar) == 1);
        CHECK(r.srv->GetVar(aId + ".stats.hp") != "0");

        // Long uptime: the anti-cheat clock is a double, so after a week the
        // budget still refills and normal movement is still fine.
        r.srv->anticheat.Tick(7.0f * 86400.0f);
        const int beforeWeek = r.srv->anticheat.Violations(A);
        for (int i = 0; i < 30; ++i) { crate->transform->localPosition.x += 0.08f; r.Pump(1); }
        r.Pump(5);
        CHECK(r.srv->anticheat.Violations(A) == beforeWeek);

        // When B leaves, its objects' tracks go with it.
        bCrate->transform->localPosition.y += 0.1f;
        r.Pump(10);
        const std::size_t withB = r.srv->anticheat.TrackedObjects();
        r.b->Stop();
        r.Pump(30);
        CHECK(r.srv->anticheat.TrackedObjects() < withB);
        CHECK(r.srv->anticheat.Violations(B) == 0);
    }

    // ---- With anti-cheat off the same messages behave as before -----------------------
    {
        Room r;
        CHECK(r.Open(false, ok));
        GameObject* crate = r.a->SpawnOwned(ok, {1.0f, 0.0f, 0.0f});
        const std::string aId = crate ? crate->GetComponent<NetworkSync>()->netId : "";
        r.Pump(30);
        crate->GetComponent<NetworkSync>()->SetOwned(false);
        r.Pump(5);
        r.b->Send("__xform", aId + "|100 0 0 0 0 0 1");
        r.b->Spawn(bad, {0.0f, 0.0f, 0.0f});
        r.b->SetVar(aId + ".hp", "0");
        r.Pump(20);
        CHECK(std::fabs(PosOf(r.ss, aId).x - 100.0f) < 0.01f);   // applied, as today
        CHECK(CountNamed(r.ss, "Nuke") == 1 && CountNamed(r.ca, "Nuke") == 1);
        CHECK(r.srv->GetVar(aId + ".hp") == "0");
        CHECK(r.reports.empty() && r.srv->anticheat.TotalReports() == 0);
    }

    Log::sink = sink;
    TEST_MAIN_RESULT();
}
