#include "okay/Core/Jobs.hpp"
#include "CoreThreads.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <deque>
#include <stdexcept>
#include <unordered_map>

namespace okay {

namespace jobs_detail {
struct JobState {
    std::function<void()> fn;
    std::atomic<int> status{(int)JobStatus::Pending};
    std::uint64_t systemId = 0;
    int priority = (int)JobPriority::Normal;
    bool mainThread = false;
    std::string name;
    // Guarded by the owning system's lock:
    std::vector<std::shared_ptr<JobState>> dependents;   // jobs waiting for this one
    int remaining = 0;                                    // unfinished dependencies
    bool queued = false;                                  // sits in a ready queue
    std::shared_ptr<JobState> workNext;                   // link while ResolveLocked walks a cascade (no allocation)
    // Written before the terminal status is stored (release), read after it is seen (acquire):
    std::exception_ptr error;
    std::string errorText;
};
} // namespace jobs_detail

using jobs_detail::JobState;
using StatePtr = std::shared_ptr<JobState>;

namespace {

using Clock = std::chrono::steady_clock;

std::atomic<std::uint64_t> g_nextSystemId{1};

// What the calling thread is running, for the self-wait and stall checks.
struct ThreadCtx {
    const void* sys = nullptr;
    int depth = 0;               // job bodies of `sys` on this thread's stack
    const JobState* job = nullptr;
};
thread_local ThreadCtx t_ctx;

JobStatus StatusOf(const JobState& s) { return (JobStatus)s.status.load(std::memory_order_acquire); }
bool Finished(const JobState& s) { return StatusOf(s) >= JobStatus::Completed; }

std::string Describe(const std::exception_ptr& e) {
    try { std::rethrow_exception(e); }
    catch (const std::exception& ex) { return ex.what(); }
    catch (...) { return "unknown exception"; }
}

} // namespace

// ---- JobHandle ----------------------------------------------------------------

JobStatus JobHandle::Status() const {
    return m_state ? StatusOf(*m_state) : JobStatus::Completed;
}
bool JobHandle::IsDone() const { return Status() >= JobStatus::Completed; }
std::string JobHandle::Error() const {
    return (m_state && Finished(*m_state)) ? m_state->errorText : std::string();
}
std::exception_ptr JobHandle::Exception() const {
    return (m_state && Finished(*m_state)) ? m_state->error : std::exception_ptr();
}

// ---- JobSystem::Impl ----------------------------------------------------------

struct JobSystem::Impl {
    enum class Mode { Running, Draining, Cancelling, Stopped };
    enum class Kind { Worker, Helper, Inline, Main };

    struct Outcome {
        bool failed = false;
        std::exception_ptr error;
        std::string text;
    };

    const std::uint64_t id = g_nextSystemId.fetch_add(1);
    corethreads::Mutex mu;
    corethreads::CondVar workCv;   // workers sleep here
    corethreads::CondVar doneCv;   // waiting threads sleep here

    // ---- guarded by mu ----
    Mode mode = Mode::Running;
    std::deque<StatePtr> ready[3];
    std::deque<StatePtr> mainQ;
    std::unordered_map<JobState*, StatePtr> blocked;   // waiting for dependencies
    std::vector<corethreads::Thread> threads;
    std::vector<std::function<void()>> graveyard;      // cancelled bodies, destroyed after unlocking
    ErrorHandler errorHandler;
    std::string lastError;
    std::uint64_t pending = 0;                          // submitted and not finished
    std::uint32_t queueDepth = 0, mainDepth = 0, peakDepth = 0;
    int liveWorkers = 0;
    int idleWorkers = 0;
    int waiters = 0;
    int running = 0;
    bool stopWorkers = false;
    bool drainOnDestroy = true;

    // ---- counters ----
    std::atomic<std::uintptr_t> mainTag{0};
    std::atomic<std::uint64_t> submitted{0}, completed{0}, failed{0}, cancelled{0};
    std::atomic<std::uint64_t> ranWorkers{0}, ranHelping{0}, ranInline{0}, ranMain{0};
    std::atomic<std::uint64_t> busyNs{0};

    Impl() { mainTag.store(corethreads::CurrentThreadTag()); }

