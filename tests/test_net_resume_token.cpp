#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Net/UdpSocket.hpp"
#include "okay/Net/SecureChannel.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace okay;

// What the resume token guarantees (docs/multiplayer_audit.md, "Resume token"):
//   * it is a bearer secret, single use, 64 random bits: an off-path sender (no sight of the
//     Welcome) cannot guess one, and a token already used or never issued gives a fresh peer
//     with none of the old state;
//   * in a sealed (encrypted) session the Welcome that carries it and the Join that presents
//     it are AEAD-protected: it is never in a datagram in the clear, and a captured rejoin
//     datagram replayed from another address resumes nothing and evicts nobody;
//   * on an unencrypted session there is no key to bind it to, so anyone who can read the
//     datagrams can use it; a host that does not accept that sets resumeRequiresEncryption and
//     then hands out no token to such a client.

namespace {
void Nap() { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }

// A recording UDP middlebox: clients talk to `Port()`, the host sees one upstream socket per
// client address, so a client that rejoins from a new socket is a new address to the host.
struct Proxy {
    net::UdpSocket front;
    net::Endpoint server;
    struct Leg { net::Endpoint client; std::unique_ptr<net::UdpSocket> up; };
    std::vector<Leg> legs;
    std::vector<std::vector<std::uint8_t>> all;                                    // every datagram, both ways
    std::vector<std::pair<std::size_t, std::vector<std::uint8_t>>> fromClient;     // (leg index, bytes)
    bool drop = false;
    explicit Proxy(std::uint16_t serverPort) {
        front.Open(); front.Bind(0);
        server = {0x7F000001u, serverPort};
    }
    std::uint16_t Port() const { return front.LocalPort(); }
    void Pump() {
        std::uint8_t buf[65536]; net::Endpoint from; int n;
        while ((n = front.RecvFrom(buf, sizeof buf, from)) > 0) {
            std::size_t i = 0;
            while (i < legs.size() && !(legs[i].client == from)) ++i;
            if (i == legs.size()) {
                Leg l; l.client = from; l.up = std::make_unique<net::UdpSocket>(); l.up->Open(); l.up->Bind(0);
                legs.push_back(std::move(l));
            }
            all.emplace_back(buf, buf + n);
            fromClient.emplace_back(i, std::vector<std::uint8_t>(buf, buf + n));
            if (!drop) legs[i].up->SendTo(server, buf, (std::size_t)n);
        }
        for (auto& l : legs)
            while ((n = l.up->RecvFrom(buf, sizeof buf, from)) > 0) {
                all.emplace_back(buf, buf + n);
                if (!drop) front.SendTo(l.client, buf, (std::size_t)n);
            }
    }
    // Send `bytes` to the host from a brand-new address (an attacker replaying a capture).
    void Replay(const std::vector<std::uint8_t>& bytes) {
        net::UdpSocket s; s.Open(); s.Bind(0);
        s.SendTo(server, bytes.data(), bytes.size());
        for (int i = 0; i < 5; ++i) Nap();
    }
    bool Contains(std::uint64_t token) const {
        std::uint8_t t[8];
        for (int i = 0; i < 8; ++i) t[i] = (std::uint8_t)(token >> (8 * i));
        for (const auto& d : all)
            if (std::search(d.begin(), d.end(), t, t + 8) != d.end()) return true;
        return false;
    }
};

struct Rig {
    Scene ss{"Server"}, cs{"Client"};
    NetworkManager* srv = nullptr;
    NetworkManager* cli = nullptr;
    std::unique_ptr<Proxy> px;
    Rig() {
        ss.physicsEnabled = false; cs.physicsEnabled = false;
        srv = ss.CreateGameObject("Net")->AddComponent<NetworkManager>();
        cli = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        srv->interpolationRate = 0.0f; cli->interpolationRate = 0.0f;
        cli->autoReconnect = true;
        cli->reconnectDelaySeconds = 0.5f;
    }
    void Join() {
        CHECK(srv->StartServer(0));
        ss.Start();
        px = std::make_unique<Proxy>(srv->ServerPort());
        CHECK(cli->StartClient("127.0.0.1", px->Port()));
        cs.Start();
        for (int i = 0; i < 400 && !(cli->Joined() && srv->Peers().size() >= 1); ++i) Step(0.02f);
        CHECK(cli->Joined());
    }
    void Step(float dt) {
        px->Pump(); ss.Update(dt); px->Pump(); cs.Update(dt); Nap();
    }
    // A full outage until the client gave the session up and the host dropped it.
    void Outage() {
        px->drop = true;
        for (int i = 0; i < 300 && (cli->IsConnected() || srv->Peers().size() > 0); ++i) Step(0.1f);
        px->drop = false;
    }
    bool Until(const std::function<bool()>& done, int steps, float dt = 0.05f) {
        for (int i = 0; i < steps && !done(); ++i) Step(dt);
        return done();
    }
};
} // namespace

