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
and set `Error()`.

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
  the next one).

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
Built-ins: `help [name]`, `list [prefix]`, `set`, `get`, `toggle`, `reset`, `history`, `echo`. Words are split on
spaces; double quotes group. `Console::History()`, `Lines()` (the output log) and `Complete(prefix)` feed a UI.
`Cvars::Save` writes atomically and keeps values for names not registered yet.

### Debug overlay

`DebugOverlay::BuildLines(DebugOverlay::Capture(&scene))` returns `"FPS: 60.0 (16.7 ms)"`, `"Objects: 42"` and any
lines added with `AddLine`. The UI draws them. The overlay is switched by the `debug.overlay` cvar.

## Stopwatch, StateMachine, BuildInfo

- `Stopwatch`: `Start/Stop/Reset/Restart/Lap/Elapsed`. Real clock by default; `Stopwatch::GameTime()` follows
  `Time::ElapsedTime()` so it stops while the game is paused.
- `StateMachine<State>`: `Add(state, {onEnter, onUpdate, onExit})`, `Start`, `Change`, `Update(dt)`, optional
  `Allow(from, to)` transition rules, `OnTransition`, `TimeInState`. A change made from inside an enter callback
  runs after that callback returns.
- `BuildInfo::Version()` (the CMake project version, from `OKAY_ENGINE_VERSION`), `Compiler()`, `Config()`,
  `Platform()`, `Architecture()`, `Summary()`, `CompareVersions`, `IsCompatible`.
