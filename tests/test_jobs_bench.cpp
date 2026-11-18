// Benchmark: the work-stealing ParallelFor / Fork against the older shared-queue ParallelFor
// (JobSystem::SetWorkStealing(false)), at 0..N workers. Prints a table; the only checks are that results
// are right (timing is never a pass/fail condition, so a slow CI machine cannot fail it).
// Usage: test_jobs_bench [scale]   (scale multiplies the work, default 1)
#include "test_framework.hpp"
#include "okay/Core/Jobs.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

using namespace okay;
using Clock = std::chrono::steady_clock;

namespace {
double MsSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

double Burn(int iters) { double x = 1.0; for (int k = 0; k < iters; ++k) x = x * 1.0000001 + 0.5; return x; }

// Best of 3 runs, in ms.
template <class F> double Best(F&& f) {
    double best = 1e30;
    for (int i = 0; i < 3; ++i) { auto t = Clock::now(); f(); const double ms = MsSince(t); if (ms < best) best = ms; }
    return best;
}

std::uint64_t Fib(JobSystem& js, int n) {
    if (n < 2) return (std::uint64_t)n;
    if (n < 14) return Fib(js, n - 1) + Fib(js, n - 2);
    JobCounter c; std::uint64_t a = 0;
    js.Fork(c, [&] { a = Fib(js, n - 1); });
    const std::uint64_t b = Fib(js, n - 2);
    js.Join(c);
    return a + b;
}
} // namespace

int main(int argc, char** argv) {
    const int scale = argc > 1 ? std::max(1, std::atoi(argv[1])) : 1;
    const unsigned hc = std::thread::hardware_concurrency();
    std::printf("job benchmark: %u hardware threads, scale %d (best of 3, ms; lower is better)\n", hc, scale);
    std::printf("%-34s %8s %12s %12s %8s\n", "case", "workers", "shared-queue", "stealing", "speedup");
    std::vector<float> data(1u << 20, 1.0f);
    for (int w : {0, 1, 2, 4}) {
        if (w > 0 && (unsigned)w + 1 > hc * 2) continue;
        JobSystem js(w);
        // A: many tiny items, tiny grain: the cost of scheduling dominates
        auto runA = [&](bool steal) {
            js.SetWorkStealing(steal);
            return Best([&] { for (int r = 0; r < 4 * scale; ++r) js.ParallelFor(0, 262144, 16, [&](std::size_t i) { data[i] = (float)Burn(40); }); });
        };
        // B: uneven work (items near the end cost 50x): needs stealing to balance
        auto runB = [&](bool steal) {
            js.SetWorkStealing(steal);
            return Best([&] { for (int r = 0; r < 5 * scale; ++r) js.ParallelFor(0, 4096, 1, [&](std::size_t i) { data[i] = (float)Burn(i > 3500 ? 3000 : 60); }); });
        };
        // C: a burst of small loops in a row (a frame with many little parallel sections)
        auto runC = [&](bool steal) {
            js.SetWorkStealing(steal);
            return Best([&] { for (int r = 0; r < 2000 * scale; ++r) js.ParallelFor(0, 64, 4, [&](std::size_t i) { data[i] = (float)Burn(30); }); });
        };
        struct Row { const char* name; double old, neu; };
        Row rows[3];
        rows[0] = {"A: 262144 items, grain 16 x4", runA(false), runA(true)};
        rows[1] = {"B: uneven work, 4096 items x5", runB(false), runB(true)};
        rows[2] = {"C: 2000 small loops of 64 items", runC(false), runC(true)};
        for (const Row& r : rows) std::printf("%-34s %8d %12.2f %12.2f %7.2fx\n", r.name, w, r.old, r.neu, r.neu > 0 ? r.old / r.neu : 0.0);
        js.SetWorkStealing(true);
        // D: nested fork-join (only the new API can do this without blocking a pool thread per level)
        std::uint64_t f = 0;
        const double d = Best([&] { f = Fib(js, 30 + (scale > 1 ? 2 : 0)); });
        CHECK(f == (scale > 1 ? 1346269ull : 832040ull));
        std::printf("%-34s %8d %12s %12.2f\n", "D: fork-join fib(30)", w, "n/a", d);
        const JobStats s = js.Stats();
        std::printf("   stats: forked %llu, stolen %llu, arena %llu bytes, %llu resets\n", (unsigned long long)s.forked,
                    (unsigned long long)s.stolen, (unsigned long long)s.taskArenaBytes, (unsigned long long)s.taskArenaResets);
    }
    TEST_MAIN_RESULT();
}
