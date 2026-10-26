#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Net/Packet.hpp"
#include "okay/Net/UdpSocket.hpp"
#include "okay/Net/DedicatedServer.hpp"

#include <chrono>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace okay;

// Anti-cheat plan 5a: an opt-in fixed network tick. The host counts ticks at a
// fixed rate whatever its frame rate, every snapshot carries the tick, and
// clients send their state once a tick instead of once a frame. Off, nothing
// changes.
namespace {
void Nap() { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
enum : std::uint8_t { kJoin = 1, kWelcome = 2, kSnapshot = 4 };

// Run host and client for `seconds` of game time, the host at hostDt and the
// client at clientDt per frame (each frame sleeps a little for the sockets).
void Run(Scene& host, Scene& client, float seconds, float hostDt, float clientDt) {
    float th = 0.0f, tc = 0.0f;
    while (th < seconds || tc < seconds) {
        if (th < seconds && th <= tc) { host.Update(hostDt); th += hostDt; }
        else { client.Update(clientDt); tc += clientDt; }
        Nap();
    }
}
} // namespace

int main() {
    RUN_SUITE("net_tick");
    auto sink = Log::sink;
    Log::sink = [](Log::Level, const std::string&) {};

    // ---- Off (the default): clients send every frame, no tick --------------------------
    {
        Scene hs("Host"), cs("Client");
        auto* host = hs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        CHECK(host->netTickRate == 0.0f);
        CHECK(host->StartServer(0));
        auto* cli = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        CHECK(cli->StartClient("127.0.0.1", host->ServerPort()));
        hs.Start(); cs.Start();
        for (int i = 0; i < 300 && !cli->Joined(); ++i) { hs.Update(0.016f); cs.Update(0.016f); Nap(); }
        CHECK(cli->Joined());
        const std::uint32_t before = cli->StatesSent();
        Run(hs, cs, 1.0f, 1.0f / 60.0f, 1.0f / 240.0f);
        const std::uint32_t sent = cli->StatesSent() - before;
        CHECK(sent >= 230 && sent <= 250);               // one per client frame, as before
        CHECK(cli->NetTickRate() == 0.0f && cli->NetTick() == 0 && host->NetTick() == 0);
        cli->Stop(); host->Stop();
    }

    // ---- On: the host ticks at 30 Hz at any frame rate; clients send once a tick -------
    {
        Scene hs("Host"), cs("Client");
        auto* host = hs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        host->netTickRate = 30.0f;
        std::vector<std::uint32_t> hostTicks;
        float tickDt = 0.0f;
        host->OnNetTick([&](std::uint32_t t, float dt) { hostTicks.push_back(t); tickDt = dt; });
        CHECK(host->StartServer(0));
        auto* cli = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        int clientTicks = 0;
        cli->OnNetTick([&](std::uint32_t, float) { ++clientTicks; });
        CHECK(cli->StartClient("127.0.0.1", host->ServerPort()));
        hs.Start(); cs.Start();
        for (int i = 0; i < 300 && !cli->Joined(); ++i) { hs.Update(0.016f); cs.Update(0.016f); Nap(); }
        CHECK(cli->Joined());
        CHECK(cli->NetTickRate() == 30.0f && host->NetTickRate() == 30.0f);

        // The host at 144 fps, then at 20 fps: 30 ticks a second either way.
        hostTicks.clear();
        const std::uint32_t before = cli->StatesSent();
        const int cBefore = clientTicks;
        Run(hs, cs, 2.0f, 1.0f / 144.0f, 1.0f / 240.0f);
        CHECK(hostTicks.size() >= 58 && hostTicks.size() <= 61);
        CHECK(std::fabs(tickDt - 1.0f / 30.0f) < 1e-6f);
        for (std::size_t i = 1; i < hostTicks.size(); ++i) CHECK(hostTicks[i] == hostTicks[i - 1] + 1);
        const std::uint32_t sent = cli->StatesSent() - before;
        CHECK(sent >= 55 && sent <= 62);                 // ~30/s, not the client's 240 frames/s
        CHECK(clientTicks - cBefore >= 55 && clientTicks - cBefore <= 62);
        hostTicks.clear();
        Run(hs, cs, 2.0f, 1.0f / 20.0f, 1.0f / 240.0f);
        CHECK(hostTicks.size() >= 58 && hostTicks.size() <= 61);
        // The client's tick follows the host's.
        const long gap = (long)host->NetTick() - (long)cli->NetTick();
        CHECK(gap >= -3 && gap <= 6);
        CHECK(cli->NetTick() > 100);

        // An older client (a plain Join) still plays: it reads the Welcome and
        // snapshots and ignores what comes after (the tick).
        net::UdpSocket raw; raw.Open(); raw.Bind(0);
        const net::Endpoint to{0x7F000001u, host->ServerPort()};
        net::Packet j(kJoin);
        for (int i = 0; i < 5; ++i) j.Write(std::string(i == 0 ? "old" : ""));
        bool welcomed = false;
        float announced = 0.0f;
        std::vector<std::uint32_t> snapTicks;
        for (int i = 0; i < 300 && snapTicks.size() < 20; ++i) {
            if (!welcomed && i % 10 == 0) raw.SendTo(to, j.Data(), j.Size());
            hs.Update(1.0f / 60.0f); cs.Update(1.0f / 60.0f); Nap();
            std::uint8_t buf[2048]; net::Endpoint from; int n;
            while ((n = raw.RecvFrom(buf, sizeof buf, from)) > 0) {
                net::Packet p(buf, (std::size_t)n);
                const std::uint8_t t = p.ReadU8();
                if (t == kWelcome) {
                    p.ReadU32(); p.ReadString(); p.ReadString(); p.ReadString();
                    const std::uint8_t flags = p.ReadU8();
                    if (flags & 2) announced = p.ReadF32();
                    welcomed = p.Ok();
                } else if (t == kSnapshot) {
                    const std::uint32_t count = p.ReadU32();
                    for (std::uint32_t k = 0; k < count; ++k) { p.ReadU32(); p.ReadF32(); p.ReadF32(); p.ReadF32(); p.ReadU8(); }
                    const std::uint32_t tick = p.ReadU32();
                    if (p.Ok()) snapTicks.push_back(tick);
                }
            }
        }
        CHECK(welcomed && announced == 30.0f);
        CHECK(snapTicks.size() >= 20);                   // a snapshot every tick, even with nothing new
        bool rising = true;
        for (std::size_t i = 1; i < snapTicks.size(); ++i) rising = rising && snapTicks[i] > snapTicks[i - 1];
        CHECK(rising);
        cli->Stop(); host->Stop();
    }

    // ---- Settings: scenes (old ones load with it off), the dedicated server ------------
    {
        Scene a("A");
        auto* nm = a.CreateGameObject("Net")->AddComponent<NetworkManager>();
        nm->netTickRate = 30.0f;
        auto* off = a.CreateGameObject("Off")->AddComponent<NetworkManager>();
        (void)off;
        const std::string path = "/tmp/okay_net_tick.okayscene";
        CHECK(SceneSerializer::SaveToFile(a, path));
        Scene b("B");
        CHECK(SceneSerializer::LoadFromFile(b, path));
        auto* back = b.Find("Net") ? b.Find("Net")->GetComponent<NetworkManager>() : nullptr;
        auto* back2 = b.Find("Off") ? b.Find("Off")->GetComponent<NetworkManager>() : nullptr;
        CHECK(back && back->netTickRate == 30.0f);
        CHECK(back2 && back2->netTickRate == 0.0f);
        std::ifstream in(path); std::stringstream ss; ss << in.rdbuf();
        CHECK(ss.str().find("\"\" \"\"\n") != std::string::npos);   // the "Off" line ends as it always did
        std::remove(path.c_str());

        auto parse = [](std::vector<std::string> args, DedicatedServerOptions& o, std::string& e) {
            std::vector<char*> argv;
            for (auto& x : args) argv.push_back(x.data());
            return ParseDedicatedServerArgs((int)argv.size(), argv.data(), o, &e);
        };
        DedicatedServerOptions o; std::string e;
        CHECK(parse({"okayspace-server", "--server", "--fixed-tick", "--tick", "30"}, o, e) && e.empty() && o.fixedTick && o.tickRate == 30.0f);
        DedicatedServerOptions o2;
        CHECK(parse({"okayspace-server", "--server"}, o2, e) && !o2.fixedTick);
    }

    Log::sink = sink;
    TEST_MAIN_RESULT();
}
