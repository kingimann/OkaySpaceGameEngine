#pragma once
// The launcher's threads, mutexes and atomics: our own code in place of SDL's
// (SDL_CreateThread, SDL_CreateMutex, SDL_AtomicGet and the rest).
//
// This header only declares opaque handles and functions, so launcher headers
// (VoiceChat.hpp) can use it without pulling <mutex>, <thread> or windows.h in: the
// MinGW "win32" thread model does not make those dependable from headers (rule 21a).
// The work is done in LauncherThreads.cpp over engine/src/Core/CoreThreads.hpp
// (Windows: critical sections, CreateThread and the Interlocked calls; elsewhere the
// standard library and the compiler's atomic builtins).
//
// The functions keep SDL2's behaviour where launcher code relied on it:
//  * mutexes are recursive (the owning thread may lock again);
//  * a null mutex or thread is ignored (lock, unlock, free, wait, detach do nothing);
//  * a detached thread frees its own bookkeeping when it ends;
//  * the atomic calls are full barriers and return the old value like SDL's.

namespace okay {
namespace lt {

struct Mutex;    // opaque: a recursive mutex
struct Thread;   // opaque: a started thread that has not been waited for or detached

/// A 32-bit integer for several threads (same shape as SDL_atomic_t: zero-initialised
/// as a static, read and written only through the functions below).
struct AtomicInt { int value; };

using ThreadFn = int (*)(void*);

Mutex* NewMutex();                 // nullptr only when out of memory
void   LockMutex(Mutex* m);
void   UnlockMutex(Mutex* m);
void   FreeMutex(Mutex* m);

/// Start fn(arg) on a new thread named `name`. nullptr if the system refused.
Thread* StartThread(ThreadFn fn, const char* name, void* arg);
/// Wait for the thread to end and free the handle. `status` (if not null) gets fn's result.
void    WaitThread(Thread* t, int* status);
/// Let the thread run on by itself and free the handle now.
void    DetachThread(Thread* t);

int  AtomicGet(AtomicInt* a);
int  AtomicSet(AtomicInt* a, int v);              // returns the old value
int  AtomicAdd(AtomicInt* a, int d);              // returns the old value
bool AtomicCAS(AtomicInt* a, int oldv, int newv); // true when it swapped
inline void AtomicIncRef(AtomicInt* a) { AtomicAdd(a, 1); }
/// True when the count reached zero.
inline bool AtomicDecRef(AtomicInt* a) { return AtomicAdd(a, -1) == 1; }

/// RAII lock (a null mutex is ignored).
class Lock {
public:
    explicit Lock(Mutex* m) : m_m(m) { LockMutex(m_m); }
    ~Lock() { UnlockMutex(m_m); }
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
private:
    Mutex* m_m;
};

} // namespace lt
} // namespace okay
