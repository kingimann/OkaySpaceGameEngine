#pragma once
// A small job system: a worker pool, a task graph, ParallelFor / ParallelReduce, a
// main-thread queue, cancellation and counters. See docs/core_runtime.md.
//
// This header exposes plain C++ types only (atomics, std::function, shared_ptr).
// The locks and threads live in Jobs.cpp behind a platform shim, because the
// MinGW build uses the "win32" thread model, where <mutex> / <thread> must not
// be included from a header.
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace okay {

/// Which queue a job waits in. Higher priorities start first; within one
/// priority jobs start in submission order.
enum class JobPriority : int { High = 0, Normal = 1, Low = 2 };

/// Where a job is in its life.
enum class JobStatus : int {
    Pending = 0,     // waiting for its dependencies, or queued and not started
    Running = 1,
    Completed = 2,   // ran to the end
    Failed = 3,      // threw; the exception is kept for Wait
    Cancelled = 4,   // never ran (cancelled, or a dependency failed or was cancelled)
};

namespace jobs_detail { struct JobState; }

/// A reference to a submitted job. Cheap to copy. An empty handle counts as
/// already completed, so it is a harmless dependency.
class JobHandle {
public:
    JobHandle() = default;

    /// True when the handle refers to a job (false for a default-constructed one).
    bool Valid() const { return m_state != nullptr; }
    explicit operator bool() const { return Valid(); }

    JobStatus Status() const;
    /// Completed, Failed or Cancelled: the job will not run or change any more.
    bool IsDone() const;
    bool Completed() const { return Status() == JobStatus::Completed; }
    bool Failed() const { return Status() == JobStatus::Failed; }
    bool Cancelled() const { return Status() == JobStatus::Cancelled; }

    /// The exception message of a failed job. For a job cancelled because a
    /// dependency failed this is "dependency failed: <the message>".
    std::string Error() const;
    /// The captured exception (null when there is none).
    std::exception_ptr Exception() const;

private:
    friend class JobSystem;
    explicit JobHandle(std::shared_ptr<jobs_detail::JobState> s) : m_state(std::move(s)) {}
    std::shared_ptr<jobs_detail::JobState> m_state;
};

/// Optional settings for one job.
struct JobOptions {
    JobPriority priority = JobPriority::Normal;
    /// Shown in error reports. Copied, so it may be a temporary.
    std::string name;
};

/// A snapshot of the counters, for the Profiler window or a debug overlay. The
/// job system never touches the Profiler itself; copy these numbers where you
/// want them.
struct JobStats {
    int workers = 0;                     // pool threads; 0 = everything runs inline
    std::uint64_t submitted = 0;         // jobs ever submitted (main-thread jobs included)
    std::uint64_t completed = 0;         // ran to the end
    std::uint64_t failed = 0;            // threw
    std::uint64_t cancelled = 0;         // never ran
    std::uint64_t ranOnWorkers = 0;      // run by a pool thread
    std::uint64_t ranHelping = 0;        // run by a thread that was waiting (Wait / ParallelFor)
    std::uint64_t ranInline = 0;         // run on the submitting thread (0 workers, or after shutdown)
    std::uint64_t ranOnMainThread = 0;   // run by PumpMainThread
    std::uint32_t queueDepth = 0;        // ready to run, not yet started (not counting the main-thread queue)
    std::uint32_t peakQueueDepth = 0;
    std::uint32_t blocked = 0;           // waiting for a dependency
    std::uint32_t running = 0;           // job bodies executing right now
    std::uint32_t mainQueueDepth = 0;    // main-thread jobs not yet pumped
    double busyMs = 0.0;                 // time spent inside job bodies, summed over all threads
    std::string lastError;               // the last failure's job name and message
};

/// Text for a debug overlay, e.g. "jobs: 3 workers, 120 run (12 helped), queue 0/9, busy 41.2 ms".
std::string FormatJobStats(const JobStats& s);

class JobSystem {
public:
    static constexpr int kAuto = -1;

    /// `workers` pool threads: kAuto picks from the CPU count (0 where threads do not
    /// exist, as in a WebGL build), 0 runs everything inline on the calling thread
    /// (deterministic; Submit finishes the job before it returns). The thread that
    /// creates the system is its "main thread" (see PumpMainThread).
    explicit JobSystem(int workers = kAuto);
    /// Drains the queue (or cancels what has not started, see SetDrainOnDestroy), then stops the workers.
    ~JobSystem();
    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    /// A shared system for the whole process, created on first use with kAuto workers.
    /// Its destructor at exit cancels what has not started instead of draining it.
    static JobSystem& Global();

    /// The pool size kAuto resolves to on this machine.
    static int DefaultWorkerCount();
    /// False where no thread can ever be started (Emscripten without pthreads).
    static bool ThreadsAvailable();

    int WorkerCount() const;
    /// Replace the pool: drains what is queued, stops the old workers, starts `workers` new ones.
    /// Do not call it from inside a job.
    void Configure(int workers);

    /// Drain on destruction (default) or cancel what has not started.
    void SetDrainOnDestroy(bool drain);

    // ---- submitting -------------------------------------------------------

    /// Run `fn` on a worker (or inline with 0 workers). The handle is how to wait
    /// for it, cancel it, or make another job depend on it.
    JobHandle Submit(std::function<void()> fn);
    /// Same, but it starts only after every job in `deps` has completed. A dependency
    /// that failed or was cancelled cancels this job too. Dependencies must come from
    /// this same system (a pending one from another throws std::invalid_argument).
    JobHandle Submit(std::function<void()> fn, std::initializer_list<JobHandle> deps);
    JobHandle Submit(std::function<void()> fn, const std::vector<JobHandle>& deps);
    JobHandle Submit(std::function<void()> fn, const JobOptions& options, const std::vector<JobHandle>& deps = {});

    /// Queue `fn` for the main thread: it runs inside PumpMainThread, never earlier,
    /// even when called from the main thread itself (so the order is always the same).
    JobHandle RunOnMainThread(std::function<void()> fn);
    JobHandle RunOnMainThread(std::function<void()> fn, std::initializer_list<JobHandle> deps);
    JobHandle RunOnMainThread(std::function<void()> fn, const std::vector<JobHandle>& deps);

    /// Run the queued main-thread jobs (call it once a frame from the game loop).
    /// Runs at most `maxJobs` (0 = all of them), and only the ones queued when the
    /// call began, so a job that posts another cannot keep a frame busy forever.
    /// A job that throws is recorded and does not stop the rest. Returns how many ran.
    std::size_t PumpMainThread(std::size_t maxJobs = 0);
    /// Make the calling thread the main thread (the one that creates the system is by default).
    void AdoptMainThread();
    bool IsMainThread() const;

    // ---- waiting ----------------------------------------------------------

    /// Wait until the job finishes. The waiting thread runs other queued jobs in the
    /// meantime, so a job may wait on another without starving the pool; the main
    /// thread also runs main-thread jobs. Rethrows the job's exception if it failed
    /// (every time it is called). A cancelled job returns normally, unless it was
    /// cancelled because a dependency threw: then that exception is rethrown.
    /// Throws std::logic_error when a job waits for itself, or when nothing could ever
    /// run it (0 workers and nothing queued).
    void Wait(const JobHandle& job);
    /// Wait with a time limit; false if it ran out. Does not rethrow.
    bool WaitFor(const JobHandle& job, double seconds);
    /// Wait for all of them, then rethrow the first exception, if any.
    void WaitAll(const std::vector<JobHandle>& jobs);
    void WaitAll(std::initializer_list<JobHandle> jobs);
    /// Wait until every submitted job has finished (main-thread ones included when
    /// called from the main thread). Do not call it from inside a job.
    void WaitIdle();
    bool WaitIdleFor(double seconds);

    // ---- cancelling and shutting down ------------------------------------

    /// Cancel a job that has not started, together with everything that depends on
    /// it. False if it already started or finished.
    bool Cancel(const JobHandle& job);
    /// Cancel every job that has not started. Returns how many.
    std::size_t CancelPending();
    /// Finish (or cancel, with `cancelPending`) the queued jobs, run the main-thread
    /// queue, stop and join the workers. Afterwards the system runs everything inline.
    /// Safe to call twice. Do not call it from inside a job.
    void Shutdown(bool cancelPending = false);

    // ---- parallel loops ---------------------------------------------------

    /// Run `fn` over [begin, end) in chunks of `grain` indices across the pool; the
    /// calling thread works too, and the call returns when every index is done.
    /// `fn(i)` is called per index, or `fn(chunkBegin, chunkEnd)` per chunk (pick by
    /// the signature). `grain` 0 chooses a size from the range and the pool. With 0
    /// workers it runs serially in order, with the same chunk boundaries. If `fn`
    /// throws, the chunks not yet started are skipped, the rest finish, and the first
    /// exception is rethrown. Chunks run in no particular order and at the same time:
    /// `fn` must only write to disjoint data.
    template <class F>
    void ParallelFor(std::size_t begin, std::size_t end, std::size_t grain, F&& fn);

    /// Fold [begin, end) into one value: `map(i)` (or `map(chunkBegin, chunkEnd)`)
    /// gives a value, `reduce(a, b)` combines two. Chunks are combined in index order
    /// whatever order they ran in, so the result does not depend on scheduling (an
    /// associative `reduce` is enough; it need not be commutative).
    template <class T, class Map, class Reduce>
    T ParallelReduce(std::size_t begin, std::size_t end, std::size_t grain, T identity, Map&& map, Reduce&& reduce);

    /// The chunk size ParallelFor uses for a range of `count` indices and a requested `grain`.
    std::size_t EffectiveGrain(std::size_t count, std::size_t grain) const;

    // ---- stats and errors -------------------------------------------------

    JobStats Stats() const;
    /// Zero the counters (the queue depths are live values and stay).
    void ResetStats();
    /// Called on the thread that ran a failing job, with the job's name and the message.
    /// Exceptions from the handler are swallowed.
    using ErrorHandler = std::function<void(const std::string& jobName, const std::string& what)>;
    void SetErrorHandler(ErrorHandler handler);

private:
    struct RangeFn {
        void* obj;
        void (*call)(void*, std::size_t, std::size_t);
    };
    void ParallelForImpl(std::size_t begin, std::size_t end, std::size_t grain, RangeFn fn);
    JobHandle SubmitImpl(std::function<void()> fn, const JobHandle* deps, std::size_t depCount,
                         const JobOptions& options, bool mainThread);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// ---- templates ----------------------------------------------------------

template <class F>
void JobSystem::ParallelFor(std::size_t begin, std::size_t end, std::size_t grain, F&& fn) {
    using Fn = std::remove_reference_t<F>;
    if constexpr (std::is_invocable_v<Fn&, std::size_t, std::size_t>) {
        RangeFn r;
        r.obj = const_cast<void*>(static_cast<const void*>(std::addressof(fn)));
        r.call = [](void* o, std::size_t b, std::size_t e) { (*static_cast<Fn*>(o))(b, e); };
        ParallelForImpl(begin, end, grain, r);
    } else {
        auto perChunk = [&fn](std::size_t b, std::size_t e) { for (std::size_t i = b; i < e; ++i) fn(i); };
        RangeFn r;
        r.obj = &perChunk;
        r.call = [](void* o, std::size_t b, std::size_t e) { (*static_cast<decltype(perChunk)*>(o))(b, e); };
        ParallelForImpl(begin, end, grain, r);
    }
}

template <class T, class Map, class Reduce>
T JobSystem::ParallelReduce(std::size_t begin, std::size_t end, std::size_t grain, T identity,
                            Map&& map, Reduce&& reduce) {
    if (end <= begin) return identity;
    const std::size_t count = end - begin;
    const std::size_t g = EffectiveGrain(count, grain);
    const std::size_t chunks = (count + g - 1) / g;
    struct Slot { T value; };   // a struct, so T = bool does not become vector<bool> bits shared between threads
    std::vector<Slot> partial(chunks, Slot{identity});
    ParallelFor(begin, end, g, [&](std::size_t b, std::size_t e) {
        T acc = identity;
        if constexpr (std::is_invocable_v<Map&, std::size_t, std::size_t>) {
            acc = map(b, e);
        } else {
            for (std::size_t i = b; i < e; ++i) acc = reduce(acc, map(i));
        }
        partial[(b - begin) / g].value = std::move(acc);
    });
    T result = identity;
    for (Slot& s : partial) result = reduce(result, s.value);
    return result;
}

} // namespace okay
