---
id: saves1
row: | saves1 | Editor saving and loading: recovery after a crash, outside-change warning, Save All, Scene File menu, safe mode | The editor (Windows release): File menu; the `okay_recovery` folder next to the editor | New: unit-tested logic and editor wiring; nothing run in a real editor window on Windows |
---
## saves1. Editor saving and loading

**What it is.** The editor now never loses your work on a bad save or a crash, and says what is going on in plain words.
Saves are whole-or-nothing (a failed or killed save leaves your old file untouched; a scene keeps one `.bak` copy of the
previous save). A recovery copy of unsaved work is written to the `okay_recovery` folder next to the editor while you
work. If the editor is killed, the next start asks to restore it. The editor warns before it overwrites a scene file that
something else changed. Details and the limits are in `docs/editor_saving.md`.

**Steps (open scenes with File > Open Scene; File > Examples is not used here).**

1. *Recovery after a crash.* Open the editor, then File > Open Scene to open any `.okayscene`. Add an object twice
   (GameObject > Create Empty), so the title shows `*`. Open File > Autosave and set Interval to 5 seconds. Wait about 10
   seconds without dragging anything. Open Task Manager and End Task on the editor. Start the editor again from the launcher.
   You should see the box "The editor closed unexpectedly. Restore your last autosave?" with the time ("Autosaved a minute
   ago (14:03)"), the scene name and its object count next to the count in the saved file. **Restore** brings the unsaved
   edits back (the title shows `*`; the file on disk is not touched until you save). **Open Both** keeps your saved file
   and opens the recovered scene as `<name> (recovered).okayscene` beside it. **Discard** throws the copy away.
2. *Outside change.* Open a scene, open the same `.okayscene` in Notepad, change the line `name "..."` and save it there.
   Switch back to the editor and wait a second: a box "This file changed outside the editor" appears (Reload / Keep Mine).
   Choose **Keep Mine**, make an edit in the editor and press Ctrl+S: the box appears again with **Save a Copy / Overwrite /
   Reload / Cancel**. Nothing is overwritten until you choose Overwrite. **Reload** brings in the Notepad version, and
   Edit > Undo brings your edits back.
3. *Save All and the unsaved list.* Change the scene. Open a script from the Project panel, type in it and do not save. Press
   File > Exit. The box says "2 things have unsaved changes:" and lists the scene and the script. **Save All** writes both,
   **Discard** closes without saving, **Cancel** stays open. In the editor, File > Save All (Ctrl+Alt+S) saves the scene,
   scripts, graphs and settings, and the Console says "Saved 3 things: ...". The status bar shows "Saved 14:03" after a save.
4. *Scene File menu.* File > Scene File: **Save a Copy...** (a new file, this scene stays open), **Duplicate Scene** (writes
   `<name> copy.okayscene` and opens it), **Revert to Saved** (greyed out until there are unsaved changes; Edit > Undo
   brings them back), **Show in Folder**, **Save Scene as Template...** and **New from Template** (templates live in
   the `okay_scene_templates` folder next to the editor).
5. *Open Recent.* File > Open Recent: right-click a scene for **Pin to top**, **Unpin** or **Remove from list**; **Remove
   missing files** cleans the list.
6. *Autosave settings.* File > Autosave: Enabled, Interval, Keep copies, and what the last copy cost in milliseconds.
7. *A damaged scene.* Close the editor, open a `.okayscene` in Notepad, delete the last 20 lines, save, open it again with
   File > Open Scene. A box "Couldn't open the scene" says what is wrong and where (line and object). **Open readable
   objects (safe mode)** opens what can be read as a new unsaved scene and the Console lists what was left out.
   **Open last good save** appears when a `.bak` exists.

**What you should see.** The boxes above, in those words. After step 1 your file on disk is unchanged until you save.
Autosave writes nothing while the game plays.

**Status.** Tested on Linux (headless): atomic writes with simulated disk-full, kill and rename failures for scenes,
prefabs, materials, meshes and the other asset saves; backup rotation; recovery write, restore, discard and cleanup;
the unsaved-mark state machine; outside-change detection; the unsaved list; autosave rules; load-error words; safe-mode
salvage; Open Recent; copy names (`test_editor_saves`, `test_menu_bar_model`). The editor screens (steps 1 to 7) were
written and compiled but not run in a real editor window, and nothing was run on Windows: unverified there. A progress bar
with Cancel for very large scenes is not done.
