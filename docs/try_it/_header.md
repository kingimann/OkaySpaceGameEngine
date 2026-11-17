# Try it: everything you can test in the Windows release

Owner rule (2026-11-15): "everything that's being made I should be able to test; if I can't test it out it's
useless." This page lists each thing that is shipped (or finished and waiting for the next release), how to reach it
in the release you run (`OkaySpace.exe`, `Tools\OkayEngine.exe`, `Tools\OkaySpacePlayer.exe`, the installer or the
zip), and how to tell it worked. CLAUDE.md rule 4a keeps it current: every change adds its row here.

**File > Examples is gone (owner request, 2026-11-17).** Where an entry below still says "File > Examples", open the same scene with
**File > Open Scene...** instead: the example scenes still ship in the `examples` folder beside the program (for example
`examples\input\...`, `examples\ui_library\...`). The older wording will be corrected as entries are touched.

Honest status words used below:

* **Verified here** means a command was run on the Linux machine that wrote this page, and the result was seen.
* **Tested on main** means a headless test exists and passed in CI when its change merged; it was not re-run for this page.
* **Unverified** means it was read from the code only. Nothing in these sessions runs on Windows or opens a real editor
  window, so every "you should see" line about the editor is unverified until you look. Tell us when one is wrong.

## Where the files are

Since this change the zip and the installer carry an `examples` folder next to `OkaySpace.exe`:

| You used | The examples are in |
|---|---|
| The installer (`OkaySpace-Setup.exe`) | `%LOCALAPPDATA%\OkaySpace\examples\` (paste that into the Explorer address bar) |
| The zip | the unzipped `OkaySpace-<version>\examples\` folder |

The editor is `Tools\OkayEngine.exe` in the same folder (or start it from the launcher). In the editor, **File > Open...**
asks for a scene path: type or paste it, or press **Browse...**. A scene finds the files it uses (sounds, scripts,
sequences) next to itself, so the folder works from any install path.

What is in `examples\` (about 0.5 MB, all small): `vehicle`, `own_wav`, `own_readers`, `obj_texture_paths`,
`spatial_audio`, `cutscene`, `behavior_trees`, `node_playground`, `physics`, `physics2d`, `weapons`, `touch_controls`,
`ui_text`.
Left out on purpose: `account-server` (a Python server), `netdemo` and `physicsdemo` (C++ sources), `editor-plugins` and
`visual_script` (not scenes). The packaging is checked by reading and by running the copy step of

What is in `examples\` (about 1 MB, all small): `vehicle`, `own_wav`, `own_readers`, `obj_texture_paths`,
`spatial_audio`, `cutscene`, `behavior_trees`, `node_playground`, `physics`, `physics2d`, `weapons`, `touch_controls`, and
`visual_script\playground` (the 40 scenes behind File > Examples > Visual Script Node Playground; earlier releases left this
folder out, so that menu said "scenes not found"). Left out on purpose: `account-server` (a Python server), `netdemo` and
`physicsdemo` (C++ sources), `editor-plugins`, and the loose graphs in `visual_script` itself (not scenes). The packaging is
checked by reading and by running the copy step of
`scripts/build-windows-dist.sh` on Linux (see "How the packaging was checked" at the end); nobody has run the whole
release job or the installer build for this change.
