// The launcher's threads, mutexes and atomics over our own CoreThreads shim (see
// LauncherThreads.hpp). Written for this engine from the Win32 and C++ standard
// library documentation; no SDL code.
#include "LauncherThreads.hpp"

#include <new>

// The private threading shim: included from .cpp files only (rule 21a).
#include "../engine/src/Core/CoreThreads.hpp"

namespace okay {
namespace lt {

namespace ct = okay::corethreads;

struct Mutex { ct::RecursiveMutex mu; };

namespace {
// What the new thread needs, and where it leaves fn's result. Shared by the thread and the
// handle with a count of two: whichever lets go last frees it, so a detached thread never
// touches memory the caller has freed, and WaitThread can still read the result.
struct Shared {
    ThreadFn fn;
    void* arg;
    int result;
    int refs;   // atomic
};

void Release(Shared* s) {
    if (ct::AtomicFetchAdd(&s->refs, -1) == 1) delete s;
}

void Trampoline(void* p) {
    Shared* s = static_cast<Shared*>(p);
    s->result = s->fn(s->arg);   // read by WaitThread only after the join
    Release(s);
}
} // namespace

struct Thread {
    ct::Thread th;
    Shared* shared = nullptr;
};

Mutex* NewMutex() { return new (std::nothrow) Mutex(); }
void LockMutex(Mutex* m)   { if (m) m->mu.Lock(); }
void UnlockMutex(Mutex* m) { if (m) m->mu.Unlock(); }
void FreeMutex(Mutex* m)   { delete m; }

Thread* StartThread(ThreadFn fn, const char* name, void* arg) {
    if (!fn) return nullptr;
    Thread* t = new (std::nothrow) Thread();
    if (!t) return nullptr;
    Shared* s = new (std::nothrow) Shared{fn, arg, 0, 2};
    if (!s) { delete t; return nullptr; }
    t->shared = s;
    if (!t->th.Start(&Trampoline, s, name)) { delete s; delete t; return nullptr; }
    return t;
}

void WaitThread(Thread* t, int* status) {
    if (!t) return;
    t->th.Join();   // the thread has stored its result
    if (status) *status = t->shared->result;
    Release(t->shared);
    delete t;
}

void DetachThread(Thread* t) {
    if (!t) return;
    t->th.Detach();
    Release(t->shared);
    delete t;
}

int  AtomicGet(AtomicInt* a)                    { return ct::AtomicLoad(&a->value); }
int  AtomicSet(AtomicInt* a, int v)             { return ct::AtomicExchange(&a->value, v); }
int  AtomicAdd(AtomicInt* a, int d)             { return ct::AtomicFetchAdd(&a->value, d); }
bool AtomicCAS(AtomicInt* a, int oldv, int newv) { return ct::AtomicCompareExchange(&a->value, oldv, newv); }

} // namespace lt
} // namespace okay
