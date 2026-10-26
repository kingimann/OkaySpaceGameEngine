#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Net/RelayServer.hpp"
#include "okay/Net/RelayProtocol.hpp"
#include "okay/Net/Packet.hpp"
#include "okay/Net/UdpSocket.hpp"

#include <chrono>
#include <cmath>
#include <string>
#include <thread>
#include <vector>

using namespace okay;
using namespace okay::net;

// Anti-cheat plan, phase 2: a relay-hosted game gets the same server checks as
// a directly hosted one, and the relay itself only forwards between a client
// and its host, can't have a session code taken over, tells clients when their
// host goes, caps each peer's rate and carries a protocol version.
namespace {
void Nap() { std::this_thread::sleep_for(std::chrono::milliseconds(2)); }

// A raw relay peer, to send what a modified game could send.
struct RawPeer {
    UdpSocket sock;
    Endpoint relay, self;
    std::uint32_t welcomeSlot = 0, welcomeHost = 0xFFFFFFFFu, welcomes = 0;
    std::uint64_t key = 0;
    std::uint8_t refused = 0, version = 0;
    std::vector<std::string> data;   // RelayData payloads

    explicit RawPeer(std::uint16_t relayPort) {
        sock.Open(); sock.Bind(0);
        relay = {0x7F000001u, relayPort};
        self = {0x7F000001u, sock.LocalPort()};
    }
    void Hello(RelayRole role, const std::string& code, std::uint64_t withKey = 0, int ver = kRelayProtocolVersion) {
        Packet p(RelayHello); p.Write(static_cast<std::uint8_t>(role)); p.Write(code);
        if (ver >= 0) {
            p.Write(static_cast<std::uint8_t>(ver));
            p.Write(static_cast<std::uint32_t>(withKey)); p.Write(static_cast<std::uint32_t>(withKey >> 32));
        }
        sock.SendTo(relay, p.Data(), p.Size());
    }
    void Data(std::uint32_t dest, const std::string& payload) {
        Packet p(RelayData); p.Write(dest);
        std::vector<std::uint8_t> d(p.Data(), p.Data() + p.Size());
        d.insert(d.end(), payload.begin(), payload.end());
        sock.SendTo(relay, d.data(), d.size());
    }
    void Bye() { const std::uint8_t b = RelayBye; sock.SendTo(relay, &b, 1); }
    void Read() {
        std::uint8_t buf[2048]; Endpoint from; int n;
        while ((n = sock.RecvFrom(buf, sizeof buf, from)) > 0) {
            Packet p(buf, static_cast<std::size_t>(n));
            const std::uint8_t tag = p.ReadU8();
            if (tag == RelayWelcome) {
                welcomeSlot = p.ReadU32(); welcomeHost = p.ReadU32();
                version = p.ReadU8();
                const std::uint32_t lo = p.ReadU32(), hi = p.ReadU32();
                if (p.Ok() && (lo | hi)) key = (static_cast<std::uint64_t>(hi) << 32) | lo;
                ++welcomes;
            } else if (tag == RelayRefused) {
                refused = p.ReadU8(); version = p.ReadU8();
            } else if (tag == RelayData && n >= 5) {
                data.emplace_back(reinterpret_cast<char*>(buf) + 5, n - 5);
            }
        }
    }
};

void Pump(RelayServer& r, std::vector<RawPeer*> peers, int frames = 15) {
    for (int i = 0; i < frames; ++i) { r.Poll(0.01f); Nap(); for (auto* p : peers) p->Read(); }
}

std::vector<std::uint8_t> DataBytes(std::uint32_t dest, std::size_t payload) {
    Packet p(RelayData); p.Write(dest);
    std::vector<std::uint8_t> d(p.Data(), p.Data() + p.Size());
    d.resize(d.size() + payload, 'x');
    return d;
}
std::vector<std::uint8_t> HelloBytes(RelayRole role, const std::string& code) {
    Packet p(RelayHello); p.Write(static_cast<std::uint8_t>(role)); p.Write(code);
    return {p.Data(), p.Data() + p.Size()};
}

Vec3 PosOf(Scene& s, const std::string& netId) {
    for (const auto& g : s.Objects())
        if (auto* ns = g->GetComponent<NetworkSync>(); ns && ns->netId == netId && g->transform)
            return g->transform->localPosition;
    return Vec3(-999.0f, -999.0f, -999.0f);
}
} // namespace

