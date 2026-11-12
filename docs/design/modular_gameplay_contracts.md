# Modular gameplay systems on shared contracts

Status: design only, no code. Written against origin/main a73fdb22.
Goal: each gameplay system is a module (components, editor panel, docs, tests) that a project can add or
remove, and modules talk through a few shared interfaces instead of each one re-inventing health, damage,
"press E", ammo and seats.

"verified" means read in code on this commit (header or source, path given). The earlier grep-based audit
was checked; where it was wrong this document says so (section 1.9).

## 1. What exists today

### 1.1 Health (four models, verified)

| Model | File | What it has | Who reads/writes it |
|---|---|---|---|
| `HealthStat` | `engine/include/okay/Components/SurvivalComponents.hpp:82` | maxHealth/health, flat armor, `equipArmor`, resistance, minDamage, godMode, i-frames, regen + delay, overheal, death/respawn/lives, `dead`, `lowHealth`, `LastHitFrom(Vec3)`, `SetHealthFromServer` | projectiles, afflictions, VS nodes (`NodesGameplay.hpp:53 GpHealth`), Ragdoll, Equipment, NetworkManager (`__dmg`), QuestLog kill hook (`HealthDeathHook`), LootSource, BehaviorTreeRunner, DeathLock, HumanoidActions |
| `SurvivalStats` | `Components/SurvivalStats.hpp:28` | its own `health/maxHealth/armor/resistance/godMode`, plus hunger/thirst/oxygen/warmth that call its own `Damage()` | `DamageHealthOn`/`HealOn` in `SurvivalAfflictions.hpp:10` (prefers SurvivalStats, falls back to HealthStat), Equipment (`ss->equipArmor`) |
| `Stats` (RPG) | `Components/Stats.hpp:13` | `health/maxHealth`, `TakeDamage` with defense (min 1), levels, mana; fires `on_death` | Equipment (`st->equipDefense`); nothing in the shooter/projectile path |
| `NPCController` | `Components/NPCController.hpp:104,138` | private `health/maxHealth`, `Damage()`, `fleeHealthPct`, `npc_died`, provoke on hit | `MeleeAttacker` (`npc->Damage`, NPC targets only, `MeleeAttacker.hpp:54`); NPC attacks use `DamageHealthOn` (`NPCController.hpp:447`) |

Consequences, all verified:
- `DamageGameObject` (`Projectile3D.hpp:17`) walks up parents for a `HealthStat` only. A projectile cannot
  hurt an NPC, a `Stats` character or a `SurvivalStats` player. `TopDownShooterController.hpp:380` uses the
  same helper for hitscan.
- `MeleeAttacker` cannot hurt a `HealthStat`; shots cannot hurt an NPC. Two incompatible damage paths.
- Visual-script nodes (`DamageNode`, `HealNode`, `gplib_rows.inc` `GlGetHealth`, `GlSetArmor`...) work on
  `HealthStat` only and raise "its object has no Health component" otherwise.
- `Equipment::ApplyBonuses` (`engine/src/Inventory/Equipment.cpp:301`) pushes armor into three models by hand.
- Scene saving: `HealthStat` is record `stat_health` (`SceneSerializer.cpp:2221` write, `:5999` read, with
  trailing optional fields added over time). `SurvivalStats`, `Stats`, NPC health each have their own records.
- Death signals are also four: `HealthDeathHook` + broadcast `died`, `on_death` script event, `npc_died`,
  `starving`-style messages.

### 1.2 Damage (no shared event, verified)
- Damage is a bare `float`: `HealthStat::Damage(float)` and `Damage(float, Vec3 fromWorld)`. No type, source
  object, hit zone, hit point, or direction (only `lastHitFrom`).
- Callers: `DamageGameObject`, `DamageHealthOn`, VS nodes, `NetworkManager::ApplyHit`
  (`engine/src/Net/NetworkManager.cpp:1796`), `NativeUIActions.hpp:45` (button "Damage"), NoCode
  `DamageOnTouch` (`Components/NoCode.hpp`).
- Floating numbers: `UIDamageNumbers` is driven by script/UI actions, not by a damage event.

### 1.3 Interaction (no shared contract, verified)
- `InventoryInteractable` (`Components/InventoryPlayer.hpp:66`): abstract base with `open`, `range`,
  `playerName`, `InteractKey()`, `Nearest()` (so one chest opens at a time). Subclasses: `ChestInventory`,
  `CraftingStation`. Fires `on_chest_opened/closed` through `InventoryEvents::Fire`.
  (The audit said "no generic Interactable": there is a base, but it is inventory-specific, toggle-only,
  key = a single `char`, no prompt, no hold.)
- Not on it, each with its own range + key + prompt code: `ItemPickup` (`interactKey`, `interactRange`,
  static `BlockInteract()` flag), `DialogueRunner` (`interactKey`, `interactRange`, `promptText`),
  `Seat` (`useRadius`, `prompt`, `playerInRange`), `HoldableObject`, `LogicSource` buttons (`LogicLinks.hpp`).
- Shared prompt UI exists only as `HoldPrompt` (label object named "Hold Prompt"; `ExtraPrompt()` static string).
- "One key press is used once" is solved three ways: `ItemPickup::InteractBlocked`, `Nearest()`, and per
  component checks.
