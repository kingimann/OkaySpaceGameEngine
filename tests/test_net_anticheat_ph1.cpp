#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Net/Packet.hpp"
#include "okay/Net/UdpSocket.hpp"

#include <chrono>
#include <fstream>
#include <map>
#include <sstream>
#include "okay/Net/DedicatedServer.hpp"
#include <thread>

using namespace okay;

// Anti-cheat plan, phase 1: holes that apply even with anti-cheat off (a
// stranger setting SyncVars, a client starting the match, chat under another
// name), then the anti-cheat follow-ups (OkayScript handlers, uninstall,
// default RPC rate limit, the dedicated server's --anticheat).
namespace {
void Nap() { std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
} // namespace

int main() {
    RUN_SUITE("net_anticheat_ph1");
    auto sink = Log::sink;
    Log::sink = [](Log::Level, const std::string&) {};

    // ---- 1a. Holes that apply with anti-cheat OFF ---------------------------------------
    {
        Scene ss("Server");
        auto* srv = ss.CreateGameObject("Net")->AddComponent<NetworkManager>();
        srv->maxMessagesPerSecond = 0;
        srv->SetLocalName("host");
        CHECK(srv->StartServer(0));
        ss.Start();
        Scene ca("A"), cb("B");
        auto* a = ca.CreateGameObject("Net")->AddComponent<NetworkManager>();
        auto* b = cb.CreateGameObject("Net")->AddComponent<NetworkManager>();
        a->SetLocalName("maya"); b->SetLocalName("theo");
        CHECK(a->StartClient("127.0.0.1", srv->ServerPort()) && b->StartClient("127.0.0.1", srv->ServerPort()));
        ca.Start(); cb.Start();
        auto pump = [&](int n) { for (int i = 0; i < n; ++i) { ss.Update(0.02f); ca.Update(0.02f); cb.Update(0.02f); Nap(); } };
        for (int i = 0; i < 300 && !(a->Joined() && b->Joined()); ++i) pump(1);
        CHECK(a->Joined() && b->Joined());
        pump(10);

        // A stranger (never joined) sends a raw SyncVar packet.
        {
            net::UdpSocket s;
            CHECK(s.Open());
            net::Endpoint to; CHECK(net::UdpSocket::Resolve("127.0.0.1", srv->ServerPort(), to));
            net::Packet p(8 /* SyncVar */); p.Write(std::string("money")); p.Write(std::string("999999"));
            s.SendTo(to, p.Data(), p.Size());
            pump(20);
        }
        CHECK(srv->GetVar("money").empty());                       // was applied and fanned out to everyone
        CHECK(a->GetVar("money").empty());
        a->SetVar("money", "5");                                    // a joined client still can
        pump(20);
        CHECK(srv->GetVar("money") == "5" && b->GetVar("money") == "5");

        // A client tries to start the match.
        a->Send("__match_start", "");
        a->SendReliable("__match_start", "");
        a->SendTo(0, "__match_start", "");
        a->SendTo(b->LocalId(), "__match_start", "");
        a->Send("__made_up", "x");
        pump(30);
        CHECK(!srv->MatchStarted());
        CHECK(!b->MatchStarted());

        // A client posts chat as someone else.
        a->Send("__chat", "theo\nI give up, maya wins");
        pump(20);
        CHECK(!srv->ChatLog().empty() && srv->ChatLog().back().name == "maya");
        CHECK(!b->ChatLog().empty() && b->ChatLog().back().name == "maya");
        CHECK(!b->ChatLog().empty() && b->ChatLog().back().text == "I give up, maya wins");
        a->Chat("hi");                                              // normal chat still works
        pump(20);
        CHECK(b->ChatLog().back().name == "maya" && b->ChatLog().back().text == "hi");
        // Game channels and RPCs still go through.
        int heard = 0;
        srv->SetMessageHandler([&](const NetworkManager::NetMessage& m) { if (m.channel == "score") ++heard; });
        a->Send("score", "1");
        pump(20);
        CHECK(heard == 1);
    }

    // ---- 1b-1d. OkayScript handlers, uninstall, the default RPC rate ---------------------
    {
        Scene ss("Server");
        auto* srv = ss.CreateGameObject("Net")->AddComponent<NetworkManager>();
        srv->maxMessagesPerSecond = 0;
        GameObject* rulesGo = ss.CreateGameObject("Rules");
        auto* ac = rulesGo->AddComponent<AntiCheatConfig>();        // defaults: 30 RPCs/s, kick after 5
        auto* nv = rulesGo->AddComponent<NetValidator>();
        nv->rpc = "fire"; nv->maxPerSecond = 100.0f;                // its own rate: the default doesn't apply
        GameObject* referee = ss.CreateGameObject("Referee");
        referee->AddComponent<ScriptComponent>("okayscript")->LoadSource(
            "var cheats = 0;\n"
            "var lastKind = \"\";\n"
            "var lastPeer = -1;\n"
            "var checked = 0;\n"
            "function on_cheat_detected(kind, peer, detail) { cheats = cheats + 1; lastKind = kind; lastPeer = peer; }\n"
            "function on_validate_rpc(name, data, sender) {\n"
            "  checked = checked + 1;\n"
            "  if (name == \"nuke\") { return false; }\n"
            "  if (name == \"boom\") { no_such_function(); }\n"
            "}\n");
        CHECK(srv->StartServer(0));
        ss.Start();
        auto* vm = referee->GetComponent<ScriptComponent>()->VM();
        CHECK(vm != nullptr && srv->anticheat.settings.enabled && srv->anticheat.settings.maxRpcsPerSecond == 30);

        std::map<std::string, int> ran;
        for (const char* n : {"hello", "nuke", "boom", "spam", "fire"})
            srv->OnRpc(n, [&ran, n](std::uint32_t, const std::string&) { ++ran[n]; });

        Scene ca("A"), cb("B");
        auto* a = ca.CreateGameObject("Net")->AddComponent<NetworkManager>();
        auto* b = cb.CreateGameObject("Net")->AddComponent<NetworkManager>();
        CHECK(a->StartClient("127.0.0.1", srv->ServerPort()) && b->StartClient("127.0.0.1", srv->ServerPort()));
        ca.Start(); cb.Start();
        auto pump = [&](int n) { for (int i = 0; i < n; ++i) { ss.Update(0.02f); ca.Update(0.02f); cb.Update(0.02f); Nap(); } };
        for (int i = 0; i < 300 && !(a->Joined() && b->Joined()); ++i) pump(1);
        CHECK(a->Joined() && b->Joined());
        pump(10);

        // on_validate_rpc: false rejects, nothing allows, an error rejects (fail closed).
        a->RpcReliable("hello", "x");
        a->RpcReliable("nuke", "x");
        a->RpcReliable("boom", "1");
        a->RpcReliable("boom", "2");
        pump(40);
        CHECK(ran["hello"] == 1);
        CHECK(ran["nuke"] == 0);
        CHECK(ran["boom"] == 0);
        CHECK(vm->GetGlobal("checked").AsFloat() == 4.0f);
        // on_cheat_detected ran on the host for each refusal.
        CHECK(vm->GetGlobal("cheats").AsFloat() == 3.0f);
        CHECK(vm->GetGlobal("lastKind").AsString() == "rpc rejected");
        CHECK(vm->GetGlobal("lastPeer").AsFloat() == (float)a->LocalId());
        CHECK(srv->anticheat.Violations(a->LocalId()) == 3);

        // A NetValidator with its own rate takes the default's place for its RPC.
        pump(60);
        for (int i = 0; i < 60; ++i) a->RpcReliable("fire", "x");
        pump(40);
        CHECK(ran["fire"] == 60);
        CHECK(nv->TrackedPeers() == 1);

        // A peer leaving is forgotten by NetValidator too.
        Scene cc("C");
        auto* c = cc.CreateGameObject("Net")->AddComponent<NetworkManager>();
        CHECK(c->StartClient("127.0.0.1", srv->ServerPort()));
        cc.Start();
        for (int i = 0; i < 300 && !c->Joined(); ++i) { pump(1); cc.Update(0.02f); }
        c->RpcReliable("fire", "x");
        for (int i = 0; i < 20; ++i) { pump(1); cc.Update(0.02f); }
        CHECK(nv->TrackedPeers() == 2);
        c->Stop();
        for (int i = 0; i < 20; ++i) { pump(1); cc.Update(0.02f); }
        CHECK(nv->TrackedPeers() == 1);

        // The default rate: 100 calls in one go, at most 30 run; past the
        // violation limit the spammer is kicked.
        pump(60);   // a fresh second (after the kick, nobody new joins from this address for a while)
        for (int i = 0; i < 100; ++i) b->RpcReliable("spam", "x");
        for (int i = 0; i < 100 && !b->WasKicked(); ++i) pump(1);
        CHECK(ran["spam"] <= 30 && ran["spam"] >= 25);
        CHECK(b->WasKicked());
        CHECK(vm->GetGlobal("lastKind").AsString() == "rpc flood");
        b->Stop();

        // Deactivating the rules turns the checks off and nothing calls into them.
        const float checkedBefore = vm->GetGlobal("checked").AsFloat();
        rulesGo->active = false;
        pump(2);
        CHECK(!srv->anticheat.settings.enabled);
        CHECK(!srv->anticheat.validateRpc && !srv->CheatHandler());
        a->RpcReliable("nuke", "x");
        pump(30);
        CHECK(ran["nuke"] == 1);                                     // allowed again
        CHECK(vm->GetGlobal("checked").AsFloat() == checkedBefore);  // the script wasn't asked
        // Re-activating turns them back on.
        rulesGo->active = true;
        pump(2);
        CHECK(srv->anticheat.settings.enabled && srv->anticheat.validateRpc);
        a->RpcReliable("nuke", "x");
        pump(30);
        CHECK(ran["nuke"] == 1);
        // A second config takes over; removing the first doesn't uninstall the second.
        auto* ac2 = ss.CreateGameObject("MoreRules")->AddComponent<AntiCheatConfig>();
        ac2->settings.maxRpcsPerSecond = 7;
        pump(2);
        CHECK(srv->anticheat.settings.maxRpcsPerSecond == 7 && ac2->Installed() && !ac->Installed());
        rulesGo->RemoveComponent(ac);
        pump(2);
        CHECK(srv->anticheat.settings.enabled && srv->anticheat.settings.maxRpcsPerSecond == 7);
        // Removing the active one puts back what it replaced.
        ac2->gameObject->RemoveComponent(ac2);
        pump(2);
        // (what it replaced was the first config's, which is gone: checks go off)
        CHECK(!srv->anticheat.settings.enabled && !srv->anticheat.validateRpc && !srv->CheatHandler());
        CHECK(srv->anticheat.installer == 0);
    }

    // ---- Tearing a scene down in either order touches nothing that's gone ----------
    for (int order = 0; order < 2; ++order) {
        Scene t("T");
        if (order == 0) { t.CreateGameObject("Net")->AddComponent<NetworkManager>(); t.CreateGameObject("R")->AddComponent<AntiCheatConfig>(); }
        else { t.CreateGameObject("R")->AddComponent<AntiCheatConfig>(); t.CreateGameObject("Net")->AddComponent<NetworkManager>(); }
        t.Start(); t.Update(0.02f);
        CHECK(t.FindObjectOfType<NetworkManager>()->anticheat.settings.enabled);
    }   // (ASan reports any use of a freed manager / component here)

    // ---- Scenes: the RPC limits save and load; older scenes keep the defaults ----------
    {
        Scene a("A");
        auto* ac = a.CreateGameObject("R")->AddComponent<AntiCheatConfig>();
        ac->settings.maxRpcsPerSecond = 17; ac->settings.maxRpcBytes = 512;
        const std::string path = "/tmp/okay_ph1_scene.okayscene";
        CHECK(SceneSerializer::SaveToFile(a, path));
        Scene b("B");
        CHECK(SceneSerializer::LoadFromFile(b, path));
        auto* back = b.Find("R") ? b.Find("R")->GetComponent<AntiCheatConfig>() : nullptr;
        CHECK(back && back->settings.maxRpcsPerSecond == 17 && back->settings.maxRpcBytes == 512);
        // The same scene as an older build wrote it: no RPC fields on the line.
        std::ifstream in(path); std::stringstream ss; ss << in.rdbuf();
        std::string text = ss.str();
        const std::size_t at = text.find(" 17 512\n");
        CHECK(at != std::string::npos);
        if (at != std::string::npos) text.erase(at, 7);
        { std::ofstream o(path); o << text; }
        Scene c("C");
        CHECK(SceneSerializer::LoadFromFile(c, path));
        auto* old = c.Find("R") ? c.Find("R")->GetComponent<AntiCheatConfig>() : nullptr;
        CHECK(old && old->settings.maxRpcsPerSecond == 30 && old->settings.maxRpcBytes == 0);
        std::remove(path.c_str());
        // .okayac
        AntiCheatSettings st; std::string err;
        CHECK(AntiCheatSettings::Parse("maxRpcsPerSecond 12\nmaxRpcBytes 99\n", st, &err) && st.maxRpcsPerSecond == 12 && st.maxRpcBytes == 99);
        CHECK(!AntiCheatSettings::Parse("maxRpcsPerSecond lots\n", st, &err));
        CHECK(AntiCheatSettings().maxRpcsPerSecond == 0);            // the raw struct: off, as before
    }

    // ---- 1e. The dedicated server takes an .okayac file --------------------------------
    {
        const std::string good = "/tmp/okay_ph1_good.okayac", bad = "/tmp/okay_ph1_bad.okayac", cfg = "/tmp/okay_ph1_server.cfg";
        { std::ofstream(good) << "enabled true\nmaxRpcsPerSecond 5\n"; }
        { std::ofstream(bad) << "enabled perhaps\n"; }
        auto parse = [](std::vector<std::string> args, DedicatedServerOptions& o, std::string& err) {
            std::vector<char*> argv; std::string prog = "okayspace-server";
            argv.push_back(prog.data());
            for (auto& x : args) argv.push_back(x.data());
            err.clear();
            ParseDedicatedServerArgs((int)argv.size(), argv.data(), o, &err);
            return err.empty();
        };
        DedicatedServerOptions o; std::string err;
        CHECK(parse({"--server", "--anticheat", good}, o, err));
        CHECK(o.anticheatFile == good && o.anticheat.enabled && o.anticheat.maxRpcsPerSecond == 5);
        DedicatedServerOptions o2;
        CHECK(!parse({"--server", "--anticheat", bad}, o2, err));
        CHECK(err.find("line 1") != std::string::npos);
        { std::ofstream(cfg) << "anticheat = " << good << "\n"; }
        DedicatedServerOptions o3;
        CHECK(LoadDedicatedServerConfig(cfg, o3, &err) && o3.anticheat.enabled);
        { std::ofstream(cfg) << "anticheat = " << bad << "\n"; }
        DedicatedServerOptions o4;
        CHECK(!LoadDedicatedServerConfig(cfg, o4, &err));
        std::remove(good.c_str()); std::remove(bad.c_str()); std::remove(cfg.c_str());
    }

    Log::sink = sink;
    TEST_MAIN_RESULT();
}
