# Web & mobile builds

The OkaySpace **player** (`player/`) is plain C++17 + SDL2 with a frame-callback
loop, so it ports to the web and mobile the same way any SDL2 app does. The game
itself — your `.okayscene` and assets — is identical on every platform; only the
player is recompiled for the target.

## Web (WebAssembly, via Emscripten)

The player already uses `emscripten_set_main_loop` under `__EMSCRIPTEN__` (a
blocking `while` loop would freeze the browser tab), and `player/CMakeLists.txt`
pulls SDL2 from Emscripten's ports (`-sUSE_SDL=2`).

```bash
# One-time: install emsdk and `source emsdk_env.sh`
scripts/build-web.sh path/to/game.okayscene
python3 -m http.server -d build-web/web      # serve it
# open http://localhost:8000/okay-player.html
```

This produces `okay-player.html` + `.js` + `.wasm`. The scene you pass is
preloaded into the virtual filesystem as `game.okayscene`; preload extra art and
audio the same way by adding `--preload-file art/@art/` to the linker flags
(or extend `scripts/build-web.sh`). Keyboard, mouse, and audio work in the
browser through SDL2; networking (UDP) does not.

## Mobile

SDL2 officially supports Android and iOS, and the player is a standard SDL2
`main()`, so it builds with the usual SDL mobile project setup.

### Android
1. Use the **SDL2 Android project template** (`SDL/android-project`).
2. Add `player/main.cpp` and the engine sources (`engine/src/**`, include
   `engine/include` and `engine/third_party`) to the `jni/src` build.
3. Bundle `game.okayscene` and assets under `app/src/main/assets`; SDL maps that
   to the base path, so the player's `SDL_GetBasePath()` lookups resolve.
4. Build the APK with Gradle / Android Studio (NDK).

### iOS
1. Use the **SDL2 Xcode iOS template**.
2. Add `player/main.cpp` + the engine sources and headers to the target.
3. Add `game.okayscene` and assets to the app bundle (Copy Bundle Resources).
4. Build and run from Xcode.

### Touch input
SDL delivers touches as mouse events by default, so `Input::MousePosition()` /
`GetMouseButton(0)` and **UI buttons** already respond to taps. For multi-touch
or gestures, read `SDL_FINGER*` events in `player/main.cpp` and feed them in.

## Notes

- The editor (Dear ImGui desktop app) is desktop-only; you author on desktop and
  ship the **player** to web/mobile/desktop.
- The data format (`.okayscene`, prefabs, prefs, WAV/PNG assets) is portable, so
  one project builds to every target without changes.
