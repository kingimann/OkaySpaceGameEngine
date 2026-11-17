---
id: 0b
group: main
row: | 0b | Our own replacements are ON by default: platform, renderers, WAV, fonts, code editor, OkayUI screens | Just start `OkaySpace.exe` or `Tools\OkayEngine.exe`; `set OKAY_THIRD_PARTY=1` for the old code | New in this change; run on Linux under Xvfb only. **Never run on Windows: expect bugs there** |
---
## 0b. Testing our own replacements

Owner's decision, 2026-11-17: "start switching them so i can test them out". From this release the launcher, the editor and
the player start on **our own code** wherever we have a replacement, and each one keeps a way back to the old third-party code.
Nothing to set: just start the programs as usual.

**What now runs on our own code by default**

| Part | Was | Now | The way back for that part alone |
|---|---|---|---|
| Window, input, sound device, clipboard (launcher, editor, player) | SDL2 | our own platform layer (Win32 on Windows) | `set OKAY_PLATFORM_BACKEND=sdl2` (and for the editor also `set OKAY_EDITOR_PLATFORM=sdl`) |
| Drawing the launcher | SDL_Renderer | our own OpenGL painter | `set OKAY_LAUNCHER_RENDERER=sdl` |
| Drawing the editor | SDL_Renderer (Direct3D 11) | our own OpenGL painter | `set OKAY_EDITOR_RENDERER=sdl` |
| Reading `.wav` sounds | SDL_LoadWAV / old reader | our own WAV reader (already the default since 2.880) | `set OKAY_OWN_WAV=0` |
| Text (font rasteriser) in the launcher and editor | Dear ImGui's | our own TrueType rasteriser | `set OKAY_OWN_FONT_RASTER=0` |
| The script editor's code box | ImGuiColorTextEdit | our own code editor (OkayUI CodeEdit) | `set OKAY_CODE_EDITOR=imgui` |
| Ported screens (launcher: What's New, Tips, Downloads, Achievements, Notifications; editor: Console, Crash Log, Audio Mixer, Problems, Stats, History, Tags and Layers, Physics, Profiler, Import settings, Post-processing, Player and Rendering settings) | Dear ImGui | OkayUI | `set OKAY_UI_BACKEND=imgui` |
| Cryptography | libsodium | **still libsodium** (owner rule 18: no switch) | nothing to do |

**All of it back at once:** `set OKAY_THIRD_PARTY=1`. A part's own variable above still wins over it, so you can also go back
for everything except one part, or the other way round.

**Automatic fallback.** If our own window, OpenGL context or renderer cannot start on your PC, the program writes why and starts
on SDL by itself (the old path). You then see the line `our own ... could not start; starting on SDL instead` in the launcher's
`okay_launcher.log` or the editor's Console.

**Steps (Windows, from the release):**

1. Start `OkaySpace.exe` as usual (double-click). Expect the launcher as before. Open the file `okay_launcher.log` next to
   `OkaySpace.exe` (in `%LOCALAPPDATA%\OkaySpace` for the installer). Its newest line should read
   `launcher: own: platform, renderer, wav, fonts, okayui screens; third party: libsodium (set OKAY_THIRD_PARTY=1 for all old paths)`.
   (`wav` is listed when the WAV reader is ours; it always is unless you turned it off.)
2. Click around: Home, What's New, Tips, Downloads, Achievements, the bell. Type in a search box. Resize the window. Close it with
   the X. Everything should work as in the last release; the text should be sharp; the ported pages should look like before.
3. Open the editor (Open on a project, or `Tools\OkayEngine.exe`). Open **View > Console**. One of the first lines reads
   `editor: own: platform, renderer, wav, fonts, code editor, okayui screens; third party: libsodium (...)`.
4. Open any script (double-click a `.okayscript` or a C# file): the code box is our own editor. Type, select with the mouse,
   undo (Ctrl+Z), find (Ctrl+F), hover a built-in name for its help. Press Play on a scene with sound: you hear it.
5. Export or open a game and run `Tools\OkaySpacePlayer.exe <scene>` from a Command Prompt: the line
   `player: own: platform, wav; third party: libsodium (...)` appears in that window.
6. **The old code, to compare.** Close everything. Open a Command Prompt:

   ```
   cd %LOCALAPPDATA%\OkaySpace
   set OKAY_THIRD_PARTY=1
   OkaySpace.exe
   ```

   The launcher (and the editor you open from it) now run on SDL, Dear ImGui and the old readers. The log line reads
   `launcher: own: nothing; third party: platform, renderer, wav, fonts, okayui screens, libsodium (OKAY_THIRD_PARTY=1 is set: old paths)`.
   (PowerShell: `$env:OKAY_THIRD_PARTY = "1"`.) The variable lives only in that window.
7. **One part only.** In a new Command Prompt try one switch, for example `set OKAY_EDITOR_RENDERER=sdl` then
   `Tools\OkayEngine.exe`: the Console line moves `renderer` to the third-party side and nothing else changes.

**If something is wrong on our own code:** note what you did, set `OKAY_THIRD_PARTY=1`, try the same thing again. If it works
there, it is our code: tell us the step and send `okay_launcher.log` (or the editor's Console text).

**What was checked, honestly.** Built and run on Linux only (Release, under Xvfb with Mesa's software OpenGL): the launcher, the
editor and the player started with no variables set and with `OKAY_THIRD_PARTY=1`, each showed its line, took keyboard and mouse
input and closed cleanly; the automatic fallback was forced with the test variable `OKAY_TEST_OWN_FAIL` (not for users) and each
program started on SDL. The Windows code (our Win32 window, OpenGL on Windows, the installer) was only compiled with MinGW, **never
run on Windows**: this is the first release where you can see it, and it may have bugs there. Older sections below (11, 11b to
11i, 12, 12b, 18) still say "off by default": that was before this change; their steps still work, without setting the variables.
