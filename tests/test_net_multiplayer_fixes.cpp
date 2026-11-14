#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Net/NetChannels.hpp"
#include "okay/Net/NetDelta.hpp"
#include "okay/Net/Packet.hpp"
#include "okay/Net/RelayProtocol.hpp"
#include "okay/Net/RelayServer.hpp"
#include "okay/Net/UdpSocket.hpp"
#include <chrono>
#include <climits>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace okay;
using namespace okay::net;

// Regression tests for the multiplayer audit (docs/multiplayer_audit.md):
//   * reliable-ordered sends past the 32-message window are queued, not dropped
//   * delta snapshots stay correct at the ends of the position range
//   * the relay does not treat a spoofed re-hello as proof of life
//   * relay logs can't be forged through a session code

namespace {

void Nap() { std::this_thread::sleep_for(std::chrono::milliseconds(3)); }

// Delivers `bytes` (a kChanRel datagram) to `rx`, returning the ack it earns (0: none).
std::uint32_t Deliver(OrderedEnd& rx, const std::vector<std::uint8_t>& bytes, std::vector<ChanMsg>& out) {
    Packet p(bytes.data(), bytes.size());
    p.ReadU8();
    const std::uint32_t seq = p.ReadU32();
    const std::string channel = p.ReadString();
    std::string data = p.ReadString();
    std::uint32_t ack = 0;
    rx.Receive(channel, seq, std::move(data), true, 1, out, ack);
    return ack;
}

// A version-4 peer with its own socket and address cookie.
struct Peer4 {
    UdpSocket sock;
    std::uint8_t cookie[kRelayCookieBytes] = {};
    bool welcomed = false;
    Endpoint to;
    explicit Peer4(const RelayServer& relay) {
        sock.Open(); sock.Bind(0);
        to = Endpoint{0x7F000001u, relay.Port()};
    }
    void Hello(RelayServer& relay, RelayRole role, const std::string& code, std::uint8_t version,
               bool withCookie) {
        Packet h(RelayHello); h.Write(static_cast<std::uint8_t>(role)); h.Write(code);
        h.Write(version); h.Write(std::uint32_t(0)); h.Write(std::uint32_t(0));
        for (std::uint8_t b : cookie) h.Write(static_cast<std::uint8_t>(withCookie ? b : 0));
        h.Write(std::uint32_t(0x1234)); h.Write(std::uint32_t(0));
        while (h.Size() < kRelayHelloMinBytes) h.Write(std::uint8_t(0));
        sock.SendTo(to, h.Data(), h.Size());
        for (int i = 0; i < 4; ++i) { relay.Poll(0.0f); Nap(); }
        std::uint8_t buf[256]; Endpoint from; int n;
        while ((n = sock.RecvFrom(buf, sizeof buf, from)) > 0) {
            if (buf[0] == RelayCookie && n >= 2 + static_cast<int>(kRelayCookieBytes))
                std::memcpy(cookie, buf + 2, kRelayCookieBytes);
            else if (buf[0] == RelayWelcome) welcomed = true;
        }
    }
    // The first hello earns the cookie, the second is accepted.
    void Register(RelayServer& relay, RelayRole role, const std::string& code) {
        Hello(relay, role, code, kRelayProtocolVersion, true);
        Hello(relay, role, code, kRelayProtocolVersion, true);
    }
};

} // namespace

int main() {
    RUN_SUITE("net_multiplayer_fixes");

    // ---- SendOrdered: a burst past the window is queued, in order, never lost ----
    {
        OrderedEnd tx, rx;
        std::vector<std::vector<std::uint8_t>> wire;
        for (int i = 0; i < 100; ++i) {
            auto b = tx.Push("chat", "m" + std::to_string(i), 0);
            if (!b.empty()) wire.push_back(std::move(b));
        }
        CHECK(wire.size() == kOrderedWindow);              // the window goes out now
        CHECK(tx.Backlog("chat") == 100 - kOrderedWindow); // the rest waits, not refused
        std::vector<ChanMsg> got;
        // A lossless link: deliver, ack, let the sender top its window up.
        for (int round = 0; round < 50 && got.size() < 100; ++round) {
            std::uint32_t ack = 0;
            for (const auto& b : wire) { std::uint32_t a = Deliver(rx, b, got); if (a > ack) ack = a; }
            wire.clear();
            if (ack) tx.Ack("chat", ack);
            tx.CollectResend(0.0f, wire);
        }
        CHECK(got.size() == 100);
        bool inOrder = got.size() == 100;
        for (std::size_t i = 0; i < got.size(); ++i) inOrder = inOrder && got[i].data == "m" + std::to_string(i);
        CHECK(inOrder);
        CHECK(tx.Backlog("chat") == 0);
    }
    // ...a message pushed after acks freed room still queues behind the backlog (order holds).
    {
        OrderedEnd tx, rx;
        std::vector<std::vector<std::uint8_t>> wire;
        for (int i = 0; i < 40; ++i) { auto b = tx.Push("c", "a" + std::to_string(i), 0); if (!b.empty()) wire.push_back(b); }
        std::vector<ChanMsg> got;
        std::uint32_t ack = 0;
        for (const auto& b : wire) { std::uint32_t a = Deliver(rx, b, got); if (a > ack) ack = a; }
        tx.Ack("c", ack);                                  // window is free again, backlog (8) not sent yet
        auto late = tx.Push("c", "late", 0);
        CHECK(late.empty());                               // must wait behind a32..a39
        wire.clear();
        tx.CollectResend(0.0f, wire);
        for (const auto& b : wire) Deliver(rx, b, got);
        CHECK(got.size() == 41);
        CHECK(got.size() == 41 && got.back().data == "late" && got[32].data == "a32");
    }
    // ...and the backlog is bounded (a runaway sender is refused, not buffered forever).
    {
        OrderedEnd tx;
        std::size_t refused = 0;
        for (std::size_t i = 0; i < kOrderedWindow + kOrderedBacklogMax + 10; ++i) {
            const auto before = tx.Backlog("c");
            auto b = tx.Push("c", "x", 0);
            if (b.empty() && tx.Backlog("c") == before) ++refused;
        }
        CHECK(refused == 10);
        CHECK(tx.Backlog("c") == kOrderedBacklogMax);
        // A byte cap too: large payloads hit it well before the message cap.
        OrderedEnd big;
        const std::string blob(kChannelPayloadMax, 'z');
        for (int i = 0; i < 1000; ++i) big.Push("b", blob, 0);
        const std::size_t queued = big.Backlog("b");
        CHECK(queued * blob.size() <= kOrderedBacklogBytes);
        CHECK(queued > 0 && queued < 1000 - kOrderedWindow);
    }

    // ---- NetDelta: no overflow at the ends of the position range ----
    {
        // Encoder: a jump across the whole saturated range must become a keyframe, not a wrapped delta.
        DeltaBase base, cur;
        base.id = 1;
        base.pose["a"] = QPose{-INT32_MAX, 0, 0, 0, 0, 0, 32767};
        cur.pose["a"] = QPose{INT32_MAX, 0, 0, 0, 0, 0, 32767};
        auto bytes = EncodeSnapshot(base, cur, 2, 0.0);
        CHECK(!bytes.empty());
        std::uint32_t bid = 0, sid = 0; double tick = 0;
        std::vector<SnapChange> ch;
        CHECK(DecodeSnapshot(bytes.data(), bytes.size(), base, bid, sid, tick, ch));
        CHECK(ch.size() == 1 && ch[0].pose.x == INT32_MAX);
    }
    {
        // Decoder: a delta applied to a saturated baseline clamps instead of wrapping.
        DeltaBase zero, cur;
        zero.id = 7;
        zero.pose["a"] = QPose{0, 0, 0, 0, 0, 0, 32767};
        cur.pose["a"] = QPose{1000, -1000, 1000, 0, 0, 0, 32767};
        auto bytes = EncodeSnapshot(zero, cur, 8, 0.0);
        DeltaBase high = zero, low = zero;
        high.pose["a"] = QPose{INT32_MAX, INT32_MAX, INT32_MAX, 0, 0, 0, 32767};
        low.pose["a"] = QPose{-INT32_MAX, -INT32_MAX, -INT32_MAX, 0, 0, 0, 32767};
        std::uint32_t bid = 0, sid = 0; double tick = 0;
        std::vector<SnapChange> ch;
        CHECK(DecodeSnapshot(bytes.data(), bytes.size(), high, bid, sid, tick, ch));
        CHECK(ch.size() == 1 && ch[0].pose.x == INT32_MAX && ch[0].pose.z == INT32_MAX);
        CHECK(ch.size() == 1 && ch[0].pose.y == INT32_MAX - 1000);
        ch.clear();
        CHECK(DecodeSnapshot(bytes.data(), bytes.size(), low, bid, sid, tick, ch));
        CHECK(ch.size() == 1 && ch[0].pose.y == -INT32_MAX);   // -INT32_MAX - 1000 clamps
        CHECK(ch.size() == 1 && ch[0].pose.x == -INT32_MAX + 1000);
    }

    // ---- Relay: a spoofed re-hello doesn't keep a registered peer alive ----
    {
        RelayServer relay;
        CHECK(relay.Start(0));
        relay.SetLegacyHello(true);
        Peer4 victim(relay);
        victim.Register(relay, RelayHost, "ROOM");
        CHECK(relay.PeerCount() == 1);
        relay.Poll(25.0f, 30.0f);
        CHECK(relay.PeerCount() == 1);
        // From the victim's address, but without its cookie: a v4 hello with zeros, and a legacy v1 hello.
        victim.Hello(relay, RelayHost, "ROOM", kRelayProtocolVersion, false);
        {
            Packet v1(RelayHello); v1.Write(static_cast<std::uint8_t>(RelayHost)); v1.Write(std::string("ROOM"));
            victim.sock.SendTo(victim.to, v1.Data(), v1.Size());
            for (int i = 0; i < 4; ++i) { relay.Poll(0.0f); Nap(); }
        }
        relay.Poll(10.0f, 30.0f);                          // 35 s since the last proven traffic
        CHECK(relay.PeerCount() == 0);
    }
    {
        // A real keepalive (valid cookie) still keeps it alive.
        RelayServer relay;
        CHECK(relay.Start(0));
        Peer4 p(relay);
        p.Register(relay, RelayHost, "ROOM");
        relay.Poll(25.0f, 30.0f);
        p.Hello(relay, RelayHost, "ROOM", kRelayProtocolVersion, true);
        relay.Poll(10.0f, 30.0f);
        CHECK(relay.PeerCount() == 1);
        // A missing cookie on a re-hello is answered with a fresh cookie, not ignored.
        const auto before = relay.CookiesSent();
        p.Hello(relay, RelayHost, "ROOM", kRelayProtocolVersion, false);
        CHECK(relay.CookiesSent() == before + 1);
    }

    // ---- Relay logs: a peer-chosen code can't forge log lines ----
    {
        CHECK(RelayLogSafe("ROOM-1") == "ROOM-1");
        CHECK(RelayLogSafe("a\nfake line") == "a\\x0afake line");
        CHECK(RelayLogSafe("\x1b[31mred") == "\\x1b[31mred");
        CHECK(RelayLogSafe(std::string("n\0l", 3)) == "n\\x00l");
        CHECK(RelayLogSafe("back\\slash") == "back\\x5cslash");
        CHECK(RelayLogSafe("\xc3\xa9") == "\\xc3\\xa9");
    }

    // ---- A host that goes silent ends the client's session; a join to nobody gives up ----
    {
        Scene serverScene("Server");
        serverScene.physicsEnabled = false;
        auto* server = serverScene.CreateGameObject("Net")->AddComponent<NetworkManager>();
        CHECK(server->StartServer(0));
        serverScene.Start();
        Scene cs("Client");
        cs.physicsEnabled = false;
        auto* c = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        CHECK(c->StartClient("127.0.0.1", server->ServerPort()));
        cs.Start();
        for (int i = 0; i < 300 && !c->Joined(); ++i) { serverScene.Update(0.02f); cs.Update(0.02f); Nap(); }
        CHECK(c->Joined());
        // A healthy session (host answering the once-a-second ping) lasts well past the timeout.
        for (int i = 0; i < 160; ++i) { serverScene.Update(0.1f); cs.Update(0.1f); Nap(); }
        CHECK(c->Joined() && c->IsConnected() && !c->ConnectionLost());
        // The host hangs (stops ticking): after serverTimeoutSeconds the client leaves.
        for (int i = 0; i < 100 && c->IsConnected(); ++i) { cs.Update(0.25f); Nap(); }
        CHECK(!c->IsConnected());
        CHECK(c->ConnectionLost());
        CHECK(!c->ConnectionLostReason().empty());
        CHECK(!c->WasKicked());
    }
    {
        // serverTimeoutSeconds = 0 opts out.
        Scene serverScene("Server");
        serverScene.physicsEnabled = false;
        auto* server = serverScene.CreateGameObject("Net")->AddComponent<NetworkManager>();
        CHECK(server->StartServer(0));
        serverScene.Start();
        Scene cs("Client");
        cs.physicsEnabled = false;
        auto* c = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        c->serverTimeoutSeconds = 0.0f;
        CHECK(c->StartClient("127.0.0.1", server->ServerPort()));
        cs.Start();
        for (int i = 0; i < 300 && !c->Joined(); ++i) { serverScene.Update(0.02f); cs.Update(0.02f); Nap(); }
        CHECK(c->Joined());
        for (int i = 0; i < 100; ++i) { cs.Update(0.25f); Nap(); }
        CHECK(c->IsConnected() && !c->ConnectionLost());
    }
    {
        UdpSocket mute; mute.Open(); mute.Bind(0);          // a port nobody answers on
        Scene cs("Client");
        cs.physicsEnabled = false;
        auto* c = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        c->joinTimeoutSeconds = 2.0f;
        CHECK(c->StartClient("127.0.0.1", mute.LocalPort()));
        cs.Start();
        for (int i = 0; i < 30; ++i) { cs.Update(0.1f); Nap(); }   // 3 s
        CHECK(c->JoinRejected());
        CHECK(c->JoinRejectReason() == "the host did not answer");
        CHECK(!c->Joined());
    }

    TEST_MAIN_RESULT();
}
