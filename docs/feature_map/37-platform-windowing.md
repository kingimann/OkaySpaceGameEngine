# 37. Platform & windowing

How the engine meets the operating system. `engine/include/okay/Platform/*` is NOT a windowing layer: it holds the online-service
clients (accounts, friends, chat, Steam, PlayFab) and two OS helpers (`NativeHttp`, `GameIpc`). The window, DPI, focus, file dialog
and web shims live in the host programs (`player/main.cpp`, `editor/main.cpp`, `launcher/main.cpp`, SDL2) plus `Core/DisplaySettings`,
`Core/Application`, `Input/Cursor` and `Graphics/TtfFont`. No engine window, DPI or thread API exists. Games copy and paste text through `Core/Clipboard` (SDL in the player and editor Play mode, a memory clipboard in tests). 37.9 maps the
online clients, which area 17 also describes. Nothing here is in an open PR.

## 37. Platform & windowing

### 37.1 Window, fullscreen and display settings
- **What:** The game player creates its SDL window (size, resizable, borderless, fullscreen, start maximized, minimum size, icon, hidden until the splash is ready), is per-monitor DPI aware on Windows, and applies the Settings screen's monitor, display mode, resolution, refresh rate, vsync and quality. A shared SDL helper lists monitors and modes for the player and the editor.
- **Parts:**
  - 37.1.1 Window creation - `player/main.cpp` - flags, centered position, minimum size, window icon PNG, hidden-then-reveal to avoid a black flash, optional boot splash
  - 37.1.2 High DPI - `player/main.cpp`, `editor/main.cpp` - `SDL_HINT_WINDOWS_DPI_AWARENESS` per-monitor, `SDL_WINDOW_ALLOW_HIGHDPI`; the editor scales its Game view by `DisplayFramebufferScale`
  - 37.1.3 Fullscreen, vsync and quality settings - `Core/DisplaySettings` - Prefs-backed window mode, resolution, refresh, monitor, `VSync`, `Quality`; hosts re-apply when `Generation()` moves; the player also toggles on Alt+Enter and F11 when Build Settings allows
  - 37.1.4 Build Settings window options - `editor/main.cpp` - run in background, mute when unfocused, fullscreen toggle, start maximized, minimum size, company and save-to-user-folder, read by the player from its config
  - 37.1.5 Console host loop - `Core/Application` - the terminal game loop (`Application::Run`) with console renderer, frame pacing and quit-on-Q; used by tests and headless demos
  - 37.1.6 GPU preference - `player/main.cpp` - on Windows exports NVIDIA Optimus and AMD flags so hybrid laptops use the discrete GPU
  - 37.1.7 Monitors, modes and the confirm clock - `Core/DisplayModes`, `shared/SdlDisplays.cpp` - list, filter and fall back; Apply then "Keep these settings?" for 10 seconds, restored on the next launch if the question was never answered
- **Code:** `player/main.cpp`, `editor/main.cpp`, `launcher/main.cpp`, `Core/DisplaySettings`, `Core/DisplayModes`, `shared/SdlDisplays.hpp`, `shared/SdlDisplays.cpp`, `Core/Application`
- **API:** `DisplaySettings::SetMode`, `SetMonitorName`, `SetResolution`, `SetRefreshHz`, `BeginTrial`, `ResolveSaved`, `sdldisplay::ListMonitors`, `sdldisplay::Apply`, script `set_display_mode`, `set_monitor`, `begin_display_confirm`
- **Editor:** Build Settings (window and presentation options); the Settings screen in play uses the same SDL helper on the editor window
- **Docs:** `docs/display_settings.md`, `docs/packaging.md`, `docs/web_mobile.md`
- **Tests:** `display_settings`, `display_screen`
- **Status:** Works for the choice itself (filtering, fallback, the 10-second revert, the Settings screen and the script nodes are tested). The player and the editor apply it through SDL. Each host still creates its own window. The own-code platform backend is not used.
- **Open:** no single window class shared by the player, editor and launcher (each still creates its SDL window); no Linux or macOS DPI hint (the hint is Windows only)
- **Related:** 7.4, 8.4, 12.8, 12.9, 15.7, 18.1, 2.1
- **Words:** window, SDL window, fullscreen, borderless, windowed, resolution, refresh rate, monitor, vsync, DPI, high DPI, HiDPI, scaling, Windows 11 125%, Alt+Enter, F11, start maximized, minimum size, window icon, DisplaySettings, SdlDisplays, keep these settings, Application
- **Seen as:** the game looks blurry on a scaled display; the window flashes black at start; fullscreen does not toggle; the game window has the wrong icon or title; the Settings screen quality button changes nothing; the window cannot be resized
- **Ask me like:** "make the game window resizable with a minimum size"; "add a fullscreen toggle on F11 to my build"

