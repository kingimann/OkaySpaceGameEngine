---
id: xbox-uwp
row: | xbox-uwp | Xbox One export (EXPERIMENTAL, UNVERIFIED): package folder, own .appx packer, SDK check | File > Build Settings > platform "Xbox (exp.)"; scripts in the source checkout | Linux-tested only |
---
## xbox-uwp. Xbox One (experimental, Developer Mode) export

**Read this first: nothing here has been built on Windows or run on an Xbox.** It makes a UWP package for an Xbox One in Developer Mode, not a native console game, and the app shell only draws a test triangle (it does not run your game). Full text: `docs/xbox_uwp.md`; audit: `docs/xbox_uwp_audit.md`.

In the release (needs a build made with `-DOKAY_BUILD_XBOX_UWP=ON`, which `build-windows-dist.sh` now sets):

1. Open the editor, File > Build Settings, click **Xbox (exp.)** at the top.
2. You should see an orange "EXPERIMENTAL and UNVERIFIED" line, the limits text, a list of Visual Studio 2022 / UWP tools / Windows SDK / SignTool as found or MISSING with the install step, and the package name, version and publisher it will use. If anything required is MISSING the **Export** button is greyed with a tooltip. **Check again** re-runs the check.
3. With everything found, press Export. Open the output folder: `package\AppxManifest.xml`, `package\Assets\` (14 PNG files; generated placeholders unless the project has an icon, then `PLACEHOLDER_ASSETS.txt` is written), `package\Data\`, `README_XBOX.txt`, `xbox-export.json`.
4. Building the app, packing with our own packer and signing: `scripts\build-xbox-uwp.ps1` (see `docs/xbox_uwp.md`, "Verify it yourself"). The checks only: `scripts\xbox-check-sdk.ps1`.

Verified on Linux: manifest, assets, package folder, our own `.appx` writer and reader (every block hash, content types), SDK decision logic (fake folders), `unzip -t` and Python `zipfile`/`hashlib` agree with the packer; `ctest -R xbox_uwp`. Not verified: everything that needs Windows, Visual Studio, SignTool, the editor window, or a console.