- No "inspect" action anywhere.

### 1.4 Weapons (verified)
- `TopDownShooterController` (`Components/TopDownShooterController.hpp`): fire modes (built-in bullet, prefab,
  hitscan), `range`, `pellets`, `spread`, `magazineSize/ammo/reloadKey/reloadTime/autoReload`, dash, script
  events `on_fire/on_reload/on_reloaded/on_empty`. Ammo is a plain int, not drawn from `Inventory`.
- `ThirdPersonShooterController`: aim camera, only calls the sibling script's `on_fire()`. No damage, ammo,
  reload (verified: its header says it calls the script). Used by `SceneTemplates::AddThirdPersonShooterPlayer`
  (`SceneTemplates.hpp:314`), editor GameObject menu (`editor/main.cpp:6426`) and the "Third Person Shooter"
  project template (`editor/main.cpp:17918`).
- `FirstPersonController`: no weapon code found (hands exist: `FirstPersonHands`).
- `Projectile3D` (`Components/Projectile3D.hpp`): swept ray, pierce, bounce, `owner`, pool reuse.
- `MeleeAttacker`: NPC targets only.
- Multiplayer: `NetworkManager::ClaimHit(targetNetId, weapon, origin, direction)` -> server validates (range,
  line of sight, lag-compensated pose history, `NetValidator "hit/<weapon>"` fire rate and ammo bucket) ->
  `ApplyHit` -> `HealthStat::Damage` -> `__dmg` reliable message -> `SetHealthFromServer`.
  Damage amount in the claim is server-side (`claim.damage`). Hit zone and damage type are not in the claim.
- `ItemDefinition` (`Inventory/ItemDatabase.hpp:42`) has `category`, `tags`, `equip` (EquipSlot incl.
  MainHand/OffHand), `useAction/useAmount`, `custom` key/value list. No weapon stats; `Equipment` armor
  comes from the `custom` "armor" field (`Equipment.hpp:23`).
- No recoil, no attachments (verified by grep and by reading the controllers). `Sockets.hpp` exists.

### 1.5 Seats and vehicles (verified)
- `Seat` (`Components/Seat.hpp`; implementation inside `engine/src/Animation/HumanoidActions.cpp`): sit point,
  exit point, `type` (chair/ground/bench), `useRadius`, `prompt`, `occupied`. `Claim(who)` / `Release()`,
  parks the occupant (disables gravity, blends position). Saved as record `seat` (`SceneSerializer.cpp:2229`).
  Driven by player "interact" in the Character path and VS `sit` node (`NodesExtra.hpp:991`). Chair only: no
  controls hand-over, no seat roles.
- `VehicleController` (+2D): arcade driving, optional raycast suspension (`suspension=false` default,
  `VehicleController.hpp:45`), no health, no seats, no doors, no occupants.
- Doors: only `LogicTarget::Kind::Door` (`LogicLinks.hpp:158`), scene doors driven by `LogicSource`.

### 1.6 Inventory and equipment (verified)
`Inventory`, `GridInventory`, `Equipment`, `Sockets`, `InventoryOps`, `ItemDatabase`/`ItemInstanceData`
(per-item values), `InventoryEvents::Fire(go, scriptEvent, vsEntry, args)` (script event + VS entry in one
call: the pattern every new event should use), `NetInventory`. Two inventory shapes (slot vs grid) are not
unified; that stays out of scope here.

### 1.7 Input (verified)
`Input/InputActions.hpp`: named actions with `InputBinding` (key, mouse, pad button, Keys4, sticks, DPad,
mouse delta), per-action `defaults`, contexts. Controllers mostly read raw keys (`Input::GetKeyDown(char)`,
for example `reloadKey = 'r'`, `interactKey = 'e'`). `RemapModel` exists but has no screen (only
`tests/test_focus_remap.cpp` uses it). Project Settings has no input section (`ProjectSettings` in
`editor/ProjectState.hpp:304`: company, version, size, gravity, ambient, skybox, splash only).

### 1.8 Network sync (verified)
- `NetworkSync` (`Components/NetworkSync.hpp:181`): `netId`, `authority {Host, Mine, Manual}`, `owned`,
  `syncAnimation`, `clientClaimable`. Replicates transform, plus Character animation and punch for proxies.
- Other channels: `SyncVars`/RPC (`NetworkManager::Rpc/RpcReliable/OnRpc`, `Components/NetworkSync.hpp` neighbours
  `NetworkObject`, `NetInventory`), `__dmg` health result, `NetValidator` per-RPC rules, `AntiCheatConfig`.
- Health is replicated only as the `__dmg` message to `HealthStat`; seats, doors, ammo, interactables are not.

### 1.9 Where the audit was wrong or incomplete
- "Ragdoll bone colliders" for limb damage: false. `Ragdoll` is a Verlet particle chain over the 15
  humanoid bones (`Ragdoll.hpp` header comment), no per-bone colliders. A hit zone has to come from the hit
  point nearest a humanoid bone (`humanoid::MapHumanoidBones`, `HumanoidRetarget`), not from colliders.
- "FocusGraph unused": it is used by `UINavigation.hpp:14,51`. `MipBudget` is referenced by
  `SceneOptimizationReport.cpp`, which has no editor caller. `RemapModel` really is unused by UI.
  These are unrelated to gameplay modules and are not touched here.
