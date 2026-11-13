#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Net/Packet.hpp"
#include "okay/Net/UdpSocket.hpp"
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace okay;
using namespace okay::net;

// Rejoin and replay edge cases around the resume token (docs/multiplayer_audit.md):
//   * a token is single-use, bound to its account, and dead after a kick
//   * a full host still lets a dropped client take its own slot back
//   * the parked states are bounded
//   * an old Leave (captured earlier) cannot remove a player who has rejoined,
//     and a duplicate Join datagram changes nothing

namespace {
void Nap() { std::this_thread::sleep_for(std::chrono::milliseconds(2)); }

struct Host {
    Scene ss{"Server"};
    NetworkManager* srv = nullptr;
    Host() {
        ss.physicsEnabled = false;
        srv = ss.CreateGameObject("Net")->AddComponent<NetworkManager>();
        srv->interpolationRate = 0.0f;
        CHECK(srv->StartServer(0));
        ss.Start();
    }
};
struct Guest {
    Scene cs{"Client"};
    NetworkManager* cli = nullptr;
    explicit Guest(const std::string& name) {
        cs.physicsEnabled = false;
        cli = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        cli->interpolationRate = 0.0f;
        cli->SetLocalName(name);
    }
    bool Join(Host& h, std::uint64_t resume = 0, int steps = 400) {
        if (!cli->StartClient("127.0.0.1", h.srv->ServerPort())) return false;
        if (resume) cli->SetResumeToken(resume);
        cs.Start();
        for (int i = 0; i < steps && !cli->Joined(); ++i) { h.ss.Update(0.02f); cs.Update(0.02f); Nap(); }
        return cli->Joined();
    }
};
void Pump(Host& h, int n) { for (int i = 0; i < n; ++i) { h.ss.Update(0.02f); Nap(); } }

// What a raw peer needs from a Welcome.
struct Welcomed { bool ok = false; std::uint32_t id = 0; std::uint64_t cookie = 0, token = 0; std::uint8_t flags = 0; };
Welcomed RawJoin(Host& h, UdpSocket& sock, const std::string& name, std::uint64_t resume = 0) {
    Packet j(std::uint8_t(1));
    j.Write(name); j.Write(std::string()); j.Write(std::string()); j.Write(std::string()); j.Write(std::string());
    if (resume) { j.Write(static_cast<std::uint32_t>(resume)); j.Write(static_cast<std::uint32_t>(resume >> 32)); }
    const Endpoint to{0x7F000001u, h.srv->ServerPort()};
    sock.SendTo(to, j.Data(), j.Size());
    Welcomed w;
    for (int i = 0; i < 40 && !w.ok; ++i) {
        Pump(h, 1);
        std::uint8_t buf[1500]; Endpoint from; int n;
        while ((n = sock.RecvFrom(buf, sizeof buf, from)) > 0) {
            Packet p(buf, (std::size_t)n);
            if (p.ReadU8() != 2) continue;
            w.id = p.ReadU32(); p.ReadString(); p.ReadString(); p.ReadString();
            const std::uint8_t flags = p.ReadU8();
            w.flags = flags;
            if (flags & 2) p.ReadF32();
            auto u64 = [&] { const std::uint64_t lo = p.ReadU32(); return lo | ((std::uint64_t)p.ReadU32() << 32); };
            if (flags & 16) w.cookie = u64();
            if (flags & 32) w.token = u64();
            w.ok = p.Ok();
        }
    }
    return w;
}
void RawLeave(Host& h, UdpSocket& sock, std::uint32_t id, std::uint64_t cookie, bool withCookie) {
    Packet l(std::uint8_t(5)); l.Write(id);
    if (withCookie) { l.Write(static_cast<std::uint32_t>(cookie)); l.Write(static_cast<std::uint32_t>(cookie >> 32)); }
    sock.SendTo(Endpoint{0x7F000001u, h.srv->ServerPort()}, l.Data(), l.Size());
    Pump(h, 5);
}
void RawState(Host& h, UdpSocket& sock, std::uint32_t id, std::uint64_t cookie) {
    Packet s(std::uint8_t(3)); s.Write(id); s.Write(1.0f); s.Write(0.0f); s.Write(1.0f); s.Write(std::uint8_t('@'));
    s.Write(static_cast<std::uint32_t>(cookie)); s.Write(static_cast<std::uint32_t>(cookie >> 32));
    sock.SendTo(Endpoint{0x7F000001u, h.srv->ServerPort()}, s.Data(), s.Size());
    Pump(h, 3);
}
} // namespace

