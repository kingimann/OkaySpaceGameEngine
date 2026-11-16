# Save games

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
- **Where things are part-way through** (the slot's state section, see below):
  - **Logic doors, lifts and levers:** open or shut, how far a door or mover has
    travelled, and the closed pose it opens from. A door saved open loads open and
    still closes to the pose it was built in. A lever left on stays on.
  - **Animations:** an `Animator`'s clip time and a `SpriteAnimator`'s clip, time,
    frame and hold. A one-shot that had finished stays finished; one half-way
    through carries on from there.
  - **Projectiles:** a `Projectile3D`'s age, hits and bounces (its position and
    velocity are in the scene). A bullet in flight keeps its remaining lifetime,
    pierce and bounce budget.

Slots are files in the player's save folder, under `saves/<slot>.okaygame`. A
slot name uses letters, digits, `-` and `_` (up to 64): `1`, `quick`, `autosave`,
`chapter_2`. Names Windows treats as devices (`con`, `prn`, `aux`, `nul`, `com1`-`com9`,
`lpt1`-`lpt9`, any case) are refused, so a save works the same on every platform.

## From scripts

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

## Autosave

Add **Auto Save** to any object (Add Component, then search "Auto Save"):

| Setting | What it does |
| --- | --- |
| Every (s) | Seconds of play between saves. 0 means only when the scene starts. |
| Save when the scene starts | A checkpoint each time a level loads. |
| Slot | `autosave` by default. |
| Label | What a load menu shows. |

## Load menus

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

## Notes

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

## Runtime state (the `--state--` section)

A slot holds the scene text (the same text as a scene file, with every object where it is
now) and then a `--state--` section with what the scene text can't hold: Rigidbody
velocities (`body3`, `body2`), script and graph variables (`script`, `graph`), the save
keys (`kv`) and component runtime state. `SaveGame::LoadNow` (and `Load`, at the end of the
frame) reads the scene, starts it, and then puts the state lines back, before the first
frame runs. Each component line is `kind<TAB>object path<TAB>index<TAB>numbers`, where the
index is the component's place among its kind on that object:

| Kind | Component | Numbers |
| --- | --- | --- |
| `logic` | Logic Target | state, last input, powered, started, travel 0..1, mover direction, has closed pose, closed position (3), closed rotation (4) |
| `lsrc` | Logic Source | output, last output, started, timer |
| `anim` | Animator | clip time, events primed |
| `sprite` | Sprite Animator | clip index (-1 = base frames), clock, frame, hold, playing |
| `proj3` | Projectile 3D | age, hits, bounces |

A line is only written when the component has moved off its starting state. A build that
doesn't know a kind skips the line, and a slot saved before these lines existed loads as it
did before (doors take the loaded pose as closed, animations and bullets start over).
Scene files and prefabs are not changed by this: the lines live only in save slots.

Why the closed pose is stored: a Logic Target door measures its open pose from its closed
pose, and it takes the closed pose from the transform whenever it is fully shut. The scene
text holds the transform as it is, so a door saved open used to load with its open pose
taken as "closed" and could never close again (#888 finding 62).

## What changed (hardening pass)

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

Not changed: `SaveGame` and the save framework (`docs/save_framework.md`) are still two
formats; a game picks one per slot name. The framework's `Saveable` component now has a scene-file
record (`saveable`), and there is a no-code way to use it: see "Saving without code" in
`docs/save_framework.md`. `CheckpointTrigger` still has no scene-file record.