- "No generic Interactable": there is an inventory-only base (1.3).
- Health models: four confirmed, with the exact consumers in 1.1.
- Traversal flags default off (`Traversal.hpp` header comment): confirmed; out of scope except as a manifest
  default (section 3).

### 1.10 Wiring facts the contracts must fit
- Components are `Behaviour` subclasses (`Scene/Component.hpp`) with `Start/Update/OnDestroy`,
  `ExecutionOrder()`, `OnAnimationEvent`.
- Scene text is line records (`SceneSerializer.cpp`), version `kSceneFormatVersion = 3`
  (`SceneSerializer.hpp:72`). New components can register through `ComponentSerializerRegistry`
  (`Scene/ComponentSerializer.hpp`) without touching the if/else chain; an unknown record is skipped with a
  warning, a newer file version loads "what is understood" (`SceneSerializer.cpp:4072`). Upgrades are
  `SceneMigration::Step` (`from -> from+1`, text rewrite or `needsEngine`).
- Properties: `PropertyBuilder` / `PropertyRegistry` (`Scene/BuiltinPropertiesGameplay.cpp`) describe fields with
  tooltips and ranges for the Inspector and for path access ("Light.intensity"); `ComponentTypeInfo` has a
  `category` used by the Add Component menu.
- Add Component is a hand-written menu in `editor/main.cpp` around `39281` (search box, category tabs,
  recent list; items such as `NetworkSync` at `:39679`).
- Visual-script nodes: hand-written node structs plus generated tables (`editor/generated/gplib_*.inc`,
  `editor/VisualScriptNodeDocs.inc` from `scripts/gen_vs_node_docs.py`; test `tests/test_vs_node_docs.cpp`).
- Script events: `InventoryEvents::Fire(go, "on_x", "vsEntry", args)`.

## 2. The contracts

Principles:
1. Contracts live in `engine/include/okay/Gameplay/` (new folder, headers only plus small `.cpp`), no editor
   dependency, no dependency on any module. A module depends on contracts, never on another module.
2. A contract is an abstract interface plus one reference component. Existing classes either become the
   reference component (kept name and record) or implement the interface.
3. One lookup per contract, `Find<I>(GameObject*)`, walking up parents like `DamageGameObject` does today.
4. Events go out through one helper (`GameplayEvents::Fire`) that does script event + visual-script entry +
   C++ listener list + optional network replication, mirroring `InventoryEvents::Fire`.
5. Server authority for anything that changes health, ammo, or seat occupancy in multiplayer.

### 2.1 Health (absorbs HealthStat, SurvivalStats health, Stats health, NPC health)

```cpp
// engine/include/okay/Gameplay/IHealth.hpp
class IHealth {
public:
    virtual ~IHealth() = default;
    virtual float Current() const = 0;           virtual float Max() const = 0;
    virtual float Armor() const = 0;             virtual bool  IsDead() const = 0;
    virtual bool  IsInvincible() const = 0;
    // The one entry point. Returns what was actually applied (after armour, resistance, i-frames).
    virtual DamageResult ApplyDamage(const DamageEvent&) = 0;
    virtual float Heal(float amount, GameObject* source = nullptr) = 0;
    virtual void  Kill(const DamageEvent* cause = nullptr) = 0;
    virtual void  Revive() = 0;
    static IHealth* Find(GameObject*);           // self, then parents
};
```
Reference component: `HealthStat` stays the class name and the `stat_health` record (so every scene, VS
node, template and the `{health}` prefs keep working), implements `IHealth`, and gains: `armourPct`
(optional percentage armour next to flat `armor`), `Resistances` (2.2), `deathEvent` fields unchanged.
Fields stay as they are (`health`, `maxHealth`, `armor`, `regenPerSecond`...).

Absorbed:
- `SurvivalStats`: its health block is replaced by a required sibling `HealthStat`; `SurvivalStats::Damage`
  forwards to `IHealth::ApplyDamage` (type `Starvation`, `Drowning`, `Cold`, source null). Hunger/thirst/etc.
  stay in `SurvivalStats`. `DamageHealthOn`/`HealOn` shrink to `IHealth::Find(go)->...`.
- `Stats` (RPG): health fields removed in favour of a sibling `HealthStat`; its defense feeds
  `Resistances` (flat reduction, min 1 kept as `minDamage = 1`); `on_death` is fired from the shared event.
- `NPCController`: `health/maxHealth/Damage` become wrappers over a sibling `HealthStat` (auto-added at load);
  provoke / flee-at-health / `npc_died` become listeners on Health events.
- `MeleeAttacker`, `Projectile3D`, hitscan, VS nodes, `NetworkManager::ApplyHit`: all call
  `IHealth::Find(target)->ApplyDamage(ev)`.

Events (see 2.2 for the payload): `on_damaged(DamageEvent, DamageResult)`, `on_healed(amount)`,
`on_health_low`, `on_died(DamageEvent)`, `on_revived`. Existing signals remain emitted by the same call:
`Broadcast("died")`, `HealthDeathHook`, script `on_death`, `npc_died`. That is the compatibility layer; no
listener has to change in the first PRs.

### 2.2 Damage and resistances

