// okay::sync implementation: Win32 primitives on Windows, pthreads elsewhere. See okay/Sync/Sync.hpp.
#include "okay/Sync/Sync.hpp"

#include <atomic>
#include <chrono>
#include <cstring>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <errno.h>
#  include <pthread.h>
#  include <time.h>
#  include <unistd.h>
#endif

namespace okay::sync {

// ---- Mutex ----------------------------------------------------------------------------------------------
#ifdef _WIN32
struct Mutex::Impl { CRITICAL_SECTION cs; };   // recursive by definition
Mutex::Mutex() : m_impl(new Impl) { InitializeCriticalSection(&m_impl->cs); }
Mutex::~Mutex() { DeleteCriticalSection(&m_impl->cs); delete m_impl; }
void Mutex::Lock() { EnterCriticalSection(&m_impl->cs); }
bool Mutex::TryLock() { return TryEnterCriticalSection(&m_impl->cs) != 0; }
void Mutex::Unlock() { LeaveCriticalSection(&m_impl->cs); }

struct Cond::Impl { CONDITION_VARIABLE cv; };
Cond::Cond() : m_impl(new Impl) { InitializeConditionVariable(&m_impl->cv); }
Cond::~Cond() { delete m_impl; }   // Windows condition variables need no destruction
void Cond::Wait(Mutex& m) { SleepConditionVariableCS(&m_impl->cv, &m.GetImpl()->cs, INFINITE); }
bool Cond::WaitFor(Mutex& m, std::uint32_t ms) {
    if (ms == INFINITE) ms = INFINITE - 1;   // INFINITE means "forever" to Windows
    if (SleepConditionVariableCS(&m_impl->cv, &m.GetImpl()->cs, ms)) return true;
    return GetLastError() != ERROR_TIMEOUT;
}
void Cond::Signal() { WakeConditionVariable(&m_impl->cv); }
void Cond::Broadcast() { WakeAllConditionVariable(&m_impl->cv); }

void SleepMs(std::uint32_t ms) { Sleep(ms); }
#else
struct Mutex::Impl { pthread_mutex_t mu; };
Mutex::Mutex() : m_impl(new Impl) {
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&m_impl->mu, &a);
    pthread_mutexattr_destroy(&a);
}
Mutex::~Mutex() { pthread_mutex_destroy(&m_impl->mu); delete m_impl; }
void Mutex::Lock() { pthread_mutex_lock(&m_impl->mu); }
bool Mutex::TryLock() { return pthread_mutex_trylock(&m_impl->mu) == 0; }
void Mutex::Unlock() { pthread_mutex_unlock(&m_impl->mu); }

struct Cond::Impl { pthread_cond_t cv; };
Cond::Cond() : m_impl(new Impl) {
    pthread_condattr_t a;
    pthread_condattr_init(&a);
    pthread_condattr_setclock(&a, CLOCK_MONOTONIC);   // a timeout must not follow the wall clock
    pthread_cond_init(&m_impl->cv, &a);
    pthread_condattr_destroy(&a);
}
Cond::~Cond() { pthread_cond_destroy(&m_impl->cv); delete m_impl; }
void Cond::Wait(Mutex& m) { pthread_cond_wait(&m_impl->cv, &m.GetImpl()->mu); }
bool Cond::WaitFor(Mutex& m, std::uint32_t ms) {
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    t.tv_sec += ms / 1000;
    t.tv_nsec += (long)(ms % 1000) * 1000000L;
    if (t.tv_nsec >= 1000000000L) { t.tv_sec += 1; t.tv_nsec -= 1000000000L; }
    return pthread_cond_timedwait(&m_impl->cv, &m.GetImpl()->mu, &t) != ETIMEDOUT;
}
void Cond::Signal() { pthread_cond_signal(&m_impl->cv); }
void Cond::Broadcast() { pthread_cond_broadcast(&m_impl->cv); }

void SleepMs(std::uint32_t ms) {
    timespec t;
    t.tv_sec = ms / 1000;
    t.tv_nsec = (long)(ms % 1000) * 1000000L;
    while (nanosleep(&t, &t) == -1 && errno == EINTR) {}
}
#endif

