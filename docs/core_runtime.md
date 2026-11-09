# Core runtime foundations

Small building blocks that game and engine code can use without pulling in the
scene or the renderer: a job system, memory arenas and pools, generational
handles, hashed string ids, console variables, and frame pacing helpers. They
sit beside `Application` and `Time` and change nothing there; you opt in by
calling them from your own loop.

| Header | What it gives you | Source |
|---|---|---|
| `okay/Core/Jobs.hpp` | worker pool, task graph, `ParallelFor`, main-thread queue | `Core/Jobs.cpp` |
| `okay/Core/Memory.hpp` | `FrameArena`, `PoolAllocator<T>`, `AllocStats` | header only |
| `okay/Core/SlotMap.hpp` | `Handle<T>`, `SlotMap<T>` | header only |
| `okay/Core/StringId.hpp` | `StringId`, the `_sid` literal | `Core/StringId.cpp` |
| `okay/Core/FramePacing.hpp` | `ClampFrameDelta`, `FrameTimer`, `FrameLimiter` | `Core/FramePacing.cpp` |

None of them is included by `Okay.hpp`; include the header you need.

Tests: `test_jobs`, `test_arena_handles`, `test_frame_pacing`.

## Job system

```cpp
#include "okay/Core/Jobs.hpp"
using namespace okay;

JobSystem& jobs = JobSystem::Global();           // or: JobSystem pool(4);

JobHandle load  = jobs.Submit([&] { LoadMesh(); });
JobHandle build = jobs.Submit([&] { BuildNavMesh(); }, {load});   // starts after `load`
jobs.Wait(build);                                // rethrows if either failed

jobs.ParallelFor(0, particles.size(), 256, [&](std::size_t i) { Step(particles[i]); });

float total = jobs.ParallelReduce<float>(0, n, 0, 0.0f,
    [&](std::size_t i) { return weight[i]; },
    [](float a, float b) { return a + b; });
```

### Workers and platforms

`JobSystem(workers)`: `kAuto` (the default) uses one thread fewer than the CPU has,
a number gives exactly that many, and **0 runs everything inline on the calling
thread**. With 0 workers `Submit` finishes the job before it returns, in submission
order, so tests and replays are deterministic. Where no thread can be started at
all (an Emscripten / WebGL build without pthreads, `JobSystem::ThreadsAvailable()`
is false) every pool has 0 workers and the same code runs unchanged. `Configure(n)`
swaps the pool; after `Shutdown()` the system also runs inline.

The threads and locks live in `Core/Jobs.cpp` behind the private shim
`engine/src/Core/CoreThreads.hpp`: native Win32 critical sections, condition variables and
`CreateThread` on Windows (the MinGW build uses the "win32" thread model), the standard
library elsewhere. The public header only uses atomics, `std::function` and `shared_ptr`.

### Jobs, handles, errors

`Submit` returns a `JobHandle`: `Status()` (`Pending`, `Running`, `Completed`, `Failed`,
`Cancelled`), `IsDone()`, `Error()` (the exception message) and `Exception()`. An empty handle counts
as done, so it is a harmless dependency. `JobOptions` sets a `priority` (`High`, `Normal`, `Low`;
FIFO inside one) and a `name` that shows up in error reports.

An exception that escapes a job never ends the process: it is captured, the job becomes `Failed`,
`Stats()` counts it, `SetErrorHandler` hears about it, and `Wait(handle)` rethrows it (every time you
call it). `WaitAll` waits for every job first, then rethrows the first failure. `WaitFor(handle,
seconds)` and `WaitIdleFor(seconds)` are the versions with a time limit; they never throw.

### Dependencies

`Submit(fn, {a, b})` (or a `std::vector<JobHandle>`) starts `fn` after every dependency completed.
A dependency that **failed or was cancelled** cancels the job too, transitively, and hands it the
exception: `Wait` on the last job of a chain tells you what broke. Dependencies must come from the same
system; a pending handle from another system throws `std::invalid_argument`.

### Waiting helps

`Wait`, `WaitAll`, `WaitIdle` and the end of a `ParallelFor` run other queued jobs on the waiting thread
instead of sleeping, so a job can wait for jobs it submitted even when the pool has one thread:

```cpp
jobs.Submit([&] {
    JobHandle child = jobs.Submit([&] { Part(); });
    jobs.Wait(child);                      // fine: this thread runs `child` itself if it must
});
```

Waiting for yourself throws `std::logic_error` (it would never finish). Do not call `WaitIdle` or
`Shutdown` from inside a job.

### ParallelFor and ParallelReduce

