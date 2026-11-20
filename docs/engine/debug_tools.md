# Profiling and debugging tools (data layer)

Headers in `engine/include/okay/Debug/`, sources in `engine/src/Debug/`, all in
`namespace okay`. These are the models behind the profiler timeline, memory
view, frame debugger and error console (feature map area 23). They hold and
compute data; they draw nothing. A window, an in-game overlay or a command
line tool reads them. They build on `Core/Log.hpp` and `Core/Profiler.hpp`
(see `core_logging_profiling.md`) and change neither.

```cpp
#include "okay/Debug/Debug.hpp"     // everything below except MemoryHooks.hpp
```

| Piece | Header | What it is |
| --- | --- | --- |
| CPU frame timeline | `FrameHistory.hpp` | Ring buffer of per-frame scope trees, statistics, spikes, call tree, exports |
| GPU timing | `GpuTimer.hpp` | `GpuTimer` interface, CPU-fence fallback, `GpuScope` |
| Render stats | `RenderStats.hpp` | `GpuFrameStats`, `RenderStatsCollector` |
| Memory tracking | `MemoryTracker.hpp` | Named categories, peaks, deltas, leak suspects, snapshot diff |
| Heap counters | `MemoryHooks.hpp` | Opt-in global `operator new/delete` counters (off by default) |
| Frame debugger | `DrawCallRecorder.hpp` | Capture one frame's draw list, step through, filter, export |
| Error console | `ConsoleModel.hpp` | Ring buffer over the Log: collapse, filters, search, source links |
| Stats overlay | `DebugStatsOverlay.hpp` | Text lines and graph points for an overlay |

Tests: `tests/test_debug_*.cpp` (ctest names `debug_*`).

Thread safety, in one table:

| Class | Threads |
| --- | --- |
| `MemoryTracker` | Add/Remove/Resize/Set lock free from any thread; EndFrame and reads take a short spin lock |
| `RenderStatsCollector` | Reporting calls lock free from any thread; reads take a spin lock |
| `DrawCallRecorder` | Any thread (spin lock); `IsCapturing()` is one atomic load |
| `ConsoleModel` | Log may call in from any thread while the UI reads (spin lock) |
| `FrameHistory`, `DebugStatsOverlay`, `GpuTimer` | One thread (the one that owns the Profiler / the renderer) |

The locks are `std::atomic` spin locks (`DebugUtil.hpp`), because `<mutex>` and
`<thread>` do not work with MinGW's "win32" thread model and none of these
headers includes them. Critical sections only copy a few small records.

## 1. CPU frame timeline: `FrameHistory`

```cpp
FrameHistory history(600);              // keep the last 600 frames
Prof().enabled = true;
// each frame, after Prof().EndFrame(ms):
history.Capture(Prof());                // copies the frames it has not seen yet

Distribution d = history.SeriesStats();                       // frame time: min/avg/max/p50/p90/p95/p99
ScopeStats  s  = history.ScopeStat("Physics");                // one scope, self time
auto all       = history.AllScopeStats(FrameHistory::Metric::Inclusive);
auto spikes    = history.FindSpikes(2.0);                     // frames slower than 2x the median
FlameGraph g   = history.BuildFlameGraph();
```

**Profiler adapter.** `Capture(const Profiler&)` reads the Profiler's public
ring (`Count()`, `FrameAt()`), recognises frames it already copied by their
`startMs`, and appends the rest, so `Profiler.hpp` needs no change. Notes:

- A paused or disabled Profiler adds nothing.
- If you capture less often than the Profiler's 240 frames, the older frames
  are gone; you get what it still holds. Capture every frame (or every few).
- A second Profiler whose clock is behind restarts the capture.
- The Profiler keeps one bucket per section name and depth per frame, so the
  tree is rebuilt from start time and depth. Two scopes with the same name and
  depth under different parents merge in the Profiler and show under one parent.
  Frames built with `FrameRecord::AddScope` (tests, imports from other tools)
  have exact trees.

Frames are addressed by timeline index, 0 = oldest, like the Profiler. Each also
has a `sequence` number that only grows, so exports stay unambiguous after the
ring wraps (`IndexOfSequence(seq)` maps back, -1 if dropped).

**Statistics.** Percentiles interpolate linearly between ranks (p95 of 1..100 is
95.05). A scope's stats cover only the frames where it ran, unless you pass
`absentAsZero` (right for a scope that runs every few frames). A scope that
recurses into itself is counted once in inclusive time.

**Spikes.** `FindSpikes(k, first, last, minMs)` returns frames whose total time
is more than `k` times the median of the range scanned (and above `minMs`),
each with the scope that rose most above its own median (`culprit`).

