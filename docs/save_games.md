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
slot name uses letters, digits, `-` and `_`: `1`, `quick`, `autosave`,
`chapter_2`.

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
its label, scene name, the time it was saved and the play time. Use it to fill
a load screen.

## Notes

- A save is written to a temporary file and then swapped in, so a crash
  mid-save never leaves a broken slot.
- A file that isn't a save game is refused rather than half loaded.
- Saves made with the older `save_game` (`saves/<slot>.okaysave`, scene only)
  don't load with the new format. Save again to make a new one.
