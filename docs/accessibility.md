# Accessibility

The engine side of accessibility. What a player can change today, and what really happens
(each row is on the Settings screen, tested, and saved to Prefs when Done is pressed):

| Settings screen row | What it does in the player |
|---|---|
| Text size (75%, 100%, 125%, 150%, 200%) | Screen-space Text objects (their wrap width too), button, toggle, keybind, tooltip and context-menu labels draw that much larger. Best Fit text already fills its box and is not scaled again. World text is not scaled. Slider, dropdown, input and progress text keep their size; widgets do not resize, so a long label can overflow its box |
| Colour filter (Off, Protanopia, Deuteranopia, Tritanopia) | The rendered 3D frame (every renderer: it is the pixel buffer the player uploads) and every flat colour and text colour of the screen UI are daltonized (`DaltonizeRgba8`, `Accessibility::FilterColor`). UI images are not filtered |
| High contrast | UI text is pushed to 7:1 against the last large solid fill drawn before it (the panel or button under it); text over the bare scene is measured against near black. A panel with a thin gradient or image under the text can be misjudged |
| Reduce motion | `Camera::Shake` is ignored and a shake in progress stops. UI Animation slides, pop, drop and grow become a quarter-length fade and its loops (pulse, breathe, bob, sway) hold still |
| Reduce flashing | The `flash` action does nothing and the UI Animation blink loop holds still |
| Subtitles | Spoken lines (cutscene subtitles and dialogue) are drawn by the caption widget. Off, those lines are not shown |
| Sound captions | While on, an Audio Source with a caption shows it, for example `[door slams]`. Off by default |
| Hold to toggle | Sprint, aim and crouch stay on after the key is released, until the next press. Aim and sprint on the action list do the same. Off, they are holds |
| Aim assist (Off, Low, Medium, High) | While the right stick is moving, the view is pulled a short way toward a nearby living target. Medium is the default. Off, the stick is not pulled. Mouse look is never pulled |
| High-visibility focus | The keyboard and gamepad focus ring becomes a thick yellow stroke with a dark halo, instead of the theme's ring |
| Focus thickness (0.5 to 3) | Multiplies the theme's focus-ring thickness. High-visibility focus multiplies again, and never goes under 6 pixels |

Everything else below is plain engine API. Settings that have no reader yet are not offered on
the Settings screen: see "Not done" at the end.

The rest of this page: subtitles and captions, colour vision support,
controller and keyboard navigation of UI, a controls-screen model with conflict
detection, and one settings object a player sets once and the whole game honours.
Everything is plain C++ (`#include <Okay.hpp>`). `CaptionOverlay` draws the shared
caption lines. The focus ring is drawn by the UI renderer (see Focus ring below). The
navigation helpers only choose which widget is focused.

| Header | What it has |
|---|---|
| `okay/Core/AccessibilitySettings.hpp` | `AccessibilitySettings`, the `Accessibility` service, `HoldToggle` |
| `okay/Core/Subtitles.hpp` | `CaptionManager`, `SubtitleTrack`, `SubtitleSettings` |
| `okay/Graphics/ColorVision.hpp` | colour vision simulation and correction, the safe palette, contrast |
| `okay/UI/FocusGraph.hpp` | `FocusGraph`: directional focus, tab order, wrap, traps |
| `okay/Components/UINavigation.hpp` | `NavigateUI` (existing), `BuildFocusGraph`, `IsNavFocusable` |
| `okay/Input/RemapModel.hpp` | the controls list, conflict detection, `RemapBind` |

## The settings

```cpp
okay::Prefs::Load("game.okayprefs");
okay::Accessibility::Load();               // the player's saved choices

okay::Accessibility::AddListener([](const okay::AccessibilitySettings& now, std::uint32_t changed) {
    if (changed & okay::kA11yTextScale) RelayoutUi(now.textScale);
});

okay::Accessibility::SetTextScale(1.25f);  // from an options menu; listeners hear it
okay::Accessibility::Save();               // into Prefs (Prefs::Save writes the file)
```

