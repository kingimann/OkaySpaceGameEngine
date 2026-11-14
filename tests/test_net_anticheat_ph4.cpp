#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Net/DedicatedServer.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include "okay/Net/Packet.hpp"
#include "okay/Net/UdpSocket.hpp"

#include <chrono>
#include <fstream>
#include <sstream>
#include <cmath>
#include <string>
#include <thread>
#include <vector>

using namespace okay;

// Anti-cheat plan, phase 4: gameplay the server takes on trust today. Server-only
// SyncVars (4a), fire rate and ammo on Net Validators (4b), hit claims checked
// against the server's own view (4c), and anti-cheat kicks filed for review (4d).
namespace {
void Nap() { std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
bool Has(const std::string& s, const std::string& n) { return s.find(n) != std::string::npos; }
int Count(const std::vector<CheatReport>& v, CheatKind k, const std::string& detail = "") {
    int n = 0;
    for (const auto& r : v) n += r.kind == k && (detail.empty() || Has(r.detail, detail));
    return n;
}
} // namespace

int main() {
    RUN_SUITE("net_anticheat_ph4");
    auto sink = Log::sink;
    Log::sink = [](Log::Level, const std::string&) {};

    // ---- 4a. Server-only SyncVars ------------------------------------------------------
    {
        Scene ss("Server");
        auto* srv = ss.CreateGameObject("Net")->AddComponent<NetworkManager>();
        srv->anticheat.settings.enabled = true;
        srv->anticheat.settings.serverVars = {"score", "money"};
        std::vector<CheatReport> reports;
        srv->SetCheatHandler([&](const CheatReport& r) { reports.push_back(r); });
        CHECK(srv->StartServer(0));
        ss.Start();
        Scene cs("Client");
        auto* cli = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        CHECK(cli->StartClient("127.0.0.1", srv->ServerPort()));
        cs.Start();
        auto pump = [&](int n) { for (int i = 0; i < n; ++i) { ss.Update(0.02f); cs.Update(0.02f); Nap(); } };
        for (int i = 0; i < 300 && !cli->Joined(); ++i) pump(1);
        CHECK(cli->Joined());
        srv->SetVar("score", "10");
        srv->SetVar("money.gold", "5");
        pump(10);
        CHECK(cli->GetVar("score") == "10");
        cli->SetVar("score", "999");        // a prefix the game marked server-only
        cli->SetVar("money.gold", "1e9");
        cli->SetVar("srv.round", "7");      // the built-in server-only prefix
        cli->SetVar("hat", "red");          // a plain key stays the client's to write
        pump(20);
        CHECK(srv->GetVar("score") == "10" && srv->GetVar("money.gold") == "5");
        CHECK(srv->GetVar("srv.round").empty());
        CHECK(srv->GetVar("hat") == "red");
        CHECK(cli->GetVar("score") == "10");          // the sender's copy is put back
        CHECK(cli->GetVar("money.gold") == "5");
        CHECK(Count(reports, CheatKind::ForgedSyncVar) == 3);
        // The server itself writes them as always.
        srv->SetVar("srv.round", "2");
        pump(10);
        CHECK(cli->GetVar("srv.round") == "2");
        // With the anti-cheat off, the old rules: plain keys are anyone's.
        srv->anticheat.settings.enabled = false;
        cli->SetVar("score", "11");
        pump(10);
        CHECK(srv->GetVar("score") == "11");
        CHECK(srv->anticheat.IsServerVar("score") && srv->anticheat.IsServerVar("srv.x") && !srv->anticheat.IsServerVar("hat"));
        cli->Stop(); srv->Stop();
    }

    // ---- 4b. Net Validator cooldown and budget ------------------------------------------
    {
        Scene t("T");
        auto* nv = t.CreateGameObject("V")->AddComponent<NetValidator>();
        nv->rpc = "fire";
        nv->cooldown = 0.5f;
        CHECK(nv->LimitsRate());
        CHECK(nv->Allows(1, "fire", ""));
        CHECK(!nv->Allows(1, "fire", ""));             // too soon
        CHECK(nv->Allows(2, "fire", ""));              // per player
        nv->Update(0.3f);
        CHECK(!nv->Allows(1, "fire", ""));
        nv->Update(0.3f);
        CHECK(nv->Allows(1, "fire", ""));
        CHECK(nv->Allows(1, "other", ""));             // another RPC: not this rule's

        auto* ammo = t.CreateGameObject("A")->AddComponent<NetValidator>();
        ammo->rpc = "shoot"; ammo->budget = 3;           // a magazine, no refill
        CHECK(ammo->Budget(1) == 3.0f);
        int shots = 0;
        for (int i = 0; i < 10; ++i) shots += ammo->Allows(1, "shoot", "");
        CHECK(shots == 3 && ammo->Budget(1) == 0.0f);
        ammo->Update(30.0f);                            // a pause isn't a reload ...
        CHECK(!ammo->Allows(1, "shoot", ""));
        CHECK(ammo->TopUp(1, 2.0f) == 2.0f);            // ... a reload is
        CHECK(ammo->Allows(1, "shoot", "") && ammo->Allows(1, "shoot", "") && !ammo->Allows(1, "shoot", ""));
        CHECK(ammo->TopUp(1, 100.0f) == 3.0f);          // up to the full budget
        ammo->ForgetPeer(1);
        CHECK(ammo->Budget(1) == 3.0f);

        auto* regen = t.CreateGameObject("R")->AddComponent<NetValidator>();
        regen->rpc = "dash"; regen->budget = 2; regen->refillPerSecond = 1.0f;
        CHECK(regen->Allows(1, "dash", "") && regen->Allows(1, "dash", "") && !regen->Allows(1, "dash", ""));
        regen->Update(1.0f);
        CHECK(regen->Allows(1, "dash", "") && !regen->Allows(1, "dash", ""));
        CHECK(NetValidator::BudgetFor(t, "shoot") == ammo && NetValidator::BudgetFor(t, "fire") == nullptr);

        // OkayScript net_budget and the Server Budget node top it up.
        GameObject* s = t.CreateGameObject("S");
        s->AddComponent<ScriptComponent>("okayscript")->LoadSource(
            "var before = 0;\nvar after = 0;\nvar none = 0;\n"
            "function start() { before = net_budget(4, \"shoot\"); after = net_budget(4, \"shoot\", -2); none = net_budget(4, \"nope\"); }\n");
        auto* g = t.CreateGameObject("G")->AddComponent<VisualScriptComponent>();
        std::string err;
        CHECK(g->LoadFromText("node 1 OnStart\nnode 2 ServerBudget shoot\nnode 3 Const 5\nnode 4 Const 1\nnode 5 SetVar left\n"
                              "data 2 0 3 0\ndata 2 1 4 0\ndata 5 0 2 0\nexec 1 0 2\nexec 2 0 5\nentry OnStart 1\n", &err));
        if (!err.empty()) std::printf("  graph: %s\n", err.c_str());
        t.Start();
        t.Update(0.016f);
        auto* vm = s->GetComponent<ScriptComponent>()->VM();
        CHECK(vm && vm->GetGlobal("before").AsFloat() == 3.0f && vm->GetGlobal("after").AsFloat() == 1.0f);
        CHECK(vm && vm->GetGlobal("none").AsFloat() == -1.0f);
        CHECK(ammo->Budget(5) == 3.0f);                 // the node's +1 on a full budget stays full
        CHECK(g->GetVariable("left").AsFloat() == 3.0f);
    }

    // ---- 4b. Firing faster than the weapon, or past its budget, is refused --------------
    {
        Scene ss("Server");
        auto* srv = ss.CreateGameObject("Net")->AddComponent<NetworkManager>();
        srv->maxMessagesPerSecond = 0;
        auto* ac = ss.CreateGameObject("Rules")->AddComponent<AntiCheatConfig>();
        ac->settings.kickAfterViolations = 0;
        auto* gun = ss.CreateGameObject("Gun")->AddComponent<NetValidator>();
        gun->rpc = "fire"; gun->cooldown = 0.1f; gun->budget = 5;
        std::vector<CheatReport> reports;
        int fired = 0;
        CHECK(srv->StartServer(0));
        ss.Start();
        srv->SetCheatHandler([&](const CheatReport& r) { reports.push_back(r); });
        srv->OnRpc("fire", [&](std::uint32_t, const std::string&) { ++fired; });
        Scene cs("Client");
        auto* cli = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        CHECK(cli->StartClient("127.0.0.1", srv->ServerPort()));
        cs.Start();
        auto pump = [&](int n) { for (int i = 0; i < n; ++i) { ss.Update(0.02f); cs.Update(0.02f); Nap(); } };
        for (int i = 0; i < 300 && !cli->Joined(); ++i) pump(1);
        CHECK(cli->Joined());
        for (int i = 0; i < 5; ++i) cli->RpcReliable("fire", "x");   // 5 at once: the cooldown lets 1 through
        pump(20);
        CHECK(fired == 1);
        for (int i = 0; i < 10; ++i) { cli->RpcReliable("fire", "x"); pump(8); }   // spaced: the magazine (5) runs out
        CHECK(fired == 5);
        CHECK(Count(reports, CheatKind::RpcRejected, "fire") >= 4 + 6);
        CHECK(gun->Budget(cli->LocalId()) == 0.0f);
        gun->TopUp(cli->LocalId(), 5.0f);               // the game reloads it
        cli->RpcReliable("fire", "x");
        pump(10);
        CHECK(fired == 6);
        cli->Stop(); srv->Stop();
    }

    // ---- 4c. Hit claims ----------------------------------------------------------------
    {
        Scene ss("Server");
        auto* srv = ss.CreateGameObject("Net")->AddComponent<NetworkManager>();
        srv->maxMessagesPerSecond = 0;
        srv->maxMoveSpeed = 1000.0f;   // a move check, so client hit claims are judged at all
        auto* ac = ss.CreateGameObject("Rules")->AddComponent<AntiCheatConfig>();
        ac->settings.kickAfterViolations = 0;
        auto* rifle = ss.CreateGameObject("Rifle")->AddComponent<NetValidator>();
        rifle->rpc = "hit/rifle"; rifle->cooldown = 0.3f;
        ac->settings.weapons = {"nerfed", "sniper"};   // known without a validator of their own
        GameObject* referee = ss.CreateGameObject("Referee");
        referee->AddComponent<ScriptComponent>("okayscript")->LoadSource(
            "var seen = 0;\n"
            "function on_validate_hit(shooter, target, weapon, damage) {\n"
            "  seen = seen + 1;\n"
            "  if (weapon == \"nerfed\") { return false; }\n"
            "  if (weapon == \"sniper\") { return 40; }\n"
            "}\n");
        GameObject* dummy = ss.CreateGameObject("Dummy");
        dummy->transform->localPosition = Vec3(10.0f, 0.0f, 0.0f);
        dummy->AddComponent<NetworkSync>()->netId = "dummy";
        auto* hp = dummy->AddComponent<HealthStat>();
        std::vector<CheatReport> reports;
        CHECK(srv->StartServer(0));
        ss.Start();
        srv->SetCheatHandler([&](const CheatReport& r) { reports.push_back(r); });

        Scene cs("Client");
        auto* cli = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();   // its object, at 0,0,0, is the avatar
        GameObject* copy = cs.CreateGameObject("Dummy");
        copy->transform->localPosition = Vec3(10.0f, 0.0f, 0.0f);
        copy->AddComponent<NetworkSync>()->netId = "dummy";
        auto* copyHp = copy->AddComponent<HealthStat>();
        CHECK(cli->StartClient("127.0.0.1", srv->ServerPort()));
        cs.Start();
        auto pump = [&](int n) { for (int i = 0; i < n; ++i) { ss.Update(0.02f); cs.Update(0.02f); Nap(); } };
        for (int i = 0; i < 300 && !cli->Joined(); ++i) pump(1);
        CHECK(cli->Joined());
        pump(40);                                        // its position reaches the server; the cooldown clears
        const Vec3 at(0.0f, 0.5f, 0.0f), east(1.0f, 0.0f, 0.0f);
        auto shot = [&](const Vec3& o, const Vec3& d, const char* weapon = "rifle") { cli->ClaimHit("dummy", weapon, o, d); pump(25); };

        shot(at, east);
        CHECK(std::fabs(hp->health - 90.0f) < 0.01f);      // the server applied it ...
        CHECK(std::fabs(copyHp->health - 90.0f) < 0.01f);  // ... and the client got the result
        CHECK(srv->HitsApplied() == 1);

        shot(Vec3(0.0f, 0.5f, 30.0f), east);              // from where the shooter isn't
        CHECK(Count(reports, CheatKind::HitRejected, "where the shooter isn't") == 1);
        shot(at, Vec3(1.0f, 0.6f, 0.0f));                  // the target wasn't on that line
        CHECK(Count(reports, CheatKind::HitRejected, "missed") == 1);
        shot(at, Vec3(-1.0f, 0.0f, 0.0f));                 // the other way
        CHECK(Count(reports, CheatKind::HitRejected, "behind") == 1);
        srv->anticheat.settings.maxHitRange = 8.0f;
        shot(at, east);                                    // 10 away, range 8
        CHECK(Count(reports, CheatKind::HitRejected, "out of range") == 1);
        srv->anticheat.settings.maxHitRange = 0.0f;
        GameObject* wall = ss.CreateGameObject("Wall");
        wall->transform->localPosition = Vec3(5.0f, 0.0f, 0.0f);
        wall->AddComponent<BoxCollider3D>()->size = Vec3(1.0f, 4.0f, 4.0f);
        shot(at, east);                                    // through the wall
        CHECK(Count(reports, CheatKind::HitRejected, "through a wall") == 1);
        CHECK(std::fabs(hp->health - 90.0f) < 0.01f && std::fabs(copyHp->health - 90.0f) < 0.01f);
        ss.Destroy(wall);
        pump(2);
        // Fire rate: two at once, the rifle's cooldown lets one through.
        cli->ClaimHit("dummy", "rifle", at, east);
        cli->ClaimHit("dummy", "rifle", at, east);
        pump(25);
        CHECK(std::fabs(hp->health - 80.0f) < 0.01f);
        CHECK(Count(reports, CheatKind::HitRejected, "fire rate") == 1);
        // The game's rules: on_validate_hit refuses one weapon and sets another's damage.
        shot(at, east, "nerfed");
        CHECK(Count(reports, CheatKind::HitRejected, "refused by the game") == 1);
        shot(at, east, "sniper");
        CHECK(std::fabs(hp->health - 40.0f) < 0.01f && std::fabs(copyHp->health - 40.0f) < 0.01f);
        auto* vm = referee->GetComponent<ScriptComponent>()->VM();
        CHECK(vm && vm->GetGlobal("seen").AsFloat() == 4.0f);   // only claims that passed the server's checks
        // A hand-made "__hit" that doesn't parse is refused too.
        cli->SendReliable("__hit", "dummy|rifle|1|nope|1 0 0");
        pump(20);
        CHECK(Count(reports, CheatKind::HitRejected, "malformed") == 1);
        // The client can't fake the result either: "__dmg" from a client is host-only.
        cli->SendReliable("__dmg", "dummy|0|999|0");
        pump(20);
        CHECK(std::fabs(hp->health - 40.0f) < 0.01f);
        // The host's own shot is applied at once.
        srv->ClaimHit("dummy", "rifle", Vec3(0, 0, 0), east);
        pump(20);
        CHECK(std::fabs(hp->health - 30.0f) < 0.01f && std::fabs(copyHp->health - 30.0f) < 0.01f);
        CHECK(srv->HitsApplied() == 4);
        cli->Stop(); srv->Stop();
    }

    // ---- 4d. Anti-cheat kicks, filed for review ------------------------------------------
    {
        const std::string uid = "6f94da84-5a9b-48c6-a2d0-90059bd6bf79";
        std::vector<std::string> sent;
        KickReporter rep("Arena", [&](const std::string& path, const std::string& method, const std::string& body,
                                      const std::string&) { sent.push_back(method + " " + path + " " + body); });
        NetworkManager::AntiCheatKick k;
        k.peer = 3; k.name = "zed"; k.userId = uid;
        k.recent = {{3, CheatKind::ForgedTransform, "door"}, {3, CheatKind::ForgedTransform, "crate"}, {3, CheatKind::SpeedHack, "sp/3/1"}};
        CHECK(rep.File(k, 0.0));
        CHECK(sent.size() == 1);
        CHECK(Has(sent[0], "POST /rest/v1/ac_reports"));
        CHECK(Has(sent[0], "\"reported\":\"" + uid + "\"") && Has(sent[0], "\"kind\":\"server_kick\""));
        CHECK(Has(sent[0], "[server check]") && Has(sent[0], "forged transform, speed hack"));
        CHECK(Has(sent[0], "forged transform: door") && Has(sent[0], "\"game\":\"Arena\""));
        CHECK(!rep.File(k, 600.0));                    // the same account within the hour: once
        CHECK(rep.File(k, 3700.0));
        NetworkManager::AntiCheatKick anon = k; anon.userId.clear();
        CHECK(!rep.File(anon, 4000.0));                // nobody to name
        int filed = 0;
        for (int i = 0; i < 30; ++i) {
            NetworkManager::AntiCheatKick o = k;
            char id[40]; std::snprintf(id, sizeof id, "00000000-0000-4000-8000-%012d", i);
            o.userId = id;
            filed += rep.File(o, 5000.0 + i);
        }
        CHECK(filed == KickReporter::kPerHour - 1);    // 20 an hour in all (one already went in this hour)

        // The engine hands the kick over with the verified account and what was caught.
        Scene ss("Server");
        auto* srv = ss.CreateGameObject("Net")->AddComponent<NetworkManager>();
        srv->maxMessagesPerSecond = 0;
        srv->anticheat.settings.enabled = true;
        srv->anticheat.settings.kickAfterViolations = 3;
        srv->SetTokenVerifier([&](const std::string& t, std::string& u) { u = t == "good" ? uid : ""; return true; });
        GameObject* door = ss.CreateGameObject("Door");
        door->AddComponent<NetworkSync>()->netId = "door";
        std::vector<NetworkManager::AntiCheatKick> kicks;
        srv->SetAntiCheatKickHandler([&](const NetworkManager::AntiCheatKick& x) { kicks.push_back(x); });
        CHECK(srv->StartServer(0));
        ss.Start();
        Scene cs("Client");
        auto* cli = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        cli->SetAuthToken("good");
        cli->SetLocalName("zed");
        CHECK(cli->StartClient("127.0.0.1", srv->ServerPort()));
        cs.Start();
        auto pump = [&](int n) { for (int i = 0; i < n; ++i) { ss.Update(0.02f); cs.Update(0.02f); Nap(); } };
        for (int i = 0; i < 300 && !cli->Joined(); ++i) pump(1);
        CHECK(cli->Joined());
        for (int i = 0; i < 6 && !cli->WasKicked(); ++i) { cli->Send("__xform", "door|50 0 0 0 0 0 1"); pump(5); }
        for (int i = 0; i < 50 && !cli->WasKicked(); ++i) pump(1);
        CHECK(cli->WasKicked());
        CHECK(kicks.size() == 1);
        CHECK(!kicks.empty() && kicks[0].userId == uid && kicks[0].name == "zed");
        CHECK(!kicks.empty() && kicks[0].recent.size() == 3 && kicks[0].recent.back().kind == CheatKind::ForgedTransform);
        srv->Stop();
    }

    // ---- The round-trip pings never report a client when the clock isn't checked ---------
    {
        // With the anti-cheat on, the heartbeat pings every client once a second
        // (hit claims allow for its round trip). With maxClockDrift 0, a client
        // that never answers (an older game) is never reported for it.
        Scene ss("Server");
        auto* srv = ss.CreateGameObject("Net")->AddComponent<NetworkManager>();
        srv->anticheat.settings.enabled = true;
        srv->anticheat.settings.maxClockDrift = 0.0f;
        srv->anticheat.settings.kickAfterViolations = 1;
        std::vector<CheatReport> reports;
        srv->SetCheatHandler([&](const CheatReport& r) { reports.push_back(r); });
        CHECK(srv->StartServer(0));
        ss.Start();
        net::UdpSocket raw; raw.Open(); raw.Bind(0);
        const net::Endpoint to{0x7F000001u, srv->ServerPort()};
        auto join = [&] {   // a plain (older) Join; re-sent so it doesn't time out
            net::Packet j(1);
            for (int i = 0; i < 5; ++i) j.Write(std::string(i == 0 ? "old" : ""));
            raw.SendTo(to, j.Data(), j.Size());
        };
        int pings = 0;
        auto drain = [&] {
            std::uint8_t buf[2048]; net::Endpoint from; int n;
            while ((n = raw.RecvFrom(buf, sizeof buf, from)) > 0) pings += buf[0] == 26;   // ClockPing
        };
        for (int i = 0; i < 50 && srv->PlayerCount() < 2; ++i) { join(); ss.Update(0.02f); Nap(); }
        CHECK(srv->PlayerCount() == 2);
        for (int i = 0; i < 750; ++i) {                 // 15 s of server time, never answering
            if (i % 25 == 0) join();
            ss.Update(0.02f); drain(); Nap();
        }
        CHECK(pings >= 10);                              // it was asked ...
        CHECK(Count(reports, CheatKind::ClockDrift) == 0);   // ... and never reported
        CHECK(srv->PlayerCount() == 2);                  // nor kicked (kick after 1)
        srv->Stop();
    }

    // ---- Settings: .okayac, scenes (old ones still load), --report-kicks ------------------
    {
        AntiCheatSettings s; std::string err;
        CHECK(AntiCheatSettings::Parse("enabled true\nserverVar score\nserverVar money.\nmaxHitRange 50\nhitDamage 25\n"
                                       "hitOriginTolerance 1.5\nhitTolerance 0.5\nhitLineOfSight false\n", s, &err));
        CHECK(s.serverVars.size() == 2 && s.serverVars[1] == "money." && s.maxHitRange == 50.0f && s.hitDamage == 25.0f &&
              s.hitOriginTolerance == 1.5f && s.hitTolerance == 0.5f && !s.hitLineOfSight);
        AntiCheatSettings back;
        CHECK(AntiCheatSettings::Parse(s.ToText(), back) && back.serverVars == s.serverVars && back.maxHitRange == 50.0f &&
              !back.hitLineOfSight && back.hitTolerance == 0.5f);
        CHECK(!AntiCheatSettings::Parse("serverVar\n", s, &err));
        CHECK(!Has(AntiCheatSettings().ToText(), "hit") && !Has(AntiCheatSettings().ToText(), "serverVar"));

        Scene a("A");
        a.CreateGameObject("Net")->AddComponent<NetworkManager>();
        GameObject* r = a.CreateGameObject("R");
        auto* ac = r->AddComponent<AntiCheatConfig>();
        ac->settings.serverVars = {"score", "srv.money"};
        ac->settings.maxHitRange = 30.0f; ac->settings.hitLineOfSight = false;
        auto* nv = r->AddComponent<NetValidator>();
        nv->rpc = "hit/rifle"; nv->cooldown = 0.25f; nv->budget = 30; nv->refillPerSecond = 2;
        auto* plain = a.CreateGameObject("P")->AddComponent<NetValidator>();
        plain->rpc = "chat"; plain->maxPerSecond = 3;
        const std::string path = "/tmp/okay_ph4_scene.okayscene";
        CHECK(SceneSerializer::SaveToFile(a, path));
        Scene b("B");
        CHECK(SceneSerializer::LoadFromFile(b, path));
        auto* ac2 = b.Find("R") ? b.Find("R")->GetComponent<AntiCheatConfig>() : nullptr;
        auto* nv2 = b.Find("R") ? b.Find("R")->GetComponent<NetValidator>() : nullptr;
        auto* plain2 = b.Find("P") ? b.Find("P")->GetComponent<NetValidator>() : nullptr;
        CHECK(ac2 && ac2->settings.serverVars == ac->settings.serverVars);
        CHECK(ac2 && ac2->settings.maxHitRange == 30.0f && !ac2->settings.hitLineOfSight);
        CHECK(nv2 && nv2->cooldown == 0.25f && nv2->budget == 30.0f && nv2->refillPerSecond == 2.0f);
        CHECK(plain2 && plain2->maxPerSecond == 3.0f && plain2->cooldown == 0.0f && plain2->budget == 0.0f);
        // A validator without the new fields writes exactly the old line.
        std::ifstream in(path); std::stringstream ss; ss << in.rdbuf();
        CHECK(Has(ss.str(), "netvalidator \"chat\" 0 3 1\n"));
        std::remove(path.c_str());

        auto parse = [](std::vector<std::string> args, DedicatedServerOptions& o, std::string& e) {
            std::vector<char*> argv;
            for (auto& x : args) argv.push_back(x.data());
            return ParseDedicatedServerArgs((int)argv.size(), argv.data(), o, &e);
        };
        DedicatedServerOptions o; std::string e;
        CHECK(parse({"okayspace-server", "--server", "--report-kicks"}, o, e) && e.empty() && o.reportKicks);
        DedicatedServerOptions o2;
        CHECK(parse({"okayspace-server", "--server"}, o2, e) && !o2.reportKicks);
    }

    Log::sink = sink;
    TEST_MAIN_RESULT();
}
