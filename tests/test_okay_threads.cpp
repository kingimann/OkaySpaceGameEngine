// shared/OkayThreads.hpp: recursive mutexes, atomics, joined and detached threads, with the exact return-value conventions
// the launcher's code was written against (the ones SDL's calls have). Runs on any machine; uses no std::thread.
#include "OkayThreads.hpp"
#include <chrono>
#include <cstdio>
#include <ctime>

namespace t = okay::thr;
static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static void SleepMs(int ms) { struct timespec ts = {0, ms * 1000000L}; nanosleep(&ts, nullptr); }

struct Shared { t::Mutex* mu; long counter = 0; t::AtomicInt atom; t::AtomicInt started; t::AtomicInt gate; };
static int Worker(void* p) {
    Shared* s = (Shared*)p;
    t::AtomicAdd(&s->started, 1);
    while (t::AtomicGet(&s->gate) == 0) SleepMs(1);   // all workers start together
    for (int i = 0; i < 50000; ++i) {
        t::LockMutex(s->mu);
        t::LockMutex(s->mu);   // recursive: the same thread locks twice
        ++s->counter;
        t::UnlockMutex(s->mu);
        t::UnlockMutex(s->mu);
        t::AtomicAdd(&s->atom, 1);
    }
    return 42;
}
static int Quick(void* p) { t::AtomicAdd((t::AtomicInt*)p, 1); return 0; }
static int SlowDetached(void* p) { SleepMs(30); t::AtomicAdd((t::AtomicInt*)p, 1); return 0; }
static int ReturnsSeven(void*) { return 7; }

int main() {
    {   // atomics: SDL's conventions
        t::AtomicInt a;
        CHECK(t::AtomicGet(&a) == 0);
        CHECK(t::AtomicSet(&a, 5) == 0 && t::AtomicGet(&a) == 5);   // Set returns the old value
        CHECK(t::AtomicAdd(&a, 3) == 5 && t::AtomicGet(&a) == 8);   // Add returns the value before
        CHECK(!t::AtomicCAS(&a, 1, 2) && t::AtomicGet(&a) == 8);
        CHECK(t::AtomicCAS(&a, 8, 2) && t::AtomicGet(&a) == 2);
        t::AtomicInt b = a;                                         // copyable (a struct member of a copied record)
        CHECK(t::AtomicGet(&b) == 2);
        static t::AtomicInt s;                                      // zero as a static
        CHECK(t::AtomicGet(&s) == 0);
    }
    {   // a recursive mutex: the owner locks several times, and a null mutex is a harmless no-op
        t::Mutex* m = t::CreateMutex();
        CHECK(m != nullptr);
        CHECK(t::LockMutex(m) == 0 && t::LockMutex(m) == 0 && t::LockMutex(m) == 0);
        CHECK(t::UnlockMutex(m) == 0 && t::UnlockMutex(m) == 0 && t::UnlockMutex(m) == 0);
        CHECK(t::LockMutex(nullptr) == 0 && t::UnlockMutex(nullptr) == 0);
        t::DestroyMutex(m);
        t::DestroyMutex(nullptr);
    }
    {   // contention: 6 threads, the counter is exact; WaitThread returns each thread's value
        Shared s; s.mu = t::CreateMutex();
        t::Thread* th[6];
        for (auto& x : th) { x = t::CreateThread(Worker, "worker", &s); CHECK(x != nullptr); }
        while (t::AtomicGet(&s.started) < 6) SleepMs(1);
        t::AtomicSet(&s.gate, 1);
        int sumStatus = 0;
        for (auto& x : th) { int st = 0; t::WaitThread(x, &st); sumStatus += st; }
        CHECK(sumStatus == 6 * 42);
        CHECK(s.counter == 6L * 50000);
        CHECK(t::AtomicGet(&s.atom) == 6 * 50000);
        t::DestroyMutex(s.mu);
    }
    {   // WaitThread without a status pointer, and a null thread
        t::Thread* x = t::CreateThread(ReturnsSeven, "seven", nullptr);
        CHECK(x != nullptr);
        t::WaitThread(x, nullptr);
        t::WaitThread(nullptr, nullptr);
        t::DetachThread(nullptr);
        CHECK(t::CreateThread(nullptr, "none", nullptr) == nullptr);
        int st = 0; t::Thread* y = t::CreateThread(ReturnsSeven, "seven", nullptr); t::WaitThread(y, &st); CHECK(st == 7);
    }
    {   // detached threads: fast ones (finish before Detach), slow ones (finish after), 300 of each; all run, nothing leaks (run under ASan to see)
        t::AtomicInt done;
        for (int i = 0; i < 300; ++i) { t::Thread* x = t::CreateThread(Quick, "q", &done); if (x) t::DetachThread(x); }
        for (int i = 0; i < 300; ++i) { t::Thread* x = t::CreateThread(SlowDetached, "s", &done); if (x) t::DetachThread(x); }
        for (int i = 0; i < 2000 && t::AtomicGet(&done) < 600; ++i) SleepMs(2);
        CHECK(t::AtomicGet(&done) == 600);
        SleepMs(50);   // let the last records be freed before the process looks at them
    }
    std::printf("%d checks, %d failures\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