    bool IsMain() const { return corethreads::CurrentThreadTag() == mainTag.load(std::memory_order_relaxed); }
    int SelfDepth() const { return t_ctx.sys == this ? t_ctx.depth : 0; }

    void UnlockFlush() {
        if (graveyard.empty()) { mu.Unlock(); return; }
        std::vector<std::function<void()>> dead;
        dead.swap(graveyard);
        mu.Unlock();
    }   // `dead` is destroyed here, outside the lock

    static void WorkerMain(void* p) { static_cast<Impl*>(p)->WorkerLoop(); }

    // Start up to n workers. Call with the lock held (or before anything else can see the system).
    void StartWorkers(int n) {
        if (!corethreads::kThreadsAvailable || n < 0) n = 0;
        if (n > 256) n = 256;
        stopWorkers = false;
        mode = Mode::Running;
        try { threads.reserve((std::size_t)n); } catch (...) { n = 0; }
        for (int i = 0; i < n; ++i) {
            threads.emplace_back();
            if (!threads.back().Start(&Impl::WorkerMain, this)) { threads.pop_back(); break; }
        }
        liveWorkers = (int)threads.size();
    }

    // ---- queues (lock held) ----

    // Throws (std::bad_alloc) without having changed anything.
    void PushReadyLocked(const StatePtr& s) {
        if (s->mainThread) {
            mainQ.push_back(s);
            s->queued = true;
            ++mainDepth;
            if (waiters > 0) doneCv.NotifyAll();
            return;
        }
        int p = s->priority < 0 ? 0 : (s->priority > 2 ? 2 : s->priority);
        ready[p].push_back(s);
        s->queued = true;
        ++queueDepth;
        if (queueDepth > peakDepth) peakDepth = queueDepth;
        if (idleWorkers > 0) workCv.NotifyOne();
        else if (waiters > 0) doneCv.NotifyAll();
    }

    StatePtr PopMainLocked() {
        while (!mainQ.empty()) {
            StatePtr s = std::move(mainQ.front());
            mainQ.pop_front();
            if (!s->queued) continue;   // cancelled while waiting
            s->queued = false;
            --mainDepth;
            s->status.store((int)JobStatus::Running, std::memory_order_release);
            ++running;
            return s;
        }
        return nullptr;
    }

    StatePtr PopReadyLocked(bool includeMain) {
        if (includeMain) {
            if (StatePtr m = PopMainLocked()) return m;
        }
        for (int p = 0; p < 3; ++p) {
            auto& q = ready[p];
            while (!q.empty()) {
                StatePtr s = std::move(q.front());
                q.pop_front();
                if (!s->queued) continue;
                s->queued = false;
                --queueDepth;
                s->status.store((int)JobStatus::Running, std::memory_order_release);
                ++running;
                return s;
            }
        }
        return nullptr;
    }

    // A Pending job becomes Cancelled. The caller set its error fields first.
    void MarkCancelledLocked(const StatePtr& s) {
        if (s->queued) {
            s->queued = false;
            if (s->mainThread) --mainDepth; else --queueDepth;
        } else {
            blocked.erase(s.get());
        }
        if (s->fn) {
            try { graveyard.push_back(std::move(s->fn)); } catch (...) {}
            s->fn = nullptr;   // (if the graveyard was full of memory trouble, destroyed here instead)
        }
        s->status.store((int)JobStatus::Cancelled, std::memory_order_release);
        --pending;
        cancelled.fetch_add(1, std::memory_order_relaxed);
    }

    // `root` just reached a terminal state: release or cancel the jobs that waited for it. Walks
    // the cascade with an intrusive stack and does not allocate, so a failure to find memory
    // cannot strand a job: one that cannot be queued is cancelled with the exception instead.
    void ResolveLocked(const StatePtr& root) {
        StatePtr stack = root;
        while (stack) {
            StatePtr j = std::move(stack);
            stack = std::move(j->workNext);
            j->workNext = nullptr;
            if (j->dependents.empty()) continue;
            std::vector<StatePtr> deps;
            deps.swap(j->dependents);
            const JobStatus st = StatusOf(*j);
            const bool bad = st == JobStatus::Failed || st == JobStatus::Cancelled;
            for (StatePtr& d : deps) {
                if (StatusOf(*d) != JobStatus::Pending) continue;   // cancelled meanwhile
                bool cancel = bad || mode == Mode::Cancelling;
                if (!cancel && --d->remaining == 0) {
                    blocked.erase(d.get());
                    try { PushReadyLocked(d); }
                    catch (...) { d->error = std::current_exception(); cancel = true; }
                    if (!cancel) continue;
                } else if (!cancel) {
                    continue;
                }
                if (bad && j->error) {
                    d->error = j->error;
                    try {
                        d->errorText = j->errorText.compare(0, 18, "dependency failed:") == 0
                                           ? j->errorText : "dependency failed: " + j->errorText;
                    } catch (...) {}
                }
                MarkCancelledLocked(d);
                d->workNext = std::move(stack);
                stack = d;
            }
        }
    }

