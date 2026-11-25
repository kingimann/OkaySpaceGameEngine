#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Net/RelayServer.hpp"
#include "okay/Net/RelayProtocol.hpp"
#include "okay/Net/Packet.hpp"
#include "okay/Net/RelayHello.hpp"
#include "net_test_util.hpp"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace okay;
using nettest::RawRelayPeer;
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

// A version-3 peer: echoes the relay's cookie, as the engine and the launcher do.
struct V3Peer {
    UdpSocket sock;
    Endpoint relay, self;
    std::uint8_t cookie[kRelayCookieBytes] = {};
    std::uint32_t welcomeSlot = 0, welcomeHost = 0xFFFFFFFFu, cookies = 0;
    std::uint64_t key = 0;
    std::uint8_t refused = 0;
    explicit V3Peer(std::uint16_t relayPort) {
        sock.Open(); sock.Bind(0);
        relay = {0x7F000001u, relayPort};
        self = {0x7F000001u, sock.LocalPort()};
    }
    void Hello(RelayRole role, const std::string& code) {
        Packet p(RelayHello); p.Write(static_cast<std::uint8_t>(role)); p.Write(code);
        p.Write(kRelayProtocolVersion);
        p.Write(static_cast<std::uint32_t>(key)); p.Write(static_cast<std::uint32_t>(key >> 32));
        for (std::uint8_t b : cookie) p.Write(b);
        while (p.Size() < kRelayHelloMinBytes) p.Write(std::uint8_t(0));
        sock.SendTo(relay, p.Data(), p.Size());
    }
    void Bye() { const std::uint8_t b = RelayBye; sock.SendTo(relay, &b, 1); }
    void Read() {
        std::uint8_t buf[2048]; Endpoint from; int n;
        while ((n = sock.RecvFrom(buf, sizeof buf, from)) > 0) {
            Packet p(buf, static_cast<std::size_t>(n));
            const std::uint8_t tag = p.ReadU8();
            if (tag == RelayCookie && n >= 2 + static_cast<int>(kRelayCookieBytes)) {
                std::memcpy(cookie, buf + 2, kRelayCookieBytes); ++cookies;
            } else if (tag == RelayWelcome) {
                welcomeSlot = p.ReadU32(); welcomeHost = p.ReadU32(); p.ReadU8();
                const std::uint32_t lo = p.ReadU32(), hi = p.ReadU32();
                if (p.Ok() && (lo | hi)) key = (static_cast<std::uint64_t>(hi) << 32) | lo;
            } else if (tag == RelayRefused) {
                refused = p.ReadU8();
            }
        }
    }
};

void PumpV3(RelayServer& r, std::vector<V3Peer*> peers, int frames = 10) {
    for (int i = 0; i < frames; ++i) {
        r.Poll(0.01f);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        for (auto* p : peers) p->Read();
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

        // A flood of new sessions from many addresses stops at the global caps.
        for (int i = 0; i < 6000; ++i) {
            const Endpoint ep{0x0B000000u + static_cast<std::uint32_t>(i), 1000};
            auto h = HelloBytes(RelayHost, "S" + std::to_string(i));
            relay.HandleDatagram(ep, h.data(), h.size());
        }
        CHECK(relay.PeerCount() <= RelayServer::kMaxPeers);
        CHECK(relay.SessionCount() == RelayServer::kMaxSessions);
    }

    // ---- One address can't take every slot and lock everyone else out ----
    {
        RelayServer relay;
        CHECK(relay.Start(0));   // Poll() advances the clock the hello rate refills on
        const std::uint32_t flooder = 0x0A000001u;
        for (std::uint32_t i = 0; i < 200; ++i) {   // one IP, many source ports, a new code each
            auto h = HelloBytes(RelayHost, "S" + std::to_string(i));
            relay.HandleDatagram(Endpoint{flooder, static_cast<std::uint16_t>(1000 + i)}, h.data(), h.size());
            if (i % 20 == 19) relay.Poll(2.0f, 1e9f);   // within the address's hello rate
        }
        CHECK(relay.SessionCount() == RelayServer::kMaxSessionsPerAddress);   // it opens at most this many
        CHECK(relay.PeerCount() == RelayServer::kMaxSessionsPerAddress);
        // Other machines' sessions: the same address may still join them, up to its peer cap.
        for (std::uint32_t j = 0; j < 8; ++j) {
            auto h = HelloBytes(RelayHost, "G" + std::to_string(j));
            relay.HandleDatagram(Endpoint{0xC0A80100u + j, 5000}, h.data(), h.size());
        }
        for (std::uint32_t i = 0; i < 200; ++i) {
            auto h = HelloBytes(RelayClient, "G" + std::to_string(i % 8));
            relay.HandleDatagram(Endpoint{flooder, static_cast<std::uint16_t>(3000 + i)}, h.data(), h.size());
            if (i % 20 == 19) relay.Poll(2.0f, 1e9f);
        }
        CHECK(relay.PeerCount() == 8 + RelayServer::kMaxPeersPerAddress);
        CHECK(relay.SessionCount() == RelayServer::kMaxSessionsPerAddress + 8);
        auto legit = HelloBytes(RelayHost, "REAL-GAME");
        const Endpoint real{0xC0A80002u, 5000};
        relay.HandleDatagram(real, legit.data(), legit.size());
        CHECK(relay.SlotOf(real) != 0);           // another machine still hosts
        auto join = HelloBytes(RelayClient, "REAL-GAME");
        const Endpoint friendEp{0xC0A80003u, 6000};
        relay.HandleDatagram(friendEp, join.data(), join.size());
        CHECK(relay.SlotOf(friendEp) != 0);
        // The flooding address frees its slot and session as its peers leave.
        const std::uint8_t bye = RelayBye;
        relay.HandleDatagram(Endpoint{flooder, 1000}, &bye, 1);
        relay.Poll(2.0f, 1e9f);
        auto again = HelloBytes(RelayHost, "AGAIN");
        const Endpoint retry{flooder, 9999};
        relay.HandleDatagram(retry, again.data(), again.size());
        CHECK(relay.SlotOf(retry) != 0);
        auto more = HelloBytes(RelayHost, "MORE");
        relay.HandleDatagram(Endpoint{flooder, 9998}, more.data(), more.size());
        CHECK(relay.SlotOf(Endpoint{flooder, 9998}) == 0);   // full again
    }

    // ---- Loopback isn't held to the per-address session cap ----
    {
        RelayServer relay;
        for (std::uint32_t i = 0; i < 40; ++i) {
            auto h = HelloBytes(RelayHost, "L" + std::to_string(i));
            relay.HandleDatagram(Endpoint{0x7F000005u, static_cast<std::uint16_t>(1000 + i)}, h.data(), h.size());
        }
        CHECK(relay.SessionCount() == 40);
    }

    // ---- Hub sessions (voice calls): v3, no host key, the next hub takes over ----
    {
        RelayServer relay;
        CHECK(relay.Start(0));
        relay.SetLegacyHello(false);   // hubs speak v3: they don't need legacy hellos
        V3Peer a(relay.Port()), b(relay.Port()), c(relay.Port());
        for (auto* p : {&a, &b, &c}) { p->Hello(RelayHub, "CALL"); PumpV3(relay, {p}); p->Hello(RelayHub, "CALL"); PumpV3(relay, {&a, &b, &c}); }
        CHECK(a.cookies == 1 && a.welcomeSlot != 0 && a.welcomeHost == a.welcomeSlot);
        CHECK(a.key == 0);                                         // never protected
        CHECK(b.welcomeHost == a.welcomeSlot && c.welcomeHost == a.welcomeSlot);
        CHECK(b.refused == 0 && c.refused == 0);
        a.Bye(); PumpV3(relay, {&a, &b, &c});
        CHECK(b.welcomeHost == 0);                                 // told the hub left
        b.Hello(RelayHub, "CALL"); PumpV3(relay, {&a, &b, &c});
        CHECK(b.welcomeHost == b.welcomeSlot && c.welcomeHost == b.welcomeSlot);   // b took over
        // A game host can't join a hub session that has a hub...
        V3Peer game(relay.Port());
        game.Hello(RelayHost, "CALL"); PumpV3(relay, {&game}); game.Hello(RelayHost, "CALL"); PumpV3(relay, {&game});
        CHECK(game.refused == RelayRefusedHosted && relay.SlotOf(game.self) == 0);
        // ...and a hub can't take a game's protected session.
        V3Peer host(relay.Port()), squat(relay.Port());
        host.Hello(RelayHost, "GAME"); PumpV3(relay, {&host}); host.Hello(RelayHost, "GAME"); PumpV3(relay, {&host});
        CHECK(host.key != 0);
        squat.Hello(RelayHub, "GAME"); PumpV3(relay, {&squat}); squat.Hello(RelayHub, "GAME"); PumpV3(relay, {&squat});
        CHECK(squat.refused == RelayRefusedHosted && relay.SlotOf(squat.self) == 0);
        relay.Stop();
    }

    // ---- Legacy (v1/v2) hellos: refused when off (the relay program's default), nothing registered (#733) ----
    {
        RelayServer relay;
        CHECK(relay.Start(0));
        relay.SetLegacyHello(false);
        Peer v1(relay.Port());
        v1.Hello(RelayHost, "LEG");                       // a v1 hello: [tag][role][code], no cookie
        Pump(relay);
        CHECK(relay.PeerCount() == 0 && relay.SlotOf(v1.self) == 0);
        CHECK(relay.LegacyHellos() >= 1);
        relay.SetLegacyHello(true);                       // the explicit opt-in still works
        v1.Hello(RelayHost, "LEG");
        Pump(relay);
        CHECK(relay.PeerCount() == 1);
        relay.Stop();
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
            const Endpoint ep{0x7F000003u, static_cast<std::uint16_t>(50000 + rng() % 64)};   // not the real peers' address
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

    // ---- SEC1: a flood of spoofed hellos must not lock new players out (the hello-rate table is bounded) ----
    {
        RelayServer relay;
        CHECK(relay.Start(0));
        relay.SetLegacyHello(true);   // a legacy hello registers at once: lets the test see "joined" without a cookie
        // Version-4 hellos with an empty cookie from 70,000 different (forged) addresses: each only earns a cookie reply
        // and a row in the per-address hello-rate table, which holds kMaxHelloTrackers (65,536) rows.
        auto v4 = [](const std::string& code) {
            Packet p(RelayHello); p.Write(static_cast<std::uint8_t>(RelayClient)); p.Write(code);
            p.Write(kRelayProtocolVersion); p.Write(std::uint32_t(0)); p.Write(std::uint32_t(0));
            for (std::size_t i = 0; i < kRelayCookieBytes; ++i) p.Write(std::uint8_t(0));
            while (p.Size() < kRelayHelloMinBytes) p.Write(std::uint8_t(0));
            return std::vector<std::uint8_t>(p.Data(), p.Data() + p.Size());
        };
        const auto spoof = v4("X");
        for (std::uint32_t i = 0; i < 70000; ++i)
            relay.HandleDatagram(Endpoint{0x0A010000u + i, 4000}, spoof.data(), spoof.size());
        CHECK(relay.PeerCount() == 0);      // nothing was allocated for a forged address
        relay.Poll(0.5f, 1e9f);   // half a second later the forged rows are back to full buckets, so the table has room again
        // A real player from a new address (a legacy hello needs no cookie round) still gets in.
        const Endpoint real{0xC6336407u, 5000};   // 198.51.100.7
        const auto hello = HelloBytes(RelayHost, "REAL-ROOM");
        relay.HandleDatagram(real, hello.data(), hello.size());
        CHECK(relay.SlotOf(real) != 0);
        relay.Stop();
    }

    // ---- SEC1: one address's total forwarding is capped, however many peers and sessions it holds ----
    {
        RelayServer relay;
        CHECK(relay.Start(0));
        relay.SetLegacyHello(true);
        const std::uint32_t attacker = 0xCB007101u;   // 203.0.113.1
        std::vector<Endpoint> hosts, clients;
        for (int sIdx = 0; sIdx < 16; ++sIdx) {       // 16 sessions (the per-address session cap), 2 clients each
            const std::string code = "FLOOD-" + std::to_string(sIdx);
            const Endpoint h{attacker, static_cast<std::uint16_t>(10000 + sIdx * 10)};
            const auto hh = HelloBytes(RelayHost, code);
            relay.HandleDatagram(h, hh.data(), hh.size());
            hosts.push_back(h);
            for (int c = 1; c <= 2; ++c) {
                const Endpoint e{attacker, static_cast<std::uint16_t>(10000 + sIdx * 10 + c)};
                const auto ch = HelloBytes(RelayClient, code);
                relay.HandleDatagram(e, ch.data(), ch.size());
                clients.push_back(e);
            }
            relay.Poll(2.0f, 1e9f);   // stay within the address's hello rate while the test sets up
        }
        CHECK(relay.PeerCount() == 48);
        std::vector<std::uint8_t> big(64000, 0x5A);
        for (std::size_t i = 0; i < clients.size(); ++i) {
            const std::uint32_t hostSlot = relay.SlotOf(hosts[i / 2]);
            big[0] = RelayData; StoreU32LE(big.data() + 1, hostSlot);
            for (int k = 0; k < 30; ++k) relay.HandleDatagram(clients[i], big.data(), big.size());   // 1.9 MB each, 61 MB in all
        }
        // Each peer, each host's inbound and each session stays within its own cap, so only a per-address cap can stop this.
        CHECK(relay.DroppedRate() > 0);
        relay.Stop();
    }

    // ---- SEC1: one session's total forwarding is capped too ----
    {
        RelayServer relay;
        CHECK(relay.Start(0));
        relay.SetLegacyHello(true);
        const Endpoint host{0xC6336401u, 20000};   // 198.51.100.1
        const auto hh = HelloBytes(RelayHost, "BUSY-ROOM");
        relay.HandleDatagram(host, hh.data(), hh.size());
        std::vector<Endpoint> clients;
        for (std::uint32_t i = 0; i < 30; ++i) {   // each from its own address, so no per-address cap applies
            const Endpoint e{0xC6336410u + i, 20001};
            const auto ch = HelloBytes(RelayClient, "BUSY-ROOM");
            relay.HandleDatagram(e, ch.data(), ch.size());
            clients.push_back(e);
        }
        CHECK(relay.PeerCount() == 31);
        relay.Poll(10.0f, 1e9f);   // let the host's forwarding allowance fill to what a 30-client host may send
        std::vector<std::uint8_t> big(64000, 0x5A);
        // The host sends 1.8 MB to each of 30 clients: 54 MB through one session, with every per-peer cap respected.
        for (const auto& c : clients) {
            big[0] = RelayData; StoreU32LE(big.data() + 1, relay.SlotOf(c));
            for (int k = 0; k < 28; ++k) relay.HandleDatagram(host, big.data(), big.size());
        }
        CHECK(relay.DroppedRate() > 0);
        relay.Stop();
    }

    // ---- Version 5 (direct paths, host succession), from the review of PR #1240 ----
    // Virtual time: each step is one Poll(dt) and every peer reads what the relay sent.
    auto step = [](RelayServer& r, std::vector<RawRelayPeer*> ps, float dt = 0.02f) {
        r.Poll(dt);
        for (auto* p : ps) p->Drain();
    };
    auto join = [&](RelayServer& r, RawRelayPeer& p, RelayRole role, const std::string& code, std::uint8_t flags) {
        p.Hello(role, code, 0, flags); step(r, {&p});   // the cookie
        p.Hello(role, code, 0, flags); step(r, {&p});   // the slot
    };

    // V5-1: a player holding the successor key cannot take a LIVE host's place: not after one lost keepalive
    // (even at the old one-a-second cadence), not during a 0.6 s host stall, with the attacker asking ten times a
    // second. Only a host that is really gone (several hellos missed, or a bye) is replaced.
    {
        RelayServer relay;
        CHECK(relay.Start(0));
        RawRelayPeer host(relay.Port()), evil(relay.Port());
        join(relay, host, RelayHost, "LIVE01", RelayHelloDirect);
        join(relay, evil, RelayClient, "LIVE01", 0);
        CHECK(host.succ != 0 && host.hostKey != 0);
        const std::uint64_t key = host.succ;   // every player gets it from the host's migration list
        const std::uint32_t hs = relay.HostSlotOf("LIVE01");
        int takenAt = -1;
        for (int frame = 0; frame < 500; ++frame) {   // 10 s
            const bool lost = frame == 250;                       // one lost keepalive at t = 5 s (a 2 s gap)
            const bool stalled = frame >= 400 && frame < 430;     // and a 0.6 s stall later
            if (frame % 50 == 0 && !lost && !stalled) host.Hello(RelayHost, "LIVE01", host.hostKey, RelayHelloDirect);
            if (frame % 5 == 0) evil.Hello(RelayHost, "LIVE01", key, 0);
            step(relay, {&host, &evil});
            if (takenAt < 0 && relay.HostSlotOf("LIVE01") != hs) takenAt = frame;
        }
        std::printf("  v5 live host: taken at frame %d; successions %llu\n", takenAt, (unsigned long long)relay.Successions());
        CHECK(takenAt < 0);
        CHECK(relay.Successions() == 0 && relay.HostSlotOf("LIVE01") == hs);
        CHECK(host.hostSlot == host.slot);
        // The host really goes (no hello at all): a key holder takes over once it has missed several hellos.
        int frames = 0;
        for (; frames < 400 && relay.HostSlotOf("LIVE01") == hs; ++frames) {
            if (frames % 5 == 0) evil.Hello(RelayHost, "LIVE01", key, 0);
            step(relay, {&host, &evil});
        }
        std::printf("  v5 host gone: taken over after %.2f s of silence\n", frames * 0.02f);
        CHECK(relay.HostSlotOf("LIVE01") == evil.slot);
        CHECK(frames * 0.02f >= kRelaySuccessionQuietSeconds - 1.0f && frames * 0.02f <= kRelaySuccessionQuietSeconds + 0.5f);
        CHECK(kRelaySuccessionQuietSeconds >= 3.0f * 1.0f);   // three of the old one-a-second hellos missed, at least
        relay.Stop();
    }

    // V5-2: a host that says bye is replaced at once, and two players asking at the same moment get one winner.
    {
        RelayServer relay;
        CHECK(relay.Start(0));
        RawRelayPeer host(relay.Port()), c1(relay.Port()), c2(relay.Port());
        join(relay, host, RelayHost, "RACE01", 0);
        join(relay, c1, RelayClient, "RACE01", 0);
        join(relay, c2, RelayClient, "RACE01", 0);
        const std::uint64_t key = host.succ;
        host.Bye(); step(relay, {&host, &c1, &c2});
        c1.Hello(RelayHost, "RACE01", key, 0); c2.Hello(RelayHost, "RACE01", key, 0);
        step(relay, {&c1, &c2});
        const std::uint32_t hs = relay.HostSlotOf("RACE01");
        CHECK(relay.Successions() == 1 && (hs == c1.slot || hs == c2.slot));
        CHECK(c1.hostSlot == hs && c2.hostSlot == hs);
        RawRelayPeer back(relay.Port());   // the old host key cannot take it back from a new address
        back.Hello(RelayHost, "RACE01", host.hostKey, 0); step(relay, {&back});
        back.Hello(RelayHost, "RACE01", host.hostKey, 0); step(relay, {&back});
        CHECK(relay.HostSlotOf("RACE01") == hs);
        relay.Stop();
    }

    // V5-3: anyone with the room code is NOT told the host's address. The relay passes addresses on only for the
    // clients the host names (RelayDirectAllow, with its host key): the ones that passed the game's own join.
    {
        RelayServer relay;
        CHECK(relay.Start(0));
        RawRelayPeer host(relay.Port()), stranger(relay.Port()), friendly(relay.Port());
        join(relay, host, RelayHost, "ROOM42", RelayHelloDirect);
        join(relay, stranger, RelayClient, "ROOM42", RelayHelloDirect);
        join(relay, friendly, RelayClient, "ROOM42", RelayHelloDirect);
        for (int i = 0; i < 20; ++i) {   // keepalives on both sides: still nothing
            host.Hello(RelayHost, "ROOM42", host.hostKey, RelayHelloDirect);
            stranger.Hello(RelayClient, "ROOM42", 0, RelayHelloDirect);
            step(relay, {&host, &stranger, &friendly}, 0.1f);
        }
        CHECK(stranger.addrs.empty() && friendly.addrs.empty() && host.addrs.empty());
        CHECK(relay.PeerAddrsSent() == 0);
        // A forged allow (wrong key, or from a client) does nothing.
        stranger.Send(net::BuildRelayDirectAllow(host.hostKey ^ 1, {stranger.slot}));
        stranger.Send(net::BuildRelayDirectAllow(0, {stranger.slot}));
        host.Send(net::BuildRelayDirectAllow(host.hostKey ^ 0x100, {stranger.slot}));
        step(relay, {&host, &stranger, &friendly});
        stranger.Hello(RelayClient, "ROOM42", 0, RelayHelloDirect); step(relay, {&host, &stranger, &friendly});
        CHECK(stranger.addrs.empty() && relay.PeerAddrsSent() == 0);
        // The host allows the friendly client only: it (and the host) learn the pair at once; the stranger nothing.
        host.Send(net::BuildRelayDirectAllow(host.hostKey, {friendly.slot}));
        step(relay, {&host, &stranger, &friendly});
        CHECK(friendly.addrs.size() == 1 && friendly.addrs[0].port == host.Port() && friendly.addrs[0].slot == host.slot);
        CHECK(host.addrs.size() == 1 && host.addrs[0].port == friendly.Port() && host.addrs[0].slot == friendly.slot);
        CHECK(host.addrs[0].token == friendly.addrs[0].token);
        for (int i = 0; i < 10; ++i) { stranger.Hello(RelayClient, "ROOM42", 0, RelayHelloDirect); step(relay, {&host, &stranger}, 0.1f); }
        CHECK(stranger.addrs.empty());
        // Version 5's keepalive cost (finding 5): ten client hellos a second apart bring at most one more address
        // pair (it is resent only when it changes or every few seconds while the client still asks), so the relay
        // sends less than it is sent.
        const std::size_t inBefore = friendly.bytesOut, outBefore = friendly.bytesIn + host.bytesIn;
        const std::size_t addrsBefore = friendly.addrs.size();
        for (int i = 0; i < 10; ++i) { friendly.Hello(RelayClient, "ROOM42", 0, RelayHelloDirect); step(relay, {&host, &friendly}, 1.0f); }
        const std::size_t in = friendly.bytesOut - inBefore, out = friendly.bytesIn + host.bytesIn - outBefore;
        std::printf("  v5 keepalive: 10 client hellos, %zu B in; the relay sent %zu B (client and host), %zu address frame(s)\n",
                    in, out, friendly.addrs.size() - addrsBefore);
        CHECK(out <= in);
        CHECK(friendly.addrs.size() - addrsBefore <= 4);
        // The host drops it from the list (it left the game, or was kicked): no more addresses for it.
        host.Send(net::BuildRelayDirectAllow(host.hostKey, {}));
        step(relay, {&host, &friendly});
        const std::size_t after = friendly.addrs.size();
        for (int i = 0; i < 10; ++i) { friendly.Hello(RelayClient, "ROOM42", 0, RelayHelloDirect); step(relay, {&host, &friendly}, 1.0f); }
        CHECK(friendly.addrs.size() == after);
        relay.Stop();
    }

    TEST_MAIN_RESULT();
}
