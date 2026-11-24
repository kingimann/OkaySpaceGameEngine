// Playing over the internet, over loopback with every peer in one process and an in-process relay,
// each peer behind a simulated home router (NetSim's NAT model: every peer has its own public port,
// unsolicited datagrams are dropped):
//
//   cone       host and client behind port-restricted cone routers: the relay passes the two public
//              addresses on, both punch at once, the direct path opens; the game then goes on with the
//              relay not running at all (so it really is direct); Stats and the overlay say "direct"
//   symmetric  both behind symmetric routers (a new port per destination): the punch fails within
//              punchTimeoutSeconds and the game stays on the relay; Stats and the overlay say "relayed"
//   private    directConnect off: the relay passes no address on (the host never learns the client's)
//   three      a host and two clients, one on a cone router (direct) and one symmetric (relayed): events
//              and a shared variable reach all three; the host's Stats count 1 direct and 1 relayed
//   migrate    a host and three clients through the relay: the host leaves; the lowest id takes the host's
//              place on the relay (same room code), the others keep their ids, the shared variable carries
//              on; then that new host crashes and the next one takes over once the relay sees it silent
//   code       HostOnline makes a room code; JoinCode joins it (any case, with a dash); the join link;
//              a code that is taken
//   graph      the Host Online and Join By Code nodes
//   okay       net.relay, net.host_online, net.join_code, net.room_code, net.path in an .okay script
//   errors     no relay set, a GitHub relay refused, a bad code, an unreachable relay, a code nobody hosts
#include "test_framework.hpp"
#include "net_test_util.hpp"
#include <Okay.hpp>
#include "okay/Net/JoinLink.hpp"
#include "okay/Net/Net.hpp"
#include "okay/Net/NetSim.hpp"
#include "okay/Net/RelayServer.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
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

struct Peer {
    std::unique_ptr<Scene> scene;
    NetworkManager* nm = nullptr;
    std::vector<std::string> got;   // "name=data" of every network event heard
    explicit Peer(const std::string& name, net::NatType nat = net::NatType::None) : scene(std::make_unique<Scene>(name)) {
        scene->physicsEnabled = false;
        nm = scene->CreateGameObject("Net")->AddComponent<NetworkManager>();
        nm->simulateNat = nat;
        nm->SetLocalName(name);
        nm->OnAnyEvent([this](const NetworkManager::ReceivedEvent& e) { got.push_back(e.name + "=" + e.data); });
        scene->Start();
    }
    bool Heard(const std::string& s) const {
        for (const auto& g : got) if (g == s) return true;
        return false;
    }
};