| Setting | Prefs key | Meaning |
|---|---|---|
| `textScale` | `a11y.textScale` | UI text multiplier, 0.75 to 2 |
| `highContrast` | `a11y.highContrast` | text is pushed to at least 7:1 contrast with its background |
| `reduceMotion` | `a11y.reduceMotion` | no camera shake, sway or parallax; short transitions |
| `reduceFlashing` | `a11y.reduceFlashing` | no strobing or full-screen flashes |
| `colorblind`, `colorblindStrength` | `a11y.colorblind`, `a11y.colorblindStrength` | the player's colour vision (`none`, `protanopia`, `deuteranopia`, `tritanopia`, `achromatopsia`) and how far colours are shifted, 0 to 1 |
| `subtitles` | `a11y.subtitles.*` | see below |
| `toggleAllHolds`, `toggleActions` | `a11y.toggleAll`, `a11y.toggle` | hold-to-toggle for every action, or for named ones (`Sprint;Aim`) |
| `aimAssist` | `a11y.aimAssist` | `off`, `low`, `medium` (default), `high` |
| `screenReaderHints` | `a11y.screenReader` | announce menus and events to a screen reader |
| `focusHighVisibility` | `a11y.focusHighVis` | thick yellow focus ring with a dark halo |
| `focusThicknessScale` | `a11y.focusThickness` | 0.5 to 3, multiplies the theme's focus-ring thickness |

`Accessibility::Set` clamps the values, does nothing when nothing changed, and
tells each listener which parts changed (`kA11yTextScale`, `kA11yHighContrast`,
`kA11yReduceMotion`, `kA11yReduceFlashing`, `kA11yColorblind`, `kA11ySubtitles`,
`kA11yHoldToggle`, `kA11yAimAssist`, `kA11yScreenReader`, `kA11yFocusVisual`, combined with `|`). Each
setting also has its own setter. It also hands the subtitle settings to the shared
`CaptionManager::Main()`. Use it from the main thread.

What games ask the service:

```cpp
float px      = Accessibility::ScaleText(18.0f);          // text size times the player's scale
float shake   = amount * Accessibility::MotionScale();    // 0 with reduced motion
float fade    = Accessibility::TransitionSeconds(0.5f);   // a quarter as long with reduced motion
float flash   = intensity * Accessibility::FlashScale();  // 0 with reduced flashing
Color tint    = Accessibility::FilterColor(teamColor);    // shifted for the player's colour vision
Color text    = Accessibility::TextColor(fg, panelBg);    // pushed to 7:1 with high contrast on
bool toggles  = Accessibility::ToggleFor("Sprint");
float assist  = Accessibility::AimAssistStrength();       // 0, 0.35, 0.65 or 1: a multiplier for your own assist
```

### Hold or toggle

`HoldToggle` turns a held input into an action's state for either kind of player:

```cpp
okay::HoldToggle sprint;                                    // one per action
bool sprinting = sprint.Update(InputActions::Get("Sprint"), Accessibility::ToggleFor("Sprint"));
if (outOfStamina) sprint.Cancel();                          // stop a toggled sprint without another press
```

In hold mode the action is active while the input is down; in toggle mode each press
flips it.

### Screen reader hints

```cpp
Accessibility::SetAnnouncer([](const std::string& text) { MyPlatformSpeech(text); });
Accessibility::Announce("Main menu, Play button");   // said only when hints are on and an announcer is set
```

The engine doesn't talk to a platform screen reader itself; the announcer is where a
game plugs one in. Announce menu titles, focus changes and important events through it.

## Subtitles and captions

```cpp
auto& captions = okay::CaptionManager::Main();      // or your own CaptionManager

captions.Say("Anna", "Did you hear that?", clip);    // lasts as long as the audio clip
captions.Say("Guard", "Halt!", 1.8f, /*priority*/ 2);
captions.Sound("door slams");                        // "[door slams]", only with sound captions on
captions.Music("tense music");

// every frame
captions.Update(dt);
for (const okay::SubtitleDrawLine& line : captions.Lines()) {
    // line.speakerLabel ("Anna: "), line.rows (wrapped text), line.speakerColour, line.textColour,
    // line.backgroundColour, line.alpha, line.scale, line.italic
}
```