`ParallelFor(begin, end, grain, fn)` splits the range into chunks of `grain` indices (0 = pick from the
range and the pool size), the calling thread works too, and the call returns when every index is
done. `fn(i)` runs per index; `fn(chunkBegin, chunkEnd)` runs per chunk (the signature picks). Chunks run
at the same time and in no particular order, so `fn` must only write to its own indices. If `fn` throws,
chunks not yet started are skipped and the first exception is rethrown after the running ones finish.
With 0 workers it is a plain serial loop with the same chunk boundaries.

`ParallelReduce<T>(begin, end, grain, identity, map, reduce)` combines the chunk results **in index order**,
so the answer does not depend on scheduling and `reduce` only has to be associative (string
concatenation works).

### Main-thread queue

```cpp
jobs.Submit([&] {
    Mesh m = Decode();                                  // on a worker
    jobs.RunOnMainThread([&, m] { gpu.Upload(m); });    // on the main thread
});
...
jobs.PumpMainThread();                                  // once a frame, from the game loop
```

`RunOnMainThread` never runs the function early, not even when called from the main thread. The
"main thread" is the one that created the system (`AdoptMainThread()` changes it). `PumpMainThread(max)`
runs the jobs that were queued when it started, so a job that posts another cannot keep a frame busy
forever. A main-thread job that throws is recorded; the rest still run. When the main thread waits in
`Wait`, it also runs main-thread jobs, so a worker waiting on one cannot deadlock the main thread.

### Cancelling and shutting down

`Cancel(handle)` stops a job that has not started (and everything that depends on it); it returns false once
the job started. `CancelPending()` cancels everything not started. `Shutdown(cancelPending = false)` finishes
(or cancels) the queue, runs the main-thread queue, and joins the workers; the destructor drains by default
(`SetDrainOnDestroy(false)` cancels instead, which `Global()` uses at exit). A job submitted while a cancelling
shutdown runs comes back already cancelled. What a cancelled job captured is destroyed right away.

### Counters

```cpp
JobStats s = jobs.Stats();
// workers, submitted, completed, failed, cancelled,
// ranOnWorkers, ranHelping (run by a waiting thread), ranInline, ranOnMainThread,
// queueDepth, peakQueueDepth, blocked, running, mainQueueDepth, busyMs, lastError
std::string line = FormatJobStats(s);
```

`JobStats` is a plain struct for the Profiler window or a debug overlay to copy; the job system does not
write to the Profiler itself. `busyMs` is time inside job bodies summed over all threads.

### Limits

- One shared queue protected by one lock (three priorities, plus the main-thread queue). There are no
  per-worker deques, so no work stealing; `ranHelping` is the counter to watch instead. Fine for jobs of
  tens of microseconds and up; use a bigger `grain` for tiny loop bodies.
- Out of memory while queueing a job gives a cancelled handle whose `Wait` rethrows `bad_alloc`; nothing hangs
  and no lock is left held (tested by failing every n-th allocation).
- Under ThreadSanitizer, libstdc++'s reference count on a rethrown `std::exception_ptr` is invisible to the tool
  (the library is not instrumented). When the last handle to a failed job is dropped on a different thread than the one
  that caught its exception, TSan can print a rare report about a `delete` against a read of `what()`. It is a false
  positive of that kind; `ParallelFor` moves its exception out of the shared state to stay clear of it.

## Memory

### FrameArena

A linear allocator for per-frame scratch data: allocate freely, `Reset()` at the start of the next frame.

```cpp
FrameArena frame(256 * 1024, /*limit*/ 4 * 1024 * 1024);
...
auto* contacts = frame.AllocArray<Contact>(n);       // uninitialised; nullptr if over the limit
auto* tmp = frame.Make<Tmp>(args...);
const char* label = frame.CopyString("hits: 3");
frame.Reset();                                       // everything above is gone
```

- `Allocate(bytes, alignment)` honours any power-of-two alignment; a bad alignment or a request over the limit
  gives `nullptr` and bumps `Stats().failures`. Nothing throws.
- `limit` caps the bytes handed out between resets (padding included); 0 means no cap. `HighWater()` is the
  largest `Used()` ever seen.
- It grows by chunks, so pointers stay valid until `Reset()`. A reset after a frame that needed several chunks
  replaces them with one chunk big enough for that frame, so a steady workload stops allocating from the system.
- `poisonOnReset` (on in debug builds) fills reset memory with `kPoisonByte` (0xDD) so a stale pointer shows up as
  garbage.
- Nothing is destructed: keep to trivially destructible types (`AllocArray` and `Make` enforce it).
- `Mark()` / `Rewind(marker)` and `ArenaScope` give back part of a frame. A marker from before a `Reset()` is refused.
- `ArenaAllocator<T>` lets standard containers use the arena: `std::vector<int, ArenaAllocator<int>> v{ArenaAllocator<int>(&frame)};`
  (it throws `std::bad_alloc` at the limit, as containers expect).