    // ---- running jobs ----

    // Call without the lock. The job must already be Running.
    Outcome Execute(const StatePtr& job) {
        Outcome out;
        const ThreadCtx prev = t_ctx;
        t_ctx.sys = this;
        t_ctx.depth = (prev.sys == this ? prev.depth : 0) + 1;
        t_ctx.job = job.get();
        const auto t0 = Clock::now();
        try {
            job->fn();
        } catch (...) {
            out.failed = true;
            out.error = std::current_exception();
            try { out.text = Describe(out.error); } catch (...) {}
        }
        const auto dt = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count();
        busyNs.fetch_add((std::uint64_t)(dt > 0 ? dt : 0), std::memory_order_relaxed);
        t_ctx = prev;
        try { job->fn = nullptr; } catch (...) {}   // drop what the job captured right away
        if (out.failed) {
            ErrorHandler h;
            try { corethreads::LockGuard g(mu); h = errorHandler; } catch (...) {}
            if (h) { try { h(job->name, out.text); } catch (...) {} }
        }
        return out;
    }

    // Lock held. Records the result of Execute and releases the dependents.
    void FinishLocked(const StatePtr& job, Outcome& out, Kind kind) {
        --running;
        if (out.failed) {
            job->error = out.error;
            job->errorText = std::move(out.text);   // (a std::string move does not throw)
            job->status.store((int)JobStatus::Failed, std::memory_order_release);
            failed.fetch_add(1, std::memory_order_relaxed);
            try {
                lastError = (job->name.empty() ? std::string("job") : "job '" + job->name + "'") + ": " + job->errorText;
            } catch (...) {}
        } else {
            job->status.store((int)JobStatus::Completed, std::memory_order_release);
            completed.fetch_add(1, std::memory_order_relaxed);
        }
        if (job->mainThread) kind = Kind::Main;
        switch (kind) {
            case Kind::Worker: ranWorkers.fetch_add(1, std::memory_order_relaxed); break;
            case Kind::Helper: ranHelping.fetch_add(1, std::memory_order_relaxed); break;
            case Kind::Inline: ranInline.fetch_add(1, std::memory_order_relaxed); break;
            case Kind::Main:   ranMain.fetch_add(1, std::memory_order_relaxed); break;
        }
        --pending;
        ResolveLocked(job);
        if (waiters > 0) doneCv.NotifyAll();
    }

    // With no worker threads, run everything that is ready on this thread. Lock held.
    void DrainInlineLocked() {
        while (StatePtr j = PopReadyLocked(false)) {
            UnlockFlush();
            Outcome o = Execute(j);
            mu.Lock();
            FinishLocked(j, o, Kind::Inline);
        }
    }

    void WorkerLoop() {
        mu.Lock();
        for (;;) {
            if (StatePtr j = PopReadyLocked(false)) {
                UnlockFlush();
                Outcome o = Execute(j);
                mu.Lock();
                FinishLocked(j, o, Kind::Worker);
                continue;
            }
            if (stopWorkers) break;
            ++idleWorkers;
            workCv.Wait(mu);
            --idleWorkers;
        }
        UnlockFlush();
    }

    // Wake threads sleeping in HelpUntil (something they wait for changed without a job finishing).
    void Poke() {
        mu.Lock();
        if (waiters > 0) doneCv.NotifyAll();
        UnlockFlush();
    }

