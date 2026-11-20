# Hand-written verification rows: build, export and updates

### V-BX-001 Build a game
- **Map:** 18
- **Where:** editor, Build Settings
- **Steps:**
  1. Open a scene and choose **File > Build Settings...** (Ctrl+B), or press the Build button on the toolbar.
  2. Set the game name and the **Output folder**, then press **Build**.
  3. Open the output folder and start the `.exe`.
- **See:** a standalone game folder with an exe; the exe opens your scene and plays it.
- **Partial:** the player (`Tools\OkaySpacePlayer.exe`) must be next to the editor; the build never downloads one.
- **Link:** [build_mode.md](../../gameplay/build_mode.md)

### V-BX-002 Build & Run and zip
- **Map:** 18
- **Where:** editor, Build Settings
- **Steps:**
  1. In **Build Settings** tick **Also create a .zip next to the folder** and **Open the output folder**.
  2. Press **Build & Run**.
- **See:** the game starts right after the build, a `.zip` appears next to the folder, and the folder opens in Explorer.
- **Partial:** Build & Run is for the desktop target only.

### V-BX-003 Strip unused assets and encrypt data
- **Map:** 18
- **Where:** editor, Build Settings
- **Steps:**
  1. Tick **Strip unused assets**, **Clean output folder** and **Encrypt game data**, then build.
  2. Open the shipped scene file in a text editor.
- **See:** only the used files are shipped; with Encrypt on, the scene is not readable text.
- **Partial:** encryption here is obfuscation, not secret keeping (the tooltip says so).

### V-BX-004 Build from the command line
- **Map:** 18
- **Where:** Command Prompt
- **Steps:**
  1. In a Command Prompt run `Tools\OkayEngine.exe --build-game MyScene.okayscene OutDir --name MyGame` with your own scene path.
  2. Check the exit code (`echo %errorlevel%`).
- **See:** exit code 0 and a built game in `OutDir`.
- **Partial:** the exit code is 0 only when the game was built; nothing is downloaded. A GUI exe may not print to the console.
- **Link:** [../../editor.md](../../editor/editor.md)
- **Grounded:** no

### V-BX-005 Editor update check
- **Map:** 18.5, 17.1.2
- **Where:** editor, Engine menu
- **Steps:**
  1. Choose **Engine > Check for Updates...**.
  2. Read the popup and the Console line ("Up to date" or "Update available").
- **See:** a message with the current and the latest version.
- **Partial:** the editor's own installer uses the same checksum rules as the launcher (docs/release/updater.md); not run on Windows by us.
- **Link:** [updater.md](../../release/updater.md)

### V-BX-006 Installer and zip layout
- **Map:** 18.1
- **Where:** Windows
- **Steps:**
  1. Run `OkaySpace-Setup.exe` (or unzip the zip).
  2. Look for `OkaySpace.exe` and the `Tools` folder (`OkayEngine.exe`, `OkaySpacePlayer.exe`).
  3. Start `OkaySpace.exe`.
- **See:** the launcher starts; the Tools folder holds the editor and player.
- **Partial:** the installer path is `%LOCALAPPDATA%\OkaySpace`; code signing is not done yet (the owner's step, CLAUDE.md rule 14), so Windows SmartScreen may warn.
- **Link:** [packaging.md](../../release/packaging.md)
- **Grounded:** no

### V-BX-007 Release files have checksums (owner check)
- **Map:** 18.5
- **Where:** a browser
- **Steps:**
  1. Open the published `versions.json` for the release and look for SHA-256 entries for `OkaySpace.exe`, `OkayEngine.exe` and `OkaySpacePlayer.exe`.
- **See:** a 64-hex checksum for each of the three files.
- **Partial:** a release without checksums must not become "latest" (CLAUDE.md rule 13).
- **Grounded:** no