Not thread safe; use one arena per thread.

### PoolAllocator

```cpp
PoolAllocator<Bullet> bullets(/*block*/ 256, /*max live*/ 4096);
Bullet* b = bullets.Create(pos, dir);     // nullptr when full or out of memory
...
bullets.Destroy(b);                       // false for nullptr or a double Destroy
```

Slots live in blocks that are never moved, so an address is stable until its own `Destroy`. The free list is LIFO
(the slot freed last is handed out next). Objects still alive when the pool is destroyed or `Clear()`ed are
destructed. `Owns`, `IsLive`, `ForEach`, `Live`, `Capacity`, `FreeSlots` and `Stats()` are there for debugging. A
constructor that throws gives the slot back.

`AllocStats` (`used`, `reserved`, `limit`, `highWater`, `live`, `allocations`, `failures`, `resets`) is shared by both.

## Handles: SlotMap

```cpp
SlotMap<Enemy> enemies;
Handle<Enemy> h = enemies.Insert(Enemy{...});
if (Enemy* e = enemies.Get(h)) { ... }     // nullptr once erased, even if the slot was reused
enemies.Erase(h);

for (Enemy& e : enemies) { ... }                       // live values only, packed
for (auto [handle, e] : enemies.Entries()) { ... }
```

`Handle` is an index plus a generation. Erasing frees the slot and moves the generation on, so an old handle is
recognised as stale; a slot whose generation would wrap around is retired instead of reused. Insert, erase and lookup
are O(1). Erasing moves the last value into the hole, so addresses and iteration order are not stable (handles are);
do not insert or erase while iterating (`EraseIf` is the safe bulk version). `Handle::Raw()` / `FromRaw()` pack it into
a `uint64_t`, and `std::hash<Handle<T>>` exists. `SlotMap<T, Tag>` makes handles of two maps of the same `T` different types.

## StringId

```cpp
constexpr StringId kJump = "player.jump"_sid;          // hashed at compile time
switch (id.Hash()) { case "player.jump"_sid.Hash(): ... }
StringId s = StringId::Intern(userText);               // hashed at run time, text remembered
std::string name = s.Name();                            // "player.jump", for logs
```

A 64-bit FNV-1a hash; the empty string and the default id are "none" (0). `FromString` never touches the table.
`Intern` records the text in a global table (thread safe) so `Name()` can give it back; `Make` interns in debug builds
only. When two different strings share a hash, `Intern` keeps the first, counts a collision (`CollisionCount()`) and calls the
handler set with `SetCollisionHandler` (in debug builds it prints one line when there is none). That check is on in debug
builds and off in release builds; `SetCollisionCheck` changes it. `InternWithHash` exists to test it.

## Console variables

Console variables already exist: see `okay/Core/Cvar.hpp` (`Cvars`, `Cvar`) and the console in
`okay/Core/Console.hpp`. They have ranges, change callbacks, a command-line override and file persistence. A
first version of this runtime work also had a second registry with read-only, cheat and restart-required flags;
it was left out so there is one system. Those flags are a follow-up for `Cvar`.

## Frame pacing

```cpp
FrameTimer timer(0.1f);              // never report a frame longer than 0.1 s
FrameLimiter limiter(0.0);           // 0 = off
s_maxFps.OnChange([&](const int& fps, const int&) { limiter.SetTargetFps(fps); });

while (running) {
    Time::Step(timer.Tick());        // clamped delta
    ...frame...
    limiter.EndFrame();              // sleeps out the rest of the frame
}
```

- `ClampFrameDelta(dt, maxDelta)`: a long stall (a breakpoint, a window drag, a slow load) becomes `maxDelta`; NaN, infinity and negative
  deltas become 0.
- `FrameTimer::Tick()` measures between calls (the first returns 0); `Tick(nowSeconds)` takes the clock for tests. `WasClamped()` and
  `ClampedCount()` tell you it happened, `Reset()` forgets the previous frame after a pause.
- `FrameLimiter` sleeps most of the wait and yields for the last 1.5 ms, keeps a steady grid (a frame a little late is paid back by a short
  next one) and starts over after being more than a frame behind. `Plan(now)` is the decision without the sleep.
  In a WebGL build nothing sleeps: the browser paces the frames.

## Not covered

- `Application` can use the limiter (`Application::Config::pacing = Pacing::Limiter`, see [loop_and_time.md](loop_and_time.md); the default is still its own sleep). No hook into the Profiler window yet: call `Time::Step`, `PumpMainThread`, `Reset` and the stats yourself.
- Work stealing and per-worker queues (see Limits above).
- `SlotMap` and `FrameArena` are not thread safe by design.
