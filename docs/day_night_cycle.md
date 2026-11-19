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

## The calendar, speed and events (time upgrade)

The same component is the game's clock and calendar. The hours are kept in a double inside, so a day stays exact
at any frame rate (a float counter lost about 1 percent of the day at 144 fps and long days).

| Field | Meaning |
|-------|---------|
| `day` | day counter, 1 on the first day, +1 every midnight |
| `daysPerSeason` | days in a season: Spring, Summer, Autumn, Winter (default 30) |
| `speed` | multiplies the day speed: 1 normal, 2 twice as fast, 0 stands still |
| `sunriseHour` / `sunsetHour` | when the sunrise and sunset events fire (6 and 18) |
| `clockName` | empty: follows game time (stops in a pause). `ui`: keeps going while the game is paused |

Methods: `SkipToHour(h)` (forward to the next time it is `h`, firing the events on the way), `AddHours(h)` (forward fires events,
backward just jumps), `Minute()`, `HourOfDay()`, `Season()`, `SeasonName()`, `ClockText()` ("08:30").
Callbacks for C++: `onHour(h)`, `onDay(n)`, `onSeason(s)`, `onSunrise()`, `onSunset()`.

Saved values for UI text (`{clock}`, `{day}`, `{seasonName}`, `{hour}`, `{minute}`, `{season}`) change when the minute changes.

**Events** go to every object in the scene, named `OnHour` (value: 0-23), `OnDay` (the new day number), `OnSunrise`, `OnSunset`,
`OnSeason` (0 Spring, 1 Summer, 2 Autumn, 3 Winter): in Visual Script an **On Custom Event** with that name, in a script a function
`OnEvent_OnHour(h)`, in an Actions list **On Message**. One frame that passes several hours sends each event in order.
`SetTime` (a jump) sends nothing.

Scripts: `day_number()`, `season()`, `clock_text()`, `time_of_day(h)` (jump), `skip_to_hour(h)`.
Older scene files load unchanged; the new fields are written only when they are not at their defaults.
Guide: [time_guide.md](time_guide.md). Example: `examples/time/time_playground.okayscene`.