int main() {
    RUN_SUITE("net_rejoin");
    auto sink = Log::sink;
    Log::sink = [](Log::Level, const std::string&) {};

    // ---- A token resumes once; a second holder of it is a new player ----
    {
        Host h;
        Guest a("A");
        CHECK(a.Join(h));
        const std::uint32_t id = a.cli->LocalId();
        const std::uint64_t token = a.cli->ResumeToken();
        CHECK(token != 0);
        h.srv->SetPeerScore(id, 8);
        a.cli->Stop();                                  // leaves: the host parks its state
        Pump(h, 10);
        CHECK(h.srv->Peers().empty() && h.srv->ParkedCount() == 1);
        Guest a2("A again");
        CHECK(a2.Join(h, token));
        CHECK(a2.cli->Resumed() && a2.cli->LocalId() == id);
        CHECK(h.srv->PeerScore(id) == 8);
        CHECK(a2.cli->ResumeToken() != token);          // a new one for the next drop
        CHECK(h.srv->ParkedCount() == 0);
        // The old token is spent: another client presenting it gets a fresh identity.
        Guest thief("Thief");
        CHECK(thief.Join(h, token));
        CHECK(!thief.cli->Resumed());
        CHECK(thief.cli->LocalId() != id);
        CHECK(h.srv->PeerScore(thief.cli->LocalId()) == 0);
        CHECK(h.srv->Peers().size() == 2);              // the real one is untouched
        CHECK(h.srv->PeerScore(id) == 8);
    }

    // ---- A token is bound to its account ----
    {
        Host h;
        h.srv->SetTokenVerifier([](const std::string& token, std::string& user) {
            if (token == "tokA") { user = "userA"; return true; }
            if (token == "tokB") { user = "userB"; return true; }
            return false;
        });
        Guest a("A"); a.cli->SetAuthToken("tokA"); a.cli->authTokenToAnyHost = true;
        CHECK(a.Join(h));
        const std::uint32_t id = a.cli->LocalId();
        const std::uint64_t token = a.cli->ResumeToken();
        h.srv->SetPeerScore(id, 6);
        // B, signed in as someone else, holds A's token while A is still connected.
        Guest b("B"); b.cli->SetAuthToken("tokB"); b.cli->authTokenToAnyHost = true;
        CHECK(b.Join(h, token));
        CHECK(!b.cli->Resumed() && b.cli->LocalId() != id);
        CHECK(h.srv->PeerScore(b.cli->LocalId()) == 0);
        CHECK(h.srv->Peers().size() == 2);              // A was not pushed out
        // And once A leaves, B still cannot take A's parked state.
        a.cli->Stop();
        Pump(h, 10);
        Guest b2("B2"); b2.cli->SetAuthToken("tokB"); b2.cli->authTokenToAnyHost = true;
        CHECK(b2.Join(h, token));
        CHECK(!b2.cli->Resumed());
        // A, with its own token on its own account, does resume (burned by B's attempt above? no: B's
        // attempt did not match A's parked entry's account, which keeps the entry, so A still can).
        Guest a2("A2"); a2.cli->SetAuthToken("tokA"); a2.cli->authTokenToAnyHost = true;
        CHECK(a2.Join(h, token));
    }

    // ---- A kicked player's state is not kept ----
    {
        Host h;
        Guest a("A");
        CHECK(a.Join(h));
        const std::uint64_t token = a.cli->ResumeToken();
        h.srv->SetPeerScore(a.cli->LocalId(), 9);
        h.srv->Kick(a.cli->LocalId(), "rude");
        Pump(h, 5);
        CHECK(h.srv->ParkedCount() == 0);
        Guest again("A");
        CHECK(again.Join(h, token));
        CHECK(!again.cli->Resumed());
        // And a kicked client does not try to reconnect by itself.
        Guest k("K"); k.cli->autoReconnect = true;
        CHECK(k.Join(h));
        h.srv->Kick(k.cli->LocalId(), "bye");
        for (int i = 0; i < 100; ++i) { h.ss.Update(0.02f); k.cs.Update(0.02f); Nap(); }
        CHECK(k.cli->WasKicked() && !k.cli->Reconnecting() && !k.cli->Joined());
    }

    // ---- A full host lets a dropped client take its own slot back ----
    {
        Host h;
        h.srv->maxPlayers = 1;
        Guest a("A"); a.cli->autoReconnect = true; a.cli->reconnectDelaySeconds = 0.3f;
        CHECK(a.Join(h));
        const std::uint32_t id = a.cli->LocalId();
        // The host hangs; the client gives up the session and comes back to a host that still
        // counts the old connection (so it is full).
        for (int i = 0; i < 200 && a.cli->IsConnected(); ++i) { a.cs.Update(0.25f); Nap(); }
        CHECK(a.cli->Reconnecting());
        bool back = false;
        for (int i = 0; i < 400 && !back; ++i) { h.ss.Update(0.05f); a.cs.Update(0.05f); Nap(); back = a.cli->Joined(); }
        CHECK(back);
        CHECK(a.cli->Resumed() && a.cli->LocalId() == id);
        CHECK(h.srv->Peers().size() == 1);
        // A stranger is still turned away.
        Guest s("S");
        CHECK(!s.Join(h, 0, 60));
        CHECK(s.cli->JoinRejected());
    }

    // ---- The parked states are bounded ----
    {
        Host h;
        h.srv->maxPlayers = 100;
        const int total = (int)NetworkManager::kMaxParked + 20;
        for (int i = 0; i < total; ++i) {
            Guest g("G" + std::to_string(i));
            CHECK(g.Join(h));
            g.cli->Stop();
            Pump(h, 3);
        }
        CHECK(h.srv->ParkedCount() == NetworkManager::kMaxParked);
    }

    // ---- A Welcome lost on the wire: the resent one still says the state was resumed ----
    {
        Host h;
        Guest a("A");
        CHECK(a.Join(h));
        const std::uint32_t id = a.cli->LocalId();
        const std::uint64_t token = a.cli->ResumeToken();
        h.srv->SetPeerScore(id, 12);
        a.cli->Stop();
        Pump(h, 10);
        UdpSocket sock; sock.Open(); sock.Bind(0);
        Welcomed first = RawJoin(h, sock, "A", token);       // (pretend this Welcome never arrived)
        CHECK(first.ok && first.id == id && (first.flags & 64));
        Welcomed again = RawJoin(h, sock, "A", token);       // the client's retry: the token is already spent
        CHECK(again.ok && again.id == id && (again.flags & 64));
        CHECK(h.srv->PeerScore(id) == 12 && h.srv->Peers().size() == 1);
    }

    // ---- Replayed and duplicated datagrams ----
    {
        Host h;
        UdpSocket sock; sock.Open(); sock.Bind(0);
        Welcomed w1 = RawJoin(h, sock, "Raw");
        CHECK(w1.ok && w1.cookie != 0);
        CHECK(h.srv->Peers().size() == 1);
        // The same Join again (a duplicate on the wire): nothing new, same player.
        Welcomed dup = RawJoin(h, sock, "Raw");
        CHECK(dup.ok && dup.id == w1.id && dup.cookie == w1.cookie);
        CHECK(h.srv->Peers().size() == 1);
        // A Leave with the wrong cookie does nothing; the right one removes the player.
        RawLeave(h, sock, w1.id, w1.cookie ^ 0xFFull, true);
        CHECK(h.srv->Peers().size() == 1);
        RawLeave(h, sock, w1.id, w1.cookie, true);
        CHECK(h.srv->Peers().empty());
        // The same socket joins again and gets a new cookie; the captured Leave cannot remove it.
        Welcomed w2 = RawJoin(h, sock, "Raw");
        CHECK(w2.ok && w2.cookie != 0 && w2.cookie != w1.cookie);
        RawLeave(h, sock, w2.id, w1.cookie, true);      // replayed from the first session
        CHECK(h.srv->Peers().size() == 1);
        // Once it has proved the cookie, a Leave without one is not taken either.
        RawState(h, sock, w2.id, w2.cookie);
        RawLeave(h, sock, w2.id, 0, false);
        CHECK(h.srv->Peers().size() == 1);
        RawLeave(h, sock, w2.id, w2.cookie, true);
        CHECK(h.srv->Peers().empty());
    }

    // ---- A player who left does not stay behind as a frozen stand-in on the others' screens ----
    {
        Host h;
        Guest watcher("Watcher");
        int made = 0;
        watcher.cli->SetRemoteFactory([&](std::uint32_t, char) { ++made; return watcher.cs.CreateGameObject("Stand-in"); });
        CHECK(watcher.Join(h));
        auto standIns = [&] {
            int n = 0;
            for (const auto& o : watcher.cs.Objects()) if (o && o->name == "Stand-in") ++n;
            return n;
        };
        {
            Guest leaver("Leaver");
            CHECK(leaver.Join(h));
            GameObject* av = leaver.cs.CreateGameObject("Avatar");
            av->transform->localPosition = Vec3(3.0f, 0.0f, 4.0f);
            leaver.cli->SetLocalAvatar(av->transform);
            for (int i = 0; i < 300 && standIns() < 2; ++i) { h.ss.Update(0.02f); leaver.cs.Update(0.02f); watcher.cs.Update(0.02f); Nap(); }
            CHECK(standIns() == 2 && made == 2);   // the host's and the leaver's
            leaver.cli->Stop();
        }
        // The host's roster (once a second) no longer lists it.
        for (int i = 0; i < 400 && standIns() > 1; ++i) { h.ss.Update(0.02f); watcher.cs.Update(0.02f); Nap(); }
        CHECK(standIns() == 1);                         // only the host's is left
        CHECK(watcher.cli->Joined());
    }

    Log::sink = sink;
    TEST_MAIN_RESULT();
}
