#pragma once
// The SDK check without freezing the editor. The editor asks `Get()` every frame from its UI thread; the first call starts the probe on a worker
// thread and returns at once with a report whose `checking` flag is set. When the worker is done the next `Get()` hands the result over. A refresh
// request while a probe runs is ignored. Hand-over: the worker writes `result_` and then stores `kDone` with release order; the UI thread reads
// `kDone` with acquire order and only then touches `result_`. Nothing else is shared, so there is no lock.
// Threads (CLAUDE.md rule 21a): on Windows one native CreateThread, elsewhere (Linux tests) std::thread, which is included ONLY when _WIN32 is not
// defined, so the MinGW "win32" thread model never sees <thread>. No <mutex>, no condition_variable, no call_once.
// UNVERIFIED on Windows: the CreateThread branch was never run; the state machine is tested on Linux with a slow fake probe.
#include "XboxSdkCheck.hpp"
#include <atomic>
#include <functional>
#include <memory>
#include <utility>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <thread>
#endif

namespace okay::xbox {

#if defined(_WIN32)
namespace detail {
inline DWORD WINAPI DetachedMain(LPVOID p) { std::unique_ptr<std::function<void()>> f(static_cast<std::function<void()>*>(p)); (*f)(); return 0; }
}
#endif

/// Runs `fn` on a new detached thread.
inline void RunDetached(std::function<void()> fn) {
#if defined(_WIN32)
    auto* box = new std::function<void()>(std::move(fn));
    HANDLE h = CreateThread(nullptr, 0, detail::DetachedMain, box, 0, nullptr);
    if (h) CloseHandle(h); else { std::unique_ptr<std::function<void()>> f(box); (*f)(); }   // no thread could be made: run it here rather than never
#else
    std::thread(std::move(fn)).detach();
#endif
}

class AsyncSdkProbe {
public:
    using Probe = std::function<SdkInputs(bool fresh)>;   // runs on the worker thread; `fresh` = the person pressed Check again (skip caches)
    using Spawn = std::function<void(std::function<void()>)>;
    explicit AsyncSdkProbe(Probe probe, Spawn spawn = RunDetached) : probe_(std::move(probe)), spawn_(std::move(spawn)) {
        checking_.checking = true; checking_.available = false;
        checking_.message = "Checking your PC for the Xbox tools...";
    }
    AsyncSdkProbe(const AsyncSdkProbe&) = delete;
    AsyncSdkProbe& operator=(const AsyncSdkProbe&) = delete;
    // A running worker still points at this object: wait for it (the editor's instance is never destroyed, so this only matters in tests).
    ~AsyncSdkProbe() { while (state_.load(std::memory_order_acquire) == kRunning) { SpinPause(); } }

    /// UI thread only. Never blocks. While the probe runs the returned report has `checking == true`. `refresh` starts a new probe unless one runs.
    const SdkReport& Get(bool refresh = false) {
        int st = state_.load(std::memory_order_acquire);
        if (st == kDone) { report_ = std::move(result_); have_ = true; state_.store(kIdle, std::memory_order_release); st = kIdle; }
        if (st == kRunning) return checking_;                       // a refresh while checking is ignored
        if (refresh || !have_) { Start(refresh); return checking_; }
        return report_;
    }
    bool Checking() const { return state_.load(std::memory_order_acquire) == kRunning; }
    bool HaveResult() const { return have_; }
    int Probes() const { return started_; }   // how many probes were started (tests)

private:
    enum { kIdle = 0, kRunning = 1, kDone = 2 };
    static void SpinPause() {
#if defined(_WIN32)
        SwitchToThread();
#else
        std::this_thread::yield();
#endif
    }
    void Start(bool fresh) {
        state_.store(kRunning, std::memory_order_release);
        ++started_;
        spawn_([this, fresh]() {
            SdkReport r;
            try { r = EvaluateSdk(probe_(fresh)); }
            catch (...) { r.available = false; r.message = "The check of this PC failed unexpectedly. Press Check again; if it keeps failing, send us the editor log."; }
            r.checking = false;
            result_ = std::move(r);
            state_.store(kDone, std::memory_order_release);          // last thing the worker does with this object
        });
    }
    Probe probe_; Spawn spawn_;
    std::atomic<int> state_{kIdle};
    SdkReport result_;      // written by the worker before kDone, read by the UI thread after
    SdkReport report_;      // UI thread only
    SdkReport checking_;    // constant
    bool have_ = false;
    int started_ = 0;
};

} // namespace okay::xbox
