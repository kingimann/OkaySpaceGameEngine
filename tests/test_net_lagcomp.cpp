#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Net/UdpSocket.hpp"

#include <chrono>
#include <cmath>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace okay;

// Net plan 5e: lag compensation. The host keeps every synced object's pose
// each tick (~1 s); a hit claim says which moment the shooter saw, and the
// host checks it against where things were then (clamped to 200 ms plus the
// shooter's round trip). Through a proxy delaying every packet 50 ms each way.
namespace {
void Nap() { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }

struct DelayProxy {
    struct Held { double at; std::vector<std::uint8_t> bytes; net::Endpoint to; net::UdpSocket* via; };
    net::UdpSocket front;
    net::Endpoint host;
    double oneWay, now = 0.0;
    std::map<std::uint64_t, std::unique_ptr<net::UdpSocket>> up;
    std::map<std::uint64_t, net::Endpoint> back;
    std::deque<Held> q;
    DelayProxy(std::uint16_t hostPort, double delay) : oneWay(delay) {
        host = net::Endpoint{0x7F000001u, hostPort};
        front.Open(); front.Bind(0);
    }
    std::uint16_t Port() const { return front.LocalPort(); }
    void Pump(double dt) {
        now += dt;
        std::uint8_t buf[4096]; net::Endpoint from; int n;
        while ((n = front.RecvFrom(buf, sizeof buf, from)) > 0) {
            const std::uint64_t key = (std::uint64_t(from.address) << 16) | from.port;
            auto& u = up[key];
            if (!u) { u = std::make_unique<net::UdpSocket>(); u->Open(); u->Bind(0); back[key] = from; }
            q.push_back({now + oneWay, std::vector<std::uint8_t>(buf, buf + n), host, u.get()});
        }
        for (auto& [key, u] : up)
            while ((n = u->RecvFrom(buf, sizeof buf, from)) > 0)
                q.push_back({now + oneWay, std::vector<std::uint8_t>(buf, buf + n), back[key], &front});
        while (!q.empty() && q.front().at <= now + 1e-9) {
            q.front().via->SendTo(q.front().to, q.front().bytes.data(), q.front().bytes.size());
            q.pop_front();
        }
    }
};

Vec3 Dir(const Vec3& from, const Vec3& to) { const Vec3 d = to - from; return d * (1.0f / d.Magnitude()); }
} // namespace

