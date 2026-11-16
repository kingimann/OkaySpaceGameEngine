#include "test_framework.hpp"
#include <Okay.hpp>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <string>
#if defined(__linux__) || defined(__APPLE__)
#include <sys/wait.h>
#include <unistd.h>
#define OKAY_TEST_CAN_FORK 1
#endif

using namespace okay;

// ParticleSystem emission limits (#888 finding 39).
//
// Emission used to cost (particles asked for) x (pool size): every new particle
// searched the pool from slot 0 for a free one, even when the pool was full. The
// steady-rate loop also took one particle at a time off a float accumulator, so a
// rate of inf (or above about 1e9 per second at 60 fps, where `accum - 1 == accum`
// in float) never left the loop and froze the game on its first frame.
//
// These checks pin the fix:
//   - one frame with an absurd rate or burst (inf, NaN, 1e9, 3e9, a burst of 1e9)
//     returns quickly, keeps the count at or under maxParticles and leaves every
//     particle finite;
//   - the cost grows linearly with the particle count;
//   - a normal rate works again afterwards;
//   - the scene loader, the script emit(), the Particle Burst node and the Action
//     List "emit" step stay safe with huge or non-finite counts.
//
// Timing budgets are generous because test machines are shared and often loaded:
// 200 ms for one frame that costs a few milliseconds after the fix.
//
// On Linux and macOS each case runs in a child process with an alarm, so a build
// that still hangs fails the test instead of hanging ctest.

