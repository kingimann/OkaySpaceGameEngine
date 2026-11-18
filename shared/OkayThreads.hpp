#pragma once
// Our own threads, recursive mutexes and atomic counters, with the same shape as the SDL calls the launcher used (so a call site
// changes by name only): CreateThread / DetachThread / WaitThread, CreateMutex / LockMutex / UnlockMutex / DestroyMutex, AtomicInt with
// Set / Get / Add / CAS. Written from the Win32 (CreateThread, CRITICAL_SECTION) and POSIX (pthread) documentation.
//
// CLAUDE.md rule 21a: nothing here includes <mutex>, <thread>, <condition_variable> or uses std::call_once. The Windows programs are
// built with MinGW's "win32" thread model, which cannot compile those. This header uses windows.h on Windows, <pthread.h> elsewhere
// and <atomic> (which needs no thread model), so any program may include it.
//
// Behaviour kept from SDL on purpose, because the launcher's code depends on it:
//   * mutexes are RECURSIVE (the owning thread may lock again; every Lock needs its own Unlock);
//   * a thread function returns an int, WaitThread hands it back and frees the thread record;
//   * DetachThread lets a thread run on and frees its record when it ends (the caller must not touch the pointer after);
//   * Add returns the value from BEFORE the addition, Set returns the value before, CAS returns true when it swapped.
#include <atomic>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <process.h>   // _beginthreadex: starts the thread with the C runtime set up for it (what SDL did), not a bare CreateThread
#else
#  include <pthread.h>
#endif

namespace okay::thr {

// ---- atomics ----
/// A 32-bit counter shared between threads (sequentially consistent). Zero when made, also as a static.
struct AtomicInt {
    std::atomic<int> value{0};
    AtomicInt() = default;
    AtomicInt(const AtomicInt& o) : value(o.value.load()) {}
    AtomicInt& operator=(const AtomicInt& o) { value.store(o.value.load()); return *this; }
};
inline int AtomicSet(AtomicInt* a, int v) { return a->value.exchange(v); }          // returns the old value
inline int AtomicGet(AtomicInt* a) { return a->value.load(); }
inline int AtomicAdd(AtomicInt* a, int v) { return a->value.fetch_add(v); }         // returns the value before the add
inline bool AtomicCAS(AtomicInt* a, int oldv, int newv) { return a->value.compare_exchange_strong(oldv, newv); }

// ---- recursive mutex ----
struct Mutex {
#if defined(_WIN32)
    CRITICAL_SECTION cs;   // recursive by nature
#else
    pthread_mutex_t mu;
#endif
};
inline Mutex* CreateMutex() {
    Mutex* m = new Mutex();
#if defined(_WIN32)
    InitializeCriticalSection(&m->cs);
#else
    pthread_mutexattr_t at;
    pthread_mutexattr_init(&at);
    pthread_mutexattr_settype(&at, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&m->mu, &at);
    pthread_mutexattr_destroy(&at);
#endif
    return m;
}
inline void DestroyMutex(Mutex* m) {
    if (!m) return;
#if defined(_WIN32)
    DeleteCriticalSection(&m->cs);
#else
    pthread_mutex_destroy(&m->mu);
#endif
    delete m;
}
/// 0 on success (like SDL_LockMutex); locking a null mutex does nothing and succeeds.
inline int LockMutex(Mutex* m) {
    if (!m) return 0;
#if defined(_WIN32)
    EnterCriticalSection(&m->cs);
    return 0;
#else
    return pthread_mutex_lock(&m->mu) == 0 ? 0 : -1;
#endif
}
inline int UnlockMutex(Mutex* m) {
    if (!m) return 0;
#if defined(_WIN32)
    LeaveCriticalSection(&m->cs);
    return 0;
#else
    return pthread_mutex_unlock(&m->mu) == 0 ? 0 : -1;
#endif
}

// ---- threads ----
using ThreadFunction = int (*)(void*);
struct Thread {
    ThreadFunction fn = nullptr;
    void* data = nullptr;
    int status = 0;
    enum State { Running = 0, Finished = 1, Detached = 2 };
    std::atomic<int> state{Running};
#if defined(_WIN32)
    HANDLE handle = nullptr;
#else
    pthread_t id;
#endif
};
namespace detail {
inline void Run(Thread* t) {
    t->status = t->fn(t->data);
    // Finished first; if the owner already detached, nobody will ever wait for us: free the record here.
    if (t->state.exchange(Thread::Finished) == Thread::Detached) delete t;
}
#if defined(_WIN32)
inline unsigned __stdcall Entry(void* p) { Run(static_cast<Thread*>(p)); return 0; }
#else
inline void* Entry(void* p) { Run(static_cast<Thread*>(p)); return nullptr; }
#endif
}
/// Start `fn(data)` on a new thread. `name` is only for humans (kept for the call shape; not used). Null when the system refuses.
inline Thread* CreateThread(ThreadFunction fn, const char* name, void* data) {
    (void)name;
    if (!fn) return nullptr;
    Thread* t = new Thread();
    t->fn = fn; t->data = data;
#if defined(_WIN32)
    t->handle = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, &detail::Entry, t, 0, nullptr));   // 0 = failed
    if (!t->handle) { delete t; return nullptr; }
#else
    if (pthread_create(&t->id, nullptr, &detail::Entry, t) != 0) { delete t; return nullptr; }
#endif
    return t;
}
/// Wait for the thread to end, store its return value in `*status` (when given) and free the record.
inline void WaitThread(Thread* t, int* status) {
    if (!t) return;
#if defined(_WIN32)
    WaitForSingleObject(t->handle, INFINITE);
    CloseHandle(t->handle);
#else
    pthread_join(t->id, nullptr);
#endif
    if (status) *status = t->status;
    delete t;
}
/// Let the thread run on its own; its record is freed when it ends. Do not use the pointer afterwards.
inline void DetachThread(Thread* t) {
    if (!t) return;
#if defined(_WIN32)
    CloseHandle(t->handle);
#else
    pthread_detach(t->id);
#endif
    if (t->state.exchange(Thread::Detached) == Thread::Finished) delete t;   // it had already ended
}

} // namespace okay::thr
