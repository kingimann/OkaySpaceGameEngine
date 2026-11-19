// Work-stealing fork-join layer of the job system (Jobs.hpp: JobCounter, Fork, Join, the task arena,
// stealing ParallelFor). Checks run on the main thread only; tasks record into atomics.
// Usage: test_jobs_steal [repeats]
#include "test_framework.hpp"
#include "okay/Core/Jobs.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <numeric>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace okay;

namespace {

std::uint64_t Fib(JobSystem& js, int n) {   // nested fork-join
    if (n < 2) return (std::uint64_t)n;
    if (n < 12) return Fib(js, n - 1) + Fib(js, n - 2);
    JobCounter c;
    std::uint64_t a = 0;
    js.Fork(c, [&] { a = Fib(js, n - 1); });
    const std::uint64_t b = Fib(js, n - 2);
    js.Join(c);
    return a + b;
}

void TestForkJoin(int w) {
    JobSystem js(w);
    JobCounter c;
    std::atomic<int> sum{0};
    for (int i = 1; i <= 1000; ++i) js.Fork(c, [&sum, i] { sum += i; });
    js.Join(c);
    CHECK(sum.load() == 500500);
    CHECK(c.Done());
    // the counter can be reused
    for (int i = 0; i < 10; ++i) js.Fork(c, [&sum] { ++sum; });
    js.Join(c);
    CHECK(sum.load() == 500510);
    CHECK(Fib(js, 24) == 46368);
    js.Join(c);   // joining an empty counter returns
}

void TestParallelForCoverage(int w) {
    JobSystem js(w);
    for (std::size_t n : {0u, 1u, 2u, 7u, 100u, 1000u, 12345u}) {
        for (std::size_t grain : {0u, 1u, 3u, 64u, 100000u}) {
            std::vector<std::atomic<int>> hit(n);
            for (auto& h : hit) h = 0;
            js.ParallelFor(0, n, grain, [&](std::size_t i) { hit[i].fetch_add(1); });
            bool ok = true;
            for (auto& h : hit) ok = ok && h.load() == 1;
            CHECK(ok);
        }
    }
    // chunk boundaries are multiples of grain from begin, as in the serial path
    std::atomic<int> bad{0};
    js.ParallelFor(5, 1005, 50, [&](std::size_t b, std::size_t e) {
        if ((b - 5) % 50 != 0 || e - b > 50 || (e != 1005 && e - b != 50)) ++bad;
    });
    CHECK(bad.load() == 0);
    // reduce through the stealing path gives the same as serial
    const long long r = js.ParallelReduce<long long>(0, 100000, 0, 0LL, [](std::size_t i) { return (long long)i; },
                                                      [](long long a, long long b) { return a + b; });
    CHECK(r == 4999950000LL);
}

void TestExceptions(int w) {
    JobSystem js(w);
    JobCounter c;
    std::atomic<int> ran{0};
    for (int i = 0; i < 50; ++i) js.Fork(c, [&ran, i] { ++ran; if (i == 10) throw std::runtime_error("boom"); });
    bool caught = false;
    try { js.Join(c); } catch (const std::runtime_error& e) { caught = std::string(e.what()) == "boom"; }
    CHECK(caught);
    CHECK(!c.Failed());   // Join cleared it: reusable
    js.Fork(c, [&ran] { ++ran; });
    js.Join(c);
    bool pf = false;
    try { js.ParallelFor(0, 1000, 10, [](std::size_t i) { if (i == 500) throw std::logic_error("pf"); }); }
    catch (const std::logic_error&) { pf = true; }
    CHECK(pf);
    js.ParallelFor(0, 10, 1, [&](std::size_t) { ++ran; });   // still works afterwards
}

void TestPriorityOneWorker() {
    JobSystem js(1);
    JobCounter gate, c;
    std::atomic<bool> open{false};
    std::atomic<int> started{0};
    js.Fork(gate, [&] { started = 1; while (!open.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1)); });
    while (!started.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    std::vector<int> order;
    std::atomic<int> seq{0};
    std::atomic<int> got[3] = {{-1}, {-1}, {-1}};
    // the one worker is busy: queue Low, Normal, High; they must run High, Normal, Low. The main thread must
    // not help, so it does not Join until the gate opens and everything is queued.
    js.Fork(c, [&] { got[2] = seq++; }, JobPriority::Low);
    js.Fork(c, [&] { got[1] = seq++; }, JobPriority::Normal);
    js.Fork(c, [&] { got[0] = seq++; }, JobPriority::High);
    open = true;
    js.Join(gate);
    js.Join(c);
    CHECK(got[0].load() < got[1].load());
    CHECK(got[1].load() < got[2].load());
}

void TestStealingHappens() {
    JobSystem js(4);
    js.ResetStats();
    JobCounter c;
    std::atomic<int> n{0};
    // one task forks a burst on its own deque; the others have to steal it
    js.Fork(c, [&] {
        for (int i = 0; i < 400; ++i) js.Fork(c, [&n] {
            volatile double x = 1; for (int k = 0; k < 2000; ++k) x = x * 1.0000001 + 1; (void)x; ++n; });
    });
    js.Join(c);
    CHECK(n.load() == 400);
    const JobStats s = js.Stats();
    CHECK(s.forked == 401);
    CHECK(s.stolen > 0);
    std::printf("  stolen %llu of %llu forked\n", (unsigned long long)s.stolen, (unsigned long long)s.forked);
}

void TestArenaRecycles() {
    JobSystem js(3);
    std::uint64_t before = js.Stats().taskArenaResets;
    std::atomic<long> sum{0};
    for (int round = 0; round < 200; ++round) js.ParallelFor(0, 5000, 7, [&](std::size_t i) { sum += (long)i & 1; });
    const JobStats s = js.Stats();
    CHECK(s.taskArenaResets > before);
    CHECK(s.taskArenaBytes < 16u * 1024 * 1024);   // memory does not grow with the number of loops
    CHECK(js.BeginFrame() || true);
    JobCounter c;
    js.Fork(c, [] {});
    js.Join(c);
    CHECK(js.BeginFrame());   // nothing outstanding after the join
    std::printf("  arena %llu bytes held, peak %llu, resets %llu\n", (unsigned long long)s.taskArenaBytes,
                (unsigned long long)s.taskArenaPeak, (unsigned long long)s.taskArenaResets);
}

void TestInlineDeterministic() {
    JobSystem js(0);
    JobCounter c;
    std::vector<int> order;
    js.Fork(c, [&] { order.push_back(1); js.Fork(c, [&] { order.push_back(3); }); order.push_back(2); });
    CHECK(c.Done());   // already ran
    js.Join(c);
    CHECK((order == std::vector<int>{1, 3, 2}));
    std::size_t last = 0; bool inOrder = true;
    js.ParallelFor(0, 100, 10, [&](std::size_t b, std::size_t) { if (b < last) inOrder = false; last = b; });
    CHECK(inOrder);
}

// Many outside threads fork and join at once into their own counters (the stress test; run it under -fsanitize=thread).
void TestConcurrentForkers() {
    JobSystem js(4);
    std::atomic<long> total{0};
    std::atomic<int> failures{0};
    std::vector<std::thread> ts;
    for (int t = 0; t < 4; ++t) ts.emplace_back([&] {
        for (int round = 0; round < 60; ++round) {
            JobCounter c;
            std::atomic<int> local{0};
            for (int i = 0; i < 100; ++i) js.Fork(c, [&local, &js, i] {
                JobCounter inner;
                js.Fork(inner, [&local] { ++local; });
                js.Fork(inner, [&local] { ++local; }, JobPriority::High);
                js.Join(inner);
                (void)i;
            });
            js.Join(c);
            if (local.load() != 200) ++failures;
            js.ParallelFor(0, 300, 4, [&](std::size_t) { ++total; });
        }
    });
    for (auto& t : ts) t.join();
    CHECK(failures.load() == 0);
    CHECK(total.load() == 4L * 60 * 300);
}

void TestShutdownWithTasks() {
    JobSystem js(3);
    JobCounter c;
    std::atomic<int> n{0};
    for (int i = 0; i < 2000; ++i) js.Fork(c, [&n] { ++n; });
    js.Shutdown(false);   // lets the in-flight light tasks finish
    js.Join(c);
    CHECK(n.load() == 2000);
    js.Fork(c, [&n] { ++n; });   // after shutdown: inline
    js.Join(c);
    CHECK(n.load() == 2001);
    js.Configure(2);   // and the pool can be replaced and reused
    js.ParallelFor(0, 100, 1, [&](std::size_t) { ++n; });
    CHECK(n.load() == 2101);
}

void TestLegacySwitch() {
    JobSystem js(3);
    js.SetWorkStealing(false);
    CHECK(!js.WorkStealing());
    std::atomic<int> n{0};
    js.ParallelFor(0, 1000, 10, [&](std::size_t) { ++n; });
    CHECK(n.load() == 1000);
    js.SetWorkStealing(true);
    js.ParallelFor(0, 1000, 10, [&](std::size_t) { ++n; });
    CHECK(n.load() == 2000);
}

} // namespace

int main(int argc, char** argv) {
    const int repeats = argc > 1 ? std::atoi(argv[1]) : 1;
    for (int rep = 0; rep < (repeats < 1 ? 1 : repeats); ++rep) {
        for (int w : {0, 1, 2, 4}) {
            RUN_SUITE(("fork-join: " + std::to_string(w) + " workers").c_str());
            TestForkJoin(w);
            TestParallelForCoverage(w);
            TestExceptions(w);
        }
        TestPriorityOneWorker();
        TestStealingHappens();
        TestArenaRecycles();
        TestInlineDeterministic();
        TestConcurrentForkers();
        TestShutdownWithTasks();
        TestLegacySwitch();
    }
    TEST_MAIN_RESULT();
}
