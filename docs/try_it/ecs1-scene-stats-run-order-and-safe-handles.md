---
id: ecs1
row: | ecs1 | Scene Stats window, Run order setting, safe object handles | Editor: View > Scene Stats window; Edit > Project Settings > Run order; handles are for C++ scripts | Linux build and tests only; editor window compiled, not opened |
---
## ecs1. Scene Stats, Run order and safe handles

**What it is.** Three small things for objects and components. (1) A **Scene Stats** window that counts objects and
components, lists components by type and can time each type. (2) A **Run order** setting that makes a component type run
early or late each frame, with no code. (3) `ObjectRef` / `ComponentRef<T>` for C++ scripts: handles that turn into
null when the object is destroyed. Also: removing many components at once is much faster (the benchmark below).
The guide is `docs/ecs_guide.md`; the measurements are in `docs/ecs_upgrade.md`.

**Steps: Scene Stats.**
1. Start the editor (`OkaySpace.exe`, then open the editor, or `Tools\OkayEngine.exe`).
2. **File > Examples > perf > many_colliders** (or open `examples\perf\many_colliders.okayscene` from the install folder).
3. Open **View > Scene Stats window** (the menu item sits next to "Profiler window").
4. You should see: Objects, Components, "Running every frame", and a table "Components by type" with a count per type.
5. Tick **Measure time per type** and press **Play**. Two more columns appear: "Update ms" and "Share". Untick it and they go.

**Steps: Run order.**
1. **Edit > Project Settings**, open the **Run order** section.
2. Pick a component type in "Add a component type..." (for example `Lifetime`) and press **Run late**. It appears in the
   list above with a "Run late" box; change it to "Normal" to undo.
3. Close the editor and look at `project.okayproj` in the project folder: there is a line `run_order_Lifetime=late`.
   Reopen the project: the setting is still there.
4. What it does: every frame that type's Update / LateUpdate / FixedUpdate runs after all the others (early: before all
   the others). You can see the order only with two scripts that print; the unit test `ecs_refs` does exactly that.

**Handles (C++ scripts).** `ObjectRef target = enemy; ... if (GameObject* e = target.Get()) { ... }`. After the enemy is
destroyed `Get()` returns null instead of a dangling pointer. The `ecs_fuzz` test runs thousands of random
create / destroy / reparent / add / remove steps and checks that no handle ever points at a wrong or gone object.

**What you should see.** The numbers in the window match what you know of the scene; the setting survives a restart.

**Status.** Engine part built and tested on Linux (`ecs_refs`, `ecs_fuzz`, `ecs_bench`; results in `docs/ecs_upgrade.md`).
**Unverified:** Windows was not run; the editor window and the Project Settings section were only compiled for syntax,
not opened, so the layout has not been seen.
