# Save games

> **What this is:** Save slots and the save game framework underneath them.
> **Status:** combined on 2026-11-20 from 2 older pages, one section each (listed below). Every fact was kept; text marked **Outdated** is kept for history.
> **Last checked:** 2026-11-20, paths and links against main.

## Contents

- [Save games](#save-games-1) (was `docs/save_games.md`)
- [Save game framework](#save-game-framework) (was `docs/save_framework.md`)

## Save games

*Was `docs/save_games.md` until 2026-11-20.*

A save game is the whole game as it is right now, in a named **slot**. Load
it and everything comes back:

- **Every object in the scene.** That includes where each object is, what it
  carries, and what it's set to.
  - **The player:** position, health and anything else on their components.
  - **Inventories, chests and placed builds:** what's in them and where they are.
  - **Objects spawned while playing**, like a dropped torch or a placed wall.
  - **Objects destroyed while playing**, like a coin that was picked up. These
    stay gone.
- **Rigidbody velocities**, so a crate that was falling keeps falling.
- **Script variables**, from Script components and Visual Script graphs.
- **The game's own Save keys**: `save("gold", 120)` values in the default save
  file. Each slot has its own copy, so slots don't share progress.
- **Play time.**

Slots are files in the player's save folder, under `saves/<slot>.okaygame`. A
slot name uses letters, digits, `-` and `_` (up to 64): `1`, `quick`, `autosave`,
`chapter_2`. Names Windows treats as devices (`con`, `prn`, `aux`, `nul`, `com1`-`com9`,
`lpt1`-`lpt9`, any case) are refused, so a save works the same on every platform.

### From scripts

C#-style scripts and visual scripts (the **SaveGame** nodes):

```csharp
SaveGame.Save("1", "Chapter 2: The Mine");   // false if it couldn't
if (SaveGame.Exists("1")) SaveGame.Load("1");
string label = SaveGame.Label("1");
SaveGame.Delete("1");
float played = SaveGame.playTime;            // seconds, carried through loads
```

OkayScript:

```
save_game("1", "Chapter 2: The Mine")
if has_save("1"): load_game("1")
label = save_label("1")
delete_save("1")
t = play_time()
```

Load-menu helpers (each takes a slot, `save1` when left out):

```
for slot in save_slots() {            // every slot, newest first, damaged ones included
    if (save_valid(slot)) {            // would it load? (its file or its backup passes the checks)
        var text = save_label(slot) + "  " + save_play_text(slot) + "  " + save_ago(slot);
        // save_scene(slot), save_play_time(slot) (seconds), save_date(slot) "2026-11-08 14:03" (UTC)
    }
}
```

`save_game()` and `load_game()` with no slot use `save1`. Loading happens at
the end of the frame, so it's safe to call from a button, a trigger or
`update()`. The scripts in the loaded scene start again, then get their saved
variables back.

### Autosave

Add **Auto Save** to any object (Add Component, then search "Auto Save"):

| Setting | What it does |
| --- | --- |
| Every (s) | Seconds of play between saves. 0 means only when the scene starts. |
| Save when the scene starts | A checkpoint each time a level loads. |
| Slot | `autosave` by default. |
| Label | What a load menu shows. |

### Load menus

`SaveGame::List()` (C++) returns every slot on disk, newest first, each with
its label, scene name, the time it was saved, the play time, the file size and
the flags a menu needs: `corrupt` (not a save file, or, with `List(true)`, failed
its checksum), `incompatible` (saved by a newer build) and `fromBackup` (the main
file is damaged; the values shown are the backup's). Damaged slots are listed,
not hidden, so a player sees that a save exists. Use it to fill a load screen.

To show a picture and your own text in the menu, save with options:

```cpp
SaveOptions so;
so.label = "Chapter 2: The Mine";
so.thumbnail = "slot1.png";            // a file in the saves folder; Delete removes it with the slot
so.gameVersion = "1.4.2";
so.fields["difficulty"] = "hard";
SaveGame::Save(scene, "slot1", so, &err);
const SaveSlotInfo i = SaveGame::Info("slot1");   // i.thumbnail is the full path, i.fields["difficulty"] == "hard"
```

`SaveGame::ThumbnailPath(slot)` is where a screenshot for the slot goes (`saves/<slot>.png`).
A thumbnail given with folders in its path is yours: it is stored as given and never deleted.
`Info(slot)` reads only the header lines, so a file cut short after its header still
looks fine there; `Info(slot, true)`, `List(true)` and `SaveGame::Check(slot, &why)` read the
whole file and check it (use them for one slot, not a long list of big saves).

### Notes

- A save is written to a temporary file, flushed to the disk and then swapped in,
  so a crash mid-save never leaves a broken slot. If the write fails (full disk, no
  permission, read-only folder) `SaveGame::Save` returns false, the old slot is
  untouched and `error` says why ("the disk is full", "permission denied...").
- Each file starts with `okaygame <version>` and ends with an end marker and
  a checksum line. A file that is cut short, edited by hand or otherwise
  damaged is detected and refused rather than half loaded. Files from before
  versioning (version 1, no checksum) still load.
- A file written by a **newer** version of the game is refused with a message
  ("saved by a newer version ..."); it is never half loaded. It is still listed
  (`incompatible`), and if this older build saves over that slot the newer file is
  first kept as `<slot>.okaygame.newer.bak`, so going back to the newer build finds it.
  To upgrade older files when you change the format, register a step with
  `SaveGame::SetMigration(fromVersion, fn)`; the steps run in order on the
  file's text before it is read.
- When a slot is saved over, the previous good file is kept as
  `<slot>.okaygame.bak`. If the main file fails its checks, loading uses the
  `.bak` instead; the load still succeeds, `error` says what happened and
  `SaveGame::LastLoadUsedBackup()` returns true. A damaged file never replaces
  a good `.bak`.
- Load menus: `SaveGame::List()` and `Info()` read only the header lines of
  each slot, not the scene, so listing many large saves is cheap.
- A slot file over 256 MB, or with more than 8 million lines, is refused
  (`SaveGame::SetLimits` changes the caps for tools).
- **Loading a slot replaces the default Save file.** The `save("gold", 120)`
  keys in the slot overwrite the game's default save file, so anything kept
  there (unlocks, settings, achievements) is rolled back to what it was when
  the slot was made. Keep data that must survive across slots in a separate
  file (`save("unlocked", true, "profile.okaysave")` / `Prefs`), which a slot
  load never touches.
- Saves made with the older `save_game` (`saves/<slot>.okaysave`, scene only)
  don't load with the new format. Save again to make a new one.

### What changed (hardening pass)

- **One crash-safe write for everything.** `savefmt::AtomicWriteChecked` (`Core/SaveData.hpp`,
  code in `Core/SaveFile.cpp`) is the single write that `SaveGame`, `SaveFile`, `Prefs`, the
  save framework, string tables and the other writers share: temp file, flush, optional
  `fsync` (`WriteOptions::sync`: on for slots and prefs, off for the `Save` facade, which
  writes on every `Set`), rename, with a reason on failure.
  Two threads writing one path take turns (they used to share the temp file and one write
  could fail or mix); it no longer deletes the old file before a rename that might fail.
- **`SaveFile` (the `save("gold", 120)` store) notices damage.** New files start `okaysave 3` and
  end with `#end` and a `#sum` checksum line, so a file cut off in a line, edited or zero-filled
  is refused instead of loading half its keys. The previous good file is kept as `<file>.bak`
  and read when the main one fails (`SaveFile::LoadedFromBackup()`, `LastError()`); a damaged
  file with no good backup is set aside as `<file>.corrupt` instead of being overwritten.
  Older files (`okaysave 1` and `2`) load as before and are upgraded on the next save. The
  same keys now write the same bytes (keys are sorted). Change the header to `okaysave 2` to
  hand-edit a file.
- **The `Save` facade** creates the folder it writes into, takes a lock (its static calls are
  safe from several threads; a `SaveFile&` kept from `Save::File` is not), reports a failed write
  in `Save::LastError()`, has `Save::Reload(file)` and removes the backup with `DeleteFile`.
- **Numbers.** Infinity and NaN were saved as text that read back as 0; they round-trip now.
  `GetInt` clamps a value beyond the int range (it was undefined) and reads `1e+09` as 1000000000.
  A vector with fewer than three numbers gives the default.
- **Slot listing.** A long label no longer pushes the save time out of the header read (the
  slot listed as 1970). Slots from a newer build, damaged slots and slots restored from their
  backup are listed and flagged. `Info` also gives the file size, `thumbnail`, `gameVersion` and
  `fields`. A migration step that changed the header length no longer shifts the parse.
- **Prefs** read files saved with CR LF line ends (they were refused as cut off), refuse a
  zero-filled file instead of loading it as an empty store, keep `<file>.bak` and fall back to it
  (`Prefs::LastLoadUsedBackup()`, `LastError()`), write keys in order, and can be used from
  several threads (listeners run outside the lock). `GetInt` of a float's text (`1e+09`) works.
- **Script builtins** for load menus: `save_slots`, `save_valid`, `save_scene`, `save_play_time`,
  `save_play_text`, `save_date`, `save_ago`.

Not changed: `SaveGame` and the save framework (`docs/engine/save_games.md`) are still two
formats; a game picks one per slot name. The framework's `Saveable` component now has a scene-file
record (`saveable`), and there is a no-code way to use it: see "Saving without code" in
`docs/engine/save_games.md`. `CheckpointTrigger` still has no scene-file record.

## Save game framework

*Was `docs/save_framework.md` until 2026-11-20.*

`save_games.md` describes **SaveGame**: one call saves the whole scene to a
slot, and loading puts the whole scene back. The save framework is a second
layer for games that want more control. It stores *state*, not the whole scene:
the level is loaded from its file as usual and the saved state is put back on
top, matched object by object.

It adds:

- **Saveable objects**: a `Saveable` component, typed field helpers, and
  `SaveSystem::CaptureScene` / `ApplyScene`.
- **Checkpoints, quick save, a ring of autosaves, and Continue.**
- **Slot headers** a menu can read without loading the save, plus listing,
  copy, rename and delete.
- **A version migration chain** and a refusal of saves from newer builds.
- **Sync hooks** (`ISaveSync`) with in-memory and folder backends.
- **`SaveMenuModel`**: rows a UI binds to, with no rendering in it.
- **A no-code route**: the `Saveable` component in the editor and the scene file, a `Save Settings`
  component, `GameSaves` (one call per action), script builtins, visual-script nodes, Action List
  actions and a Save Slots screen. See "Saving without code" just below.

### Saving without code

You do not need C++ to use the framework.

**1. Mark what is saved.** In the editor, Add Component > `Saveable (keep when saving)` on every
object that should be kept (the player, a door, a coin, an enemy). The component is stored in the
scene file, so a level loaded from a file has it. It lists what of the object is saved:

| Setting | Saves |
| --- | --- |
| Save id | The stable key the object is found by. A fresh unique id is made when the component is added; press **New id** on a duplicated object. A warning shows when another object has the same id. |
| Position, rotation, scale | The local transform. |
| Active (on / off) | Whether the object is active. |
| Rigidbody, Inventory, Stats | Velocities, the bag, health and attributes. |
| Script and visual-script variables | The object's text-script globals and the variables of its visual scripts. |
| Scene-wide variables | Action List numbers and text, and the visual scripts' scene and global variables, once per save. Tick it on one manager object. |
| Also save these properties | Any registered component property, picked from a list ("Light.intensity", "Health.current"). They go through the property registry (`docs/engine/properties.md`), so they are stored as text and put back with the same checks a Set Property node uses. |
| Made while playing | The object was spawned from a prefab at run time. It is recreated on load from the prefab path (or `factory:Name`); a spawned object that did not exist at the save is removed. |

Objects *without* a Saveable are not stored: they come back as the level file has them. An object
the level contains that was **destroyed** before the save stays destroyed after a load (the save
records it), including after the level is reloaded from its file.

**2. Autosave setting.** Add Component > `Save Settings (autosave)` (class `SaveConfig`) to one object per scene:
*Autosave every (s)* (0 = off), *Autosave when the scene starts* and *Autosave slots* (the ring
`autosave_1` ... `autosave_N`; the oldest is replaced first). A scene-start autosave is skipped
while a load is putting a save back, and so is the interval one.

**3. Save, load, delete, continue.** Every route calls `GameSaves` (`okay/Core/GameSaves.hpp`):

| What | Text script | Visual script node | Action List action |
| --- | --- | --- | --- |
| Save to a slot | `game_save("slot_1", "label")` | `Fn.game_save` | `game_save slot [label...]` |
| Load a slot | `game_load("slot_1")` | `Fn.game_load` | `game_load slot` |
| Continue (newest valid save) | `game_continue()` | `Fn.game_continue` | `game_continue` |
| Delete a slot | `game_delete_slot("slot_1")` | `Fn.game_delete_slot` | `game_delete_slot slot` |
| Slot exists | `game_slot_exists("slot_1")` | `Fn.game_slot_exists` | `game_slot_exists slot intoVar` |
| Is there a save to continue | `game_can_continue()` | `Fn.game_can_continue` | `game_can_continue intoVar` |
| When it was saved (text) | `game_slot_text("slot_1")` | `Fn.game_slot_text` | `game_slot_text slot intoTextVar` |
| Seconds since it was saved | `game_slot_age("slot_1")` | `Fn.game_slot_age` | `game_slot_age slot intoVar` |
| The label it was saved with | `game_slot_label("slot_1")` | `Fn.game_slot_label` | (use `game_slot_text`) |
| Save into the autosave ring | `game_autosave()` | `Fn.game_autosave` | `game_autosave` |

`game_load` happens at the start of the next frame, so it is safe from an Update or a button. If the
slot was saved in a scene that is in the build list, that level is loaded first and the state is put
on it (objects are matched by save id), so Continue from a main menu goes to the right level. Slot
names are letters, digits, `-` and `_`. The saved time text is UTC, like `save_date`.

**4. A ready screen.** UI > Prebuilt Screens > Menus > `Save Slots` adds three slots, each with its
label and saved time (or "Empty") and **Save**, **Load**, **Delete** buttons, plus **Continue**, **Close**
and a status line. It is built from the script builtins above, so it is editable like any other UI.

**Errors.** A missing slot, a damaged one (it fails its checksum, or is cut short) and one saved by a
newer version of the game are refused at the call, return false and write a `[save]` error to the
Console naming the slot and the reason ("was saved by a newer version of the game ..."); the scene is
left untouched. A damaged main file whose `.bak` is good loads from the backup with a warning. Continue
skips damaged and newer-version slots, and says why when nothing is left.

**Versions and migration.** The format is versioned (see "Versions and migrations" below). A game that
changes what it saves raises `SaveStore::SetDataVersion` and registers a `RegisterMigration` step; the
no-code route uses the store behind `GameSaves::Manager()` (`SaveManager::Default().Store()`), so call
those on it at startup. Older saves are upgraded when they are read.

**Limits.** Only a save remembers destroyed objects: leave a level without saving and come back, and
the level file's objects are all there again. Numbers, booleans, text, vectors, colours, lists and maps are stored (lists and maps nested, as
value text); a variable holding an object reference is not, and an object inside a list comes back
as its name. A visual script's variables are matched to the object's first, second ...
visual script by order. A Saveable on an object that is inactive when the level loads is noted when it
first becomes active.

```cpp
#include "okay/Core/SaveFramework.hpp"
```

Framework slots are files named `<slot>.okayslot` in the save folder
(`saves/` under `Save::BaseDir()` by default). They never touch SaveGame's
`<slot>.okaygame` files, and each system lists only its own.

### Quick start

```cpp
SaveManager& saves = SaveManager::Default();

// Mark what should be saved (see "Saveable objects").
player->AddComponent<Saveable>()->saveId = "player";

SaveRequest req;
req.label = "Chapter 2";
saves.Save(scene, "slot_1", req);
saves.QuickSave(scene);                 // slot "quicksave"
saves.AutoSave(scene);                  // autosave_1 ... autosave_3, rotating
if (saves.CanContinue()) saves.Continue(scene);   // newest slot that loads
saves.Load(scene, "slot_1");
```

`SaveStore` is the file layer underneath (`SaveStore::Default()`, or
`SaveStore store("some/folder")` for your own, which is also how the tests run
in a temp folder). `SaveSystem` is the scene capture layer. `SaveManager` ties
them together.

### Saveable objects

An object is saved when it has a **Saveable** component. Two other ways to pick
objects exist: `SaveCaptureOptions::includeTag` (every object with that tag) and
`allObjects` (every object that isn't transient).

| Saveable setting | What it does |
| --- | --- |
| `saveId` | The object's stable key. Survives renaming and re-parenting. |
| `dynamic` | The object was spawned while playing; it is respawned on load. |
| `prefab` | `.okayprefab` path (or `factory:Name`) a dynamic object respawns from. |
| `saveTransform`, `saveActive` | Store the local position/rotation/scale, and whether it is active. |
| `saveComponents` | Also store Rigidbody, Inventory and Stats (below). |
| `properties` | Property-registry paths to store too ("Light.intensity"). |
| `saveVariables`, `saveSceneVariables` | The object's script / visual-script variables; the scene-wide Action List and visual-script variables. |
| `onSave`, `onLoad` | Lambdas for quick custom fields. |

For your own fields, subclass it:

```cpp
class Door : public Saveable {
public:
    bool open = false;
    int uses = 0;
    std::string SaveName() const override { return "door"; }
    void OnSave(SaveWriter& w) override { w.Bool("open", open); w.Int("uses", uses); }
    void OnLoad(SaveReader& r) override { open = r.Bool("open", open); uses = r.Int("uses", uses); }
    void OnLoadComplete() override { /* every object is back: fix up references */ }
};
```

Any other component can take part too: derive from `ISaveable` as well as
`Component`. `SaveWriter` has `Int`, `Int64`, `Float`, `Bool`, `String`,
`Vector3`, `Rotation`, `Strings`, and `Scope("name")` to nest fields. Every
`SaveReader` getter takes the value to use when the field is missing, so
`open = r.Bool("open", open)` leaves a field alone that an older save never wrote.

#### Keys: how an object is found again

`GameObject` has no GUID in this build, so an object is keyed by:

- `id:<saveId>`, if its Saveable has a `saveId`, otherwise
- `path:Root/Child`: the names from the root down, with `#1`, `#2` for the
  second and third sibling with the same name (`path:Crate#1`).

A path key breaks when the object is renamed or a same-named sibling before it
is destroyed. **Give a `saveId` to anything that can be destroyed, renamed or
that has same-named siblings** (coins, enemies, pickups). Children of an object
with a `saveId` are keyed under it (`id:chest-4/Lid`).
`SaveSystem::KeyOf(go)` returns the key.

#### What is stored

For each saved object: active, local transform, every `ISaveable` component's
fields, and the built-in adapters:

| Adapter | Stored |
| --- | --- |
| `rigidbody3d`, `rigidbody2d` | velocity and angular velocity |
| `inventory` | capacity and every slot (item, count, durability, uid, custom values) |
| `stats` | health, mana, level, xp and the attributes |

Restoring an `Inventory` calls its `NotifyChanged()`, as carrying a bag to the
next scene does, so UI refreshes. Add your own with `SaveSystem::RegisterAdapter`
(a name plus `save` and `load` functions), and `ResetAdapters()` puts the
built-ins back.

#### Spawned and destroyed objects

- **Spawned while playing.** Create them with
  `SaveSystem::Spawn(scene, "prefabs/bullet.okayprefab", position)` (or any
  object with a Saveable whose `dynamic` is true). They get a fresh unique id and
  are saved with their prefab. On load, a missing one is built from the prefab
  again and then gets its state. For objects built in code, register a factory
  and use `"factory:Torch"` as the prefab:

  ```cpp
  SaveSystem::RegisterFactory("Torch", [](Scene& s) {
      GameObject* g = s.CreateGameObject("Torch");
      g->AddComponent<TorchSave>();
      return g;
  });
  ```

  A dynamic object that exists in the scene but isn't in the save was made
  *after* the save, so loading removes it (turn that off with
  `SaveApplyOptions::removeUnsavedDynamic`). A dynamic object with no prefab can't
  be respawned; `CaptureScene` says so in its report.
- **Destroyed while playing.** Call `SaveSystem::MarkSceneLoaded(scene)` right
  after a level loads. It remembers which objects the level started with. A later
  capture records the ones that are gone, and loading destroys them again even
  though the level file still has them.

#### Capturing and applying

```cpp
SaveDocument doc = SaveSystem::CaptureScene(scene);        // + SaveCaptureReport for warnings
doc.Writer("game").Int("gold", 120);                       // your own blocks go beside it
...
SaveApplyReport r = SaveSystem::ApplyScene(scene, doc);    // restored, spawned, destroyed, missing
```

- Call `ApplyScene` **between frames**, not from inside a component's `Update`.
  It can create objects and start them. `SaveManager::Load` with
  `deferred = true` (or `reloadScene`) picks a safe moment for you.
- Objects queued with `Scene::Destroy` count as present until the frame ends,
  so save at the start of a frame, or in a deferred save.
- The scene file stores the Saveable component (record `saveable`), so a level loaded
  from a file has it. For objects without one, attach them from code in
  `SaveApplyOptions::prepare` (it runs after the level loads, before state goes
  back), or mark objects with a tag and save with `includeTag`.
- `SaveSystem::VerifyCapture(scene)` is a debug check: it captures, writes the
  text, parses it, and compares. It also fails on two objects with the same key.

### Slots

```cpp
SaveRequest req;
req.label = "Chapter 2: The Mine";
req.thumbnail = "slot_1.png";            // a path you made; a bare name beside the slots is moved with the slot
req.fields["difficulty"] = "hard";       // custom text
req.extra.Writer("game").Int("gold", 120);   // or req.collect = [](SaveDocument& d) { ... };
saves.Save(scene, "slot_1", req);

SaveLoadOptions lo;
lo.onLoaded = [](const SaveDocument& doc, const SaveSlotInfo& info) { /* read your blocks */ };
saves.Load(scene, "slot_1", lo);
```

`SaveLoadOptions::reloadScene` loads a level file first and puts the state on it
at the end of the frame (`onDone` tells you how it went). `SaveRequest::deferred`
and `SaveLoadOptions::deferred` run at the start of the next frame.
Play time (`SaveSystem::PlayTime()`) is stored and carried through loads.

#### Slot metadata

Every slot starts with a header: `SaveSlotInfo` with `slot`, `label`, `scene`,
`savedAt` / `savedAtMs`, `playTime`, `thumbnail`, `gameVersion`, `kind`
(`manual`, `quick`, `auto`, `checkpoint`), `fields`, `version` (the data version),
`sizeBytes`, and the flags `corrupt`, `incompatible`, `fromBackup` with an
`error` text. The header has its own checksum and records the payload's size
and checksum, so:

- `store.Info(slot)` and `store.ListSlots()` read only the header. A file that
  is cut short or has an edited header is flagged without reading the payload.
- `store.Info(slot, true)` / `ListSlots(sort, true)` also read the whole file
  and check the payload checksum, which finds a flipped byte.

`ListSlots(SlotSort::Newest | Oldest | Name | PlayTime)` includes every slot
file, damaged ones too (flagged, and last when their time is unknown).
`store.Delete`, `Copy(from, to, overwrite)`, `Rename(from, to, overwrite)` and
`SetLabel` / `UpdateInfo` (header-only edits that leave the payload bytes alone)
work on the slot with its `.bak` files and a thumbnail named `<slot>.<ext>`
beside it. A thumbnail path anywhere else is yours and is never touched.

#### Durability

Same rules as SaveGame: files are written aside and swapped in, the previous
good file becomes `<slot>.okayslot.bak`, and a damaged main file falls back to
its `.bak` on load (the load succeeds, `SaveLoadReport::usedBackup` is set and
the error text says what happened). A damaged file never replaces a good `.bak`.
`SetMaxBytes` caps the file size. The write is the one every save writer shares
(`savefmt::AtomicWriteChecked`: temp file, flush to the disk, rename); a failed write says why
("the disk is full", "permission denied...") and leaves the slot as it was.

### Quick save, autosaves, continue

- `QuickSave` / `QuickLoad` use the slot `quicksave` (`SetQuickSlot` to change).
- `AutoSave` writes a ring of `autosave_1` ... `autosave_N` (`SetAutosaveCount`,
  default 3): each save goes to the first empty place, then to the oldest. A
  damaged slot in the ring is replaced first. `NextAutosaveSlot()` says where
  the next one goes.
- `Continue` loads the **newest valid slot of any kind**. A slot from a newer
  build, one with a damaged header, and one whose payload fails its checksum are
  skipped. `CanContinue()` and `ContinueSlot()` answer without loading.

### Checkpoints

```cpp
CheckpointManager& cp = CheckpointManager::Global();
cp.Reach(scene, "bridge");           // saves slot "checkpoint_bridge"
cp.RestoreLast(scene);               // after the player dies
cp.Restore(scene, "start");
cp.Last(); cp.Names();               // newest first
cp.SetMaxCheckpoints(5);             // keep only the newest five
```

Add a **CheckpointTrigger** component to a gate or a flag: it fires when an
object with `targetTag` ("Player") comes within `radius`, or enters a trigger
collider on the object. `once` makes it fire a single time per level load, and
`deferred` (the default) saves at the start of the next frame, because the
trigger fires from inside an Update. It uses `CheckpointManager::Active()`: the
global one, or `SetActive(&mine)`.

A restore puts the scene state back to the moment the checkpoint was reached.
Pass `SaveLoadOptions::reloadScene` to reload the level first for a clean start.

### Versions and migrations

A slot records the **data version** of the build that wrote it. A build with
`store.SetDataVersion(4)` reads versions 1 to 4:

```cpp
store.SetDataVersion(3);
store.RegisterMigration(1, [](SaveDocument& d, std::string*) {   // 1 -> 2
    SaveBlock& g = d.Block("game");
    g.SetString("player.name", g.GetString("name"));
    g.Erase("name");
    return true;
});
store.RegisterMigration(2, [](SaveDocument& d, std::string* err) {   // 2 -> 3
    ...
    return true;                   // or set *err and return false
});
```

- Steps run in order on the parsed document, so a version 1 slot goes through
  the 1 -> 2 and then the 2 -> 3 step. A version with no step is left as it is.
- The file on disk isn't rewritten by a load; the next save writes the new
  version, and the old file becomes the `.bak`. `SaveLoadReport` lists the steps
  that ran.
- A step that fails stops the load with
  `Slot "x" couldn't be upgraded from data version 2 to 3: <your message>`.
- **A slot from a newer build is refused**: `was saved by a newer version of the
  game (save data version 5, this build reads up to 3). It was left untouched`.
  `Info` flags it `incompatible`, Continue skips it, and it is never half loaded.
  If this older build then saves over that slot, the newer file is first copied
  to `<slot>.okayslot.newer.bak`, so going back to the newer build finds it.
- `store.Verify(slot, &report)` is a debug check: it reads the main file whole,
  checks both checksums, parses it, runs the migrations on a copy, and checks
  that the payload round-trips. `store.SetVerifyAfterWrite(true)` reads every
  save back after writing it.

### Sync hooks

`ISaveSync` is a place slots can be mirrored to (`List`, `Upload`, `Download`,
`Remove`). Write one for your platform's cloud service; there is no network code
in the engine. `MemorySaveSync` and `FolderSaveSync` (a synced drive or share)
are included, and are what the tests use.

```cpp
FolderSaveSync cloud("D:/Sync/MyGame/saves");
SaveSyncer syncer(SaveStore::Default(), cloud);
SaveSyncer::Report r = syncer.Sync();       // r.uploaded, r.downloaded, r.conflicts ...
```

`Sync` uploads what only changed here, downloads what only changed there, and
leaves what matches alone. It remembers the checksum both sides held at the last
sync (`sync_state.txt` in the save folder) to tell which side changed. If both
changed, or the slot was never synced and the two differ, it is a **conflict**:

| `ConflictPolicy` | Result |
| --- | --- |
| `Newest` (default) | The later save time wins. The same time with different bytes stays unresolved. |
| `PreferLocal`, `PreferRemote` | That side wins. |
| `Ask` | `Options::resolve(conflict)` returns `UseLocal`, `UseRemote`, `KeepBoth` or `Skip`. |
| `Skip` | Nothing is changed; the conflict is counted in `unresolved`. |

`KeepBoth` downloads the cloud copy as `<slot>_remote` and uploads yours.
A downloaded slot replaces the local one but the local one becomes its `.bak`.
A damaged local slot is never uploaded, a damaged remote file is never installed,
and an unreachable backend gives `Report::ok == false` and changes nothing.
`syncer.Plan(items)` shows what a sync would do. Deleting is not synced.

### Menus

`SaveMenuModel` turns the store into rows a UI binds to:

```cpp
SaveMenuModel::Options o;
o.manualSlots = 6;                 // always list slot_1 ... slot_6, empty ones included
SaveMenuModel menu(store, o);
menu.Refresh();
for (const SaveMenuRow& row : menu.Rows()) { /* row.title, row.savedText, row.playTimeText ... */ }
```

Each `SaveMenuRow` has `slot`, `title` (the label, or "Empty slot"), `label`,
`scene`, `thumbnail`, `gameVersion`, `kind`, `fields`, ready-made text
(`savedText` "2026-11-08 14:03", `agoText` "5 minutes ago", `playTimeText`
"1h 05m"), `statusText` ("Damaged", "Saved by a newer version", "Restored from
backup"), and flags: `empty`, `corrupt`, `incompatible`, `fromBackup`, `isAuto`,
`isQuick`, `isCheckpoint`, `isNewest` (what Continue loads), `canLoad`, `canSave`
(manual slots only), `canDelete`.

Options choose which kinds to show (`includeAuto`, `includeQuick`,
`includeCheckpoints`), whether damaged slots appear (`showCorrupt`), the sort,
and `utcOffsetMinutes` to show local time. The model also keeps a selection
(`Select`, `SelectSlot`, `SelectNext`, `SelectPrevious`, `DeleteSelected`) that
survives a refresh, and `CanContinue()` / `ContinueSlot()` / `ContinueRow()`.
The formatting helpers `FormatPlayTime`, `FormatDateTime` and `FormatAgo` are
static and can be used on their own.

### Notes

- Use the framework from the game's main thread.
- Slot names use letters, digits, `-` and `_` (up to 64), like SaveGame's, and are not Windows
  device names (`con`, `nul`, `com1`...).
- Framework slots and SaveGame slots are separate files and separate lists. A
  game picks one for a given slot name; nothing converts between them.
- Loading a framework slot doesn't touch the default Save file (`save("gold", 1)`
  keys); put the values you want in a block of your own (`req.extra`).