int main() {
    RUN_SUITE("net_anticheat_ph2");
    auto sink = Log::sink;
    Log::sink = [](Log::Level, const std::string&) {};

    const std::string crate = "okay_ac2_crate.okayprefab";
    { Scene t("T"); CHECK(SceneSerializer::SaveObjectToFile(*t.CreateGameObject("Crate"), crate)); }

    // ---- 2a. A relay-hosted game runs the anti-cheat like a direct one --------------------
    {
        RelayServer relay;
        CHECK(relay.Start(0));
        Scene hs("Host");
        auto* host = hs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        host->interpolationRate = 0.0f;
        host->maxMessagesPerSecond = 0;
        hs.CreateGameObject("Rules")->AddComponent<AntiCheatConfig>();   // defaults: kick after 5
        GameObject* referee = hs.CreateGameObject("Referee");
        referee->AddComponent<ScriptComponent>("okayscript")->LoadSource(
            "var cheats = 0;\nvar lastKind = \"\";\n"
            "function on_cheat_detected(kind, peer, detail) { cheats = cheats + 1; lastKind = kind; }\n");
        auto* vsc = hs.CreateGameObject("Graph")->AddComponent<VisualScriptComponent>();
        std::string err;
        CHECK(vsc->LoadFromText("node 1 OnCheatDetected\nnode 2 AddVar n\nnode 3 Const 1\n"
                                "data 2 0 3 0\nexec 1 0 2\nentry OnCheatDetected 1\n", &err));
        // A scene object the host drives (not spawned, so its id names no owner).
        GameObject* door = hs.CreateGameObject("Door");
        door->transform->localPosition = Vec3(3.0f, 0.0f, 0.0f);
        door->AddComponent<NetworkSync>()->netId = "door";
        CHECK(host->HostViaRelay("127.0.0.1", relay.Port(), "AC-ROOM"));
        hs.Start();
        // The server-side hooks are in place for a relay host too.
        CHECK(host->anticheat.serverOwns != nullptr && host->anticheat.exists != nullptr && host->anticheat.onCheat != nullptr);
        CHECK(host->anticheat.settings.enabled);

        Scene as("A"), bs("B");
        auto* a = as.CreateGameObject("Net")->AddComponent<NetworkManager>();
        auto* b = bs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        a->interpolationRate = b->interpolationRate = 0.0f;
        GameObject* aDoor = as.CreateGameObject("Door");     // A's copy of the host's door
        aDoor->transform->localPosition = Vec3(3.0f, 0.0f, 0.0f);
        aDoor->AddComponent<NetworkSync>()->netId = "door";
        CHECK(a->JoinViaRelay("127.0.0.1", relay.Port(), "AC-ROOM"));
        CHECK(b->JoinViaRelay("127.0.0.1", relay.Port(), "AC-ROOM"));
        as.Start(); bs.Start();
        auto pump = [&](int n) {
            for (int i = 0; i < n; ++i) { relay.Poll(0.02f); hs.Update(0.02f); as.Update(0.02f); bs.Update(0.02f); Nap(); }
        };
        for (int i = 0; i < 400 && !(a->Joined() && b->Joined()); ++i) pump(1);
        CHECK(a->Joined() && b->Joined());

        GameObject* box = host->SpawnOwned(crate, {1.0f, 0.0f, 0.0f});
        CHECK(box != nullptr);
        const std::string id = box ? box->GetComponent<NetworkSync>()->netId : "";
        pump(30);
        CHECK(std::fabs(PosOf(as, id).x - 1.0f) < 0.01f);

        // B forges transforms for the host's door and crate: refused, reported, kicked.
        b->Send("__xform", "door|50 0 0 0 0 0 1");
        pump(10);
        CHECK(std::fabs(door->transform->localPosition.x - 3.0f) < 0.01f);
        CHECK(std::fabs(aDoor->transform->localPosition.x - 3.0f) < 0.01f);   // not relayed to A
        CHECK(host->anticheat.TotalReports() == 1);
        for (int i = 0; i < 8 && !b->WasKicked(); ++i) {
            b->Send("__xform", id + "|" + std::to_string(100 + i) + " 0 0 0 0 0 1");
            pump(5);
        }
        for (int i = 0; i < 100 && !b->WasKicked(); ++i) pump(1);
        CHECK(std::fabs(PosOf(hs, id).x - 1.0f) < 0.01f);    // the host's crate didn't move
        CHECK(std::fabs(PosOf(as, id).x - 1.0f) < 0.01f);    // nor A's copy
        auto* vm = referee->GetComponent<ScriptComponent>()->VM();
        CHECK(vm && vm->GetGlobal("cheats").AsFloat() >= 1.0f);
        CHECK(vm && vm->GetGlobal("lastKind").AsString() == "forged transform");
        CHECK(vsc->GetVariable("n").AsFloat() >= 1.0f);
        CHECK(b->WasKicked());
        CHECK(a->Joined() && !a->WasKicked());

        // Stop() ends the session's anti-cheat state; the rules stay.
        host->Stop();
        CHECK(host->anticheat.serverOwns == nullptr && host->anticheat.exists == nullptr && host->anticheat.onCheat == nullptr);
        CHECK(host->anticheat.TotalReports() == 0);
        CHECK(host->anticheat.settings.enabled);
        a->Stop(); b->Stop();
        relay.Stop();
    }

    // ---- 2b. The relay forwards only between a client and its host ------------------------
    {
        RelayServer relay;
        CHECK(relay.Start(0));
        RawPeer h(relay.Port()), c1(relay.Port()), c2(relay.Port());
        h.Hello(RelayHost, "ROUTE");
        c1.Hello(RelayClient, "ROUTE");
        c2.Hello(RelayClient, "ROUTE");
        Pump(relay, {&h, &c1, &c2});
        const std::uint32_t hs = relay.SlotOf(h.self), s1 = relay.SlotOf(c1.self), s2 = relay.SlotOf(c2.self);
        CHECK(hs && s1 && s2);
        CHECK(c1.welcomeHost == hs && c2.welcomeHost == hs);
        CHECK(h.version == kRelayProtocolVersion);
        CHECK(h.key != 0);          // the host gets a key ...
        CHECK(c1.key == 0);         // ... clients don't

        c1.Data(s2, "sneaky");      // client -> client
        c1.Data(hs, "to-host");
        h.Data(s2, "to-c2");
        h.Data(hs, "self");         // host -> itself
        Pump(relay, {&h, &c1, &c2});
        CHECK(c2.data.size() == 1 && c2.data[0] == "to-c2");
        CHECK(h.data.size() == 1 && h.data[0] == "to-host");
        CHECK(relay.DroppedRoute() == 2);
        relay.Stop();
    }

    // ---- 2b. Nobody can take a session code over ------------------------------------------
    {
        RelayServer relay;
        CHECK(relay.Start(0));
        RawPeer h(relay.Port()), c(relay.Port()), thief(relay.Port()), moved(relay.Port());
        h.Hello(RelayHost, "MINE");
        c.Hello(RelayClient, "MINE");
        Pump(relay, {&h, &c});
        const std::uint32_t hostSlot = relay.SlotOf(h.self);
        CHECK(hostSlot != 0 && h.key != 0 && c.welcomeHost == hostSlot);

        // A second host without the key, with a wrong key, or as an old peer: refused.
        thief.Hello(RelayHost, "MINE");
        Pump(relay, {&thief});
        CHECK(thief.refused == RelayRefusedHosted && relay.SlotOf(thief.self) == 0);
        thief.refused = 0;
        thief.Hello(RelayHost, "MINE", h.key ^ 1);
        thief.Hello(RelayHost, "MINE", 0, /*no version*/ -1);
        Pump(relay, {&thief});
        CHECK(thief.refused == RelayRefusedHosted && relay.SlotOf(thief.self) == 0);
        CHECK(relay.RefusedHellos() == 3);
        CHECK(c.welcomeHost == hostSlot);    // the client is still wired to its host

        // The host moving to a new address with its key keeps its slot.
        const std::uint32_t welcomesBefore = c.welcomes;
        moved.Hello(RelayHost, "MINE", h.key);
        Pump(relay, {&moved, &c});
        CHECK(relay.SlotOf(moved.self) == hostSlot && relay.SlotOf(h.self) == 0);
        CHECK(moved.welcomeSlot == hostSlot && moved.key == h.key);
        c.Hello(RelayClient, "MINE");
        Pump(relay, {&moved, &c});
        CHECK(c.welcomes > welcomesBefore && c.welcomeHost == hostSlot);
        c.Data(hostSlot, "still-you");
        Pump(relay, {&moved, &c});
        CHECK(moved.data.size() == 1 && moved.data[0] == "still-you");

        // The host leaving: its clients are told (host slot 0), and the code
        // still can't be taken while they're there.
        moved.Bye();
        Pump(relay, {&moved, &c});
        CHECK(c.welcomeHost == 0);
        thief.refused = 0;
        thief.Hello(RelayHost, "MINE");
        Pump(relay, {&thief});
        CHECK(thief.refused == RelayRefusedHosted && relay.SlotOf(thief.self) == 0);
        // The real host can come back, on its old slot.
        RawPeer back(relay.Port());
        back.Hello(RelayHost, "MINE", h.key);
        Pump(relay, {&back, &c});
        CHECK(relay.SlotOf(back.self) == hostSlot);
        relay.Stop();
    }

    // ---- 2b. A client disconnects when its host leaves; a host is told its code is taken -
    {
        RelayServer relay;
        CHECK(relay.Start(0));
        Scene hs("Host"), cs("Client"), ts("Thief");
        auto* host = hs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        auto* cli = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        auto* thief = ts.CreateGameObject("Net")->AddComponent<NetworkManager>();
        CHECK(host->HostViaRelay("127.0.0.1", relay.Port(), "GAME"));
        CHECK(cli->JoinViaRelay("127.0.0.1", relay.Port(), "GAME"));
        hs.Start(); cs.Start(); ts.Start();
        auto pump = [&](int n) {
            for (int i = 0; i < n; ++i) { relay.Poll(0.02f); hs.Update(0.02f); cs.Update(0.02f); ts.Update(0.02f); Nap(); }
        };
        for (int i = 0; i < 400 && !cli->Joined(); ++i) pump(1);
        CHECK(cli->Joined());
        pump(60);                         // a keepalive or two carry the host's key back
        CHECK(host->Relayed() && host->RelayError().empty());

        CHECK(thief->HostViaRelay("127.0.0.1", relay.Port(), "GAME"));
        for (int i = 0; i < 100 && thief->Relayed(); ++i) pump(1);
        CHECK(!thief->Relayed());
        CHECK(thief->RelayError().find("already hosted") != std::string::npos);
        CHECK(cli->Joined() && host->Relayed());

        host->Stop();
        for (int i = 0; i < 100 && cli->Relayed(); ++i) pump(1);
        CHECK(!cli->Relayed() && !cli->Joined());
        CHECK(cli->RelayError() == "the host left the session");
        relay.Stop();
    }

    // ---- 2b. Version-1 sessions keep their rules (the launcher's voice calls) -------------
    {
        // Every voice member announces the host role with a version-1 hello: the
        // first is the hub, the others send to it, it fans out, and when it
        // leaves the next hello takes over.
        RelayServer relay;
        CHECK(relay.Start(0));
        RawPeer hub(relay.Port()), m1(relay.Port()), m2(relay.Port());
        hub.Hello(RelayHost, "CALL", 0, -1);
        Pump(relay, {&hub});
        m1.Hello(RelayHost, "CALL", 0, -1);
        m2.Hello(RelayHost, "CALL", 0, -1);
        Pump(relay, {&hub, &m1, &m2});
        const std::uint32_t hubSlot = relay.SlotOf(hub.self), s1 = relay.SlotOf(m1.self), s2 = relay.SlotOf(m2.self);
        CHECK(hubSlot && s1 && s2);
        CHECK(m1.welcomeHost == hubSlot && m2.welcomeHost == hubSlot);
        CHECK(m1.key == 0 && m2.key == 0);              // no key leaks to the members
        m1.Data(hubSlot, "voice");
        hub.Data(s2, "voice-fanout");
        m1.Data(s2, "direct");                          // member to member: not forwarded
        Pump(relay, {&hub, &m1, &m2});
        CHECK(hub.data.size() == 1 && hub.data[0] == "voice");
        CHECK(m2.data.size() == 1 && m2.data[0] == "voice-fanout");
        hub.Bye();
        Pump(relay, {&hub, &m1, &m2});
        CHECK(m1.welcomeHost == 0);
        m1.Hello(RelayHost, "CALL", 0, -1);             // the next hello becomes the hub
        Pump(relay, {&m1, &m2});
        m2.Hello(RelayHost, "CALL", 0, -1);
        Pump(relay, {&m1, &m2});
        CHECK(m1.welcomeHost == s1 && m2.welcomeHost == s1);
        // A version-2 game host doesn't join someone else's version-1 session.
        RawPeer game(relay.Port());
        game.Hello(RelayHost, "CALL");
        Pump(relay, {&game});
        CHECK(game.refused == RelayRefusedHosted && relay.SlotOf(game.self) == 0);
        relay.Stop();
    }

    // ---- 2b. Per-peer rate caps ------------------------------------------------------------
    {
        RelayServer relay;
        CHECK(relay.Start(0));
        const Endpoint h{0x7F000001u, 61001}, c{0x7F000001u, 61002};   // nothing listens there
        auto hh = HelloBytes(RelayHost, "FLOOD"), ch = HelloBytes(RelayClient, "FLOOD");
        relay.HandleDatagram(h, hh.data(), hh.size());
        relay.HandleDatagram(c, ch.data(), ch.size());
        auto small = DataBytes(relay.SlotOf(h), 16);
        for (int i = 0; i < 5000; ++i) relay.HandleDatagram(c, small.data(), small.size());
        const auto burst = static_cast<std::uint64_t>(2.0f * RelayServer::kPacketsPerSecond);
        CHECK(relay.DroppedRate() == 5000 - burst);
        // A second later the client may send again, about a second's worth.
        relay.Poll(1.0f);
        const std::uint64_t before = relay.DroppedRate();
        for (int i = 0; i < 2000; ++i) relay.HandleDatagram(c, small.data(), small.size());
        const std::uint64_t passed = 2000 - (relay.DroppedRate() - before);
        CHECK(passed >= 990 && passed <= 1010);
        // Bytes are capped too: 64 KB datagrams stop after about 2 MB.
        relay.Poll(5.0f);
        auto big = DataBytes(relay.SlotOf(h), 60000);
        const std::uint64_t b0 = relay.DroppedRate();
        for (int i = 0; i < 100; ++i) relay.HandleDatagram(c, big.data(), big.size());
        const std::uint64_t bigPassed = 100 - (relay.DroppedRate() - b0);
        CHECK(bigPassed >= 30 && bigPassed <= 40);
        relay.Stop();
    }

    // ---- 2b. Protocol version --------------------------------------------------------------
    {
        RelayServer relay;
        CHECK(relay.Start(0));
        RawPeer future(relay.Port()), ancient(relay.Port()), old(relay.Port());
        ancient.Hello(RelayClient, "V", 0, /*version*/ 0);
        old.Hello(RelayHost, "V", 0, /*no version: a version-1 peer*/ -1);
        Pump(relay, {&ancient, &old});
        CHECK(ancient.refused == RelayRefusedVersion && ancient.version == kRelayProtocolVersion);
        CHECK(relay.SlotOf(ancient.self) == 0);
        CHECK(relay.SlotOf(old.self) != 0 && old.welcomeSlot != 0);   // version 1 still works
        relay.Stop();
    }

    std::remove(crate.c_str());
    Log::sink = sink;
    TEST_MAIN_RESULT();
}
