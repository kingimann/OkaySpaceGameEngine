#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Net/NetSim.hpp"
#include "okay/Net/RelayServer.hpp"
#include "okay/Net/UdpSocket.hpp"
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace okay;

// A peer's address changing in the middle of a relayed session (a NAT rebinding, or a phone moving
// from Wi-Fi to mobile data), with and without a lossy link (docs/multiplayer_audit.md):
//   * the host moves: the relay keeps its slot (its host key proves who it is), clients notice nothing;
//   * a client moves: the relay gives it a new slot (the host's old one leads nowhere), the client
//     sees that in the next welcome, a second or two later, and says the connection is lost at once
//     (not after the 10 s silence timeout); the automatic reconnect then resumes it (same peer id,
//     score) through its new address; with autoReconnect off it is told, not left hanging;
//   * the same under 20% loss, delay and duplication from NetSim.
// "NAT" here is a middlebox between a peer and the relay that can swap the socket it sends from,
// which the relay sees as a new source address; the old mapping then goes nowhere.

namespace {
void Nap() { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }

struct NatBox {
    net::UdpSocket front;
    net::Endpoint relay;
    struct Leg { net::Endpoint inside; std::unique_ptr<net::UdpSocket> up; };
    std::vector<Leg> legs;
    int rebinds = 0;
    explicit NatBox(std::uint16_t relayPort) { front.Open(); front.Bind(0); relay = {0x7F000001u, relayPort}; }
    std::uint16_t Port() const { return front.LocalPort(); }
    std::uint16_t OutsidePort() const { return legs.empty() ? 0 : legs[0].up->LocalPort(); }
    void Pump() {
        std::uint8_t buf[65536]; net::Endpoint from; int n;
        while ((n = front.RecvFrom(buf, sizeof buf, from)) > 0) {
            std::size_t i = 0;
            while (i < legs.size() && !(legs[i].inside == from)) ++i;
            if (i == legs.size()) {
                Leg l; l.inside = from; l.up = std::make_unique<net::UdpSocket>(); l.up->Open(); l.up->Bind(0);
                legs.push_back(std::move(l));
            }
            legs[i].up->SendTo(relay, buf, (std::size_t)n);
        }
        for (auto& l : legs)
            while ((n = l.up->RecvFrom(buf, sizeof buf, from)) > 0) front.SendTo(l.inside, buf, (std::size_t)n);
    }
    // The mapping changes: the peer now reaches the relay from a new port; the old one is gone.
    void Rebind() {
        for (auto& l : legs) { l.up = std::make_unique<net::UdpSocket>(); l.up->Open(); l.up->Bind(0); }
        ++rebinds;
    }
};

struct Rig {
    net::RelayServer relay;
    std::unique_ptr<NatBox> natH, natC;
    Scene hs{"Host"}, cs{"Client"};
    NetworkManager* host = nullptr;
    NetworkManager* cli = nullptr;
    Rig() {
        hs.physicsEnabled = false; cs.physicsEnabled = false;
        host = hs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        cli = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        host->interpolationRate = 0.0f; cli->interpolationRate = 0.0f;
        CHECK(relay.Start(0));
        natH = std::make_unique<NatBox>(relay.Port());
        natC = std::make_unique<NatBox>(relay.Port());
    }
    ~Rig() { Link(false); }
    static void Link(bool lossy) {
        auto& sim = net::Simulator();
        sim.enabled = lossy; sim.latencyMs = 20.0f; sim.jitterMs = 10.0f; sim.loss = lossy ? 0.2f : 0.0f;
        sim.duplicate = lossy ? 0.15f : 0.0f;
        if (!lossy) net::ResetSimulator();
    }
    void Join(const std::string& code) {
        CHECK(host->HostViaRelay("127.0.0.1", natH->Port(), code));
        CHECK(cli->JoinViaRelay("127.0.0.1", natC->Port(), code));
        hs.Start(); cs.Start();
        CHECK(Until([&] { return cli->Joined() && host->Peers().size() >= 1; }, 3000));
    }
    void Step(float dt) {
        relay.Poll(dt); natH->Pump(); natC->Pump();
        hs.Update(dt); natH->Pump(); relay.Poll(0.0f); natC->Pump();
        cs.Update(dt); natC->Pump();
        simTime += dt;
        Nap();
    }
    bool Until(const std::function<bool()>& done, int steps, float dt = 0.02f) {
        for (int i = 0; i < steps && !done(); ++i) Step(dt);
        return done();
    }
    double simTime = 0.0;
};

struct Got {
    int n = 0;
    void Drain(NetworkManager* m, const std::string& channel) {
        NetworkManager::NetMessage msg;
        while (m->PopMessage(msg)) if (msg.channel == channel) ++n;
    }
};
} // namespace