```cpp
enum class DamageType : uint8_t { Generic, Bullet, Explosion, Melee, Fall, Fire, Cold, Poison, Starvation,
                                  Drowning, Collision, Environment };   // plus project-defined names via DamageTypeId
struct DamageEvent {
    float       amount = 0;                 // raw, before mitigation
    DamageType  type = DamageType::Generic;
    GameObject* source = nullptr;           // who/what caused it (the shooter, the vehicle), may be null
    GameObject* instigator = nullptr;       // the player behind it (driver of a vehicle), for kill credit
    std::string weapon;                     // weapon id, used by NetValidator "hit/<weapon>" and kill feed
    std::string zone;                       // HitZone name ("head", "left_leg") or empty
    Vec3        point{};  Vec3 direction{}; // world hit point, travel direction (zero = unknown)
    bool        hasPoint = false;
    uint32_t    netTick = 0;                // set by the server path
};
struct DamageResult { float applied = 0; bool killed = false; bool blocked = false; };
class IDamageable { public: virtual DamageResult ApplyDamage(const DamageEvent&) = 0; };  // IHealth extends it
```
Resistances: `struct Resistances { float byType[kDamageTypeCount]; float flat; }` on `HealthStat` (data, no
new component): multiplier per type, `0` = immune. Order, replacing the current order in `ApplyDamage`:
zone multiplier -> type multiplier -> flat armor + equipArmor -> `resistance` -> `minDamage` -> i-frames/dodge.
The current numbers are unchanged when zone is empty and all type multipliers are 1.

The old `Damage(float)` and `Damage(float, Vec3)` overloads stay as inline wrappers (type `Generic`), so
scripts, VS nodes and `NativeUIActions` compile and behave the same.

### 2.3 HitZone (limb multipliers)

```cpp
class HitZones : public Behaviour {            // optional, on a humanoid or any body
    struct Zone { std::string name; int humanoidBone = -1; float multiplier = 1.0f;
                  float limbHealth = 0; /*0 = no limb state*/ std::string statusOnZero; };
    std::vector<Zone> zones;
    const Zone* ZoneForPoint(const Vec3& world) const;   // nearest mapped bone to the hit point
};
```
- Resolution is by nearest humanoid bone to `DamageEvent.point` (bones from `humanoid::MapHumanoidBones`),
  not by colliders (see 1.9). Non-humanoid bodies use named child colliders instead: a `HitZones` zone can
  list a child object name.
- `HealthStat` asks `HitZones::ZoneForPoint` when `ev.zone` is empty and a point exists, so shooters only
  have to send a point.
- Limb state (broken leg = slower) emits `on_limb_disabled(zone)`; `MoveSpeed` (existing) reads a
  `limbSpeedFactor` from it. First release ships multipliers only; limb state is a later PR.

### 2.4 Interactable