    // Run queued jobs on this thread until pred() (evaluated with the lock held) is true.
    // False on timeout (timeoutSec >= 0) or when nothing could ever run (see `stalled`).
    template <class Pred>
    bool HelpUntil(Pred pred, double timeoutSec, bool forceMain, bool* stalled) {
        const bool useMain = forceMain || IsMain();
        const bool timed = timeoutSec >= 0.0;
        const auto deadline = Clock::now() + std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double>(timed ? timeoutSec : 0.0));
        bool ok = false;
        mu.Lock();
        ++waiters;
        for (;;) {
            if (pred()) { ok = true; break; }
            if (StatePtr j = PopReadyLocked(useMain)) {
                UnlockFlush();
                Outcome o = Execute(j);
                mu.Lock();
                FinishLocked(j, o, Kind::Helper);
                continue;
            }
            if (liveWorkers == 0 && useMain && running <= SelfDepth()) {
                if (stalled) *stalled = true;
                break;
            }
            if (timed) {
                const double left = std::chrono::duration<double>(deadline - Clock::now()).count();
                if (left <= 0.0) break;
                doneCv.WaitFor(mu, left);
            } else {
                doneCv.Wait(mu);
            }
        }
        --waiters;
        UnlockFlush();
        return ok;
    }

    // Waits for one job (no rethrow). A handle from another system cannot wake us, so poll it.
    bool WaitDone(const StatePtr& st, double timeoutSec, bool* stalled) {
        auto pred = [&st] { return Finished(*st); };
        if (st->systemId == id) return HelpUntil(pred, timeoutSec, false, stalled);
        const auto end = Clock::now() + std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double>(timeoutSec >= 0.0 ? timeoutSec : 0.0));
        while (!Finished(*st)) {
            if (timeoutSec >= 0.0 && Clock::now() >= end) return false;
            HelpUntil(pred, 0.002, false, nullptr);
        }
        return true;
    }

    // Cancels everything that has not started, blocked jobs included. Does not allocate.
    std::size_t CancelPendingLocked() {
        const std::uint64_t before = cancelled.load();
        // Cancelling a job cancels its dependents (which are blocked, never queued), so the
        // queues are not modified while they are walked.
        for (auto& q : ready) {
            for (StatePtr& s : q) {
                if (!s->queued) continue;
                StatePtr keep = s;
                MarkCancelledLocked(keep);
                ResolveLocked(keep);
            }
            q.clear();
        }
        for (StatePtr& s : mainQ) {
            if (!s->queued) continue;
            StatePtr keep = s;
            MarkCancelledLocked(keep);
            ResolveLocked(keep);
        }
        mainQ.clear();
        while (!blocked.empty()) {
            StatePtr keep = blocked.begin()->second;
            MarkCancelledLocked(keep);   // removes it from `blocked`
            ResolveLocked(keep);
        }
        if (waiters > 0) doneCv.NotifyAll();
        return (std::size_t)(cancelled.load() - before);
    }

    void Shutdown(bool cancelPending) {
        if (t_ctx.sys == this && t_ctx.depth > 0)
            throw std::logic_error("JobSystem::Shutdown cannot be called from inside a job");
        mu.Lock();
        if (mode == Mode::Stopped) { UnlockFlush(); return; }
        if (mode != Mode::Running) {   // another thread is already shutting down: wait for it
            UnlockFlush();
            HelpUntil([this] { return mode == Mode::Stopped; }, -1.0, true, nullptr);
            return;
        }
        mode = cancelPending ? Mode::Cancelling : Mode::Draining;
        if (cancelPending) CancelPendingLocked();
        UnlockFlush();
        for (;;) {
            bool stalled = false;
            HelpUntil([this] { return pending == 0; }, -1.0, true, &stalled);
            mu.Lock();
            if (pending == 0) break;
            if (stalled) CancelPendingLocked();   // nothing can ever run them: do not spin
            UnlockFlush();
        }
        // Lock held, nothing left to run: stop the pool for good.
        mode = Mode::Stopped;
        stopWorkers = true;
        liveWorkers = 0;
        workCv.NotifyAll();
        std::vector<corethreads::Thread> old;
        old.swap(threads);
        UnlockFlush();
        for (corethreads::Thread& t : old) t.Join();
    }
};

// ---- JobSystem ----------------------------------------------------------------

