---
id: 11d
group: appendix
row: | 11d | Editor input and 3D-view OpenGL through our own code (off by default) | `OKAY_EDITOR_PLATFORM=own` (optionally also `OKAY_PLATFORM_BACKEND=own`), then `Tools\OkayEngine.exe` (section "Editor: input and the 3D views' OpenGL through our own code") | Console's first lines say `editor: platform own (...)`; the editor works as usual except that panels cannot be dragged out into their own windows; untested in a real editor run and on Windows |
---
## Editor: input and the 3D views' OpenGL through our own code (branch `claude2/sdl-editor-seam`, not merged or released yet)

What changed (SDL removal, editor stage E1): with the switch below, the editor's Dear ImGui gets its mouse, keyboard, typed text,
clipboard and cursor shapes from **our own code** (the same code the launcher uses with `OKAY_LAUNCHER_RENDERER=own`), and the hidden
OpenGL context the Scene and Game views render into is made through our platform layer. SDL still draws the editor's window and
still reads the events from Windows; only what happens with them changed. It is **off by default**: with no variable nothing changes.

* **Switch.** `OKAY_EDITOR_PLATFORM=own` (anything else, or no variable, is the old SDL path). Adding `OKAY_PLATFORM_BACKEND=own` also
  makes the 3D views' OpenGL context with our own Windows code (WGL) instead of SDL's.
* **How you can tell it is on.** The Console's first lines say `editor: platform own (ImGui glue on okay::platform; viewport GL on sdl2; ...)`
  (`... on own` with both variables). Without the variable it says `editor: platform sdl (...)`.

Steps (Windows, from the release):

1. Open a Command Prompt and run:
   `cd %LOCALAPPDATA%\OkaySpace`, `set OKAY_EDITOR_PLATFORM=own`, then `Tools\OkayEngine.exe`.
2. Expect the editor as usual. Open the Console: the first lines name the platform (above).
3. Mouse: click menus, drag a panel's tab to dock it somewhere else, drag a splitter, scroll the Hierarchy with the wheel and with a
   touchpad (small, smooth steps should scroll a little, not jump). The cursor should change to arrows over splitters and to a text
   bar over text fields.
4. Keyboard: rename an object in the Inspector (type letters with accents too, for example "é"), use Backspace, the arrows, Home / End,
   Ctrl+A, Ctrl+C, Ctrl+V; paste text copied from Notepad, and copy text out to Notepad.
5. 3D views: the Scene and Game views draw as usual (View > GPU renderer: the OpenGL one). Repeat with `set OKAY_PLATFORM_BACKEND=own`
   added before starting: the Console then says `viewport GL on own`, and the views must still draw.
6. Play: press Play in a 3D template, walk with WASD, look with the mouse, press Esc, type into a game text field if the template has one.
7. Drop a picture from Explorer onto the editor window: it is imported into the Assets folder.
8. Close the editor with the X: it closes normally.

Known differences with the switch on (on purpose, for now): panels cannot be dragged out of the editor into their own windows (View >
multi-window does nothing); ImGui's own Ctrl shortcuts in text fields follow the key's place on the keyboard, so on an AZERTY keyboard
Ctrl+A is the key where Q is; a slider or splitter drag stops at the window's edge. If anything else behaves differently only with the
switch on, that is our code: report the step.

**Status.** Tested on Linux only, under Xvfb, **without the editor itself** (the full editor is too big to build on the test machine):
`editor_platform` and `editor_platform_own` check the window, the 3D views' OpenGL context (3.2 compatibility, depth, and SDL's own
context given back so the UI still draws), mouse, wheel, keys, typed text, clipboard, cursor shape and a dropped file through the
switch, and that our ImGui input code gives ImGui exactly what ImGui's SDL code gives it for the same events (27 frames, all
identical). `editor/main.cpp` passes a compile check with the change. **Never run in a real editor session and never on Windows:
steps 2 to 8 are unverified until you run them.**
