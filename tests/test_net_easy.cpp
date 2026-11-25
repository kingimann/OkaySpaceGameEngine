// The easy multiplayer layer (NetworkManagerEvents.cpp, okay::Net, the Visual Script
// and OkayScript calls on it), end to end over loopback with two peers in one process:
//
//   cpp        host -> join -> a player spawned for each side -> the moved player and a
//              Networked object follow -> named events both ways (and to one player) ->
//              player list, join and leave events on both sides -> the network clock agrees
//   errors     plain words for a host that never answers, a port in use, a bad event name;
//              a client can't forge who sent an event
//   auto       HostOrJoin: the first copy hosts, the second joins (two copies on one PC)
//   lossy      40 ms latency, 15 ms jitter, 15% loss, 5% duplicates: 40 events arrive
//              once each and in order, and a 12 KB event arrives whole
//   graph      the SAME Visual Script graph on two peers: Host or Join, On Player Joined,
//              Send Network Event / On Network Event, Event Sender, Player Count
//   example    examples/multiplayer/two_players.okayscene loaded twice, as two copies of the game on one PC:
//              the first hosts, the second joins, each gets a player per side, the host's ball follows
//   okay       two .okay scripts: net.host_or_join, net.on (a function name and a lambda),
//              net.send, on_player_joined / on_player_left, net.players
#include "test_framework.hpp"
#include "net_test_util.hpp"
#include <Okay.hpp>
#include "okay/Net/Net.hpp"
#include "okay/Net/NetSim.hpp"

#include <chrono>
#include <cmath>
#include <functional>
#include <string>
#include <thread>
#include <vector>

using namespace okay;
using nettest::ScriptVar;
using nettest::GraphVar;

