# Hand-written verification rows: own-code switches (all off by default)

How to set a variable (Windows Command Prompt): `cd %LOCALAPPDATA%\OkaySpace`, `set OKAY_OWN_STACK=1`, `Tools\OkayEngine.exe`. The variable lives only in that window.

### V-OC-001 Startup stack line (default)
- **Map:** 40, 37
- **Where:** editor, Help menu
- **Steps:**
  1. Start `Tools\OkayEngine.exe` with no variables.
  2. Open **Help > About OkaySpace** and read the stack line.
- **See:** `[okay] stack: platform=sdl2 http=winhttp readers=ufbx+assimp crypto=libsodium ui=imgui` (the readers part may differ). `crypto=` must say libsodium in every release.
- **Partial:** a GUI program may not print to the console; the About popup is the sure way.
- **Link:** [../../try_it.md](../../try_it.md)

### V-OC-002 OKAY_OWN_STACK turns on the own readers
- **Map:** 3, 40
- **Where:** Command Prompt, then editor
- **Steps:**
  1. Run `set OKAY_OWN_STACK=1` then `Tools\OkayEngine.exe`.
  2. Open **Help > About OkaySpace**.
- **See:** `readers=own(fbx,dae,3ds,...)` in the stack line instead of `ufbx+assimp`.
- **Partial:** `OKAY_OWN_STACK=1` does not select the own platform layer on Windows (on purpose) and does not include `OKAY_OWN_WAV`.
- **Link:** [../../own_stack_rollout.md](../../own_stack_rollout.md)

