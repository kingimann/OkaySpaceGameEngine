# Undo, autosave, crash recovery and backups

Three small, headless building blocks in `okay/Core`, none of which knows about
scenes or UI, so the editor, other tools and tests can all use them:

| Header | What it is |
| --- | --- |
| `Undo.hpp`, `UndoCommands.hpp` | `UndoStack`: command based undo/redo with groups, merging, caps and dirty tracking, plus ready-made commands |
| `Recovery.hpp` | `RecoveryManager`: periodic autosave to rotating files, unclean-shutdown detection, restore |
| `Backup.hpp` | `BackupManager`: timestamped backups taken before a file is overwritten, with a prune policy |

None of them uses threads or timers. Time is passed in (`Tick(dt)`), and each class
takes an injectable clock, so tests are deterministic. They are not thread safe:
call them from the thread that owns the data.

Status: these are the engine side only. The editor's own undo is still the full
scene snapshot described in [editor.md](editor.md); see "Adopting it in the
editor" at the end of this page.

Include them directly (`#include <okay/Core/Undo.hpp>`, and so on).

## Undo and redo

### The stack

```cpp
UndoStack undo;

float speed = 1.0f;
UndoSet(undo, "Set Speed", &speed, 9.0f);   // applies it and records it
undo.Undo();                                // speed == 1
undo.Redo();                                // speed == 9
```

A command is anything that implements `ICommand`:

```cpp
struct ICommand {
    virtual bool Do() = 0;                  // first run and every redo
    virtual bool Undo() = 0;
    virtual std::string Name() const = 0;   // "Move Cube", for the Edit menu / history list
    virtual bool Merge(const ICommand& next);     // absorb a later edit (default: no)
    virtual std::size_t MemoryBytes() const;      // for the memory cap
    virtual bool IsNoOp() const;                  // changed nothing: not recorded
};
```

`Do()` and `Undo()` return false when the edit can't be applied (the target no
longer exists, a parse failed). The stack never gets stuck on such a step: it is
dropped, `LastError()` says why, and the redo branch is cleared because it can no
longer be trusted. A stale step at the save point also leaves the stack dirty.

Two ways to record:

- `Execute(cmd)` calls `Do()` and records the command. Use it when the code makes the change.
- `Record(cmd)` records a change that is already applied. Use it when a widget has already
  written the new value (ImGui sliders, drag fields).

A new command discards the redo branch. `UndoMany(n)` / `RedoMany(n)` walk several steps
(clicking an entry of a history list); `UndoNames()` / `RedoNames()` give the labels.

Edits made from inside `Do()`/`Undo()` or from a listener are refused (the stack is
"busy"), so code that reacts to "property changed" can't record a stray step while an undo is
running. `IsApplying()` lets such code skip the work.

### Groups

One user action that touches many things is one undo step:

```cpp
undo.BeginGroup("Delete 3 objects");
for (ObjectId id : selection) undo.Execute(DeleteObjectCommand::Make(ops, id, "Delete"));
undo.EndGroup();

{ UndoGroup g(undo, "Paste"); /* ... */ }   // RAII: EndGroup on scope exit, g.Cancel() rolls back
```

Commands inside a group are applied at once, so later ones see the effect of earlier ones.
Groups nest; only the outermost makes a history entry. An empty group records nothing.
`CancelGroup()` undoes everything done in the group and discards it.

Undoing or redoing a group is atomic. If a child fails half way, the children already done are
rolled back, so a group is never left half applied (the group is then dropped as stale).

### Merging: slider drags

```cpp
float before = intensity;
if (ImGui::SliderFloat("Intensity", &intensity, 0, 10))
    UndoRecordSet(undo, "Set Intensity", &intensity, before);
if (ImGui::IsItemDeactivatedAfterEdit())
    undo.BreakMerge();                      // the next drag is a new step
```

Consecutive commands that return true from `Merge` fold into the newest step: its `Undo` keeps
the first `before`, its `Do` gets the last `after`. `SetProperty` merges when the target (or
merge key) matches. If the value ends up where it started, the step is removed again.

Merging stops at `BreakMerge()`, at `MarkSaved()`, and on any undo or redo. With
`SetMergeWindow(seconds)` plus `Tick(dt)` a command also only merges if it follows the previous
one within that time (typing into a field). A group around a single edit with no name stays
mergeable.