**Call tree.** `FlameGraph` merges scopes with the same name path across frames:
`inclusiveMs`, `selfMs`, `calls` per node, children largest first. `ToText()`,
`ToFolded()` (`a;b;c <self microseconds>`, the input format of common flame
graph tools), `ToJson()`, and `Layout()` (rectangles with x0..x1 fractions of
the total, for drawing).

**Export** of a frame range: `ExportText`, `ExportJson`, `ExportChromeTrace`
(and `...ToFile`). The trace has a `Frame N` event per frame on thread 0 and one
event per scope on thread 1; open it in `chrome://tracing` or Perfetto.

## 2. GPU timing and render stats

### `GpuTimer`

```cpp
CpuFenceGpuTimer timer([] { glFinish(); });   // fence = wait for the GPU to drain
timer.SetEnabled(true);                       // starts disabled: fencing stalls the pipeline
SetActiveGpuTimer(&timer);

// renderer
if (auto* t = ActiveGpuTimer()) t->BeginFrame();
{ GpuScope s("Shadows"); /* draw shadow casters */ }
{ GpuScope s("Opaque");  /* draw the scene */ }
if (auto* t = ActiveGpuTimer()) t->EndFrame();

// anywhere, every frame
GpuFrameTiming timing;
while (ActiveGpuTimer() && ActiveGpuTimer()->Poll(timing)) Prof().Gpu(timing.totalMs);
```

`Poll` hands back finished frames oldest first, because real GPU timestamp
queries resolve a frame or two late. `CpuFenceGpuTimer` has its results ready at
once. With no fence it only measures how long the CPU took to submit work, which
is a lower bound. `timing.fromHardware` says which kind of number you have.

**Hook points per renderer** (the renderers are not modified; a hardware
`GpuTimer` subclass goes in these places):

| Renderer | Where |
| --- | --- |
| `GLRenderer` | In `RenderToPixels`: `BeginFrame` before the first GL call, a scope per pass (shadow maps, scene, post chain) using `GL_TIMESTAMP` / `GL_TIME_ELAPSED` queries, `EndFrame` after the read-back |
| `D3D11Renderer` | A `D3D11_QUERY_TIMESTAMP_DISJOINT` around the frame and a `D3D11_QUERY_TIMESTAMP` at each scope edge; resolve one or two frames later |
| `D3D12Renderer` | A timestamp query heap, `EndQuery` at scope edges, `ResolveQueryData` into a readback buffer, convert with the queue's timestamp frequency |
| `SoftwareRenderer`, `ConsoleRenderer` | CPU rasterisers: wrap the frame in a scope; the CPU-fence timer (no fence needed) is exact |

Feeding the result into the timeline: `Prof().Gpu(timing.totalMs)` before
`EndFrame`, so each `FrameRecord` carries `gpuMs`.

### `GpuFrameStats` and `RenderStatsCollector`

```cpp
RenderStatsCollector& rs = RenderStatsCollector::Global();
rs.BeginFrame();
rs.BindMaterial(RenderStatsCollector::IdOf(&material));   // counts only when it changes
rs.BindShader(shaderId);   rs.BindTexture(textureId);
rs.DrawCall(mesh.TriangleCount(), mesh.VertexCount());    // per instance; pass `instances` for instanced calls
rs.EndFrame();
GpuFrameStats last = rs.Last();      // draw calls, triangles, material/shader/texture/target switches, ...
```

The additive API for renderers is: `BeginFrame`, `EndFrame`, `DrawCall`,
`Dispatch`, `BindMaterial/Shader/Texture/RenderTarget`, `TextureCreated/Destroyed`,
`MeshCreated/Destroyed`, `SetGpuMs`. A switch is counted when the id differs from
the one bound before it in the frame (the first bind of a frame counts).
`GpuFrameStats::EstimateTextureBytes(w, h, bpp, mipmaps, layers)` gives a rough
size (+1/3 for a mip chain). `ApplyToProfiler(Prof())` copies object and
triangle counts into `Profiler::Stat3`.

## 3. Memory tracking: `MemoryTracker`

```cpp
MemoryTracker& mem = MemoryTracker::Global();
mem.Add(MemoryTracker::Textures, tex.SizeBytes());        // loaded
mem.Remove(MemoryTracker::Textures, tex.SizeBytes());     // unloaded
mem.Resize(MemoryTracker::Scripts, oldBytes, newBytes);   // grew in place
int fluids = mem.Category("Fluids");                      // your own category (up to 64)
mem.EndFrame();                                           // once per frame
```

Built-in categories: Textures, Meshes, Audio, Scripts, Scene Objects, Animation,
Physics, UI, Shaders, Other. The tracker keeps live bytes and counts, peaks,
`FrameDelta` (change since the previous `EndFrame`), and the last N frames of
live bytes per category (`History`). Removing more than is live clamps to zero
and counts an underflow (`Underflows()`), which usually points at a Remove with
a wrong size. `Set` replaces a category's figures, for systems that know their
own total.

