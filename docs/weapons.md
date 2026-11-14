# Weapons

A weapon is a row in a `.okayweapon` file, not a set of numbers typed onto every fighter. The project keeps one list at `weapons.okayweapon` (next to `items.okayitems`). **View > Editors & Tools > Weapons** edits it: add a melee weapon or a gun, change it, duplicate it, delete it. Dropping a `.okayweapon` file on the Project panel opens it here. Ctrl+Z undoes an edit while the window is focused.

A weapon can name an **item**. Equipping that item in the main hand selects the weapon on a Melee Attacker or a Ranged Weapon that has not picked one itself. An item can also name a weapon with a custom field `weapon`.

## What a weapon stores

Every weapon has a name, a kind (melee or gun), damage, range and knockback.

A melee weapon also has:

- **Swing time** — how long the swing lasts. 0 lands the hit at once.
- **Arc** — how wide the swing is, in degrees. 360 hits all around.
- **Hit window** — the part of the swing, in seconds from the start, during which a hit can land.
- **Combo** — how many hits belong to one chain, how long the next hit may wait, and the extra damage fraction added for each hit after the first.
- **Hit box** — a box in front of the fighter, used instead of the arc when it is on.

A gun also has:

- **Shots per second**
- **Rounds in the gun** — 0 means it never reloads
- **Spare rounds** — -1 means they never run out
- **Reload time**
- **Instant shot** — on, the shot lands at once; off, it spawns a projectile

## Using one

**Add Component > Gameplay > Melee Attacker** or **Ranged Weapon (gun)**. The Weapon slot is a dropdown of the list, and it accepts a dropped `.okayweapon` file. The numbers under it are the fallback used when no weapon is chosen.

A melee hit lands on whichever of these the target has: Health, survival health, RPG stats, or an NPC. A gun's instant shot does the same to the first of those a ray reaches, and both push a dynamic body by the knockback. Each swing broadcasts `player_attack`. Each shot that leaves the gun broadcasts `weapon_fire`.

The top-down shooter copies a gun's damage, fire rate, range, magazine, reload, spread and knockback when its Weapon field names one. If the same object also has a Ranged Weapon, that component fires and the shooter keeps moving and aiming.

## Scripts and visual script

| Script | Visual script node | What it does |
|---|---|---|
| `weapon_swing()` | Swing Weapon | Starts a swing. A hit window lands during the window, not on the call. |
| `weapon_fire()` | Fire Weapon | Fires once, if the rate, the reload and the rounds allow it. |
| `weapon_reload()` | Reload Weapon | Starts a reload. |
| `weapon_ammo()` | Get Ammo | Rounds left in the gun. |
| `weapon_set("sword")` | Set Weapon | Points the melee, the gun and the top-down shooter at that id. |

## Example

`examples/weapons/arena.okayscene` is a fighter with the practice sword, a dummy in front of them, and a pistol. `examples/weapons/weapons.okayweapon` is the list those names point at. The sword's hit window opens at 0.25 seconds, so a swing does not connect before then, and it misses anything outside the arc.

## File

```
okayweapon 1
weapon "sword"
  name "Sword"
  kind melee
  item "sword"
  damage 18
  swing 0.6
  range 2.2
  arc 110
  hit 0.15 0.35
  knockback 6
  combo 3 0.5 0.25
  hitbox 0 1.2 1.6 1.4
end
weapon "pistol"
  name "Pistol"
  kind gun
  item "pistol"
  damage 12
  fire 5
  range 40
  magazine 12
  ammo 36
  reload 1.2
  knockback 3
  hitscan 1
end
```

`ammo` is the spare rounds. `hit` is the window, in seconds. `combo` is the chain length, the wait, and the extra damage fraction.
