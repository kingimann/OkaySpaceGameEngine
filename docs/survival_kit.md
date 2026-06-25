# Survival Kit — native gameplay components

The Survival Kit is a set of built-in native components for common survival-game
mechanics. Drop them onto your Player (or any object) and tune the numbers in the
Inspector — no coding required, and no script VM in the loop. Two ways to use them:

- **Survival Stats (native)** — one all-in-one component with every stat wired
  together. Recommended when stats should interact (hunger/thirst damage health).
- **Individual stat components** — add just the stats you need (e.g. Health +
  Stamina), each running independently.

Both publish saved values, fill `*Bar` progress widgets, and broadcast messages, so
a HUD works the same either way.

## Survival Stats (all-in-one)

*Add Component ▸ Gameplay ▸ Survival Stats (native)*. Drop it on the Player and tune
the Inspector groups (Health, Hunger, Thirst, Stamina, Oxygen, Temperature, Output).
Each frame it drains the stats, and when hunger/thirst/oxygen/warmth hit zero it
calls `Damage()` **directly** (no message round-trip). `armor` soaks incoming
damage; health regenerates while fed + hydrated after a `regenDelay` pause. On death
it broadcasts `died`, plays a sibling AudioSource, and deactivates the object.

Sprinting (`SetSprinting`) burns stamina **and** drains hunger/thirst faster.

Methods: `Damage`, `Heal`, `Eat`, `Drink`, `Breathe`, `Warm`, `AddArmor`, `Revive`,
`SetSprinting`, `SetSubmerged`, `SetCold`, plus `Fraction()` / `…Fraction()` /
`IsDead()` / `CanSprint()`. **Output** toggles control whether it writes saved
values, fills `*Bar` widgets, and broadcasts messages.

## Individual stat components

Prefer to mix and match? *Add Component ▸ Gameplay ▸ Stat: …* adds just one stat:

| Component | What it does | Key fields | Methods | Critical msg |
|-----------|--------------|-----------|---------|--------------|
| **Stat: Health** | HP with armor, optional delayed regen, death | `maxHealth`, `armor`, `regenPerSecond`, `regenDelay`, `lowThreshold` | `Damage`, `Heal`, `AddArmor`, `Revive` | `died` |
| **Stat: Hunger** | Drains over time, faster sprinting | `drainPerSecond`, `sprintMultiplier` | `Eat`, `SetSprinting` | `starving` |
| **Stat: Thirst** | Drains over time, faster sprinting | `drainPerSecond`, `sprintMultiplier` | `Drink`, `SetSprinting` | `dehydrated` |
| **Stat: Stamina** | Sprint/jump cost, delayed regen, exhaustion lockout | `sprintCost`, `jumpCost`, `regenDelay`, `exhaustedUntil` | `TryJump`, `SetSprinting`, `CanSprint` | — |
| **Stat: Oxygen** | Drains submerged, refills breathing | `drainPerSecond`, `refillPerSecond` | `Breathe`, `SetSubmerged` | `drowning` |
| **Stat: Temperature** | Warmth drains when cold, recovers near fire | `coldDrain`, `warmRegen` | `Warm`, `SetCold`, `SetNearFire` | `freezing` |
| **Stat: Sleep / Energy** | Tiredness over time, recovers resting | `drainPerSecond`, `restPerSecond`, `tiredThreshold` | `Rest`, `SetResting` | `exhausted` |
| **Stat: Sanity** | Drains in danger, recovers when safe | `drainInDark`, `regenInLight`, `lowThreshold` | `Restore`, `SetInDanger` | `insane` |

Unlike the all-in-one, standalone stats **don't cross-talk** — an empty `Stat:
Hunger` won't drain `Stat: Health`. Use **Survival Stats** when you want hunger/
thirst to damage health.

## Making it show up (it's wired for you)

Every component **publishes itself each frame** so you see it working immediately:

- It writes a **saved value** per stat (`health`, `hunger`, `thirst`, `stamina`,
  `oxygen`, `warmth`, `energy`, `sanity`). Put a UI text with `bind="HP: {health}"`
  and it updates live.
- It pushes the 0–1 fraction to a **same-named progress bar** if one exists:
  `HealthBar`, `HungerBar`, `ThirstBar`, `StaminaBar`, `OxygenBar`,
  `TemperatureBar`, `EnergyBar`, `SanityBar`. Add a `progress` widget with that
  `name`/`id` and it fills automatically — no extra wiring.
- When a stat bottoms out it **broadcasts a message** once (`died`, `starving`,
  `dehydrated`, `drowning`, `freezing`, `exhausted`, `insane`) you can catch with an
  Action List `OnMessage` trigger (e.g. show a game-over panel, play a sound).
- Per-component **Output** toggles turn the saved value / bar / message off.

A 10-second HUD: add a `progress` widget named `HealthBar` (and `HungerBar`, …) to a
Canvas, drop **Survival Stats** on the Player, press Play — the bars drain.

## No-code buttons (On Click → native methods)

A **Button's On Click** can call these native methods directly. Set **Target** to the
object carrying the stat, pick a **Function** from the *Native (gameplay)* group
(`Eat`, `Drink`, `Heal`, `Damage`, `Breathe`, `Warm`, `SetSprinting`, …), and set the
**Amount** field — e.g. Target = Player, Function = `Drink`, Amount = `25` makes the
click call `Drink(25)`. Native verbs take priority; if the name isn't one of them the
button falls back to a script's public function, so custom handlers still work.

Toggle states (`SetSprinting`, `SetSubmerged`, `SetCold`, `SetNearFire`,
`SetResting`, `SetInDanger`) treat a non-zero Amount as "on".
