#include "test_framework.hpp"
#include <Okay.hpp>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <thread>

using namespace okay;

// The anti-cheat as a game uses it: an AntiCheatConfig component (inline rules
// or a .okayac file) turns it on, On Cheat Detected runs in the server's visual
// scripts, NetValidator and On Validate Rpc / Reject Rpc refuse client RPCs,
// and all of it saves and loads with the scene.
int main() {
    RUN_SUITE("net_anticheat_components");
    auto sink = Log::sink;
    Log::sink = [](Log::Level, const std::string&) {};
    auto nap = [] { std::this_thread::sleep_for(std::chrono::milliseconds(2)); };

    // ---- Scene save / load keeps both components -------------------------------------
    {
        Scene a("A");
        GameObject* go = a.CreateGameObject("Rules");
        auto* ac = go->AddComponent<AntiCheatConfig>();
        ac->file = "rules/arena.okayac";
        ac->settings.maxSyncSpeed = 9.5f;
        ac->settings.maxSyncVarsPerSecond = 12;
        ac->settings.kickAfterViolations = 3;
        ac->settings.ownerOnlySyncVars = false;
        ac->settings.spawnAllowlist = {"prefabs/crate.okayprefab", "prefabs/ball.okayprefab"};
        auto* v1 = go->AddComponent<NetValidator>();
        v1->rpc = "fire"; v1->maxBytes = 16; v1->maxPerSecond = 4.0f;
        auto* v2 = go->AddComponent<NetValidator>();
        v2->rpc = "admin give"; v2->clientsMaySend = false;
        const std::string path = "/tmp/okay_ac_scene.okayscene";
        CHECK(SceneSerializer::SaveToFile(a, path));
        Scene b("B");
        CHECK(SceneSerializer::LoadFromFile(b, path));
        GameObject* r = b.Find("Rules");
        auto* bac = r ? r->GetComponent<AntiCheatConfig>() : nullptr;
        CHECK(bac != nullptr);
        if (bac) {
            CHECK(bac->file == "rules/arena.okayac");
            CHECK(bac->settings.enabled && bac->settings.maxSyncSpeed == 9.5f);
            CHECK(bac->settings.maxSyncVarsPerSecond == 12 && bac->settings.kickAfterViolations == 3);
            CHECK(!bac->settings.ownerOnlySyncVars);
            CHECK(bac->settings.spawnAllowlist.size() == 2 && bac->settings.spawnAllowlist[1] == "prefabs/ball.okayprefab");
        }
        auto vs = r ? r->GetComponents<NetValidator>() : std::vector<NetValidator*>{};
        CHECK(vs.size() == 2);
        if (vs.size() == 2) {
            CHECK(vs[0]->rpc == "fire" && vs[0]->maxBytes == 16 && vs[0]->maxPerSecond == 4.0f && vs[0]->clientsMaySend);
            CHECK(vs[1]->rpc == "admin give" && !vs[1]->clientsMaySend);
        }
        std::remove(path.c_str());
    }

    // ---- A server with the component: rules applied, cheats reach the graph, RPCs checked ----
    {
        const std::string rules = "/tmp/okay_ac_rules.okayac";
        { std::ofstream f(rules); f << "enabled true\nspawn /tmp/okay_ac_ok.okayprefab\nkickAfterViolations 0\n"; }

        Scene ss("Server");
        GameObject* netGo = ss.CreateGameObject("Net");
        auto* srv = netGo->AddComponent<NetworkManager>();
        srv->maxMessagesPerSecond = 0;
        GameObject* rulesGo = ss.CreateGameObject("Rules");
        auto* ac = rulesGo->AddComponent<AntiCheatConfig>();
        ac->file = rules;
        auto* nv = rulesGo->AddComponent<NetValidator>();
        nv->rpc = "fire"; nv->maxBytes = 8;
        auto* vsc = ss.CreateGameObject("Referee")->AddComponent<VisualScriptComponent>();
        // On Cheat Detected: kind <- Event Name, who <- Event Value, n += 1.
        // On Validate Rpc: if Event Name == "nuke" -> Reject Rpc.
        std::string err;
        CHECK(vsc->LoadFromText(
            "node 1 OnCheatDetected\nnode 2 SetVar kind\nnode 3 EventName\nnode 4 SetVar who\nnode 5 EventValue\n"
            "node 6 AddVar n\nnode 7 Const 1\n"
            "data 2 0 3 0\ndata 4 0 5 0\ndata 6 0 7 0\nexec 1 0 2\nexec 2 0 4\nexec 4 0 6\n"
            "node 10 OnValidateRpc\nnode 11 Branch\nnode 12 Equals\nnode 13 EventName\nnode 14 Const nuke\nnode 15 RejectRpc\n"
            "data 11 0 12 0\ndata 12 0 13 0\ndata 12 1 14 0\nexec 10 0 11\nexec 11 0 15\n"
            "entry OnCheatDetected 1\nentry OnValidateRpc 10\n", &err));
        if (!err.empty()) std::printf("  graph: %s\n", err.c_str());
        CHECK(srv->StartServer(0));
        ss.Start();
        CHECK(srv->anticheat.settings.enabled);                        // the file's rules are in force
        CHECK(ac->LoadError().empty());
        CHECK(srv->anticheat.settings.spawnAllowlist.size() == 1);

        int fired = 0, nuked = 0;
        srv->OnRpc("fire", [&](std::uint32_t, const std::string&) { ++fired; });
        srv->OnRpc("nuke", [&](std::uint32_t, const std::string&) { ++nuked; });

        Scene cs("Client");
        auto* cli = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        CHECK(cli->StartClient("127.0.0.1", srv->ServerPort()));
        cs.Start();
        auto pump = [&](int frames) { for (int i = 0; i < frames; ++i) { ss.Update(0.02f); cs.Update(0.02f); nap(); } };
        for (int i = 0; i < 300 && !cli->Joined(); ++i) pump(1);
        CHECK(cli->Joined());
        pump(10);

        cli->Rpc("fire", "short");           // passes the NetValidator
        cli->Rpc("fire", "far too long a payload");
        cli->Rpc("nuke", "");                // the graph rejects it
        pump(20);
        CHECK(fired == 1);
        CHECK(nuked == 0);
        CHECK(srv->anticheat.TotalReports() == 2);
        CHECK(vsc->GetVariable("n").AsFloat() == 2.0f);
        CHECK(vsc->GetVariable("kind").AsString() == "rpc rejected");
        CHECK(vsc->GetVariable("who").AsFloat() == (float)cli->LocalId());
        CHECK(vsc->GetVariable("cheat_detail").AsString() == "nuke");

        // A spawn that isn't on the file's allowlist reaches the graph too.
        cli->Spawn("/tmp/okay_ac_not_allowed.okayprefab", Vec3::Zero);
        pump(20);
        CHECK(vsc->GetVariable("kind").AsString() == "spawn not allowed");
        CHECK(vsc->GetVariable("n").AsFloat() == 3.0f);

        // A bad file: the component's own rules are used and the error is kept.
        { std::ofstream f(rules); f << "enabled maybe\n"; }
        ac->settings.maxSyncSpeed = 7.0f;
        CHECK(!ac->Apply());
        CHECK(ac->LoadError().find("line 1") != std::string::npos);
        CHECK(srv->anticheat.settings.enabled && srv->anticheat.settings.maxSyncSpeed == 7.0f);
        std::remove(rules.c_str());
    }

    // ---- Without the component nothing is on ------------------------------------------
    {
        Scene ss("Server");
        auto* srv = ss.CreateGameObject("Net")->AddComponent<NetworkManager>();
        CHECK(srv->StartServer(0));
        ss.Start();
        ss.Update(0.02f);
        CHECK(!srv->anticheat.settings.enabled);
    }

    Log::sink = sink;
    TEST_MAIN_RESULT();
}
