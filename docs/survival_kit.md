# Survival Kit — ready-made gameplay scripts

The Survival Kit is a set of pre-written, fully customizable scripts for common
survival-game mechanics. Drop them onto your Player (or any object) and tune the
numbers in the Inspector — no coding required, but every script is plain
OkaySource you can open and edit.

## Adding them

Two ways:

- **Inspector ▸ Add Component ▸ Survival Kit** — attaches the script straight to
  the selected object. The source is stored on the object, so it saves with the
  scene and ships with your built game (no separate file to manage).
- **Project panel ▸ right-click ▸ New Script from Template ▸ Survival** — creates a
  reusable `.okay` file you can attach to many objects.

Every `public` field shows up in the Inspector (grouped under headers) for tweaking,
and each script exposes methods you can call from a **Button's On Click**, an
**Action List** (`call`), or another script.

## The scripts

| Script | What it does | Key fields | Methods |
|--------|--------------|-----------|---------|
| **Survival (all-in-one)** | Health, hunger, thirst, stamina, oxygen & temperature wired together | every field below | `Damage`, `Heal`, `Eat`, `Drink`, `Breathe`, `Warm`, `AddArmor`, `SetSprinting`, `SetSubmerged`, `SetCold`, `Revive` |
| **Health** | HP with armor, optional regen (with after-hit delay), death/low flags | `maxHealth`, `armor`, `regenPerSecond`, `regenDelay`, `lowThreshold` | `Damage`, `Heal`, `AddArmor`, `Revive`, `Fraction` |
| **Hunger** | Drains over time, faster while sprinting | `drainPerSecond`, `sprintMultiplier` | `Eat`, `SetSprinting`, `Fraction` |
| **Thirst** | Drains over time, faster while sprinting | `drainPerSecond`, `sprintMultiplier` | `Drink`, `SetSprinting`, `Fraction` |
| **Stamina** | Sprint/jump cost, regen with delay, exhaustion lockout | `sprintCost`, `jumpCost`, `regenDelay`, `exhaustedUntil` | `TryJump`, `SetSprinting`, `CanSprint`, `Fraction` |
| **Oxygen** | Drains while submerged, refills breathing air | `drainPerSecond`, `refillPerSecond` | `Breathe`, `SetSubmerged`, `Fraction` |
| **Temperature** | Warmth drains when cold, recovers near fire | `coldDrain`, `warmRegen` | `Warm`, `SetCold`, `SetNearFire`, `Fraction` |
| **Sleep / Energy** | Tiredness over time, recovers while resting | `drainPerSecond`, `restPerSecond`, `tiredThreshold` | `Rest`, `SetResting`, `Fraction` |
| **Sanity** | Drains in danger/darkness, recovers when safe | `drainInDark`, `regenInLight`, `lowThreshold` | `Restore`, `SetInDanger`, `Fraction` |

### How the all-in-one interacts

- Sprinting (`SetSprinting(1)`) burns stamina **and** drains hunger/thirst faster.
- Hunger, thirst, oxygen and warmth each chip away at health when they hit zero.
- `armor` reduces incoming damage; health regen pauses for `regenDelay` after a hit,
  then resumes only while fed **and** hydrated.

### Wiring example

Give the Player the **Survival (all-in-one)** component, then on a "Drink" button
set **On Click ▸ Target = Player, Function = Drink**. Toggle states from an Action
List — e.g. an `OnKey` list with `call SetSprinting` while shift is held, or a water
trigger volume that calls `SetSubmerged`.
