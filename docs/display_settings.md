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

and fills the list once the window exists (`SDL_GetDesktopDisplayMode`, `SDL_GetNumDisplayModes`
into `SetDesktopResolution` and `SetSupportedResolutions`). The Settings screen component
shows the three older controls (fullscreen toggle, vsync toggle, quality dropdown); dropdowns
for resolution and window mode bound to `Resolutions()` and `Modes()` are the next step.

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
- Tests: `display_settings` (there was none).
