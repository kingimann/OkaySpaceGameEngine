#pragma once
// Private threading shim for the Core runtime (Jobs.cpp, StringId.cpp, CVar.cpp).
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
    /// Start `fn(arg)` on a new thread. False if the system refused.
    bool Start(void (*fn)(void*), void* arg) {
        Packet* p = new (std::nothrow) Packet{fn, arg};
        if (!p) return false;
        m_h = CreateThread(nullptr, 0, &Entry, p, 0, nullptr);
        if (!m_h) { delete p; return false; }
        return true;
    }
    void Join() { if (m_h) { WaitForSingleObject(m_h, INFINITE); CloseHandle(m_h); m_h = nullptr; } }
private:
    struct Packet { void (*fn)(void*); void* arg; };
    static DWORD WINAPI Entry(LPVOID p) {
        const Packet pk = *static_cast<Packet*>(p);
        delete static_cast<Packet*>(p);
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
    bool Start(void (*fn)(void*), void* arg) {
#if OKAY_CORE_THREADS
        try { m_t = std::thread([fn, arg] { fn(arg); }); return true; }
        catch (...) { return false; }
#else
        (void)fn; (void)arg;
        return false;
#endif
    }
    void Join() { if (m_t.joinable()) m_t.join(); }
private:
    std::thread m_t;
};

#endif

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