int JobSystem::DefaultWorkerCount() {
    if (!corethreads::kThreadsAvailable) return 0;
    const unsigned hc = corethreads::HardwareThreads();
    if (hc <= 1) return 0;
    return (int)std::min(hc - 1, 31u);
}

bool JobSystem::ThreadsAvailable() { return corethreads::kThreadsAvailable; }

JobSystem::JobSystem(int workers) : m_impl(new Impl) {
    if (workers < 0) workers = DefaultWorkerCount();
    m_impl->mu.Lock();
    m_impl->StartWorkers(workers);
    m_impl->mu.Unlock();
}

JobSystem::~JobSystem() {
    try { m_impl->Shutdown(!m_impl->drainOnDestroy); } catch (...) {}
}

JobSystem& JobSystem::Global() {
    struct Holder {
        JobSystem js;
        Holder() : js(kAuto) { js.SetDrainOnDestroy(false); }
    };
    static Holder h;
    return h.js;
}

int JobSystem::WorkerCount() const {
    corethreads::LockGuard g(m_impl->mu);
    return m_impl->liveWorkers;
}

void JobSystem::Configure(int workers) {
    if (workers < 0) workers = DefaultWorkerCount();
    m_impl->Shutdown(false);
    m_impl->mu.Lock();
    m_impl->StartWorkers(workers);
    m_impl->mu.Unlock();
}

void JobSystem::SetDrainOnDestroy(bool drain) {
    corethreads::LockGuard g(m_impl->mu);
    m_impl->drainOnDestroy = drain;
}

void JobSystem::AdoptMainThread() { m_impl->mainTag.store(corethreads::CurrentThreadTag()); }
bool JobSystem::IsMainThread() const { return m_impl->IsMain(); }

void JobSystem::SetErrorHandler(ErrorHandler handler) {
    corethreads::LockGuard g(m_impl->mu);
    m_impl->errorHandler = std::move(handler);
}

// ---- submitting ---------------------------------------------------------------

namespace {
// If a dependency already failed or was cancelled (a terminal state never changes), the new job
// is cancelled with it and takes its exception. Sets `bad`; never throws.
template <class Peek>
void InheritFailure(JobState& s, const JobHandle* deps, std::size_t n, bool& bad, Peek peek) {
    for (std::size_t i = 0; i < n; ++i) {
        const JobState* d = peek(deps[i]);
        if (!d) continue;
        const JobStatus st = StatusOf(*d);
        if (st != JobStatus::Failed && st != JobStatus::Cancelled) continue;
        if (!bad && d->error) {
            s.error = d->error;
            try {
                s.errorText = d->errorText.compare(0, 18, "dependency failed:") == 0
                                  ? d->errorText : "dependency failed: " + d->errorText;
            } catch (...) {}
        }
        bad = true;
    }
}
} // namespace

JobHandle JobSystem::SubmitImpl(std::function<void()> fn, const JobHandle* deps, std::size_t depCount,
                                const JobOptions& options, bool mainThread) {
    Impl& I = *m_impl;
    auto peek = [](const JobHandle& h) -> const JobState* { return h.m_state.get(); };
    for (std::size_t i = 0; i < depCount; ++i) {
        const JobState* d = peek(deps[i]);
        if (d && d->systemId != I.id && !Finished(*d))
            throw std::invalid_argument("JobSystem::Submit: a dependency belongs to another JobSystem");
    }
    auto s = std::make_shared<JobState>();   // out of memory throws here, before anything has changed
    s->fn = fn ? std::move(fn) : std::function<void()>([] {});
    s->systemId = I.id;
    s->priority = (int)options.priority;
    s->mainThread = mainThread;
    s->name = options.name;
    bool bad = false;
    InheritFailure(*s, deps, depCount, bad, peek);

    I.mu.Lock();
    I.submitted.fetch_add(1, std::memory_order_relaxed);
    if (!bad) InheritFailure(*s, deps, depCount, bad, peek);   // one may have failed while we got the lock
    if (bad || I.mode == Impl::Mode::Cancelling) {
        s->fn = nullptr;
        s->status.store((int)JobStatus::Cancelled, std::memory_order_release);
        I.cancelled.fetch_add(1, std::memory_order_relaxed);
        I.UnlockFlush();
        return JobHandle(std::move(s));
    }
    ++I.pending;
    try {
        int remaining = 0;
        for (std::size_t i = 0; i < depCount; ++i) {
            const StatePtr& d = deps[i].m_state;
            if (!d || Finished(*d)) continue;
            d->dependents.push_back(s);
            ++remaining;
        }
        s->remaining = remaining;
        if (remaining == 0) I.PushReadyLocked(s);
        else I.blocked.emplace(s.get(), s);
    } catch (...) {
        // Out of memory while queueing: the job never runs, and Wait reports why. (Deps it was
        // already added to skip it when they finish, since it is no longer Pending.)
        s->error = std::current_exception();
        try { s->errorText = "could not queue the job: out of memory"; } catch (...) {}
        s->remaining = 0;
        I.MarkCancelledLocked(s);
        I.UnlockFlush();
        return JobHandle(std::move(s));
    }
    if (I.liveWorkers == 0) I.DrainInlineLocked();
    I.UnlockFlush();
    return JobHandle(std::move(s));
}