static std::uint64_t NowMs() {
    return (std::uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ---- Semaphore (the same on both systems, from a mutex, a condition and a count) ------------------------------
void Semaphore::Post() { LockGuard g(m_mu); ++m_count; m_cv.Signal(); }
void Semaphore::Wait() {
    LockGuard g(m_mu);
    while (m_count == 0) m_cv.Wait(m_mu);
    --m_count;
}
bool Semaphore::TryWait() {
    LockGuard g(m_mu);
    if (m_count == 0) return false;
    --m_count;
    return true;
}
bool Semaphore::WaitFor(std::uint32_t ms) {
    // The deadline is kept by counting the time left, so a spurious wake-up does not restart the full wait.
    LockGuard g(m_mu);
    while (m_count == 0) {
        const std::uint64_t t0 = NowMs();
        if (!m_cv.WaitFor(m_mu, ms)) { if (m_count == 0) return false; break; }
        const std::uint64_t spent = NowMs() - t0;
        if (spent >= ms) { if (m_count == 0) return false; break; }
        ms -= (std::uint32_t)spent;
    }
    --m_count;
    return true;
}
std::uint32_t Semaphore::Value() { LockGuard g(m_mu); return m_count; }

// ---- Thread ---------------------------------------------------------------------------------------------
// One context shared by the Thread object and the running thread; whoever lets go last deletes it.
struct Thread::Ctx {
    ThreadFn fn = nullptr;
    void* arg = nullptr;
    int ret = 0;
    std::atomic<int> refs{2};
#ifdef _WIN32
    HANDLE h = nullptr;
#else
    pthread_t t;
    char name[16] = {};
#endif
    void Release() { if (refs.fetch_sub(1) == 1) delete this; }
};

#ifdef _WIN32
static DWORD WINAPI ThreadMain(LPVOID p) {
    Thread::Ctx* c = static_cast<Thread::Ctx*>(p);
    c->ret = c->fn(c->arg);
    c->Release();
    return 0;
}
std::unique_ptr<Thread> Thread::Start(ThreadFn fn, const char*, void* arg) {
    if (!fn) return nullptr;
    Ctx* c = new Ctx;
    c->fn = fn; c->arg = arg;
    c->h = CreateThread(nullptr, 0, &ThreadMain, c, 0, nullptr);
    if (!c->h) { delete c; return nullptr; }
    return std::unique_ptr<Thread>(new Thread(c));
}
int Thread::Join() {
    if (!m_ctx) return 0;
    WaitForSingleObject(m_ctx->h, INFINITE);
    CloseHandle(m_ctx->h);
    const int r = m_ctx->ret;   // the thread has ended: its Release came before the handle was signalled
    m_ctx->Release();
    m_ctx = nullptr;
    return r;
}
void Thread::Detach() {
    if (!m_ctx) return;
    CloseHandle(m_ctx->h);
    m_ctx->Release();
    m_ctx = nullptr;
}
#else
static void* ThreadMain(void* p) {
    Thread::Ctx* c = static_cast<Thread::Ctx*>(p);
    if (c->name[0]) pthread_setname_np(pthread_self(), c->name);
    c->ret = c->fn(c->arg);
    c->Release();
    return nullptr;
}
std::unique_ptr<Thread> Thread::Start(ThreadFn fn, const char* name, void* arg) {
    if (!fn) return nullptr;
    Ctx* c = new Ctx;
    c->fn = fn; c->arg = arg;
    if (name) { std::strncpy(c->name, name, sizeof(c->name) - 1); }
    if (pthread_create(&c->t, nullptr, &ThreadMain, c) != 0) { delete c; return nullptr; }
    return std::unique_ptr<Thread>(new Thread(c));
}
int Thread::Join() {
    if (!m_ctx) return 0;
    pthread_join(m_ctx->t, nullptr);
    const int r = m_ctx->ret;
    m_ctx->Release();
    m_ctx = nullptr;
    return r;
}
void Thread::Detach() {
    if (!m_ctx) return;
    pthread_detach(m_ctx->t);
    m_ctx->Release();
    m_ctx = nullptr;
}
#endif
Thread::~Thread() { Detach(); }

} // namespace okay::sync