`Lines()` is the draw list, oldest first. Colours already carry the player's text and
background opacity and the fade, `rows` are wrapped to the player's line width, and
`scale` is their text size. Where on screen to draw is up to you: `settings.top` is
the player's choice of top or bottom.

**Timing.** A line lasts `duration` seconds when you give one, else the audio's
length plus a quarter second when you give `audioSeconds` (or an `AudioClip`), else a
reading time: 15 character cells a second, at least 1.5 and at most 8 seconds. It fades in
over 0.12 s and out over 0.25 s (`fadeIn`, `fadeOut`, `linger` are members).

**Queue and priority.** Up to `settings.maxLines` lines show at once. More wait; the
highest priority waits at the front, equal priorities in the order they came. A line
with a higher priority than the lowest line on screen takes that line's place (the
displaced line is dropped, `onDropped` fires). A line that waits longer than its
`maxDelay` (2 s) is dropped as stale; the queue holds `maxQueue` (16) lines, the lowest
priority and latest going first. `Cancel(id)`, `CancelTag(tag)` and `Clear()` remove lines.
Sound and music captions default to priority -1, below speech. The same caption shown
again while it is up only restarts its time.

**Settings the player chooses** (`SubtitleSettings`, saved under `a11y.subtitles.*`):
`enabled` (speech subtitles), `soundCaptions` (sound and music descriptions for deaf
and hard-of-hearing players, off by default), `speakerNames`, `speakerColours`,
`textScale` (0.5 to 3), `background`, `backgroundOpacity`, `textOpacity`, `maxLines`
(1 to 6), `maxCharsPerLine` (16 to 120), `top`. `Load()` and `Save()` go through Prefs.

**Speakers.** Each new speaker takes the next colour of the colour-vision-safe palette
(below); `SetSpeakerColour("Anna", color)` overrides, `hasColour` on a line colours that
line's label alone. The text itself stays white so it reads on any background; the colour
is on the speaker's label.

**Localized lines.** `SubtitleLine::textKey` names a string-table key; its text in the
current language is used when the table has it, else `text`.

**Tracks.** A `SubtitleTrack` is lines with start times:

```cpp
okay::SubtitleTrack track;
okay::SubtitleTrack::ParseSrt(srtText, track);       // SubRip: "Anna: Hello" gives a speaker, "[door slams]" a sound caption
track.Add(2.0f, "Bo", "After two seconds", 1.5f);
track.AddSound(3.0f, "wind howls");
int handle = captions.Play(track);                    // cues show as captions.Update's clock passes them
captions.Stop(handle);
std::string srt = track.ToSrt();
```

`Play(track, startAt)` can start part way in: a cue already running shows for what is
left of it. Time is whatever you pass to `Update`, so a track follows a cutscene or
audio clock if you feed it that.

## Colour vision

```cpp
Color seen    = SimulateColorblind(color, ColorblindMode::Deuteranopia);   // what a deuteranope sees
Color fixed   = DaltonizeColor(color, ColorblindMode::Deuteranopia);       // shifted so it is easier to tell apart
SimulateColorblind(image, ColorblindMode::Protanopia);                     // an Image, in place
Image corrected = DaltonizedImage(image, ColorblindMode::Deuteranopia);    // a copy
```

* **Simulation** uses the Machado, Oliveira and Fernandes (2009) matrices for
  protanopia, deuteranopia and tritanopia (`ColorblindMatrix(mode)`, a 3x3 on linear
  light; rows add to 1 so white stays white) and luminance for achromatopsia.
  `severity` 0 to 1 blends from normal vision to the full matrix, a linear blend
  that approximates the paper's own partial matrices. Alpha is never changed.
  Use it to check your art.