JobHandle JobSystem::Submit(std::function<void()> fn) {
    return SubmitImpl(std::move(fn), nullptr, 0, JobOptions(), false);
}
JobHandle JobSystem::Submit(std::function<void()> fn, std::initializer_list<JobHandle> deps) {
    return SubmitImpl(std::move(fn), deps.begin(), deps.size(), JobOptions(), false);
}
JobHandle JobSystem::Submit(std::function<void()> fn, const std::vector<JobHandle>& deps) {
    return SubmitImpl(std::move(fn), deps.data(), deps.size(), JobOptions(), false);
}
JobHandle JobSystem::Submit(std::function<void()> fn, const JobOptions& options, const std::vector<JobHandle>& deps) {
    return SubmitImpl(std::move(fn), deps.data(), deps.size(), options, false);
}
JobHandle JobSystem::RunOnMainThread(std::function<void()> fn) {
    return SubmitImpl(std::move(fn), nullptr, 0, JobOptions(), true);
}
JobHandle JobSystem::RunOnMainThread(std::function<void()> fn, std::initializer_list<JobHandle> deps) {
    return SubmitImpl(std::move(fn), deps.begin(), deps.size(), JobOptions(), true);
}
JobHandle JobSystem::RunOnMainThread(std::function<void()> fn, const std::vector<JobHandle>& deps) {
    return SubmitImpl(std::move(fn), deps.data(), deps.size(), JobOptions(), true);
}

std::size_t JobSystem::PumpMainThread(std::size_t maxJobs) {
    Impl& I = *m_impl;
    I.mu.Lock();
    std::size_t budget = I.mainDepth;
    if (maxJobs != 0 && maxJobs < budget) budget = maxJobs;
    std::size_t ran = 0;
    while (ran < budget) {
        StatePtr j = I.PopMainLocked();
        if (!j) break;
        I.UnlockFlush();
        Impl::Outcome o = I.Execute(j);
        I.mu.Lock();
        I.FinishLocked(j, o, Impl::Kind::Main);
        ++ran;
    }
    if (I.liveWorkers == 0) I.DrainInlineLocked();   // a main job may have released inline jobs
    I.UnlockFlush();
    return ran;
}

// ---- waiting ------------------------------------------------------------------

namespace {
void RethrowIfFailed(const JobState& s) {
    const JobStatus st = StatusOf(s);
    if ((st == JobStatus::Failed || st == JobStatus::Cancelled) && s.error)
        std::rethrow_exception(s.error);
}
} // namespace

void JobSystem::Wait(const JobHandle& job) {
    if (!job.m_state) return;
    if (t_ctx.job == job.m_state.get())
        throw std::logic_error("JobSystem::Wait: a job cannot wait for itself");
    bool stalled = false;
    m_impl->WaitDone(job.m_state, -1.0, &stalled);
    if (stalled && !Finished(*job.m_state))
        throw std::logic_error("JobSystem::Wait: the job can never run (no workers and nothing queued that could release it)");
    RethrowIfFailed(*job.m_state);
}

bool JobSystem::WaitFor(const JobHandle& job, double seconds) {
    if (!job.m_state) return true;
    if (seconds < 0.0) seconds = 0.0;
    return m_impl->WaitDone(job.m_state, seconds, nullptr);
}

