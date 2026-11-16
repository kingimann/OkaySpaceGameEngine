#pragma once
// Private threading shim for the Core runtime (Jobs.cpp, StringId.cpp) and the launcher
// (launcher/LauncherThreads.cpp, which gives launcher headers opaque handles over it).
// Include it from .cpp files only, never from a public header: the MinGW CI job
// uses the "win32" thread model, where <mutex>, <thread> and <condition_variable>
// are not dependable from headers. On Windows this wraps the native primitives
// (the same pattern DedicatedServer.cpp uses); elsewhere it wraps the standard
// ones. Under Emscripten without pthreads no thread can be started, so the job
// system falls back to running everything inline.
#include <cstddef>
#include <cstdint>
#include <new>

#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
#  define OKAY_CORE_THREADS 0
#elif defined(OKAY_FORCE_NO_THREADS)
#  define OKAY_CORE_THREADS 0
#else
#  define OKAY_CORE_THREADS 1
#endif

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <chrono>
#  include <condition_variable>
#  include <mutex>
#  include <thread>
#  if defined(__linux__) || defined(__APPLE__)
#    include <pthread.h>
#  endif
#endif

namespace okay {
namespace corethreads {

/// True where worker threads can be started at all.
constexpr bool kThreadsAvailable = OKAY_CORE_THREADS != 0;

/// A number that is unique to the calling thread while it lives (the address of a
/// thread-local), usable to ask "am I the thread that did X".
inline std::uintptr_t CurrentThreadTag() {
    static thread_local char tag;
    return reinterpret_cast<std::uintptr_t>(&tag);
}

inline unsigned HardwareThreads() {
#if !OKAY_CORE_THREADS
    return 0;
#elif defined(_WIN32)
    SYSTEM_INFO si; GetSystemInfo(&si);
    return si.dwNumberOfProcessors;
#else
    return std::thread::hardware_concurrency();
#endif
}

/// Sleep the calling thread (the OS may round up). Not for use in a WebGL build.
inline void SleepSeconds(double seconds) {
    if (!(seconds > 0.0)) return;
#if defined(_WIN32)
    Sleep(static_cast<DWORD>(seconds * 1000.0));
#elif OKAY_CORE_THREADS
    std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
#else
    (void)seconds;
#endif
}

/// Let other threads run for a moment.
inline void YieldThread() {
#if defined(_WIN32)
    SwitchToThread();
#elif OKAY_CORE_THREADS
    std::this_thread::yield();
#endif
}

/// Give the calling thread a name that debuggers and profilers show. Best effort: does
/// nothing where the system has no way to do it. Linux keeps at most 15 bytes of it.
inline void SetCurrentThreadName(const char* name) {
    if (!name || !*name) return;
#if defined(_WIN32)
    // SetThreadDescription exists from Windows 10 1607 on; look it up so older systems still start.
    typedef HRESULT (WINAPI *SetDescFn)(HANDLE, PCWSTR);
    const HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    if (!k32) return;
    const FARPROC p = GetProcAddress(k32, "SetThreadDescription");
    if (!p) return;
    const SetDescFn setDesc = reinterpret_cast<SetDescFn>(reinterpret_cast<void (*)()>(p));
    wchar_t wide[64];
    if (MultiByteToWideChar(CP_UTF8, 0, name, -1, wide, 64) <= 0) return;
    wide[63] = 0;
    setDesc(GetCurrentThread(), wide);
#elif OKAY_CORE_THREADS && defined(__linux__)
    char shortName[16];
    std::size_t n = 0;
    while (n < 15 && name[n]) { shortName[n] = name[n]; ++n; }
    shortName[n] = 0;
    pthread_setname_np(pthread_self(), shortName);
#elif OKAY_CORE_THREADS && defined(__APPLE__)
    pthread_setname_np(name);
#endif
}

// ---- Atomic 32-bit integers ------------------------------------------------------------
// Free functions on a plain int slot, so a struct can hold one with no special type. Every
// operation is sequentially consistent (a full barrier), like the interlocked calls.
#if defined(_WIN32)
static_assert(sizeof(LONG) == sizeof(int), "the interlocked calls work on 32-bit LONG");
inline int AtomicLoad(const int* p)          { return (int)InterlockedCompareExchange((volatile LONG*)p, 0, 0); }
/// Store `v`; returns the value it replaced.
inline int AtomicExchange(int* p, int v)     { return (int)InterlockedExchange((volatile LONG*)p, (LONG)v); }
/// Add `d`; returns the value before the add.
inline int AtomicFetchAdd(int* p, int d)     { return (int)InterlockedExchangeAdd((volatile LONG*)p, (LONG)d); }
/// If *p == expected, store `desired` and return true; otherwise leave it and return false.
inline bool AtomicCompareExchange(int* p, int expected, int desired) {
    return (int)InterlockedCompareExchange((volatile LONG*)p, (LONG)desired, (LONG)expected) == expected;
}
#else
inline int AtomicLoad(const int* p)          { return __atomic_load_n(p, __ATOMIC_SEQ_CST); }
inline int AtomicExchange(int* p, int v)     { return __atomic_exchange_n(p, v, __ATOMIC_SEQ_CST); }
inline int AtomicFetchAdd(int* p, int d)     { return __atomic_fetch_add(p, d, __ATOMIC_SEQ_CST); }
inline bool AtomicCompareExchange(int* p, int expected, int desired) {
    return __atomic_compare_exchange_n(p, &expected, desired, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
#endif

#if defined(_WIN32)

class Mutex {
public:
    Mutex()  { InitializeCriticalSection(&m_cs); }
    ~Mutex() { DeleteCriticalSection(&m_cs); }
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;
    void Lock()   { EnterCriticalSection(&m_cs); }
    void Unlock() { LeaveCriticalSection(&m_cs); }
private:
    friend class CondVar;
    CRITICAL_SECTION m_cs;
};

/// A mutex the owning thread may lock again (it must unlock as many times). A critical
/// section is already recursive.
class RecursiveMutex {
public:
    RecursiveMutex()  { InitializeCriticalSection(&m_cs); }
    ~RecursiveMutex() { DeleteCriticalSection(&m_cs); }
    RecursiveMutex(const RecursiveMutex&) = delete;
    RecursiveMutex& operator=(const RecursiveMutex&) = delete;
    void Lock()   { EnterCriticalSection(&m_cs); }
    void Unlock() { LeaveCriticalSection(&m_cs); }
private:
    CRITICAL_SECTION m_cs;
};

class CondVar {
public:
    CondVar() { InitializeConditionVariable(&m_cv); }
    CondVar(const CondVar&) = delete;
    CondVar& operator=(const CondVar&) = delete;
    /// Release `m` and sleep until notified (or spuriously woken); `m` is held again on return.
    void Wait(Mutex& m) { SleepConditionVariableCS(&m_cv, &m.m_cs, INFINITE); }
    /// Same, with a timeout. False when the time ran out.
    bool WaitFor(Mutex& m, double seconds) {
        double ms = seconds * 1000.0;
        if (ms < 0.0) ms = 0.0;
        if (ms > 4.0e9) ms = 4.0e9;
        return SleepConditionVariableCS(&m_cv, &m.m_cs, (DWORD)ms) != 0;
    }
    void NotifyOne() { WakeConditionVariable(&m_cv); }
    void NotifyAll() { WakeAllConditionVariable(&m_cv); }
private:
    CONDITION_VARIABLE m_cv;
};

class Thread {
public:
    Thread() = default;
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;
    Thread(Thread&& o) noexcept : m_h(o.m_h) { o.m_h = nullptr; }
    ~Thread() { Join(); }
    /// Start `fn(arg)` on a new thread, optionally named (see SetCurrentThreadName). False if
    /// the system refused. A Thread that is still running must be joined or detached first.
    bool Start(void (*fn)(void*), void* arg, const char* name = nullptr) {
        if (m_h) return false;
        Packet* p = new (std::nothrow) Packet{fn, arg, {0}};
        if (!p) return false;
        if (name) {
            std::size_t n = 0;
            while (n + 1 < sizeof(p->name) && name[n]) { p->name[n] = name[n]; ++n; }
            p->name[n] = 0;
        }
        m_h = CreateThread(nullptr, 0, &Entry, p, 0, nullptr);
        if (!m_h) { delete p; return false; }
        return true;
    }
    /// True while this object owns a started thread (not yet joined or detached).
    bool Joinable() const { return m_h != nullptr; }
    void Join() { if (m_h) { WaitForSingleObject(m_h, INFINITE); CloseHandle(m_h); m_h = nullptr; } }
    /// Let the thread run on by itself: it cleans up when it ends, and cannot be joined.
    void Detach() { if (m_h) { CloseHandle(m_h); m_h = nullptr; } }
private:
    struct Packet { void (*fn)(void*); void* arg; char name[64]; };
    static DWORD WINAPI Entry(LPVOID p) {
        const Packet pk = *static_cast<Packet*>(p);
        delete static_cast<Packet*>(p);
        if (pk.name[0]) SetCurrentThreadName(pk.name);
        pk.fn(pk.arg);
        return 0;
    }
    HANDLE m_h = nullptr;
};

#else   // standard primitives

class Mutex {
public:
    Mutex() = default;
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;
    void Lock()   { m_mu.lock(); }
    void Unlock() { m_mu.unlock(); }
private:
    friend class CondVar;
    std::mutex m_mu;
};

/// A mutex the owning thread may lock again (it must unlock as many times).
class RecursiveMutex {
public:
    RecursiveMutex() = default;
    RecursiveMutex(const RecursiveMutex&) = delete;
    RecursiveMutex& operator=(const RecursiveMutex&) = delete;
    void Lock()   { m_mu.lock(); }
    void Unlock() { m_mu.unlock(); }
private:
    std::recursive_mutex m_mu;   // lint-allow: win32-thread (the non-Windows branch of #if defined(_WIN32))
};

class CondVar {
public:
    CondVar() = default;
    CondVar(const CondVar&) = delete;
    CondVar& operator=(const CondVar&) = delete;
    void Wait(Mutex& m) {
        std::unique_lock<std::mutex> lk(m.m_mu, std::adopt_lock);
        m_cv.wait(lk);
        lk.release();
    }
    bool WaitFor(Mutex& m, double seconds) {
        if (seconds < 0.0) seconds = 0.0;
        std::unique_lock<std::mutex> lk(m.m_mu, std::adopt_lock);
        const bool ok = m_cv.wait_for(lk, std::chrono::duration<double>(seconds)) == std::cv_status::no_timeout;
        lk.release();
        return ok;
    }
    void NotifyOne() { m_cv.notify_one(); }
    void NotifyAll() { m_cv.notify_all(); }
private:
    std::condition_variable m_cv;
};

class Thread {
public:
    Thread() = default;
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;
    Thread(Thread&&) noexcept = default;
    ~Thread() { Join(); }
    bool Start(void (*fn)(void*), void* arg, const char* name = nullptr) {
#if OKAY_CORE_THREADS
        if (m_t.joinable()) return false;
        struct Named { char text[64]; } nm{{0}};
        if (name) {
            std::size_t n = 0;
            while (n + 1 < sizeof(nm.text) && name[n]) { nm.text[n] = name[n]; ++n; }
            nm.text[n] = 0;
        }
        try {
            m_t = std::thread([fn, arg, nm] {   // lint-allow: win32-thread (the non-Windows branch)
                if (nm.text[0]) SetCurrentThreadName(nm.text);
                fn(arg);
            });
            return true;
        }
        catch (...) { return false; }
#else
        (void)fn; (void)arg; (void)name;
        return false;
#endif
    }
    bool Joinable() const { return m_t.joinable(); }
    void Join() { if (m_t.joinable()) m_t.join(); }
    void Detach() { if (m_t.joinable()) m_t.detach(); }
private:
    std::thread m_t;
};

#endif

/// An int that several threads read and write (all operations are full barriers).
class AtomicInt {
public:
    explicit AtomicInt(int v = 0) : m_v(v) {}
    AtomicInt(const AtomicInt&) = delete;
    AtomicInt& operator=(const AtomicInt&) = delete;
    int  Load() const                         { return AtomicLoad(&m_v); }
    int  Exchange(int v)                      { return AtomicExchange(&m_v, v); }
    int  FetchAdd(int d)                      { return AtomicFetchAdd(&m_v, d); }
    bool CompareExchange(int expected, int v) { return AtomicCompareExchange(&m_v, expected, v); }
private:
    int m_v;
};

/// RAII lock for the Mutex above.
class LockGuard {
public:
    explicit LockGuard(Mutex& m) : m_m(m) { m_m.Lock(); }
    ~LockGuard() { m_m.Unlock(); }
    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;
private:
    Mutex& m_m;
};

} // namespace corethreads
} // namespace okay
