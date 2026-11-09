// Job system: inline and threaded pools, dependencies, waiting inside jobs, ParallelFor
// and ParallelReduce against a serial reference, exceptions, cancellation, shutdown,
// the main-thread queue and the counters. Usage: test_jobs [repeats]
// CHECK is not thread safe, so it is only used on the main thread; jobs record into atomics.
#include "test_framework.hpp"
#include "okay/Core/Jobs.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <new>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace okay;

// ---- allocation fault injection --------------------------------------------------------
// Every global allocation goes through here. A test arms a countdown on its own thread: the
// n-th allocation from then on throws std::bad_alloc. Other threads are never affected.
namespace {
thread_local long t_failCountdown = -1;
void* AllocOrThrow(std::size_t n) {
    if (t_failCountdown > 0 && --t_failCountdown == 0) { t_failCountdown = -1; throw std::bad_alloc(); }
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
} // namespace
void* operator new(std::size_t n) { return AllocOrThrow(n); }
void* operator new[](std::size_t n) { return AllocOrThrow(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {

constexpr double kLimit = 30.0;   // seconds any wait may take before the test calls it a hang

void SleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

// A job body parks on this until the test opens it.
struct Gate {
    std::atomic<bool> open{false};
    std::atomic<int> waiting{0};
    void Pass() {
        ++waiting;
        while (!open.load()) SleepMs(1);
    }
    void Open() { open = true; }
    bool WaitForWaiters(int n, int ms = 10000) {
        for (int i = 0; i < ms && waiting.load() < n; ++i) SleepMs(1);
        return waiting.load() >= n;
    }
};

bool Finish(JobSystem& js, const JobHandle& h) { return js.WaitFor(h, kLimit); }

const int kWorkerCounts[] = {0, 1, 2, 4};

// ---- basics ------------------------------------------------------------------

void TestInline() {
    RUN_SUITE("jobs: inline (0 workers)");
    JobSystem js(0);
    CHECK(js.WorkerCount() == 0);
    std::vector<int> order;
    std::thread::id caller = std::this_thread::get_id();
    bool sameThread = true;
    JobHandle a = js.Submit([&] { order.push_back(1); sameThread = sameThread && std::this_thread::get_id() == caller; });
    CHECK(a.IsDone() && a.Completed());                       // finished before Submit returned
    JobHandle b = js.Submit([&] { order.push_back(2); }, {a});
    JobHandle c = js.Submit([&] { order.push_back(3); }, {a, b});
    CHECK(b.IsDone() && c.IsDone());
    CHECK((order == std::vector<int>{1, 2, 3}));
    CHECK(sameThread);
    CHECK(js.Stats().ranInline == 3);

    // nested submit runs at once
    std::vector<int> nested;
    js.Submit([&] { nested.push_back(1); js.Submit([&] { nested.push_back(2); }); nested.push_back(3); });
    CHECK((nested == std::vector<int>{1, 2, 3}));

    // ParallelFor is serial, in order, with the same chunk boundaries as the pool would use
    using Chunk = std::pair<std::size_t, std::size_t>;
    std::vector<Chunk> chunks;
    js.ParallelFor(5, 27, 8, [&](std::size_t b0, std::size_t e0) { chunks.emplace_back(b0, e0); });
    CHECK(chunks.size() == 3 && chunks[0] == Chunk(5, 13) && chunks[1] == Chunk(13, 21) && chunks[2] == Chunk(21, 27));
    std::vector<std::size_t> seen;
    js.ParallelFor(0, 10, 3, [&](std::size_t i) { seen.push_back(i); });
    CHECK(seen.size() == 10 && std::is_sorted(seen.begin(), seen.end()));
    // empty and reversed ranges do nothing
    int calls = 0;
    js.ParallelFor(4, 4, 1, [&](std::size_t) { ++calls; });
    js.ParallelFor(9, 3, 1, [&](std::size_t) { ++calls; });
    CHECK(calls == 0);

    // main-thread jobs wait for the pump even here
    int ran = 0;
    JobHandle m = js.RunOnMainThread([&] { ++ran; });
    CHECK(ran == 0 && !m.IsDone());
    JobHandle after = js.Submit([&] { ran += 10; }, {m});   // blocked until the main job ran
    CHECK(ran == 0 && !after.IsDone());
    CHECK(js.PumpMainThread() == 1);
    CHECK(ran == 11 && m.IsDone() && after.IsDone());        // the pump also released the inline job

    // Wait on a blocked job from the main thread pumps the main queue itself
    int w = 0;
    JobHandle m2 = js.RunOnMainThread([&] { w = 1; });
    JobHandle d2 = js.Submit([&] { w += 1; }, {m2});
    js.Wait(d2);
    CHECK(w == 2);

    // a failing job is reported, not thrown, at Submit
    JobHandle bad = js.Submit([] { throw std::runtime_error("boom"); });
    CHECK(bad.Failed() && bad.Error() == "boom");
    bool threw = false;
    try { js.Wait(bad); } catch (const std::runtime_error& e) { threw = std::string(e.what()) == "boom"; }
    CHECK(threw);
}

void TestEmptyHandle() {
    RUN_SUITE("jobs: handles");
    JobHandle none;
    CHECK(!none.Valid() && none.IsDone() && !static_cast<bool>(none));
    CHECK(none.Error().empty() && !none.Exception());
    JobSystem js(2);
    js.Wait(none);
    CHECK(js.WaitFor(none, 0.0));
    CHECK(!js.Cancel(none));
    JobHandle h = js.Submit([] {}, {none, none});          // empty handles are harmless dependencies
    CHECK(Finish(js, h) && h.Completed());
    js.Submit(std::function<void()>());                     // an empty function is a no-op, not a crash
    CHECK(js.WaitIdleFor(kLimit));
}

void TestPool(int workers) {
    JobSystem js(workers);
    const int N = 2000;
    std::atomic<int> sum{0};
    std::vector<JobHandle> hs;
    for (int i = 0; i < N; ++i) hs.push_back(js.Submit([&sum, i] { sum += i; }));
    js.WaitAll(hs);
    CHECK(sum.load() == N * (N - 1) / 2);
    for (auto& h : hs) CHECK(h.Completed());
    CHECK(js.WaitIdleFor(kLimit));
    const JobStats s = js.Stats();
    CHECK(s.submitted == (std::uint64_t)N && s.completed == (std::uint64_t)N && s.failed == 0 && s.cancelled == 0);
    CHECK(s.queueDepth == 0 && s.blocked == 0 && s.running == 0);
    CHECK(s.ranOnWorkers + s.ranHelping + s.ranInline == (std::uint64_t)N);
    if (workers == 0) CHECK(s.ranInline == (std::uint64_t)N);
}

// ---- dependencies ---------------------------------------------------------------

void TestDependencies(int workers) {
    JobSystem js(workers);
    // chain: strictly ordered
    {
        std::mutex mu; std::vector<int> order;
        auto rec = [&](int v) { std::lock_guard<std::mutex> g(mu); order.push_back(v); };
        JobHandle prev;
        for (int i = 0; i < 50; ++i) prev = js.Submit([&rec, i] { rec(i); }, {prev});
        CHECK(Finish(js, prev));
        std::vector<int> want(50); std::iota(want.begin(), want.end(), 0);
        CHECK(order == want);
    }
    // diamond: D runs after both B and C, which run after A
    {
        std::atomic<int> a{0}, b{0}, c{0}, d{0}; std::atomic<int> bad{0};
        JobHandle A = js.Submit([&] { a = 1; });
        JobHandle B = js.Submit([&] { if (!a) ++bad; b = 1; }, {A});
        JobHandle C = js.Submit([&] { if (!a) ++bad; c = 1; }, {A});
        JobHandle D = js.Submit([&] { if (!b || !c) ++bad; d = 1; }, {B, C});
        CHECK(Finish(js, D));
        CHECK(bad.load() == 0 && d.load() == 1);
    }
    // wide fan-in and fan-out
    {
        std::atomic<int> done{0}; std::atomic<int> early{0};
        std::vector<JobHandle> parts;
        for (int i = 0; i < 200; ++i) parts.push_back(js.Submit([&] { ++done; }));
        JobHandle join = js.Submit([&] { if (done.load() != 200) ++early; }, parts);
        std::vector<JobHandle> after;
        std::atomic<int> afterRan{0};
        for (int i = 0; i < 100; ++i) after.push_back(js.Submit([&] { if (done.load() != 200) ++early; ++afterRan; }, {join}));
        js.WaitAll(after);
        CHECK(early.load() == 0 && afterRan.load() == 100 && join.Completed());
    }
    // a dependency that already finished is simply satisfied
    {
        JobHandle first = js.Submit([] {});
        CHECK(Finish(js, first));
        std::atomic<int> ran{0};
        JobHandle second = js.Submit([&] { ++ran; }, {first});
        CHECK(Finish(js, second) && ran.load() == 1);
    }
    // a failing dependency cancels the dependents (transitively) and hands them its exception
    {
        std::atomic<int> ran{0};
        JobHandle bad = js.Submit([] { throw std::logic_error("upstream broke"); });
        JobHandle d1 = js.Submit([&] { ++ran; }, {bad});
        JobHandle d2 = js.Submit([&] { ++ran; }, {d1});
        JobHandle ok = js.Submit([&] { ++ran; });
        CHECK(Finish(js, d2) && Finish(js, ok));
        CHECK(bad.Failed() && d1.Cancelled() && d2.Cancelled());
        CHECK(ran.load() == 1);                                     // only the independent one
        CHECK(d2.Error().find("upstream broke") != std::string::npos);
        bool threw = false;
        try { js.Wait(d2); } catch (const std::logic_error& e) { threw = std::string(e.what()) == "upstream broke"; }
        CHECK(threw);
        // submitting after the failure is cancelled at once
        JobHandle late = js.Submit([&] { ++ran; }, {bad});
        CHECK(late.IsDone() && late.Cancelled() && ran.load() == 1);
    }
    // dependencies from another system are refused while pending
    if (workers > 0) {
        JobSystem other(1);
        Gate g;
        JobHandle held = other.Submit([&] { g.Pass(); });
        CHECK(g.WaitForWaiters(1));
        bool threw = false;
        try { js.Submit([] {}, {held}); } catch (const std::invalid_argument&) { threw = true; }
        CHECK(threw);
        g.Open();
        CHECK(Finish(other, held));
        JobHandle fine = js.Submit([] {}, {held});                   // fine once it is done
        CHECK(Finish(js, fine));
    }
}

// ---- waiting inside jobs --------------------------------------------------------

long FibJobs(JobSystem& js, int n) {
    if (n < 2) return n;
    long a = 0, b = 0;
    JobHandle ha = js.Submit([&] { a = FibJobs(js, n - 1); });
    JobHandle hb = js.Submit([&] { b = FibJobs(js, n - 2); });
    js.Wait(ha);
    js.Wait(hb);
    return a + b;
}

void TestWaitInsideJob(int workers) {
    JobSystem js(workers);
    // one job waits on a child it submitted: with 1 worker this only works if the waiter helps
    std::atomic<int> v{0};
    JobHandle outer = js.Submit([&] {
        JobHandle inner = js.Submit([&] { v = 42; });
        js.Wait(inner);
        if (v.load() != 42) v = -1;
    });
    CHECK(Finish(js, outer) && v.load() == 42);

    // a deep chain of jobs each waiting for the next
    std::atomic<int> depth{0};
    std::function<void(int)> chain = [&](int n) {
        ++depth;
        if (n == 0) return;
        JobHandle h = js.Submit([&, n] { chain(n - 1); });
        js.Wait(h);
    };
    JobHandle top = js.Submit([&] { chain(40); });
    CHECK(Finish(js, top) && depth.load() == 41);

    // a tree of waits (more waiting jobs than workers)
    long r = 0;
    JobHandle fib = js.Submit([&] { r = FibJobs(js, 15); });
    CHECK(Finish(js, fib) && r == 610);

    // ParallelFor from inside jobs
    std::atomic<long> total{0};
    std::vector<JobHandle> hs;
    for (int j = 0; j < 8; ++j)
        hs.push_back(js.Submit([&] { js.ParallelFor(0, 1000, 10, [&](std::size_t i) { total += (long)i; }); }));
    js.WaitAll(hs);
    CHECK(total.load() == 8L * (999 * 1000 / 2));

    // a job waiting on itself is an error, not a hang (inline mode runs the job before Submit returns,
    // so there is no handle to wait on yet: skip it there)
    if (workers > 0) {
        std::shared_ptr<JobHandle> self = std::make_shared<JobHandle>();
        std::atomic<bool> started{false}, release{false}, caught{false};
        *self = js.Submit([&, self] {
            started = true;
            while (!release.load()) SleepMs(1);
            try { js.Wait(*self); } catch (const std::logic_error&) { caught = true; }
        });
        for (int i = 0; i < 5000 && !started.load(); ++i) SleepMs(1);
        release = true;
        CHECK(Finish(js, *self));
        CHECK(caught.load());
    }
}

// ---- ParallelFor / ParallelReduce -------------------------------------------------

void TestParallelFor(int workers) {
    JobSystem js(workers);
    const std::size_t sizes[] = {0, 1, 2, 3, 7, 64, 100, 1000, 4097, 20000};
    const std::size_t grains[] = {0, 1, 2, 3, 7, 64, 1000, 1u << 30};
    for (std::size_t n : sizes) {
        for (std::size_t grain : grains) {
            const std::size_t begin = (n % 3) * 5;   // a nonzero start too
            std::vector<std::atomic<int>> hits(begin + n);
            for (auto& h : hits) h = 0;
            js.ParallelFor(begin, begin + n, grain, [&](std::size_t i) { hits[i].fetch_add(1); });
            bool exact = true;
            for (std::size_t i = 0; i < hits.size(); ++i) {
                const int want = (i >= begin) ? 1 : 0;
                if (hits[i].load() != want) { exact = false; break; }
            }
            CHECK(exact);
            // range form: chunks tile the range exactly, none longer than the grain
            std::atomic<std::size_t> covered{0};
            std::atomic<int> badChunk{0};
            const std::size_t g = js.EffectiveGrain(n, grain);
            js.ParallelFor(begin, begin + n, grain, [&](std::size_t b, std::size_t e) {
                if (b >= e || e - b > g || (b - begin) % g != 0) ++badChunk;
                covered += e - b;
            });
            CHECK(covered.load() == n && badChunk.load() == 0);
        }
    }
    // against a serial reference
    const std::size_t N = 50000;
    std::vector<double> in(N), out(N), ref(N);
    for (std::size_t i = 0; i < N; ++i) in[i] = std::sin((double)i) * 100.0;
    for (std::size_t i = 0; i < N; ++i) ref[i] = in[i] * in[i] + (double)i;
    js.ParallelFor(0, N, 100, [&](std::size_t i) { out[i] = in[i] * in[i] + (double)i; });
    CHECK(out == ref);
    // nested loops
    std::atomic<long> cells{0};
    js.ParallelFor(0, 30, 1, [&](std::size_t) { js.ParallelFor(0, 100, 7, [&](std::size_t) { ++cells; }); });
    CHECK(cells.load() == 3000);
    // the helpers it queued are gone afterwards
    CHECK(js.WaitIdleFor(kLimit));
    CHECK(js.Stats().queueDepth == 0);
}

void TestParallelReduce(int workers) {
    JobSystem js(workers);
    const std::size_t sizes[] = {0, 1, 5, 100, 4097, 30000};
    const std::size_t grains[] = {0, 1, 13, 1000};
    for (std::size_t n : sizes) {
        for (std::size_t grain : grains) {
            std::vector<long> v(n);
            for (std::size_t i = 0; i < n; ++i) v[i] = (long)((i * 2654435761u) % 1000) - 500;
            const long serial = std::accumulate(v.begin(), v.end(), 0L);
            const long viaIndex = js.ParallelReduce<long>(0, n, grain, 0L, [&](std::size_t i) { return v[i]; }, [](long a, long b) { return a + b; });
            const long viaRange = js.ParallelReduce<long>(0, n, grain, 0L, [&](std::size_t b, std::size_t e) {
                long s = 0; for (std::size_t i = b; i < e; ++i) s += v[i]; return s; }, [](long a, long b) { return a + b; });
            CHECK(viaIndex == serial && viaRange == serial);
            const long mx = js.ParallelReduce<long>(0, n, grain, -1000000L, [&](std::size_t i) { return v[i]; }, [](long a, long b) { return std::max(a, b); });
            CHECK(mx == (n ? *std::max_element(v.begin(), v.end()) : -1000000L));
        }
    }
    // a combine that is not commutative: the order of the chunks is kept
    const std::size_t N = 5000;
    std::string serial;
    for (std::size_t i = 0; i < N; ++i) serial += static_cast<char>('a' + i % 26);
    const std::string cat = js.ParallelReduce<std::string>(0, N, 37, std::string(),
        [](std::size_t b, std::size_t e) { std::string s; for (std::size_t i = b; i < e; ++i) s += static_cast<char>('a' + i % 26); return s; },
        [](const std::string& a, const std::string& b) { return a + b; });
    CHECK(cat == serial);
    // bool results (no shared vector<bool> bits)
    const bool any = js.ParallelReduce<bool>(0, 10000, 3, false, [](std::size_t i) { return i == 7777; }, [](bool a, bool b) { return a || b; });
    CHECK(any);
}

// ---- exceptions -----------------------------------------------------------------

void TestExceptions(int workers) {
    JobSystem js(workers);
    std::atomic<int> handlerCalls{0};
    std::string handlerName, handlerWhat;
    std::mutex hm;
    js.SetErrorHandler([&](const std::string& name, const std::string& what) {
        std::lock_guard<std::mutex> g(hm);
        ++handlerCalls; handlerName = name; handlerWhat = what;
    });
    JobOptions named; named.name = "loader";
    JobHandle h = js.Submit([] { throw std::runtime_error("disk on fire"); }, named);
    CHECK(Finish(js, h));
    CHECK(h.Failed() && h.Error() == "disk on fire" && h.Exception());
    for (int i = 0; i < 3; ++i) {   // every Wait rethrows
        bool threw = false;
        try { js.Wait(h); } catch (const std::runtime_error& e) { threw = std::string(e.what()) == "disk on fire"; }
        CHECK(threw);
    }
    {
        std::lock_guard<std::mutex> g(hm);
        CHECK(handlerCalls.load() == 1 && handlerName == "loader" && handlerWhat == "disk on fire");
    }
    // an exception that is not a std::exception
    JobHandle weird = js.Submit([] { throw 7; });
    CHECK(Finish(js, weird) && weird.Failed() && weird.Error() == "unknown exception");
    bool gotInt = false;
    try { js.Wait(weird); } catch (int v) { gotInt = v == 7; }
    CHECK(gotInt);
    // WaitAll waits for everything, then throws the first failure
    std::atomic<int> ran{0};
    std::vector<JobHandle> group;
    group.push_back(js.Submit([&] { ++ran; }));
    group.push_back(js.Submit([&] { SleepMs(5); ++ran; throw std::runtime_error("second"); }));
    group.push_back(js.Submit([&] { SleepMs(20); ++ran; }));
    bool allThrew = false;
    try { js.WaitAll(group); } catch (const std::runtime_error& e) { allThrew = std::string(e.what()) == "second"; }
    CHECK(allThrew && ran.load() == 3 && group[2].IsDone());
    // ParallelFor: first exception comes out, the loop does not run forever, the system lives on
    std::atomic<int> visited{0};
    bool pfThrew = false;
    try {
        js.ParallelFor(0, 2000000, 10, [&](std::size_t i) { ++visited; if (i == 5) throw std::runtime_error("pf"); });
    } catch (const std::runtime_error& e) { pfThrew = std::string(e.what()) == "pf"; }
    CHECK(pfThrew);
    CHECK(visited.load() < 2000000 || workers == 0);   // the rest were skipped
    std::atomic<int> after{0};
    js.ParallelFor(0, 1000, 10, [&](std::size_t) { ++after; });
    CHECK(after.load() == 1000);
    // ParallelReduce propagates too
    bool prThrew = false;
    try {
        js.ParallelReduce<int>(0, 1000, 10, 0, [](std::size_t i) -> int { if (i == 500) throw std::runtime_error("pr"); return 1; },
                               [](int a, int b) { return a + b; });
    } catch (const std::runtime_error&) { prThrew = true; }
    CHECK(prThrew);
    // a throwing error handler is swallowed
    js.SetErrorHandler([](const std::string&, const std::string&) { throw std::runtime_error("handler"); });
    JobHandle h2 = js.Submit([] { throw std::runtime_error("x"); });
    CHECK(Finish(js, h2) && h2.Failed());
    // failures are counted and remembered
    const JobStats s = js.Stats();
    CHECK(s.failed >= 4 && s.lastError.find("x") != std::string::npos);
    // a job still works after all that
    std::atomic<int> fine{0};
    CHECK(Finish(js, js.Submit([&] { fine = 1; })) && fine.load() == 1);
}

// ---- cancellation -----------------------------------------------------------------

void TestCancel() {
    RUN_SUITE("jobs: cancellation");
    JobSystem js(1);
    Gate gate;
    JobHandle blocker = js.Submit([&] { gate.Pass(); });
    CHECK(gate.WaitForWaiters(1));                         // the only worker is parked
    std::atomic<int> ran{0};
    std::vector<JobHandle> q;
    for (int i = 0; i < 10; ++i) q.push_back(js.Submit([&] { ++ran; }));
    JobHandle dep = js.Submit([&] { ++ran; }, {q[3]});
    JobHandle dep2 = js.Submit([&] { ++ran; }, {dep});
    JobStats s = js.Stats();
    CHECK(s.queueDepth == 10 && s.blocked == 2 && s.peakQueueDepth >= 10);
    CHECK(js.Cancel(q[3]));                                // queued: cancelled, and so are its dependents
    CHECK(q[3].Cancelled() && dep.Cancelled() && dep2.Cancelled());
    CHECK(!js.Cancel(q[3]));                               // already cancelled
    CHECK(!js.Cancel(blocker));                            // running: too late
    CHECK(js.Cancel(q[7]));
    s = js.Stats();
    CHECK(s.queueDepth == 8 && s.blocked == 0);
    gate.Open();
    CHECK(Finish(js, blocker));
    CHECK(js.WaitIdleFor(kLimit));
    CHECK(ran.load() == 8);                                // 10 queued - 2 cancelled; dependents never ran
    CHECK(!js.Cancel(q[0]));                               // finished
    s = js.Stats();
    CHECK(s.cancelled == 4 && s.completed == 9 && s.submitted == 13);   // q3, q7, dep, dep2 cancelled
    // a cancelled job's handle reads as done and waiting on it returns
    js.Wait(q[3]);
    CHECK(q[3].IsDone() && q[3].Error().empty());

    // CancelPending takes everything that has not started, blocked jobs included
    Gate g2;
    JobHandle blocker2 = js.Submit([&] { g2.Pass(); });
    CHECK(g2.WaitForWaiters(1));
    std::atomic<int> ran2{0};
    JobHandle a = js.Submit([&] { ++ran2; });
    JobHandle b = js.Submit([&] { ++ran2; }, {blocker2});  // blocked on the running job
    JobHandle c = js.Submit([&] { ++ran2; }, {a});
    CHECK(js.CancelPending() == 3);
    CHECK(a.Cancelled() && b.Cancelled() && c.Cancelled());
    g2.Open();
    CHECK(Finish(js, blocker2) && js.WaitIdleFor(kLimit));
    CHECK(ran2.load() == 0);

    // captured state is released when a job is cancelled, and after it ran
    std::weak_ptr<int> weak;
    Gate g3;
    JobHandle blocker3 = js.Submit([&] { g3.Pass(); });
    CHECK(g3.WaitForWaiters(1));
    {
        auto token = std::make_shared<int>(5);
        weak = token;
        JobHandle victim = js.Submit([token] { (void)token; });
        CHECK(!weak.expired());
        CHECK(js.Cancel(victim));
    }
    CHECK(weak.expired());
    g3.Open();
    CHECK(Finish(js, blocker3));
    std::weak_ptr<int> weak2;
    {
        auto token = std::make_shared<int>(6);
        weak2 = token;
        JobHandle h = js.Submit([token] { (void)token; });
        CHECK(Finish(js, h));
    }
    CHECK(weak2.expired());
}

void TestPriority() {
    RUN_SUITE("jobs: priority");
    JobSystem js(1);
    Gate gate;
    JobHandle blocker = js.Submit([&] { gate.Pass(); });
    CHECK(gate.WaitForWaiters(1));
    std::mutex mu; std::string order;
    auto job = [&](char c) { return [&, c] { std::lock_guard<std::mutex> g(mu); order += c; }; };
    JobOptions lo, no, hi;
    lo.priority = JobPriority::Low; no.priority = JobPriority::Normal; hi.priority = JobPriority::High;
    JobHandle l1 = js.Submit(job('l'), lo);
    JobHandle n1 = js.Submit(job('n'), no);
    JobHandle h1 = js.Submit(job('h'), hi);
    JobHandle n2 = js.Submit(job('N'), no);
    JobHandle h2 = js.Submit(job('H'), hi);
    gate.Open();
    // poll instead of Wait: a waiting main thread would run jobs itself and blur the order
    for (int i = 0; i < 10000 && !(l1.IsDone() && n1.IsDone() && h1.IsDone() && n2.IsDone() && h2.IsDone()); ++i) SleepMs(1);
    CHECK(Finish(js, blocker));
    std::lock_guard<std::mutex> g(mu);
    CHECK(order == "hHnNl");
}

// ---- shutdown ---------------------------------------------------------------------

void TestShutdown() {
    RUN_SUITE("jobs: shutdown");
    // drain: everything queued still runs
    {
        JobSystem js(2);
        std::atomic<int> ran{0};
        Gate gate;
        js.Submit([&] { gate.Pass(); });
        js.Submit([&] { gate.Pass(); });
        CHECK(gate.WaitForWaiters(2));
        for (int i = 0; i < 100; ++i) js.Submit([&] { ++ran; });
        std::thread opener([&] { SleepMs(20); gate.Open(); });
        js.Shutdown(false);
        opener.join();
        CHECK(ran.load() == 100);
        CHECK(js.WorkerCount() == 0);
        // afterwards the system runs inline
        JobHandle h = js.Submit([&] { ran += 1000; });
        CHECK(h.IsDone() && ran.load() == 1100);
        js.Shutdown(false);                                // twice is fine
    }
    // cancel: what has not started never starts; the running job finishes
    {
        JobSystem js(1);
        std::atomic<int> ran{0}; std::atomic<bool> finished{false};
        Gate gate;
        js.Submit([&] { gate.Pass(); SleepMs(10); finished = true; });
        CHECK(gate.WaitForWaiters(1));
        std::vector<JobHandle> hs;
        for (int i = 0; i < 20; ++i) hs.push_back(js.Submit([&] { ++ran; }));
        std::thread opener([&] { SleepMs(20); gate.Open(); });
        js.Shutdown(true);
        opener.join();
        CHECK(finished.load() && ran.load() == 0);
        bool all = true;
        for (auto& h : hs) all = all && h.Cancelled();
        CHECK(all);
        CHECK(js.Stats().cancelled == 20);
    }
    // destructor drains by default...
    {
        std::atomic<int> ran{0};
        Gate gate;                               // outlives the system: its jobs touch it while the destructor runs
        std::thread opener;
        {
            JobSystem js(1);
            js.Submit([&] { gate.Pass(); });
            CHECK(gate.WaitForWaiters(1));
            for (int i = 0; i < 30; ++i) js.Submit([&] { ++ran; });
            opener = std::thread([&] { SleepMs(10); gate.Open(); });
        }
        opener.join();
        CHECK(ran.load() == 30);
    }
    // ...or cancels
    {
        std::atomic<int> ran{0};
        Gate gate;
        std::thread opener;
        {
            JobSystem js(1);
            js.SetDrainOnDestroy(false);
            js.Submit([&] { gate.Pass(); });
            CHECK(gate.WaitForWaiters(1));
            for (int i = 0; i < 30; ++i) js.Submit([&] { ++ran; });
            opener = std::thread([&] { SleepMs(10); gate.Open(); });
        }
        opener.join();
        CHECK(ran.load() == 0);
    }
    // a running job that submits while a cancelling shutdown is in progress gets a cancelled handle
    {
        JobSystem js(1);
        std::atomic<bool> started{false}, go{false};
        std::atomic<int> childRan{0};
        std::shared_ptr<JobHandle> child = std::make_shared<JobHandle>();
        std::atomic<bool> childSet{false};
        js.Submit([&, child] {
            started = true;
            while (!go.load()) SleepMs(1);
            *child = js.Submit([&] { ++childRan; });
            childSet = true;
        });
        for (int i = 0; i < 5000 && !started.load(); ++i) SleepMs(1);
        std::thread t([&] { SleepMs(30); go = true; });
        js.Shutdown(true);
        t.join();
        CHECK(childSet.load() && child->Cancelled() && childRan.load() == 0);
    }
    // shutdown with main-thread jobs queued: they are run, not dropped (a job may wait on them)
    {
        JobSystem js(2);
        std::atomic<int> mainRan{0};
        std::atomic<bool> workerSaw{false};
        JobHandle m = js.RunOnMainThread([&] { ++mainRan; });
        js.Submit([&, m] { js.Wait(m); workerSaw = true; });
        js.Shutdown(false);
        CHECK(mainRan.load() == 1 && workerSaw.load());
    }
    // Configure swaps the pool
    {
        JobSystem js(1);
        CHECK(js.WorkerCount() == 1);
        js.Configure(3);
        CHECK(js.WorkerCount() == 3);
        std::atomic<int> ran{0};
        js.ParallelFor(0, 1000, 10, [&](std::size_t) { ++ran; });
        CHECK(ran.load() == 1000);
        js.Configure(0);
        CHECK(js.WorkerCount() == 0);
        CHECK(js.Submit([&] { ++ran; }).IsDone() && ran.load() == 1001);
    }
    // many create / destroy cycles with work in flight must not hang or crash
    for (int i = 0; i < 30; ++i) {
        JobSystem js(1 + i % 3);
        std::atomic<int> ran{0};
        for (int k = 0; k < 20; ++k) js.Submit([&] { ++ran; });
        if (i % 2) js.Shutdown(true); else js.Shutdown(false);
        CHECK(ran.load() <= 20);
    }
}

// ---- the main-thread queue ----------------------------------------------------------

void TestMainThread() {
    RUN_SUITE("jobs: main thread queue");
    JobSystem js(2);
    CHECK(js.IsMainThread());
    const auto mainId = std::this_thread::get_id();
    std::atomic<int> wrongThread{0};
    std::vector<int> order;                                  // touched only by the pump
    std::vector<JobHandle> posted;
    // post from workers
    JobHandle producer = js.Submit([&] {
        for (int i = 0; i < 20; ++i)
            posted.push_back(js.RunOnMainThread([&, i] { if (std::this_thread::get_id() != mainId) ++wrongThread; order.push_back(i); }));
    });
    for (int i = 0; i < 10000 && !producer.IsDone(); ++i) SleepMs(1);   // poll: Wait on the main thread would pump
    CHECK(producer.IsDone());
    SleepMs(5);
    CHECK(order.empty());                                    // nothing runs until the pump
    CHECK(js.Stats().mainQueueDepth == 20);
    CHECK(js.PumpMainThread(5) == 5);                        // a cap
    CHECK(order.size() == 5);
    CHECK(js.PumpMainThread() == 15);
    CHECK(wrongThread.load() == 0 && order.size() == 20);
    bool fifo = true;
    for (int i = 0; i < 20; ++i) fifo = fifo && order[i] == i;
    CHECK(fifo);
    for (auto& h : posted) CHECK(h.Completed());
    CHECK(js.Stats().ranOnMainThread == 20 && js.Stats().mainQueueDepth == 0);
    CHECK(js.PumpMainThread() == 0);

    // a job posted by a main job waits for the next pump
    int first = 0, second = 0;
    js.RunOnMainThread([&] { first = 1; js.RunOnMainThread([&] { second = 1; }); });
    CHECK(js.PumpMainThread() == 1 && first == 1 && second == 0);
    CHECK(js.PumpMainThread() == 1 && second == 1);

    // a worker waiting on a main-thread job, and the main thread pumping while it waits
    std::atomic<int> v{0};
    JobHandle m = js.RunOnMainThread([&] { v = 9; });
    std::atomic<int> seen{-1};
    JobHandle waiter = js.Submit([&] { js.Wait(m); seen = v.load(); });
    for (int i = 0; i < 2000 && !waiter.IsDone(); ++i) { js.PumpMainThread(); SleepMs(1); }
    CHECK(waiter.IsDone() && seen.load() == 9);

    // Wait on the main thread for a main-thread job does not need a separate pump
    int hit = 0;
    JobHandle m2 = js.RunOnMainThread([&] { hit = 1; });
    js.Wait(m2);
    CHECK(hit == 1);

    // main jobs can depend on pool jobs and the other way round
    std::atomic<int> stage{0};
    JobHandle pool = js.Submit([&] { SleepMs(5); stage = 1; });
    int sawStage = -1;
    JobHandle mainAfter = js.RunOnMainThread([&] { sawStage = stage.load(); }, {pool});
    js.Wait(mainAfter);
    CHECK(sawStage == 1);

    // an exception in a main job is recorded; the pump goes on
    int ok1 = 0, ok2 = 0;
    js.RunOnMainThread([&] { ok1 = 1; });
    JobHandle bad = js.RunOnMainThread([] { throw std::runtime_error("main boom"); });
    js.RunOnMainThread([&] { ok2 = 1; });
    CHECK(js.PumpMainThread() == 3 && ok1 == 1 && ok2 == 1);
    CHECK(bad.Failed() && bad.Error() == "main boom");

    // cancelling a queued main job
    int cancelledRan = 0;
    JobHandle c = js.RunOnMainThread([&] { ++cancelledRan; });
    CHECK(js.Cancel(c) && js.PumpMainThread() == 0 && cancelledRan == 0);
    CHECK(js.Stats().mainQueueDepth == 0);

    // another thread can be named the main thread
    std::atomic<bool> otherIsMain{false};
    std::thread t([&] {
        js.AdoptMainThread();
        otherIsMain = js.IsMainThread();
    });
    t.join();
    CHECK(otherIsMain.load() && !js.IsMainThread());
    js.AdoptMainThread();
    CHECK(js.IsMainThread());
}

// ---- stats, global, concurrency --------------------------------------------------------

void TestStats() {
    RUN_SUITE("jobs: stats");
    JobSystem js(2);
    CHECK(js.Stats().workers == 2);
    std::vector<JobHandle> hs;
    for (int i = 0; i < 20; ++i) hs.push_back(js.Submit([] { SleepMs(2); }));
    js.WaitAll(hs);
    JobStats s = js.Stats();
    CHECK(s.busyMs >= 20 * 1.5);                             // 20 jobs of 2 ms each, summed over threads
    CHECK(s.completed == 20 && s.submitted == 20);
    CHECK(s.ranOnWorkers + s.ranHelping == 20);
    CHECK(!FormatJobStats(s).empty());
    js.ResetStats();
    s = js.Stats();
    CHECK(s.submitted == 0 && s.completed == 0 && s.busyMs == 0.0 && s.workers == 2);
    // the waiting thread helps: with the single worker parked, the main thread runs the job itself
    JobSystem one(1);
    Gate gate;
    one.Submit([&] { gate.Pass(); });
    CHECK(gate.WaitForWaiters(1));
    std::atomic<int> ran{0};
    JobHandle h = one.Submit([&] { ++ran; });
    one.Wait(h);
    CHECK(ran.load() == 1 && one.Stats().ranHelping == 1);
    gate.Open();
    CHECK(one.WaitIdleFor(kLimit));   // the parked job must be done with `gate` before it goes out of scope
}

void TestGlobal() {
    RUN_SUITE("jobs: global");
    JobSystem& g = JobSystem::Global();
    CHECK(&g == &JobSystem::Global());
    CHECK(g.WorkerCount() == JobSystem::DefaultWorkerCount() || g.WorkerCount() >= 0);
    CHECK(JobSystem::DefaultWorkerCount() >= 0);
    std::atomic<int> v{0};
    CHECK(Finish(g, g.Submit([&] { v = 3; })) && v.load() == 3);
    std::atomic<long> sum{0};
    g.ParallelFor(0, 10000, 0, [&](std::size_t i) { sum += (long)i; });
    CHECK(sum.load() == 10000L * 9999 / 2);
    CHECK(JobSystem::ThreadsAvailable());
}

void TestConcurrentSubmitters() {
    RUN_SUITE("jobs: concurrent submitters");
    JobSystem js(3);
    const int T = 4, PER = 1500;
    std::atomic<int> ran{0};
    std::vector<std::thread> threads;
    std::atomic<int> waitFailures{0};
    for (int t = 0; t < T; ++t)
        threads.emplace_back([&, t] {
            std::vector<JobHandle> mine;
            for (int i = 0; i < PER; ++i) {
                if (i % 7 == 0 && !mine.empty()) mine.push_back(js.Submit([&] { ++ran; }, {mine.back()}));
                else mine.push_back(js.Submit([&] { ++ran; }));
                if (i % 100 == 99) { try { js.Wait(mine[i - 50]); } catch (...) { ++waitFailures; } }
            }
            js.WaitAll(mine);
            (void)t;
        });
    for (auto& t : threads) t.join();
    CHECK(js.WaitIdleFor(kLimit));
    CHECK(ran.load() == T * PER && waitFailures.load() == 0);
    const JobStats s = js.Stats();
    CHECK(s.submitted == (std::uint64_t)(T * PER) && s.completed == s.submitted);
    // concurrent ParallelFors from several threads
    std::atomic<long> total{0};
    threads.clear();
    for (int t = 0; t < T; ++t)
        threads.emplace_back([&] { for (int k = 0; k < 20; ++k) js.ParallelFor(0, 500, 5, [&](std::size_t) { ++total; }); });
    for (auto& t : threads) t.join();
    CHECK(total.load() == (long)T * 20 * 500);
    // cancel racing with execution: every job either ran or was cancelled, never both, never neither
    std::atomic<int> ranCount{0};
    std::vector<JobHandle> hs;
    for (int i = 0; i < 2000; ++i) hs.push_back(js.Submit([&] { ++ranCount; }));
    int cancelledNow = 0;
    for (int i = 0; i < 2000; i += 2) if (js.Cancel(hs[i])) ++cancelledNow;
    CHECK(js.WaitIdleFor(kLimit));
    int c = 0, d = 0;
    for (auto& h : hs) { if (h.Cancelled()) ++c; else if (h.Completed()) ++d; }
    CHECK(c == cancelledNow && c + d == 2000 && ranCount.load() == d);
}

// ---- running out of memory ----------------------------------------------------------------

// The n-th allocation fails while a small job graph is built. Whatever n is, nothing may hang or be
// left locked, every job must end up finished or cancelled, and the system must still work.
void TestAllocationFailure(int workers) {
    RUN_SUITE(("jobs: allocation failure, " + std::to_string(workers) + " workers").c_str());
    int thrown = 0, survived = 0, hung = 0, brokenCounts = 0, notUsable = 0, queueFailures = 0;
    for (long n = 1; n <= 160; ++n) {
        JobSystem js(workers);
        std::atomic<int> ran{0};
        std::vector<JobHandle> hs;
        bool threw = false;
        t_failCountdown = n;
        try {
            JobHandle a = js.Submit([&] { ++ran; }); hs.push_back(a);
            JobHandle b = js.Submit([&] { ++ran; }, {a}); hs.push_back(b);
            JobHandle c = js.Submit([&] { throw std::runtime_error("x"); }, {a, b}); hs.push_back(c);
            JobHandle d = js.Submit([&] { ++ran; }, {c}); hs.push_back(d);
            JobHandle m = js.RunOnMainThread([&] { ++ran; }, {a}); hs.push_back(m);
            js.ParallelFor(0, 64, 4, [&](std::size_t) { ++ran; });
            hs.push_back(js.Submit([&] { ++ran; }));
            js.Cancel(hs.back());
            js.CancelPending();
        } catch (const std::bad_alloc&) {
            threw = true;
        } catch (...) {
        }
        t_failCountdown = -1;
        if (threw) ++thrown; else ++survived;
        // main-thread jobs are run by the pump; everything must settle
        for (int i = 0; i < 2000 && !js.WaitIdleFor(0.01); ++i) js.PumpMainThread();
        if (!js.WaitIdleFor(5.0)) ++hung;
        for (auto& h : hs) {
            if (!h.IsDone()) ++notUsable;
            if (h.Cancelled() && h.Error().find("out of memory") != std::string::npos) ++queueFailures;   // failed while being queued
        }
        const JobStats st = js.Stats();
        if (st.completed + st.failed + st.cancelled != st.submitted) ++brokenCounts;
        std::atomic<int> after{0};
        JobHandle f = js.Submit([&] { after = 1; });
        if (!js.WaitFor(f, 5.0) || after.load() != 1) ++notUsable;
        js.Shutdown(false);
    }
    std::cout << "  (" << thrown << " of 160 runs threw bad_alloc, " << queueFailures << " jobs failed while being queued, " << survived << " runs went through)\n";
    CHECK(hung == 0);
    CHECK(notUsable == 0);
    CHECK(brokenCounts == 0);
    CHECK(thrown > 0 && survived > 0 && (queueFailures > 0 || workers == 0));   // the sweep covered failures at the call, failures while queueing, and none
}

// Where no thread can be started (WebGL, or built with OKAY_FORCE_NO_THREADS) a pool of any
// size has 0 workers and everything runs inline: check that, and only the suites that do not
// park a job on a gate (a gate would never open while the job runs inline).
void TestNoThreads() {
    RUN_SUITE("jobs: no threads available");
    CHECK(!JobSystem::ThreadsAvailable() && JobSystem::DefaultWorkerCount() == 0);
    JobSystem js(4);
    CHECK(js.WorkerCount() == 0);
    JobSystem autoPool;
    CHECK(autoPool.WorkerCount() == 0);
    js.Configure(3);
    CHECK(js.WorkerCount() == 0);
    TestEmptyHandle();
    TestInline();
    TestPool(4);
    TestDependencies(0);
    TestWaitInsideJob(0);
    TestParallelFor(4);
    TestParallelReduce(4);
    TestExceptions(4);
}

} // namespace

int main(int argc, char** argv) {
    int reps = 1;
    if (argc > 1) reps = std::max(1, std::atoi(argv[1]));
    RUN_SUITE("jobs");
    if (!JobSystem::ThreadsAvailable()) {
        TestNoThreads();
        TEST_MAIN_RESULT();
    }
    TestEmptyHandle();
    TestInline();
    for (int r = 0; r < reps; ++r) {
        for (int w : kWorkerCounts) {
            RUN_SUITE(("jobs: " + std::to_string(w) + " workers").c_str());
            TestPool(w);
            TestDependencies(w);
            TestWaitInsideJob(w);
            TestParallelFor(w);
            TestParallelReduce(w);
            TestExceptions(w);
        }
        TestCancel();
        TestPriority();
        TestShutdown();
        TestMainThread();
        TestStats();
        TestConcurrentSubmitters();
    }
    TestAllocationFailure(0);
    TestAllocationFailure(2);
    TestGlobal();
    TEST_MAIN_RESULT();
}