```cpp
class IInteractable {
public:
    struct Context { GameObject* actor; bool hold; float heldSeconds; bool inspect; };
    virtual bool  CanInteract(const Context&) const = 0;    // faction/item/lock checks
    virtual std::string Prompt(const Context&) const = 0;   // localized through LocKeys like HoldPrompt
    virtual void  Interact(const Context&) = 0;
    virtual void  Inspect(const Context&) {}                 // optional "examine"
};
class Interactable : public Behaviour {          // the reference component and the broker
    float range = 2.5f;  float holdSeconds = 0;  std::string prompt, inspectText;
    std::string action = "interact";                     // InputActions name, falls back to key 'e'
    std::string requiredItem, requiredTag;               // who can interact
    bool  oneShot = false, enabledInteract = true;
    // Events: on_interact(actor), on_inspect(actor), on_hold_progress(actor, 0..1), on_focus(actor), on_blur(actor)
};
```
- `InteractionFocus` (static, per scene, owned by the player's `InteractionSensor` component) picks one target
  per actor per frame: nearest in range that `CanInteract`. It replaces `ItemPickup::InteractBlocked`,
  `InventoryInteractable::Nearest` and the per-component range checks, and drives the prompt label that
  `HoldPrompt` shows today (HoldPrompt becomes the view, keeping its name and `ExtraPrompt()` until PR 5).
- Wraps/absorbs: `InventoryInteractable` (`ChestInventory`, `CraftingStation` implement `IInteractable`; their
  `open` state and `on_chest_opened/closed` stay), `ItemPickup` (interact path), `DialogueRunner` (start path),
  `Seat` (enter), `HoldableObject`, `LogicSource` button, door `LogicTarget`.
- Door/lever without code: an `Interactable` next to a `LogicSource` of kind Button fires it (so I/O links
  keep working).
- Network: the actor's interact request is an RPC to the server (`interact/<netId>`), the server runs
  `Interact`, state that changes (open, occupied) is a synced property (2.8). Prompt/focus are client-only.

### 2.5 Weapon

```cpp
enum class FireMode { Single, Burst, Auto };   enum class Delivery { Hitscan, Projectile, Melee };
struct WeaponDef {                              // data, lives in an .okayitems custom block or on the component
    std::string id;  FireMode fire = FireMode::Single;  Delivery delivery = Delivery::Hitscan;
    float damage = 20, range = 50, fireRate = 8;  int burstCount = 3, pellets = 1;
    float spreadDeg = 1, spreadPerShot = 0.3f, spreadRecoverPerSec = 6, aimSpreadMul = 0.4f;
    float recoilPitch = 1.2f, recoilYaw = 0.4f, recoilRecoverPerSec = 8;     // kick applied to look and camera
    int   magazineSize = 30;  float reloadTime = 1.8f;  float tacticalReloadTime = 1.4f;
    std::string ammoType;                     // ItemDefinition id; empty = infinite
    DamageType damageType = DamageType::Bullet;
    std::string projectilePrefab; float projectileSpeed = 60;
};
class IWeapon {
public:
    virtual bool TryFire(const AimRay&) = 0;  virtual bool TryReload() = 0;
    virtual int  MagazineAmmo() const = 0;    virtual int ReserveAmmo() const = 0;  virtual bool IsReloading() const = 0;
    virtual void SetAiming(bool) = 0;
};
class Weapon : public Behaviour, public IWeapon {   // on the weapon object (held in a socket) or on the player
    WeaponDef def;  std::vector<std::string> attachments;   // Sockets child names, each an `WeaponAttachment`
    // Events: on_fire, on_empty, on_reload_start, on_reloaded, on_hit(DamageEvent, DamageResult), on_recoil
};
```
- Ammo link: the magazine count lives on the weapon (and in `ItemInstanceData` values when the weapon is an
  inventory item, so it survives drop and save). Reserve ammo is counted from the owner's `Inventory` by
  `ammoType` (`InventoryOps` count/remove); reload moves reserve -> magazine in one operation, so there is one
  ammo number per source of truth. Without an `Inventory`, `reserve` is an int on `Weapon`.
- Fire pipeline (one implementation): spread -> ray or projectile -> for each hit build `DamageEvent` (type, source =
  weapon owner, weapon id, point, direction, zone from `HitZones`) -> `IDamageable::ApplyDamage`.
  Multiplayer: client predicts effects, then `NetworkManager::ClaimHit` (extended with `zone` and `weapon`
  already in the claim); the server computes damage from `WeaponDef`, never from the claim.
- Absorbs: the shooting block of `TopDownShooterController` (fields `fireMode, projectile, pellets, spread,
  range, magazineSize, ammo, reload*` map 1:1 onto `WeaponDef`), the empty `on_fire` hook of
  `ThirdPersonShooterController` (it now just forwards the fire input to `IWeapon`), `Projectile3D` (kept, gets
  `DamageEvent` instead of `float`), `MeleeAttacker` (becomes `Delivery::Melee` on `Weapon`; the class
  remains as a thin preset until its removal), the pellet/spread math in the top-down controller.
- Attachments: `WeaponAttachment` (optional component on a Sockets child) declares modifiers: spread/recoil
  multipliers, magazine size delta, damage multiplier, sight zoom. `Weapon` recomputes effective stats when a
  socket child changes. Needs `Sockets` (verified present).
- Controllers keep ownership of input and camera (`FirstPersonController`, `ThirdPersonShooterController`,
  `TopDownShooterController`); they call `IWeapon` and receive `on_recoil` to kick the camera. No controller
  contains damage code after the port.

### 2.6 Seat and vehicle seat

```cpp
enum class SeatRole { Sit, Driver, Passenger, Gunner };
class Seat : public Behaviour {      // existing class, extended; existing fields stay
    SeatRole role = SeatRole::Sit;    GameObject* vehicle = nullptr;  // null = a chair, today's behaviour
    std::string doorName;            // an Interactable-driven door; seat is blocked while locked
    bool exitFromInside = true;  bool exitEjectsAtSpeed = false;
    // Events: on_seat_entered(actor), on_seat_exited(actor), on_vehicle_entered(actor, role)
};
class IVehicle {                     // implemented by VehicleController and VehicleController2D
public:
    virtual void  SetDriver(GameObject* actor) = 0;      // routes input to the vehicle, null = no driver
    virtual std::vector<Seat*> Seats() = 0;
    virtual IHealth* Health() = 0;                       // body health, null if none
};
```
- `Seat` keeps its record `seat` with the same 11 fields; new fields are appended as optional trailing values.
  A Seat with `vehicle == null` and `role == Sit` behaves exactly as today (chair/bench/ground clips).
- Enter/exit go through `Interactable` (prompt "Enter"/"Sit"), so `Seat::playerInRange`/`useRadius`/`prompt`
  become the Interactable's `range`/`prompt` (migrated on load).
- Driver: `VehicleController` stops reading player input directly when it has an `IVehicle::SetDriver`
  actor; the actor's controller is disabled while seated (as `Seat::Claim` already parks the body).
- Passengers: parented to the seat anchor, no input, can use their `Weapon` if the seat allows (`Gunner`).
- Doors: a vehicle door is a child with `Interactable` + a small `LogicTarget` Door preset (existing); a
  locked door blocks `Seat::Claim`. Door open state is a synced property.
- Vehicle damage: `HealthStat` on the vehicle body with `DamageType::Collision` from impact impulse (a
  `VehicleDamage` module component: crash threshold, smoke/fire thresholds, wreck). Occupants take a fraction
  through `IHealth` of each seat's occupant on explosion.
- Multiplayer: seat occupancy and driver id are server-owned properties; a client requests enter/exit by RPC;
  the driver's client has authority on the vehicle transform (`NetworkSync.authority` handed to the driver, via
  `SetOwned`, with `clientClaimable` semantics unchanged for the anti-cheat).