void JobSystem::WaitAll(const std::vector<JobHandle>& jobs) {
    bool stalled = false;
    for (const JobHandle& j : jobs) {
        if (!j.m_state) continue;
        if (t_ctx.job == j.m_state.get())
            throw std::logic_error("JobSystem::WaitAll: a job cannot wait for itself");
        m_impl->WaitDone(j.m_state, -1.0, &stalled);
        if (stalled && !Finished(*j.m_state))
            throw std::logic_error("JobSystem::WaitAll: a job can never run");
    }
    for (const JobHandle& j : jobs)
        if (j.m_state) RethrowIfFailed(*j.m_state);
}

void JobSystem::WaitAll(std::initializer_list<JobHandle> jobs) {
    WaitAll(std::vector<JobHandle>(jobs));
}

bool JobSystem::WaitIdleFor(double seconds) {
    Impl& I = *m_impl;
    if (I.SelfDepth() > 0) throw std::logic_error("JobSystem::WaitIdle cannot be called from inside a job");
    return I.HelpUntil([&I] { return I.pending == 0; }, seconds < 0.0 ? 0.0 : seconds, false, nullptr);
}

void JobSystem::WaitIdle() {
    Impl& I = *m_impl;
    if (I.SelfDepth() > 0) throw std::logic_error("JobSystem::WaitIdle cannot be called from inside a job");
    bool stalled = false;
    I.HelpUntil([&I] { return I.pending == 0; }, -1.0, false, &stalled);
    if (stalled && I.pending != 0)
        throw std::logic_error("JobSystem::WaitIdle: the remaining jobs can never run");
}

// ---- cancelling and shutting down ---------------------------------------------

bool JobSystem::Cancel(const JobHandle& job) {
    if (!job.m_state) return false;
    Impl& I = *m_impl;
    bool ok = false;
    I.mu.Lock();
    if (job.m_state->systemId == I.id && StatusOf(*job.m_state) == JobStatus::Pending) {
        const StatePtr s = job.m_state;
        I.MarkCancelledLocked(s);
        I.ResolveLocked(s);   // whatever waited for it never runs either
        if (I.waiters > 0) I.doneCv.NotifyAll();
        ok = true;
    }
    I.UnlockFlush();
    return ok;
}

std::size_t JobSystem::CancelPending() {
    Impl& I = *m_impl;
    I.mu.Lock();
    const std::size_t n = I.CancelPendingLocked();
    I.UnlockFlush();
    return n;
}

void JobSystem::Shutdown(bool cancelPending) { m_impl->Shutdown(cancelPending); }

// ---- parallel loops -----------------------------------------------------------

std::size_t JobSystem::EffectiveGrain(std::size_t count, std::size_t grain) const {
    if (grain != 0) return grain;
    const std::size_t parts = ((std::size_t)WorkerCount() + 1) * 4;
    const std::size_t g = count / parts;
    return g < 1 ? 1 : g;
}

namespace {
struct ParallelState {
    std::atomic<std::size_t> next{0};
    std::atomic<std::size_t> completed{0};
    std::atomic<bool> failed{false};
    std::exception_ptr error;   // written once, by whoever wins `failed`
};
} // namespace