**Leak suspects.** `LeakSuspects()` reports categories whose live bytes did not
drop over the last `windowFrames` frames, rose in at least `minRisingFraction` of
the steps, and grew by `minGrowthBytes`. A one-off load that then stays flat is
not flagged. It is a heuristic: a cache that is still filling looks the same, so
treat the result as a lead. Raise `toleranceBytes` to allow small dips.

**Snapshots.** `Snapshot("label")` copies the figures; `MemoryTracker::Diff(a, b)`
lists what changed, largest first, as text or JSON. Typical use: snapshot before
and after loading a scene and diff.

**Asset manager integration (by convention, no code change needed).** When the
asset manager loads or unloads an asset, call `Add` / `Remove` with the asset
type's category and the asset's byte size; on a hot reload call `Resize`. Use the
same size for both calls (store it on the asset) so Remove balances Add. Textures
map to `Textures`, meshes and model imports to `Meshes`, clips to `Audio`, script
and visual script assets to `Scripts`; spawned objects to `Scene Objects`.

### Heap counters: `MemoryHooks.hpp` (opt-in, off by default)

Including the header does nothing by itself. To count every `new` and `delete`
in a program, put this in exactly **one** .cpp file of the executable (its
`main.cpp`):

```cpp
#define OKAY_ENABLE_MEMORY_HOOKS
#include "okay/Debug/MemoryHooks.hpp"
```

Then `MemoryHooks::Read()` returns `HeapStats` (allocations, frees, live and
peak bytes, total requested), `Delta(before, after)` compares two reads, and
`SyncTo(tracker, category)` puts the live heap into a tracker category. The
engine never defines the macro, and no target does. Defining it in two files is a
link error (duplicate `operator new`), so it cannot be half enabled. Each block
gets a 16 byte header and `malloc`/`free` underneath, so do not ship builds with
the hooks on. Only `operator new/delete` is seen, not direct `malloc`, OS
allocations or GPU memory. Tests: `test_debug_memory_hooks` (the only target that
turns it on).

## 4. Frame debugger: `DrawCallRecorder`

A renderer reports each call through `IFrameDebugHook`; the recorder keeps the
list; a window shows and steps through it.

```cpp
// tool side
DrawCallRecorder rec;
SetActiveFrameDebugHook(&rec);
rec.RequestCapture();                       // capture the next frame
// ... a frame later:
for (size_t i = 0; i < rec.Count(); ++i) { DrawCallRecord c; rec.Get(i, c); /* show c */ }
rec.SetBreakAfter(12);  rec.StepForward();  // step through
rec.ExportJson();

// renderer side (additive, a few lines, not applied to any renderer yet)
auto* hook = ActiveFrameDebugHook();
if (hook) hook->BeginFrame(frameNumber);
...
if (hook && hook->IsCapturing()) {          // one atomic load when nothing is being captured
    DrawCallRecord r;
    r.name = obj->name; r.objectPath = path; r.material = mat->name; r.mesh = mesh->name;
    r.shaderState.shader = "lit"; r.viewport = {0, 0, w, h};
    r.indexCount = n; r.triangleCount = n / 3;
    hook->Record(r);
}
if (hook) hook->EndFrame();
```

- `DrawCallRecord` holds kind (draw, indexed, instanced, dispatch, clear, copy),
  name, pass, object path, material, mesh, render target, shader state (shader,
  blend, depth test/write, cull, wireframe, free-form name/value pairs),
  viewport, vertex/index/instance/triangle counts and dispatch groups. `order`
  is set by the recorder.
- A capture runs from the `BeginFrame` after `RequestCapture()` to the next
  `EndFrame`, and is readable once that frame ends (`Status`: Idle, Armed,
  Capturing, Captured). At most `maxCalls` (default 100000) are kept; `Truncated()` says if more arrived.
- **Stepping.** `SetBreakAfter(n)` means calls 0..n count as drawn and the rest
  not yet; -1 = no break (everything). `StepForward`, `StepBack`, `StepToStart`,
  `StepToEnd`, `ExecutedCount`, `CurrentIndex`. For a visual replay, call
  `BeginReplay()`, re-render the captured frame asking
  `hook->ShouldSkipDraw(order)` before each draw, then `EndReplay()`. Outside a
  replay `ShouldSkipDraw` is always false, so a left-over break cannot clip
  normal rendering.
- **Filters.** `DrawCallFilter`: text (case-insensitive substring across name,
  pass, object, material, mesh, shader, render target), kind mask, exact pass /
  material / mesh / shader, minimum triangles, executed-only.