### Capacity

`SetMaxCommands(n)` (default 200) and `SetMaxMemory(bytes)` (default 64 MiB; `0` means
unlimited) drop the oldest steps first. The newest step is always kept, even if it alone is
bigger than the memory cap. `MemoryBytes()` reports what the commands say they hold.

### Dirty tracking

```cpp
undo.MarkSaved();      // after writing the file
undo.IsDirty();        // false again whenever undo/redo lands back on the saved state
undo.MarkDirty();      // an untracked change happened
undo.Clear();          // after loading another scene: no history, state counts as saved
```

The saved state becomes unreachable (the stack stays dirty until the next `MarkSaved`) when its
steps are evicted, when a new command discards the redo branch that held it, or when a step on
the way back is stale.

### Listeners

`AddListener(fn)` is called after every change with a `Notice { event, name }`
(`Pushed`, `Merged`, `Undone`, `Redone`, `Failed`, `Evicted`, `Cleared`, `SavePoint`). `Revision()`
bumps on every change if you would rather poll.

## Ready-made commands (`UndoCommands.hpp`)

| Command | Use |
| --- | --- |
| `SetProperty<T>` | Set a value through a pointer (`SetProperty<float>("n", &x, before, after)`) or an accessor (a setter lambda that returns false if the target is gone). Merges drags. Helpers: `UndoSet`, `UndoRecordSet`. |
| `ListEdit<T>` | Add, remove or move an item of a `std::vector<T>`: `ListEdit<T>::Add/Remove/Move(name, &list, ...)`. The list can be reached through a getter that re-resolves it. |
| `SnapshotCommand` | Before and after state as text, plus a callback that loads a snapshot. The fallback for edits without a finer command. |
| `SnapshotRecorder` | Remembers the last snapshot so `Commit("Move Cube")` after an edit records `before -> after`. |
| `CreateObjectCommand`, `DeleteObjectCommand`, `ReparentObjectCommand` | Object lifecycle through ids (below). |
| `SetObjectProperty<Obj, T>` | Set one property of an object found by id. Merges drags of the same object and property. |

A pointer-form `SetProperty` keeps the raw pointer, so use it only for data that outlives the
history (settings, tool state). For anything inside a scene use ids.

### Whole-scene snapshots as a bridge

The editor today captures the whole scene as text on every step. That keeps working inside the
new stack, one step at a time:

```cpp
SnapshotRecorder snap(undo,
    [&] { return SceneSerializer::Serialize(scene); },          // capture
    [&](const std::string& s) { return Restore(scene, s); });   // apply
...
snap.Commit("Add Cube");     // after the edit
```

Consecutive snapshots share their text (each step holds one scene string, not two). The recorder
takes a fresh baseline itself after any other command, undo or redo, so snapshot steps and cheap
commands can live in one stack. Screens can move to `SetProperty` / object commands one at a
time and stop paying for a scene serialization.

### Object commands and ids

Commands for scene objects never store a pointer. They store an `ObjectId`
(`std::uint64_t`; for the engine this is `GameObject::InstanceId()`, or any stable id you keep)
and look the object up again on every `Do` and `Undo`. After a scene reload (undo of a snapshot,
Load) the lookup finds the new instance, or fails cleanly if the object is gone, in which case
the stack drops the step.

The scene is reached through `ObjectOps`, a bundle of callbacks the editor fills in:

```cpp
auto ops = std::make_shared<ObjectOps>();
ops->capture   = [&](ObjectId id, std::string& blob) { ... };   // object + subtree as text
ops->restore   = [&](ObjectId id, const std::string& blob, ObjectId parent, int index) { ... };
ops->destroy   = [&](ObjectId id) { ... };
ops->locate    = [&](ObjectId id, ObjectId& parent, int& index) { ... };
ops->reparent  = [&](ObjectId id, ObjectId parent, int index) { ... };
```

The contract that makes undo safe:

- `restore` must give the objects of the subtree back the ids they had, so older history entries
  that mention them still resolve. If your scene can't keep ids across a reload, keep an id to
  object map in the callbacks.
