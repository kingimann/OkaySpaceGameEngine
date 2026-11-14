# Logging and profiling

Two small core systems, both header-only: `okay/Core/Log.hpp` and
`okay/Core/Profiler.hpp`.

## Log

```cpp
Log::Info("loaded ", n, " assets");     // [okay][INFO ] loaded 12 assets
Log::Warning("low memory");             // stderr; Error also goes to stderr
```

Levels are `Trace`, `Info`, `Warning`, `Error`. `Log::minLevel` is the global
threshold (set it at startup; it is a plain value). A message below the
threshold is dropped before its arguments are formatted. Everything is
thread safe: each line is written whole, so threads never interleave.

### Categories

```cpp
Log::Category net("net");
net.Warning("lost ", n, " packets");    // [okay][WARN ][net] lost 3 packets
Log::SetCategoryLevel("net", Log::Level::Warning);
```

A category override beats `minLevel` in both directions (quieter or louder).
`Log::ClearCategoryLevels()` removes all overrides.

### Configuration string

`Log::Configure("info,net=trace,physics=error")` sets the global level and
per-category levels (`trace`, `info`, `warn`/`warning`, `error`, any case).
Bad entries are skipped; it returns how many were applied. The environment
variable `OKAY_LOG` takes the same format and is read once, before the first thing that touches the levels (the first
message, `Configure`, `SetCategoryLevel` or `ClearCategoryLevels`). A `Configure` call in your program therefore always
wins over the environment, whether or not anything was logged before it. Assigning `Log::minLevel` directly is not seen
by that rule: do it after the first message, or use `Configure`.

### Timestamps, console, sink

- `Log::showTimestamps = true` prefixes `HH:MM:SS.mmm ` (local time) to console and file lines.
- `Log::console = false` silences stdout/stderr (file and sink keep working).
- `Log::sink` still receives `(level, bare message)`; the editor Console uses it.
  It is called under the log lock, so it must be quick. It may itself call `Log`: such a message reaches the console and the
  file but is not handed to the sink again, so a sink that logs cannot recurse.

### File sink

```cpp
if (!Log::OpenFile("game.log", 5 * 1024 * 1024, 3)) { /* could not open */ }
...
Log::CloseFile();
```

Appends to the file. When a line would take it past `maxBytes`, `game.log`
becomes `game.log.1`, `.1` becomes `.2` and so on, keeping `keep` old files, so the disk use is at most
`(keep + 1) * maxBytes` (plus one line longer than `maxBytes`). `maxBytes` 0 means never rotate (it used to rotate before
every line, keeping only the last). The file is flushed on Warning and Error, and on `CloseFile`.

- `Log::Flush()` writes what is buffered now: call it from a crash or quit handler, because Info lines are otherwise only
  flushed by a later Warning or Error.
- A write that fails (disk full, a drive that went away) is counted in `Log::FileWriteFailures()`; the stream is cleared, so
  later lines try again and the log resumes by itself when space comes back. The console and the sink are not affected.
- The player and the editor do not open a log file yet; a game that wants one calls
  `Log::OpenFile(Paths::Join(Paths::LogDir(), "game.log"))` after `Paths::EnsureDir(Paths::LogDir())`.

## Profiler

`Prof()` is the global frame profiler (off by default; the editor window turns
it on). `OKAY_PROFILE("Name")` times a scope, `BeginFrame()`/`EndFrame(ms)` bracket
a frame, and the last 240 frames are kept. When `enabled` is false every call
returns at once and nothing is allocated.

Each bucket (one per section name and nesting depth per frame) now also keeps
`startMs`, its first start relative to the frame start, and each frame keeps
`startMs` since the first frame, so a timeline can be rebuilt.

Frames are addressed by timeline index, 0 = oldest. An index outside `0 .. Count() - 1` gives an empty frame (and 0 from
`SeriesTotal`, `SeriesCpu`, `SeriesGpu`); it used to read outside the ring for a negative index.

The profiler records the thread that called `BeginFrame` (the main thread). A scope opened on another thread (a job, a
loader) is ignored instead of being pushed onto the main thread's scope stack, so `OKAY_PROFILE` can stay in code that
also runs on workers. A frame time or GPU time that is NaN, infinite or negative is stored as 0, so one bad number cannot
poison the averages (`Range`, `Summary`).

| Call | Result |
| --- | --- |
| `ExportChromeTrace(out, first, last)` | Chrome trace JSON for `chrome://tracing` or Perfetto; one `"ph":"X"` event per bucket, `ts`/`dur` in microseconds, names JSON-escaped. Returns the event count. |
| `ExportChromeTraceToFile(path, first, last)` | Same, written to a file; false if it cannot be written. |
| `ExportCsv(out, first, last)` / `ExportCsvToFile(path, ...)` | `frame,section,ms,calls,depth,self_ms` rows (`ms` is inclusive; names are CSV-quoted when needed). |
| `DumpFrame(idx)` | An indented text tree for one frame, parents before children. |
| `Summary()` | Per-section min/avg/max self time over the whole ring, as text. |

A bucket merges repeated calls of one section, so its trace event spans the
summed time of those calls (the call count is in `args`). In a tracing viewer
this can make a merged event extend over a following sibling.

Trace names are written as valid UTF-8: text that is not (a name read from a Latin-1 file) has its bad bytes replaced by
U+FFFD, so the file always opens in a trace viewer. Quotes, backslashes and control characters are escaped as before.

## What changed (core wave 6)

| Was | Now |
| --- | --- |
| With `OKAY_LOG=error` set, `Log::Configure("trace")` was undone by the environment at the first message (when nothing had logged before it). | The environment is applied first, always; a `Configure` / `SetCategoryLevel` call in code wins. |
| `OpenFile(path, 0)` rotated before every line. | 0 means never rotate. |
| A sink that logged recursed until the stack ran out. | A message logged from inside the sink skips the sink. |
| A failed file write (disk full) was silent and left the stream failed, so the log never resumed. | Counted in `FileWriteFailures()`, the stream is cleared, later lines retry. New: `Log::Flush()`. |
| `Prof().FrameAt(-1)` (a scrubber stepping left of the oldest frame) read outside the ring; `FrameAt(n)` past the end gave a wrong frame. | An empty frame. |
| `OKAY_PROFILE` on a worker thread corrupted the main thread's scope stack, and its `EndFrame` could end the main frame. | Only the thread that began the frame is profiled. |
| `EndFrame(NaN)` made every later average NaN. | Stored as 0. |
| A non-UTF-8 section name made the Chrome trace unreadable. | Repaired with U+FFFD. |

Looked at and found fine: rotation renames (a failed rename still opens a fresh file), the ring wrap at 240 frames, CSV
quoting, the trace event fields (`%.3f` microseconds, always finite). Not done: a section's time on another thread (the
profiler is a main-thread frame profiler), the C locale assumption in `%.3f` (nothing in the engine changes the locale).
