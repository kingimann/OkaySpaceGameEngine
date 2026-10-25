#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Net/RelayServer.hpp"
#include "okay/Net/RelayProtocol.hpp"
#include "okay/Net/Packet.hpp"
#include <chrono>
#include <cstring>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace okay;
using namespace okay::net;

// The relay is reachable from the internet, so it must not let one session's
// peers reach another's, must not hand out guessable slots, must stay bounded
// under floods, and must survive any datagram.

namespace {

struct Peer {
    UdpSocket sock;
    Endpoint relay;
    Endpoint self;
    explicit Peer(std::uint16_t relayPort) {
        sock.Open(); sock.Bind(0);
        relay = {0x7F000001u, relayPort};
        self = {0x7F000001u, sock.LocalPort()};
    }
    void Hello(RelayRole role, const std::string& code) {
        Packet p(RelayHello); p.Write(static_cast<std::uint8_t>(role)); p.Write(code);
        sock.SendTo(relay, p.Data(), p.Size());
    }
    void Data(std::uint32_t dest, const std::string& payload) {
        Packet p(RelayData); p.Write(dest);
        std::vector<std::uint8_t> d(p.Data(), p.Data() + p.Size());
        d.insert(d.end(), payload.begin(), payload.end());
        sock.SendTo(relay, d.data(), d.size());
    }
    // Every RelayData payload received (welcomes skipped).
    std::vector<std::string> Received() {
        std::vector<std::string> out;
        std::uint8_t buf[2048]; Endpoint from;
        int n;
        while ((n = sock.RecvFrom(buf, sizeof buf, from)) > 0)
            if (buf[0] == RelayData && n >= 5) out.emplace_back(reinterpret_cast<char*>(buf) + 5, n - 5);
        return out;
    }
};

void Pump(RelayServer& r, int frames = 20) {
    for (int i = 0; i < frames; ++i) {
        r.Poll(0.01f);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

std::vector<std::uint8_t> HelloBytes(std::uint8_t role, const std::string& code) {
    Packet p(RelayHello); p.Write(role); p.Write(code);
    return {p.Data(), p.Data() + p.Size()};
}

} // namespace

int main() {
    RUN_SUITE("relay_security");

    // ---- Packets only travel inside a session ----
    {
        RelayServer relay;
        CHECK(relay.Start(0));
        Peer hostA(relay.Port()), clientA(relay.Port()), attacker(relay.Port());
        hostA.Hello(RelayHost, "ROOM-A");
        clientA.Hello(RelayClient, "ROOM-A");
        attacker.Hello(RelayHost, "ROOM-B");
        Pump(relay);
        const std::uint32_t hostSlot = relay.SlotOf(hostA.self);
        const std::uint32_t clientSlot = relay.SlotOf(clientA.self);
        CHECK(hostSlot != 0 && clientSlot != 0 && relay.SlotOf(attacker.self) != 0);
        hostA.Received(); clientA.Received(); attacker.Received();

        clientA.Data(hostSlot, "legit");
        attacker.Data(hostSlot, "injected");     // another session's slot
        attacker.Data(clientSlot, "injected");
        Pump(relay);
        auto got = hostA.Received();
        CHECK(got.size() == 1 && got[0] == "legit");
        CHECK(clientA.Received().empty());
        relay.Stop();
    }

    // ---- Slots are random, not 1, 2, 3 ... ----
    {
        RelayServer relay;
        std::set<std::uint32_t> slots;
        int consecutive = 0;
        std::uint32_t prev = 0;
        for (int i = 0; i < 64; ++i) {
            const Endpoint ep{0x7F000001u, static_cast<std::uint16_t>(40000 + i)};
            auto h = HelloBytes(RelayClient, "ROOM-" + std::to_string(i));
            relay.HandleDatagram(ep, h.data(), h.size());
            const std::uint32_t s = relay.SlotOf(ep);
            CHECK(s != 0);
            if (prev && s == prev + 1) ++consecutive;
            prev = s;
            slots.insert(s);
        }
        CHECK(slots.size() == 64);
        CHECK(consecutive < 3);
    }

    // ---- A session goes away with its last peer ----
    {
        RelayServer relay;
        const Endpoint a{0x7F000001u, 41001}, b{0x7F000001u, 41002};
        auto h = HelloBytes(RelayHost, "ROOM-X");
        auto c = HelloBytes(RelayClient, "ROOM-X");
        relay.HandleDatagram(a, h.data(), h.size());
        relay.HandleDatagram(b, c.data(), c.size());
        CHECK(relay.SessionCount() == 1 && relay.PeerCount() == 2);
        const std::uint8_t bye = RelayBye;
        relay.HandleDatagram(a, &bye, 1);
        CHECK(relay.SessionCount() == 1);        // the client still waits in it
        relay.HandleDatagram(b, &bye, 1);
        CHECK(relay.SessionCount() == 0 && relay.PeerCount() == 0);
        // A client that never got a host and goes silent is expired, session too.
        CHECK(relay.Start(0));
        relay.HandleDatagram(b, c.data(), c.size());
        CHECK(relay.SessionCount() == 1);
        relay.Poll(10.0f);
        CHECK(relay.SessionCount() == 1);        // still within the 30 s timeout
        relay.Poll(25.0f);
        CHECK(relay.SessionCount() == 0 && relay.PeerCount() == 0);
        relay.Stop();
    }

    // ---- Codes, peers and sessions are bounded ----
    {
        RelayServer relay;
        const Endpoint e{0x7F000001u, 42000};
        auto longCode = HelloBytes(RelayClient, std::string(RelayServer::kMaxCodeLength + 1, 'x'));
        relay.HandleDatagram(e, longCode.data(), longCode.size());
        auto empty = HelloBytes(RelayClient, "");
        relay.HandleDatagram(e, empty.data(), empty.size());
        auto badRole = HelloBytes(7, "ROOM");
        relay.HandleDatagram(e, badRole.data(), badRole.size());
        CHECK(relay.PeerCount() == 0);

        // One session can't take more than kMaxPeersPerSession peers.
        for (std::uint32_t i = 0; i < RelayServer::kMaxPeersPerSession + 10; ++i) {
            const Endpoint ep{0x7F000001u, static_cast<std::uint16_t>(43000 + i)};
            auto h = HelloBytes(RelayClient, "CROWDED");
            relay.HandleDatagram(ep, h.data(), h.size());
        }
        CHECK(relay.PeerCount() == RelayServer::kMaxPeersPerSession);

        // A flood of new sessions stops at the caps.
        for (int i = 0; i < 6000; ++i) {
            const Endpoint ep{0x7F000002u + static_cast<std::uint32_t>(i / 60000), static_cast<std::uint16_t>(1000 + i)};
            auto h = HelloBytes(RelayHost, "S" + std::to_string(i));
            relay.HandleDatagram(ep, h.data(), h.size());
        }
        CHECK(relay.PeerCount() <= RelayServer::kMaxPeers);
        CHECK(relay.SessionCount() <= RelayServer::kMaxSessions);
    }

    // ---- 10,000 random datagrams: no crash, still bounded, still works ----
    {
        RelayServer relay;
        CHECK(relay.Start(0));
        std::mt19937 rng(1234);
        std::vector<std::uint8_t> d;
        for (int i = 0; i < 10000; ++i) {
            d.resize(rng() % 96);
            for (auto& b : d) b = static_cast<std::uint8_t>(rng());
            // Bias toward the real tags so the parser paths get exercised.
            if (!d.empty() && rng() % 2) d[0] = static_cast<std::uint8_t>(RelayHello + rng() % 4);
            if (d.size() > 2 && d[0] == RelayHello && rng() % 2) { d[1] = rng() % 2; }
            const Endpoint ep{0x7F000001u, static_cast<std::uint16_t>(50000 + rng() % 64)};
            relay.HandleDatagram(ep, d.data(), d.size());
        }
        CHECK(relay.PeerCount() <= RelayServer::kMaxPeers);
        // A real pair still gets through afterwards.
        Peer host(relay.Port()), client(relay.Port());
        host.Hello(RelayHost, "AFTER-FUZZ");
        client.Hello(RelayClient, "AFTER-FUZZ");
        Pump(relay);
        host.Received();
        client.Data(relay.SlotOf(host.self), "ok");
        Pump(relay);
        auto got = host.Received();
        CHECK(got.size() == 1 && got[0] == "ok");
        relay.Stop();
    }

    TEST_MAIN_RESULT();
}
