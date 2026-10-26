#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Net/Packet.hpp"
#include "okay/Net/UdpSocket.hpp"
#include "okay/Net/SecureChannel.hpp"
#include "okay/Net/SessionTicket.hpp"
#include "okay/Net/DedicatedServer.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace okay;
using namespace okay::net;

// Anti-cheat plan, phase 3 (what #302 doesn't cover): a client only listens to
// its server, nobody's plaintext gets in once a session is keyed, the Join
// (password, token) goes sealed behind a versioned key exchange, sealed packets
// authenticate their header, reliable de-dup is a bounded window, a client can
// pin its host's key, and the server checks each client's game clock.
namespace {
void Nap() { std::this_thread::sleep_for(std::chrono::milliseconds(2)); }

// The engine's message numbers (NetworkManager.cpp), for packets a modified
// game could send.
enum : std::uint8_t {
    kJoin = 1, kWelcome = 2, kSyncVar = 8, kReject = 12, kKicked = 13,
    kReliableMsg = 14, kReliableAck = 15, kJoinHello = 24, kJoinKey = 25, kEnv = 0xEE,
};

struct Raw {
    UdpSocket sock;
    Endpoint to;
    explicit Raw(std::uint16_t port) { sock.Open(); sock.Bind(0); to = {0x7F000001u, port}; }
    void Send(const Packet& p) { sock.SendTo(to, p.Data(), p.Size()); }
    void SendTo(const Endpoint& e, const Packet& p) { sock.SendTo(e, p.Data(), p.Size()); }
    // Every datagram waiting, with who sent it.
    std::vector<std::pair<Endpoint, std::vector<std::uint8_t>>> Drain() {
        std::vector<std::pair<Endpoint, std::vector<std::uint8_t>>> out;
        std::uint8_t buf[65536]; Endpoint from; int n;
        while ((n = sock.RecvFrom(buf, sizeof buf, from)) > 0) out.push_back({from, {buf, buf + n}});
        return out;
    }
};

Packet LegacyJoin(const std::string& name, const std::string& pass, const std::string& token,
                  const std::vector<std::uint8_t>& pk) {
    Packet j(kJoin);
    j.Write(name); j.Write(std::string()); j.Write(pass); j.Write(token);
    j.Write(std::string(pk.begin(), pk.end()));
    return j;
}
std::vector<std::uint8_t> Sealed(SecureChannel& s, const Packet& p) {
    std::vector<std::uint8_t> out;
    s.Seal(p.Data(), p.Size(), out);
    out.insert(out.begin(), kEnv);
    return out;
}
std::string B64(const std::vector<std::uint8_t>& v) { return SessionTicket::Base64UrlEncode(v); }
std::string RandomSeed() {
    std::random_device rd;
    std::vector<std::uint8_t> s(32);
    for (auto& b : s) b = static_cast<std::uint8_t>(rd());
    return B64(s);
}
bool Contains(const std::vector<std::uint8_t>& d, const std::string& needle) {
    return std::search(d.begin(), d.end(), needle.begin(), needle.end()) != d.end();
}
} // namespace