* **Correction** ("daltonization") moves what a person with the deficiency loses (the
  difference between a colour and its simulation) into channels they still see:
  protanopia and deuteranopia into green and blue, tritanopia into red and green.
  `strength` scales it. Achromatopsia has no hue to recover, so the colour comes back
  unchanged. Correction helps the red and green pairs that merge for the first two
  most; for tritanopia it is mild. It is a per-colour tweak, not a replacement for
  not relying on colour alone: pair colours with shapes, icons or labels.
* **`SafePalette()`** is 12 colours that stay at least about 20 delta E apart under
  normal vision, protanopia, deuteranopia and tritanopia (measured with
  `MinColorSeparation(colors, mode)`), light enough for dark backgrounds (3.5:1 or
  more against black), ordered so the first few are also well separated (use the
  first 4 or 6 for few teams). With no colour vision only brightness is left and
  twelve colours can't all differ in it: pair them with a shape or label.
  `SafePaletteColor(i)` wraps around.
* **Contrast**: `ContrastRatio(a, b)` (WCAG, 1 to 21), `RelativeLuminance`,
  `EnsureContrast(fg, bg, 4.5)` moves a colour towards white or black until it reaches
  the ratio (4.5 is AA for text, 3 for large text, 7 is AAA), and `ColorDistance`
  (CIE76 delta E; about 2.3 is just noticeable, 10 or more reads as different at a
  glance).

The filters work on colours and images; applying one to the whole rendered frame is a
renderer job (an image in, an image out per frame is too slow on the CPU for the
full screen; a GPU filter would use `ColorblindMatrix`).

## Controller and keyboard navigation

`NavigateUI(scene)` (Up/Down/Left/Right, WASD or the D-pad, Enter or A to activate)
already moves focus between a scene's buttons, toggles, sliders, dropdowns, tabs and
keybind fields; see [game_ui.md](game_ui.md). `FocusGraph` is the same idea without the
scene: rectangles in, "what gets focus next" out. Use it for custom UI, or to get tab
order, explicit links and focus traps.

```cpp
okay::FocusGraph g;
int play = g.Add(40, 100, 200, 48);
int quit = g.Add(40, 160, 200, 48);

g.SetFocus(play);
g.Navigate(okay::FocusDir::Down);                // -> quit
g.Navigate(okay::FocusDir::Down);                // -> play (wraps)
```

* **Directional moves** score each other rectangle by the distance ahead between
  centres plus twice the sideways gap (`FocusScore`), so the widget straight below a
  left-aligned entry beats a closer one off to the side. `NavigateUI` uses the same
  function. A move with nothing that way wraps as `FocusOptions` say: `Edge` (default)
  comes out the opposite side in the same row or column, `ReadingOrder` goes to the first
  or last in reading order (what `NavigateUI` does going up and down), `None` stays.
* **Tab order** (`FocusDir::Next` / `Previous`): nodes with a positive `tabIndex` first,
  lowest first, then `tabIndex` 0 in reading order (rows top to bottom with a small
  tolerance, left to right), negative `tabIndex` skipped (arrows still reach them).
  `TabOrder()` and `ReadingOrder()` list them; Tab wraps unless `tabWraps` is off.
* **Focus traps** for modals: give the modal's nodes a `group`, then `PushTrap(group)`.
  Only that group takes focus, focus moves into it, and `PopTrap()` returns focus to
  where it was (or the first node if that is gone). Traps nest.
* **Explicit links**: `SetLink(id, FocusDir::Right, otherId)` overrides the geometry
  for one direction; `-1` blocks it. A link to a disabled node is ignored.
* **Disabled nodes** never take focus (`SetEnabled`); disabling the focused node clears focus.
  From no focus, Down or Next starts at the first node and Up or Previous at the last.

`BuildFocusGraph(scene, graph)` in `UINavigation.hpp` fills a graph from the scene's
focusable widgets (visible, not behind an open modal, the same set `NavigateUI` uses,
at their screen rectangles) and returns the objects, indexed by node id minus 1.

