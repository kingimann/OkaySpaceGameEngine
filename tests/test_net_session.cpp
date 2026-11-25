// The second multiplayer batch, end to end over loopback with several peers in one process,
// under the program-wide network simulator (latency, jitter, loss):
//
//   bigmsg     a 12 KB event at 15% loss: only the missing pieces are sent again (per-piece
//              resend), so it arrives in about a second of game time instead of 3 to 38 s
//   roster     a client's player list and join events fill in at once on joining (not ~1 s later)
//   validate   a Net Validator for "hit" refuses the network EVENT "hit" (validators see event names)
//   hostloss   the host measures loss and ping to each client too (Stats, PeerLink)
//   overlay    the stats overlay's lines on host and client; net.show_stats and Show Network Stats turn it on
//   migrate    4 peers; the host quits: the lowest id hosts, the others come back with their ids, the
//              shared variables, ownership and the Networked crate carry on; On Host Migrated in C++,
//              a Visual Script graph and an OkayScript script
//   failover   4 peers under 40 ms latency, jitter and loss; the host AND the next host crash (stop
//              answering): the remaining two find each other and play on
//   rollback   two peers at 100 ms latency with jitter and loss run the same deterministic game with
//              input delay and rollback: they end in identical states (checksums compared, no desync)
//   rbscript   the same through OkayScript (net.rollback_start and the rollback_* functions)
//   knock      (review of #1232, F1) a player on the list sends another a forged "join" while the host
//              is alive: nothing changes; a real migration still works
//   miglan     (F2) a player on the host's computer is reached at the host's address from another one
//   fragstate  (F3) forged piece receipts grow nothing; a gone peer's piece state is forgotten
//   vars       shared variables at 40% loss: every change arrives, the newest value wins, all acked
//   netsim     the simulator's loss and latency apply once between two sockets of one program, and a
//              burst is delayed, not dropped
//   rbguard    (F4) rollback refuses a peer with another tick rate, input size or player set, and does
//              not keep inputs for frames far in the future
#include "test_framework.hpp"
#include "net_test_util.hpp"
#include <Okay.hpp>
#include "okay/Net/Net.hpp"
#include "okay/Net/NetSim.hpp"
#include "okay/Net/Rollback.hpp"
#include "okay/Core/Random.hpp"
#include "okay/Core/StringId.hpp"
#include "okay/Net/Packet.hpp"
#include "okay/Components/AntiCheatConfig.hpp"
#include "okay/Components/NetValidator.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace okay;
using nettest::ScriptVar;
using nettest::GraphVar;
using nettest::HasLine;

