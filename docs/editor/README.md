# editor/: The editor

> **What this is:** the list of pages in `docs/editor/`, one line each. The editor: panels, menus, undo, projects, level tools, modeling.
>
> **Status:** index, made 2026-11-20 when the docs were sorted into folders. Back to the [start page](../README.md).
>
> **Last checked:** 2026-11-20.

- [Build Settings upgrade](build_settings_upgrade.md): Owner request: "Building settings needs improvements.
- [Editor plugins](editor-plugins.md): You can extend the OkaySpace editor itself with **plugins** written in OkayScript (the same language as game scripts).
- [OkaySpace Editor](editor.md): A desktop editor built with **Dear ImGui (docking) + SDL2**.
- [Editor menus: audit and map](editor_menus.md): The editor's main menu bar and the Hierarchy panel's right-click menus, cleaned up on 2026-11-15 at the owner's request ("clean the menu bar better, and improve the context menus, add more").
- [Editor speed: seeing it, measuring it, tuning it](editor_performance.md): The editor shows how long each frame takes and where the time goes, and has a few switches to make it cheaper.
- [Editor undo and redo](editor_undo.md): How the editor's Ctrl+Z works, which edits it covers, and which edits have an undo of their own or none.
- [Guided modeling](guided_modeling.md): Guided procedural modeling: the design, detail levels, the speed work and the generator quality audit.
- [Level tools: foliage, terrain layers, decals, cables, measuring and orthographic views](level_tools.md): Small tools for dressing a 3D level. Everything here is saved in the scene and has an undo step.
- [Modeling](modeling.md): The Modeling window (built-in mesh editor) and its audit.
- [Project panel](project_panel.md): The Project panel is the editor's asset browser: a folder tree on the left, the folder's contents on the right (tiles or a list), a path bar with Back and Forward, search, and a strip at the bottom for the selected asset.
- [Project versions and upgrading a project](project_upgrade.md): A project remembers which version of OkaySpace last saved it.
- [Scene view: right-click to add](scene_right_click_add.md): Owner request (2026-11-18): "I want to be able to right click inside of the scene and add stuff."
- [Undo, autosave, crash recovery and backups](undo_recovery.md): Three small, headless building blocks in `okay/Core`, none of which knows about scenes or UI, so the editor, other tools and tests can all use them:
