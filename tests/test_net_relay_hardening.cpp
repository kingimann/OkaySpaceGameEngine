#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Net/RelayServer.hpp"
#include "okay/Net/RelayProtocol.hpp"
#include "okay/Net/Packet.hpp"
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace okay;
using namespace okay::net;

// The relay client trusts nothing that arrives on its socket: a relay that never
// answers ends the attempt with an error, a welcome has to echo the nonce our hello
// carried (a forged one is ignored), refusals are reported for what they are, a
// truncated host welcome isn't accepted, and a flood of control frames is drained a
// bounded amount per call. A local UDP socket plays the relay, so every reply here is
// exactly what we choose to send.

namespace {

void Nap() { std::this_thread::sleep_for(std::chrono::milliseconds(3)); }

struct FakeRelay {
    UdpSocket sock;
    Endpoint client;                // the last sender: the game under test
    std::uint64_t nonce = 0;        // from its hello
    std::uint64_t key = 0;          // the host key its hello carried
    std::size_t helloBytes = 0;
    int hellos = 0;
    FakeRelay() { sock.Open(); sock.Bind(0); }
    std::uint16_t Port() const { return sock.LocalPort(); }
    // Read every pending datagram; parse hellos ([tag][role][string code][ver][key][cookie][nonce]).
    void Drain() {
        std::uint8_t buf[2048]; Endpoint from; int n;
        while ((n = sock.RecvFrom(buf, sizeof buf, from)) > 0) {
            if (buf[0] != RelayHello) continue;
            client = from;
            ++hellos;
            helloBytes = static_cast<std::size_t>(n);
            Packet p(buf, static_cast<std::size_t>(n));
            p.ReadU8(); p.ReadU8();
            const std::string code = p.ReadString();
            p.ReadU8();
            const std::uint32_t kl = p.ReadU32(), kh = p.ReadU32();
            key = (static_cast<std::uint64_t>(kh) << 32) | kl;
            for (int i = 0; i < 16; ++i) p.ReadU8();
            const std::uint32_t nl = p.ReadU32(), nh = p.ReadU32();
            if (p.Ok()) nonce = (static_cast<std::uint64_t>(nh) << 32) | nl;
        }
    }
    void Send(const Packet& p) { sock.SendTo(client, p.Data(), p.Size()); }
    // A welcome as the relay builds it; `nonceEcho` appended when non-null.
    static Packet Welcome(std::uint32_t slot, std::uint32_t host, std::uint64_t hostKey,
                          const std::uint64_t* nonceEcho) {
        Packet w(RelayWelcome);
        w.Write(slot); w.Write(host); w.Write(kRelayProtocolVersion);
        w.Write(static_cast<std::uint32_t>(hostKey)); w.Write(static_cast<std::uint32_t>(hostKey >> 32));
        if (nonceEcho) { w.Write(static_cast<std::uint32_t>(*nonceEcho)); w.Write(static_cast<std::uint32_t>(*nonceEcho >> 32)); }
        return w;
    }
};

// One game under test plus the fake relay. Ticks until the relay has seen a hello.
struct Rig {
    FakeRelay relay;
    Scene scene{"Net"};
    NetworkManager* nm = nullptr;
    Rig(bool host, float timeout = 15.0f, bool legacy = false) {
        scene.physicsEnabled = false;
        nm = scene.CreateGameObject("Net")->AddComponent<NetworkManager>();
        nm->relayTimeoutSeconds = timeout;
        nm->allowUnverifiedRelay = legacy;
        if (host) { nm->serverName = "H"; nm->HostViaRelay("127.0.0.1", relay.Port(), "ROOM"); }
        else nm->JoinViaRelay("127.0.0.1", relay.Port(), "ROOM");
        scene.Start();
    }
    void Tick(float dt = 0.02f, int n = 1) { for (int i = 0; i < n; ++i) { scene.Update(dt); Nap(); } }
    void WaitHello() { for (int i = 0; i < 200 && relay.hellos == 0; ++i) { Tick(); relay.Drain(); } }
};

// A raw version-4 peer for the real RelayServer: does the cookie round, returns the welcome bytes.
std::vector<std::uint8_t> Handshake(RelayServer& relay, RelayRole role, const std::string& code,
                                    std::uint8_t version, const std::uint64_t* nonce) {
    UdpSocket s; s.Open(); s.Bind(0);
    const Endpoint to{0x7F000001u, relay.Port()};
    std::uint8_t cookie[kRelayCookieBytes] = {};
    std::vector<std::uint8_t> welcome;
    for (int round = 0; round < 40 && welcome.empty(); ++round) {
        Packet h(RelayHello); h.Write(static_cast<std::uint8_t>(role)); h.Write(code);
        h.Write(version); h.Write(std::uint32_t(0)); h.Write(std::uint32_t(0));
        for (std::uint8_t b : cookie) h.Write(b);
        if (nonce) { h.Write(static_cast<std::uint32_t>(*nonce)); h.Write(static_cast<std::uint32_t>(*nonce >> 32)); }
        while (h.Size() < kRelayHelloMinBytes) h.Write(std::uint8_t(0));
        s.SendTo(to, h.Data(), h.Size());
        for (int i = 0; i < 5; ++i) { relay.Poll(0.01f); Nap(); }
        std::uint8_t buf[256]; Endpoint from; int n;
        while ((n = s.RecvFrom(buf, sizeof buf, from)) > 0) {
            if (buf[0] == RelayCookie && n >= 2 + (int)kRelayCookieBytes) std::memcpy(cookie, buf + 2, kRelayCookieBytes);
            else if (buf[0] == RelayWelcome) welcome.assign(buf, buf + n);
        }
    }
    return welcome;
}

} // namespace

int main() {
    RUN_SUITE("net_relay_hardening");

    // ---- #701: a relay that never answers ends the attempt with an error ----
    {
        Rig c(false, 1.0f);
        c.WaitHello();
        CHECK(c.relay.hellos >= 1);
        CHECK(c.nm->Relayed() && !c.nm->RelayReady() && c.nm->RelayError().empty());
        c.Tick(0.2f, 3);
        CHECK(c.nm->Relayed());                  // not yet: 0.6 s of 1 s
        c.Tick(0.2f, 4);
        CHECK(!c.nm->Relayed());                 // given up, and stopped
        CHECK(c.nm->RelayError().find("never answered") != std::string::npos);

        Rig h(true, 1.0f);                       // a host waits on the relay too
        h.Tick(0.4f, 4);
        CHECK(!h.nm->Relayed());
        CHECK(h.nm->RelayError().find("never answered") != std::string::npos);

        Rig f(false, 0.0f);                      // 0 = wait forever
        f.Tick(1.0f, 60);
        CHECK(f.nm->Relayed() && f.nm->RelayError().empty());

        Rig ok(false, 1.0f);                     // an answer stops the clock
        ok.WaitHello();
        const std::uint64_t n = ok.relay.nonce;
        ok.relay.Send(FakeRelay::Welcome(2, 1, 0, &n));
        ok.Tick(0.2f, 3);
        CHECK(ok.nm->RelayReady());
        ok.Tick(0.2f, 10);
        CHECK(ok.nm->Relayed() && ok.nm->RelayReady() && ok.nm->RelayError().empty());
    }

    // ---- #702: a welcome has to echo our nonce ----
    {
        Rig c(false);
        c.WaitHello();
        CHECK(c.relay.nonce != 0);               // the hello carries one
        CHECK(c.relay.helloBytes >= kRelayHelloMinBytes);
        const std::uint64_t good = c.relay.nonce, bad = good ^ 0x1234;
        c.relay.Send(FakeRelay::Welcome(2, 1, 0, nullptr));       // no nonce: a forger can't know it
        c.Tick(0.02f, 5);
        CHECK(!c.nm->RelayReady());
        c.relay.Send(FakeRelay::Welcome(2, 1, 0, &bad));          // wrong nonce
        c.Tick(0.02f, 5);
        CHECK(!c.nm->RelayReady());
        c.relay.Send(FakeRelay::Welcome(2, 0, 0, &bad));          // forged "host left"
        c.Tick(0.02f, 5);
        CHECK(!c.nm->RelayReady() && c.nm->Relayed() && c.nm->RelayError().empty());
        c.relay.Send(FakeRelay::Welcome(2, 1, 0, &good));         // the real one
        c.Tick(0.02f, 5);
        CHECK(c.nm->RelayReady());
        // Once paired, a forged welcome can't end the session either.
        c.relay.Send(FakeRelay::Welcome(2, 0, 0, &bad));
        c.Tick(0.02f, 5);
        CHECK(c.nm->Relayed() && c.nm->RelayError().empty());
        c.relay.Send(FakeRelay::Welcome(2, 0, 0, &good));         // the genuine "host left"
        c.Tick(0.02f, 5);
        CHECK(!c.nm->Relayed() && c.nm->RelayError().find("host left") != std::string::npos);

        // A relay older than version 4 can't echo: only with allowUnverifiedRelay.
        Rig legacy(false, 15.0f, true);
        legacy.WaitHello();
        legacy.relay.Send(FakeRelay::Welcome(2, 1, 0, nullptr));
        legacy.Tick(0.02f, 5);
        CHECK(legacy.nm->RelayReady());

        // Strict mode that only ever sees unverifiable welcomes says why it gave up.
        Rig strict(false, 1.0f);
        strict.WaitHello();
        strict.relay.Send(FakeRelay::Welcome(2, 1, 0, nullptr));
        strict.Tick(0.2f, 8);
        CHECK(!strict.nm->Relayed() && strict.nm->RelayError().find("could not be verified") != std::string::npos);

        // A host: a forged welcome can't plant a host key, and the real one's key comes back in hellos.
        Rig h(true);
        h.WaitHello();
        const std::uint64_t hn = h.relay.nonce, forged = 0xDEADBEEFCAFEF00Dull, real = 0x1122334455667788ull;
        h.relay.Send(FakeRelay::Welcome(1, 1, forged, nullptr));
        h.Tick(1.1f, 2); h.relay.Drain();
        CHECK(!h.nm->RelayReady() && h.relay.key == 0);
        h.relay.Send(FakeRelay::Welcome(1, 1, real, &hn));
        h.Tick(0.02f, 5);
        CHECK(h.nm->RelayReady());
        h.Tick(1.1f, 2); h.relay.Drain();
        CHECK(h.relay.key == real);
    }

    // ---- #704: refusal reasons ----
    {
        struct Case { std::uint8_t why; const char* has; } cases[] = {
            {RelayRefusedHosted, "already hosted"},
            {RelayRefusedVersion, "protocol version 9"},
            {77, "unknown reason (code 77)"},
            {0, "unknown reason (code 0)"},
        };
        for (const auto& k : cases) {
            Rig c(false);
            c.WaitHello();
            Packet r(RelayRefused); r.Write(k.why); r.Write(std::uint8_t(9));
            c.relay.Send(r);
            c.Tick(0.02f, 5);
            const std::string e = c.nm->RelayError();
            CHECK(e.find(k.has) != std::string::npos);
            if (k.why != RelayRefusedVersion) CHECK(e.find("protocol version") == std::string::npos);
            CHECK(!c.nm->Relayed());
        }
    }

    // ---- #705: a truncated welcome isn't accepted by a host ----
    {
        Rig h(true, 15.0f, true);                 // unverified welcomes allowed: only the layout is under test
        h.WaitHello();
        Packet shortW(RelayWelcome); shortW.Write(std::uint32_t(1)); shortW.Write(std::uint32_t(1));   // 9 bytes
        h.relay.Send(shortW);
        h.Tick(0.02f, 5);
        CHECK(!h.nm->RelayReady());
        h.relay.Send(FakeRelay::Welcome(1, 1, 0x42, nullptr));
        h.Tick(0.02f, 5);
        CHECK(h.nm->RelayReady());

        Rig c(false, 15.0f, true);                // a version-1 relay's short welcome is fine for a client
        c.WaitHello();
        c.relay.Send(shortW);
        c.Tick(0.02f, 5);
        CHECK(c.nm->RelayReady());
    }

    // ---- #705: control frames are drained a bounded amount per call ----
    {
        Rig c(false);
        c.WaitHello();
        const std::uint64_t n = c.relay.nonce;
        Packet junk(std::uint8_t(0xEE)); junk.Write(std::uint32_t(0));
        for (int i = 0; i < 150; ++i) c.relay.Send(junk);
        c.relay.Send(FakeRelay::Welcome(2, 1, 0, &n));
        c.Tick(0.02f, 1);
        CHECK(!c.nm->RelayReady());              // one tick didn't swallow all 150 frames
        for (int i = 0; i < 40 && !c.nm->RelayReady(); ++i) c.Tick(0.02f, 1);
        CHECK(c.nm->RelayReady());               // but they drain over a few ticks
    }

    // ---- the real relay: v4 welcomes echo the nonce, older peers get the old welcome ----
    {
        RelayServer relay;
        CHECK(relay.Start(0));
        const std::uint64_t nonce = 0x0807060504030201ull;
        auto w4 = Handshake(relay, RelayHost, "R4", 4, &nonce);
        CHECK(w4.size() == 26 && w4[0] == RelayWelcome);
        CHECK(w4.size() == 26 && w4[9] == 4);                          // relay version
        const std::uint8_t expectNonce[8] = {1, 2, 3, 4, 5, 6, 7, 8};  // little-endian
        CHECK(w4.size() == 26 && std::memcmp(w4.data() + 18, expectNonce, 8) == 0);
        auto w3 = Handshake(relay, RelayHost, "R3", 3, nullptr);
        CHECK(w3.size() == 18 && w3[0] == RelayWelcome);
        auto w4n = Handshake(relay, RelayHost, "R4N", 4, nullptr);      // a v4 hello without the trailer
        CHECK(w4n.size() == 18);

        // The slot the relay stamps on forwarded data is little-endian.
        UdpSocket hs, cs; hs.Open(); hs.Bind(0); cs.Open(); cs.Bind(0);
        const Endpoint to{0x7F000001u, relay.Port()};
        std::uint8_t hc[kRelayCookieBytes] = {}, cc[kRelayCookieBytes] = {};
        auto hello = [&](UdpSocket& s, RelayRole role, std::uint8_t* ck) {
            Packet h(RelayHello); h.Write(static_cast<std::uint8_t>(role)); h.Write(std::string("RD"));
            h.Write(kRelayProtocolVersion); h.Write(std::uint32_t(0)); h.Write(std::uint32_t(0));
            for (int i = 0; i < 16; ++i) h.Write(ck[i]);
            while (h.Size() < kRelayHelloMinBytes) h.Write(std::uint8_t(0));
            s.SendTo(to, h.Data(), h.Size());
        };
        std::uint32_t hostSlot = 0, clientSlot = 0;
        for (int round = 0; round < 6; ++round) {
            hello(hs, RelayHost, hc); hello(cs, RelayClient, cc);
            for (int i = 0; i < 6; ++i) { relay.Poll(0.01f); Nap(); }
            std::uint8_t b[256]; Endpoint f; int n;
            while ((n = hs.RecvFrom(b, sizeof b, f)) > 0) {
                if (b[0] == RelayCookie) std::memcpy(hc, b + 2, 16);
                else if (b[0] == RelayWelcome) hostSlot = LoadU32LE(b + 1);
            }
            while ((n = cs.RecvFrom(b, sizeof b, f)) > 0) {
                if (b[0] == RelayCookie) std::memcpy(cc, b + 2, 16);
                else if (b[0] == RelayWelcome) clientSlot = LoadU32LE(b + 1);
            }
        }
        CHECK(hostSlot != 0 && clientSlot != 0);
        std::uint8_t d[6] = {RelayData, 0, 0, 0, 0, 0x55};
        StoreU32LE(d + 1, hostSlot);
        cs.SendTo(to, d, sizeof d);
        for (int i = 0; i < 6; ++i) { relay.Poll(0.01f); Nap(); }
        std::uint8_t b[64]; Endpoint f; int n, got = 0;
        while ((n = hs.RecvFrom(b, sizeof b, f)) > 0) {
            if (b[0] != RelayData || n != 6) continue;
            got = 1;
            CHECK(b[1] == (clientSlot & 0xFF) && b[2] == ((clientSlot >> 8) & 0xFF) &&
                  b[3] == ((clientSlot >> 16) & 0xFF) && b[4] == ((clientSlot >> 24) & 0xFF));
            CHECK(b[5] == 0x55);
        }
        CHECK(got == 1);
        relay.Stop();
    }

    TEST_MAIN_RESULT();
}
