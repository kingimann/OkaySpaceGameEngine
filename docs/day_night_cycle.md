# Day / Night Cycle

`DayNightCycle` advances a 24-hour clock and drives the scene's sun and sky so it
slowly turns day → dusk → night → dawn — no scripting.

## Setup

1. Make a **Sun**: an object with a **Light** (Directional). 
2. Add a **Camera** (it becomes the scene's main camera).
3. Drop **Add Component ▸ Gameplay ▸ Day / Night Cycle** on any object. Press Play.

It drives the sibling Light if the cycle is on the Sun, otherwise the first Light in
the scene, plus the main camera for the sky.

## What it does each frame

- Advances `time` (hours, 0–24); a full day takes `dayLengthSeconds` of real time.
- **Sun light**: rotates across the sky (noon overhead), brightens toward
  `dayIntensity` at noon and dims to `nightIntensity` at night, warm at sunrise/
  sunset and cool (moonlight) deep at night. Ambient floor eases between
  `dayAmbient` and `nightAmbient`.
- **Sky**: blends the main camera's background through `skyDay` → `skyHorizon`
  (sunrise/sunset orange) → `skyNight`.
- Publishes the current hour to a saved value `hour` — bind a UI text with
  `{hour}` for a clock readout.

## Fields & API

| Field | Meaning |
|-------|---------|
| `dayLengthSeconds` | real seconds for a full 24h day (e.g. 120) |
| `time` | current hour, 0–24 |
| `paused` | stop the clock (set `time` by hand) |
| `controlSun` / `rotateSun` | drive the sun light / rotate it across the sky |
| `controlSky` | drive the camera background |
| `dayIntensity` / `nightIntensity`, `dayAmbient` / `nightAmbient` | light range |
| `dayLight` / `nightLight` | sun tint at noon / midnight |
| `skyDay` / `skyHorizon` / `skyNight` | sky gradient stops |

Methods: `SetTime(hour)`, `AddHours(h)`, `Pause(on)`, `Hour()`, `IsNight()`,
`Elevation()` (−1 midnight … +1 noon).