### 2.7 Inventory link (what the contracts need, no new inventory model)
- `IAmmoSource { int Count(ammoType); int Take(ammoType, n); int Give(ammoType, n); }` implemented by
  `Inventory` and `GridInventory` through `InventoryOps`; `Weapon` and loot/pickup code use it only.
- `ItemDefinition` gets no new typed fields in the first step: weapon and armour data stay in `custom`
  (`weapon.id`, `ammo.type`...). A later PR can add a typed block if wanted.
- `Equipment` armour application calls `HealthStat` only (after the health merge), replacing the three
  `if (auto* ...)` branches in `Equipment::ApplyBonuses`.

### 2.8 Network contract
A single opt-in list on `NetworkSync`: `std::vector<SyncedProperty> properties` where an entry is
`{ componentType, propertyPath }` (path syntax of `PropertyRegistry`, e.g. `HealthStat.health`,
`Seat.occupied`). Rules:
- Server-authoritative by default for gameplay properties; `NetworkSync.authority` still decides the transform.
- Sent on change, reliable, as `__prop/<netId>/<path>`; clients apply with a setter that does not re-fire
  gameplay events except `on_*_replicated`.
- `HealthStat` is on the list by default for objects that already have a `NetworkSync` (replaces the
  special case in the `__dmg` handler at `NetworkManager.cpp:944`; `__dmg` keeps being sent so old clients work).
- Hit events: `ClaimHit` unchanged on the wire for old clients; new clients add `zone`. The server ignores
  unknown fields.
- Rejected: replicating `DamageEvent` from clients. Clients never apply damage locally (as the existing
  `ClaimHit` comment states).

### 2.9 Module manifest

```cpp
// engine/include/okay/Gameplay/ModuleManifest.hpp  (data, no behaviour)
struct ModuleManifest {
    std::string name;                         // "weapons"
    std::string title, description, docPage;  // docs/modules/weapons.md
    std::vector<std::string> requires;        // module names: {"health"}
    std::vector<std::string> provides;        // contracts: {"IWeapon"}
    std::vector<std::string> components;      // C++ type keys (ComponentSerializerRegistry::TypeKey)
    std::vector<std::string> sceneRecords;    // record names the serializer owns: {"weapon"}
    std::vector<std::string> vsNodeGroups;    // visual-script node group ids shown only when enabled
    std::vector<std::string> assetRoots;      // content the module needs shipped (fonts, default prefabs)
    bool defaultOn = true;                    // new projects
    bool coreContract = false;                // health/damage/interaction: cannot be removed
};
```
- Registered by one `RegisterModule(ModuleManifest)` call per module (same pattern as
  `ComponentSerializerRegistry::Register`). Registry lives in the engine so the player, editor and tools share it.
- Each module has a manifest, a header folder, a doc page `docs/modules/<name>.md`, a test file, and an
  editor panel entry (an optional inspector extension or window), all listed in the manifest.
- Modules provided initially: `health` (core), `damage` (core), `interaction` (core), `weapons`, `seats`
  (core, grows into vehicles), `vehicles`, `survival`, `inventory`, `npcs`, `dialogue_quests`, `building`.
  Only the first four PRs touch code; the rest are manifests that point at existing components.

## 3. Adding and removing modules per project

