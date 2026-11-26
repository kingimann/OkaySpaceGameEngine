# Editor plugins

> **What this is:** You can extend the OkaySpace editor itself with **plugins** written in OkayScript (the same language as game scripts).
>
> **Status:** Reference page for this feature. Kept as written; not extended (rule 5).
>
> **Last checked:** 2026-11-20 (moved into `editor/`, paths and links checked; the text itself was not re-checked against the code). 2026-11-26: the Script Editor API section and TodoTools added (owner asked for plugins of the script editor).

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

The Plugins menu lists this project's plugins first, then plugins that apply to
every project, each group alphabetical. Two plugins with the same name show the
file name as well. A plugin that failed to load is marked "(error)"; click it
to open the script, and hover for the error. **Turned off** lists plugins you
disabled and turns one back on. A plugin with both a panel and `on_menu()` gets
a submenu (Panel / Run) instead of only toggling the panel. Panel shows a check while the window is open.

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
| `on_menu()` | when its Plugins-menu entry is clicked. If the plugin also has a panel, the entry is a submenu and **Run** calls this |
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

## Script Editor API

A plugin can also add to the Script Editor (2026-11-26). Call these from `start()` (or the top level); they are
removed when the plugin is turned off or reloaded. Code: `editor/PluginScriptApi.cpp` (the calls),
`editor/PluginRegistry.hpp` (what they fill; the Browser can make its own registry later).

| Call | Does |
| --- | --- |
| `script_command(id, title, shortcut, fn)` | a command in the command palette (Ctrl+Shift+P) and Search Everywhere; `shortcut` like `"Ctrl+Alt+T"` ("" for none) runs it while the Script Editor has the focus; `fn` is the function to call (default: `id`) |
| `script_lint(fn, language)` | `fn(path, text, language)` checks the open script; inside it, `script_problem(line, col, severity, message)` reports one ("error", "warning" or "info") and `script_fix(title, line, col, length, text)` gives the last problem a quick fix (Alt+Enter, the light bulb). `language` "" means every language |
| `script_completion(fn, language)` | `fn(prefix, language)` calls `script_suggest(label, detail)` for words the autocomplete list should offer |
| `script_formatter(fn, language)` | `fn(text, language)` returns the formatted text (or "" for no change); Format Document uses it first |
| `script_hover(fn, language)` | `fn(word, language)` returns text shown when the pointer rests on a word |
| `script_snippet(prefix, body, language, title)` | a snippet; the palette lists it as "Insert Snippet: title" |
| `script_panel(title, fn)` | a section in the Script Editor's side pane; `fn()` draws it with the `ui_*` widgets |
| `script_text()`, `script_path()`, `script_language()`, `script_caret_line()` | the open script (empty outside the Script Editor) |
| `script_insert(text)`, `script_set_text(text)`, `script_open(path, line)` | change the open script (one undo step) or open another |

## Examples

`examples/editor-plugins/` has three to copy into your project's `EditorPlugins/`:

- **AlignTools.okay** — a panel that aligns, spaces out and drops the selection.
- **CleanupNames.okay** — a one-click menu command that strips `" (3)"` suffixes.
- **TodoTools.okay** — a Script Editor command (Insert TODO Note, Ctrl+Alt+T) and a check that lists every TODO
  and FIXME in Problems, with a quick fix that turns FIXME into TODO.