- `Summarize()` counts draws, dispatches, triangles, switches and unique
  materials/meshes/shaders; `ExportJson(filter)` / `ExportText(filter)` write the
  capture, with the summary and the current break.

## 5. Error console: `ConsoleModel`

```cpp
ConsoleModel console;
console.Attach();                               // chains into Log::sink; the earlier sink keeps working
ConsoleFilter f; f.levelMask = ConsoleModel::LevelBit(Log::Level::Error);
console.SetFilter(f);
for (const ConsoleRow& r : console.Rows()) draw(r.entry.message, r.repeat);
if (console.ConsumePauseRequest()) game.Pause();
```

`Log` has a single `Log::sink` function, so `Attach()` wraps whatever is there
(call it from the main thread at startup, like any assignment to `Log::sink`).
`Detach()` restores the previous sink if this model's hook is still on top;
otherwise the hook just goes quiet. Two models can be attached side by side.

- **Entries**: level, category, message, time (seconds since the model was made,
  or from `SetClock`), frame (`SetFrame` / `NextFrame`), source reference.
- **Category.** The sink receives only the level and bare message, so a
  category comes from a leading `[tag] ` in the message (`Log::Error("[net] lost 3")`
  gives category `net`), or from `Add(level, message, category)` when you call it
  yourself. `SetParseCategoryPrefix(false)` turns the parsing off. Tags with a
  colon or slash are source references, not categories. (If `Log::Category` ever
  passes its name to the sink, `Add` is the place to feed it.)
- **Collapse.** Identical consecutive entries (same level, category, message)
  become one row with `repeat`, first and last time/frame. Consecutive means
  after filtering, so `Error X, Info Y, Error X` shows one `X (x2)` with an error
  filter. `SetCollapse(false)` gives every entry its own row; the data is stored
  uncollapsed either way.
- **Filters and search.** Level mask, exact category, text (substring of the
  message, category or source path; no regular expressions), case option.
  `Find(query, startRow, forward)` returns the next matching row (wrapping),
  `FindAll` every one.
- **Pause** drops new entries and counts them (`DroppedWhilePaused`).
  **Pause on error**: with `SetPauseOnError(true)`, a stored Error raises
  `PauseRequested()`; the host calls `ConsumePauseRequest()` (true once) and
  pauses play mode. Repeated errors raise it once until consumed.
- **Source links.** `ParseConsoleSource(message)` recognises
  `[Object: World/Props/Crate]` (also `[GameObject: ...]`), `[Asset: path]`
  (also `[File: ...]`, may end in `:line`) and `Assets/Level1.okayscene:12` or
  `script.okayscript:12:5` anywhere in the text (a path with an extension plus
  `:line`). Result: `kind` (Object or Asset), `path`, `line`, `column`. A click in
  the console selects the object by path or opens the asset at the line. To make
  messages linkable, log them in these forms.
- `CopyAll()` returns the visible rows as text (`[12.345s f60] ERROR [net] message (x3)`),
  or every entry with `CopyAll(false)`.

## 6. `DebugStatsOverlay`

Builds the lines and graph points for an overlay; named `DebugStatsOverlay` to
stay separate from the `DebugOverlay` line builder of the Core console work.

```cpp
DebugStatsOverlay overlay;
overlay.SetSources(&history, &MemoryTracker::Global(), &RenderStatsCollector::Global());   // any may be null
for (const std::string& line : overlay.BuildLines()) DrawLabel(line);
auto graph = overlay.BuildFrameGraph();     // points: x 0..1 (oldest to newest), y 0..1, value in ms, overTarget flag
```

Lines: `FPS: 62.5 (16.0 ms)`, `CPU: 9.5 ms  GPU: 4.2 ms`, `Memory: 10.50 MB (peak ...)`
with the biggest categories, `Possible leak: Audio +2.27 MB over 120 frames`,
`Draw calls: 2  Tris: 45.6k  Mat switches: 2`, optional `Spikes: n in last N frames`,
and your own lines (`AddLine`). `BuildLines(true)` adds percentiles and the
extra counters. `Options` switches each group, the FPS smoothing window, the
graph length, a fixed or automatic graph top and the target line (default 60 fps).
`BuildMemoryGraph()` plots total live MB; `BuildGraph(values, ...)` plots any series.

## Not covered here

- No renderer calls the hooks yet: the GPU timer, render stats and frame debugger
  hooks are documented, not wired. Nothing feeds the Profiler automatically either:
  `RenderStatsCollector::ApplyToProfiler` and `Prof().Gpu(ms)` are explicit calls.
- No editor windows: this is the data layer for them.
- `FrameHistory`'s tree comes from the Profiler's merged buckets (see above).
- The asset manager is not edited; its integration is the convention in section 3.
