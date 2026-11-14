# Core tools

Small, dependency-free helpers in `okay/Core/`. All of them come with `#include <Okay.hpp>`.
None are thread safe; use them from the main thread.

## Application quit handling

`Application::Run` ends when any of these happens: the quit key is pressed
(`Config::quitKey`, default `'q'`, `0` for no quit key), `Game::RequestQuit()` is called (a PauseMenu or a
script `quit`), `Quit()` is called, or `Config::maxFrames` is reached. `Run` calls `Game::Reset()` first, so a
pause or quit left over from an earlier run does not leak in.

## Math fixes

- `Rect::Overlaps` is strict: rectangles that only share an edge or a corner do not overlap. A negative width or
  height is treated as the same rectangle flipped. (Editor marquee selection in the Animator, Behavior Tree and
  Dialogue windows no longer picks nodes the box merely touches.)
- `Mathf::SmoothStep(from, to, t)` returns 0..1 and accepts edges in either order: `SmoothStep(1, 0, t)` is a
  mirrored fade-out. Equal edges give a step.
- `Quat::ToEuler` only treats pitch within about 0.08 degrees of straight up or down as gimbal lock, so roll
  survives at 89 degrees.

## CommandLine

```cpp
CommandLine cl;
cl.Flag("windowed", "run in a window").Short('w');
cl.Value("width", "N", "window width", "1280");
cl.Value("cvar", "NAME=VALUE", "set a console variable (repeatable)");
cl.OptionalValue("find-match", "MODE", "matchmaking");
if (!cl.Parse(argc, argv)) { std::fputs(cl.Error().c_str(), stderr); return 1; }
int w = cl.GetInt("width");
for (const std::string& a : cl.GetAll("cvar")) Cvars::SetFromAssignment(a);
std::string scene = cl.Positional(0);
std::puts(cl.Usage("player", "Runs a game.").c_str());
```

Forms: `--flag`, `--key=value`, `--key value`, `-k`, `-k value`, grouped `-ab`, positionals anywhere, and `--`
(everything after is positional). A `Value` option always takes the next argument, even a negative number; an
`OptionalValue` takes it only if it does not start with a dash. `AllowUnknown()` keeps unknown `--options`
instead of failing. Typed getters: `GetInt`, `GetFloat`, `GetBool`, `GetAll`; `TryGetInt/TryGetFloat` check a range
and set `Error()`. Every getter reads the command line first, then the default the option was declared with, then the
default you pass (`GetBool` skipped the declared default before).

The player uses it to find the scene path (`player --windowed game.okayscene`, `--scene FILE`), and
`--log FILE` now works for a normal player, not only with `--server`.

## Paths

```cpp
Paths::SetAppName("MyGame");
std::string saves = Paths::UserDataDir();   // Windows %APPDATA%/MyGame, Linux $XDG_DATA_HOME/MyGame,
                                            // macOS ~/Library/Application Support/MyGame
Paths::ConfigDir(); Paths::CacheDir(); Paths::LogDir();
Paths::EnsureDir(Paths::LogDir());
Paths::SetAssetBaseDir(baseDir + "Data/");   // one place for the assets folder
std::string f = Paths::Resolve("scenes/a.okayscene");
Paths::AddAssetBaseDirListener([](const std::string& d) { Save::SetBaseDir(d); });
```

The `...For(platform, env, app)` functions compute any platform's folders from a given environment (used by the
tests). Existing per-class `SetBaseDir` calls keep working; a listener is the way to feed them from `Paths`.

## EventBus extras

The existing API is unchanged (a publish still dispatches to a snapshot of the handlers).

- `Subscribe<E>(handler, priority)`: higher priority runs first; same priority runs in subscription order.
- `SubscribeOnce<E>(handler)`: runs for the next event only.
- `Subscription s = bus.Listen<E>(handler)`: unsubscribes when `s` is destroyed. `Release()` keeps the handler.
- `bus.Queue(event)` stores a copy; `bus.Flush()` publishes everything queued (events queued during a flush wait for
  the next one). If a handler throws during `Flush`, the exception reaches the caller and the events behind it stay queued
  for the next `Flush` (they used to be dropped).
- `Subscription::Active()` is true only while the handler is really subscribed: after a once-only handler ran, after
  `Unsubscribe(token)` or `Clear()` it is false (it used to stay true). `Publish` leaves the bus usable when a handler
  throws (the "dispatching" state is released, so later unsubscribes are swept).
- The bus is single threaded. To raise an event from a worker, post it to the main thread:
  `JobSystem::Global().RunOnMainThread([&bus, e] { bus.Publish(e); });` and call `PumpMainThread()` each frame.

## Cvars and Console

```cpp
Cvar& shadows = Cvars::Int("r.shadows", 1, "shadow quality", 0, 3, /*persist*/ true);
Cvars::Bool("cheat.god", false, "take no damage");
shadows.OnChange([](const Cvar& c) { ApplyShadows(c.AsInt()); });

Console::Register("spawn", [](const std::vector<std::string>& a) { ...; return std::string("ok"); },
                  "spawn a thing", "spawn <name> [count]");
Console::Execute("r.shadows 2");      // set; "r.shadows" alone prints the value
Console::Execute("help");  Console::Execute("list r.");
Cvars::Load(path);  ...  Cvars::Save(path);   // persistent cvars that differ from their default
```