- Every callback resolves ids afresh; none of them may cache a pointer between calls.
- Created objects are recorded after the fact: `undo.Record(CreateObjectCommand::Make(ops, id, "Add Cube"))`.
  Deletes are executed: `undo.Execute(DeleteObjectCommand::Make(ops, id, "Delete Cube"))`.
  `Make` returns null if the object can't be captured; do not record it then.

For property edits use `SetObjectProperty<GameObject, T>(name, resolve, id, "propertyName", setter, before, after)`
where `resolve(id)` returns the object or null.

## Autosave and crash recovery

```cpp
RecoveryManager::Config cfg;
cfg.directory = projectDir + "/.okay/recovery";
cfg.intervalSeconds = 120;
RecoveryManager recovery(cfg);
recovery.SetProvider([&](std::string& text, std::string& label) {
    if (!sceneDirty) return false;               // nothing to save
    text = SceneSerializer::Serialize(scene);
    label = scenePath;
    return true;
});
recovery.StartSession();

// every frame:
recovery.Tick(dt);

// at startup, after StartSession():
for (const auto& c : recovery.FindRecoverable()) { /* show: c.timestamp, c.size, c.label */ }
std::string text;
if (recovery.Restore(candidate, text)) LoadFromText(text);   // keeps the file; Discard(c) removes it
```

### What happens on disk

Everything lives in one directory:

| File | Meaning |
| --- | --- |
| `<base>-<session>.lock` | Written by `StartSession()`, removed by `EndSession()`. Holds the pid. |
| `<base>-<session>-<slot>.autosave` | An autosave. Each session rotates over `slots` files (default 3), so the newest write never replaces the only good copy. |

An `.autosave` file is a short text header (session, sequence number, unix time, label, payload size,
FNV-1a checksum) followed by the payload. Every write goes through `savefmt::AtomicWrite`.

On `StartSession()`:

1. Locks left by other sessions are checked: if their process is gone the session is a crash
   (`UncleanShutdownDetected()`, `CrashedSessions()`) and its lock is cleared. A lock whose process
   is alive is a running peer; its autosaves are never offered or deleted.
2. Half-written `.tmp` files and unreadable autosaves of dead sessions are removed
   (`removeCorruptOnStart`); a file from a newer format is kept.
3. Autosaves of old sessions beyond `maxOtherSessions` are dropped (oldest first).
4. This session's lock is written.

A clean exit (`EndSession()`) removes the lock and this session's autosaves: nothing is left to
recover. `OnSaved()` removes this session's autosaves after the user saves the real file.

`FindRecoverable()` returns the valid autosaves of other, dead sessions, newest first, each with
`timestamp`, `size`, `label`, `sessionId`, `seq` and `unclean`. A file that fails validation (cut off,
flipped byte, wrong size, bad header) is skipped and listed in `SkippedFiles()` with the reason, so
the next older slot is still offered. `Restore` re-validates before handing back the text;
`RestoreToFile(candidate, path)` writes it straight to a file; `Discard` and `DiscardAll` delete.

`Tick(dt)` autosaves when `intervalSeconds` has passed. If the provider returns false (clean
document) or the text is unchanged since the last autosave, nothing is written. A failed write retries
after `retrySeconds`, not a whole interval later.

Caveat: "is the process alive" is the operating system's pid lookup, so a stale lock whose pid was
reused by an unrelated program reads as a live peer, and its autosaves stay hidden until that
program exits. `SetProcessProbe` replaces the check if you need something stricter.

## Backups

```cpp
BackupManager::Config cfg;
cfg.directory = projectDir + "/.okay/backups";
cfg.policy.keepLast = 10;
cfg.policy.keepDailyDays = 14;
BackupManager backups(cfg);

backups.SaveWithBackup(scenePath, text);       // keeps the old file, then writes the new one

for (const auto& b : backups.List(scenePath))  // newest first: b.timestamp, b.size
    ...;
backups.Restore(list[2], scenePath);           // the file it replaces is backed up first
```

Backups are `<file name>.<8 hex of the path hash>.<YYYYMMDD-HHMMSS>[-n].bak` in one directory
(UTC time; `-n` tells apart backups taken in the same second). The hash keeps two files with the
same name in different folders apart.