namespace {
constexpr float kFrame = 0.02f;
// Run every scene frame by frame until `done` (or `frames` frames). True if done.
bool PumpAll(const std::vector<Scene*>& scenes, int frames, const std::function<bool()>& done = {}, int* used = nullptr,
             int sleepMs = 2) {
    for (int i = 0; i < frames; ++i) {
        for (Scene* s : scenes) s->Update(kFrame);
        if (used) *used = i + 1;
        if (done && done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    }
    return done ? done() : true;
}
struct SimGuard {   // the simulator is program-wide: put it back however a section ends
    net::SimSettings saved = net::Simulator();
    ~SimGuard() { net::Simulator() = saved; net::ResetSimulator(); }
};

// One peer of the migration tests: a scene with a Network Manager, a host-driven crate and a
// crate that gets handed to a player, a graph and a script that count On Host Migrated.
struct Peer {
    std::unique_ptr<Scene> scene;
    NetworkManager* nm = nullptr;
    GameObject* crate = nullptr;
    GameObject* logic = nullptr;
    std::vector<std::uint32_t> migratedArgs;
    std::vector<std::uint32_t> leftIds;
    explicit Peer(const std::string& name) : scene(std::make_unique<Scene>(name)) {
        scene->physicsEnabled = false;
        nm = scene->CreateGameObject("Net")->AddComponent<NetworkManager>();
        nm->interpolationRate = 0.0f;   // snap: positions compare exactly
        crate = scene->CreateGameObject("Crate");
        crate->AddComponent<NetworkSync>();
        GameObject* ball = scene->CreateGameObject("Ball");
        ball->AddComponent<NetworkSync>();
        logic = scene->CreateGameObject("Logic");
        std::string err;
        CHECK(logic->AddComponent<VisualScriptComponent>()->LoadFromText(
            "node 1 OnHostMigrated\nnode 2 AddVar migrated\nnode 3 Const 1\ndata 2 0 3 0\nexec 1 0 2\nentry OnHostMigrated 1\n", &err));
        logic->AddComponent<ScriptComponent>("okayscript")->LoadSource(
            "var moved = -1; var shown = 0;\n"
            "function start() { }\n"
            "function show() { net.show_stats(true); shown = 1; }\n"
            "function on_host_migrated(prev) { moved = prev; }\n");
        nm->OnHostMigrated([this](std::uint32_t prev) { migratedArgs.push_back(prev); });
        nm->OnPlayerLeft([this](std::uint32_t id) { leftIds.push_back(id); });
        scene->Start();
    }
};
std::vector<Scene*> ScenesOf(const std::vector<Peer*>& ps) {
    std::vector<Scene*> out;
    for (Peer* p : ps) out.push_back(p->scene.get());
    return out;
}
} // namespace

int main() {
    RUN_SUITE("net_session");
    // The netsim and connreset sections open raw UdpSockets after every Network Manager
    // has stopped; on Windows the last Stop's WSACleanup unloads Winsock, so the test
    // holds its own reference (the Windows run failed every netsim check without it).
    CHECK(net::Startup());

    // ---------------------------------------------------------------- bigmsg
    {
        Scene hs("BigHost"); hs.physicsEnabled = false;
        Scene cs("BigClient"); cs.physicsEnabled = false;
        auto* hn = hs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        auto* cn = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        CHECK(hn->StartServer(0));
        CHECK(cn->StartClient("127.0.0.1", hn->ServerPort()));
        CHECK(PumpAll({&hs, &cs}, 200, [&] { return cn->Joined(); }));
        std::string got;
        hn->OnEvent("big", [&](std::uint32_t, const std::string& d) { got = d; });
        std::string payload(12 * 1024, ' ');
        for (std::size_t i = 0; i < payload.size(); ++i) payload[i] = (char)('a' + (i * 7) % 26);
        std::vector<float> seconds;
        {
            SimGuard guard;
            net::SimSettings& sim = net::Simulator();
            sim.enabled = true; sim.latencyMs = 40.0f; sim.jitterMs = 15.0f; sim.loss = 0.15f;
            for (int trial = 0; trial < 8; ++trial) {
                got.clear();
                payload[0] = (char)('A' + trial);   // a new message each time
                CHECK(cn->SendEvent("big", payload, false));
                int frames = 0;
                const bool ok = PumpAll({&hs, &cs}, 3000, [&] { return got == payload; }, &frames);
                CHECK(ok);
                seconds.push_back(frames * kFrame);
            }
        }
        std::sort(seconds.begin(), seconds.end());
        float sum = 0.0f;
        for (float s : seconds) sum += s;
        std::printf("  bigmsg: 12 KB at 15%% loss, 8 tries: fastest %.2f s, median %.2f s, slowest %.2f s, mean %.2f s (game time)\n",
                    seconds.front(), seconds[seconds.size() / 2], seconds.back(), sum / seconds.size());
        std::printf("  bigmsg: pieces sent again %zu, in %zu partial resends\n", cn->PiecesResent(), cn->PieceResends());
        CHECK(seconds.back() < 4.0f);          // was 3 to 38 s with whole-message resends (#1232); ~1-2 s here
        CHECK(sum / seconds.size() < 2.5f);
        CHECK(cn->PiecesResent() > 0);
        hn->Stop(); cn->Stop();
    }

    // ---------------------------------------------------------------- roster, hostloss, overlay
    {
        Scene hs("RosterHost"); hs.physicsEnabled = false;
        Scene cs("RosterClient"); cs.physicsEnabled = false;
        auto* hn = hs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        auto* cn = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        hn->SetLocalName("Hana");
        std::vector<std::uint32_t> cliJoined;
        cn->OnPlayerJoined([&](std::uint32_t id, const std::string&) { cliJoined.push_back(id); });
        CHECK(hn->StartServer(0));
        CHECK(cn->StartClient("127.0.0.1", hn->ServerPort()));
        int frames = 0;
        CHECK(PumpAll({&hs, &cs}, 200, [&] { return cn->Joined(); }, &frames));
        int after = 0;
        const bool listed = PumpAll({&hs, &cs}, 100, [&] { return cn->Players().size() == 2 && !cliJoined.empty(); }, &after);
        std::printf("  roster: joined after %d frames; the player list and the host's join event %d frame(s) later\n", frames, after);
        CHECK(listed);
        CHECK(after <= 5);   // was about a second (50 frames): the roster went out once a second
        CHECK(!cliJoined.empty() && cliJoined[0] == 0);
        const auto cp = cn->Players();
        CHECK(cp.size() == 2 && cp[0].id == 0 && cp[0].name == "Hana");

        // The host pings its clients too: its Stats have a round trip and a loss figure.
        {
            SimGuard guard;
            net::SimSettings& sim = net::Simulator();
            sim.enabled = true; sim.latencyMs = 30.0f; sim.loss = 0.25f;
            PumpAll({&hs, &cs}, 1200, {}, nullptr, 8);   // ~24 s of game time, ~20 pings each way (one a second)
        }
        float rtt = -1.0f, loss = -1.0f;
        CHECK(hn->PeerLink(cn->LocalId(), rtt, loss));
        const auto hst = hn->Stats(), cst = cn->Stats();
        std::printf("  hostloss: host sees %.0f ms, %.1f %% loss; client sees %.0f ms, %.1f %% loss (25%% each way simulated)\n",
                    hst.rttMs, hst.lossPercent, cst.rttMs, cst.lossPercent);
        CHECK(hst.lossPercent > 10.0f && hst.lossPercent <= 100.0f);   // (the simulator drops on the way out and in)
        CHECK(loss == hst.lossPercent);
        CHECK(hst.rttMs > 0.0f);
        CHECK(cst.lossPercent > 10.0f);
        CHECK(!hn->PeerLink(999, rtt, loss));
        // A clean link again: after 16 answered pings, no loss on either side.
        PumpAll({&hs, &cs}, 1100);
        const auto hst2 = hn->Stats(), cst2 = cn->Stats();
        std::printf("  hostloss: then a clean link: host %.0f ms, %.1f %% loss; client %.1f %% loss\n", hst2.rttMs, hst2.lossPercent, cst2.lossPercent);
        CHECK(hst2.lossPercent == 0.0f && cst2.lossPercent == 0.0f);
        CHECK(hst2.rttMs > 0.0f && hst2.rttMs < 200.0f);

        // The overlay's lines.
        PumpAll({&hs, &cs}, 30);
        const auto hl = hn->StatsLines(), cl = cn->StatsLines();
        for (const auto& l : cl) std::printf("  overlay (client): %s\n", l.c_str());
        CHECK(HasLine(hl, "Network: host") && HasLine(hl, "2 players") && HasLine(hl, "Ping") && HasLine(hl, "loss"));
        CHECK(HasLine(cl, "Network: client") && HasLine(cl, "Loss") && HasLine(cl, "KB/s"));
        CHECK(NetworkManager::OverlayManager() == nullptr);   // off by default
        cn->showStatsOverlay = true;
        PumpAll({&hs, &cs}, 2);
        CHECK(NetworkManager::OverlayManager() == cn);        // cn was the last to update
        cn->showStatsOverlay = false;
        // Turned on from a script and from a node.
        GameObject* so = cs.CreateGameObject("Script");
        so->AddComponent<ScriptComponent>("okayscript")->LoadSource("function go() { net.show_stats(true); }\n");
        PumpAll({&hs, &cs}, 2);   // started and bound to its object
        so->GetComponent<ScriptComponent>()->VM()->CallEventArgs("go", {});
        CHECK(cn->showStatsOverlay);
        GameObject* go = hs.CreateGameObject("Graph");
        std::string err;
        CHECK(go->AddComponent<VisualScriptComponent>()->LoadFromText(
            "node 1 OnUpdate\nnode 2 NetShowStats\nnode 3 Const true\ndata 2 0 3 0\nexec 1 0 2\nentry OnUpdate 1\n", &err));
        PumpAll({&hs, &cs}, 3);
        CHECK(hn->showStatsOverlay);
        CHECK(cn->StatsLines().empty() == false);
        hn->Stop(); cn->Stop();
        CHECK(cn->StatsLines().empty());   // offline: nothing to show
    }

    // ---------------------------------------------------------------- validate (Net Validator sees event names)
    {
        Scene hs("ValHost"); hs.physicsEnabled = false;
        Scene cs("ValClient"); cs.physicsEnabled = false;
        GameObject* hnet = hs.CreateGameObject("Net");
        auto* hn = hnet->AddComponent<NetworkManager>();
        hnet->AddComponent<AntiCheatConfig>();
        auto* rule = hnet->AddComponent<NetValidator>();
        rule->rpc = "hit";
        rule->clientsMaySend = false;   // only the host may send "hit"
        auto* cn = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        std::vector<std::string> heard;
        hn->OnAnyEvent([&](const NetworkManager::ReceivedEvent& e) { heard.push_back(e.name + "=" + e.data); });
        hs.Start(); cs.Start();
        CHECK(hn->StartServer(0));
        CHECK(cn->StartClient("127.0.0.1", hn->ServerPort()));
        CHECK(PumpAll({&hs, &cs}, 200, [&] { return cn->Joined(); }));
        CHECK(cn->SendEvent("hit", "999", false));
        CHECK(cn->SendEvent("chat", "hello", false));
        PumpAll({&hs, &cs}, 100, [&] { return !heard.empty(); });
        PumpAll({&hs, &cs}, 10);
        std::printf("  validate: host heard %zu event(s)%s\n", heard.size(), heard.empty() ? "" : (": " + heard[0]).c_str());
        CHECK(heard.size() == 1 && heard[0] == "chat=hello");   // "hit" refused by its name, "chat" passed (in order)
        hn->Stop(); cn->Stop();
    }

    // ---------------------------------------------------------------- migrate (clean leave, 4 peers)
    {
        Peer h("MigHost"), a("MigA"), b("MigB"), c("MigC");
        CHECK(h.nm->StartServer(0));
        const std::uint16_t port = h.nm->ServerPort();
        a.nm->SetLocalName("Ann"); b.nm->SetLocalName("Bob"); c.nm->SetLocalName("Cat");
        std::vector<Peer*> all{&h, &a, &b, &c};
        CHECK(a.nm->StartClient("127.0.0.1", port));
        CHECK(PumpAll(ScenesOf(all), 200, [&] { return a.nm->Joined(); }));   // join one by one: ids 1, 2, 3
        CHECK(b.nm->StartClient("127.0.0.1", port));
        CHECK(PumpAll(ScenesOf(all), 200, [&] { return b.nm->Joined(); }));
        CHECK(c.nm->StartClient("127.0.0.1", port));
        auto ready = [&] {
            for (Peer* p : {&a, &b, &c}) if (!p->nm->Joined() || !p->nm->MigrationOffered() || p->nm->Players().size() != 4) return false;
            return h.nm->Players().size() == 4;
        };
        CHECK(PumpAll(ScenesOf(all), 400, ready));
        const std::uint32_t ida = a.nm->LocalId(), idb = b.nm->LocalId(), idc = c.nm->LocalId();
        CHECK(ida < idb && idb < idc);
        h.nm->SetVar("score", "7");
        CHECK(h.nm->GiveOwnership("Ball", idb));
        h.crate->transform->SetPosition(Vec3(1.0f, 2.0f, 3.0f));
        CHECK(PumpAll(ScenesOf(all), 300, [&] {
            return c.nm->GetVar("score") == "7" && a.nm->OwnerOf("Ball") == (std::int64_t)idb && c.nm->OwnerOf("Ball") == (std::int64_t)idb &&
                   (c.crate->transform->Position() - Vec3(1.0f, 2.0f, 3.0f)).Magnitude() < 0.01f;
        }));

        h.nm->Stop();   // the host quits (it says goodbye)
        std::vector<Peer*> left{&a, &b, &c};
        int frames = 0;
        const bool moved = PumpAll(ScenesOf(left), 600, [&] {
            for (Peer* p : left) if (p->nm->HostMigrations() != 1 || p->nm->Migrating()) return false;
            return b.nm->Joined() && c.nm->Joined() && a.nm->Players().size() == 3 && b.nm->Players().size() == 3 && c.nm->Players().size() == 3;
        }, &frames);
        std::printf("  migrate: the host quit; a new host in %d frames (%.2f s of game time)\n", frames, frames * kFrame);
        CHECK(moved);
        CHECK(a.nm->IsServer() && a.nm->LocalId() == 0);
        CHECK(b.nm->IsClient() && b.nm->LocalId() == idb);   // the same ids as before
        CHECK(c.nm->IsClient() && c.nm->LocalId() == idc);
        for (Peer* p : left) {
            CHECK(p->migratedArgs.size() == 1 && p->migratedArgs[0] == ida);   // the new host was player ida
            CHECK(p->nm->GetVar("score") == "7");                              // the shared variables carry on
            CHECK(GraphVar(p->logic, "migrated") == "1");                      // Visual Script: On Host Migrated
            CHECK(ScriptVar(p->logic, "moved") == std::to_string(ida));        // OkayScript: on_host_migrated(prev)
            CHECK(std::find(p->leftIds.begin(), p->leftIds.end(), 0u) != p->leftIds.end());   // the old host left
        }
        const auto bp = b.nm->Players();
        CHECK(bp.size() == 3 && bp[0].id == 0 && bp[0].name == "Ann" && bp[0].isHost);
        CHECK(c.nm->OwnerOf("Ball") == (std::int64_t)idb && a.nm->OwnerOf("Ball") == (std::int64_t)idb);   // Bob keeps the ball
        CHECK(b.nm->IsMine("Ball") && !c.nm->IsMine("Ball"));
        CHECK(a.nm->IsMine("Crate") && !b.nm->IsMine("Crate"));   // the host-driven crate is the new host's
        // The game goes on: the new host moves the crate, Bob moves his ball, an event goes round.
        a.crate->transform->SetPosition(Vec3(5.0f, 0.0f, 0.0f));
        b.scene->Find("Ball")->transform->SetPosition(Vec3(0.0f, 0.0f, 9.0f));
        std::string cGot, aGot;
        c.nm->OnEvent("ping", [&](std::uint32_t from, const std::string& d) { cGot = std::to_string(from) + ":" + d; });
        a.nm->OnEvent("ping", [&](std::uint32_t from, const std::string& d) { aGot = std::to_string(from) + ":" + d; });
        CHECK(b.nm->SendEvent("ping", "after", false));
        const bool goesOn = PumpAll(ScenesOf(left), 300, [&] {
            return (c.crate->transform->Position() - Vec3(5.0f, 0.0f, 0.0f)).Magnitude() < 0.01f &&
                   (c.scene->Find("Ball")->transform->Position() - Vec3(0.0f, 0.0f, 9.0f)).Magnitude() < 0.01f &&
                   cGot == std::to_string(idb) + ":after" && aGot == cGot;
        });
        CHECK(goesOn);
        // A newcomer gets a fresh id (never one of the old ones).
        Peer d("MigD");
        CHECK(d.nm->StartClient("127.0.0.1", a.nm->ServerPort()));
        std::vector<Peer*> withD{&a, &b, &c, &d};
        CHECK(PumpAll(ScenesOf(withD), 300, [&] { return d.nm->Joined() && d.nm->GetVar("score") == "7" && d.nm->Players().size() == 4; }));
        CHECK(d.nm->LocalId() > idc);
        CHECK(d.nm->OwnerOf("Ball") == (std::int64_t)idb);
        for (Peer* p : withD) p->nm->Stop();
    }

    // ---------------------------------------------------------------- failover (crashes under a bad network)
    {
        SimGuard guard;
        Peer h("FailHost"), a("FailA"), b("FailB"), c("FailC");
        for (Peer* p : {&h, &a, &b, &c}) { p->nm->serverTimeoutSeconds = 1.5f; p->nm->migrationJoinTimeoutSeconds = 1.5f; }
        CHECK(h.nm->StartServer(0));
        const std::uint16_t port = h.nm->ServerPort();
        std::vector<Peer*> all{&h, &a, &b, &c};
        CHECK(a.nm->StartClient("127.0.0.1", port));
        CHECK(PumpAll(ScenesOf(all), 200, [&] { return a.nm->Joined(); }));
        CHECK(b.nm->StartClient("127.0.0.1", port));
        CHECK(PumpAll(ScenesOf(all), 200, [&] { return b.nm->Joined(); }));
        CHECK(c.nm->StartClient("127.0.0.1", port));
        CHECK(PumpAll(ScenesOf(all), 400, [&] {
            for (Peer* p : {&a, &b, &c}) if (!p->nm->Joined() || !p->nm->MigrationOffered() || p->nm->Players().size() != 4) return false;
            return true;
        }));
        h.nm->SetVar("level", "3");
        CHECK(PumpAll(ScenesOf(all), 300, [&] { return b.nm->GetVar("level") == "3" && c.nm->GetVar("level") == "3"; }));
        net::SimSettings& sim = net::Simulator();
        sim.enabled = true; sim.latencyMs = 40.0f; sim.jitterMs = 10.0f; sim.loss = 0.05f;
        PumpAll(ScenesOf(all), 100, {}, nullptr, 8);   // a while on the bad network first
        const std::uint32_t idb = b.nm->LocalId(), idc = c.nm->LocalId();
        // The host and the player who would be next both stop answering (crash: no goodbye).
        std::vector<Peer*> left{&b, &c};
        int frames = 0;
        const bool moved = PumpAll(ScenesOf(left), 1500, [&] {
            return b.nm->IsServer() && c.nm->Joined() && c.nm->HostMigrations() == 1 && b.nm->HostMigrations() == 1 && !c.nm->Migrating() &&
                   c.nm->Players().size() == 2;
        }, &frames, 8);
        std::printf("  failover: host and next host crashed; %u took over after %d frames (%.2f s of game time)\n", idb, frames, frames * kFrame);
        if (!moved)
            std::printf("  failover: b host %d id %u moves %d; c joined %d host %d moves %d migrating %d players %zu\n", (int)b.nm->IsServer(),
                        b.nm->LocalId(), b.nm->HostMigrations(), (int)c.nm->Joined(), (int)c.nm->IsServer(), c.nm->HostMigrations(),
                        (int)c.nm->Migrating(), c.nm->Players().size());
        CHECK(moved);
        CHECK(b.nm->LocalId() == 0 && c.nm->LocalId() == idc);
        CHECK(c.migratedArgs.size() == 1 && c.migratedArgs[0] == idb);
        CHECK(b.nm->GetVar("level") == "3" && c.nm->GetVar("level") == "3");
        std::string got;
        c.nm->OnEvent("still", [&](std::uint32_t from, const std::string& dd) { got = std::to_string(from) + dd; });
        CHECK(b.nm->SendEvent("still", "here", false));
        CHECK(PumpAll(ScenesOf(left), 400, [&] { return got == "0here"; }, nullptr, 8));
        sim.enabled = false;
        for (Peer* p : all) p->nm->Stop();
    }

    // ---------------------------------------------------------------- migration off, and alone
    {
        Peer h("OffHost"), a("OffA"), b("OffB");
        a.nm->hostMigration = NetworkManager::HostMigration::Off;
        CHECK(h.nm->StartServer(0));
        std::vector<Peer*> all{&h, &a, &b};
        CHECK(a.nm->StartClient("127.0.0.1", h.nm->ServerPort()));
        CHECK(b.nm->StartClient("127.0.0.1", h.nm->ServerPort()));
        CHECK(PumpAll(ScenesOf(all), 300, [&] { return a.nm->MigrationOffered() && b.nm->MigrationOffered() && b.nm->Players().size() == 3; }));
        h.nm->Stop();
        std::vector<Peer*> left{&a, &b};
        PumpAll(ScenesOf(left), 150, [&] { return !a.nm->IsConnected(); });
        CHECK(!a.nm->IsConnected() && a.nm->ConnectionLost());   // its own setting is Off: the session ends (at once: the host said goodbye)
        PumpAll(ScenesOf(left), 300, [&] { return b.nm->HostMigrations() == 1 || !b.nm->IsConnected(); });
        // B: A was the lowest id but did not take over (Off); B is struck through to itself and hosts alone.
        std::printf("  off: a ended (%s); b %s\n", a.nm->ConnectionLostReason().c_str(), b.nm->IsServer() ? "hosts" : "ended");
        CHECK(b.nm->IsServer() || !b.nm->IsConnected());
        for (Peer* p : all) p->nm->Stop();
        // The host's own setting Off: no list is offered.
        Peer h2("OffHost2"), x("OffX"), y("OffY");
        h2.nm->hostMigration = NetworkManager::HostMigration::Off;
        CHECK(h2.nm->StartServer(0));
        std::vector<Peer*> all2{&h2, &x, &y};
        CHECK(x.nm->StartClient("127.0.0.1", h2.nm->ServerPort()));
        CHECK(y.nm->StartClient("127.0.0.1", h2.nm->ServerPort()));
        CHECK(PumpAll(ScenesOf(all2), 300, [&] { return y.nm->Players().size() == 3 && x.nm->Players().size() == 3; }));
        PumpAll(ScenesOf(all2), 20);
        CHECK(!x.nm->MigrationOffered() && !y.nm->MigrationOffered());
        for (Peer* p : all2) p->nm->Stop();
    }

    // ---------------------------------------------------------------- knock (review F1: a forged knock changes nothing)
    {
        Peer h("KnockHost"), a("KnockA"), b("KnockB"), c("KnockC");
        CHECK(h.nm->StartServer(0));
        const std::uint16_t port = h.nm->ServerPort();
        std::vector<Peer*> all{&h, &a, &b, &c};
        CHECK(a.nm->StartClient("127.0.0.1", port));
        CHECK(PumpAll(ScenesOf(all), 200, [&] { return a.nm->Joined(); }));
        CHECK(b.nm->StartClient("127.0.0.1", port));
        CHECK(PumpAll(ScenesOf(all), 200, [&] { return b.nm->Joined(); }));
        CHECK(c.nm->StartClient("127.0.0.1", port));
        CHECK(PumpAll(ScenesOf(all), 400, [&] {
            for (Peer* p : {&a, &b, &c}) if (!p->nm->Joined() || !p->nm->MigrationOffered() || p->nm->Players().size() != 4) return false;
            return true;
        }));
        // c is on everyone's migration list. It sends a's address one plain datagram that looks like a
        // join (byte 1, then byte 24), from its own listed address, while the host is alive.
        const std::uint16_t cPort = c.nm->ServerPort();
        net::Endpoint aEp;
        CHECK(net::UdpSocket::Resolve("127.0.0.1", a.nm->ServerPort(), aEp));
        c.nm->Stop();   // frees c's port for the forger; a still lists c until the host's next list
        net::UdpSocket forger;
        CHECK(forger.Open() && forger.Bind(cPort));
        std::vector<std::uint8_t> knock(64, 0);
        std::vector<Peer*> live{&h, &a, &b};
        for (int i = 0; i < 60; ++i) {   // and keeps knocking, with both bytes
            knock[0] = (i % 2) ? 24 : 1;
            if (i < 2 || i % 10 == 0) forger.SendTo(aEp, knock.data(), knock.size());
            PumpAll(ScenesOf(live), 1);
        }
        std::printf("  knock: after forged knocks a is %s, %d move(s); host has %zu players\n",
                    a.nm->IsServer() ? "HOSTING (split)" : a.nm->Joined() ? "still on the host" : "gone", a.nm->HostMigrations(),
                    h.nm->Players().size());
        CHECK(a.nm->IsClient() && a.nm->Joined() && !a.nm->Migrating() && a.nm->HostMigrations() == 0);
        CHECK(b.nm->IsClient() && b.nm->Joined() && b.nm->HostMigrations() == 0);
        CHECK(h.nm->IsServer());
        CHECK(PumpAll(ScenesOf(live), 200, [&] { return h.nm->Players().size() == 3 && a.nm->Players().size() == 3; }));
        forger.Close();
        // A real migration still works: the host quits, a hosts, b comes back.
        h.nm->Stop();
        std::vector<Peer*> left{&a, &b};
        CHECK(PumpAll(ScenesOf(left), 600, [&] {
            return a.nm->IsServer() && b.nm->Joined() && a.nm->HostMigrations() == 1 && b.nm->HostMigrations() == 1 && !b.nm->Migrating();
        }));
        for (Peer* p : all) p->nm->Stop();
    }

    // ---------------------------------------------------------------- miglan (review F2: a host-machine player on the list)
    {
        const net::Endpoint loop{0x7F000001u, 5000}, hostLan{0xC0A80105u, 7777}, hostLoop{0x7F000001u, 7777};
        const net::Endpoint other{0xC0A80107u, 6000};
        const net::Endpoint e = NetworkManager::MigrationAddress(loop, hostLan);
        CHECK(e.address == hostLan.address && e.port == 5000);   // another computer reaches it at the host's address
        CHECK(NetworkManager::MigrationAddress(loop, hostLoop) == loop);   // on the host's computer: as listed
        CHECK(NetworkManager::MigrationAddress(other, hostLan) == other);  // a LAN player: as listed
        CHECK(NetworkManager::MigrationAddress(other, hostLoop) == other);
    }

    // ---------------------------------------------------------------- fragstate (review F3)
    {
        Scene hs("FragHost"); hs.physicsEnabled = false;
        Scene cs("FragClient"); cs.physicsEnabled = false;
        auto* hn = hs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        auto* cn = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        CHECK(hn->StartServer(0));
        CHECK(cn->StartClient("127.0.0.1", hn->ServerPort()));
        CHECK(PumpAll({&hs, &cs}, 200, [&] { return cn->Joined() && hn->Players().size() == 2; }));
        std::string up, down;
        hn->OnEvent("bigup", [&](std::uint32_t, const std::string& d) { up = d; });
        cn->OnEvent("bigdown", [&](std::uint32_t, const std::string& d) { down = d; });
        std::string payload(12 * 1024, 'x');
        for (std::size_t i = 0; i < payload.size(); ++i) payload[i] = (char)('a' + (i * 11) % 26);
        CHECK(cn->SendEvent("bigup", payload, false));
        CHECK(PumpAll({&hs, &cs}, 300, [&] { return up == payload; }));
        CHECK(hn->FragDonePeers() == 1);   // the host remembers what it put together from this client
        // A flood of forged piece receipts "from the client" while the host sends it a big message.
        const net::Endpoint cEp{0x7F000001u, cn->ServerPort()};
        CHECK(hn->SendEvent("bigdown", payload, false));
        const std::size_t msgs0 = hn->FragOutMessages(), bytes0 = hn->FragOutBytes();
        CHECK(msgs0 >= 1);
        Random rng(99);
        std::vector<std::uint8_t> ack;
        for (int i = 0; i < 20000; ++i) {
            const std::uint32_t id = (std::uint32_t)rng.Range(0, 2000), count = (std::uint32_t)rng.Range(1, 4096);
            net::Packet p((std::uint8_t)35); p.Write(id); p.Write(count);
            ack.assign(p.Data(), p.Data() + p.Size());
            ack.resize(ack.size() + (count + 7) / 8, (std::uint8_t)rng.Range(0, 255));
            hn->FeedFragAckForTest(cEp, ack.data(), ack.size());
        }
        std::printf("  fragstate: after 20000 forged receipts the host holds %zu message(s), %zu bytes (was %zu, %zu)\n",
                    hn->FragOutMessages(), hn->FragOutBytes(), msgs0, bytes0);
        CHECK(hn->FragOutMessages() <= msgs0 && hn->FragOutBytes() <= bytes0);   // nothing grows
        CHECK(PumpAll({&hs, &cs}, 600, [&] { return down == payload; }));        // and the message still arrives
        // The client leaves: the host forgets its per-address piece state (it leaked until Stop before).
        cn->Stop();
        CHECK(PumpAll({&hs}, 300, [&] { return hn->Players().size() == 1 && hn->FragDonePeers() == 0; }));
        std::printf("  fragstate: after the client left the host keeps %zu piece list(s)\n", hn->FragDonePeers());
        CHECK(hn->FragDonePeers() == 0 && hn->FragOutMessages() == 0);
        hn->Stop();
    }

    // ---------------------------------------------------------------- vars (shared variables survive loss)
    {
        SimGuard guard;
        Scene hs("VarHost"); hs.physicsEnabled = false;
        Scene cs("VarClient"); cs.physicsEnabled = false;
        auto* hn = hs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        auto* cn = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        CHECK(hn->StartServer(0));
        CHECK(cn->StartClient("127.0.0.1", hn->ServerPort()));
        CHECK(PumpAll({&hs, &cs}, 200, [&] { return cn->Joined() && hn->Players().size() == 2; }));
        net::SimSettings& sim = net::Simulator();
        sim.enabled = true; sim.latencyMs = 30.0f; sim.jitterMs = 25.0f; sim.loss = 0.4f;
        for (int i = 0; i < 40; ++i) {
            hn->SetVar("h" + std::to_string(i), std::to_string(i));
            cn->SetVar("c" + std::to_string(i), std::to_string(i * 2));
        }
        for (int v = 1; v <= 60; ++v) {   // a value changed every frame: the newest one wins, not a late old one
            hn->SetVar("hot", std::to_string(v));
            cn->SetVar("cold", std::to_string(v));
            PumpAll({&hs, &cs}, 1, {}, nullptr, 1);
        }
        auto allThere = [&] {
            for (int i = 0; i < 40; ++i) {
                if (cn->GetVar("h" + std::to_string(i)) != std::to_string(i)) return false;
                if (hn->GetVar("c" + std::to_string(i)) != std::to_string(i * 2)) return false;
            }
            return cn->GetVar("hot") == "60" && hn->GetVar("cold") == "60" && cn->GetVar("cold") == "60" &&
                   hn->VarsUnacked() == 0 && cn->VarsUnacked() == 0;
        };
        int frames = 0;
        const bool ok = PumpAll({&hs, &cs}, 1500, allThere, &frames, 4);
        int missing = 0;
        for (int i = 0; i < 40; ++i) {
            if (cn->GetVar("h" + std::to_string(i)) != std::to_string(i)) ++missing;
            if (hn->GetVar("c" + std::to_string(i)) != std::to_string(i * 2)) ++missing;
        }
        std::printf("  vars: 80 changes and 2 x 60 quick ones at 40%% loss: %d missing, hot \"%s\" cold \"%s\", unacked %zu + %zu, after %d frames\n",
                    missing, cn->GetVar("hot").c_str(), hn->GetVar("cold").c_str(), hn->VarsUnacked(), cn->VarsUnacked(), frames);
        CHECK(ok);
        sim.enabled = false;
        hn->Stop(); cn->Stop();
    }

    // ---------------------------------------------------------------- netsim (the settings mean what they say)
    {
        SimGuard guard;
        net::UdpSocket sa, sb;
        CHECK(sa.Open() && sa.Bind(0) && sb.Open() && sb.Bind(0));
        net::Endpoint toB;
        CHECK(net::UdpSocket::Resolve("127.0.0.1", sb.LocalPort(), toB));
        std::vector<std::uint8_t> got(net::kMaxDatagram);
        net::Endpoint from;
        auto drain = [&] {   // read B (through the simulator) and pump A's held datagrams
            int n = 0;
            net::PumpSimulator(sa);
            while (net::TakeDatagram(sb, got.data(), got.size(), from) > 0) ++n;
            return n;
        };
        auto send = [&](const std::uint8_t* d, std::size_t len) {
            if (!net::HoldDatagram(sa, toB, d, len)) sa.SendTo(toB, d, len);
        };
        drain();   // B reads through the simulator, as a Network Manager's socket does
        net::SimSettings& sim = net::Simulator();
        sim.enabled = true; sim.latencyMs = 0.0f; sim.jitterMs = 0.0f; sim.loss = 0.3f; sim.duplicate = 0.0f;
        const std::uint8_t msg[32] = {7};
        int received = 0;
        const int sent = 3000;
        for (int i = 0; i < sent; ++i) {
            send(msg, sizeof msg);
            if (i % 50 == 49) received += drain();
        }
        for (int i = 0; i < 20; ++i) { received += drain(); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
        const float lossSeen = 1.0f - (float)received / (float)sent;
        std::printf("  netsim: loss 30%% set, %.1f%% seen between two sockets of one program\n", lossSeen * 100.0f);
        CHECK(lossSeen > 0.25f && lossSeen < 0.35f);   // was ~51% (dropped on the way out and again on the way in)
        // A burst under 200 ms of latency is delayed, not lost (one queue of 256 for everyone dropped the rest).
        sim.loss = 0.0f; sim.latencyMs = 200.0f;
        received = 0;
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 1000; ++i) {
            send(msg, sizeof msg);
            if (i % 25 == 24) received += drain();
        }
        double firstAt = -1.0;
        while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(900)) {
            const int n = drain();
            if (n > 0 && firstAt < 0.0) firstAt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            received += n;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        std::printf("  netsim: 1000 datagrams at 200 ms latency: %d arrived, the first after %.0f ms, %zu overflowed\n", received,
                    firstAt * 1000.0, net::SimulatorOverflows());
        CHECK(received == 1000);
        CHECK(firstAt > 0.15 && firstAt < 0.35);   // the latency once (it was added on the way out and again on the way in)
        sim.enabled = false;
        net::ResetSimulator(sa); net::ResetSimulator(sb);
    }

    // ---------------------------------------------------------------- connreset (a send to a closed port does not block reading)
    {
        // Windows turns the "port unreachable" a send to a closed port gets back into a
        // WSAECONNRESET on the next recvfrom unless the socket asks it not to; a receive
        // loop then stops for that frame, which is what a host sees when a player quits.
        net::UdpSocket a, b, gone;
        CHECK(a.Open() && a.Bind(0) && b.Open() && b.Bind(0) && gone.Open() && gone.Bind(0));
        net::Endpoint toGone{0x7F000001u, gone.LocalPort()}, toA{0x7F000001u, a.LocalPort()};
        gone.Close();
        const std::uint8_t ping[4] = {1, 2, 3, 4};
        for (int i = 0; i < 3; ++i) a.SendTo(toGone, ping, sizeof ping);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));   // let the unreachable replies come back
        for (int i = 0; i < 3; ++i) b.SendTo(toA, ping, sizeof ping);
        CHECK(a.WaitReadable(1000));
        std::this_thread::sleep_for(std::chrono::milliseconds(20));   // all three are in
        std::uint8_t buf[16]; net::Endpoint from;
        int got = 0, errors = 0;
        for (int i = 0; i < 10; ++i) {
            const int n = a.RecvFrom(buf, sizeof buf, from);
            if (n > 0) ++got;
            else if (n < 0) ++errors;
            else break;
        }
        std::printf("  connreset: 3 datagrams after 3 sends to a closed port: %d read in one loop, %d errors\n", got, errors);
        CHECK(got == 3 && errors == 0);
    }

    // ---------------------------------------------------------------- rbguard (review F4: settings agreed, future inputs capped)
    {
        Scene hs("RbgHost"); hs.physicsEnabled = false;
        Scene cs("RbgClient"); cs.physicsEnabled = false;
        auto* hn = hs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        auto* cn = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        CHECK(hn->StartServer(0));
        CHECK(cn->StartClient("127.0.0.1", hn->ServerPort()));
        CHECK(PumpAll({&hs, &cs}, 300, [&] { return cn->Joined() && cn->Players().size() == 2 && hn->Players().size() == 2; }));
        const std::uint32_t cid = cn->LocalId();
        net::Rollback::Settings st;
        st.tickRate = 50.0f; st.inputDelay = 2; st.maxPrediction = 8; st.maxInputBytes = 255;
        int runs = 0;
        auto start = [&] {
            ++runs;
            return hn->Rollback().Start(st, [] { return std::string("s"); }, [](const std::string&) {}, [] { return std::string("i"); },
                                        [](std::uint32_t, const std::vector<net::Rollback::FrameInput>&) {});
        };
        auto put32 = [](std::string& w, std::uint32_t v) { for (int i = 0; i < 4; ++i) w.push_back((char)((v >> (8 * i)) & 0xFF)); };
        const std::uint32_t goodHash = (std::uint32_t)StringId::HashOf("0," + std::to_string(cid) + ",");
        // The client's "__rb" packet as the wire describes it (okay/Net/Rollback.cpp), with every field chosen.
        auto packet = [&](std::uint8_t delay, std::uint8_t maxBytes, float hz, std::uint8_t players, std::uint32_t hash,
                          std::uint32_t first, int n, int len) {
            std::string w;
            w.push_back((char)2); w.push_back((char)runs); w.push_back((char)delay); w.push_back((char)maxBytes);
            put32(w, (std::uint32_t)std::lround(hz * 1000.0f)); w.push_back((char)players); put32(w, hash);
            put32(w, first); w.push_back((char)n);
            for (int i = 0; i < n; ++i) { w.push_back((char)len); w.append((std::size_t)len, 'z'); }
            w.push_back((char)0);                      // no acks
            put32(w, 0xFFFFFFFFu); put32(w, 0); put32(w, 0);   // no checksum yet
            return w;
        };
        CHECK(start());
        hn->Rollback().OnMessage(cid, packet(2, 255, 50.0f, 2, goodHash, 2, 3, 4));   // a fair packet is taken
        CHECK(hn->Rollback().Running());
        const std::size_t held0 = hn->Rollback().HeldInputs();
        CHECK(held0 >= 3);
        // A peer parks 255 inputs of 255 bytes for frames far in the future, again and again.
        for (std::uint32_t k = 0; k < 400; ++k) hn->Rollback().OnMessage(cid, packet(2, 255, 50.0f, 2, goodHash, 1000 + k * 255, 255, 255));
        // And a run of 255 from the next frame on: only those up to how far ahead a player can be are kept.
        hn->Rollback().OnMessage(cid, packet(2, 255, 50.0f, 2, goodHash, 5, 255, 255));
        const std::size_t held1 = hn->Rollback().HeldInputs(), bytes1 = hn->Rollback().HeldInputBytes();
        std::printf("  rbguard: after forged future inputs the host holds %zu inputs, %zu bytes (from %zu)\n", held1, bytes1, held0);
        CHECK(held1 <= held0 + 2 * 2 + 8 + net::Rollback::kAheadMargin + 2);
        CHECK(bytes1 < 16 * 1024);   // was up to ~25 MB
        CHECK(hn->Rollback().Running());
        // A different tick rate, input size or player set stops it in plain words.
        hn->Rollback().OnMessage(cid, packet(2, 255, 60.0f, 2, goodHash, 2, 1, 1));
        std::printf("  rbguard: tick rate: \"%s\"\n", hn->Rollback().LastError().c_str());
        CHECK(!hn->Rollback().Running() && hn->Rollback().LastError().find("60 Hz") != std::string::npos);
        CHECK(start());
        hn->Rollback().OnMessage(cid, packet(2, 64, 50.0f, 2, goodHash, 2, 1, 1));
        std::printf("  rbguard: input size: \"%s\"\n", hn->Rollback().LastError().c_str());
        CHECK(!hn->Rollback().Running() && hn->Rollback().LastError().find("64 bytes") != std::string::npos);
        CHECK(start());
        hn->Rollback().OnMessage(cid, packet(2, 255, 50.0f, 3, 12345u, 2, 1, 1));
        std::printf("  rbguard: players: \"%s\"\n", hn->Rollback().LastError().c_str());
        CHECK(!hn->Rollback().Running() && hn->Rollback().LastError().find("players") != std::string::npos);
        CHECK(start());
        hn->Rollback().OnMessage(cid, packet(2, 255, 50.0f, 2, 12345u, 2, 1, 1));   // the same count, other players
        CHECK(!hn->Rollback().Running() && hn->Rollback().LastError().find("players") != std::string::npos);
        CHECK(start());
        hn->Rollback().OnMessage(cid, packet(3, 255, 50.0f, 2, goodHash, 2, 1, 1));   // the input delay, as before
        CHECK(!hn->Rollback().Running() && hn->Rollback().LastError().find("input delay") != std::string::npos);
        hn->Stop(); cn->Stop();
    }

    // ---------------------------------------------------------------- rollback (C++)
    {
        SimGuard guard;
        Scene hs("RbHost"); hs.physicsEnabled = false;
        Scene cs("RbClient"); cs.physicsEnabled = false;
        auto* hn = hs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        auto* cn = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        CHECK(hn->StartServer(0));
        CHECK(cn->StartClient("127.0.0.1", hn->ServerPort()));
        CHECK(PumpAll({&hs, &cs}, 300, [&] { return cn->Joined() && cn->Players().size() == 2 && hn->Players().size() == 2; }));
        // The game: two dots on a line and a score that depends on both (they meet: +1). Fully determined by the inputs.
        struct Game { int x[2] = {0, 50}; int score = 0; int steps = 0; };
        auto save = [](const Game& g) {
            return std::to_string(g.x[0]) + " " + std::to_string(g.x[1]) + " " + std::to_string(g.score) + " " + std::to_string(g.steps);
        };
        auto load = [](Game& g, const std::string& s) { std::sscanf(s.c_str(), "%d %d %d %d", &g.x[0], &g.x[1], &g.score, &g.steps); };
        auto step = [](Game& g, const std::vector<net::Rollback::FrameInput>& in) {
            for (std::size_t i = 0; i < in.size() && i < 2; ++i) {
                const int d = in[i].input.empty() ? 0 : (in[i].input[0] == 'L' ? -1 : in[i].input[0] == 'R' ? 1 : 0);
                g.x[i] += d;
            }
            if (std::abs(g.x[0] - g.x[1]) < 3) { ++g.score; g.x[0] -= 10; g.x[1] += 10; }
            ++g.steps;
        };
        Game gh, gc;
        Random seedH(1), seedC(7);
        auto pick = [](Random& rng) {   // each player changes its mind now and then: rollbacks happen
            const int r = rng.Range(0, 10);
            return std::string(r < 4 ? "R" : r < 8 ? "L" : "-");
        };
        std::string lastH, lastC;
        int sinceH = 0, sinceC = 0;
        net::Rollback::Settings st;
        st.tickRate = 50.0f; st.inputDelay = 3; st.maxPrediction = 20;
        CHECK(hn->Rollback().Start(st, [&] { return save(gh); }, [&](const std::string& s) { load(gh, s); },
              [&] { if (lastH.empty() || ++sinceH >= 6) { lastH = pick(seedH); sinceH = 0; } return lastH; },
              [&](std::uint32_t, const std::vector<net::Rollback::FrameInput>& in) { step(gh, in); }));
        CHECK(cn->Rollback().Start(st, [&] { return save(gc); }, [&](const std::string& s) { load(gc, s); },
              [&] { if (lastC.empty() || ++sinceC >= 6) { lastC = pick(seedC); sinceC = 0; } return lastC; },
              [&](std::uint32_t, const std::vector<net::Rollback::FrameInput>& in) { step(gc, in); }));
        CHECK(hn->Rollback().Players().size() == 2 && cn->Rollback().Players() == hn->Rollback().Players());
        net::SimSettings& sim = net::Simulator();
        sim.enabled = true; sim.latencyMs = 100.0f; sim.jitterMs = 20.0f; sim.loss = 0.10f;
        const std::uint32_t target = 400;
        const bool ran = PumpAll({&hs, &cs}, 4000, [&] {
            return hn->Rollback().ConfirmedFrame() >= target && cn->Rollback().ConfirmedFrame() >= target;
        }, nullptr, 18);   // ~real time: 20 ms frames, so the 100 ms delay is ~5 frames
        sim.enabled = false;
        CHECK(ran);
        const net::Rollback& rh = hn->Rollback();
        const net::Rollback& rc = cn->Rollback();
        std::printf("  rollback: host frame %u (final %u), %u rollbacks, %u frames run again, %u stalls; client frame %u (final %u), "
                    "%u rollbacks, %u stalls; %u + %u checksums compared, %u + %u desyncs\n",
                    rh.Frame(), rh.ConfirmedFrame(), rh.Rollbacks(), rh.FramesResimulated(), rh.Stalls(), rc.Frame(), rc.ConfirmedFrame(),
                    rc.Rollbacks(), rc.Stalls(), rh.ChecksumsCompared(), rc.ChecksumsCompared(), rh.Desyncs(), rc.Desyncs());
        // The same state at every final frame both still hold, and at the target.
        int compared = 0, same = 0;
        const std::uint32_t upTo = std::min(rh.ConfirmedFrame(), rc.ConfirmedFrame());
        for (std::uint32_t f = upTo > 120 ? upTo - 120 : 0; f <= upTo; ++f) {
            std::string sh, sc;
            if (rh.StateAt(f, sh) && rc.StateAt(f, sc)) { ++compared; if (sh == sc) ++same; }
        }
        std::string th, tc;
        CHECK(rh.StateAt(upTo - 1, th) && rc.StateAt(upTo - 1, tc));
        std::printf("  rollback: %d final frames compared, %d identical; frame %u: \"%s\" and \"%s\"\n", compared, same, upTo, th.c_str(), tc.c_str());
        CHECK(compared > 50 && same == compared);
        CHECK(th == tc && !th.empty());
        CHECK(rh.Rollbacks() > 0 || rc.Rollbacks() > 0);   // the latency made it guess, and guess wrong sometimes
        CHECK(rh.Desyncs() == 0 && rc.Desyncs() == 0);
        CHECK(rh.ChecksumsCompared() > 0 && rc.ChecksumsCompared() > 0);
        CHECK(HasLine(hn->StatsLines(), "Rollback: frame"));
        // A game that is NOT deterministic is caught by the checksums.
        bool flip = false;
        CHECK(cn->Rollback().Start(st, [&] { return save(gc) + (flip ? "x" : ""); }, [&](const std::string& s) { load(gc, s); },
              [&] { return std::string("-"); }, [&](std::uint32_t, const std::vector<net::Rollback::FrameInput>& in) { step(gc, in); flip = !flip; }));
        CHECK(hn->Rollback().Start(st, [&] { return save(gh); }, [&](const std::string& s) { load(gh, s); },
              [&] { return std::string("-"); }, [&](std::uint32_t, const std::vector<net::Rollback::FrameInput>& in) { step(gh, in); }));
        PumpAll({&hs, &cs}, 400, [&] { return hn->Rollback().Desyncs() > 0 || cn->Rollback().Desyncs() > 0; }, nullptr, 4);
        CHECK(hn->Rollback().Desyncs() > 0 || cn->Rollback().Desyncs() > 0);
        // A player leaving stops it, in plain words.
        cn->Stop();
        PumpAll({&hs}, 200, [&] { return !hn->Rollback().Running(); });
        CHECK(!hn->Rollback().Running() && hn->Rollback().LastError().find("left") != std::string::npos);
        hn->Stop();
    }

    // ---------------------------------------------------------------- rbscript (rollback from OkayScript)
    {
        Scene hs("RbsHost"); hs.physicsEnabled = false;
        Scene cs("RbsClient"); cs.physicsEnabled = false;
        auto* hn = hs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        auto* cn = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        const std::string src =
            "var x = \"\"; var n = 0; var ok = false;\n"
            "function begin() { ok = net.rollback_start(2, 50, 12); }\n"
            "function rollback_save() { return x; }\n"
            "function rollback_load(s) { x = s; }\n"
            "function rollback_input() { n = n + 1; if (n % 3 == 0) { return \"a\"; } return \"b\"; }\n"
            "function rollback_step(frame, inputs) { x = x + inputs[0] + inputs[1]; }\n";
        GameObject* ho = hs.CreateGameObject("Logic");
        GameObject* co = cs.CreateGameObject("Logic");
        ho->AddComponent<ScriptComponent>("okayscript")->LoadSource(src);
        co->AddComponent<ScriptComponent>("okayscript")->LoadSource(src);
        hs.Start(); cs.Start();
        CHECK(hn->StartServer(0));
        CHECK(cn->StartClient("127.0.0.1", hn->ServerPort()));
        CHECK(PumpAll({&hs, &cs}, 300, [&] { return cn->Joined() && cn->Players().size() == 2; }));
        ho->GetComponent<ScriptComponent>()->VM()->CallEventArgs("begin", {});
        co->GetComponent<ScriptComponent>()->VM()->CallEventArgs("begin", {});
        CHECK(ho->GetComponent<ScriptComponent>()->VM()->GetGlobal("ok").AsBool());
        CHECK(co->GetComponent<ScriptComponent>()->VM()->GetGlobal("ok").AsBool());
        CHECK(PumpAll({&hs, &cs}, 1000, [&] { return hn->Rollback().ConfirmedFrame() >= 60 && cn->Rollback().ConfirmedFrame() >= 60; }, nullptr, 4));
        std::string sh, sc;
        CHECK(hn->Rollback().StateAt(59, sh) && cn->Rollback().StateAt(59, sc));   // the state before frame 59 ran
        std::printf("  rbscript: frame 59 \"%s\"\n", sh.c_str());
        CHECK(sh == sc && sh.size() == 114);   // 59 frames, the first 2 (the input delay) with no input, then two letters each
        CHECK(hn->Rollback().Desyncs() == 0 && cn->Rollback().Desyncs() == 0);
        hn->Stop(); cn->Stop();
    }

    TEST_MAIN_RESULT();
}
