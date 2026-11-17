---
id: 11f
group: appendix
row: | 11f | Editor with no SDL at all (off by default) | `OKAY_EDITOR_PLATFORM=own`, `OKAY_EDITOR_RENDERER=own` and `OKAY_PLATFORM_BACKEND=own`, then `Tools\OkayEngine.exe` (section "Editor with no SDL at all") | Log and Console say `editor: no SDL ...`; drag a file onto the window to import it; Linux/Xvfb only (the whole editor, with an SDL trap), **unverified on Windows** |
---
## Editor with no SDL at all (branch `claude2/sdl-editor-no-sdl`, not merged or released yet)

What changed (SDL removal, editor stage E3): with the three switches below the editor's main window, its keyboard, mouse, cursor, file
drops, window title, game display settings, clipboard and message box all come from **our own platform code**, and the editor makes no
call into SDL at all (it does not even start SDL). It is **off by default**: with no variable nothing changes. With only the first two
switches the window and events go through okay::platform's SDL backend (SDL still runs underneath).

* **Switches.** `OKAY_EDITOR_PLATFORM=own`, `OKAY_EDITOR_RENDERER=own` and `OKAY_PLATFORM_BACKEND=own`, all three.
* **How you can tell it is on.** The log (and the Console's first lines) say `editor: no SDL (own platform, own renderer, own platform
  backend)` and `editor: platform own (...; main window and events on own)`.

Steps (Windows, from the release, once this branch is in one):

1. Open a Command Prompt and run: `cd %LOCALAPPDATA%\OkaySpace`, `set OKAY_EDITOR_PLATFORM=own`, `set OKAY_EDITOR_RENDERER=own`,
   `set OKAY_PLATFORM_BACKEND=own`, then `Tools\OkayEngine.exe`.
2. Expect the loading screen, then the editor. Open a project (for example the physics example). Check the Console lines above.
3. Click around: a Hierarchy row, the Hierarchy search box (type a few letters), a Scene view object. Switch the view to 3D and fly with
   the right mouse button held and W / A / S / D.
4. Drag a picture or a model from Explorer onto the editor window: it is imported into the Project panel's folder.
5. Press Ctrl+P (Play). In the Game view tick "Capture mouse", move the mouse (the pointer is hidden and held), press Esc (it comes
   back in the middle of the Game view), then Ctrl+P again (Stop).
6. In Play, copy and paste in a game text field (if the scene has one), and open the game's display settings (if it has them):
   windowed, borderless and fullscreen switch the window (fullscreen is the desktop size here: no resolution change yet).
7. Close the editor with the X: it closes normally.

Known differences with the switches on (on purpose, for now): no multi-window panels (they work since stage E4, section 11j); the game's resolution list shows only the
desktop size and "fullscreen" does not change the monitor's resolution; no touch-screen input.

**Status.** Run on Linux only, under Xvfb with software OpenGL, as the whole editor, with a trap that stops the program on any SDL call
(`scripts/editor_no_sdl_check.sh`): start, clicks, typing, a right-mouse fly, Play, mouse capture, Esc, Stop and closing the window
made no SDL call, and the screens match the default editor's except for frame-rate numbers and the text caret. File drops, the
refresh rate and the message box of our Linux code are tested on their own (`platform_editor_calls_own`). **Never run on Windows:
steps 2 to 7 are unverified there until you run them.**