namespace {
// Run both scenes frame by frame until `done` (or `frames` frames). True if done.
bool Pump(Scene& a, Scene& b, int frames, const std::function<bool()>& done = {}) {
    for (int i = 0; i < frames; ++i) {
        a.Update(0.02f);
        b.Update(0.02f);
        if (done && done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return done ? done() : true;
}
bool Near(const Vec3& p, const Vec3& q, float tol) {
    return std::fabs(p.x - q.x) < tol && std::fabs(p.y - q.y) < tol && std::fabs(p.z - q.z) < tol;
}
// A free UDP port on this computer, found by hosting on port 0 for a moment.
std::uint16_t FreePort() {
    Scene s("probe");
    auto* n = s.CreateGameObject("Net")->AddComponent<NetworkManager>();
    if (!n->StartServer(0)) return 0;
    const std::uint16_t p = n->ServerPort();
    n->Stop();
    return p;
}
} // namespace

int main() {
    RUN_SUITE("net_easy");

    // ---------------------------------------------------------------- cpp
    {
        // Both scenes are the same game: a player template the spawner clones for
        // every player, and a crate marked Networked (a NetworkSync), host-driven.
        auto build = [](Scene& s) {
            GameObject* t = s.CreateGameObject("PlayerTemplate");
            t->AddComponent<MeshRenderer>()->mesh = Mesh::Cube();
            s.CreateGameObject("Spawner")->AddComponent<NetworkPlayerSpawner>();
            GameObject* crate = s.CreateGameObject("Crate");
            crate->AddComponent<NetworkSync>();   // the Inspector's "Networked" tick
            return crate;
        };
        Scene hs("Host"); hs.physicsEnabled = false;
        Scene cs("Client"); cs.physicsEnabled = false;
        GameObject* hostCrate = build(hs);
        GameObject* cliCrate = build(cs);

        NetworkManager* hn = Net::Use(hs);
        CHECK(hn != nullptr);
        hs.Start();
        CHECK(Net::Host(0));
        CHECK(Net::IsHost());
        CHECK(Net::LastError().empty());
        const std::uint16_t port = hn->ServerPort();

        std::vector<std::string> hostLog, clientLog;
        Net::OnPlayerJoined([&](std::uint32_t id, const std::string& name) { hostLog.push_back("join " + std::to_string(id) + " " + name); });
        Net::OnPlayerLeft([&](std::uint32_t id) { hostLog.push_back("left " + std::to_string(id)); });
        Net::On("hit", [&](std::uint32_t from, const std::string& d) { hostLog.push_back("hit " + std::to_string(from) + " " + d); });

        NetworkManager* cn = Net::Use(cs);
        CHECK(cn != nullptr && cn != hn);
        cn->SetLocalName("Bea");
        cs.Start();
        CHECK(Net::Join("127.0.0.1:" + std::to_string(port)));
        Net::OnPlayerJoined([&](std::uint32_t id, const std::string& name) { clientLog.push_back("join " + std::to_string(id) + " " + name); });
        Net::On("hit", [&](std::uint32_t from, const std::string& d) { clientLog.push_back("hit " + std::to_string(from) + " " + d); });
        Net::On("round", [&](std::uint32_t from, const std::string& d) { clientLog.push_back("round " + std::to_string(from) + " " + d); });
        Net::On("whisper", [&](std::uint32_t from, const std::string& d) { clientLog.push_back("whisper " + std::to_string(from) + " " + d); });

        // Join: both sides announce the other (the client hears the roster about once a second).
        const bool joined = Pump(hs, cs, 200, [&] { return cn->Joined() && !hostLog.empty() && !clientLog.empty(); });
        CHECK(joined);
        const std::uint32_t cid = cn->LocalId();
        CHECK(cid != 0);
        CHECK(!hostLog.empty() && hostLog[0] == "join " + std::to_string(cid) + " Bea");
        CHECK(!clientLog.empty() && clientLog[0].rfind("join 0 ", 0) == 0);

        // The player list is the same on both sides: the host (0) and Bea.
        auto hp = hn->Players(), cp = cn->Players();
        CHECK(hp.size() == 2 && cp.size() == 2);
        if (hp.size() == 2) { CHECK(hp[0].id == 0 && hp[0].isHost && hp[0].isLocal); CHECK(hp[1].id == cid && hp[1].name == "Bea" && !hp[1].isLocal); }
        if (cp.size() == 2) { CHECK(cp[0].id == 0 && !cp[0].isLocal); CHECK(cp[1].id == cid && cp[1].isLocal); }
        Net::Use(cs);
        CHECK(Net::MyId() == cid && !Net::IsHost() && Net::IsConnected());

        // A player is spawned for each side: the host sees Bea's, Bea sees the host's.
        Pump(hs, cs, 100, [&] { return hs.Find("Player_" + std::to_string(cid)) && cs.Find("Player_0"); });
        GameObject* beaOnHost = hs.Find("Player_" + std::to_string(cid));
        CHECK(beaOnHost != nullptr);
        CHECK(cs.Find("Player_0") != nullptr);
        CHECK(hs.Find("LocalPlayer") != nullptr && cs.Find("LocalPlayer") != nullptr);

        // Bea moves her player; the host's copy of it follows. The host moves the crate; Bea's follows.
        GameObject* beaLocal = cs.Find("LocalPlayer");
        if (beaLocal) beaLocal->transform->localPosition = {4.0f, 0.0f, 2.0f};
        hostCrate->transform->localPosition = {-3.0f, 1.0f, 5.0f};
        const bool moved = Pump(hs, cs, 200, [&] {
            GameObject* b = hs.Find("Player_" + std::to_string(cid));
            return b && Near(b->transform->localPosition, {4.0f, 0.0f, 2.0f}, 0.1f) &&
                   Near(cliCrate->transform->localPosition, {-3.0f, 1.0f, 5.0f}, 0.1f);
        });
        CHECK(moved);

        // Named events: Bea's "hit" reaches the host (stamped with her id) and runs on her side too;
        // the host's "round" reaches Bea from 0; a whisper goes to Bea only.
        hostLog.clear(); clientLog.clear();
        Net::Use(cs);
        CHECK(Net::Send("hit", "10"));
        Net::Use(hs);
        CHECK(Net::Send("round", "go"));
        CHECK(Net::SendTo(cid, "whisper", "psst"));
        Pump(hs, cs, 150, [&] { return hostLog.size() >= 1 && clientLog.size() >= 3; });
        CHECK(hostLog.size() == 1 && hostLog[0] == "hit " + std::to_string(cid) + " 10");
        CHECK(clientLog.size() == 3);
        auto has = [&](const std::string& line) { for (auto& l : clientLog) if (l == line) return true; return false; };
        CHECK(has("hit " + std::to_string(cid) + " 10"));   // includeSelf: it ran on Bea's side at once
        CHECK(has("round 0 go"));
        CHECK(has("whisper 0 psst"));

        // The network clock agrees to well under a frame or two.
        Pump(hs, cs, 80);
        CHECK(hn->NetworkTime() > 0.5);
        CHECK(std::fabs(hn->NetworkTime() - cn->NetworkTime()) < 0.15);

        // Bea leaves: the host hears it at once.
        hostLog.clear();
        cn->Stop();
        Pump(hs, cs, 50, [&] { return !hostLog.empty(); });
        CHECK(hostLog.size() == 1 && hostLog[0] == "left " + std::to_string(cid));
        CHECK(hn->Players().size() == 1);
        hn->Stop();
    }

    // ---------------------------------------------------------------- errors
    {
        // A host that never answers: plain words, with the address and the wait.
        const std::uint16_t dead = FreePort();
        CHECK(dead != 0);
        Scene es("Err"); es.physicsEnabled = false;
        Scene idle("Idle"); idle.physicsEnabled = false;
        auto* en = es.CreateGameObject("Net")->AddComponent<NetworkManager>();
        en->joinTimeoutSeconds = 0.5f;
        CHECK(en->StartClient("127.0.0.1", dead));
        CHECK(en->LastError().empty());
        Pump(es, idle, 60, [&] { return !en->LastError().empty(); });
        const std::string want = "Could not reach host 127.0.0.1:" + std::to_string(dead) + " (no answer after ";
        CHECK(en->LastError().rfind(want, 0) == 0);
        std::printf("  error text: %s\n", en->LastError().c_str());
        CHECK(!en->SendEvent("hit", "1"));   // not in a session
        CHECK(en->LastError().find("not in a network session") != std::string::npos);
        en->Stop();

        // A port already in use.
        auto* a = es.CreateGameObject("A")->AddComponent<NetworkManager>();
        auto* b = idle.CreateGameObject("B")->AddComponent<NetworkManager>();
        CHECK(a->StartServer(0));
        CHECK(!b->StartServer(a->ServerPort()));
        CHECK(b->LastError().find("already in use") != std::string::npos);
        // A bad event name, and data that is too big.
        CHECK(!a->SendEvent("bad|name", "x"));
        CHECK(a->LastError().find("was not sent") != std::string::npos);
        CHECK(!a->SendEvent("big", std::string(NetworkManager::kMaxEventData + 1, 'x')));

        // A client can't speak for someone else: a forged stamp is not a fair name, so it is dropped.
        CHECK(b->StartClient("127.0.0.1", a->ServerPort()));
        CHECK(Pump(es, idle, 150, [&] { return b->Joined(); }));
        int heard = 0;
        a->OnAnyEvent([&](const NetworkManager::ReceivedEvent&) { ++heard; });
        b->SendOrderedTo(0, "__ev", "0|hit\nforged");
        b->SendOrderedTo(0, "__ev", "@99|hit");   // no name line
        Pump(es, idle, 40);
        CHECK(heard == 0);
        CHECK(b->SendEvent("hit", "real"));
        Pump(es, idle, 60, [&] { return heard > 0; });
        CHECK(heard == 1);
        a->Stop(); b->Stop();
    }

    // ---------------------------------------------------------------- auto (two copies on one PC)
    {
        const std::uint16_t port = FreePort();
        Scene s1("Copy1"); s1.physicsEnabled = false;
        Scene s2("Copy2"); s2.physicsEnabled = false;
        auto* n1 = s1.CreateGameObject("Net")->AddComponent<NetworkManager>();
        auto* n2 = s2.CreateGameObject("Net")->AddComponent<NetworkManager>();
        n1->autoStart = n2->autoStart = NetworkManager::AutoStart::Auto;
        n1->autoPort = n2->autoPort = port;
        s1.Start();
        s2.Start();
        CHECK(n1->IsServer());
        CHECK(n2->IsClient());
        CHECK(Pump(s1, s2, 150, [&] { return n2->Joined(); }));
        CHECK(n2->LastError().empty());
        n1->Stop(); n2->Stop();
    }

    // ---------------------------------------------------------------- lossy
    {
        net::SimSettings& sim = net::Simulator();
        const net::SimSettings saved = sim;
        Scene hs("LossyHost"); hs.physicsEnabled = false;
        Scene cs("LossyClient"); cs.physicsEnabled = false;
        auto* hn = hs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        auto* cn = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        CHECK(hn->StartServer(0));
        CHECK(cn->StartClient("127.0.0.1", hn->ServerPort()));
        CHECK(Pump(hs, cs, 200, [&] { return cn->Joined(); }));
        std::vector<std::string> got;
        std::string big;
        hn->OnEvent("n", [&](std::uint32_t, const std::string& d) { got.push_back(d); });
        hn->OnEvent("big", [&](std::uint32_t, const std::string& d) { big = d; });

        sim.enabled = true; sim.latencyMs = 40.0f; sim.jitterMs = 15.0f; sim.loss = 0.15f; sim.duplicate = 0.05f;
        for (int i = 0; i < 40; ++i) CHECK(cn->SendEvent("n", std::to_string(i), false));
        std::string payload(12 * 1024, ' ');
        for (std::size_t i = 0; i < payload.size(); ++i) payload[i] = (char)('a' + (i * 7) % 26);
        CHECK(cn->SendEvent("big", payload, false));
        int frames = 0;
        Pump(hs, cs, 3000, [&] { ++frames; return got.size() >= 40 && !big.empty(); });
        sim = saved;
        net::ResetSimulator();
        CHECK(got.size() == 40);
        bool inOrder = got.size() == 40;
        for (std::size_t i = 0; i < got.size() && inOrder; ++i) inOrder = got[i] == std::to_string(i);
        CHECK(inOrder);
        CHECK(big == payload);
        std::printf("  lossy: %zu of 40 events in order, big event %zu bytes, %d frames\n", got.size(), big.size(), frames);
        hn->Stop(); cn->Stop();
    }

    // ---------------------------------------------------------------- ownership (handoff, under loss) and stats
    {
        Scene hs("OwnHost"); hs.physicsEnabled = false;
        Scene cs("OwnClient"); cs.physicsEnabled = false;
        auto* hn = hs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        auto* cn = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        hn->interpolationRate = cn->interpolationRate = 0.0f;   // snap, so positions compare exactly
        GameObject* hb = hs.CreateGameObject("Ball"); auto* hns = hb->AddComponent<NetworkSync>();
        GameObject* cb = cs.CreateGameObject("Ball"); auto* cns = cb->AddComponent<NetworkSync>();
        hns->clientClaimable = cns->clientClaimable = true;
        hs.Start(); cs.Start();
        CHECK(hn->StartServer(0));
        CHECK(cn->StartClient("127.0.0.1", hn->ServerPort()));
        CHECK(Pump(hs, cs, 200, [&] { return cn->Joined() && hn->Players().size() == 2; }));
        const std::uint32_t cid = cn->LocalId();
        CHECK(hn->IsMine("Ball") && !cn->IsMine("Ball"));   // the host's by default
        CHECK(hn->OwnerOf("Ball") == -1);

        std::vector<std::string> changes;
        cn->OnOwnershipChanged([&](const std::string& id, std::uint32_t o) { changes.push_back(id + " " + std::to_string(o)); });
        net::SimSettings& sim = net::Simulator();
        const net::SimSettings saved = sim;
        sim.enabled = true; sim.latencyMs = 30.0f; sim.jitterMs = 10.0f; sim.loss = 0.15f; sim.duplicate = 0.0f;
        CHECK(hn->GiveOwnership("Ball", cid));
        CHECK(Pump(hs, cs, 300, [&] { return cn->IsMine("Ball"); }));
        CHECK(!hn->IsMine("Ball") && hn->OwnerOf("Ball") == (std::int64_t)cid);
        CHECK(changes.size() == 1 && changes[0] == "Ball " + std::to_string(cid));
        CHECK(cns->IsLocallyOwned() && !hns->IsLocallyOwned());
        // Now the client moves it and the host follows; the host's own writes no longer stick.
        cb->transform->localPosition = {2.0f, 3.0f, 4.0f};
        CHECK(Pump(hs, cs, 300, [&] { return Near(hb->transform->localPosition, {2.0f, 3.0f, 4.0f}, 0.05f); }));
        sim = saved;
        net::ResetSimulator();
        // Back to the host: the client asks for it, the ball is claimable, so the host... gives it to the
        // client again (it holds it already); the host takes it back with GiveOwnership(…, 0).
        CHECK(hn->GiveOwnership("Ball", 0));
        CHECK(Pump(hs, cs, 150, [&] { return !cn->IsMine("Ball"); }));
        CHECK(hn->IsMine("Ball"));
        cn->RequestOwnership("Ball");                      // claimable, nobody else holds it: granted
        CHECK(Pump(hs, cs, 150, [&] { return cn->IsMine("Ball"); }));
        // A client can't take what isn't claimable.
        hns->clientClaimable = false;
        CHECK(hn->GiveOwnership("Ball", 0));
        CHECK(Pump(hs, cs, 150, [&] { return !cn->IsMine("Ball"); }));
        cn->RequestOwnership("Ball");
        Pump(hs, cs, 80);
        CHECK(!cn->IsMine("Ball") && hn->IsMine("Ball"));
        // A client's transform for an object it does not hold is refused by the host.
        hb->transform->localPosition = {0.0f, 0.0f, 0.0f};
        cb->transform->localPosition = {9.0f, 9.0f, 9.0f};
        Pump(hs, cs, 60);
        CHECK(Near(hb->transform->localPosition, {0.0f, 0.0f, 0.0f}, 0.01f));
        CHECK(!hn->GiveOwnership("Ball", 999));            // no such player
        CHECK(!cn->GiveOwnership("Ball", 0));              // only the host gives

        // Stats: traffic both ways, a round trip, and (no loss now) 0% loss.
        Pump(hs, cs, 120);
        const auto cst = cn->Stats(), hst = hn->Stats();
        std::printf("  stats: client rtt %.1f ms, loss %.0f%%, in %.0f B/s, out %.0f B/s; host in %.0f B/s, out %.0f B/s\n",
                    cst.rttMs, cst.lossPercent, cst.bytesInPerSec, cst.bytesOutPerSec, hst.bytesInPerSec, hst.bytesOutPerSec);
        CHECK(cst.bytesInPerSec > 0.0f && cst.bytesOutPerSec > 0.0f);
        CHECK(hst.bytesInPerSec > 0.0f && hst.bytesOutPerSec > 0.0f);
        CHECK(cst.players == 2 && cst.networkTime > 0.0);
        CHECK(cst.lossPercent < 50.0f);

        // The bad-network settings on the Network Manager drive the simulator, and 0 turns it off again.
        net::ResetSimulator();
        hn->simulateLatencyMs = 50.0f; hn->simulateLossPercent = 10.0f;
        Pump(hs, cs, 2);
        CHECK(net::Simulator().enabled && net::Simulator().latencyMs == 50.0f && std::fabs(net::Simulator().loss - 0.1f) < 1e-4f);
        hn->simulateLatencyMs = 0.0f; hn->simulateLossPercent = 0.0f;
        Pump(hs, cs, 2);
        CHECK(!net::Simulator().enabled);
        net::Simulator() = saved;
        net::ResetSimulator();
        hn->Stop(); cn->Stop();
    }

    // ---------------------------------------------------------------- graph (Visual Script, the same graph on both peers)
    {
        const std::uint16_t port = FreePort();
        // On Start: Host or Join <port>.
        // On Player Joined: joined += 1; if Is Client: Send Network Event "hello" with "hi".
        // On Network Event "hello": got <- Data, from <- Event Sender.
        // On Update: players <- Player Count.
        const std::string graph =
            "node 1 OnStart\nnode 2 NetHostOrJoin " + std::to_string(port) + "\nexec 1 0 2\n"
            "node 3 OnPlayerJoined\nnode 4 AddVar joined\nnode 5 Const 1\nnode 6 Branch\nnode 7 NetIsClient\n"
            "node 8 NetSendEvent hello\nnode 9 Const hi\n"
            "data 4 0 5 0\ndata 6 0 7 0\ndata 8 0 9 0\nexec 3 0 4\nexec 4 0 6\nexec 6 0 8\n"
            "node 10 OnNetworkEvent hello\nnode 11 SetVar got\nnode 12 SetVar from\nnode 13 NetEventSender\n"
            "data 11 0 10 0\ndata 12 0 13 0\nexec 10 0 11\nexec 11 0 12\n"
            "node 14 OnUpdate\nnode 15 SetVar players\nnode 16 NetPlayerCount\ndata 15 0 16 0\nexec 14 0 15\n"
            "entry OnStart 1\nentry OnPlayerJoined 3\nentry OnNetworkEvent:hello 10\nentry OnUpdate 14\n";
        Scene hs("GraphHost"); hs.physicsEnabled = false;
        Scene cs("GraphClient"); cs.physicsEnabled = false;
        GameObject* hg = hs.CreateGameObject("Logic");
        GameObject* cg = cs.CreateGameObject("Logic");
        std::string err;
        CHECK(hg->AddComponent<VisualScriptComponent>()->LoadFromText(graph, &err));
        CHECK(cg->AddComponent<VisualScriptComponent>()->LoadFromText(graph, &err));
        if (!err.empty()) std::printf("  graph error: %s\n", err.c_str());
        hs.Start();
        cs.Start();
        NetworkManager* hn = hs.FindObjectOfType<NetworkManager>();
        NetworkManager* cn = cs.FindObjectOfType<NetworkManager>();
        CHECK(hn && hn->IsServer());
        CHECK(cn && cn->IsClient());
        Pump(hs, cs, 250, [&] { return GraphVar(hg, "got") == "hi" && GraphVar(cg, "got") == "hi" && GraphVar(hg, "players") == "2"; });
        const std::string cid = cn ? std::to_string(cn->LocalId()) : "?";
        CHECK(GraphVar(hg, "joined") == "1");
        CHECK(GraphVar(cg, "joined") == "1");
        CHECK(GraphVar(hg, "got") == "hi");
        CHECK(GraphVar(hg, "from") == cid);
        CHECK(GraphVar(cg, "got") == "hi");        // the sender's own graph ran it too
        CHECK(GraphVar(hg, "players") == "2");
        CHECK(GraphVar(cg, "players") == "2");
        if (hn) hn->Stop();
        if (cn) cn->Stop();
    }

    // ---------------------------------------------------------------- example (the shipped two-player sample)
    {
        const std::string path = std::string(OKAY_SOURCE_DIR) + "/examples/multiplayer/two_players.okayscene";
        const std::uint16_t port = FreePort();   // the sample uses 45000; a free port keeps parallel tests apart
        Scene a("CopyA"); Scene b("CopyB");
        std::string err;
        CHECK(SceneSerializer::LoadFromFile(a, path, &err));
        CHECK(SceneSerializer::LoadFromFile(b, path, &err));
        if (!err.empty()) std::printf("  example load error: %s\n", err.c_str());
        a.physicsEnabled = b.physicsEnabled = false;
        NetworkManager* na = a.FindObjectOfType<NetworkManager>();
        NetworkManager* nb = b.FindObjectOfType<NetworkManager>();
        CHECK(na && nb && na->autoStart == NetworkManager::AutoStart::Auto);
        if (na && nb) {
            na->autoPort = nb->autoPort = port;
            a.Start(); b.Start();
            CHECK(na->IsServer());
            CHECK(nb->IsClient());
            const bool ok = Pump(a, b, 300, [&] {
                return nb->Joined() && a.Find("Player_" + std::to_string(nb->LocalId())) && b.Find("Player_0") &&
                       a.Find("Ball") && a.Find("Ball")->transform->localPosition.x != 0.0f &&
                       na->Players().size() == 2 && nb->Players().size() == 2;
            });
            CHECK(ok);
            CHECK(na->Players().size() == 2 && nb->Players().size() == 2);
            CHECK(a.Find("LocalPlayer") && b.Find("LocalPlayer"));
            Pump(a, b, 60);
            GameObject* ba = a.Find("Ball"); GameObject* bb = b.Find("Ball");
            CHECK(ba && bb);
            if (ba && bb) {   // the client's ball follows the host's circle (smoothed, so within a little)
                const Vec3 pa = ba->transform->localPosition, pb = bb->transform->localPosition;
                std::printf("  example: host ball (%.2f, %.2f), client ball (%.2f, %.2f)\n", pa.x, pa.z, pb.x, pb.z);
                CHECK(Near(pa, pb, 1.0f));
                CHECK(std::fabs(pb.x) + std::fabs(pb.z) > 1.0f);   // it moved on the client too
            }
            na->Stop(); nb->Stop();
        }
    }

    // ---------------------------------------------------------------- okay (OkayScript on two peers)
    {
        const std::string port = std::to_string(FreePort());
        const std::string hostSrc =
            "var got = \"\"; var who = -1; var joined = 0; var left = 0; var count = 0;\n"
            "function start() { net.host_or_join(" + port + "); net.on(\"hello\", \"on_hello\"); }\n"
            "function on_hello(data, sender) { got = data; who = sender; var ps = net.players(); count = ps.Count; }\n"
            "function on_player_joined(id, name) { joined = joined + 1; }\n"
            "function on_player_left(id) { left = left + 1; }\n";
        const std::string clientSrc =
            "var sent = 0; var echo = \"\"; var host = -1;\n"
            "function start() { net.host_or_join(" + port + "); net.on(\"hello\", (data, sender) => { echo = data; }); }\n"
            "function on_player_joined(id, name) {\n"
            "  host = id;\n"
            "  if (sent == 0) { net.send(\"hello\", \"hi from \" + net.id()); sent = 1; }\n"
            "}\n";
        Scene hs("ScriptHost"); hs.physicsEnabled = false;
        Scene cs("ScriptClient"); cs.physicsEnabled = false;
        GameObject* ho = hs.CreateGameObject("Logic");
        GameObject* co = cs.CreateGameObject("Logic");
        ho->AddComponent<ScriptComponent>("okayscript")->LoadSource(hostSrc);
        co->AddComponent<ScriptComponent>("okayscript")->LoadSource(clientSrc);
        hs.Start();
        cs.Start();
        NetworkManager* hn = hs.FindObjectOfType<NetworkManager>();
        NetworkManager* cn = cs.FindObjectOfType<NetworkManager>();
        CHECK(hn && hn->IsServer());
        CHECK(cn && cn->IsClient());
        Pump(hs, cs, 250, [&] { return !ScriptVar(ho, "got").empty() && !ScriptVar(co, "echo").empty(); });
        const std::string cid = cn ? std::to_string(cn->LocalId()) : "?";
        std::printf("  okay: host got \"%s\" from %s; client echo \"%s\"\n", ScriptVar(ho, "got").c_str(),
                    ScriptVar(ho, "who").c_str(), ScriptVar(co, "echo").c_str());
        CHECK(ScriptVar(ho, "got") == "hi from " + cid);
        CHECK(ScriptVar(ho, "who") == cid);
        CHECK(ScriptVar(ho, "joined") == "1");
        CHECK(ScriptVar(ho, "count") == "2");
        CHECK(ScriptVar(co, "echo") == "hi from " + cid);
        CHECK(ScriptVar(co, "host") == "0");
        if (cn) cn->Stop();
        Pump(hs, cs, 50, [&] { return ScriptVar(ho, "left") == "1"; });
        CHECK(ScriptVar(ho, "left") == "1");
        if (hn) hn->Stop();
    }

    TEST_MAIN_RESULT();
}