### 3.1 Project data
`project.okayproj` (`editor/ProjectState.hpp:304`, key=value lines) gets `modules=health,damage,weapons`.
Rules: absent key = "all modules on" (every existing project keeps today's menus); the list is written only once
the user opens the Modules page and presses Apply. `ParseProjectSettings` already ignores unknown keys, so an
older editor opening a newer project loses nothing.

### 3.2 Project Settings > Modules page
A new section in Edit > Project Settings (`editor/main.cpp:6195` opens it): one row per module with title,
description, doc link, toggle, "requires" and "required by" lists. Toggling off a module that others require is
blocked with the dependent names listed. Turning a module off never deletes scene data: components already in
scenes stay and show as "module disabled" in the Inspector (read-only header, still serialized), and the
Problems panel (`ProjectDiagnostics`) gets one rule `module.disabled_component_in_scene` listing them.

### 3.3 Add Component filtering
The Add Component popup (`editor/main.cpp:39281`) hard-codes items. First step is data, not rewrite: every item
passes through `ModuleRegistry::Allows(componentTypeKey)`; components with no manifest are always shown. The
popup's category tabs hide empty categories. Recent list drops disabled entries. GameObject > templates
(shooter player, vehicle) get the same check through the manifest's `provides`.

### 3.4 Visual-script nodes
Nodes are hand-written structs and generated tables (`editor/generated/gplib_*.inc`). Each node group id in
`vsNodeGroups` (for example `gp.combat`, `inventory`, `story`) maps to the palette categories; the palette,
search and "Add node" popup skip groups of disabled modules. Loading a graph that uses a hidden node still works
and shows the node with a "module disabled" badge (graphs must never fail to load because of a toggle). The
node docs generator (`scripts/gen_vs_node_docs.py`) records the group so the docs page for a module lists its
nodes. Verify on the first PR where the palette categories are really assembled (not located in this survey).

### 3.5 Export stripping and PR #683
Branch `claude/build-pipeline` (PR #683, not merged) adds `engine/include/okay/Core/BuildPipeline.hpp` with a
dependency walk: `AssetRef{ref, referrer}` roots from the built scenes, `WalkAssets(roots, WalkSettings)`
follows text assets, `ShippedAsset` carries a `reason`, `Report::AddExcluded(path, bytes, reason)` records what
was left out, and `FindUnshipped` lists project files nothing referenced. Module manifests feed it at two
points, both data-only so they do not conflict with the walk:
1. Extra roots: `ModuleManifest.assetRoots` of enabled modules are added to the `AssetRef` list with referrer
   `module:<name>` (so default prefabs/fonts that a module needs ship even if no scene mentions them).
2. Exclusions: assets under `Assets/Modules/<name>/` of a disabled module are never roots, so the walk drops
   them, and the report shows reason `module <name> disabled`.
Code is not stripped per module in the first version: the player binary links every component (a disabled
module's scene records still load). A second step could compile the player with module groups switched off in
CMake; that is a size optimisation and out of this plan. Land #683 first; PR 8 below depends on it and must be
rebased on it, never re-implement the walk.

### 3.6 Docs and tests per module
`docs/modules/<name>.md` is required by the manifest (a test fails when `docPage` does not exist). Module tests
sit in `tests/test_<module>_*.cpp` and are listed in the manifest `tests` field once PR 8 adds it, so a module
can be built and tested alone (CMake option per module is not part of this plan).

## 4. Migration plan

Rules for every PR: it builds, passes the existing suite, adds its own tests, changes nothing for existing
scenes unless stated, nothing hidden/off, no dead code (a contract lands together with its first consumer).
Sizes: S under ~300 lines, M ~300-900, L above.

| # | PR | Size | Contents |
|---|---|---|---|
| 1 | Health + Damage contracts | M | `Gameplay/IHealth.hpp`, `DamageEvent`, `DamageResult`, `Resistances`; `HealthStat` implements them, new `ApplyDamage(DamageEvent)` is the core and `Damage(float)` wraps it; `GameplayEvents::Fire`; `DamageGameObject`, `DamageHealthOn`, `HealOn`, VS `DamageNode/HealNode`, `NetworkManager::ApplyHit`, `NativeUIActions` call `IHealth::Find`. Serializer: `stat_health` gets trailing optional resistance fields; no version bump needed (older files lack the tail, already tolerated). |
| 2 | Health merge: SurvivalStats, Stats, NPCController | M | Each stops owning health and uses a sibling `HealthStat` (auto-added on load when an old record has `health` values, values copied). Old records `survival`, `stats`, `npc` keep reading their health fields. Scene format version 3 -> 4 with a `SceneMigration::Step` that adds the `stat_health` line (needs engine for NPC/Survival so it is `needsEngine`, like the character-animator step). `Equipment::ApplyBonuses` shrinks to one branch. `MeleeAttacker` can now hit any health. |
| 3 | Interactable contract + InteractionFocus | M | `Gameplay/Interactable.hpp`, `Interactable` component, `InteractionSensor` on the player (auto-added by player templates and by any controller that can interact), `HoldPrompt` becomes the view. Port `ChestInventory`, `CraftingStation`, `ItemPickup`, `DialogueRunner`. Old fields (`interactKey`, `interactRange`, `useRadius`) still read; they populate the Interactable at load. Adds hold-to-use and inspect with a test scene. |
| 4 | Weapon contract + port TopDown shooter | L | `Weapon`, `WeaponDef`, `IWeapon`, `IAmmoSource` on `Inventory`; `TopDownShooterController` shooting block moves into `Weapon` (its fields become a read-only migration into `WeaponDef` at load; record `topdownshooter` keeps its fields). Net: `ClaimHit` carries the weapon id and zone. Includes a magazine in `ItemInstanceData`. |
| 5 | Third-person and first-person use Weapon | M | `ThirdPersonShooterController` fires through `IWeapon` (works without a script now; `on_fire` still fires for old scripts). `FirstPersonController` gets the same weapon hook. Recoil and spread-per-shot ship here (they need a controller to kick). |
| 6 | Seats become vehicle seats | L | `Seat.role/vehicle/doorName` (optional trailing fields of record `seat`), `IVehicle` on `VehicleController` and 2D, driver input routing, passengers, doors via `Interactable`. Network: occupancy and driver authority. Wheels visuals not included. |
| 7 | Vehicle damage + limb damage | M + M | Two PRs. `VehicleDamage` (crash damage, smoke, wreck, occupant damage) on the `IHealth` of the body. Then `HitZones` (multipliers, limb state) with the zone sent through `ClaimHit`. |
| 8 | Module manifest + Modules page + Add Component filter | M | `ModuleManifest`, registry, `modules=` key in `project.okayproj`, page, `Allows()` in the Add Component popup and palette, Problems rule, `docs/modules/*.md` for the modules that exist by then, manifest tests. Requires #683 merged for 3.5; if not merged, the export parts are left out and listed, not stubbed. |
| 9 | Attachments | M | `WeaponAttachment` on `Sockets`, effective-stat recompute, docs and sample. Needs PR 4. |
| 10 | Network property sync | M | `NetworkSync.properties`, `__prop` channel, `HealthStat` and `Seat.occupied` on it; keeps `__dmg`. Can run in parallel after PR 2. |

Order rationale: 1 and 2 remove the four health models before anything else can build on them (`Weapon` and
`HitZones` need one target type). 3 before 6, because enter/exit uses Interactable. 4 before 5, 9. 8 comes after
the contracts exist so the manifests describe real components; it can be moved earlier if the owner wants the
Modules page sooner (nothing else depends on it).

### Tests each PR needs (new files named `tests/test_gameplay_*.cpp`)
- 1: damage order table (armor/resistance/type/min/i-frames), result values, events fire once, old `Damage(float)`
  gives the same health as before on a fixed matrix, VS node on `HealthStat`, `ApplyHit` result matches `__dmg`.
- 2: load a scene saved by main (checked-in fixture with all four old components) -> same health values on the
  `HealthStat`; save then load round trip; `Equipment` armour; NPC dies -> `npc_died`, `died`, `HealthDeathHook`
  all fire once; QuestLog kill count unchanged; migration step test (v3 -> v4).
- 3: one interactor chosen at a time, hold progress and cancel, prompt string through localization, old chest /
  pickup / dialogue scenes behave as before, `InteractBlocked` replacement keeps "one press, one use".
- 4: fire modes, ammo from `Inventory`, reload moves reserve to magazine, magazine survives drop and save,
  top-down fixture scene fires identically (same pellet count, spread bounds), server hit validation test with a
  fake `NetworkManager` mode.
- 5: third-person player fires, damages and runs out of ammo with no script; recoil kick recovers to baseline.
- 6: enter/exit/swap seats, driver routes input and releases it, locked door, occupant parents to vehicle and
  leaves on destroy, chair seat scene from main unchanged, occupancy replicates.
- 7: crash impulse thresholds, wreck state, headshot multiplier through the real raycast on a humanoid, limb
  status slows `MoveSpeed`.
- 8: manifest dependency resolution (cannot disable a required module), Add Component filter, project file
  round trip with and without `modules=`, VS palette hides a group but a saved graph still loads, docPage exists.
- 9, 10: modifiers stack and revert; property sync round trip and rejection of client writes.

### Risks
- `editor/main.cpp` is a hotspot (#683 changes 800 lines of it). Keep editor edits per PR small and put new UI in
  new files under `editor/` (the Modules page and the Interactable inspector extension), as other panels do.
- PR 2 changes what a scene writes (a `stat_health` line on objects that did not have one). Older editors read it
  fine (a known record); old players ignore a newer version with a warning (`SceneSerializer.cpp:4072`). Do not
  bump the version if the migration is not needed for correctness: decide when PR 2 is written (open question 3).
- Behaviour drift in damage order: PR 1 must prove equality on the old matrix before adding new multipliers.
- Third-party scripts that read `ThirdPersonShooterController` or call `npc.health` directly: keep the fields as
  read-through properties for one release, with a Problems warning, then remove (no permanent duplicate).
- `SurvivalStats` has the `{health}` / `healthMax` prefs publishing in `StatComponent`; two writers of the same key
  must not exist after PR 2 (only `HealthStat` publishes).
- Multiplayer: server must remain the only place applying damage; test with the existing net sim panel.

### What to verify on a real run (per PR)
1. Open the existing sample scenes saved by main in each affected template: Top Down Shooter, Third Person Shooter,
   Survival, NPC sample, inventory/chest sample. Play 2 minutes; health bars, death, respawn, pickup, chest, dialogue.
2. Save and reopen in the editor; diff the scene text with `scripts`/SceneDiff: only intended lines changed.
3. Host + one client with the net sim: shoot, take damage, die, respawn; seat enter/exit from both sides (PR 6).
4. Build a game with the export check (`tests/export/verify.sh`) after PR 8, with one module off.
5. Visual script: open a graph using Damage/Heal/Get Health on an NPC after PR 2 (it must work now).

## 5. Open questions for the owner

1. Health merge: remove the duplicated health fields from `SurvivalStats`, `Stats` and `NPCController` in PR 2, or
   keep them for a release as forwarding properties?
   Recommended: forward for one release (script and VS compatibility), remove next release.
2. Where does the player's ammo live: in the `Inventory` (as item stacks) or as a number on the weapon?
   Recommended: Inventory when one exists, plain number on `Weapon` otherwise (as in 2.5). No new item model.
3. Do we bump the scene format version for PR 2?
   Recommended: yes, 3 -> 4 with a `needsEngine` migration step, because objects gain a `HealthStat` line; it keeps
   the "newer file" warning meaningful for older players.
4. Module removal: block only the menus (data stays) or also strip code from the player build?
   Recommended: menus and assets only in this plan; compile-time stripping later, only if player size matters.
5. Is the Modules page default "all on" for existing projects and "recommended set" for new ones?
   Recommended: existing = all on (no change), new project asks for a preset (shooter, survival, vehicles, none).
6. Should `Interactable` use `InputActions` ("interact") for its key, which needs a default binding seeded for new
   projects?
   Recommended: yes, with fallback to key `e` when the action is not defined, so nothing breaks before the input
   defaults work lands.
