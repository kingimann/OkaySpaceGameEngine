---
id: xbox-uwp
row: | xbox-uwp | Xbox One export (EXPERIMENTAL, UNVERIFIED): one-click build (check, export, compile, shaders, pack, sign, install), own .appx packer, engine mode | File > Build Settings > platform "Xbox (exp.)" > Build Xbox package | Linux-tested only |
---
## xbox-uwp. Xbox One (experimental, Developer Mode) export

**Read this first: the triangle-test package was built, installed and run on a Windows 11 PC on 2026-11-18 (Visual Studio 2026, MSVC 14.52, toolset v145, Windows SDK 10.0.26100.0); nothing has run on an Xbox, and the editor button, the gamepad, suspend/resume and engine mode are not verified.** It makes a UWP package for an Xbox One in Developer Mode, not a native console game. Full text: `docs/xbox_uwp.md`; audit: `docs/xbox_uwp_audit.md`.

What to do (needs a Windows PC and a release built with `-DOKAY_BUILD_XBOX_UWP=ON`, which `build-windows-dist.sh` sets; it also ships `xbox-src` next to the editor):

1. **Once:** install Visual Studio 2022 (free Community edition) with the workload "Universal Windows Platform development", its C++ UWP tools and a Windows 10/11 SDK.
2. Open the editor, **File > Build Settings**, click **Xbox (exp.)** at the top. You should see an orange "EXPERIMENTAL and UNVERIFIED" line, the limits text, the tools list (found / MISSING with the install step), and the package name, version and publisher. A missing required tool greys the button with a tooltip; **Check again** re-checks.
3. Leave **Engine mode** unticked the first time (a triangle test, the safest). Tick **Also install on this PC afterwards** if you want it on this PC. Press **Build Xbox package**.
4. Watch the page: a step list (Check the Windows tools, Export, Set up the UWP app build, Compile the UWP app, Put the program into the package, Compile the shaders, Pack and check the .appx, Sign, Install) with a status per step and a live log. **Cancel build** stops it. If a step fails you get a plain message and the first error lines: send us those.
5. If you ticked install, Windows shows **one** administrator (UAC) prompt: accept it. Then start the app from the Start menu: you should see a dark window with a coloured triangle (the background turns green once a pad is connected; left stick moves it, A centres it, B exits).
6. The result is `<output>\<name>_<version>_x64.appx` (signed with a test certificate from PowerShell + SignTool: the page says so) and `OkaySpaceXbox-test.cer` (public part only).
7. **Engine mode** (EXPERIMENTAL): tick it and build again to try running your exported game (`package\Data`): 3D scene, scripts, physics, gamepad and keyboard. The 2D / UI layer is not drawn. It has never been compiled with Microsoft's compiler, so a compile error is likely: the page then tells you to untick it. Please send the first error lines.
8. **No editor with the Xbox tab yet?** One command from a source checkout or `xbox-src`:
   `powershell -ExecutionPolicy Bypass -File scripts\build-xbox-uwp.ps1 -SourceRoot . -ExportDir .\xbox-test -MinimalTest -SkipInstall`
   It writes a test export, builds, packs and signs it (progress lines `[1/9]` ... `[9/9]`). On a failure it prints the step and the last 15 lines of output; send us `xbox-test\build.log`.
9. For the console: `docs/xbox_uwp.md`, "Deploy to an Xbox in Developer Mode" (Device Portal upload).

Checks you can run without Windows: `ctest -R xbox_uwp` (needs `-DOKAY_BUILD_XBOX_UWP=ON -DOKAY_BUILD_TESTS=ON`) runs the Linux tests: the old package/packer tests, `xbox_uwp_runtime` (shader bundle, display, lifecycle, pad mapping, paths, audio conversion, the backend over a fake host, the build plan and runner, the API audit, game config), `xbox_uwp_audit` (no forbidden UWP API in the engine sources the UWP target compiles) and `xbox_uwp_sources_cmake`.

Verified on Windows (owner, 2026-11-18, the `-MinimalTest` command): the whole script, MakeAppx accepting our package, signing, install and the triangle on a PC. Verified on Linux: everything in the tests. Not verified: the editor page drawing and one-click button, the gamepad, suspend/resume, engine mode, the console. The `Microsoft.VCLibs` install line printed at the end is optional and may fail harmlessly with 0x80073D02 when the framework is in use.