namespace {

using Clock = std::chrono::steady_clock;
constexpr double kFrameBudgetMs = 200.0;
constexpr unsigned kCaseAlarmSeconds = 5;
constexpr float kDt = 1.0f / 60.0f;

double MsSince(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

bool Finite3(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

bool AllFinite(const ParticleSystem& ps) {
    for (const ParticleSystem::Particle& p : ps.Particles()) {
        if (!p.alive) continue;
        if (!Finite3(p.position) || !Finite3(p.velocity)) return false;
        if (!std::isfinite(p.life) || !std::isfinite(p.maxLife) || !std::isfinite(p.size) ||
            !std::isfinite(p.size0) || !std::isfinite(p.rotation) || !std::isfinite(p.angularVel))
            return false;
        if (!std::isfinite(p.color.r) || !std::isfinite(p.color.g) || !std::isfinite(p.color.b) ||
            !std::isfinite(p.color.a))
            return false;
    }
    return true;
}

void Say(const std::string& s) {
    std::cout << s << std::endl;   // flushed: a child killed by the alarm keeps what it printed
}

// Run `body` where a hang cannot stall the whole test: in a child process with an
// alarm where fork() exists, in this process otherwise.
void Guarded(const char* name, unsigned seconds, const std::function<void()>& body) {
#ifdef OKAY_TEST_CAN_FORK
    std::cout.flush();
    std::cerr.flush();
    const pid_t pid = fork();
    if (pid == 0) {
        alarm(seconds);
        const int failuresBefore = okaytest::g_failures;
        body();
        std::cout.flush();
        std::cerr.flush();
        _exit(okaytest::g_failures == failuresBefore ? 0 : 1);
    }
    ++okaytest::g_checks;
    int status = 0;
    if (pid < 0 || waitpid(pid, &status, 0) != pid) {
        ++okaytest::g_failures;
        std::cerr << "  FAIL: " << name << ": could not run the case in a child process\n";
        return;
    }
    if (WIFSIGNALED(status)) {
        ++okaytest::g_failures;
        std::cerr << "  FAIL: " << name << ": did not finish within " << seconds
                  << " s (killed by signal " << WTERMSIG(status) << ")\n";
    } else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        ++okaytest::g_failures;
        std::cerr << "  FAIL: " << name << " (see the lines above)\n";
    } else {
        Say(std::string("  ok: ") + name);
    }
#else
    (void)seconds;
    const int failuresBefore = okaytest::g_failures;
    body();
    if (okaytest::g_failures == failuresBefore) Say(std::string("  ok: ") + name);
    else std::cerr << "  FAIL: " << name << " (see the lines above)\n";
#endif
}

struct Fx {
    Scene scene{"ParticleLimits"};
    GameObject* go = nullptr;
    ParticleSystem* ps = nullptr;
    explicit Fx(int maxParticles) {
        scene.physicsEnabled = false;
        go = scene.CreateGameObject("Fx");
        ps = go->AddComponent<ParticleSystem>();
        ps->maxParticles = maxParticles;
        ps->emissionRate = 0.0f;
        ps->burstCount = 0;
        ps->Awake();
    }
};

// One frame with an absurd rate or burst, then nine more: each frame stays under
// the budget, the count stays at or under maxParticles, and everything is finite.
void AbsurdFrames(const char* label, float rate, int burst, int expectAlive) {
    const int kMax = 1000;
    Fx fx(kMax);
    fx.ps->emissionRate = rate;
    fx.ps->burstCount = burst;
    fx.ps->burstTime = 0.0f;
    double worst = 0.0;
    for (int f = 0; f < 10; ++f) {
        const auto t0 = Clock::now();
        fx.ps->Update(kDt);
        const double ms = MsSince(t0);
        if (ms > worst) worst = ms;
        CHECK(fx.ps->AliveCount() <= kMax);
    }
    char line[160];
    std::snprintf(line, sizeof line, "    %s: slowest of 10 frames %.2f ms, alive %d of %d", label, worst,
                  fx.ps->AliveCount(), kMax);
    Say(line);
    CHECK(worst < kFrameBudgetMs);
    CHECK(fx.ps->AliveCount() == expectAlive);
    CHECK(AllFinite(*fx.ps));
}

// A burst of n into an empty pool of n, then two frames asking for n more each
// into the full pool. The old search from slot 0 made both quadratic.
double Workload(int n) {
    Fx fx(n);
    fx.ps->burstCount = n;
    fx.ps->burstTime = 0.0f;
    fx.ps->startLifetime = 100.0f;
    const auto t0 = Clock::now();
    fx.ps->Update(kDt);
    fx.ps->emissionRate = static_cast<float>(n) * 60.0f;
    fx.ps->Update(kDt);
    fx.ps->Update(kDt);
    const double ms = MsSince(t0);
    CHECK(fx.ps->AliveCount() == n);
    return ms;
}

// The fastest of up to `runs` runs (stops early once `capMs` is spent, so a slow
// build still finishes in reasonable time).
double FastestWorkload(int n, int runs, double capMs) {
    double best = std::numeric_limits<double>::infinity(), spent = 0.0;
    for (int r = 0; r < runs; ++r) {
        const double ms = Workload(n);
        if (ms < best) best = ms;
        spent += ms;
        if (spent > capMs) break;
    }
    return best;
}

// FNV-1a over every slot of the pool (alive flag and the fields the renderer
// reads), so the state after a normal run can be compared before and after a change.
std::uint64_t PoolHash(const ParticleSystem& ps) {
    std::uint64_t h = 1469598103934665603ull;
    auto mix = [&h](const void* data, std::size_t n) {
        const unsigned char* b = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    };
    for (const ParticleSystem::Particle& p : ps.Particles()) {
        const unsigned char alive = p.alive ? 1 : 0;
        mix(&alive, 1);
        if (!p.alive) continue;
        const float f[] = {p.position.x, p.position.y, p.position.z, p.velocity.x, p.velocity.y, p.velocity.z,
                           p.life, p.maxLife, p.size, p.size0, p.rotation, p.angularVel,
                           p.color.r, p.color.g, p.color.b, p.color.a};
        mix(f, sizeof f);
    }
    return h;
}

} // namespace

int main() {
    RUN_SUITE("particle_emission_limits");

    // ---- One frame with an absurd rate or burst ----
    Guarded("rate inf fills the pool and returns", kCaseAlarmSeconds,
            [] { AbsurdFrames("rate inf", std::numeric_limits<float>::infinity(), 0, 1000); });
    Guarded("rate NaN emits nothing and returns", kCaseAlarmSeconds,
            [] { AbsurdFrames("rate NaN", std::numeric_limits<float>::quiet_NaN(), 0, 0); });
    Guarded("rate 1e9 fills the pool and returns", kCaseAlarmSeconds,
            [] { AbsurdFrames("rate 1e9", 1e9f, 0, 1000); });
    Guarded("rate 3e9 (past float's whole numbers per frame) returns", kCaseAlarmSeconds,
            [] { AbsurdFrames("rate 3e9", 3e9f, 0, 1000); });
    Guarded("burst 1e9 fills the pool and returns", kCaseAlarmSeconds,
            [] { AbsurdFrames("burst 1e9", 0.0f, 1000000000, 1000); });
    Guarded("burst of 100000 into a pool of 100000 returns", kCaseAlarmSeconds, [] {
        Fx fx(100000);
        fx.ps->burstCount = 100000;
        const auto t0 = Clock::now();
        fx.ps->Update(kDt);
        const double ms = MsSince(t0);
        char line[120];
        std::snprintf(line, sizeof line, "    burst 100000 / pool 100000: %.2f ms, alive %d", ms, fx.ps->AliveCount());
        Say(line);
        CHECK(ms < kFrameBudgetMs);
        CHECK(fx.ps->AliveCount() == 100000);
        CHECK(AllFinite(*fx.ps));
    });
    Guarded("50000 single emits into a full pool return", kCaseAlarmSeconds, [] {
        // A script loop of emit(1) calls: each one used to walk the whole full pool.
        Fx fx(50000);
        fx.ps->startLifetime = 100.0f;
        fx.ps->Emit(50000);
        const auto t0 = Clock::now();
        for (int i = 0; i < 50000; ++i) fx.ps->Emit(1);
        const double ms = MsSince(t0);
        char line[120];
        std::snprintf(line, sizeof line, "    50000 x Emit(1) into a full pool of 50000: %.2f ms", ms);
        Say(line);
        CHECK(ms < kFrameBudgetMs);
        CHECK(fx.ps->AliveCount() == 50000);
    });

    // ---- Cost grows linearly with the particle count ----
    Guarded("emission cost is linear (4N takes < 10x N)", 60, [] {
        const int n = 20000;
        const double small = FastestWorkload(n, 5, 3000.0);
        const double large = FastestWorkload(4 * n, 5, 3000.0);
        const double ratio = large / (small > 1e-6 ? small : 1e-6);
        char line[160];
        std::snprintf(line, sizeof line, "    N=%d: %.2f ms, 4N=%d: %.2f ms, ratio %.1fx (linear ~4x, quadratic ~16x)",
                      n, small, 4 * n, large, ratio);
        Say(line);
        CHECK(ratio < 10.0);
    });

    // ---- A normal rate works again afterwards ----
    Guarded("after an inf frame a normal rate works again", kCaseAlarmSeconds, [] {
        Fx fx(1000);
        fx.ps->startLifetime = 0.1f;
        fx.ps->emissionRate = std::numeric_limits<float>::infinity();
        fx.ps->Update(kDt);
        CHECK(fx.ps->AliveCount() == 1000);
        fx.ps->emissionRate = 60.0f;
        for (int f = 0; f < 60; ++f) fx.ps->Update(kDt);
        const int alive = fx.ps->AliveCount();
        // One a frame, each living 0.1 s (6 frames): about 6 alive.
        CHECK(alive >= 3 && alive <= 8);
        if (!(alive >= 3 && alive <= 8)) std::cerr << "    alive after 1 s at 60/s: " << alive << "\n";
        CHECK(AllFinite(*fx.ps));
    });
    Guarded("rate inf with dt 0 (inf * 0 = NaN) does not stop emission for good", kCaseAlarmSeconds, [] {
        Fx fx(1000);
        fx.ps->startLifetime = 10.0f;
        fx.ps->emissionRate = std::numeric_limits<float>::infinity();
        fx.ps->Update(0.0f);   // the old accumulator became NaN here and stayed NaN
        CHECK(fx.ps->AliveCount() <= 1000);
        fx.ps->emissionRate = 60.0f;
        for (int f = 0; f < 60; ++f) fx.ps->Update(kDt);
        const int alive = fx.ps->AliveCount();
        CHECK(alive >= 58 && alive <= 62);
        if (!(alive >= 58 && alive <= 62)) std::cerr << "    alive after 1 s at 60/s: " << alive << "\n";
    });

    // ---- Normal emission is unchanged ----
    // Rate, timed bursts, random lifetimes (so particles die out of order and leave
    // holes), a full pool, extra Emit calls and a Restart. The counts here are
    // exact; the hash is printed so a change can be compared by hand.
    {
        Fx fx(64);
        fx.ps->emissionRate = 37.5f;
        fx.ps->burstCount = 25;
        fx.ps->burstTime = 0.1f;
        fx.ps->duration = 0.7f;
        fx.ps->loop = true;
        fx.ps->startLifetimeRandom = 0.4f;
        fx.ps->startSizeRandom = 0.05f;
        fx.ps->speedRandom = 0.2f;
        fx.ps->shape = ParticleSystem::Shape::Cone;
        long long aliveSum = 0;
        int peak = 0;
        for (int f = 0; f < 300; ++f) {
            if (f % 50 == 25) fx.ps->Emit(7);
            if (f == 200) fx.ps->Restart();
            fx.ps->Update(kDt);
            const int a = fx.ps->AliveCount();
            aliveSum += a;
            if (a > peak) peak = a;
            CHECK(a <= 64);
        }
        CHECK(peak == 64);   // the bursts fill the pool
        CHECK(AllFinite(*fx.ps));
        char line[160];
        std::snprintf(line, sizeof line, "    normal run: alive summed over 300 frames %lld, pool hash %016llx", aliveSum,
                      static_cast<unsigned long long>(PoolHash(*fx.ps)));
        Say(line);
    }

    // ---- Paths that pass a count or a rate in from outside ----
    Guarded("scene loader clamps a huge rate and burst", kCaseAlarmSeconds, [] {
        Scene scene("Saved");
        auto* ps = scene.CreateGameObject("Fx")->AddComponent<ParticleSystem>();
        ps->emissionRate = 12345.0f;   // markers the text edit below finds
        ps->maxParticles = 300;
        ps->burstCount = 98765;
        std::string text = SceneSerializer::Serialize(scene);
        auto swap = [&text](const std::string& from, const std::string& to) {
            const std::size_t at = text.find(from);
            CHECK(at != std::string::npos);
            if (at != std::string::npos) text.replace(at, from.size(), to);
        };
        swap("particles 12345 ", "particles 3e+09 ");
        swap(" 98765 ", " 2000000000 ");
        Scene loaded("Loaded");
        std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));
        GameObject* go = loaded.Find("Fx");
        ParticleSystem* r = go ? go->GetComponent<ParticleSystem>() : nullptr;
        CHECK(r != nullptr);
        if (!r) return;
        CHECK(r->emissionRate >= 0.0f && r->emissionRate <= 100000.0f);
        CHECK(r->burstCount >= 0 && r->burstCount <= 100000);
        r->Awake();
        const auto t0 = Clock::now();
        r->Update(kDt);
        CHECK(MsSince(t0) < kFrameBudgetMs);
        CHECK(r->AliveCount() <= 300);

        // A negative rate or burst loads as 0.
        std::string neg = SceneSerializer::Serialize(scene);
        const std::size_t at = neg.find("particles 12345 ");
        CHECK(at != std::string::npos);
        if (at != std::string::npos) neg.replace(at, 16, "particles -5 ");
        const std::size_t bt = neg.find(" 98765 ");
        if (bt != std::string::npos) neg.replace(bt, 7, " -7 ");
        Scene loaded2("Loaded2");
        CHECK(SceneSerializer::Deserialize(loaded2, neg, &err));
        GameObject* go2 = loaded2.Find("Fx");
        ParticleSystem* r2 = go2 ? go2->GetComponent<ParticleSystem>() : nullptr;
        CHECK(r2 != nullptr && r2->emissionRate == 0.0f && r2->burstCount == 0);
    });

    Guarded("script emit(1000000000) fills the pool and returns", kCaseAlarmSeconds, [] {
        Scene scene("Script");
        scene.physicsEnabled = false;
        GameObject* go = scene.CreateGameObject("Sparks");
        auto* ps = go->AddComponent<ParticleSystem>();
        ps->maxParticles = 300;
        ps->emissionRate = 0.0f;
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource("function start() { emit(1000000000); }"));
        const auto t0 = Clock::now();
        scene.Start();
        const double ms = MsSince(t0);
        CHECK(ms < kFrameBudgetMs);
        CHECK(ps->AliveCount() == 300);
    });

    Guarded("Particle Burst node with 1e9, inf and NaN counts", kCaseAlarmSeconds, [] {
        struct Case { const char* value; int expect; };
        for (const Case c : {Case{"1000000000", 300}, Case{"inf", 300}, Case{"nan", 0}, Case{"-5", 0}}) {
            Scene scene("Vs");
            scene.physicsEnabled = false;
            GameObject* go = scene.CreateGameObject("Hero");
            auto* ps = go->AddComponent<ParticleSystem>();
            ps->maxParticles = 300;
            ps->emissionRate = 0.0f;
            ps->playing = false;
            auto* vs = go->AddComponent<VisualScriptComponent>();
            std::string err;
            const std::string graph = std::string("node 1 OnStart\nnode 2 ParticleBurst\nnode 3 Const ") + c.value +
                                      "\nexec 1 0 2\ndata 2 0 3 0\nentry OnStart 1\n";
            CHECK(vs->LoadFromText(graph, &err));
            const auto t0 = Clock::now();
            scene.Start();
            scene.Update(0.0f);
            CHECK(MsSince(t0) < kFrameBudgetMs);
            CHECK(ps->AliveCount() == c.expect);
            if (ps->AliveCount() != c.expect)
                std::cerr << "    Const " << c.value << ": alive " << ps->AliveCount() << ", expected " << c.expect << "\n";
        }
    });

    Guarded("Action List emit with 1e9, inf and nan counts", kCaseAlarmSeconds, [] {
        struct Case { const char* value; int expect; };
        for (const Case c : {Case{"1e9", 300}, Case{"inf", 300}, Case{"nan", 0}}) {
            ActionList::ResetVars();
            Scene scene("Actions");
            scene.physicsEnabled = false;
            GameObject* go = scene.CreateGameObject("Fx");
            auto* ps = go->AddComponent<ParticleSystem>();
            ps->maxParticles = 300;
            ps->emissionRate = 0.0f;
            ps->playing = false;
            auto* al = go->AddComponent<ActionList>();
            al->trigger = ActionList::Trigger::OnStart;
            al->instructions = {ActionList::Item{"emit", {c.value}}};
            const auto t0 = Clock::now();
            scene.Start();
            scene.Update(0.016f);
            CHECK(MsSince(t0) < kFrameBudgetMs);
            CHECK(ps->AliveCount() == c.expect);
            if (ps->AliveCount() != c.expect)
                std::cerr << "    emit " << c.value << ": alive " << ps->AliveCount() << ", expected " << c.expect << "\n";
        }
    });

    TEST_MAIN_RESULT();
}
