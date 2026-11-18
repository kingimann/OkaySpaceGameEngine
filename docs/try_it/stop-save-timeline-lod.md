---
id: stop-save-timeline-lod
---
## Stop prompt saves, Timeline keys pass each other, LOD distances on big objects (bug fixes, issue #888 findings 85, 76, 77)

Branch `claude2/editor-stop-save-timeline-lod`, not merged or released yet.

**Save in the Stop prompt (finding 85).**
1. Open any scene in the editor and change something (select a cube, press **Ctrl+D**). The title says "unsaved".
2. Press **Play**. In "Save your work before playing?" click **Don't Save**.
3. Press **Stop**. "Save your work before stopping?" lists "Scene". Click **Save**.
4. Expected: Play stops, the title no longer says "unsaved", the Console says `Project saved: scene, settings`, and the
   `.okayscene` file has the copied cube (open it again from the Project panel to check). The next Play does not ask.
   Anything you changed *while playing* is still thrown away, as before.
   Before the fix the Console said "The scene wasn't saved while playing" and the scene stayed unsaved.
   The same now happens for "Save your work before opening another scene?" while playing.

**Timeline keys (finding 76).**
1. **Window > Timeline**, open or make a sequence, add a Transform track and keys at 0 s, 1 s and 2 s (double-click the row).
2. Drag the 0 s key to 1.5 s, past the 1 s key.
3. Expected: three keys (1 s, 1.5 s, 2 s); the 1 s key keeps its pose. Before the fix the 1 s key disappeared.
   A key cannot land exactly on another key: it stops one frame before and moves on once you drag past. Typing another
   key's time in the **Time** field is refused the same way (the field keeps the old time). Letterbox, Shake and Fade
   keys behave the same.

**LOD distances (finding 77).**
1. Add a big object (a cube scaled to 20) and give it a **LOD Group** (distance mode, the default) with distances 5, 15, 40.
   Turn on its debug tint so each level has a colour.
2. Move the Scene camera away from it. Expected: level 1 from about 5.5 m and level 2 from about 16.7 m from the object's
   centre (the extra 11 % is the 0.1 hysteresis), whatever the object's size. Before the fix a 20-unit object switched at
   about 19 m and 39 m, and a 100-unit one at about 96 m and 193 m.

**Checked (Linux, headless):** `test_stop_save_timeline_lod` 45 checks, 0 failures (21 of 41 failed before the fix);
`test_editor_hooks`, `test_editor_undo`, `test_play*`, `test_player*`, `test_sequence`, `test_visibility_lod`,
`test_optimize_simplify` and `test_optimize_occlusion` all pass. `editor/main.cpp` and `editor/TimelineWindow.cpp` were
syntax-checked only (not built or run): the three walkthroughs above have not been tried in the GUI or on Windows.