Values outside a range are clamped; text that does not parse leaves the value unchanged and reports an error.
A float cvar refuses `inf`, `nan` and numbers too big for a float (`1e39`); text too small for a double (`1e-999`) is just 0.
`AsInt` / `AsFloat` saturate instead of casting out of range (`AsInt` of a float cvar holding 1e20 gives `INT_MAX`).

### Flags: read only, cheat, restart required

```cpp
Cvars::String("build.version", "1.4").SetFlags(Cvar::kReadOnly);            // the player cannot change it
Cvars::Bool("cheat.god", false).SetFlags(Cvar::kCheat);                      // only while Cvars::SetCheatsEnabled(true)
Cvars::Int("gfx.msaa", 4, "anti-aliasing", 0, 8, true).SetFlags(Cvar::kRestartRequired);
...
Cvars::Load(settingsPath);
Cvars::MarkAllApplied();                       // the values in use at startup
...
for (const std::string& name : Cvars::RestartPending()) { /* "restart to apply: gfx.msaa" */ }
```

- The flags guard text input: `Cvar::Set(text)`, `Cvars::Set`, `SetFromAssignment` (`--cvar name=value`), the console
  (a bare `name value`, `set`, `toggle`, `reset`) and settings files (`Cvars::Load`, including a value loaded before the
  variable was registered). `cvar.CanSetFromText(&why)` asks first. Code is trusted: `SetInt`, `SetFloat`, `SetBool`,
  `SetString`, `Reset` and `Cvars::ResetAll` ignore the flags, so the engine can publish a read-only value.
- `kCheat` variables are not saved, and neither are read-only ones. Turning cheats off again does not undo values already set.
  `Cvars::Clear()` turns them off.
- `kRestartRequired`: it can be changed any time; `RestartPending()` is true while its value differs from the one in use.
  The console prints `(takes effect after a restart)`, `list` / `get` / `help` show the flags.
Built-ins: `help [name]`, `list [prefix]`, `set`, `get`, `toggle`, `reset`, `history`, `echo`, `exec <file>`. Words are split on
spaces; double quotes group. `Console::History()`, `Lines()` (the output log) and `Complete(prefix)` feed a UI.
`Cvars::Save` writes atomically and keeps values for names not registered yet. A command may run another line with
`Console::Execute`; nesting deeper than 32 levels gives an `error:` result instead of overflowing the stack (a command
that runs itself used to crash the program). A settings file or script saved with a UTF-8 byte order mark loads its first
line (`Cvars::Load`, `Console::ExecuteScript`, `exec`). `exec <file>` runs the lines of a script file (an autoexec): a relative
path is looked up under `Paths::AssetBaseDir()` and then as given, a byte order mark is skipped, `#` and `//` lines and blank lines
are ignored, every other line runs even if one failed, and it answers `ran N lines (M failed)`; a script that runs itself ends at
the nesting limit.

### Debug overlay

`DebugOverlay::BuildLines(DebugOverlay::Capture(&scene))` returns `"FPS: 60.0 (16.7 ms)"`, `"Objects: 42"` and any
lines added with `AddLine`. The UI draws them. The overlay is switched by the `debug.overlay` cvar.

## Stopwatch, StateMachine, BuildInfo

- `Stopwatch`: `Start/Stop/Reset/Restart/Lap/Elapsed`. Real clock by default; `Stopwatch::GameTime()` follows
  `Time::ElapsedTime()` so it stops while the game is paused.
- `StateMachine<State>`: `Add(state, {onEnter, onUpdate, onExit})`, `Start`, `Change`, `Update(dt)`, optional
  `Allow(from, to)` transition rules, `OnTransition`, `TimeInState`. A change made from inside an enter or an exit
  callback runs after the transition it was made in has finished (a `Change` from an exit callback used to call the
  exit callback again, without end).
- `BuildInfo::Version()` (the CMake project version, from `OKAY_ENGINE_VERSION`), `Compiler()`, `Config()`,
  `Platform()`, `Architecture()`, `Summary()`, `CompareVersions`, `IsCompatible`.

## What changed (core wave 6)

Fixed, each with a test in `core_utils_audit` (and `core_events_ecs` for the bus): `CommandLine::GetBool` ignoring a declared
default; `Cvar::AsInt` / `AsFloat` casting out of range (undefined behaviour); a float cvar accepting `inf` and refusing `1e-999`;
`Console::Execute` recursing without limit; a byte order mark hiding the first line of a settings file or script; the
`StateMachine` exit-callback recursion; a `DebugOverlay` line provider that adds or removes lines (a one-shot line removing itself)
while the overlay was being built walked a list it was changing (new lines now show from the next call); `Subscription::Active()` staying true after the handler was gone; an `EventBus`
handler that throws leaving the bus "dispatching" and `Flush` dropping the queue behind it.

Added: cvar flags `kReadOnly`, `kCheat`, `kRestartRequired` with `Cvars::SetCheatsEnabled`, `RestartPending`,
`MarkAllApplied` (the follow-up the feature map listed for `Cvar`), and the `exec <file>` console command.

Looked at and found fine: the command-line forms above (empty and null arguments, a lone `-`, `--`, a negative value, a
value too big for an int), `SlotMap` generations, `StringId` collision handling, `FrameLimiter` and `FrameTimer` with
bad clocks, `Stopwatch` with a source that goes backwards. Not done: `Cvars` and `Console` are main-thread only by
design (no lock was added); a cvar name with a space or a leading `#` cannot survive the settings file (the console
cannot type one either); there is still no in-game console window (the editor's Console panel and a game's own UI call
`Console::Execute`).
