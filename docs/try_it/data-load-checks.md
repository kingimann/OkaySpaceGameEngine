---
id: data-load-checks
---
## Damaged project, texture and material files load safely (branch `claude2/data-load-checks`, not merged or released yet)

Three bug fixes from issue #888 (findings 74, 75 and 78). Nothing new to click; what changes is what you see after loading.

**Project settings with bad numbers (finding 74).**
1. Close the editor. Open your project folder and edit `project.okayproj` in Notepad. Change three lines to
   `gravity3d=0 nan 0`, `ambient=5` and `splash_bg_top=nan 1e39 -3`, and save.
2. Open the project in the editor and open **Edit > Project Settings**.
3. You should see Gravity 3D back at `0, -9.81, 0` (a line with `nan` is ignored, so the default stays), Ambient at
   `1` (5 is pulled down to the top of 0..1) and the splash background top colour at its default. Before this fix the
   window showed `nan`, and Apply to current scene copied `nan` into the scene.
   The rules: sizes 16..16384, gravity -10000..10000 per part, steps per second 1..500, ambient and colours 0..1. Each
   corrected line writes one `WARN` line to the engine log.

**Tagging a texture no longer shrinks it (finding 75).**
1. Put a large picture (for example 4096 x 4096 px) in `Assets` and use it on a material, so you can see it is sharp.
2. In the Project panel, right-click the picture and add a tag.
3. Close and reopen the project (or reimport the picture). It should still be sharp: open its Import Settings and
   **Largest side** says **No limit**. Before this fix the tag wrote `set maxSize 2048` into `big.png.okaymeta` and the
   picture came back at 2048 px. To shrink a picture on purpose, choose a size in **Largest side**; that still works.
   Pictures that already have a meta keep whatever it says (an old meta with `maxSize 2048` still shrinks; pick
   **No limit** to undo it).

**Materials with spaces in the texture path, or a bad number (finding 78).**
1. Double-click a `.okaymat` (or make one in the Material editor). Set its texture to a file whose name has a space,
   for example `Assets/Art/stone wall.png`. Save, close it, and open it again.
2. You should see the full path and the texture on the object. Before this fix it came back as `Assets/Art/stone`
   and the object was untextured. In the file the line is now `texture "Assets/Art/stone wall.png"` (a path without
   spaces is still written bare, so older versions read it as before).
3. Edit the `.okaymat` in Notepad and change the `emissive` line to `emissive inf 0 0`. Open it again: only emissive
   red falls back to its default; tiling, rim, outline and the texture are all still there. Before, every line after
   the `inf` was lost.

**Checked:** `test_data_load_checks` (headless, Linux). **Not checked:** the editor windows above (the editor was
not built for this change); anything on Windows.