### Focus ring

The UI renderer draws one ring around the focused widget. It appears after a keyboard or
gamepad move, or after a script sets focus, and a mouse click hides it until the next
one of those. The focused widget is scrolled into view, including when a script sets focus.

```cpp
okay::SetUIFocus(scene.Find("Play"));          // ring on, scrolled into view
// from a text script or a visual-script node Fn.ui_set_focus:
//   ui_set_focus("Play")
```

The ring's colour, thickness, padding and optional pulse come from the UI theme on the
widget (or an ancestor, or a global theme, or the first theme in the scene). Each built-in
look has its own ring. Reduce motion and reduce flashing hold a pulse still.

High-visibility focus (`Accessibility::SetFocusHighVisibility`) replaces that ring with a
thick yellow stroke and a dark halo. Focus thickness (`SetFocusThicknessScale`, 0.5 to 3)
multiplies the theme thickness first; high-visibility then doubles it and keeps it at
least 6 pixels. Both are on the Settings screen (High-vis focus, Thickness) and saved
under `a11y.focusHighVis` and `a11y.focusThickness`. A Settings screen saved before those
rows gains them the next time it opens.

## Remapping

`RemapModel.hpp` is what a Controls screen needs, on top of `InputActions` (see
[input.md](input.md)); rebinding still goes through `InputActions::Rebind` and is
saved in Prefs.

```cpp
for (const okay::RemapRow& row : okay::RemapRows(options)) {
    // row.action, row.direction, row.reserved
    // row.keyboard / row.gamepad: bound, label ("Space", "Left Stick"), isDefault, partLabels (W S A D)
}

auto taken = okay::RemapConflicts("Jump", "key:e");        // who else uses it
okay::RemapResult r = okay::RemapBind("Jump", "key:e", okay::RemapResolve::Swap);
```

`RemapOptions` can list `reserved` actions (can't be rebound: "Pause") and `hidden` ones.
`RemapConflicts(action, binding, part)` lists the other actions using an input: the
same key, button or stick; a key that is one of a four-key direction's keys (`part` tells
which); the D-pad and its buttons. `RemapAllConflicts()` lists every pair that shares an
input right now, for a warning badge.

`RemapBind(action, binding, how, part)` rebinds, dealing with a taken binding as the
player chose:

* `Cancel`: change nothing and return the conflicts (`r.ok` false, `r.error` says who has it).
* `Swap`: the other action gets the binding this one gave up (a key swaps with a key of a
  four-key direction too).
* `Unbind`: the other action loses it, as long as it keeps another binding (a saved action
  with no bindings would load back as its defaults).
* `Allow`: both use it.

An action in `reserved` can't be moved by Swap or Unbind. `part` 0 to 3 rebinds one key of a
four-key direction (up, down, left, right); its own other keys count as conflicts.
`RemapDirectionChoices()` lists what a stick or D-pad direction action can take.

Not covered yet: the navigation keys of `NavigateUI` (WASD, Enter, Space, the D-pad and A)
are fixed in code, so UI navigation itself can't be remapped.

## Not done

These settings exist in `AccessibilitySettings` but nothing reads them yet, so the Settings
screen does not show them:

* **Screen reader hints**: no platform announcer reads them.
* **Reduce motion** does not yet change first person head bob, the landing dip, parallax or sprite sway;
  only camera shake and UI animation.
* **Achromatopsia** has no correction (a grayscale user has no hue to recover), so it is not offered as a filter.
* **okayui** (the editor and launcher chrome) does not read the settings; only the in-game UI does.
* Subtitle size, background, speaker names and the top-or-bottom choice are saved with the other
  subtitle settings and the caption widget honours them, but the Settings screen only offers
  subtitles on or off and sound captions on or off.
* A Settings screen saved in a scene before this change lacks the new rows until the prefab is added again
  (the bindings skip rows that are missing).
* The editor has no accessibility windows or colour-blindness preview of the Game view.