### 37.2 Focus, cursor and clipboard
- **What:** How the game behaves when the window loses focus (keep running or pause, mute audio, release a locked cursor), the OS text-input event for typing, and the clipboard games and scripts use to copy and paste text.
- **Parts:**
  - 37.2.1 Focus handling - `player/main.cpp` - `runInBackground`, `muteOnFocusLoss` (sets `AudioMixer::focusMuted`), focus loss releases a locked cursor until the next click
  - 37.2.2 Cursor lock and visibility - `Input/Cursor` - gameplay sets the wish, the player applies it with SDL relative mouse mode (see 8.4)
  - 37.2.3 Text input - `player/main.cpp`, `Input/Input` - `SDL_StartTextInput` and `Input::FeedText` carry real typed characters (see 8.1)
  - 37.2.4 Clipboard - `Core/Clipboard`, `shared/SdlClipboard.hpp`, `player/main.cpp`, `editor/main.cpp` - `GetClipboardText` / `SetClipboardText`; the player and editor Play mode install an SDL clipboard (SDL directly, not `okay::platform`); tests use the memory clipboard; a focused input field copies, cuts and pastes with Ctrl or Command; `get_clipboard_text` / `set_clipboard_text` and the visual-script nodes Get Clipboard Text and Set Clipboard Text. The editor's own copy buttons still use Dear ImGui, and the node editors keep an internal clipboard for nodes
- **Code:** `Core/Clipboard`, `shared/SdlClipboard.hpp`, `player/main.cpp`, `editor/main.cpp`, `Components/UIInputField`, `Input/Cursor`
- **API:** `GetClipboardText`, `SetClipboardText`, `get_clipboard_text`, `set_clipboard_text`, `Cursor::Capture`, `AudioMixer::focusMuted`, `Input::TypedText`, Build Settings "run in background" and "mute when unfocused"
- **Editor:** Build Settings; Play mode uses the same clipboard as a built game; Console panel Copy buttons
- **Docs:** `docs/game_ui.md` (text fields), `docs/scripting_reference.md`, `docs/audio.md` (mute when unfocused), `docs/input.md`
- **Tests:** `tests/test_clipboard.cpp` (sanitize, field copy/cut/paste, script and visual-script functions)
- **Status:** Works - a focused game text field and scripts can copy and paste text; the player and editor Play mode use SDL; tests never write the operating-system clipboard
- **Open:** the input field has no caret selection, so there is no select-all shortcut; node editor copy and paste stays inside each window; no IME composition handling beyond SDL text events
- **Related:** 7.4, 8.1, 8.4, 12.3
- **Words:** clipboard, copy, paste, Ctrl+V, focus, alt-tab, background, run in background, mute when unfocused, IME, text input, cursor release, SetClipboardText
- **Seen as:** the sound keeps playing when the window loses focus; the mouse stays captured after alt-tab; the game pauses (or does not) when minimized
- **Ask me like:** "let players paste into the chat box"; "pause the game when the window loses focus"

