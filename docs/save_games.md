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
  versioning (version 1, no checksum) still load. A damaged first line is caught
  too: see "Damaged first lines" below.
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

## Damaged first lines (#888 finding 47)

Before this fix, the end marker and checksum were only checked when the first line named the
current format exactly (`okaysave 3`, or `okaygame 2` and up). A byte changed in that first line
(a bad disk sector, a sync tool, a hand edit) made the file look like an old one: it loaded with no
check, edited or cut-off values came back as if they were fine, and the good `.bak` was never used.
In a test that changed one random byte of a save file 5000 times, 608 loads came back "fine" with
wrong data; now none do (every change is caught and the backup is loaded).

The rules now, for both `SaveFile` (`save("gold", 120)`) and `SaveGame` (slots):

- **A file that has the end marker and checksum line is always checked**, whatever its first line
  says (`#end` / `#sum` for `SaveFile`, `--end--` / `checksum` for slots). The checksum covers the
  file from its first byte, so a changed first line fails it like a change anywhere else. A
  mismatch is damage: the `.bak` is loaded instead (`LoadedFromBackup()`,
  `SaveGame::LastLoadUsedBackup()`), and the Console says "... was damaged and its backup was
  loaded instead".
- **A first line that starts with `okaysave` or `okaygame` must go on with one space and a
  number**, because that is all any version ever wrote. Anything else (`okaygame ,`,
  `okaysavex 3`, no number, `0`) is damage, reported as "has an unknown format version '...'",
  never "an old file".
- **A higher number than this build knows is a newer format.** It is refused, never parsed:
  `SaveGame` reports `Newer` (as before) and `SaveFile::Load` returns false with an empty store and
  `LastError()` saying "was saved by a newer version of the game". Its `.bak` is not loaded
  instead (it is most likely the same newer build's file, and an older one would quietly undo
  what was played in the newer build). When this build saves over it, the newer file is first
  copied to `<file>.newer.bak` (slots: `<slot>.okaygame.newer.bak`), so going back to the newer
  build finds it. If the file has a checksum that does not match, it is damage instead (a damaged
  digit), and the `.bak` is loaded.
- **A `SaveFile` with no header at all** (the oldest layout: plain `key=value` lines) must start
  with a `key=value` line. A file whose first line is neither a header nor a key is not a save
  file, so a damaged first line on a file that was also cut short is caught as well.
- Saving over a slot or file whose first line is damaged never makes it the `.bak`.
- A UTF-8 byte order mark that an editor put in front of a hand-edited `SaveFile` is ignored.

Old files that still load exactly as before (each is covered by `tests/test_save_header_damage.cpp`,
built by hand in the old layout): a `SaveFile` with no header (plain lines, nothing escaped), `okaysave 1`
(nothing escaped, also with Windows line ends), `okaysave 2` (escaped, no end marker) and `okaysave 3`;
a slot in format 1 (`okaygame 1`, no end marker, also with Windows line ends). The only file that loads
differently is a hand edit that changed the first line to `okaysave 2` but left the `#end` / `#sum`
lines in: it is now checked, so the edit is refused and the backup loaded (the error says to delete
those lines).

## Prefs and framework slots: damaged first lines

The same hole was in two more places, found while fixing finding 47:

- **Prefs** (`game.okayprefs`, the settings and high-score file) read its first line with `atoi`.
  `okayprefs 2` with a damaged digit read as version 1, and version 1 files are not checked for
  their `#end` line, so a file that was cut short and had a damaged first line loaded as if it were
  whole. A clean higher number (`okayprefs 9`) was read as if this build knew that format. Prefs also
  had no checksum, so any changed byte in a key or value loaded silently.
- **Framework slots** (`<slot>.okayslot`, `docs/save_framework.md`) read `okayslot 1` with `atoi`. A
  damaged digit (`okayslot 7`) made a damaged slot look like one from a newer build: it was refused
  instead of loading its `.bak`, listed as "incompatible", and kept as `.newer.bak` on the next save.

What happens now:

- **Prefs files are written as `okayprefs 3`**: the same `key<TAB>value` lines, then `#end` and a
  `#sum <16 hex>` checksum line (the same trailer as `SaveFile`). Older builds still read them (they
  skip the `#sum` line).
- **The `#end` / `#sum` lines are checked whenever they are there**, whatever the first line says. A
  changed byte anywhere (first line included) is damage: the `.bak` is loaded
  (`Prefs::LastLoadUsedBackup()`), and the Console says `Prefs file ... was damaged and its backup was
  loaded instead: it ...`. With no good backup, `Load` returns false and the settings the game already
  had are kept (the Console says so).
- **A first line that starts `okayprefs` or `okayslot` must go on with one space and a number.**
  Anything else is damage ("has an unknown format version"), never "an old file".
- **A higher number is a newer format**, refused and never parsed: `Prefs::Load` returns false, the
  current settings stay, `LastError()` says "was saved by a newer version", and the `.bak` is not
  used. The next `Prefs::Save` first copies it to `<file>.newer.bak`. A slot with a higher number is
  only called newer when its `headerSum` line matches (or it has none); otherwise it is damage and its
  `.bak` is loaded.
- **Editing the prefs file by hand.** An edit to an `okayprefs 3` file fails its checksum, so the
  backup is loaded instead (the error says how to edit it). To edit on purpose, set the first line
  to `okayprefs 2` and delete the `#sum` line (keep `#end`). The next save writes `okayprefs 3` again.

Old files that still load (each built by hand in `tests/test_prefs_slot_header.cpp`): prefs with no
header (`key<TAB>value` lines only); `okayprefs 1` as #24 wrote it (only `\\` and `\n` escaped, a raw
tab inside a value); `okayprefs 1` as #279 wrote it (`\t` and `\r` escaped too); `okayprefs 2` with its
`#end` line (#552, #611), also with Windows line ends or a byte order mark; and `okayslot 1`, the only
slot container version ever written (#572 on). Not accepted any more: a header-less prefs file whose
first line is not a key line, or that has an `#end` line (no writer ever made one; this is what a
prefs file with a destroyed first line looks like).

Fuzz (`tests/test_prefs_slot_header.cpp`, 5000 random one-byte changes per format, fixed seed): before
this fix 4044 of the 5000 prefs changes loaded "fine" with wrong data (43 of them from a byte in the
first line), and slots had 0 (their checksums already caught every random change; the `okayslot 7`
case above is a hand-made one, refused as "newer" before the fix); now 0 silent wrong loads and 0 damaged files mistaken for newer ones in both.

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
  same keys now write the same bytes (keys are sorted). To hand-edit a file, change its first
  line to `okaysave 2` **and delete the `#end` and `#sum` lines**: a file that still has them is
  checked (see "Damaged first lines" below), so an edit with them left in is refused and the
  backup is loaded.
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
