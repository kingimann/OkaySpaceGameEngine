#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Net/UdpSocket.hpp"

#include <chrono>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace okay;

// Net plan 5d: with a fixed net tick, other players' objects are shown from an
// interpolation buffer, 3 ticks (100 ms at 30 Hz) in the past, between the
// states either side of that moment. Through a proxy that delays each packet
// by a random 30-70 ms (so they arrive unevenly and out of order), an object
// moving at a steady speed still moves at that speed on everyone else's
// screen. Without a fixed tick the old easing (interpolationRate) is kept.
namespace {
void Nap() { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }

// A UDP proxy that holds every datagram for a random delay (game time).
struct JitterProxy {
    struct Held { double at; std::vector<std::uint8_t> bytes; net::Endpoint to; net::UdpSocket* via; };
    net::UdpSocket front;
    net::Endpoint host;
    double minDelay, maxDelay, now = 0.0;
    std::uint32_t rng = 12345;
    std::map<std::uint64_t, std::unique_ptr<net::UdpSocket>> up;
    std::map<std::uint64_t, net::Endpoint> back;
    std::vector<Held> q;
    JitterProxy(std::uint16_t hostPort, double lo, double hi) : minDelay(lo), maxDelay(hi) {
        host = net::Endpoint{0x7F000001u, hostPort};
        front.Open(); front.Bind(0);
    }
    double Delay() { rng = rng * 1664525u + 1013904223u; return minDelay + (maxDelay - minDelay) * ((rng >> 8) / 16777216.0); }
    std::uint16_t Port() const { return front.LocalPort(); }
    void Pump(double dt) {
        now += dt;
        std::uint8_t buf[4096]; net::Endpoint from; int n;
        while ((n = front.RecvFrom(buf, sizeof buf, from)) > 0) {
            const std::uint64_t key = (std::uint64_t(from.address) << 16) | from.port;
            auto& u = up[key];
            if (!u) { u = std::make_unique<net::UdpSocket>(); u->Open(); u->Bind(0); back[key] = from; }
            q.push_back({now + Delay(), std::vector<std::uint8_t>(buf, buf + n), host, u.get()});
        }
        for (auto& [key, u] : up)
            while ((n = u->RecvFrom(buf, sizeof buf, from)) > 0)
                q.push_back({now + Delay(), std::vector<std::uint8_t>(buf, buf + n), back[key], &front});
        for (std::size_t i = 0; i < q.size();)
            if (q[i].at <= now + 1e-9) { q[i].via->SendTo(q[i].to, q[i].bytes.data(), q[i].bytes.size()); q.erase(q.begin() + (std::ptrdiff_t)i); }
            else ++i;
    }
};

// Per-frame steps of B's copy while A moves it at `speed`, after a warm-up,
// and how far behind A it is.
struct Motion { std::vector<float> steps, lag; };
float Smooth(const std::vector<float>& steps, float want) {   // share of frames within 20% of the real speed
    int ok = 0;
    for (float s : steps) ok += std::fabs(s - want) <= want * 0.2f;
    return steps.empty() ? 0.0f : (float)ok / (float)steps.size();
}

struct World {
    Scene hs{"Host"}, as{"A"}, bs{"B"};
    NetworkManager *host = nullptr, *a = nullptr, *b = nullptr;
    std::unique_ptr<JitterProxy> proxy;
    GameObject* mine = nullptr;   // A's object
    std::string id;
    const float dt = 1.0f / 60.0f;
    bool Open(float tickRate, float interpRate, const std::string& prefab, bool hostOwns = false, double lo = 0.03, double hi = 0.07) {
        host = hs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        host->netTickRate = tickRate;
        host->spawnablePrefabs = {prefab};
        host->maxMessagesPerSecond = 0;
        if (!host->StartServer(0)) return false;
        proxy = std::make_unique<JitterProxy>(host->ServerPort(), lo, hi);
        a = as.CreateGameObject("Net")->AddComponent<NetworkManager>();
        b = bs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        a->interpolationRate = b->interpolationRate = interpRate;
        if (!a->StartClient("127.0.0.1", proxy->Port()) || !b->StartClient("127.0.0.1", proxy->Port())) return false;
        hs.Start(); as.Start(); bs.Start();
        for (int i = 0; i < 600 && !(a->Joined() && b->Joined()); ++i) Frame();
        if (!a->Joined() || !b->Joined()) return false;
        mine = (hostOwns ? host : a)->SpawnOwned(prefab, Vec3(0.0f, 0.0f, 0.0f));
        if (!mine) return false;
        id = mine->GetComponent<NetworkSync>()->netId;
        for (int i = 0; i < 60; ++i) Frame();
        return Theirs() != nullptr;
    }
    void Frame() { hs.Update(dt); proxy->Pump(dt); as.Update(dt); bs.Update(dt); Nap(); }
    GameObject* Theirs() {
        for (const auto& g : bs.Objects())
            if (auto* ns = g->GetComponent<NetworkSync>(); ns && ns->netId == id) return g.get();
        return nullptr;
    }
};
} // namespace

