# Core runtime upgrade: jobs, allocators, profiler

Branch `claude/core-perf-1`. Owner request: "improve the core engine" (core runtime and performance). This page says what
already existed, what was missing, what was added, and how it was measured. Everything here is our own code.

## Existing / Gap

| Area | Existing (before this change) | Gap | What was done |
|---|---|---|---|
| Job system | `Core/Jobs` (#578): worker pool behind one lock, task graph with dependencies, 3 priorities, `ParallelFor`/`ParallelReduce` (workers claim chunks from a shared counter), main-thread queue, cancel, counters | No per-worker deques or stealing; no cheap fork-join (every job is a `shared_ptr` + `std::function`); no frame arena for job memory; nested parallelism blocks a thread per level | `JobCounter` + `JobSystem::Fork/Join`: per-worker deques (owner takes the newest task, other threads steal the oldest), per-priority, tasks in a lock-free task arena that is recycled whenever no task is outstanding, workers spin briefly before sleeping; `ParallelFor` now uses it (recursive halving down to about 8 leaves per thread; chunk boundaries unchanged); the old path stays behind `SetWorkStealing(false)` and is the "shared-queue" column in the benchmark. The old API (`Submit`, dependencies, `Wait`) is unchanged |
| Allocators | `Core/Memory`: `FrameArena` (bump, marker/rewind, limit, poison), `PoolAllocator<T>` (stable addresses, free list), `ArenaAllocator` for std containers. Nothing in the engine used them | No stack allocator; no tracking; not used in a real hot path | `StackAllocator` (LIFO, one buffer). `Core/MemoryTrack.hpp`: opt-in tracking by tag (`SetTag("name")` on any of the three) with live bytes, reserved, peak, allocation counts, fragmentation (share of reserved memory not in use) and a leak report (pool objects or stack bytes still alive when their allocator dies, or when the process exits). Scene's per-frame camera list (`ResolveMainCamera`, every frame) and its click snapshot (`wasOn`, on frames with a mouse press) now live in a `FrameArena` owned by the Scene |
| Profiler | `Core/Profiler`: main-thread scopes merged into per-section buckets, 240-frame ring, Chrome trace export of those buckets (one event per section per frame), CSV, editor window with pause and scrub. `Debug/FrameHistory` also has a trace export for frame records. `Debug/MemoryTracker` (manual per-category byte counters) and `Debug/MemoryHooks` (opt-in operator new counters) | Scopes on other threads are ignored; the trace has one merged event per section, not the real nested calls; no per-thread tracks; no counters in the trace; no button or switch to capture | `Core/TraceCapture.hpp`: a ring buffer per thread (32768 events each), nested scopes as real events on their thread's track, jobs and fork-join tasks on worker tracks, counters (frame ms, draw objects, triangles, particles, tracked memory, scene objects), frame events; written as Chrome Trace Event Format. Start it from the editor Profiler window or `OKAY_TRACE` |

`Debug/MemoryTracker` (manual categories for textures, audio and so on) and the new `MemTrack` (per allocator instance, with
leaks and fragmentation) answer different questions and are not merged; `MemTrack` totals appear as counters in a frame
capture, and nothing feeds `MemoryTracker` from it yet.

## What each new file was written from (rule 19a)

- `Core/Jobs.cpp` work-stealing part: the general idea of per-thread deques with stealing from the opposite end and
  recursive range splitting (textbook algorithms, described in many papers; Blumofe and Leiserson's 1999 work stealing paper
  for the scheduling idea). The deque is a spin-locked ring buffer written here, not a lock-free Chase-Lev deque. No code from
  another project.
- `Core/MemoryTrack.hpp`, `StackAllocator`: from the problem statement. A registry of stats callbacks behind a spin lock.
- `Core/TraceCapture.hpp`: from the public description of the Chrome Trace Event Format (JSON object with `traceEvents`;
  `X` complete events with `ts` and `dur` in microseconds; `C` counter events with `args`; `M` metadata `thread_name`).

Windows rule (21a): no `<mutex>`, `<thread>`, `<condition_variable>` or `call_once` in any of these headers. They use atomics,
spin locks, `thread_local`, and the existing `CoreThreads.hpp` wrapper inside `Jobs.cpp`.

## Tests

| Test | What it covers |
|---|---|
| `test_jobs_steal` | fork-join sums, nested fork-join (fib), `ParallelFor` coverage and chunk boundaries at 0, 1, 2 and 4 workers, exceptions, priorities with one worker, stealing really happens, arena recycles (no growth), 0-worker inline order, 4 outside threads forking and joining at once, shutdown with tasks in flight, the legacy switch. Run under ThreadSanitizer and ASan/UBSan: clean (Linux) |
| `test_jobs` | the existing job tests, unchanged, still pass (also under ThreadSanitizer) |
| `test_memory_track` | stack allocator, tracking off costs nothing, tags, peak, fragmentation, leak report, move keeps the tag |
| `test_trace_capture` | nested scopes, a capture started inside a scope, several threads and jobs, ring overwrite, counters, auto-stop after N frames writing a file, JSON syntax checked by a small parser, escaping |
| `test_jobs_bench` | prints the table below (never fails on speed) |
| `test_frame_arena_bench` | Scene::Update timing and heap-vector versus arena list timing |

## Measured numbers

See the end of this page (filled in from the runs in the report). All on a shared 4-thread Linux container, so treat small
differences as noise.

## Not done / unverified

- Windows: nothing was run on Windows. The editor change (`editor/main.cpp`) was not compiled in this container.
- The deque is spin-locked, not lock-free; under heavy contention a Chase-Lev deque would scale better.
- `JobSystem::Wait` (the older handle API) does not run fork-join tasks; use `Join`.
- Nothing else in the engine calls `Fork` yet besides `ParallelFor`; per-system parallel loops are a separate job.
- Memory tracking is not wired to the editor UI; use `OKAY_MEMTRACK=1` (leak report on stderr at exit) or `MemTrack::Report()`.
