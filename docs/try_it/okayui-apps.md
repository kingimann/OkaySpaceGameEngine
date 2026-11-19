---
id: okayui-apps
row: | okayui-apps | The launcher and editor built on OkayUI (OkaySpaceOkayUI / OkayEngineOkayUI), off by default | CMake `OKAY_LAUNCHER_ON_OKAYUI` / `OKAY_EDITOR_ON_OKAYUI`; Settings > Experimental > Run on our own UI | Launcher ran headless on Linux; the editor on OkayUI not built or run yet |
---
## The launcher (and editor) on our own UI: OkaySpaceOkayUI / OkayEngineOkayUI (off by default)

What it is: the same launcher and editor programs, compiled against OkayUI instead of Dear ImGui (`docs/own_ui_toolkit.md` 6.26). They are extra programs; the normal ones are unchanged.

1. Build: configure CMake with `-DOKAY_LAUNCHER_ON_OKAYUI=ON` (and `-DOKAY_EDITOR_ON_OKAYUI=ON` with `-DOKAY_BUILD_EDITOR=ON`), then build the targets `okayspace-launcher-okayui` and `okay-editor-okayui`. They are written to the same `bin` folder as `OkaySpaceOkayUI` and `OkayEngineOkayUI` (`.exe` on Windows). `scripts/build-windows-dist.sh` copies them into the Windows folder when they were built (the launcher beside `OkaySpace.exe`, the editor in `Tools\`).
2. Run: double-click `OkaySpaceOkayUI.exe`, or in the normal launcher open Settings > Experimental and press Restart on "Run on our own UI (restart into OkaySpaceOkayUI.exe)" (the row appears only when that file is next to the launcher). In the OkayUI launcher the same page has "Back to the normal UI".
3. What you should see: the launcher looks like the normal one (same pages, fonts, colours). The start-up text line says "UI drawing: OkayUI frame through okay::platform::Painter (the OkayUI build; Dear ImGui is not in this program)". Starting a project from the OkayUI launcher opens `OkayEngineOkayUI` when it is next to it.
4. Honest status: the launcher ran only headless on Linux (scripted clicks, pictures looked at); the editor on OkayUI has not been built or run yet; nothing was run on Windows. Report anything that looks or behaves differently from the normal programs.