void JobSystem::ParallelForImpl(std::size_t begin, std::size_t end, std::size_t grain, RangeFn fn) {
    if (end <= begin) return;
    Impl& I = *m_impl;
    const std::size_t count = end - begin;
    const std::size_t g = EffectiveGrain(count, grain);
    const std::size_t chunks = count / g + (count % g != 0 ? 1 : 0);
    const int workers = WorkerCount();

    if (chunks == 1 || workers == 0) {   // serial, same chunk boundaries, in order
        for (std::size_t c = 0; c < chunks; ++c) {
            const std::size_t b = begin + c * g;
            const std::size_t e = (end - b) > g ? b + g : end;
            fn.call(fn.obj, b, e);
        }
        return;
    }

    auto st = std::make_shared<ParallelState>();
    Impl* impl = &I;
    // Claims chunks until none are left. `fn` (the caller's lambda) is only touched
    // while a chunk is claimed, and the caller waits for every claimed chunk, so a
    // helper that starts late sees no chunks and never uses it.
    auto runChunks = [impl, st, fn, begin, end, g, chunks]() {
        for (;;) {
            const std::size_t c = st->next.fetch_add(1, std::memory_order_relaxed);
            if (c >= chunks) break;
            if (!st->failed.load(std::memory_order_relaxed)) {
                const std::size_t b = begin + c * g;
                const std::size_t e = (end - b) > g ? b + g : end;
                try { fn.call(fn.obj, b, e); }
                catch (...) {
                    if (!st->failed.exchange(true)) st->error = std::current_exception();
                }
            }
            if (st->completed.fetch_add(1, std::memory_order_acq_rel) + 1 == chunks) impl->Poke();
        }
    };

    const std::size_t helpers = std::min<std::size_t>((std::size_t)workers, chunks - 1);
    std::vector<JobHandle> handles;
    JobOptions opt;
    opt.priority = JobPriority::High;
    for (std::size_t i = 0; i < helpers; ++i) {
        try { handles.push_back(SubmitImpl(runChunks, nullptr, 0, opt, false)); }
        catch (...) { break; }   // could not queue a helper: the caller just does more itself
    }
    runChunks();   // the calling thread works too
    I.HelpUntil([&st, chunks] { return st->completed.load(std::memory_order_acquire) >= chunks; },
                -1.0, false, nullptr);
    for (const JobHandle& h : handles) Cancel(h);   // helpers that never got a turn
    if (st->failed.load(std::memory_order_acquire)) {
        // Take the exception out of the shared state first: a helper may still hold `st` and
        // drop it on another thread, and that thread should not touch the exception's refcount.
        std::exception_ptr err = std::move(st->error);
        std::rethrow_exception(err);
    }
}

// ---- stats --------------------------------------------------------------------

JobStats JobSystem::Stats() const {
    Impl& I = *m_impl;
    JobStats s;
    {
        corethreads::LockGuard g(I.mu);
        s.workers = I.liveWorkers;
        s.queueDepth = I.queueDepth;
        s.peakQueueDepth = I.peakDepth;
        s.blocked = (std::uint32_t)I.blocked.size();
        s.running = (std::uint32_t)I.running;
        s.mainQueueDepth = I.mainDepth;
        s.lastError = I.lastError;
    }
    s.submitted = I.submitted.load(std::memory_order_relaxed);
    s.completed = I.completed.load(std::memory_order_relaxed);
    s.failed = I.failed.load(std::memory_order_relaxed);
    s.cancelled = I.cancelled.load(std::memory_order_relaxed);
    s.ranOnWorkers = I.ranWorkers.load(std::memory_order_relaxed);
    s.ranHelping = I.ranHelping.load(std::memory_order_relaxed);
    s.ranInline = I.ranInline.load(std::memory_order_relaxed);
    s.ranOnMainThread = I.ranMain.load(std::memory_order_relaxed);
    s.busyMs = (double)I.busyNs.load(std::memory_order_relaxed) / 1.0e6;
    return s;
}

void JobSystem::ResetStats() {
    Impl& I = *m_impl;
    I.mu.Lock();
    I.peakDepth = I.queueDepth;
    I.lastError.clear();
    I.mu.Unlock();
    I.submitted = 0; I.completed = 0; I.failed = 0; I.cancelled = 0;
    I.ranWorkers = 0; I.ranHelping = 0; I.ranInline = 0; I.ranMain = 0;
    I.busyNs = 0;
}

std::string FormatJobStats(const JobStats& s) {
    char buf[256];
    const std::uint64_t run = s.ranOnWorkers + s.ranHelping + s.ranInline + s.ranOnMainThread;
    std::snprintf(buf, sizeof buf,
                  "jobs: %d workers, %llu run (%llu helped, %llu main), queue %u/%u, %llu failed, %llu cancelled, busy %.1f ms",
                  s.workers, (unsigned long long)run, (unsigned long long)s.ranHelping,
                  (unsigned long long)s.ranOnMainThread, (unsigned)s.queueDepth, (unsigned)s.peakQueueDepth,
                  (unsigned long long)s.failed, (unsigned long long)s.cancelled, s.busyMs);
    return buf;
}

} // namespace okay
