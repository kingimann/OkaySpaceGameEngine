// okay::sync stress tests (engine/include/okay/Sync/Sync.hpp): many threads, no lost wake-ups, timeouts, thread
// results. Uses only okay::sync and std::atomic (no <thread>), so it also builds with MinGW's win32 thread model.
// Run under ThreadSanitizer too: see docs/sync.md.
#include "test_framework.hpp"
#include "okay/Sync/Sync.hpp"

#include <atomic>
#include <chrono>
#include <deque>
#include <vector>

using namespace okay::sync;

namespace {
double NowMs() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

template <class F> struct Fn { F f; static int Run(void* p) { return static_cast<Fn*>(p)->f(); } };
// Start `n` threads each running f(index); join all; returns the sum of return values.
template <class F> int RunN(int n, F f) {
    struct Arg { F* f; int i; };
    std::vector<Arg> args((size_t)n);
    std::vector<std::unique_ptr<Thread>> th;
    for (int i = 0; i < n; ++i) {
        args[(size_t)i] = {&f, i};
        th.push_back(Thread::Start([](void* p) { Arg* a = static_cast<Arg*>(p); return (*a->f)(a->i); }, "t", &args[(size_t)i]));
        CHECK(th.back() != nullptr);
    }
    int sum = 0;
    for (auto& t : th) if (t) sum += t->Join();
    return sum;
}

void TestMutex() {
    RUN_SUITE("mutex");
    Mutex mu;
    long counter = 0;
    RunN(8, [&](int) { for (int i = 0; i < 100000; ++i) { LockGuard g(mu); ++counter; } return 0; });
    CHECK(counter == 800000);
    mu.Lock(); mu.Lock();   // recursive, like SDL's
    CHECK(mu.TryLock());
    mu.Unlock(); mu.Unlock(); mu.Unlock();
    mu.Lock();
    std::atomic<int> got{-1};
    RunN(1, [&](int) { got = mu.TryLock() ? 1 : 0; if (got == 1) mu.Unlock(); return 0; });
    CHECK(got == 0);   // another thread cannot take it
    mu.Unlock();
    RunN(1, [&](int) { got = mu.TryLock() ? 1 : 0; if (got == 1) mu.Unlock(); return 0; });
    CHECK(got == 1);
}

void TestQueue() {
    RUN_SUITE("producer / consumer with a condition");
    Mutex mu; Cond notEmpty;
    std::deque<int> q;
    bool closed = false;
    const int kPer = 25000, kProd = 4, kCons = 4;
    std::atomic<long long> sum{0}, count{0};
    std::atomic<int> producersLeft{kProd};
    RunN(kProd + kCons, [&](int idx) {
        if (idx < kProd) {
            for (int i = 1; i <= kPer; ++i) { LockGuard g(mu); q.push_back(i); notEmpty.Signal(); }
            if (--producersLeft == 0) { LockGuard g(mu); closed = true; notEmpty.Broadcast(); }
        } else {
            for (;;) {
                int v;
                { LockGuard g(mu);
                  while (q.empty() && !closed) notEmpty.Wait(mu);
                  if (q.empty()) return 0;
                  v = q.front(); q.pop_front(); }
                sum += v; ++count;
            }
        }
        return 0;
    });
    CHECK(count == (long long)kPer * kProd);
    CHECK(sum == (long long)kProd * kPer * (kPer + 1) / 2);
}

void TestPingPong() {
    RUN_SUITE("no lost wake-up (ping-pong)");
    Mutex mu; Cond cv;
    int turn = 0;   // 0: A's turn, 1: B's turn
    const int kRounds = 20000;
    int a = 0, b = 0;
    RunN(2, [&](int who) {
        for (int i = 0; i < kRounds; ++i) {
            LockGuard g(mu);
            while (turn != who) cv.Wait(mu);
            (who == 0 ? a : b)++;
            turn = 1 - who;
            cv.Signal();
        }
        return 0;
    });
    CHECK(a == kRounds && b == kRounds);
}

void TestBroadcastAndTimeout() {
    RUN_SUITE("broadcast, timeouts");
    Mutex mu; Cond cv;
    bool go = false;
    std::atomic<int> woke{0}, waiting{0};
    RunN(9, [&](int idx) {
        if (idx < 8) {
            LockGuard g(mu);
            ++waiting;
            while (!go) cv.Wait(mu);
            ++woke;
        } else {
            for (;;) { { LockGuard g(mu); if (waiting == 8) { go = true; cv.Broadcast(); break; } } SleepMs(1); }
        }
        return 0;
    });
    CHECK(woke == 8);

    mu.Lock();
    double t0 = NowMs();
    const bool r = cv.WaitFor(mu, 60);
    double dt = NowMs() - t0;
    mu.Unlock();
    CHECK(!r && dt >= 55 && dt < 1500);
    // woken before the timeout
    bool flag = false;
    struct SigArg { Mutex* m; Cond* c; bool* f; } sa{&mu, &cv, &flag};
    auto th = Thread::Start([](void* p) { SleepMs(30); auto* a = static_cast<SigArg*>(p);
                                          LockGuard g(*a->m); *a->f = true; a->c->Signal(); return 0; }, "sig", &sa);
    mu.Lock();
    t0 = NowMs();
    bool ok = true;
    while (!flag && ok) ok = cv.WaitFor(mu, 5000);
    dt = NowMs() - t0;
    mu.Unlock();
    th->Join();
    CHECK(ok && flag && dt < 2000);
}

void TestSemaphore() {
    RUN_SUITE("semaphore");
    Semaphore s(0);
    CHECK(!s.TryWait() && s.Value() == 0);
    double t0 = NowMs();
    CHECK(!s.WaitFor(50));
    CHECK(NowMs() - t0 >= 45);
    s.Post(); s.Post();
    CHECK(s.Value() == 2 && s.TryWait() && s.WaitFor(10) && !s.TryWait());
    // 4 posters x 5000 posts, 4 waiters x 5000 waits: every post is consumed exactly once
    std::atomic<int> consumed{0};
    RunN(8, [&](int i) {
        for (int k = 0; k < 5000; ++k) { if (i < 4) s.Post(); else { s.Wait(); ++consumed; } }
        return 0;
    });
    CHECK(consumed == 20000 && s.Value() == 0);
    // WaitFor woken by a Post from another thread well before the timeout
    auto th = Thread::Start([](void* p) { SleepMs(30); static_cast<Semaphore*>(p)->Post(); return 0; }, nullptr, &s);
    t0 = NowMs();
    CHECK(s.WaitFor(5000) && NowMs() - t0 < 2500);
    th->Join();
    Semaphore gate(3);   // initial count
    CHECK(gate.TryWait() && gate.TryWait() && gate.TryWait() && !gate.TryWait());
}

void TestThreads() {
    RUN_SUITE("threads");
    int dummy = 7;
    auto t = Thread::Start([](void* p) { return *static_cast<int*>(p) * 6; }, "answer", &dummy);
    CHECK(t && t->Joinable());
    CHECK(t->Join() == 42 && !t->Joinable());
    CHECK(t->Join() == 0);                                   // a second Join is harmless
    CHECK(Thread::Start(nullptr, "x", nullptr) == nullptr);   // no function: no thread
    std::atomic<int> ran{0};
    for (int i = 0; i < 1000; ++i) {                         // start / join churn
        auto q = Thread::Start([](void* p) { ++*static_cast<std::atomic<int>*>(p); return 0; }, nullptr, &ran);
        if (q) q->Join();
    }
    CHECK(ran == 1000);
    std::atomic<int> det{0};
    for (int i = 0; i < 200; ++i) {                          // detached and destroyed without join
        auto q = Thread::Start([](void* p) { SleepMs(1); ++*static_cast<std::atomic<int>*>(p); return 0; }, nullptr, &det);
        if (q) q->Detach();
    }
    for (int i = 0; i < 50; ++i) {                           // destroying a joinable Thread detaches it
        Thread::Start([](void* p) { ++*static_cast<std::atomic<int>*>(p); return 0; }, nullptr, &det);
    }
    const double t0 = NowMs();
    while (det < 250 && NowMs() - t0 < 5000) SleepMs(5);
    CHECK(det == 250);
    CHECK(RunN(16, [](int i) { return i; }) == 120);         // return values come back
}
} // namespace

int main() {
    TestMutex();
    TestQueue();
    TestPingPong();
    TestBroadcastAndTimeout();
    TestSemaphore();
    TestThreads();
    TEST_MAIN_RESULT();
}
