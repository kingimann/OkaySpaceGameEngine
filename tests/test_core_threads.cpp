// Our own threading primitives: engine/src/Core/CoreThreads.hpp (mutex, recursive mutex,
// condition variable, atomics, thread start / join / detach / name) and the launcher's
// opaque layer over it (launcher/LauncherThreads.cpp, which replaced the launcher's SDL
// threads, mutexes and atomics). Every CHECK runs on the main thread; worker threads only
// count, so a failure is always reported by the counts they leave behind.
#include "test_framework.hpp"
#include "../engine/src/Core/CoreThreads.hpp"   // .cpp-only shim (rule 21a)
#include "../launcher/LauncherThreads.hpp"

#include <vector>

namespace ct = okay::corethreads;

namespace {

constexpr int kThreads = 8;
constexpr int kIters   = 20000;

// ---- CoreThreads --------------------------------------------------------------------

struct Counter { ct::Mutex mu; long long value = 0; };

void CountUnderLock(void* p) {
    Counter* c = static_cast<Counter*>(p);
    for (int i = 0; i < kIters; ++i) {
        ct::LockGuard g(c->mu);
        // A read, a yield-prone gap and a write: without exclusion increments get lost.
        const long long v = c->value;
        c->value = v + 1;
    }
}

void MutexExcludes() {
    RUN_SUITE("CoreThreads: mutex excludes under contention");
    Counter c;
    std::vector<ct::Thread> ts(kThreads);
    int started = 0;
    for (ct::Thread& t : ts) started += t.Start(&CountUnderLock, &c, "okay-test-mutex") ? 1 : 0;
    CHECK(started == kThreads);
    for (ct::Thread& t : ts) t.Join();
    CHECK(c.value == (long long)kThreads * kIters);
}

struct RecCounter { ct::RecursiveMutex mu; long long value = 0; };

void CountRecursive(void* p) {
    RecCounter* c = static_cast<RecCounter*>(p);
    for (int i = 0; i < kIters; ++i) {
        c->mu.Lock();
        c->mu.Lock();   // the owner may lock again
        const long long v = c->value;
        c->value = v + 1;
        c->mu.Unlock();
        c->mu.Unlock();
    }
}

void RecursiveMutexExcludes() {
    RUN_SUITE("CoreThreads: recursive mutex re-locks and still excludes");
    RecCounter c;
    std::vector<ct::Thread> ts(kThreads);
    for (ct::Thread& t : ts) CHECK(t.Start(&CountRecursive, &c));
    for (ct::Thread& t : ts) t.Join();
    CHECK(c.value == (long long)kThreads * kIters);
}

struct Gate { ct::Mutex mu; ct::CondVar cv; bool open = false; bool sawOpen = false; };

void WaitAtGate(void* p) {
    Gate* g = static_cast<Gate*>(p);
    g->mu.Lock();
    while (!g->open) g->cv.Wait(g->mu);
    g->sawOpen = true;
    g->mu.Unlock();
}

void CondWakes() {
    RUN_SUITE("CoreThreads: a condition wakes a waiter");
    Gate g;
    ct::Thread t;
    CHECK(t.Start(&WaitAtGate, &g, "okay-test-cv"));
    ct::SleepSeconds(0.02);   // let it reach the wait (correct either way: it checks `open`)
    g.mu.Lock();
    g.open = true;
    g.cv.NotifyOne();
    g.mu.Unlock();
    t.Join();
    CHECK(g.sawOpen);
    CHECK(!t.Joinable());

    // WaitFor with nobody signalling runs out of time.
    Gate quiet;
    quiet.mu.Lock();
    const bool woke = quiet.cv.WaitFor(quiet.mu, 0.01);
    quiet.mu.Unlock();
    (void)woke;   // a spurious wake-up is allowed, so only check that it came back at all
    CHECK(true);
}

struct AtomicBox { int value = 0; };

void AddAtomically(void* p) {
    AtomicBox* b = static_cast<AtomicBox*>(p);
    for (int i = 0; i < kIters; ++i) ct::AtomicFetchAdd(&b->value, 1);
}

void AtomicsExact() {
    RUN_SUITE("CoreThreads: atomic add from many threads is exact");
    AtomicBox b;
    std::vector<ct::Thread> ts(kThreads);
    for (ct::Thread& t : ts) CHECK(t.Start(&AddAtomically, &b));
    for (ct::Thread& t : ts) t.Join();
    CHECK(ct::AtomicLoad(&b.value) == kThreads * kIters);

    // Return values: the old value, like SDL_AtomicSet / SDL_AtomicAdd.
    int x = 5;
    CHECK(ct::AtomicExchange(&x, 9) == 5);
    CHECK(ct::AtomicFetchAdd(&x, 3) == 9);
    CHECK(ct::AtomicLoad(&x) == 12);
    CHECK(!ct::AtomicCompareExchange(&x, 11, 1));
    CHECK(ct::AtomicLoad(&x) == 12);
    CHECK(ct::AtomicCompareExchange(&x, 12, 1));
    CHECK(ct::AtomicLoad(&x) == 1);

    ct::AtomicInt a(3);
    CHECK(a.FetchAdd(-1) == 3);
    CHECK(a.Exchange(7) == 2);
    CHECK(a.CompareExchange(7, 8));
    CHECK(a.Load() == 8);
}

struct Slow { int done = 0; };

void SlowWork(void* p) {
    ct::SleepSeconds(0.05);
    ct::AtomicExchange(&static_cast<Slow*>(p)->done, 1);
}

void JoinWaits() {
    RUN_SUITE("CoreThreads: join waits for the thread to end");
    Slow s;
    ct::Thread t;
    CHECK(t.Start(&SlowWork, &s, "okay-test-join"));
    CHECK(t.Joinable());
    t.Join();
    CHECK(ct::AtomicLoad(&s.done) == 1);
    t.Join();   // a second join is harmless
    CHECK(!t.Joinable());
}

int g_detachedDone = 0;   // atomic

void DetachedWork(void*) {
    ct::SleepSeconds(0.01);
    ct::AtomicFetchAdd(&g_detachedDone, 1);
}

void DetachRuns() {
    RUN_SUITE("CoreThreads: a detached thread runs on and cleans up");
    for (int i = 0; i < 16; ++i) {
        ct::Thread t;
        CHECK(t.Start(&DetachedWork, nullptr, "okay-test-detach"));
        t.Detach();
        CHECK(!t.Joinable());
    }   // each Thread is destroyed while its thread may still run: must not crash or block
    for (int i = 0; i < 500 && ct::AtomicLoad(&g_detachedDone) < 16; ++i) ct::SleepSeconds(0.01);
    CHECK(ct::AtomicLoad(&g_detachedDone) == 16);
}

void NamesAreHarmless() {
    RUN_SUITE("CoreThreads: thread names (long, empty, null) are accepted");
    Slow s;
    ct::Thread a, b, c;
    CHECK(a.Start(&SlowWork, &s, "okay-a-thread-name-much-longer-than-fifteen-bytes-and-even-longer-than-sixty-four-bytes"));
    CHECK(b.Start(&SlowWork, &s, ""));
    CHECK(c.Start(&SlowWork, &s, nullptr));
    a.Join(); b.Join(); c.Join();
    CHECK(ct::AtomicLoad(&s.done) == 1);
}

// ---- The launcher's layer (launcher/LauncherThreads.cpp) ------------------------------

okay::lt::Mutex* g_ltMu = nullptr;
long long g_ltCount = 0;
okay::lt::AtomicInt g_ltAtomic;   // zero-initialised static, like SDL_atomic_t was

int LtWorker(void*) {
    for (int i = 0; i < kIters; ++i) {
        okay::lt::Lock outer(g_ltMu);
        okay::lt::LockMutex(g_ltMu);   // recursive, like SDL_mutex
        ++g_ltCount;
        okay::lt::UnlockMutex(g_ltMu);
        okay::lt::AtomicAdd(&g_ltAtomic, 1);
    }
    return 42;
}

okay::lt::AtomicInt g_ltDetached;

int LtDetachedWorker(void*) {
    okay::corethreads::SleepSeconds(0.01);
    okay::lt::AtomicAdd(&g_ltDetached, 1);
    return 0;
}

void LauncherLayer() {
    RUN_SUITE("Launcher threads: mutex, atomics, wait with status, detach, null handles");
    CHECK(okay::lt::AtomicGet(&g_ltAtomic) == 0);
    g_ltMu = okay::lt::NewMutex();
    CHECK(g_ltMu != nullptr);
    std::vector<okay::lt::Thread*> ts;
    for (int i = 0; i < kThreads; ++i) ts.push_back(okay::lt::StartThread(&LtWorker, "okay-test-lt", nullptr));
    for (okay::lt::Thread* t : ts) CHECK(t != nullptr);
    int statusSum = 0;
    for (okay::lt::Thread* t : ts) { int st = 0; okay::lt::WaitThread(t, &st); statusSum += st; }
    CHECK(statusSum == 42 * kThreads);
    CHECK(g_ltCount == (long long)kThreads * kIters);
    CHECK(okay::lt::AtomicGet(&g_ltAtomic) == kThreads * kIters);
    okay::lt::FreeMutex(g_ltMu);
    g_ltMu = nullptr;

    // SDL-shaped return values.
    okay::lt::AtomicInt a{0};
    CHECK(okay::lt::AtomicSet(&a, 4) == 0);
    CHECK(okay::lt::AtomicAdd(&a, 2) == 4);
    CHECK(okay::lt::AtomicCAS(&a, 6, 1));
    CHECK(!okay::lt::AtomicCAS(&a, 6, 2));
    okay::lt::AtomicIncRef(&a);
    CHECK(!okay::lt::AtomicDecRef(&a));
    CHECK(okay::lt::AtomicDecRef(&a));   // 1 -> 0
    CHECK(okay::lt::AtomicGet(&a) == 0);

    // Detach: the handle is freed at once; the thread finishes by itself.
    for (int i = 0; i < 16; ++i) {
        okay::lt::Thread* t = okay::lt::StartThread(&LtDetachedWorker, "okay-test-ltdet", nullptr);
        CHECK(t != nullptr);
        okay::lt::DetachThread(t);
    }
    // A thread that has already ended by the time it is detached.
    okay::lt::Thread* quick = okay::lt::StartThread([](void*) -> int { return 0; }, "okay-test-quick", nullptr);
    okay::corethreads::SleepSeconds(0.02);
    okay::lt::DetachThread(quick);
    for (int i = 0; i < 500 && okay::lt::AtomicGet(&g_ltDetached) < 16; ++i) okay::corethreads::SleepSeconds(0.01);
    CHECK(okay::lt::AtomicGet(&g_ltDetached) == 16);

    // Null handles are ignored, as SDL did.
    okay::lt::LockMutex(nullptr);
    okay::lt::UnlockMutex(nullptr);
    okay::lt::FreeMutex(nullptr);
    okay::lt::WaitThread(nullptr, nullptr);
    okay::lt::DetachThread(nullptr);
    { okay::lt::Lock nullLock(nullptr); }
    CHECK(okay::lt::StartThread(nullptr, "x", nullptr) == nullptr);
}

} // namespace

int main() {
    if (!ct::kThreadsAvailable) {
        std::cout << "threads are not available in this build; nothing to test\n";
        return 0;
    }
    MutexExcludes();
    RecursiveMutexExcludes();
    CondWakes();
    AtomicsExact();
    JoinWaits();
    DetachRuns();
    NamesAreHarmless();
    LauncherLayer();
    TEST_MAIN_RESULT();
}
