---
id: 88
row: | 88 | Safer Save As, recovery and newer scenes | File > Save Scene As...; File > Open; the restore question after a crash | Tested headless (project_save); editor window not run, Windows not run |
---
## 88. Safer Save As, recovery and newer scenes

**What it is.** Three small safety fixes. Save As into a different project is refused with a plain message instead of leaving the editor half in one project. Restoring a recovery copy never leaves a temp file behind and never touches the real file. A scene made by a newer version says so when you open it, and the original is kept as `<scene>.v<N>.bak` before this version saves over it.

**Steps.**
1. Open a project, then File > Save Scene As... and pick a folder inside a different project (one that has its own `Assets` folder). You see: "That folder belongs to another project ... Save the scene inside this project ..., or open the other project first." Nothing is written.
2. Save As into a folder that is not in any project: it saves, and the project stays as it was.
3. Recovery: edit a scene, wait for the autosave, end the editor from Task Manager, start it again and press Restore. The scene opens marked unsaved; the real scene file has not changed, and no `.recovery-tmp` file is next to it.
4. Newer scene: open a scene file whose first line says `okayscene 99`. The Console shows a yellow note that it was made by a newer version. Save it: a copy `<name>.okayscene.v99.bak` appears beside it.

**What you should see.** The messages above, in plain words, and no half-finished files.

**Status.** The save, recovery, newer-scene and moved-project behaviour is covered by `test_project_save`. The editor windows and Windows were not run: unverified.