int main() {
    RUN_SUITE("net_lagcomp");
    auto sink = Log::sink;
    Log::sink = [](Log::Level, const std::string&) {};
    const float dt = 1.0f / 60.0f, speed = 10.0f;

    // The host and a target running across the line of fire at 10 u/s.
    Scene hs("Host");
    auto* host = hs.CreateGameObject("Net")->AddComponent<NetworkManager>();
    host->netTickRate = 30.0f;
    host->maxMessagesPerSecond = 0;
    host->anticheat.settings.hitTolerance = 0.5f;
    host->anticheat.settings.maxSyncSpeed = speed;
    host->anticheat.settings.kickAfterViolations = 0;
    host->maxMoveSpeed = 1000.0f;   // a move check, so client hit claims are judged at all
    GameObject* dummy = hs.CreateGameObject("Dummy");
    dummy->transform->localPosition = Vec3(10.0f, 0.0f, -10.0f);
    dummy->AddComponent<NetworkSync>()->netId = "dummy";
    dummy->AddComponent<BoxCollider3D>()->size = Vec3(1.0f, 2.0f, 1.0f);
    auto* hp = dummy->AddComponent<HealthStat>();
    CHECK(host->StartServer(0));
    std::vector<CheatReport> reports;
    host->SetCheatHandler([&](const CheatReport& r) { reports.push_back(r); });
    DelayProxy proxy(host->ServerPort(), 0.05);

    // The shooter: its NetworkManager object is its avatar, at the origin.
    Scene cs("Shooter");
    auto* cli = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
    GameObject* copy = cs.CreateGameObject("Dummy");
    copy->transform->localPosition = Vec3(10.0f, 0.0f, -10.0f);
    copy->AddComponent<NetworkSync>()->netId = "dummy";
    copy->AddComponent<HealthStat>();
    CHECK(cli->StartClient("127.0.0.1", proxy.Port()));
    hs.Start(); cs.Start();
    std::deque<Vec3> hostPath;   // where the target really was, each frame (newest last)
    auto frame = [&](bool move) {
        if (move) dummy->transform->localPosition.z += speed * dt;
        hs.Update(dt); proxy.Pump(dt); cs.Update(dt); Nap();
        hostPath.push_back(dummy->transform->localPosition);
        if (hostPath.size() > 120) hostPath.pop_front();
    };
    for (int i = 0; i < 600 && !cli->Joined(); ++i) frame(false);
    CHECK(cli->Joined());
    for (int i = 0; i < 60; ++i) frame(false);
    const Vec3 at(0.0f, 0.0f, 0.0f);
    auto settle = [&](int n) { for (int i = 0; i < n; ++i) frame(true); };
    settle(60);                                            // running, and seen running

    // ---- A shot at where the shooter sees it counts: rewound to that moment -------
    const Vec3 seen = copy->transform->localPosition;
    const Vec3 real = dummy->transform->localPosition;
    CHECK(real.z - seen.z > 1.0f);                         // it really is well past that (>0.1 s at 10 u/s)
    cli->ClaimHit("dummy", "rifle", at, Dir(at, seen));
    settle(20);
    CHECK(host->HitsApplied() == 1 && std::fabs(hp->health - 90.0f) < 0.01f);
    // Rewound about the interpolation delay plus the trip there.
    CHECK(host->LastRewind() > 0.1f && host->LastRewind() < 0.35f);
    std::printf("  rewound %.0f ms (the shooter shows others %.0f ms back, rtt %.0f ms); target was %.2f u ahead\n",
                host->LastRewind() * 1000.0f, cli->InterpolationDelay() * 1000.0f, cli->RttMs(), real.z - seen.z);
    // The target is back where it is now (the rewind is undone).
    CHECK(std::fabs(dummy->transform->localPosition.z - hostPath.back().z) < 1e-4f);

    // ---- ... and a shot at where it really is now (not what was seen) misses ------
    cli->ClaimHit("dummy", "rifle", at, Dir(at, dummy->transform->localPosition));
    settle(20);
    CHECK(host->HitsApplied() == 1);
    int missed = 0;
    for (const auto& r : reports) missed += r.kind == CheatKind::HitRejected && r.detail.find("missed") != std::string::npos;
    CHECK(missed == 1);

    // ---- A claim to have seen it a second ago is clamped to 200 ms + the RTT -----
    {
        const Vec3 old = hostPath[hostPath.size() - 60];   // where it was 1 s ago
        const Vec3 d = Dir(at, old);
        char buf[256];
        std::snprintf(buf, sizeof buf, "dummy|rifle|0:%.3f|%.9g %.9g %.9g|%.9g %.9g %.9g",
                      (double)host->NetTick() - 30.0, at.x, at.y, at.z, d.x, d.y, d.z);
        cli->Send("__hit", buf);
        settle(20);
        CHECK(host->HitsApplied() == 1);                   // not that far back
        CHECK(host->LastRewind() < 0.2f + cli->RttMs() / 1000.0f + 0.1f);
        missed = 0;
        for (const auto& r : reports) missed += r.kind == CheatKind::HitRejected && r.detail.find("missed") != std::string::npos;
        CHECK(missed == 2);
    }

    // ---- RewindRaycast: a ray against where things were ----------------------------
    {
        const Vec3 now = dummy->transform->localPosition;
        const Vec3 then = hostPath[hostPath.size() - 12]; // 12 frames = 6 ticks ago (2 u back)
        const RaycastHit3D past = host->RewindRaycast(at, Dir(at, then), 50.0f, (double)host->NetTick() - 6.0);
        CHECK(past.hit && past.gameObject == dummy);
        const RaycastHit3D present = host->RewindRaycast(at, Dir(at, then), 50.0f, (double)host->NetTick());
        CHECK(!present.hit);                               // it isn't there now
        CHECK(dummy->transform->localPosition.z == now.z); // and nothing was left moved
    }

    cli->Stop(); host->Stop();
    Log::sink = sink;
    TEST_MAIN_RESULT();
}
