# Editor plugins

You can extend the OkaySpace editor itself with **plugins** written in
OkayScript (the same language as game scripts). A plugin can add a panel with
buttons and sliders, a one-click command in the **Plugins** menu, or react to
the selection, and it works on the open scene through the `editor_*` API.

## Where plugins live

| Folder | Applies to |
| --- | --- |
| `<project>/EditorPlugins/*.okay` | that project only (commit it with the project) |
| `<editor folder>/plugins/*.okay` | every project you open |

**Plugins > New Plugin...** creates a starter plugin in the project and opens it
in the Script Editor. Saving the file reloads the plugin immediately.
**Plugins > Plugin Manager...** lists every plugin with its errors, lets you turn
one off, and opens its folder.

## Anatomy

```c
// @name Align Tools
// @description Line up the selected objects.
// @window

gap = 2                       // top-level code runs once when the plugin loads

on_gui() {                    // draws the plugin's panel every frame it's open
    ui_text(editor_selection_count() + " selected")
    gap = ui_slider("Gap", gap, 0.1, 10)
    if (ui_button("Space evenly")) distribute()
}

distribute() { /* ... */ }
```

Header comments (at the very top):

- `// @name` — the name in the Plugins menu and the panel title.
- `// @description` — the tooltip and the Plugin Manager subtitle.
- `// @window` — give the plugin a panel (also implied by defining `on_gui()`).

## Hooks (all optional)

| Function | Called |
| --- | --- |
| `start()` | once after the plugin loads (and after each reload) |
| `on_gui()` | every frame while its panel is open |
| `on_menu()` | when its Plugins-menu entry is clicked (plugins without a panel) |
| `on_selection()` | when the selection changes |
| `on_tick()` | every editor frame (keep it cheap) |

## Panel widgets

The standard `ui_*` builtins draw inside the plugin's panel:
`ui_text(s)`, `ui_button(label)`, `ui_checkbox(label, v)`, `ui_switch(label, v)`,
`ui_slider(label, v, lo, hi)`, `ui_progress(t)`, `ui_sameline()`, `ui_separator()`.

Editor-only extras: `editor_input(label, text)` returns the edited text,
`editor_heading(text)` draws a section title, `editor_hint(text)` draws muted
wrapped text.

## The editor API

Objects are addressed by **name**; pass `""` for the current selection.

| Call | Does |
| --- | --- |
| `editor_selected()` | name of the selected object (`""` if none) |
| `editor_selection_count()`, `editor_selection_name(i)` | the full multi-selection |
| `editor_select(name)` | select an object |
| `editor_object_count()`, `editor_object_name(i)` | every object in the scene |
| `editor_exists(name)` | 1 if an object has that name |
| `editor_create(kind, name)` | create `"empty"`, `"cube"`, `"sphere"`, `"plane"`, `"cylinder"`, `"capsule"`, `"cone"`, `"pyramid"`, `"camera"`, `"sprite"` or `"light"`; returns its name |
| `editor_delete(name)` | delete an object |
| `editor_rename(name, newName)` | rename |
| `editor_set_active(name, on)` | enable / disable |
| `editor_set_parent(name, parent)` | reparent (`""` = scene root), keeping the world position |
| `editor_get_pos/rot/scale(name, "x"/"y"/"z")` | read a local position / euler rotation / scale axis |
| `editor_set_pos/rot/scale(name, x, y, z)` | set it |
| `editor_frame(name)` | point the Scene camera at an object |
| `editor_undo_point()` | record an undo step (call once before a batch of changes) |
| `editor_log(msg)`, `editor_warn(msg)`, `editor_error(msg)` | write to the Console |
| `editor_scene_name()`, `editor_project_dir()`, `editor_is_playing()` | context |

Every other OkayScript builtin (math, strings, `random_range`, JSON, files...)
works too.

## Examples

`examples/editor-plugins/` has two to copy into your project's `EditorPlugins/`:

- **AlignTools.okay** — a panel that aligns, spaces out and drops the selection.
- **CleanupNames.okay** — a one-click menu command that strips `" (3)"` suffixes.