### 37.3 File dialogs
- **What:** Open, save and choose-folder dialogs in the editor (animation clips, dialogue, quests, sequences, behavior trees, fonts, CSV, assets, scenes, build settings) and the launcher (chat attachments, project folders, marketplace files), written for OkaySpace in `shared/FileDialog.*` (no third-party library). The system's own dialog is used when there is one; otherwise a built-in ImGui browser opens, so picking a file never needs an outside tool. The shipped game has no file dialog.
- **Parts:**
  - 37.3.1 Dialog API - `shared/FileDialog.hpp`, `shared/FileDialog.cpp` - `okay::filedialog::Open`, `Save`, `Folder` (and the `*One` forms) with a callback, filters, multi-select, extension enforcing on save; path, glob and result-parsing helpers
  - 37.3.2 System backends - `shared/FileDialog.cpp` - Windows: IFileOpenDialog / IFileSaveDialog (Unicode paths); Linux: zenity, then kdialog (argument vector, no shell); macOS: osascript; `OKAY_FILE_DIALOG=builtin` forces the built-in browser
  - 37.3.3 Built-in browser - `shared/FileDialogUI.cpp`, `BrowserModel` in `shared/FileDialog.cpp` - modal ImGui window (places, folder list, name field, filter box, replace prompt); shows in the UI context that asked (the launcher's chat has its own window)
  - 37.3.4 Callers - `editor/main.cpp`, `editor/TimelineWindow.*`, `editor/DialogueWindow.*`, `editor/BehaviorTreeWindow.*`, `editor/LocalizationWindow.*`, `editor/AnimatorWindow.*`, `launcher/main.cpp`
- **Code:** `shared/FileDialog.hpp`, `shared/FileDialog.cpp`, `shared/FileDialogUI.cpp`
- **API:** `okay::filedialog::Open`, `OpenOne`, `Save`, `SaveOne`, `Folder`, `FolderOne`, `DrawBuiltin` (editor and launcher only; no engine wrapper)
- **Editor:** each window's New, Open, Save As and Pick buttons; File > Open Project Folder, Save Scene As, Import
- **Docs:** `docs/editor.md`
- **Tests:** `tests/test_file_dialog.cpp` (helpers, argument lists, browser model, queue, fake helper programs)
- **Status:** Works in the editor and launcher (built-in browser and zenity/kdialog argument handling tested on Linux); not available to games. NOT YET TESTED ON WINDOWS OR MACOS: the Windows IFileOpenDialog / IFileSaveDialog path is compile-checked only (MinGW cross-compile), and the macOS osascript path was never run. Test both on real machines
- **Open:** no remembered last folder; no dialog from scripts or the player (a game cannot ask the user for a file)
- **Related:** 15.4, 15.5, 10.1, 10.2, 9.4, 31
- **Words:** file dialog, open file, save as, FileDialog, file browser, zenity, file picker, browse, choose file, native dialog
- **Seen as:** the Open button does nothing; the dialog opens in the wrong folder; the file filter hides my files
- **Ask me like:** "remember the last folder in file dialogs"; "let a script open a file picker"

### 37.4 System fonts
- **What:** When the chosen UI font lacks the characters a text needs (accents, Cyrillic, Japanese, Korean, Chinese), the engine finds installed OS fonts by well-known paths and draws the text with one of them.
- **Parts:**
  - 37.4.1 Installed font list - `Graphics/TtfFont` - `SystemFallbackFonts` checks Windows, macOS and Linux candidate paths and keeps the ones that exist
  - 37.4.2 Font choice for a text - `Graphics/TtfFont` - `FontForText` tries the language font, the built-in UI font, then the installed fonts and logs the switch once
  - 37.4.3 Editor font preview - `editor/LocalizationWindow.*` - lists the installed fallback fonts for a language
- **Code:** `Graphics/TtfFont`, `editor/LocalizationWindow.*`
- **API:** `SystemFallbackFonts`, `FontForText`, `Localization::LanguageFont`
- **Editor:** Localization window font picker
- **Docs:** `docs/localization.md`
- **Tests:** `localization`, `ttf`
- **Status:** Works - the candidate list and `FontForText` fallback chain read in TtfFont.cpp; `FontForText` is exercised in `localization`; the path list itself is not tested
- **Open:** fixed path lists only, no OS font enumeration API, so a font in a non-standard folder is not found; the fonts are not bundled, so a Linux machine without them shows missing glyphs; owned by 2.9 and 1.8 (listed here for the OS side)
- **Related:** 2.9, 1.8, 31.2
- **Words:** system font, fallback font, glyph fallback, CJK, missing characters, tofu boxes, segoeui, Noto Sans, DejaVu, SystemFallbackFonts
- **Seen as:** boxes or question marks instead of Japanese, Chinese or Korean text; the font looks different on another machine
- **Ask me like:** "bundle a CJK font so every platform looks the same"; "text shows boxes on Linux"

### 37.5 Web and mobile shims
- **What:** The same player compiles for the browser through Emscripten (frame callback loop, SDL2 port) and is documented for Android and iOS with the standard SDL2 templates; platform differences are `#ifdef` blocks in the player.
- **Parts:**
  - 37.5.1 Web build - `scripts/build-web.sh`, `player/main.cpp` - `emscripten_set_main_loop`, no hidden window, no UDP networking in the browser, scene preloaded into the virtual file system
  - 37.5.2 Mobile setup - `docs/web_mobile.md` - SDL2 Android and iOS project steps; no project files in the repo
  - 37.5.3 Touch feed - `player/main.cpp`, `editor/main.cpp` - SDL finger events into `Input::FeedTouch` (see 8.4)
- **Code:** `scripts/build-web.sh`, `player/main.cpp`, `docs/web_mobile.md`
- **API:** `scripts/build-web.sh <scene>`; `__EMSCRIPTEN__` blocks in the player
- **Editor:** Build Settings export targets
- **Docs:** `docs/web_mobile.md`
- **Tests:** -
- **Status:** Partial - web build script and the `__EMSCRIPTEN__` main loop exist (read); Android and iOS are instructions only; the WebGL CI job is covered in area 18.3
- **Open:** Android and iOS projects are not in the repo; web has no networking. The player feeds SDL finger events on desktop and web, and gestures and on-screen controls are described in `docs/input.md`
- **Related:** 18.2, 18.3, 8.4, 12.9
- **Words:** WebGL, Emscripten, WebAssembly, wasm, browser build, Android, iOS, mobile build, APK, SDL2 port, touch, build-web.sh
- **Seen as:** the web build shows a blank page or freezes the tab; multiplayer does not connect in the browser; touch controls do not work on a phone; the mobile build has no project to open
- **Ask me like:** "export my game to the browser"; "set up an Android project for the player"

### 37.6 Native HTTP and the game-to-launcher link
- **What:** Two OS helpers: an in-process HTTPS client on Windows (WinHTTP with kept-alive connections, falling back to curl elsewhere), and a file-based link between a running game and the launcher (presence, invites, join requests, recently played with).
- **Parts:**
  - 37.6.1 Native HTTP - `Platform/NativeHttp` - `okay::http::Request` returns `reached` false off Windows so callers use the curl path; thread safe
  - 37.6.2 Game and launcher link - `Platform/GameIpc` - small files in the per-user config folder keyed by process id: presence, invite request, join link, `played-with.tsv`
- **Code:** `Platform/NativeHttp`, `Platform/GameIpc`
- **API:** `okay::http::Request`, `okay::http::NativeAvailable`, `GameIpc::WritePresence`, `GameIpc::TakeJoin`, `GameIpc::RequestInvite`, `GameIpc::RecordPlayedWith`
- **Editor:** none
- **Docs:** `docs/launcher-multiplayer.md`, `docs/accounts.md`
- **Tests:** `launcher_join`, `match_results`
- **Status:** Works - both headers read; the player calls GameIpc (presence, join, invite) and the launcher and AccountService use NativeHttp (call sites read); `launcher_join` covers the link
- **Open:** NativeHttp is Windows only (other systems use the curl process); file-based IPC polls with a 30 s presence age rather than a socket; no tests for NativeHttp itself
- **Related:** 17.1, 17.3, 17.2, 16.4, 18.1
- **Words:** NativeHttp, WinHTTP, HTTPS client, curl, GameIpc, launcher link, presence, join link, invite friends, played with, ipc folder
- **Seen as:** launcher is slow to fill in at start; invites do not reach the running game; friends do not see what I am playing; account calls fail behind a proxy
- **Ask me like:** "why does the launcher take a second to load the friend list?"; "add a Linux in-process HTTP client"

### 37.7 Steam backend
- **What:** One Steam interface with two backends: an in-memory simulation (default, no SDK needed) and the real Steamworks backend when built with `OKAY_WITH_STEAM`; covers achievements, stats, leaderboards, cloud files, Workshop, friends, lobbies and overlay.
- **Parts:**
  - 37.7.1 Interface - `Platform/Steam/SteamService` - `ISteamService`, `SteamConfig`, leaderboard, Workshop and lobby structs with harmless defaults
  - 37.7.2 Accessor - `Platform/Steam/Steam` - `Steam::Get()` creates and initializes the best available service
  - 37.7.3 Simulation backend - `engine/src/Platform/Steam/NullSteamService.cpp` - in-memory achievements, stats, cloud and lobbies
  - 37.7.4 Real backend - `engine/src/Platform/Steam/SteamworksService.cpp` - compiled only with the SDK; CMake option in `engine/CMakeLists.txt`
  - 37.7.5 Scene component - `Components/SteamManager` - a component wrapper for games
- **Code:** `Platform/Steam/Steam`, `Platform/Steam/SteamService`, `engine/src/Platform/Steam/NullSteamService.cpp`, `engine/src/Platform/Steam/SteamworksService.cpp`, `engine/src/Platform/Steam/Steam.cpp`, `Components/SteamManager`
- **API:** `ISteamService`, `Steam::Get`, `SteamConfig`, `UnlockAchievement`, `SetStat`, `UploadLeaderboardScore`, `CloudWrite`, `WorkshopPublish`, `CreateLobby`, script `steam_*` builtins
- **Editor:** the Services window, Steam section (`DrawServices` in editor/main.cpp)
- **Docs:** `docs/steam_release.md`
- **Tests:** `steam`, `steam_script`
- **Status:** Works for the simulation backend (headers and Steam.cpp read; covered by `steam` and `steam_script`); the real Steamworks backend is compiled only with the SDK and is untested in CI
- **Open:** the real backend cannot be tested without the SDK and a running Steam client; no Steam Input or rich presence beyond the interface; same ground as area 17.10 (this page covers the OS side)
- **Related:** 17.10, 17.7, 18.1
- **Words:** Steam, Steamworks, ISteamService, achievements, leaderboard, cloud saves, Steam Workshop, lobby, overlay, OKAY_WITH_STEAM, SteamManager, App ID 480
- **Seen as:** achievements never unlock on Steam; the Steam overlay does not appear; cloud saves do not sync; the game runs but Steam says "not running"
- **Ask me like:** "unlock a Steam achievement when the boss dies"; "ship my game on Steam"

### 37.8 Per-user folders
- **What:** Where per-user data goes: saves in the user's app folder (via SDL), launcher data under the OS config folder, and achievements in a folder shared by games and the launcher.
- **Parts:**
  - 37.8.1 Save folder - `player/main.cpp` - `SDL_GetPrefPath(company, app)` when "save to user folder" is on
  - 37.8.2 Launcher config folder - `Platform/Account/AccountService` - `%APPDATA%/OkaySpace` on Windows, `$XDG_CONFIG_HOME` or `~/.config/OkaySpace` elsewhere; the IPC folder in 37.6 lives under it
  - 37.8.3 Achievements folder - `Platform/Achievements` - `%LOCALAPPDATA%\OkaySpace` on Windows, `~/.okayspace` elsewhere
- **Code:** `player/main.cpp`, `Platform/Account/AccountService`, `Platform/Achievements`
- **API:** Build Settings "save to user folder", `company`; `okay::account::DefaultConfigDir`
- **Editor:** Build Settings
- **Docs:** `docs/save_games.md`, `docs/packaging.md`
- **Tests:** -
- **Status:** Basic - the three folder rules read in code; three separate implementations, no shared path helper
- **Open:** achievements and launcher data use different base folders (LOCALAPPDATA versus APPDATA); a shared `Core/Paths` helper (per-platform data, config, cache and log folders, 1.18.2) is on main since #571 but nothing outside `engine/src/Core/Paths.cpp` calls it yet (grep), so those folders were not unified; the helper does compute the macOS Application Support folder
- **Related:** 1.7, 35, 17.1, 18.1
- **Words:** persistent data path, save folder, AppData, LOCALAPPDATA, APPDATA, XDG_CONFIG_HOME, SDL_GetPrefPath, per-user folder, user directory, company name
- **Seen as:** saves are lost after an update; saves land next to the exe; two games share or overwrite saves
- **Ask me like:** "where do my saves go on Linux?"; "put saves in the Documents folder"

### 37.9 Online service clients (shared with area 17)
- **What:** Header-only clients for the online services: a local or remote account service, local-first achievements, friends, parties, direct messages and servers, forums, Workshop, public profiles, admin tools and PlayFab. They all drive one `AccountService` and no-op when offline or signed out.
- **Parts:**
  - 37.9.1 Accounts - `Platform/Account/Account`, `Platform/Account/AccountService`, `engine/src/Platform/Account/Account.cpp` - sign in, register, session, local on-disk or Supabase backend, `account_*` script builtins
  - 37.9.2 Achievements - `Platform/Achievements` - per-user local achievements shared by game and launcher
  - 37.9.3 Friends and parties - `Platform/Friends`, `Platform/Parties` - friend codes, requests, activity; party leader's session followed by members
  - 37.9.4 Chat and servers - `Platform/Messages`, `Platform/Servers` - direct messages and Discord-style servers and channels
  - 37.9.5 Forums and Workshop - `Platform/Forums`, `Platform/Workshop` - threads and posts; shared games and templates with download URLs
  - 37.9.6 Profiles - `Platform/Profiles` - public player profiles, showcase picks, mirrored achievements
  - 37.9.7 Admin - `Platform/Admin` - roles, site bans, audit log, dashboard numbers
  - 37.9.8 PlayFab - `Platform/PlayFab/PlayFab` - PlayFab Client REST (login, stats, leaderboards, cloud data) over curl, no SDK
- **Code:** `Platform/Account/Account`, `Platform/Account/AccountService`, `engine/src/Platform/Account/Account.cpp`, `Platform/Achievements`, `Platform/Friends`, `Platform/Parties`, `Platform/Messages`, `Platform/Servers`, `Platform/Forums`, `Platform/Workshop`, `Platform/Profiles`, `Platform/Admin`, `Platform/PlayFab/PlayFab`
- **API:** `Account::Get`, `okay::account::AccountService`, `Friends`, `Parties`, `Messages`, `Servers`, `Forums`, `Workshop`, `Profiles`, `Admin`, `PlayFab::Get`, script `account_*`
- **Editor:** none in the engine; the launcher shows these pages (area 17)
- **Docs:** `docs/accounts.md`, `docs/supabase-backend.md`, `docs/launcher-multiplayer.md`, `docs/photon.md`
- **Tests:** `account`, `account_script`, `friends_json`, `chat_json`
- **Status:** Works for accounts, friends and chat JSON handling (headers read in outline; covered by `account`, `account_script`, `friends_json` and `chat_json`); the remote paths need a live Supabase and are not covered by tests
- **Open:** no tests for forums, Workshop, profiles, admin, parties or PlayFab; PlayFab and the non-Windows account path need a system `curl` binary; ownership of these files should be settled with area 17, which lists them as 17.2-17.10
- **Related:** 17.2, 17.3, 17.4, 17.5, 17.6, 17.7, 17.9, 17.10
- **Words:** account, sign in, Supabase, friends, party, messages, chat, servers, channels, forums, Workshop, marketplace, profile, achievements, admin, PlayFab, leaderboard, offline mode
- **Seen as:** cannot sign in; the friends list is empty; messages do not arrive; achievements are not shared with the launcher; an admin page is empty for a normal user
- **Ask me like:** "add a sign-in button to my game"; "post a score to the PlayFab leaderboard"