int main() {
    RUN_SUITE("net_resume_token");
    auto sink = Log::sink;
    Log::sink = [](Log::Level, const std::string&) {};

    // ---- Sealed session: the token is never on the wire in the clear; a replayed rejoin gets nothing ----
    if (net::SecureChannel::Available()) {
        Rig r;
        r.srv->encryption = true; r.cli->encryption = true;
        r.Join();
        CHECK(r.cli->Encrypted() && r.cli->SealedJoin());
        const std::uint32_t id = r.cli->LocalId();
        const std::uint64_t t1 = r.cli->ResumeToken();
        CHECK(t1 != 0);
        r.srv->SetPeerScore(id, 33);
        for (int i = 0; i < 20; ++i) r.Step(0.05f);
        CHECK(!r.px->Contains(t1));              // not in the Welcome, not in any later datagram
        r.Outage();
        CHECK(r.cli->Reconnecting());
        CHECK(r.Until([&] { return r.cli->Joined(); }, 800));
        CHECK(r.cli->Resumed() && r.cli->LocalId() == id && r.srv->PeerScore(id) == 33);
        const std::uint64_t t2 = r.cli->ResumeToken();
        CHECK(t2 != 0 && t2 != t1);               // single use: the rejoin got a fresh token
        for (int i = 0; i < 20; ++i) r.Step(0.05f);
        CHECK(!r.px->Contains(t1) && !r.px->Contains(t2));   // neither the presented token nor the new one
        // An eavesdropper replays everything the client sent on its rejoin leg from another address.
        const std::size_t legs = r.px->legs.size();
        CHECK(legs >= 2);
        const std::size_t peersBefore = r.srv->Peers().size();
        const std::size_t parkedBefore = r.srv->ParkedCount();
        auto captured = r.px->fromClient;
        for (auto& [leg, bytes] : captured) if (leg == legs - 1) r.px->Replay(bytes);
        for (int i = 0; i < 40; ++i) r.Step(0.05f);
        CHECK(r.srv->Peers().size() == peersBefore);          // nobody new, nobody evicted
        CHECK(r.srv->ParkedCount() == parkedBefore);
        CHECK(r.cli->Joined() && !r.cli->ConnectionLost());
        CHECK(r.srv->PeerScore(id) == 33);
    }

    // ---- Unencrypted: an off-path sender cannot guess a token; a spent or invented one is a new peer ----
    {
        Rig r;
        r.Join();
        const std::uint32_t id = r.cli->LocalId();
        const std::uint64_t spent = r.cli->ResumeToken();
        CHECK(spent != 0);
        // The stated limit, and the control for the sealed test's search: with no encryption the
        // Welcome shows the token to anyone who can read the datagrams.
        CHECK(r.px->Contains(spent));
        r.srv->SetPeerScore(id, 8);
        r.Outage();
        CHECK(r.Until([&] { return r.cli->Joined(); }, 800));
        CHECK(r.cli->Resumed() && r.cli->LocalId() == id && r.srv->PeerScore(id) == 8);
        CHECK(r.cli->ResumeToken() != spent);
        // A second player presents the token the first one already used, then a made-up one.
        for (std::uint64_t forged : {spent, (std::uint64_t)0x1234567890ABCDEFull}) {
            Scene as("Attacker"); as.physicsEnabled = false;
            auto* an = as.CreateGameObject("Net")->AddComponent<NetworkManager>();
            an->interpolationRate = 0.0f;
            CHECK(an->StartClient("127.0.0.1", r.srv->ServerPort()));
            an->SetResumeToken(forged);
            as.Start();
            bool in = false;
            for (int i = 0; i < 300 && !in; ++i) { as.Update(0.02f); r.Step(0.02f); in = an->Joined(); }
            CHECK(in);
            CHECK(!an->Resumed() && an->LocalId() != id);
            CHECK(r.srv->PeerScore(an->LocalId()) == 0);       // none of the victim's state
            CHECK(r.srv->PeerScore(id) == 8);
            CHECK(r.cli->Joined());                            // and the victim was not evicted
        }
    }

    // ---- resumeRequiresEncryption: an unencrypted client is handed no token at all ----
    {
        Rig r;
        r.srv->resumeRequiresEncryption = true;
        r.Join();
        CHECK(!r.cli->Encrypted());
        CHECK(r.cli->ResumeToken() == 0);
        const std::uint32_t id = r.cli->LocalId();
        r.srv->SetPeerScore(id, 5);
        r.Outage();
        CHECK(r.Until([&] { return r.cli->Joined(); }, 800));
        CHECK(!r.cli->Resumed());
        CHECK(r.srv->PeerScore(r.cli->LocalId()) == 0);
    }

    // ---- ... and a sealed one still resumes under it ----
    if (net::SecureChannel::Available()) {
        Rig r;
        r.srv->resumeRequiresEncryption = true;
        r.srv->encryption = true; r.cli->encryption = true;
        r.Join();
        CHECK(r.cli->SealedJoin() && r.cli->ResumeToken() != 0);
        const std::uint32_t id = r.cli->LocalId();
        r.srv->SetPeerScore(id, 6);
        r.Outage();
        CHECK(r.Until([&] { return r.cli->Joined(); }, 800));
        CHECK(r.cli->Resumed() && r.cli->LocalId() == id && r.srv->PeerScore(id) == 6);
    }

    Log::sink = sink;
    TEST_MAIN_RESULT();
}
