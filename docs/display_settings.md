# Display settings

`okay::DisplaySettings` (`okay/Core/DisplaySettings.hpp`) holds what the Settings screen
changes about the window and the picture: the **window mode**, the **resolution**, **vsync**
and the **quality preset**. It is plain data: nothing in it needs a window, so a settings
screen, a tool or a test can use it before a window exists. A host (the player, the editor's
Play mode) watches `DisplaySettings::Generation()`, which goes up on every change, and applies
what it finds. Tests: `display_settings`.

```cpp
DisplaySettings::SetMode(WindowMode::Borderless);   // Windowed, Borderless or Fullscreen
DisplaySettings::SetResolution(1920, 1080);         // clamped into 640x360 .. 16384x16384
DisplaySettings::SetVSync(false);
DisplaySettings::SetQuality(1);                     // 0 Low, 1 Medium, 2 High
```

Everything is saved in Prefs (`video.windowmode`, `video.width`, `video.height`, `video.vsync`,
`video.quality`, and the older `video.fullscreen`, which is 1 in both full-screen modes so
older builds still go full screen). `DisplaySettings::Save()` writes them (each Set does) and
the host writes the prefs file as it does for the other settings.

## Window modes

| `WindowMode` | Meaning | SDL flag a host uses |
| --- | --- | --- |
| `Windowed` | a normal window of the chosen size | none |
| `Borderless` | a borderless window over the whole desktop | `SDL_WINDOW_FULLSCREEN_DESKTOP` |
| `Fullscreen` | exclusive full screen at a listed resolution | `SDL_WINDOW_FULLSCREEN` with the display mode of `EffectiveResolution()` |

`Modes()` lists the three, `ModeName(mode)` and `ParseMode(text, mode)` convert to and from
`"windowed"`, `"borderless"` and `"fullscreen"` (any case; `"window"` and `"fullscreen_desktop"`
are accepted too). `Fullscreen()` is true in both full-screen modes, and `SetFullscreen(true)`
means `Borderless` (what the older on/off switch drew: a desktop-size full screen window),
so the existing Settings screen toggle and the Alt+Enter key keep working as before.

## Resolutions

`Resolutions()` is the list a settings screen offers, sorted small to large with no repeats.
Without help it is a standard list (800x600, 1024x768, 1280x720, 1280x800, 1366x768, 1440x900,
1600x900, 1680x1050, 1920x1080, 1920x1200, 2560x1080, 2560x1440, 2560x1600, 3440x1440,
3840x2160). A host that knows the display gives it better data:

```cpp
DisplaySettings::SetDesktopResolution(dm.w, dm.h);                  // the list stops there and includes it
DisplaySettings::SetSupportedResolutions({{1280, 720}, {1920, 1080}});   // or the monitor's own mode list
```

Entries outside the allowed range and repeats are dropped; an empty list goes back to the
standard one. `SetResolutionIndex(i)` chooses from the list, `ResolutionIndex()` says where
the current size is in it (-1 when it is not listed), `NearestResolution(w, h)` finds the
listed size closest to any size, `ClampResolution(w, h)` forces a size into the allowed
range, and `ClearResolution()` goes back to the host's own default size. A size in range is
kept as given (a window may be any size); `HasResolution()` is false until one was chosen.

What a host applies is `EffectiveResolution()`:

| Mode | Result |
| --- | --- |
| Windowed | the chosen size |
| Borderless | the desktop's size when `SetDesktopResolution` was called, else the chosen size |
| Fullscreen | the listed size nearest to the chosen one (the desktop's when none is chosen) |

It is `0 x 0` when nothing is chosen and the desktop is unknown: keep the host's own size.

## Quality presets

`SetQuality(q)` (0, 1, 2; other numbers are clamped) applies the level to the renderer's shared
switches and bumps `Generation()`. The table, `DisplaySettings::Preset(q)`:

| | Low | Medium | High |
| --- | --- | --- | --- |
| shadows (`ShadowsEnabled`) | off | on | on |
| shadow map size (`ShadowMapResolution`) | 1024 | 2048 | 4096 |
| shadow distance (`ShadowDistance`) | 40 | 80 | 120 |
| shadow cascades (`ShadowCascades`) | 1 | 3 | 4 |
| shadow softness (`ShadowSoftness`) | 1.0 | 2.5 | 3.0 |
| per-pixel lighting | off | off | on |
| bloom, ambient occlusion | off | off | on |
| FXAA | off | on | on |

Medium is the look the renderer ships with. Shadows, the shadow map size, distance and
cascades, bloom and ambient occlusion are read by the software renderer and the GPU backends
(OpenGL and Direct3D 11 through `ShadowSetup.hpp`; Direct3D 12 reads the map size); FXAA and
per-pixel lighting only change the software path.

The preset is applied once the **player chose a level** (`QualityChosen()`: `SetQuality` was
called, or a saved `video.quality` was loaded). Until then `ApplyQuality()` does nothing, so
a game's own render settings (Build Settings: shadow distance, bloom...) are not overwritten the
first time the player toggles vsync. `ApplyPreset(q)` applies a level regardless.

## Saved settings at start-up

Call `DisplaySettings::Restore()` once after `Prefs::Load`. It reads the saved values and, when
there are any, bumps `Generation()` so a host that watches it applies them on its first frame
(the player does this). Without saved values nothing changes. `Load()` alone reads the values
without bumping the generation. Bad saved values are ignored or clamped: an unknown window mode
falls back to the older `video.fullscreen` key, a size that is not two positive numbers is
dropped, a quality outside 0 to 2 is clamped.

## Hooking a host

The player reads `Generation()` each frame and applies fullscreen, vsync and quality; it now
calls `Restore()` at start. A host that wants the window mode and size applies them in the same
place:

```cpp
const okay::DisplayResolution r = okay::DisplaySettings::EffectiveResolution();
switch (okay::DisplaySettings::Mode()) {
    case okay::WindowMode::Windowed:
        SDL_SetWindowFullscreen(window, 0);
        if (r.Valid()) SDL_SetWindowSize(window, r.width, r.height);
        break;
    case okay::WindowMode::Borderless:
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
        break;
    case okay::WindowMode::Fullscreen: {
        SDL_DisplayMode want{0, r.width, r.height, 0, nullptr}, got;
        if (r.Valid() && SDL_GetClosestDisplayMode(0, &want, &got)) SDL_SetWindowDisplayMode(window, &got);
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);
        break;
    }
}
```

The player and the editor do this with `shared/SdlDisplays.hpp` (`okay::sdldisplay::ListMonitors`
and `Apply`). That helper talks to SDL directly. It is not the `okay::platform` seam, and it
does not turn the own-code window backend on. SDL stays the default. A host that is not on
SDL (the own backend, chosen only with `OKAY_PLATFORM_BACKEND=own`) still toggles borderless
fullscreen and does not get a monitor list.

## Monitors and refresh rate

`DisplaySettings::SetMonitors` takes the list from the helper. Each monitor has a name, a
place on the desktop, the desktop size, and its modes (`width`, `height`, `refreshHz`).
Modes outside 640x360 .. 16384x16384, or with a refresh rate below 0 or above 1000, are
dropped. The rest are sorted small to large, then by rate, with exact repeats removed.

`SetMonitorIndex` / `SetMonitorName` choose one (the name matches any case). `RefreshHz` /
`SetRefreshHz` is the rate in hertz; 0 means the monitor may choose. `Resolutions()` for the
Settings screen is that monitor's sizes. `RefreshRates()` is the rates listed for the chosen size.

`ResolveSaved()` runs after the list is known. A saved monitor name that is not connected
falls back to the first monitor. A saved size that monitor does not list falls back to the
nearest listed size (the smaller one when two are equally close). A saved rate that is not
listed falls back to the nearest listed rate (the lower one on a tie). Nothing saved stays
nothing: the host keeps the size it opened with.

## Keep these settings?

Apply on the Settings screen remembers the current choice and starts a 10-second clock
(`BeginTrial`). The screen asks "Keep these settings?" and counts down. Keep
(`KeepTrial`, or Done) stores the new choice. Revert, or the clock running out
(`TickTrial`), puts the previous monitor, mode, size and rate back and the host applies
that because `Generation()` moved.

The choice is saved with `video.pending=1` and `video.prev.*` while the question is open.
If the game quits before Keep or Revert, the next launch (`Restore`) throws the unconfirmed
choice away and uses the previous one.

## The Settings screen

The shipped screen (`UITemplates::AddSettings`, driven by `SettingsScreen`) has Monitor,
Display mode (Windowed, Borderless, Fullscreen), Resolution, Refresh rate, VSync, Quality
and Apply, plus the audio and accessibility rows and Done. The four display rows wait for
Apply. VSync, quality, volume and the accessibility rows still change as you move them.
An older scene that still has a Fullscreen switch keeps working: on means borderless.

## Scripts

Text scripts, the visual-script nodes `Fn.*` (titles such as Get Monitor, Set Display Mode,
Set Refresh Rate, Begin Display Confirm) and the action-list steps read and set the same
choice. `begin_display_confirm` starts the 10-second question; `keep_display_settings` and
`revert_display_settings` answer it. `set_display_mode` takes `windowed`, `borderless` or
`fullscreen`.

The pure rules (filtering, the nearest mode, the fallback, the saved text, the clock) live
in `okay/Core/DisplayModes.hpp` and do not need a window. Tests: `display_settings`,
`display_screen`.

## What changed

- **Quality presets drive the shadows.** `ApplyQuality` only flipped shadows, per-pixel lighting,
  bloom, ambient occlusion and FXAA; the shadow map size, distance, cascades and softness stayed
  where they were (the feature map's "the preset does not change shadow resolution or
  distance"). They are part of the table now.
- **Window mode and resolution** are new (see above); `Fullscreen` / `SetFullscreen` keep their
  meaning.
- **A preset no longer overwrites Build Settings by accident.** Any change (vsync, fullscreen)
  made the host call `ApplyQuality()`, which applied High over a game's own render settings; it
  now applies only a level the player chose. A level that is already the default (High) is
  applied when chosen, and a level nobody chose is no longer saved to the prefs.
- **Saved settings reach the window.** The player never read `DisplaySettings` at start-up, so a
  saved fullscreen choice only applied after the Settings screen was opened; `Restore()` fixes
  that.
- **Monitor, resolution, refresh and a 10-second revert.** The Settings screen lists the connected monitors and their modes. Apply asks "Keep these settings?" and reverts if nobody answers. A quit during that question restores the previous choice next launch.
- Tests: `display_settings`, `display_screen`.