struct Rig {
    net::RelayServer relay;
    bool relayOn = true;   // false: the relay is not run (it hears nothing, forwards nothing)
    Rig() { CHECK(relay.Start(0)); }
    std::string Addr() const { return "127.0.0.1:" + std::to_string(relay.Port()); }
    bool Pump(const std::vector<Peer*>& peers, int frames, const std::function<bool()>& done = {}, int* used = nullptr) {
        for (int i = 0; i < frames; ++i) {
            if (relayOn) relay.Poll(kFrame);
            for (Peer* p : peers) p->scene->Update(kFrame);
            if (used) *used = i + 1;
            if (done && done()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return done ? done() : true;
    }
};

} // namespace

int main() {
    RUN_SUITE("net_online");
#ifdef _WIN32
    _putenv("OKAY_RELAY=");
#else
    unsetenv("OKAY_RELAY");
#endif

    // ---------------------------------------------------------------- cone
    {
        Rig rig;
        Peer host("ConeHost", net::NatType::PortRestricted), client("ConeClient", net::NatType::PortRestricted);
        CHECK(host.nm->HostViaRelay("127.0.0.1", rig.relay.Port(), "CONE1"));
        CHECK(client.nm->JoinViaRelay("127.0.0.1", rig.relay.Port(), "CONE1"));
        CHECK(net::SimulatedNat(host.nm->Socket()) == net::NatType::PortRestricted);
        int frames = 0;
        const bool open = rig.Pump({&host, &client}, 600, [&] {
            return client.nm->Joined() && client.nm->DirectToHost() && host.nm->DirectPeers() == 1;
        }, &frames);
        std::printf("  cone: joined and direct after %d frames (%.2f s); relay sent %llu addresses; %d punch(es) opened\n",
                    frames, frames * kFrame, (unsigned long long)rig.relay.PeerAddrsSent(), client.nm->PunchesOpened());
        CHECK(open);
        CHECK(rig.relay.PeerAddrsSent() >= 2);
        CHECK(client.nm->Stats().direct && !client.nm->Stats().relayed);
        CHECK(host.nm->Stats().directPeers == 1 && host.nm->Stats().relayedPeers == 0);
        CHECK(client.nm->PathText() == "direct (punched)");
        CHECK(HasLine(client.nm->StatsLines(), "Path: direct (punched) via relay"));
        CHECK(HasLine(host.nm->StatsLines(), "1 direct, 0 via relay"));
        CHECK(HasLine(host.nm->StatsLines(), "ConeClient: ") && HasLine(host.nm->StatsLines(), ", direct"));
        // The relay knows the client by its router's public port (its socket itself never sent a datagram).
        const std::uint16_t pub = net::SimulatedNatPort(client.nm->Socket(), net::Endpoint{0x7F000001u, rig.relay.Port()});
        CHECK(pub != 0 && rig.relay.SlotOf(net::Endpoint{0x7F000001u, pub}) != 0);
        CHECK(client.nm->Socket().LocalPort() != pub);
        // Now turn the relay off: the game goes on over the direct path alone.
        rig.relayOn = false;
        rig.Pump({&host, &client}, 25);
        CHECK(client.nm->SendEvent("ping", "from client"));
        CHECK(host.nm->SendEvent("pong", "from host"));
        const bool flowed = rig.Pump({&host, &client}, 150, [&] {
            return host.Heard("ping=from client") && client.Heard("pong=from host");
        });
        std::printf("  cone: with the relay stopped, events both ways %s\n", flowed ? "arrived" : "did NOT arrive");
        CHECK(flowed);
        CHECK(client.nm->Joined() && client.nm->DirectToHost());
        host.nm->Stop(); client.nm->Stop();
    }

    // ---------------------------------------------------------------- symmetric
    {
        Rig rig;
        Peer host("SymHost", net::NatType::Symmetric), client("SymClient", net::NatType::Symmetric);
        host.nm->punchTimeoutSeconds = client.nm->punchTimeoutSeconds = 1.5f;
        const std::size_t dropsBefore = net::SimulatedNatDrops();
        CHECK(host.nm->HostViaRelay("127.0.0.1", rig.relay.Port(), "SYM1"));
        CHECK(client.nm->JoinViaRelay("127.0.0.1", rig.relay.Port(), "SYM1"));
        CHECK(rig.Pump({&host, &client}, 400, [&] { return client.nm->Joined(); }));
        int frames = 0;
        const bool gaveUp = rig.Pump({&host, &client}, 400, [&] {
            return client.nm->PunchesFailed() >= 1 && host.nm->PunchesFailed() >= 1;
        }, &frames);
        std::printf("  symmetric: the punch gave up after %d more frames; routers dropped %zu datagrams; direct %d/%d\n",
                    frames, net::SimulatedNatDrops() - dropsBefore, client.nm->PunchesOpened(), host.nm->PunchesOpened());
        CHECK(gaveUp);
        CHECK(net::SimulatedNatDrops() > dropsBefore);
        CHECK(client.nm->PunchesOpened() == 0 && host.nm->PunchesOpened() == 0);
        CHECK(!client.nm->DirectToHost());
        CHECK(client.nm->Stats().relayed && !client.nm->Stats().direct);
        CHECK(client.nm->PathText() == "relayed");
        CHECK(HasLine(client.nm->StatsLines(), "Path: relayed via relay"));
        CHECK(HasLine(host.nm->StatsLines(), "0 direct, 1 via relay"));
        // ...and the game carries on through the relay.
        CHECK(client.nm->SendEvent("ping", "via relay"));
        CHECK(rig.Pump({&host, &client}, 150, [&] { return host.Heard("ping=via relay"); }));
        // A client no longer asks for the host's address once it gave up (the relay stops sending it).
        const auto sent = rig.relay.PeerAddrsSent();
        rig.Pump({&host, &client}, 150);
        CHECK(rig.relay.PeerAddrsSent() == sent);
        host.nm->Stop(); client.nm->Stop();
    }

    // ---------------------------------------------------------------- private
    {
        Rig rig;
        Peer host("PrivHost", net::NatType::PortRestricted), client("PrivClient", net::NatType::PortRestricted);
        client.nm->directConnect = false;
        CHECK(host.nm->HostViaRelay("127.0.0.1", rig.relay.Port(), "PRIV1"));
        CHECK(client.nm->JoinViaRelay("127.0.0.1", rig.relay.Port(), "PRIV1"));
        CHECK(rig.Pump({&host, &client}, 400, [&] { return client.nm->Joined(); }));
        rig.Pump({&host, &client}, 150);
        CHECK(rig.relay.PeerAddrsSent() == 0);   // nobody was told anybody's address
        CHECK(host.nm->DirectPeers() == 0 && client.nm->PathText() == "relayed");
        host.nm->Stop(); client.nm->Stop();
    }

    // ---------------------------------------------------------------- three
    {
        Rig rig;
        Peer host("TriHost", net::NatType::PortRestricted), a("TriCone", net::NatType::PortRestricted),
            b("TriSym", net::NatType::Symmetric);
        host.nm->punchTimeoutSeconds = a.nm->punchTimeoutSeconds = b.nm->punchTimeoutSeconds = 1.5f;
        b.nm->simulateNat = net::NatType::Symmetric;
        CHECK(host.nm->HostViaRelay("127.0.0.1", rig.relay.Port(), "TRIO"));
        CHECK(a.nm->JoinViaRelay("127.0.0.1", rig.relay.Port(), "TRIO"));
        CHECK(b.nm->JoinViaRelay("127.0.0.1", rig.relay.Port(), "TRIO"));
        const bool settled = rig.Pump({&host, &a, &b}, 800, [&] {
            return a.nm->Joined() && b.nm->Joined() && a.nm->DirectToHost() && b.nm->PunchesFailed() >= 1 &&
                   host.nm->Players().size() == 3 && a.nm->Players().size() == 3 && b.nm->Players().size() == 3;
        });
        CHECK(settled);
        const auto hs = host.nm->Stats();
        std::printf("  three: host sees %d direct, %d relayed; players %d/%d/%d\n", hs.directPeers, hs.relayedPeers,
                    (int)host.nm->Players().size(), (int)a.nm->Players().size(), (int)b.nm->Players().size());
        CHECK(hs.directPeers == 1 && hs.relayedPeers == 1);
        CHECK(HasLine(host.nm->StatsLines(), "1 direct, 1 via relay"));
        CHECK(b.nm->SendEvent("hello", "from the relayed one"));
        CHECK(a.nm->SendEvent("hello", "from the direct one"));
        host.nm->SetVar("score", "7");
        const bool all = rig.Pump({&host, &a, &b}, 250, [&] {
            for (Peer* p : {&host, &a, &b})
                if (!p->Heard("hello=from the relayed one") || !p->Heard("hello=from the direct one") || p->nm->GetVar("score") != "7")
                    return false;
            return true;
        });
        CHECK(all);
        host.nm->Stop(); a.nm->Stop(); b.nm->Stop();
    }

    // ---------------------------------------------------------------- migrate
    {
        Rig rig;
        std::vector<std::unique_ptr<Peer>> ps;
        const char* names[] = {"MigHost", "MigA", "MigB", "MigC"};
        for (const char* nmName : names) {
            ps.push_back(std::make_unique<Peer>(nmName, net::NatType::PortRestricted));
            ps.back()->nm->serverTimeoutSeconds = 2.0f;
            ps.back()->nm->migrationJoinTimeoutSeconds = 3.0f;
        }
        std::vector<Peer*> all;
        for (auto& p : ps) all.push_back(p.get());
        Peer& h = *ps[0];
        CHECK(h.nm->HostViaRelay("127.0.0.1", rig.relay.Port(), "MIGR8"));
        for (int i = 1; i < 4; ++i) {   // in order, so their ids are 1, 2, 3
            CHECK(ps[i]->nm->JoinViaRelay("127.0.0.1", rig.relay.Port(), "MIGR8"));
            CHECK(rig.Pump(all, 300, [&] { return ps[i]->nm->Joined(); }));
        }
        h.nm->SetVar("level", "forest");
        const bool offered = rig.Pump(all, 300, [&] {
            for (int i = 1; i < 4; ++i)
                if (!ps[i]->nm->MigrationOffered() || ps[i]->nm->GetVar("level") != "forest" || ps[i]->nm->Players().size() != 4) return false;
            return true;
        });
        CHECK(offered);
        std::vector<std::uint32_t> ids;
        for (int i = 1; i < 4; ++i) ids.push_back(ps[i]->nm->LocalId());
        // The list names relay slots, never addresses.
        for (const auto& [id, ep] : ps[2]->nm->MigrationList()) CHECK(ep.port == net::kRelaySlotPort);
        const std::uint32_t slotA = rig.relay.SlotOf(net::Endpoint{0x7F000001u, net::SimulatedNatPort(ps[1]->nm->Socket(), net::Endpoint{0x7F000001u, rig.relay.Port()})});
        CHECK(slotA != 0);

        // The host leaves: A (the lowest id) takes the host's place on the relay; B and C join it there.
        h.nm->Stop();
        std::vector<Peer*> left = {ps[1].get(), ps[2].get(), ps[3].get()};
        int frames = 0;
        const bool moved = rig.Pump(left, 600, [&] {
            return ps[1]->nm->IsServer() && ps[2]->nm->Joined() && ps[3]->nm->Joined() &&
                   ps[2]->nm->HostMigrations() == 1 && ps[3]->nm->HostMigrations() == 1 &&
                   ps[1]->nm->Players().size() == 3;
        }, &frames);
        std::printf("  migrate: the host left; player %u hosts on the relay after %d frames; successions %llu; host slot %u (A's %u)\n",
                    ids[0], frames, (unsigned long long)rig.relay.Successions(), rig.relay.HostSlotOf("MIGR8"), slotA);
        CHECK(moved);
        CHECK(rig.relay.Successions() == 1);
        CHECK(rig.relay.HostSlotOf("MIGR8") == slotA);
        CHECK(ps[1]->nm->RoomCode() == "MIGR8" && ps[2]->nm->RoomCode() == "MIGR8");
        CHECK(ps[2]->nm->LocalId() == ids[1] && ps[3]->nm->LocalId() == ids[2]);
        CHECK(ps[2]->nm->GetVar("level") == "forest" && ps[1]->nm->GetVar("level") == "forest");
        CHECK(ps[2]->nm->SendEvent("after", "move"));
        CHECK(rig.Pump(left, 200, [&] { return ps[1]->Heard("after=move") && ps[3]->Heard("after=move"); }));
        // Direct paths to the new host are punched again.
        CHECK(rig.Pump(left, 400, [&] { return ps[2]->nm->DirectToHost() && ps[3]->nm->DirectToHost(); }));

        // Now the new host crashes (stops answering): the next lowest takes over once the relay sees it silent.
        const bool offered2 = rig.Pump(left, 300, [&] { return ps[2]->nm->MigrationOffered() && ps[3]->nm->MigrationOffered(); });
        CHECK(offered2);
        std::vector<Peer*> last = {ps[2].get(), ps[3].get()};
        frames = 0;
        const bool failover = rig.Pump(last, 900, [&] {
            return ps[2]->nm->IsServer() && ps[3]->nm->Joined() && ps[3]->nm->HostMigrations() == 2 &&
                   ps[2]->nm->Players().size() == 2;
        }, &frames);
        std::printf("  migrate: the new host crashed; player %u hosts after %d frames (%.1f s); successions %llu\n",
                    ids[1], frames, frames * kFrame, (unsigned long long)rig.relay.Successions());
        CHECK(failover);
        CHECK(rig.relay.Successions() == 2);
        CHECK(ps[3]->nm->LocalId() == ids[2]);
        CHECK(ps[3]->nm->GetVar("level") == "forest");
        for (auto& p : ps) p->nm->Stop();
    }

    // ---------------------------------------------------------------- code
    {
        Rig rig;
        Peer host("CodeHost", net::NatType::PortRestricted), client("CodeClient", net::NatType::PortRestricted);
        host.nm->relayServer = client.nm->relayServer = rig.Addr();
        CHECK(host.nm->OnlineRelay() == rig.Addr());
        CHECK(host.nm->HostOnline());
        const std::string code = host.nm->RoomCode();
        std::printf("  code: hosting under room code %s\n", code.c_str());
        CHECK(code.size() == 6);
        CHECK(code.find_first_of("01OI") == std::string::npos);
        CHECK(NetworkManager::NormalizeRoomCode(" k7q-29x ") == "K7Q29X");
        CHECK(NetworkManager::NormalizeRoomCode("ab").empty() && NetworkManager::NormalizeRoomCode("ab!cd").empty());
        std::string typed;   // the way a friend types it: lower case, a dash in the middle
        for (char c : code) typed += (char)std::tolower((unsigned char)c);
        typed.insert(3, "-");
        CHECK(client.nm->JoinCode(typed));
        CHECK(rig.Pump({&host, &client}, 400, [&] { return client.nm->Joined() && host.nm->Players().size() == 2; }));
        CHECK(client.nm->RoomCode() == code);
        JoinLink l;
        CHECK(JoinLink::Parse(host.nm->OnlineJoinLink(), l) && l.Relayed() && l.code == code && l.relay == rig.Addr());
        // A code that is already hosted: a plain error for the second host.
        Peer other("CodeOther");
        other.nm->relayServer = rig.Addr();
        CHECK(other.nm->HostOnline(code));
        rig.Pump({&host, &client, &other}, 200, [&] { return !other.nm->IsServer(); });
        std::printf("  code: a second host for %s: \"%s\"\n", code.c_str(), other.nm->LastError().c_str());
        CHECK(!other.nm->IsServer());
        CHECK(other.nm->LastError().find("already in use") != std::string::npos);
        CHECK(client.nm->Joined());   // the first game is untouched
        // The C++ front door does the same.
        Net::Use(*other.scene);
        other.nm->relayServer = rig.Addr();
        CHECK(Net::JoinCode(code));
        CHECK(rig.Pump({&host, &client, &other}, 400, [&] { return other.nm->Joined() && host.nm->Players().size() == 3; }));
        CHECK(Net::RoomCode() == code);
        host.nm->Stop(); client.nm->Stop(); other.nm->Stop();
    }

    // ---------------------------------------------------------------- graph
    {
        Rig rig;
        Peer host("GraphHost"), client("GraphClient");
        host.nm->relayServer = client.nm->relayServer = rig.Addr();
        GameObject* hg = host.scene->CreateGameObject("Logic");
        GameObject* cg = client.scene->CreateGameObject("Logic");
        std::string err;
        // Host: On Start -> Host Online (room "GRAPH1") -> code <- Room Code. Client: On Start -> Join By Code "graph-1".
        CHECK(hg->AddComponent<VisualScriptComponent>()->LoadFromText(
            "node 1 OnStart\nnode 2 NetHostOnline\nnode 3 Const GRAPH1\nnode 4 SetVar code\n"
            "data 2 0 3 0\ndata 4 0 2 0\nexec 1 0 2\nexec 2 0 4\nentry OnStart 1\n", &err));
        CHECK(cg->AddComponent<VisualScriptComponent>()->LoadFromText(
            "node 1 OnStart\nnode 2 NetJoinCode\nnode 3 Const graph-1\ndata 2 0 3 0\nexec 1 0 2\n"
            "node 4 OnUpdate\nnode 5 SetVar err\nnode 6 NetLastError\ndata 5 0 6 0\nexec 4 0 5\n"
            "entry OnStart 1\nentry OnUpdate 4\n", &err));
        if (!err.empty()) std::printf("  graph error: %s\n", err.c_str());
        CHECK(rig.Pump({&host, &client}, 400, [&] { return client.nm->Joined(); }));
        std::printf("  graph: code \"%s\", joined %d, error \"%s\"\n", GraphVar(hg, "code").c_str(), (int)client.nm->Joined(),
                    GraphVar(cg, "err").c_str());
        CHECK(GraphVar(hg, "code") == "GRAPH1");
        CHECK(host.nm->IsServer() && host.nm->RoomCode() == "GRAPH1");
        CHECK(client.nm->Joined() && GraphVar(cg, "err").empty());
        host.nm->Stop(); client.nm->Stop();
    }

    // ---------------------------------------------------------------- okay (OkayScript)
    {
        Rig rig;
        Scene hs("ScriptHost"); hs.physicsEnabled = false;
        Scene cs("ScriptClient"); cs.physicsEnabled = false;
        const std::string relay = "\"" + rig.Addr() + "\"";
        GameObject* ho = hs.CreateGameObject("Logic");
        GameObject* co = cs.CreateGameObject("Logic");
        ho->AddComponent<ScriptComponent>("okayscript")->LoadSource(
            "var code = \"\"; var relay = \"\";\n"
            "function start() { relay = net.relay(" + relay + "); code = net.host_online(\"okay42\"); }\n");
        co->AddComponent<ScriptComponent>("okayscript")->LoadSource(
            "var ok = 0; var path = \"\"; var room = \"\";\n"
            "function start() { net.relay(" + relay + "); if (net.join_code(\"OKAY42\")) { ok = 1; } }\n"
            "function update(dt) { if (net.connected()) { path = net.path(); room = net.room_code(); } }\n");
        hs.Start();
        cs.Start();
        NetworkManager* hn = hs.FindObjectOfType<NetworkManager>();
        NetworkManager* cn = cs.FindObjectOfType<NetworkManager>();
        CHECK(hn && cn);
        bool joined = false;
        for (int i = 0; i < 400 && !joined; ++i) {
            rig.relay.Poll(kFrame);
            hs.Update(kFrame); cs.Update(kFrame);
            joined = !ScriptVar(co, "room").empty();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        std::printf("  okay: code \"%s\", relay \"%s\", client ok %s, room \"%s\", path \"%s\"\n", ScriptVar(ho, "code").c_str(),
                    ScriptVar(ho, "relay").c_str(), ScriptVar(co, "ok").c_str(), ScriptVar(co, "room").c_str(), ScriptVar(co, "path").c_str());
        CHECK(joined);
        CHECK(ScriptVar(ho, "code") == "OKAY42");
        CHECK(ScriptVar(ho, "relay") == rig.Addr());
        CHECK(ScriptVar(co, "ok") == "1");
        CHECK(ScriptVar(co, "room") == "OKAY42");
        CHECK(ScriptVar(co, "path").compare(0, 6, "direct") == 0 || ScriptVar(co, "path").compare(0, 7, "relayed") == 0);
        if (hn) hn->Stop();
        if (cn) cn->Stop();
    }

    // ---------------------------------------------------------------- errors
    {
        Peer p("Errors");
        CHECK(p.nm->OnlineRelay().empty() || p.nm->OnlineRelay().find("github") == std::string::npos);
        if (p.nm->OnlineRelay().empty()) {   // (a build made with a default relay has one)
            CHECK(!p.nm->HostOnline());
            std::printf("  errors: no relay: \"%s\"\n", p.nm->LastError().c_str());
            CHECK(p.nm->LastError().find("needs a relay server") != std::string::npos);
            CHECK(!p.nm->JoinCode("K7Q29X"));
            CHECK(p.nm->LastError().find("needs a relay server") != std::string::npos);
        }
        p.nm->relayServer = "relay.github.io:45100";   // never a GitHub address
        CHECK(p.nm->OnlineRelay().empty() || p.nm->OnlineRelay() != p.nm->relayServer);
        p.nm->relayServer = "127.0.0.1:45100";
        CHECK(!p.nm->JoinCode("!?"));
        std::printf("  errors: bad code: \"%s\"\n", p.nm->LastError().c_str());
        CHECK(p.nm->LastError().find("is not a room code") != std::string::npos);

        // A relay nobody runs: the port of a socket that is closed again.
        std::uint16_t dead = 0;
        { net::UdpSocket s; s.Open(); s.Bind(0); dead = s.LocalPort(); s.Close(); }
        p.nm->relayServer = "127.0.0.1:" + std::to_string(dead);
        p.nm->relayTimeoutSeconds = 1.0f;
        CHECK(p.nm->JoinCode("K7Q29X"));
        for (int i = 0; i < 120 && p.nm->LastError().empty(); ++i) p.scene->Update(kFrame);
        std::printf("  errors: unreachable relay: \"%s\"\n", p.nm->LastError().c_str());
        CHECK(p.nm->LastError().find("Could not reach the relay server") != std::string::npos);
        CHECK(!p.nm->IsClient());

        // A live relay, but nobody hosts that code.
        Rig rig;
        p.nm->relayServer = rig.Addr();
        CHECK(p.nm->JoinCode("NOPE42"));
        rig.Pump({&p}, 150, [&] { return !p.nm->LastError().empty(); });
        std::printf("  errors: unknown code: \"%s\"\n", p.nm->LastError().c_str());
        CHECK(p.nm->LastError().find("No game is hosted with code NOPE42") != std::string::npos);

        // An unknown relay name.
        p.nm->relayServer = "no-such-relay.invalid:45100";
        CHECK(!p.nm->JoinCode("K7Q29X"));
        std::printf("  errors: unknown relay name: \"%s\"\n", p.nm->LastError().c_str());
        CHECK(p.nm->LastError().find("Could not find the relay server") != std::string::npos);
        p.nm->Stop();
    }

    net::ClearSimulatedNats();
    TEST_MAIN_RESULT();
}
