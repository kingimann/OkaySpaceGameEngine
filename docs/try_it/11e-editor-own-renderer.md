---
id: 11e
group: appendix
row: | 11e | Editor drawn by our own renderer (off by default) | `OKAY_EDITOR_PLATFORM=own` and `OKAY_EDITOR_RENDERER=own`, then `Tools\OkayEngine.exe` (section "Editor: drawing with our own renderer") | Console's first lines say `editor: renderer own (opengl painter ...)`; no multi-window panels; **untested in a real editor run and on Windows** |
---
## Editor: drawing with our own renderer (branch `claude2/sdl-editor-gfx`, not merged or released yet)

What changed (SDL removal, editor stage E2): with the switches below, the editor's whole window (every panel, the Scene and Game views,
thumbnails, icons, previews, the loading screen) is drawn by **our own OpenGL painter and our own Dear ImGui renderer** (the same code
the launcher uses with `OKAY_LAUNCHER_RENDERER=own`) instead of SDL's renderer. It is **off by default**: with no variable nothing changes.

* **Switch.** `OKAY_EDITOR_RENDERER=own`, and it **only works together with `OKAY_EDITOR_PLATFORM=own`** (section "Editor: input and the
  3D views' OpenGL through our own code"). With `OKAY_EDITOR_RENDERER=own` alone the editor draws as before and the Console says
  `... (OKAY_EDITOR_RENDERER=own could not start: it needs OKAY_EDITOR_PLATFORM=own as well)`.
* **How you can tell it is on.** The Console's first lines say `editor: renderer own (opengl painter on a sdl2 window, own ImGui renderer)`.
  Without it they say `editor: renderer sdl (SDL_Renderer direct3d11, ...)`.

Steps (Windows, from the release, once this branch is in one):

1. Open a Command Prompt and run: `cd %LOCALAPPDATA%\OkaySpace`, `set OKAY_EDITOR_PLATFORM=own`, `set OKAY_EDITOR_RENDERER=own`, then
   `Tools\OkayEngine.exe`.
2. Expect the loading screen, then the editor as usual. Open the Console: the first lines name the renderer (above).
3. Look at every kind of picture: the Scene and Game views (move the camera, resize the panels), the Project panel's thumbnails, a
   material or model preview, the Modeling panel's primitive tiles, the Sprite Editor (paint a few pixels: they must stay sharp), a UI
   Image in the Inspector ("Set Native Size" shows the picture's size), a text in a TTF font in the Game view, the first-person hands
   preview. Each must look as it does without the variables.
4. View > VSync on and off; take a screenshot with the diagnostics shot if you use it.
5. Press Play in a 3D template; if it shows OkayUI widgets (or Tools > Test UI), they must draw on top.
6. Close the editor with the X: it closes normally.

Known differences with the switch on (on purpose, for now): panels cannot be dragged out into their own windows (as with
`OKAY_EDITOR_PLATFORM=own`); on Windows the Direct3D 11 views do not draw straight into the window's textures, they are copied (read
back and uploaded) like the OpenGL ones, which may be slower on big views.

**Status.** Tested on Linux only, under Xvfb with software OpenGL, **without the editor itself** (the full editor is too big to build on
the test machine): `editor_gfx` and `editor_gfx_own` draw the same editor-like frame (shapes, clipping, text, a thumbnail, a 3D view
rendered in the views' own OpenGL context, a texture with blending off, a texture made again) through SDL's renderer and through ours and
find them identical pixel for pixel. `editor/main.cpp` passes a compile check (Linux and MinGW) with the change. **Never run in a real
editor session and never on Windows: steps 2 to 6 are unverified until you run them.**
