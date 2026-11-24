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
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Net/Net.hpp"
#include "okay/Net/NetSim.hpp"
#include "okay/Net/Rollback.hpp"
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
std::string ScriptVar(GameObject* go, const char* name) {
    auto* sc = go ? go->GetComponent<ScriptComponent>() : nullptr;
    return sc && sc->VM() ? sc->VM()->GetGlobal(name).AsString() : std::string();
}
std::string GraphVar(GameObject* go, const char* name) {
    auto* v = go ? go->GetComponent<VisualScriptComponent>() : nullptr;
    return v ? v->GetVariable(name).AsString() : std::string();
}
bool HasLine(const std::vector<std::string>& lines, const std::string& part) {
    for (const auto& l : lines) if (l.find(part) != std::string::npos) return true;
    return false;
}

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
        CHECK(seconds.back() < 3.0f);          // was 3 to 38 s with whole-message resends
        CHECK(sum / seconds.size() < 1.5f);
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
        CHECK(hst.lossPercent > 5.0f && hst.lossPercent < 90.0f);
        CHECK(loss == hst.lossPercent);
        CHECK(hst.rttMs > 0.0f);
        CHECK(cst.lossPercent > 5.0f);
        CHECK(!hn->PeerLink(999, rtt, loss));

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
        net::SimSettings& sim = net::Simulator();
        sim.enabled = true; sim.latencyMs = 40.0f; sim.jitterMs = 10.0f; sim.loss = 0.05f;
        CHECK(PumpAll(ScenesOf(all), 300, [&] { return b.nm->GetVar("level") == "3" && c.nm->GetVar("level") == "3"; }));
        const std::uint32_t idb = b.nm->LocalId(), idc = c.nm->LocalId();
        // The host and the player who would be next both stop answering (crash: no goodbye).
        std::vector<Peer*> left{&b, &c};
        int frames = 0;
        const bool moved = PumpAll(ScenesOf(left), 1500, [&] {
            return b.nm->IsServer() && c.nm->Joined() && c.nm->HostMigrations() == 1 && b.nm->HostMigrations() == 1 && !c.nm->Migrating() &&
                   c.nm->Players().size() == 2;
        }, &frames, 8);
        std::printf("  failover: host and next host crashed; %u took over after %d frames (%.2f s of game time)\n", idb, frames, frames * kFrame);
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
        unsigned seedH = 1, seedC = 7;
        auto pick = [](unsigned& seed) {   // each player changes its mind now and then: rollbacks happen
            seed = seed * 1103515245u + 12345u;
            const unsigned r = (seed >> 16) % 10;
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
        CHECK(hn->Rollback().StateAt(60, sh) && cn->Rollback().StateAt(60, sc));
        std::printf("  rbscript: frame 60 \"%s\"\n", sh.c_str());
        CHECK(sh == sc && sh.size() == 120);   // 60 frames of two one-letter inputs
        CHECK(hn->Rollback().Desyncs() == 0 && cn->Rollback().Desyncs() == 0);
        hn->Stop(); cn->Stop();
    }

    TEST_MAIN_RESULT();
}
