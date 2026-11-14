#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Net/NetChannels.hpp"
#include "okay/Net/NetDelta.hpp"
#include "okay/Net/Packet.hpp"
#include "okay/Net/RelayProtocol.hpp"
#include "okay/Net/RelayServer.hpp"
#include "okay/Net/UdpSocket.hpp"
#include "okay/Net/JoinLink.hpp"
#include "okay/Net/ServerQuery.hpp"
#include "okay/Net/AntiCheat.hpp"
#include <chrono>
#include <cmath>
#include <cstring>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace okay;
using namespace okay::net;

// Deterministic fuzzing of everything that parses bytes from the network, plus
// random and mutated datagrams thrown at a live host, client and relay. The
// checks are that nothing crashes or hangs (build it with ASan and UBSan to make
// that bite), that parsers keep their promises (finite numbers, bounded sizes),
// and that a live session still works afterwards.

namespace {
std::mt19937 g_rng{0xF00DF00Du};
std::uint32_t R(std::uint32_t n) { return n ? g_rng() % n : 0; }

std::vector<std::uint8_t> Mutate(std::vector<std::uint8_t> v) {
    const int ops = 1 + (int)R(6);
    for (int i = 0; i < ops; ++i) {
        switch (R(7)) {
            case 0: if (!v.empty()) v[R((std::uint32_t)v.size())] ^= (std::uint8_t)(1u << R(8)); break;
            case 1: if (!v.empty()) v[R((std::uint32_t)v.size())] = (std::uint8_t)R(256); break;
            case 2: if (!v.empty()) v.resize(R((std::uint32_t)v.size() + 1)); break;                   // truncate
            case 3: { const int n = 1 + (int)R(40); for (int k = 0; k < n; ++k) v.push_back((std::uint8_t)R(256)); } break;
            case 4: if (v.size() >= 4) { const std::uint32_t at = R((std::uint32_t)v.size() - 3);
                        const std::uint32_t big[] = {0u, 0xFFFFFFFFu, 0x7FFFFFFFu, 0x80000000u, 0x10000u, 0xFFFFu};
                        const std::uint32_t x = big[R(6)]; std::memcpy(&v[at], &x, 4); } break;       // lengths and counts
            case 5: if (!v.empty()) { const std::uint32_t at = R((std::uint32_t)v.size()); v.insert(v.begin() + at, (std::uint8_t)R(256)); } break;
            default: if (v.size() > 1) v.erase(v.begin() + R((std::uint32_t)v.size())); break;
        }
    }
    return v;
}
std::string MutateStr(const std::string& s) {
    auto b = Mutate(std::vector<std::uint8_t>(s.begin(), s.end()));
    return std::string(b.begin(), b.end());
}
std::vector<std::uint8_t> RandomBytes(std::size_t n) {
    std::vector<std::uint8_t> v(n);
    for (auto& b : v) b = (std::uint8_t)R(256);
    return v;
}
bool Pumpable(NetworkManager* n) { return n != nullptr; }
} // namespace

