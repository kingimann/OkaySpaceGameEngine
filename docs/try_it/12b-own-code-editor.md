---
id: 12b
group: switches
row: | 12b | Our own code editor in the Script Editor (off by default) | `OKAY_CODE_EDITOR=own`, then open any script | New; tested headless only, never run in a real window or on Windows (see 12b) |
---
### 12b. Our own code editor in the Script Editor (`OKAY_CODE_EDITOR=own`)

The Script Editor draws its code with the vendored ImGuiColorTextEdit. `OKAY_CODE_EDITOR=own` swaps in our own
`OkayUI::CodeEdit` for the code area only; the rest of the editor stays Dear ImGui. `OKAY_CODE_EDITOR=imgui` (or leaving it
unset) keeps the old one.

1. Open a Command Prompt, run `set OKAY_CODE_EDITOR=own`, then start `Tools\OkayEngine.exe` from that window.
2. Open any project from a template (for example New Project > First Person) and double-click a script in the Project panel,
   or use **Add Component > Script** on any object and open it.
3. What to expect: the same colours as before (keywords orange, types teal, built-ins purple, strings green, comments grey),
   line numbers, the caret and selection; typing, Enter with auto-indent, Tab and Shift+Tab, Ctrl+Z / Ctrl+Y, Ctrl+C / X / V,
   Ctrl+A, the Insert key (a block caret: typing overwrites), Shift+Delete (cut), Ctrl+Insert / Shift+Insert (copy / paste),
   the mouse wheel and the scroll bars, double click for a word, a click on the line numbers for a line. Break the script
   (type `labl` where a name is expected): the line turns red and the message (for example `unknown name 'labl'`) is written
   after the end of the line in red, as in the old editor; hover it for `Error at line N: ...`. A warning is amber. Hover a
   built-in such as `print` to see its signature. Ctrl+wheel zoom also zooms the code.
   It should look like the old editor: the same code font (JetBrains Mono), the same line height and letter spacing, the
   current line boxed, a bookmarked line (the old editor's breakpoint) filled blue. Type `pri` so the autocomplete list opens
   over the code: the list must be drawn ON TOP of the code (before this change it was hidden under it).
4. How to tell it is on: `okay_log.txt` (next to the editor) has the line `Script editor code widget: own (OkayUI::CodeEdit, OKAY_CODE_EDITOR=own)`
   the first time a script is opened. Without the variable the line says `imgui (default)`.

5. Keyboard and mouse, as with the old editor (step 30): click in the code and press the arrow keys, Space and Enter many times:
   only the code moves, no button of the Script Editor gets pressed or highlighted. Ctrl+S saves the script, Ctrl+W closes its
   tab, Ctrl+P starts Play, all while typing; Ctrl+Z undoes in the code, never in the scene. Open the find box or Search
   Everywhere (double Shift) and type: the letters go there, not into the code; close it and type again: they go into the code.
   With auto-pair on, type `(` then `)`: one `)`, the caret steps over it; Enter between `{` and `}` opens one indented line.
   On a keyboard with AltGr (German, French...), AltGr types `{ [ @ \`. Alt+Backspace undoes. Double-click a word and drag:
   whole words are selected. The pointer is an I-beam over the code. Hover `print` or the broken line: the tooltip looks like
   the old one (title, line, message) and sits above the autocomplete list. Zoom far in (Ctrl+wheel, up to 300 %) at a large
   UI scale: the code stays sharp (Ctrl+wheel only zooms, it does not also scroll).
6. Fonts' licences (no switch needed): the zip and the installed folder have `THIRD_PARTY_NOTICES.txt` and a `licenses` folder
   with `JetBrainsMono-OFL.txt` (the code font built into the programs) at the top and in `Tools\`.

Known differences, said plainly: ours also boxes the matching bracket next to the caret (the old one does not); glyph edges
are a little sharper (our own TrueType rasteriser, no 2x oversampling); Tab on a selection indents with four spaces (the old
one put a tab character), Shift+Tab with nothing selected removes one level, Home goes to the first non-blank first, Ctrl+Backspace
deletes a word, Shift+click extends the selection (the old one did none of these). **Unverified**: this path has never been run in a real window or on Windows; only headless tests
and headless screenshots ran (`codeedit_bench shots`).
If the Windows build has no OkayUI compiled in, the log line says so and the old editor is used.
