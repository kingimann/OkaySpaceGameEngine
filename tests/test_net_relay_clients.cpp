#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Net/RelayServer.hpp"
#include "okay/Net/RelayHello.hpp"
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

using namespace okay;
using namespace okay::net;

// Relay clients speak the current protocol (issue #733, step 1): the shared hello builder
// the launcher's voice calls and the game's NetworkManager both use sends version 4 with the
// address cookie and a welcome nonce, so a relay with legacy hellos switched off serves them,
// counts no legacy hello from them, and the relay's monitoring (per-version counts, live legacy
// peers) tells an operator when it is safe to turn legacy hellos off. The relay's default
// stays ON.

namespace {
void Nap() { std::this_thread::sleep_for(std::chrono::milliseconds(2)); }

// What the launcher's voice session does on the wire, using only the shared helpers.
struct VoiceLikeClient {
    UdpSocket sock;
    Endpoint relay;
    RelayHelloParams hello;
    std::uint64_t nonce;
    RelayWelcomeInfo welcome;
    bool welcomed = false;
    int forged = 0, refusedCode = 0;
    bool allowUnverified = false;
    VoiceLikeClient(std::uint16_t relayPort, const std::string& room, std::uint64_t n) : nonce(n) {
        sock.Open(); sock.Bind(0);
        relay = {0x7F000001u, relayPort};
        hello.role = RelayHub; hello.code = room; hello.nonce = nonce;
    }
    void SendHello() {
        auto pk = BuildRelayHello(hello);
        sock.SendTo(relay, pk.data(), pk.size());
    }
    void Read() {
        std::uint8_t buf[256]; Endpoint from; int n;
        while ((n = sock.RecvFrom(buf, sizeof buf, from)) > 0) {
            if (buf[0] == RelayCookie && n >= 2 + (int)kRelayCookieBytes) {
                std::memcpy(hello.cookie.data(), buf + 2, kRelayCookieBytes);
                SendHello();
            } else if (buf[0] == RelayWelcome) {
                RelayWelcomeInfo w;
                if (ParseRelayWelcome(buf, (std::size_t)n, nonce, allowUnverified, w)) { welcome = w; welcomed = true; }
                else ++forged;
            } else if (buf[0] == RelayRefused && n >= 2) {
                refusedCode = buf[1];
            }
        }
    }
};

void Pump(RelayServer& r, std::vector<VoiceLikeClient*> cs, int frames = 40) {
    for (int i = 0; i < frames; ++i) {
        r.Poll(0.01f);
        Nap();
        for (auto* c : cs) c->Read();
    }
}
} // namespace

int main() {
    RUN_SUITE("net_relay_clients");

    // ---- The default is unchanged: legacy hellos stay ON ----
    {
        RelayServer relay;
        CHECK(relay.LegacyHello());
    }

    // ---- The shared hello is a well-formed version-4 hello ----
    {
        RelayHelloParams h;
        h.role = RelayHub; h.code = "ROOM"; h.nonce = 0x1122334455667788ull; h.hostKey = 0xAABBCCDD00112233ull;
        for (std::size_t i = 0; i < h.cookie.size(); ++i) h.cookie[i] = (std::uint8_t)(i + 1);
        const auto b = BuildRelayHello(h);
        CHECK(b.size() >= kRelayHelloMinBytes);
        CHECK(b[0] == RelayHello && b[1] == RelayHub);
        CHECK(b[1 + 1 + 4 + 4] == kRelayProtocolVersion);   // version follows the code
        // Padded, never shorter than the minimum, even for an empty-ish code.
        h.code = "A";
        CHECK(BuildRelayHello(h).size() >= kRelayHelloMinBytes);
    }

    // ---- A relay with legacy hellos OFF serves a voice-style client; no legacy hello is counted ----
    {
        RelayServer relay;
        CHECK(relay.Start(0));
        relay.SetLegacyHello(false);
        VoiceLikeClient a(relay.Port(), "CALL-1", 0x1111222233334444ull);
        VoiceLikeClient b(relay.Port(), "CALL-1", 0x5555666677778888ull);
        a.SendHello(); b.SendHello();
        Pump(relay, {&a, &b});
        CHECK(a.welcomed && b.welcomed);
        CHECK(a.welcome.haveNonce && b.welcome.haveNonce);           // the relay echoed each client's nonce
        CHECK(a.welcome.hostSlot == a.welcome.slot);                 // first in is the hub
        CHECK(b.welcome.hostSlot == a.welcome.slot);
        CHECK(relay.LegacyHellos() == 0);
        CHECK(relay.LegacyPeers() == 0);
        CHECK(relay.HellosByVersion(4) >= 4);                         // hello, cookie'd hello, for both
        CHECK(relay.HellosByVersion(1) == 0 && relay.HellosByVersion(2) == 0 && relay.HellosByVersion(3) == 0);
        relay.Stop();
    }

    // ---- A forged welcome (wrong or missing nonce) is not accepted ----
    {
        RelayHelloParams h;   // helpers only
        std::uint8_t good[26] = {RelayWelcome, 7, 0, 0, 0, 7, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0};
        const std::uint64_t nonce = 0xDEADBEEF12345678ull;
        std::memcpy(good + 18, &nonce, 8);                            // this host is little-endian-agnostic below
        for (int i = 0; i < 8; ++i) good[18 + i] = (std::uint8_t)(nonce >> (8 * i));
        RelayWelcomeInfo w;
        CHECK(ParseRelayWelcome(good, 26, nonce, false, w) && w.slot == 7 && w.hostSlot == 7 && w.haveNonce);
        CHECK(!ParseRelayWelcome(good, 26, nonce + 1, false, w));     // wrong echo: never, even if unverified is allowed
        CHECK(!ParseRelayWelcome(good, 26, nonce + 1, true, w));
        CHECK(!ParseRelayWelcome(good, 18, nonce, false, w));         // older relay's welcome: only when allowed
        CHECK(ParseRelayWelcome(good, 18, nonce, true, w) && !w.haveNonce);
        CHECK(!ParseRelayWelcome(good, 8, nonce, true, w));           // truncated
        good[0] = RelayData;
        CHECK(!ParseRelayWelcome(good, 26, nonce, true, w));          // not a welcome
        (void)h;
    }

    // ---- Legacy peers are counted and visible, and the default still serves them ----
    {
        RelayServer relay;
        CHECK(relay.Start(0));
        UdpSocket s; s.Open(); s.Bind(0);
        const Endpoint rel{0x7F000001u, relay.Port()};
        Packet p(RelayHello); p.Write(std::uint8_t(RelayHost)); p.Write(std::string("OLD-GAME"));   // a version-1 hello
        s.SendTo(rel, p.Data(), p.Size());
        for (int i = 0; i < 20; ++i) { relay.Poll(0.01f); Nap(); }
        CHECK(relay.LegacyHellos() == 1);
        CHECK(relay.LegacyPeers() == 1);
        CHECK(relay.HellosByVersion(1) == 1);
        relay.SetLegacyHello(false);
        UdpSocket s2; s2.Open(); s2.Bind(0);
        s2.SendTo(rel, p.Data(), p.Size());
        for (int i = 0; i < 20; ++i) { relay.Poll(0.01f); Nap(); }
        CHECK(relay.LegacyHellos() == 2);                             // counted even when refused
        CHECK(relay.LegacyPeers() == 1);                              // but not registered
        relay.Stop();
    }

    // ---- A game host and client (NetworkManager) work against a legacy-off relay, none counted ----
    {
        RelayServer relay;
        CHECK(relay.Start(0));
        relay.SetLegacyHello(false);
        Scene host("Host"); host.physicsEnabled = false;
        auto* hn = host.CreateGameObject("Net")->AddComponent<NetworkManager>();
        CHECK(hn->HostViaRelay("127.0.0.1", relay.Port(), "GAME-1"));
        Scene client("Client"); client.physicsEnabled = false;
        auto* cn = client.CreateGameObject("Net")->AddComponent<NetworkManager>();
        CHECK(cn->JoinViaRelay("127.0.0.1", relay.Port(), "GAME-1"));
        host.Start(); client.Start();
        for (int i = 0; i < 600 && !(cn->Joined() && hn->Peers().size() >= 1); ++i) {
            relay.Poll(0.02f); host.Update(0.02f); client.Update(0.02f); Nap();
        }
        CHECK(cn->Joined());
        CHECK(relay.LegacyHellos() == 0 && relay.LegacyPeers() == 0);
        CHECK(relay.HellosByVersion(4) > 0);
    }

    TEST_MAIN_RESULT();
}