int main() {
    RUN_SUITE("net_interp");
    auto sink = Log::sink;
    Log::sink = [](Log::Level, const std::string&) {};

    const std::string prefab = "okay_interp_obj.okayprefab";
    {
        Scene t("T");
        GameObject* p = t.CreateGameObject("Box");
        p->AddComponent<NetworkSync>();
        CHECK(SceneSerializer::SaveObjectToFile(*p, prefab));
    }
    const float speed = 3.0f;

    auto run = [&](World& w, int frames) {
        Motion m;
        GameObject* theirs = w.Theirs();
        float prev = theirs->transform->localPosition.x;
        for (int i = 0; i < frames; ++i) {
            w.mine->transform->localPosition.x += speed * w.dt;
            w.Frame();
            const float x = theirs->transform->localPosition.x;
            if (i >= 120) { m.steps.push_back(x - prev); m.lag.push_back(w.mine->transform->localPosition.x - x); }
            prev = x;
        }
        return m;
    };
    const float want = speed * (1.0f / 60.0f);
    float oldSmooth = 0.0f;

    // ---- No fixed tick: the old easing, under the same jitter (the baseline) ---------
    {
        World w;
        CHECK(w.Open(0.0f, 12.0f, prefab));
        CHECK(!w.b->InterpolationBuffer());
        const Motion m = run(w, 420);
        oldSmooth = Smooth(m.steps, want);
        w.a->Stop(); w.b->Stop(); w.host->Stop();
    }

    // ---- Fixed tick: the buffer -----------------------------------------------------
    {
        World w;
        CHECK(w.Open(30.0f, 12.0f, prefab));
        CHECK(w.b->InterpolationBuffer() && w.host->InterpolationBuffer());
        GameObject* theirs = w.Theirs();
        // A moves its object at a steady 3 u/s for 7 s (the first 2 s warm up).
        const Motion m = run(w, 420);
        // Steady on B too, whatever order and spacing the packets came in:
        // never backwards, the right speed on average, and nearly every frame
        // within 20% of it (a delay spike bigger than any before can still
        // hold it for a frame while the buffer grows).
        float lo = 1e9f, sum = 0.0f;
        for (float s : m.steps) { lo = std::min(lo, s); sum += s; }
        CHECK(lo >= 0.0f);
        CHECK(std::fabs(sum / m.steps.size() - want) < want * 0.05f);
        const float smooth = Smooth(m.steps, want);
        CHECK(smooth >= 0.85f);
        CHECK(smooth > oldSmooth + 0.2f);                  // and far steadier than the old easing
        std::printf("  smooth: buffer %.0f%% of frames within 20%%, old easing %.0f%%; delay %.0f ms\n",
                    smooth * 100.0f, oldSmooth * 100.0f, w.b->InterpolationDelay() * 1000.0f);
        // Shown 100 ms (3 ticks) or a little more in the past: here the states
        // take two trips of 30-70 ms (A to the host to B) and come every 2 ticks,
        // so the buffer settles at what that needs, never past a second.
        CHECK(w.b->InterpolationDelay() >= 0.1f - 1e-4f && w.b->InterpolationDelay() < 0.3f);
        float lagLo = 1e9f, lagHi = -1e9f;
        for (float l : m.lag) { lagLo = std::min(lagLo, l); lagHi = std::max(lagHi, l); }
        CHECK(lagLo > speed * 0.1f && lagHi < speed * 0.45f);
        // A stops: B arrives exactly where A stopped, without overshooting.
        const float end = w.mine->transform->localPosition.x;
        float most = -1e9f;
        for (int i = 0; i < 60; ++i) { w.Frame(); most = std::max(most, theirs->transform->localPosition.x); }
        CHECK(std::fabs(theirs->transform->localPosition.x - end) < 1e-3f);
        CHECK(most <= end + 1e-3f);
        w.a->Stop(); w.b->Stop(); w.host->Stop();
    }

    // ---- The host's own objects (server-authoritative players): one trip ------------
    for (double jitter : {0.0, 1.0}) {
        World w;
        CHECK(w.Open(30.0f, 12.0f, prefab, /*hostOwns=*/true, 0.03 * jitter, 0.07 * jitter));
        const Motion m = run(w, 420);
        float lo = 1e9f, sum = 0.0f;
        for (float s : m.steps) { lo = std::min(lo, s); sum += s; }
        const float smooth = Smooth(m.steps, want);
        std::printf("  host's object, %s: %.0f%% of frames within 20%%; delay %.0f ms\n",
                    jitter > 0.0 ? "30-70 ms trip" : "no added delay", smooth * 100.0f, w.b->InterpolationDelay() * 1000.0f);
        CHECK(lo >= 0.0f && std::fabs(sum / m.steps.size() - want) < want * 0.05f);
        CHECK(smooth >= (jitter > 0.0 ? 0.85f : 0.97f));
        if (jitter == 0.0) CHECK(w.b->InterpolationDelay() < 0.1f + 0.07f);   // ~100 ms (+ the 2-tick spacing at syncRate 15)
        w.a->Stop(); w.b->Stop(); w.host->Stop();
    }

    // ---- interpolationRate 0 still snaps to the newest state -------------------------
    {
        World w;
        CHECK(w.Open(30.0f, 0.0f, prefab));
        CHECK(!w.b->InterpolationBuffer());
        w.mine->transform->localPosition.x = 5.0f;
        for (int i = 0; i < 30; ++i) w.Frame();
        CHECK(std::fabs(w.Theirs()->transform->localPosition.x - 5.0f) < 1e-4f);
        w.a->Stop(); w.b->Stop(); w.host->Stop();
    }

    // ---- No fixed tick: the old easing, as before -------------------------------------
    {
        World w;
        CHECK(w.Open(0.0f, 12.0f, prefab));
        w.mine->transform->localPosition.x = 5.0f;
        bool eased = false;
        for (int i = 0; i < 120; ++i) {
            w.Frame();
            const float x = w.Theirs()->transform->localPosition.x;
            eased = eased || (x > 0.01f && x < 4.9f);       // part way: eased, not snapped
        }
        CHECK(eased);
        CHECK(std::fabs(w.Theirs()->transform->localPosition.x - 5.0f) < 0.01f);
        w.a->Stop(); w.b->Stop(); w.host->Stop();
    }

    std::remove(prefab.c_str());
    Log::sink = sink;
    TEST_MAIN_RESULT();
}