- **Identical content** is not backed up twice (`skipIdentical`).
- **Prune policy** (`BackupPolicy`, applied after every backup, or by `Prune` / `PruneAll`):
  keep the newest `keepLast` (at least 1), plus the newest backup of each of the last
  `keepDailyDays` days that isn't already covered, then enforce `maxTotalBytes` oldest first (the
  newest always stays). Day boundaries are UTC; set `dayOffsetSeconds` to your UTC offset for
  local midnight.
- **Size limit**: files over `maxFileBytes` (default 256 MiB) are not backed up. `SaveWithBackup`
  still saves unless `requireBackup` is set.
- **Restore** refuses to overwrite the current file if it could not be backed up first
  (`backupBeforeRestore`).

`SelectForRemoval`, `FormatStamp`, `ParseStamp` and `DayIndex` are public static so tools and tests
can use the pure parts.

## Adopting it in the editor

The editor needs no data model change to start:

1. Own one `UndoStack` in `EditorState`. Replace `PushUndo()`/`Undo()`/`Redo()` with a
   `SnapshotRecorder` (`Commit(name)` after an edit instead of `PushUndo()` before it), so the
   behaviour stays the same while the stack, dirty state and caps come from the new code. Call
   `undo.Clear()` and `rec.Reset()` after a scene load.
2. Move hot paths over one at a time: Inspector sliders to `UndoRecordSet`
   (+ `BreakMerge()` on release), create/delete/duplicate/group to the object commands wrapped in
   a group. Each one stops serializing the whole scene.
3. Use `IsDirty()` for the title-bar asterisk and the "save before closing?" prompt instead of a
   separate flag.
4. Replace the `<scene>.autosave` sidecar with a `RecoveryManager`: `StartSession()` at launch,
   `Tick(dt)` each frame, `EndSession()` on a clean exit, `OnSaved()` after Save, and show
   `FindRecoverable()` in the "Recover" popup.
5. Route `Save` through `BackupManager::SaveWithBackup`, and list `List(path)` in a
   "File > Restore from backup" window.

## What the editor does today

The editor has not adopted the three classes above yet. It uses its own simpler pieces, which this page
documents so the two are not confused (the user-facing description is in [editor.md](editor.md#projects-saving-and-loading)):

| Need | What the editor does |
| --- | --- |
| Undo | `EditorState::PushUndo` keeps up to 100 whole-scene text snapshots. Opening a scene (`Load`) clears the history: it belongs to one scene, and keeping it let Undo put the previous scene's objects into the new file. A failed `Load` changes nothing, history included. |
| Dirty state | `EditorState::dirty` (set by edits), plus `HasUnsavedChanges()`: the scene's saved text is fingerprinted at every save, load and new scene and compared at the moments that matter (quit, opening another scene or project, autosave), so an edit that forgot to raise the flag is still found. `SyncDirty()` raises the flag when they differ. While Play runs it answers for the scene being edited, and `Stop` puts `dirty` back, so changes to the running game never count. |
| Saving | `EditorState::Save` refuses during Play, writes through `SceneSerializer::SaveToFile` (a `.savetmp` file, checked, renamed over the target, one rolling `.bak` copy of the previous file) and reports why it failed. Project, build and preference files use `okayproj::WriteTextAtomic` (`shared/ProjectCreate.hpp`): the same write-then-rename, and a read-only target is refused. |
| Autosave | A `<scene>.autosave` copy every `g_autosaveInterval` seconds (default 120) when the scene has unsaved changes and a file path; deleted by a successful save; offered by the "Recover Autosave" window when it is newer than the scene. A never-saved scene has no path and is not autosaved. |
| Recovery of a failed new project | `EditorState::CreateProject` makes folders and files, builds the template, saves it, and on any failure puts the previous scene, path, project and undo history back and removes what it made (`okayproj::Made` / `Undo`). |

Not yet the engine's `RecoveryManager` (rotating autosaves, crash detection by lock file) or `BackupManager`
(timestamped backups): moving the editor onto them is step 4 and 5 of "Adopting it in the editor" above.

## Tests

`test_undo`, `test_recovery` and `test_backup` (suites `undo`, `recovery`, `backup`) use temporary
directories and an injected clock. They cover merging of drag edits, group atomicity, redo
invalidation, count and memory eviction, dirty tracking, simulated crashes (a lock left behind),
torn and flipped-byte autosaves, slot rotation, backup rotation and the daily policy.