### V-OC-003 One own reader: OKAY_OWN_OFF
- **Map:** 3
- **Where:** Command Prompt, then editor
- **Needs:** a release that ships the `examples` folder (PR #887, not merged when this was written)
- **Steps:**
  1. Run `set OKAY_OWN_OFF=1` then start `Tools\OkayEngine.exe`; read **Help > About OkaySpace** (`readers=own(off)+assimp`).
  2. Choose **File > Import Model...** and pick `examples/own_readers/cube.off`.
  3. Close the editor, start it again with no variable and import the same file.
- **See:** a white unit cube both times; only the About line shows which reader ran.
- **Partial:** no Console line says which reader read the file. A variable set to `0` wins over `OKAY_OWN_STACK=1`.
- **Link:** [../../own_off_reader.md](../../own_off_reader.md)

### V-OC-004 Other own readers
- **Map:** 3
- **Where:** Command Prompt, then editor
- **Needs:** a release that ships `examples/own_readers/README.txt` (branch `claude2/own-reader-samples`, not merged when this was written)
- **Steps:**
  1. Pick a switch from `examples/own_readers/README.txt` (one per reader, for example `OKAY_OWN_PMX`), run `set OKAY_OWN_PMX=1`, then start `Tools\OkayEngine.exe`.
  2. Choose **File > Import Model...** and pick that switch's file (`examples/own_readers/rig.pmx`). For `cube.irr`, `cube.lws` and `rig.md5mesh` untick **Copy the file into the project's Assets** first.
  3. Press **Import**.
- **See:** the Import Model window lists what the README says (meshes, skeleton, clips) and the shape appears.
- **Partial:** the startup line names only 20 of the switches; for the newer readers (AC3D to LWS) nothing shows which reader ran, and Assimp in the release reads most of these formats too. STL and PLY have no switch yet. NDO has no sample (its reader refuses every file).
- **Link:** [../../try_it.md](../../try_it.md)

### V-OC-005 Own WAV decoder
- **Map:** 7
- **Where:** Command Prompt, then editor
- **Needs:** a release that ships the `examples` folder (PR #887, not merged when this was written)
- **Steps:**
  1. Start the editor with no variable, open `examples/own_wav/own_wav_test.okayscene` and press Play.
  2. Close it, run `set OKAY_OWN_WAV=1`, start the editor, open the same scene and press Play.
- **See:** the old decoder gives a distorted, buzzy note; the own decoder gives a clean steady tone (about 440 Hz, A). Neither was heard by us.
- **Partial:** not part of `OKAY_OWN_STACK=1`; no startup-line entry; streamed large files still use the old parser. The release may not ship the example (PR #887).
- **Link:** [../../own_wav_decoder.md](../../own_wav_decoder.md)

### V-OC-006 Own HTTP client
- **Map:** 17.2, 41
- **Where:** none on Windows
- **Steps:**
  1. Nothing to try: Windows uses WinHTTP, `OKAY_OWN_HTTP=1` does nothing there. Check that the stack line says `http=winhttp`.
- **See:** `http=winhttp` in the About popup.
- **Partial:** the own client exists for Linux builds only and is tested on Linux.
- **Link:** [../../own_http_client.md](../../own_http_client.md)

### V-OC-007 Own platform layer
- **Map:** 37
- **Where:** Command Prompt, then the player
- **Steps:**
  1. Run `set OKAY_PLATFORM_BACKEND=own` then `Tools\OkaySpacePlayer.exe` on an exported game.
  2. Read the stack line (`platform=own` if it took effect).
- **See:** `platform=own` if the build has it, otherwise `platform=sdl2`. Unverified, not ready; never ship with it.
- **Partial:** the Win32 version has only run under wine, never on real Windows.
- **Link:** [../../own_stack_rollout.md](../../own_stack_rollout.md)

### V-OC-008 OkayUI backend switch
- **Map:** 40
- **Where:** Command Prompt, then editor
- **Steps:**
  1. Run `set OKAY_UI_BACKEND=okayui` then `Tools\OkayEngine.exe`.
  2. Open **Help > About OkaySpace** (`ui=okayui` if it took effect) and open the Console panel.
- **See:** the Console, Crash Log and Audio Mixer screens are drawn by OkayUI; everything else is still Dear ImGui.
- **Partial:** only five screens are ported, never run in a real window; whether the release has OkayUI compiled in is unverified (the line stays `ui=imgui` if not).
- **Link:** [../../own_ui_toolkit.md](../../own_ui_toolkit.md)

### V-OC-009 Own crypto is not switchable
- **Map:** 39
- **Where:** editor, Help menu
- **Steps:**
  1. Set `OKAY_OWN_CRYPTO=1` and start the editor.
  2. Read the stack line.
- **See:** `crypto=libsodium` still. Own crypto cannot be switched on (CLAUDE.md rule 18).
- **Partial:** nothing to try beyond this; it must stay libsodium in every release.
- **Link:** [../../crypto.md](../../crypto.md)

### V-OC-010 Own script editor (OkayUI CodeEdit)
- **Map:** 40, 13
- **Where:** `Tools\OkayEngine.exe`, Script Editor window
- **Steps:**
  1. Run `set OKAY_UI_BACKEND=okayui` then `Tools\OkayEngine.exe` (or, with `OKAY_DEV=1` set, open **View > Rendering & Performance > UI backend** and pick OkayUI (preview)). The script view follows the UI backend.
  2. Put a script in a project and double-click it in the **Project** panel: it opens in the **Script Editor** tab.
  3. Type, press Enter after a `{`, Tab and Shift+Tab, Ctrl+Z and Ctrl+Y, and click the narrow column left of the line numbers.
  4. To go back run with `OKAY_UI_BACKEND=imgui`.
- **See:** colours for keywords, strings, numbers and comments; Enter keeps the indent; the click toggles a blue breakpoint mark; a script error shows a red mark. The Console and Help > About OkaySpace line "Script editor: our own code editor (OkayUI backend, preview)" needs the next release (it is on branch claude/own-code-editor, PR pending).
- **Partial:** never run in a real window; one caret only; no folding, no word wrap. Console and About lines also draw the Console, Crash Log and Audio Mixer with OkayUI.
- **Link:** [own_code_editor.md](https://github.com/kingimann/OkaySpaceGameEngine/blob/claude/own-code-editor/docs/own_code_editor.md)
- **Grounded:** no