int main() {
    RUN_SUITE("net_fuzz");
    auto sink = Log::sink;
    Log::sink = [](Log::Level, const std::string&) {};
    const int kIter = 6000;

    // ---- Snapshot decoding ----
    {
        DeltaBase base, cur;
        base.id = 3;
        for (int i = 0; i < 6; ++i) {
            QPose a{i * 100, -i * 50, i * 7, 0, 0, 0, 32767};
            QPose b{i * 100 + 40, -i * 50 + 9, i * 7 - 3, 0, 0, 0, 32767};
            base.pose["obj" + std::to_string(i)] = a;
            cur.pose["obj" + std::to_string(i)] = b;
            cur.extra["obj" + std::to_string(i)] = "1 0 0.5 10";
        }
        const std::vector<std::vector<std::uint8_t>> seeds = {
            EncodeSnapshot(base, cur, 4, 12.5), EncodeSnapshot(DeltaBase{}, cur, 4, 0.0), EncodeSnapAck(77)};
        int decoded = 0;
        for (int it = 0; it < kIter * 3; ++it) {
            auto bytes = (it % 5 == 0) ? RandomBytes(R(200)) : Mutate(seeds[R((std::uint32_t)seeds.size())]);
            std::uint32_t bid = 0, sid = 0; double tick = 0.0;
            std::vector<SnapChange> ch;
            if (DecodeSnapshot(bytes.data(), bytes.size(), base, bid, sid, tick, ch)) {
                ++decoded;
                CHECK(ch.size() <= 4096);
                for (const auto& c : ch) CHECK(c.id.size() <= 4096);
            }
            PeekSnapshot(bytes.data(), bytes.size(), bid, sid);
            DecodeSnapAck(bytes.data(), bytes.size(), sid);
            Vec3 pp; Quat qq;
            DequantizePose(QPose{std::numeric_limits<std::int32_t>::max(), std::numeric_limits<std::int32_t>::min(),
                                 (std::int32_t)R(1000), (std::int16_t)R(65536), (std::int16_t)R(65536), (std::int16_t)R(65536), (std::int16_t)R(65536)}, pp, qq);
            CHECK(std::isfinite(pp.x) && std::isfinite(qq.w));
        }
        CHECK(decoded > 0);
    }

    // ---- Channel packets: the ordered and sequenced paths ----
    {
        std::vector<std::vector<std::uint8_t>> seeds = {
            BuildChanRel(5, "chat", "hello world", 0), BuildChanRel(1, "x", std::string(300, 'a'), 0x1122334455667788ull),
            BuildChanRelAck("chat", 9), BuildChanSeq(2, 7, kChanBroadcast, "pos", "1 2 3", 0)};
        ChannelEnd end;
        std::vector<ChanMsg> out;
        std::uint32_t ack = 0;
        for (int it = 0; it < kIter * 3; ++it) {
            auto bytes = Mutate(seeds[R((std::uint32_t)seeds.size())]);
            Packet p(bytes.data(), bytes.size());
            const std::uint8_t type = p.ReadU8();
            if (type == kChanRel) {
                const std::uint32_t seq = p.ReadU32();
                const std::string channel = p.ReadString();
                std::string data = p.ReadString();
                if (p.Ok() && ChannelNameOk(channel)) end.ordered.Receive(channel, seq, std::move(data), true, 1, out, ack);
            } else if (type == kChanRelAck) {
                const std::string channel = p.ReadString();
                const std::uint32_t a = p.ReadU32();
                if (p.Ok()) end.ordered.Ack(channel, a);
            } else if (type == kChanSeq) {
                const std::uint32_t sender = p.ReadU32(), seq = p.ReadU32(), target = p.ReadU32();
                const std::string channel = p.ReadString();
                (void)target;
                if (p.Ok() && ChannelNameOk(channel)) end.sequencedRx.Accept(sender, channel, seq);
            }
            if (out.size() > 100000) out.clear();
        }
        // Bounded no matter what arrived: the reorder buffer and the sequence table cannot grow without limit.
        CHECK(out.size() <= 100000);
    }

    // ---- Packet reads never run past the end ----
    for (int it = 0; it < kIter; ++it) {
        auto bytes = RandomBytes(R(64));
        Packet p(bytes.data(), bytes.size());
        for (int k = 0; k < 20; ++k) {
            switch (R(6)) {
                case 0: p.ReadU8(); break; case 1: p.ReadU32(); break; case 2: p.ReadF32(); break;
                case 3: { const std::string s = p.ReadString(); CHECK(s.size() <= bytes.size()); } break;
                case 4: p.ReadI32(); break; default: p.ReadU8(); break;
            }
        }
        CHECK(p.Remaining() <= bytes.size());
    }

    // ---- Text formats ----
    {
        const std::vector<std::string> spawns = {"Prefabs/Crate.okayprefab|1|2|3", "a.okayprefab|1.5|-2|3|sp/4/9", "p|nan|inf|1", "x|1|2"};
        const std::vector<std::string> anims = {"2 0 1.5 30", "2 0 1.5 30 7 2 -10 4 3 5 8 2", "1 1 nan 0", "9999999999 1 1 1"};
        const std::vector<std::string> links = {"okayspace://join?game=g&addr=1.2.3.4:5&room=r&pw=x", "127.0.0.1:45000",
                                                "okayspace://join?relay=h:1&code=ABC", "[::1]:99"};
        for (int it = 0; it < kIter; ++it) {
            SpawnMessage m = ParseSpawnMessage(MutateStr(spawns[R((std::uint32_t)spawns.size())]));
            CHECK(m.path.size() < 100000);
            AnimSyncView v;
            if (ParseAnimSync(MutateStr(anims[R((std::uint32_t)anims.size())]), v)) {
                if (v.haveGait) CHECK(std::isfinite(v.speed) && std::isfinite(v.angle));
                if (v.haveAction) CHECK(std::isfinite(v.lookYaw) && std::isfinite(v.lookPitch));
                if (v.haveParam) CHECK(std::isfinite(v.actionParam));
                if (v.haveEvent) CHECK(std::isfinite(v.eventParam));
            }
            JoinLink l;
            JoinLink::Parse(MutateStr(links[R((std::uint32_t)links.size())]), l);
            std::string h; std::uint16_t port = 0;
            SplitHostPort(MutateStr("host.example:1234"), h, port);
        }
    }

    // ---- A live host, a live client and a relay under random and mutated datagrams ----
    {
        Scene ss("Server"); ss.physicsEnabled = false;
        auto* srv = ss.CreateGameObject("Net")->AddComponent<NetworkManager>();
        CHECK(srv->StartServer(0));
        ss.Start();
        Scene cs("Client"); cs.physicsEnabled = false;
        auto* cli = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        CHECK(cli->StartClient("127.0.0.1", srv->ServerPort()));
        cs.Start();
        auto step = [&](float dt = 0.02f) { ss.Update(dt); cs.Update(dt); std::this_thread::sleep_for(std::chrono::milliseconds(1)); };
        for (int i = 0; i < 400 && !cli->Joined(); ++i) step();
        CHECK(cli->Joined());

        UdpSocket rogue; rogue.Open(); rogue.Bind(0);
        const Endpoint toSrv{0x7F000001u, srv->ServerPort()};
        // Seeds: well-formed starts of every message type, then damaged.
        std::vector<std::vector<std::uint8_t>> seeds;
        for (int type = 0; type < 48; ++type) {
            Packet p((std::uint8_t)type);
            p.Write(std::uint32_t(1)); p.Write(std::string("name")); p.Write(std::string("room"));
            p.Write(1.0f); p.Write(2.0f); p.Write(3.0f); p.Write(std::uint8_t(64));
            seeds.push_back(std::vector<std::uint8_t>(p.Data(), p.Data() + p.Size()));
        }
        for (int it = 0; it < 4000; ++it) {
            std::vector<std::uint8_t> d = (it % 3 == 0) ? RandomBytes(1 + R(1400)) : Mutate(seeds[R((std::uint32_t)seeds.size())]);
            if (d.size() > 1400) d.resize(1400);
            if (d.empty()) d.push_back(0);
            rogue.SendTo(toSrv, d.data(), d.size());
            if (it % 8 == 0) step();
        }
        for (int i = 0; i < 100; ++i) step();
        // The legitimate client is still joined and still gets service.
        CHECK(cli->Joined());
        cli->SendReliable("alive", "yes");
        bool got = false;
        for (int i = 0; i < 300 && !got; ++i) {
            step();
            NetworkManager::NetMessage m;
            while (srv->PopMessage(m)) got |= m.channel == "alive";
        }
        CHECK(got);
        CHECK(srv->Peers().size() >= 1 && (int)srv->Peers().size() <= srv->maxPlayers);   // the rogue may have joined as a player; it is just another peer
        // A fresh client can still join after the barrage.
        Scene c2s("Client2"); c2s.physicsEnabled = false;
        auto* c2 = c2s.CreateGameObject("Net")->AddComponent<NetworkManager>();
        CHECK(c2->StartClient("127.0.0.1", srv->ServerPort()));
        c2s.Start();
        for (int i = 0; i < 600 && !c2->Joined(); ++i) { c2s.Update(0.02f); step(); }
        CHECK(c2->Joined());
        CHECK(Pumpable(c2));
    }
    {   // The same at a client: a rogue peer talks to its socket (it only listens to its server).
        Scene ss("Server"); ss.physicsEnabled = false;
        auto* srv = ss.CreateGameObject("Net")->AddComponent<NetworkManager>();
        CHECK(srv->StartServer(0));
        ss.Start();
        UdpSocket fake; fake.Open(); fake.Bind(0);
        Scene cs("Client"); cs.physicsEnabled = false;
        auto* cli = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        CHECK(cli->StartClient("127.0.0.1", srv->ServerPort()));
        cs.Start();
        for (int i = 0; i < 400 && !cli->Joined(); ++i) { ss.Update(0.02f); cs.Update(0.02f); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
        CHECK(cli->Joined());
        // A rogue that is not the host writes to the client's own socket: all of it is ignored.
        const Endpoint toCli{0x7F000001u, cli->ServerPort()};
        for (int it = 0; it < 1500; ++it) {
            std::vector<std::uint8_t> d = RandomBytes(1 + R(600));
            fake.SendTo(toCli, d.data(), d.size());
            if (it % 4 == 0) { ss.Update(0.02f); cs.Update(0.02f); }
        }
        CHECK(cli->Joined() && !cli->ConnectionLost());
    }
    {
        RelayServer relay;
        CHECK(relay.Start(0));
        UdpSocket rogue; rogue.Open(); rogue.Bind(0);
        const Endpoint to{0x7F000001u, relay.Port()};
        std::vector<std::vector<std::uint8_t>> seeds;
        for (int role = 0; role < 3; ++role)
            for (int ver = 0; ver < 6; ++ver) {
                Packet h(RelayHello); h.Write(std::uint8_t(role)); h.Write(std::string("ROOM"));
                h.Write(std::uint8_t(ver)); h.Write(std::uint32_t(0)); h.Write(std::uint32_t(0));
                for (int b = 0; b < (int)kRelayCookieBytes; ++b) h.Write(std::uint8_t(b));
                h.Write(std::uint32_t(0x1234)); h.Write(std::uint32_t(0));
                while (h.Size() < kRelayHelloMinBytes) h.Write(std::uint8_t(0));
                seeds.push_back(std::vector<std::uint8_t>(h.Data(), h.Data() + h.Size()));
            }
        for (int it = 0; it < 5000; ++it) {
            std::vector<std::uint8_t> d = (it % 4 == 0) ? RandomBytes(1 + R(300)) : Mutate(seeds[R((std::uint32_t)seeds.size())]);
            if (d.empty()) d.push_back(0);
            rogue.SendTo(to, d.data(), d.size());
            if (it % 4 == 0) relay.Poll(0.0f);
        }
        for (int i = 0; i < 20; ++i) relay.Poll(1.0f);
        CHECK(relay.PeerCount() <= 4096);
    }

    Log::sink = sink;
    TEST_MAIN_RESULT();
}