int main() {
    RUN_SUITE("net_relay_nat");
    auto sink = Log::sink;
    Log::sink = [](Log::Level, const std::string&) {};

    for (const bool lossy : {false, true}) {
        const char* tag = lossy ? "lossy " : "clean ";
        const int budget = lossy ? 6000 : 3000;   // frames of 20 ms

        // ---- The host's address changes: the relay keeps its slot; nobody reconnects ----
        {
            Rig r;
            r.Join(lossy ? "NAT-H-L" : "NAT-H");
            Rig::Link(lossy);
            const std::uint32_t id = r.cli->LocalId();
            r.host->SetPeerScore(id, 12);
            const int reconnectsBefore = r.cli->Reconnects();
            r.natH->Rebind();
            // Traffic flows again both ways, with no new session.
            Got toHost, toClient;
            int sent = 0;
            bool ok = r.Until([&] {
                if (sent < 400 && sent % 20 == 0) { r.cli->SendReliable("up", "u" + std::to_string(sent)); r.host->SendReliableTo(id, "down", "d" + std::to_string(sent)); }
                ++sent;
                toHost.Drain(r.host, "up"); toClient.Drain(r.cli, "down");
                return toHost.n >= 5 && toClient.n >= 5;
            }, budget);
            CHECK(ok);
            CHECK(r.cli->Joined() && !r.cli->ConnectionLost() && !r.cli->Reconnecting());
            CHECK(r.cli->Reconnects() == reconnectsBefore);
            CHECK(r.cli->LocalId() == id && r.host->Peers().size() == 1 && r.host->PeerScore(id) == 12);
            if (!ok) std::printf("  %shost rebind: no recovery\n", tag);
        }

        // ---- A client's address changes: it is lost, then resumes through its new address ----
        {
            Rig r;
            r.cli->autoReconnect = true;
            r.cli->reconnectDelaySeconds = 0.5f;
            r.Join(lossy ? "NAT-C-L" : "NAT-C");
            Rig::Link(lossy);
            const std::uint32_t id = r.cli->LocalId();
            r.host->SetPeerScore(id, 21);
            r.natC->Rebind();
            const double t0 = r.simTime;
            CHECK(r.Until([&] { return r.cli->Reconnecting() || r.cli->ConnectionLost(); }, budget));   // it notices
            CHECK(r.simTime - t0 < (lossy ? 12.0 : 4.0));                       // from the relay's welcome, not the silence timeout
            CHECK(r.cli->ConnectionLostReason().find("address changed") != std::string::npos);
            CHECK(r.Until([&] { return r.cli->Joined() && r.cli->Reconnects() == 1; }, budget * 2));
            std::printf("  %sclient rebind: back after %.1f s of game time\n", tag, r.simTime - t0);
            CHECK(r.cli->Resumed() && r.cli->LocalId() == id);
            CHECK(r.host->PeerScore(id) == 21 && r.host->Peers().size() == 1);
            // And it works: a message each way.
            Got toHost, toClient;
            r.cli->SendReliable("up", "again"); r.host->SendReliableTo(id, "down", "again");
            CHECK(r.Until([&] { toHost.Drain(r.host, "up"); toClient.Drain(r.cli, "down"); return toHost.n >= 1 && toClient.n >= 1; }, budget));
            CHECK(r.simTime - t0 < (lossy ? 30.0 : 8.0));
        }

        // ---- A direct (no relay) session: nothing tells the client its address changed, so it is the
        //      silence timeout (10 s) that ends the session, and the reconnect then resumes it ----
        {
            Scene ds("Host"), dc("Client");
            ds.physicsEnabled = false; dc.physicsEnabled = false;
            auto* h = ds.CreateGameObject("Net")->AddComponent<NetworkManager>();
            auto* c = dc.CreateGameObject("Net")->AddComponent<NetworkManager>();
            h->interpolationRate = 0.0f; c->interpolationRate = 0.0f;
            c->autoReconnect = true; c->reconnectDelaySeconds = 0.5f;
            CHECK(h->StartServer(0));
            NatBox nat(h->ServerPort());
            CHECK(c->StartClient("127.0.0.1", nat.Port()));
            ds.Start(); dc.Start();
            double t = 0.0;
            auto step = [&](float dt) { nat.Pump(); ds.Update(dt); nat.Pump(); dc.Update(dt); nat.Pump(); t += dt; Nap(); };
            for (int i = 0; i < 3000 && !(c->Joined() && h->Peers().size() >= 1); ++i) step(0.02f);
            CHECK(c->Joined());
            Rig::Link(lossy);
            const std::uint32_t id = c->LocalId();
            h->SetPeerScore(id, 17);
            nat.Rebind();
            const double t0 = t;
            for (int i = 0; i < budget * 3 && !(c->Reconnects() == 1 && c->Joined()); ++i) step(0.02f);
            std::printf("  %sdirect client rebind: back after %.1f s of game time\n", tag, t - t0);
            CHECK(c->Joined() && c->Reconnects() == 1);
            CHECK(c->Resumed() && c->LocalId() == id && h->PeerScore(id) == 17 && h->Peers().size() == 1);
            CHECK(t - t0 < (lossy ? 45.0 : 25.0));
            Rig::Link(false);
        }

        // ---- Without autoReconnect the player is told, not left hanging ----
        {
            Rig r;
            r.Join(lossy ? "NAT-N-L" : "NAT-N");
            Rig::Link(lossy);
            r.natC->Rebind();
            const double t0 = r.simTime;
            CHECK(r.Until([&] { return r.cli->ConnectionLost(); }, budget * 2));
            CHECK(r.simTime - t0 < (lossy ? 12.0 : 4.0));
            CHECK(!r.cli->ConnectionLostReason().empty() && !r.cli->Reconnecting() && !r.cli->IsConnected());
        }
    }

    Log::sink = sink;
    TEST_MAIN_RESULT();
}