int main() {
    RUN_SUITE("net_anticheat_ph3");
    auto sink = Log::sink;
    Log::sink = [](Log::Level, const std::string&) {};
    const bool crypto = SecureChannel::Available();

    // ---- 3c. Associated data: a packet sealed under another header doesn't open ------------
    if (crypto) {
        SecureChannel a, b;
        a.GenerateKeys(); b.GenerateKeys();
        CHECK(a.Establish(b.PublicKey(), false) && b.Establish(a.PublicKey(), true));
        const std::string msg = "hello";
        std::vector<std::uint8_t> sealed, plain;
        a.SetAssociatedData({0xEE, 2}); b.SetAssociatedData({0xEE, 2});
        CHECK(a.Seal((const std::uint8_t*)msg.data(), msg.size(), sealed));
        CHECK(b.Open(sealed.data(), sealed.size(), plain) && std::string(plain.begin(), plain.end()) == msg);
        b.SetAssociatedData({0xEE, 3});                  // another version
        CHECK(!b.Open(sealed.data(), sealed.size(), plain));
        b.SetAssociatedData({});                          // the old format
        CHECK(!b.Open(sealed.data(), sealed.size(), plain));
        a.SetAssociatedData({});
        CHECK(a.Seal((const std::uint8_t*)msg.data(), msg.size(), sealed));
        CHECK(b.Open(sealed.data(), sealed.size(), plain));   // no AD on either end: as before
    }

    // ---- 3a. Server: plaintext from a keyed client's address is dropped ------------------
    if (crypto) {
        Scene ss("Server");
        auto* srv = ss.CreateGameObject("Net")->AddComponent<NetworkManager>();
        srv->encryption = true;
        CHECK(srv->StartServer(0));
        ss.Start();
        Raw cli(srv->ServerPort());
        SecureChannel cs; cs.GenerateKeys();
        auto pump = [&](int n) { for (int i = 0; i < n; ++i) { ss.Update(0.02f); Nap(); } };
        bool keyed = false;
        for (int i = 0; i < 100 && !keyed; ++i) {   // an older (plaintext-join) client, encrypted
            cli.Send(LegacyJoin("old", "", "", cs.PublicKey()));
            pump(2);
            for (auto& [from, d] : cli.Drain()) {
                Packet w(d.data(), d.size());
                if (w.ReadU8() != kWelcome) continue;
                w.ReadU32(); w.ReadString(); w.ReadString();
                const std::string spk = w.ReadString();
                keyed = cs.Establish(std::vector<std::uint8_t>(spk.begin(), spk.end()), false);
            }
        }
        CHECK(keyed);
        Packet a(kSyncVar); a.Write(std::string("a")); a.Write(std::string("1"));
        const auto sa = Sealed(cs, a);
        cli.sock.SendTo(cli.to, sa.data(), sa.size());
        Packet b(kSyncVar); b.Write(std::string("b")); b.Write(std::string("2"));
        cli.Send(b);                                   // the same address, not sealed
        pump(10);
        CHECK(srv->GetVar("a") == "1");
        CHECK(srv->GetVar("b").empty());
        srv->Stop();
    }

    // ---- 3a. Client: only its server, and only sealed once keyed ---------------------------
    if (crypto) {
        Raw server(0);                                  // plays an older server
        const std::uint16_t serverPort = server.sock.LocalPort();
        Scene cs("Client");
        auto* cli = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        cli->encryption = true;
        CHECK(cli->StartClient("127.0.0.1", serverPort));
        cs.Start();
        SecureChannel ss; ss.GenerateKeys();
        Endpoint cliEp{};
        auto serve = [&]() {
            for (auto& [from, d] : server.Drain()) {
                cliEp = from;
                if (d.empty()) continue;
                if (d[0] == kJoinHello) {               // "no encryption here": join in the clear
                    Packet k(kJoinKey); k.Write(std::uint8_t(2)); k.Write(std::string()); k.Write(std::string());
                    server.SendTo(from, k);
                } else if (d[0] == kJoin) {
                    Packet j(d.data(), d.size()); j.ReadU8();
                    for (int i = 0; i < 4; ++i) j.ReadString();
                    const std::string cpk = j.ReadString();
                    if (!ss.Ready()) ss.Establish(std::vector<std::uint8_t>(cpk.begin(), cpk.end()), true);
                    Packet w(kWelcome); w.Write(std::uint32_t(7)); w.Write(std::string("raw")); w.Write(std::string());
                    const auto spk = ss.PublicKey(); w.Write(std::string(spk.begin(), spk.end()));
                    server.SendTo(from, w);
                }
            }
        };
        for (int i = 0; i < 200 && !cli->Joined(); ++i) { serve(); cs.Update(0.02f); Nap(); }
        CHECK(cli->Joined() && cli->Encrypted());
        Packet kick(kKicked); kick.Write(std::string("pwned"));
        Raw stranger(cli->ServerPort());
        stranger.Send(kick);                            // not the server
        server.SendTo(cliEp, kick);                      // the server's address, in the clear
        for (int i = 0; i < 10; ++i) { cs.Update(0.02f); Nap(); }
        CHECK(!cli->WasKicked() && cli->Joined());
        const auto sk = Sealed(ss, kick);               // the real thing still works
        server.sock.SendTo(cliEp, sk.data(), sk.size());
        for (int i = 0; i < 10 && !cli->WasKicked(); ++i) { cs.Update(0.02f); Nap(); }
        CHECK(cli->WasKicked() && cli->KickReason() == "pwned");
    }

    // ---- 3b. The Join goes sealed: a sniffer sees no password or token -----------------------
    if (crypto) {
        Scene ss("Server");
        auto* srv = ss.CreateGameObject("Net")->AddComponent<NetworkManager>();
        srv->encryption = true;
        srv->password = "hunter2";
        std::string seenToken;
        srv->SetTokenVerifier([&](const std::string& t, std::string& uid) { seenToken = t; uid = "u1"; return true; });
        CHECK(srv->StartServer(0));
        ss.Start();
        // A forwarder between the two records every byte.
        Raw fwd(srv->ServerPort());
        const Endpoint srvEp{0x7F000001u, srv->ServerPort()};
        Endpoint clientEp{};
        std::vector<std::vector<std::uint8_t>> wire;
        Scene cs("Client");
        auto* cli = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        cli->encryption = true;
        cli->password = "hunter2";
        cli->SetAuthToken("tok-SECRET-123");
        CHECK(cli->StartClient("127.0.0.1", fwd.sock.LocalPort()));
        cs.Start();
        for (int i = 0; i < 300 && !cli->Joined(); ++i) {
            for (auto& [from, d] : fwd.Drain()) {
                wire.push_back(d);
                if (from == srvEp) fwd.sock.SendTo(clientEp, d.data(), d.size());
                else { clientEp = from; fwd.sock.SendTo(srvEp, d.data(), d.size()); }
            }
            ss.Update(0.02f); cs.Update(0.02f); Nap();
        }
        CHECK(cli->Joined() && cli->SealedJoin() && cli->ReplayProtected());
        CHECK(seenToken == "tok-SECRET-123");            // the server got it ...
        bool leaked = false;
        for (const auto& d : wire) leaked = leaked || Contains(d, "hunter2") || Contains(d, "tok-SECRET");
        CHECK(!leaked);                                 // ... nobody else did
        CHECK(!wire.empty());
        CHECK(srv->PeerUserId(cli->LocalId()) == "u1");
        // A reconnect makes a new session (it used to keep the old keys).
        cli->Stop();
        CHECK(cli->StartClient("127.0.0.1", srv->ServerPort()));
        int heard = 0;
        srv->SetMessageHandler([&](const NetworkManager::NetMessage& m) { if (m.channel == "hi") ++heard; });
        for (int i = 0; i < 300 && !cli->Joined(); ++i) { ss.Update(0.02f); cs.Update(0.02f); Nap(); }
        CHECK(cli->Joined() && cli->Encrypted());
        cli->Send("hi", "again");
        for (int i = 0; i < 20; ++i) { ss.Update(0.02f); cs.Update(0.02f); Nap(); }
        CHECK(heard == 1);
        // A key hello shorter than the answer is ignored (no amplification).
        Raw probe(srv->ServerPort());
        Packet h(kJoinHello); h.Write(std::uint8_t(2));
        h.Write(std::string(32, 'k'));
        probe.Send(h);
        for (int i = 0; i < 5; ++i) { ss.Update(0.02f); Nap(); }
        CHECK(probe.Drain().empty());
        cli->Stop(); srv->Stop();
    }

    // ---- 3b. A server that requires encryption refuses the plaintext Join -------------------
    if (crypto) {
        Scene ss("Server");
        auto* srv = ss.CreateGameObject("Net")->AddComponent<NetworkManager>();
        srv->anticheat.settings.enabled = true;
        srv->anticheat.settings.requireEncryption = true;
        CHECK(srv->StartServer(0));
        ss.Start();
        Raw old(srv->ServerPort());
        SecureChannel oc; oc.GenerateKeys();
        std::string reply;
        for (int i = 0; i < 50 && reply.empty(); ++i) {
            old.Send(LegacyJoin("old", "", "a-ticket", oc.PublicKey()));
            ss.Update(0.02f); Nap();
            for (auto& [from, d] : old.Drain()) {
                Packet r(d.data(), d.size());
                const std::uint8_t t = r.ReadU8();
                if (t == kReject) reply = "reject: " + r.ReadString();
                else if (t == kWelcome) reply = "welcome";
            }
        }
        CHECK(reply.find("reject: encryption required") == 0);
        CHECK(srv->PlayerCount() == 1);   // just the host
        srv->Stop();
    }

    // ---- 3b / 3d. Older servers, pins ------------------------------------------------------
    if (crypto) {
        // An older server never answers the key hello: the client tries 4 times,
        // then joins in the clear. A pinned client never does.
        auto tryOldServer = [&](const std::string& pin, int& hellos, int& plainJoins, bool& joined) {
            Raw server(0);
            Scene cs("C");
            auto* cli = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
            cli->encryption = true;
            cli->pinnedHostKey = pin;
            cli->StartClient("127.0.0.1", server.sock.LocalPort());
            cs.Start();
            hellos = plainJoins = 0;
            for (int i = 0; i < 250 && !cli->Joined(); ++i) {
                for (auto& [from, d] : server.Drain()) {
                    if (d.empty()) continue;
                    if (d[0] == kJoinHello) ++hellos;
                    if (d[0] == kJoin) {
                        ++plainJoins;
                        Packet w(kWelcome); w.Write(std::uint32_t(3)); w.Write(std::string("old"));
                        w.Write(std::string()); w.Write(std::string());
                        server.SendTo(from, w);
                    }
                }
                cs.Update(0.02f); Nap();
            }
            joined = cli->Joined();
            CHECK(!cli->SealedJoin());
        };
        int hellos = 0, plain = 0; bool joined = false;
        tryOldServer("", hellos, plain, joined);
        CHECK(hellos == 4 && plain >= 1 && joined);
        tryOldServer(B64(std::vector<std::uint8_t>(32, 7)), hellos, plain, joined);
        CHECK(hellos > 4 && plain == 0 && !joined);

        // A server with an identity: the pinned key joins, another key doesn't.
        Scene ss("Server");
        auto* srv = ss.CreateGameObject("Net")->AddComponent<NetworkManager>();
        srv->encryption = true;
        CHECK(!srv->SetHostIdentity("not-a-key"));
        CHECK(srv->HostIdentityPublicKey().empty());
        CHECK(srv->SetHostIdentity(RandomSeed()));
        const std::string hostPub = srv->HostIdentityPublicKey();
        CHECK(hostPub.size() == 43);
        CHECK(srv->StartServer(0));
        ss.Start();
        Scene g("Good"), b("Bad"), n("NoPin");
        auto* good = g.CreateGameObject("Net")->AddComponent<NetworkManager>();
        auto* bad = b.CreateGameObject("Net")->AddComponent<NetworkManager>();
        auto* nopin = n.CreateGameObject("Net")->AddComponent<NetworkManager>();
        good->encryption = bad->encryption = nopin->encryption = true;
        good->pinnedHostKey = hostPub;
        std::vector<std::uint8_t> otherPk, otherSk;
        CHECK(SessionTicket::GenerateKeyPair(otherPk, otherSk));
        bad->pinnedHostKey = B64(otherPk);
        for (auto* c : {good, bad, nopin}) c->StartClient("127.0.0.1", srv->ServerPort());
        g.Start(); b.Start(); n.Start();
        for (int i = 0; i < 300 && !(good->Joined() && nopin->Joined()); ++i) {
            ss.Update(0.02f); g.Update(0.02f); b.Update(0.02f); n.Update(0.02f); Nap();
        }
        for (int i = 0; i < 100; ++i) { ss.Update(0.02f); g.Update(0.02f); b.Update(0.02f); n.Update(0.02f); Nap(); }
        CHECK(good->Joined() && good->SealedJoin());
        CHECK(nopin->Joined() && nopin->SealedJoin());
        CHECK(!bad->Joined());
        CHECK(srv->PlayerCount() == 3);
        // A pin with encryption off can't be checked: no join at all.
        Scene o("Off");
        auto* off = o.CreateGameObject("Net")->AddComponent<NetworkManager>();
        off->pinnedHostKey = hostPub;
        off->StartClient("127.0.0.1", srv->ServerPort());
        o.Start();
        for (int i = 0; i < 60; ++i) { ss.Update(0.02f); o.Update(0.02f); Nap(); }
        CHECK(!off->Joined() && srv->PlayerCount() == 3);
        srv->Stop();
    }

    // ---- 3c. Reliable de-dup is a bounded window ------------------------------------------
    {
        Scene ss("Server");
        auto* srv = ss.CreateGameObject("Net")->AddComponent<NetworkManager>();
        srv->maxMessagesPerSecond = 0;
        int got = 0;
        srv->SetMessageHandler([&](const NetworkManager::NetMessage& m) { if (m.channel == "r") ++got; });
        CHECK(srv->StartServer(0));
        ss.Start();
        Raw cli(srv->ServerPort());
        for (int i = 0; i < 50 && srv->PlayerCount() < 2; ++i) { cli.Send(LegacyJoin("r", "", "", {})); ss.Update(0.02f); Nap(); }
        CHECK(srv->PlayerCount() == 2);   // the host and the raw client
        cli.Drain();
        auto rel = [&](std::uint32_t seq) {
            Packet m(kReliableMsg); m.Write(seq); m.Write(std::string("r")); m.Write(std::string("x"));
            cli.Send(m);
            for (int i = 0; i < 3; ++i) { ss.Update(0.02f); Nap(); }
        };
        rel(1); rel(1); rel(3); rel(2);
        CHECK(got == 3);                                // a repeat runs once; late ones run
        rel(10000);
        CHECK(got == 4);
        rel(2);                                         // older than the window: not run again ...
        rel(5);                                         // ... and not run at all (counted as delivered)
        CHECK(got == 4);
        int acked5 = 0;
        for (auto& [from, d] : cli.Drain()) {
            Packet a(d.data(), d.size());
            if (a.ReadU8() == kReliableAck && a.ReadU32() == 5) ++acked5;
        }
        CHECK(acked5 == 1);                             // but acked, so the sender stops resending
        rel(9999);
        CHECK(got == 5);                                // inside the window
        srv->Stop();
    }

    // ---- 3f. The clock heartbeat ----------------------------------------------------------
    {
        Scene ss("Server");
        auto* srv = ss.CreateGameObject("Net")->AddComponent<NetworkManager>();
        srv->maxMessagesPerSecond = 0;
        srv->anticheat.settings.enabled = true;
        srv->anticheat.settings.maxClockDrift = 0.05f;
        srv->anticheat.settings.kickAfterViolations = 3;
        std::vector<CheatReport> reports;
        srv->SetCheatHandler([&](const CheatReport& r) { reports.push_back(r); });
        CHECK(srv->StartServer(0));
        ss.Start();
        Scene hs("Honest"), fs("Fast");
        auto* honest = hs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        auto* fast = fs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        honest->StartClient("127.0.0.1", srv->ServerPort());
        fast->StartClient("127.0.0.1", srv->ServerPort());
        hs.Start(); fs.Start();
        Raw silent(srv->ServerPort());                  // joins, stays, never answers the clock
        for (int i = 0; i < 300 && !(honest->Joined() && fast->Joined() && srv->PlayerCount() == 4); ++i) {
            if (i % 5 == 0) silent.Send(LegacyJoin("silent", "", "", {}));
            ss.Update(0.02f); hs.Update(0.02f); fs.Update(0.02f); Nap();
        }
        CHECK(honest->Joined() && fast->Joined() && srv->PlayerCount() == 4);
        const std::uint32_t H = honest->LocalId(), F = fast->LocalId();
        std::uint32_t S = 0;
        for (const auto& r : srv->Roster()) if (r.name == "silent") S = r.id;
        CHECK(S != 0);
        // The speed hack: the fast client's game runs 1.5x (its dt is 0.03 per 0.02).
        for (int i = 0; i < 800; ++i) {                 // 16 s of server time
            if (i % 25 == 0) silent.Send(LegacyJoin("silent", "", "", {}));   // keeps it from timing out
            ss.Update(0.02f); hs.Update(0.02f); fs.Update(0.03f); Nap();
        }
        int fastDrift = 0, honestDrift = 0, silentDrift = 0;
        std::string fastDetail, silentDetail;
        for (const auto& r : reports) {
            if (r.kind != CheatKind::ClockDrift) continue;
            if (r.peer == F) { ++fastDrift; fastDetail = r.detail; }
            if (r.peer == H) ++honestDrift;
            if (r.peer == S) { ++silentDrift; silentDetail = r.detail; }
        }
        CHECK(fast->WasKicked());
        CHECK(fastDrift >= 3);
        CHECK(fastDetail.find('+') == 0);               // "+50%"-ish: fast
        CHECK(honestDrift == 0 && honest->Joined() && !honest->WasKicked());
        CHECK(silentDrift >= 1 && silentDetail == "no clock answer");
        CHECK(std::string(CheatKindName(CheatKind::ClockDrift)) == "clock drift");
        srv->Stop();
    }

    // ---- 3f. Settings: .okayac, the scene, the inspector's range ---------------------------
    {
        AntiCheatSettings s; std::string err;
        CHECK(AntiCheatSettings::Parse("enabled true\nmaxClockDrift 0.05\n", s, &err));
        CHECK(std::fabs(s.maxClockDrift - 0.05f) < 1e-6f);
        AntiCheatSettings back;
        CHECK(AntiCheatSettings::Parse(s.ToText(), back) && std::fabs(back.maxClockDrift - 0.05f) < 1e-6f);
        CHECK(!AntiCheatSettings::Parse("maxClockDrift 2\n", s, &err));
        CHECK(!AntiCheatSettings::Parse("maxClockDrift -0.1\n", s, &err));
        CHECK(AntiCheatSettings().ToText().find("maxClockDrift") == std::string::npos);   // off: not written

        Scene a("A");
        a.CreateGameObject("Net")->AddComponent<NetworkManager>();
        auto* ac = a.CreateGameObject("R")->AddComponent<AntiCheatConfig>();
        ac->settings.maxClockDrift = 0.08f;
        const std::string path = "/tmp/okay_ph3_scene.okayscene";
        CHECK(SceneSerializer::SaveToFile(a, path));
        Scene b("B");
        CHECK(SceneSerializer::LoadFromFile(b, path));
        auto* got = b.Find("R") ? b.Find("R")->GetComponent<AntiCheatConfig>() : nullptr;
        CHECK(got && std::fabs(got->settings.maxClockDrift - 0.08f) < 1e-6f);
        std::remove(path.c_str());
    }

    // ---- 3d. The dedicated server's --host-key ---------------------------------------------
    {
        const std::string keyFile = "/tmp/okay_ph3_host.key", badFile = "/tmp/okay_ph3_bad.key";
        const std::string seed = RandomSeed();
        { std::ofstream f(keyFile); f << seed << "\n"; }
        { std::ofstream f(badFile); f << "SECRET-BUT-WRONG\n"; }
        auto parse = [](std::vector<std::string> args, DedicatedServerOptions& o, std::string& err) {
            std::vector<char*> argv;
            for (auto& a : args) argv.push_back(a.data());
            return ParseDedicatedServerArgs((int)argv.size(), argv.data(), o, &err);
        };
        DedicatedServerOptions o; std::string err;
        CHECK(parse({"okayspace-server", "--server", "--host-key", keyFile}, o, err) && err.empty());
        CHECK(o.hostKeySeed == seed && o.hostKeyFile == keyFile);
        DedicatedServerOptions o2; std::string err2;
        parse({"okayspace-server", "--server", "--host-key", badFile}, o2, err2);   // (an error is reported in err)
        CHECK(err2.find("32-byte") != std::string::npos && o2.hostKeySeed.empty());
        CHECK(err2.find("SECRET") == std::string::npos);   // the file's content isn't echoed
        std::remove(keyFile.c_str()); std::remove(badFile.c_str());
    }

    Log::sink = sink;
    TEST_MAIN_RESULT();
}
