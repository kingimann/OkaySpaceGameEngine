#pragma once
// okay::sync: our own threads, mutexes, condition variables and semaphores, the replacement for the
// SDL_CreateThread / SDL_mutex / SDL_cond / SDL_sem family the launcher uses (docs/sync.md).
//
// Why not <thread> and <mutex>: the Windows programs are built with MinGW's "win32" thread model, which
// cannot compile them (CLAUDE.md rule 21a). So Windows uses CreateThread, CRITICAL_SECTION and
// CONDITION_VARIABLE, Linux uses pthreads. This header includes no system header of either.
//
// Written from the Win32 and POSIX threads documentation. Nothing copied from SDL or any other project.
// Atomics are not wrapped: std::atomic needs no thread library and works with the win32 thread model.
// Thread-local storage: use the thread_local keyword.
#include <cstdint>
#include <memory>

namespace okay::sync {

/// A mutex that can be locked again by the thread that holds it (as SDL's is), so code moved over from
/// SDL_mutex keeps working. Every Lock needs its own Unlock. Cond::Wait on a mutex locked more than once
/// does not release it fully: lock it once around a wait.
class Mutex {
public:
    Mutex();
    ~Mutex();
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;
    void Lock();
    bool TryLock();
    void Unlock();
    struct Impl;
    Impl* GetImpl() { return m_impl; }
private:
    Impl* m_impl;
};

class LockGuard {
public:
    explicit LockGuard(Mutex& m) : m_m(m) { m_m.Lock(); }
    ~LockGuard() { m_m.Unlock(); }
    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;
private:
    Mutex& m_m;
};

/// Condition variable. Wait / WaitFor must be called with `m` locked; they unlock it while waiting and
/// lock it again before returning. Wake-ups can be spurious: always wait in a loop on a predicate.
class Cond {
public:
    Cond();
    ~Cond();
    Cond(const Cond&) = delete;
    Cond& operator=(const Cond&) = delete;
    void Wait(Mutex& m);
    /// False when `ms` passed without a wake-up (the mutex is held again either way).
    bool WaitFor(Mutex& m, std::uint32_t ms);
    void Signal();
    void Broadcast();
    struct Impl;
private:
    Impl* m_impl;
};

/// Counting semaphore.
class Semaphore {
public:
    explicit Semaphore(std::uint32_t initial = 0) : m_count(initial) {}
    void Post();
    void Wait();
    bool TryWait();
    bool WaitFor(std::uint32_t ms);
    std::uint32_t Value();
private:
    Mutex m_mu;
    Cond m_cv;
    std::uint32_t m_count;
};

using ThreadFn = int (*)(void*);

/// A running thread. Start it, then Join (get the function's return value) or Detach (let it finish by
/// itself, nobody waits). Destroying a Thread that was neither joined nor detached detaches it.
class Thread {
public:
    /// Null when the system could not start a thread. `name` (may be null) is shown by debuggers where supported.
    static std::unique_ptr<Thread> Start(ThreadFn fn, const char* name, void* arg);
    ~Thread();
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;
    /// Waits for the thread to end and returns what its function returned. Once only.
    int Join();
    void Detach();
    bool Joinable() const { return m_ctx != nullptr; }
    struct Ctx;
private:
    explicit Thread(Ctx* c) : m_ctx(c) {}
    Ctx* m_ctx;
};

/// Milliseconds the calling thread sleeps.
void SleepMs(std::uint32_t ms);

} // namespace okay::sync
