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
variable `OKAY_LOG` takes the same format and is read once, at the first log call.

### Timestamps, console, sink

- `Log::showTimestamps = true` prefixes `HH:MM:SS.mmm ` (local time) to console and file lines.
- `Log::console = false` silences stdout/stderr (file and sink keep working).
- `Log::sink` still receives `(level, bare message)`; the editor Console uses it.
  It is called under the log lock, so it must be quick; it may itself call `Log`.

### File sink

```cpp
if (!Log::OpenFile("game.log", 5 * 1024 * 1024, 3)) { /* could not open */ }
...
Log::CloseFile();
```

Appends to the file. When a line would take it past `maxBytes`, `game.log`
becomes `game.log.1`, `.1` becomes `.2` and so on, keeping `keep` old files.
The file is flushed on Warning and Error, and on `CloseFile`.

## Profiler

`Prof()` is the global frame profiler (off by default; the editor window turns
it on). `OKAY_PROFILE("Name")` times a scope, `BeginFrame()`/`EndFrame(ms)` bracket
a frame, and the last 240 frames are kept. When `enabled` is false every call
returns at once and nothing is allocated.

Each bucket (one per section name and nesting depth per frame) now also keeps
`startMs`, its first start relative to the frame start, and each frame keeps
`startMs` since the first frame, so a timeline can be rebuilt.

Frames are addressed by timeline index, 0 = oldest.

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
