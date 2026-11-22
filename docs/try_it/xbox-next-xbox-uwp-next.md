---
id: xbox-next
row: | xbox-next | Xbox UWP next: one-page guide (PC test, Dev Mode, IP, Device Portal, deploy, logs) and the new log file | Windows release: `xbox-src\scripts\build-xbox-uwp.ps1`; editor File > Build Settings > Xbox (exp.) | Linux-tested only; nothing here has run on a console |
---
## xbox-next. Xbox UWP next: PC test, Dev Mode, Device Portal, logs

**Read this first: EXPERIMENTAL and UNVERIFIED.** The triangle package ran on a Windows 11 PC (owner, 2026-11-18). Nothing has run on an Xbox. This makes a UWP app for an Xbox One in **Developer Mode**. It is not a native console game, and we make no claim that any console hardware is supported. Steps that talk to the Xbox are from Microsoft's public pages and may look different on your console: if a screen differs, write down what it says and send it. Background: `docs/platform/xbox_uwp.md`.

**What is new in this change.** (1) The app now writes a log file `okayspace.log` (see step 9). (2) The build script prints a "WHAT TO DO NEXT" line at every failure, re-uses its test certificate, and prints the install lines for the PC and the Xbox at the end. (3) Linux tests that run the pack-and-verify pipeline and check the PowerShell scripts without Windows: `ctest -R xbox_uwp`.

### A. The PC test (do this first; no Xbox needed)

1. Install Visual Studio (2022 or newer, free Community edition) with the workload **Universal Windows Platform development**. Check with: `powershell -ExecutionPolicy Bypass -File scripts\xbox-check-sdk.ps1`. Every line should say FOUND. A MISSING line tells you what to install.
2. Open a normal PowerShell in the source folder (in the release: the `xbox-src` folder next to `OkayEngine.exe`). Run the triangle test (about a few minutes):
   `powershell -ExecutionPolicy Bypass -File scripts\build-xbox-uwp.ps1 -SourceRoot . -ExportDir .\xbox-test -MinimalTest`
   It prints `[1/9]` to `[9/9]`. If a step fails it prints `FAILED in step ...` and then **WHAT TO DO NEXT**. The full log is `xbox-test\build.log`: send it to us.
3. At the end the script prints the lines for the PC (block A). In an **administrator** PowerShell run them (remove an older copy first if there is one, then `Import-Certificate ...`, then `Add-AppxPackage ...`).
4. Start **OkaySpace Test** from the Start menu. You should see a dark window with a coloured triangle. With a pad connected the background turns green, the left stick moves the triangle, **A** centres it, **B** closes it.
5. Then the engine mode (long compile, may fail; this has never been through Microsoft's compiler on a console path):
   `powershell -ExecutionPolicy Bypass -File scripts\build-xbox-uwp.ps1 -SourceRoot . -ExportDir .\xbox-engine -MinimalTest -EngineMode`
   Install it the same way. You should see an orange cube in a blue sky and a dark panel "Hello Xbox (UI layer)". **B** closes it. Press the Windows key and come back to test suspend and resume.
6. If Windows refuses to install: `0x800B0109` = the certificate was not imported as administrator; `0x80073CFB` = an older copy with the same version is installed (`Get-AppxPackage com.okayspace.test | Remove-AppxPackage`); `0x80073CF3` with "dependency" = install the VCLibs package whose path the script prints.

### B. Turn on Developer Mode on the Xbox (reported steps, unverified)

7. On the Xbox install the free **Dev Mode Activation** app from the Microsoft Store and follow it (it needs a Microsoft account registered as a developer; cost and rules are on Microsoft's page "Activate Developer Mode on your Xbox"). The console restarts into **Dev Home**. Dev Mode is a separate mode: your normal games are not available while it is on.

### C. Find the Xbox's address and open the Device Portal

8. In Dev Home the console's **IP address** is shown (also under the console's Settings > Network > Advanced settings). Write it down. The PC and the Xbox must be on the same network.
   In Dev Home open the remote-access settings and turn on **Windows Device Portal** (it asks for a user name and password: choose any, do not reuse a real password). Then on the PC open `https://<the IP address>:11443` in a browser. The browser warns about the certificate: continue. Sign in with the user name and password you just chose. (The "pair with Visual Studio" PIN screen is for Visual Studio, not needed here.)

### D. Deploy the package

9. In the Device Portal choose **Apps** (Add / Install app). Pick the signed `.appx` (`xbox-test\com.okayspace.test_1.0.0.0_x64.appx` or the one the script printed). Add `Microsoft.VCLibs.x64.14.00.appx` as a dependency if the page has the field (the script prints where that file is). If the page has a **certificate** field, choose `OkaySpaceXbox-test.cer`. Press Install / Go.
10. Start **OkaySpace Test** from Dev Home's list of Dev Mode apps. Expected: the same triangle, a pad turns the background green, B exits. Whether the console accepts our self-made test certificate is **unknown**: if the portal refuses the package, write down the exact message. Microsoft's usual route is Visual Studio's Remote Machine deployment, which we have not set up.

### E. Where the logs are (new)

11. The app writes `okayspace.log` (and the older `okayspace.1.log` once it grew past 512 KB) into its **LocalState\logs** folder. Each line starts with the seconds since the app started. Look there first when something goes wrong: it lists the start-up line, the folders in use, pads connected, suspend and resume, and the reason the app exited.
    * On a PC: in Explorer type `%LOCALAPPDATA%\Packages\` and open the folder whose name starts with `com.okayspace.test`, then `LocalState\logs`.
    * On the Xbox: Device Portal > Apps > the app > its file browser (**LocalAppData / LocalState / logs**), if your portal has that page. Browser address form (unverified): `https://<ip>:11443/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=<package full name>&path=\LocalState\logs&filename=okayspace.log`.
    * The build log is on the PC: `<ExportDir>\build.log`.

### F. What to send back

12. A photo or description of each screen, the exact text of any error (the script's `FAILED` block, the portal's message, the console's error code), the console model (One, One S, One X), the Dev Home / OS version, `okayspace.log` if the app started, and whether the pad worked. Never send passwords or the contents of certificates.

### Without Windows

`ctest -R xbox_uwp` (needs `-DOKAY_BUILD_XBOX_UWP=ON -DOKAY_BUILD_TESTS=ON`) now also runs: `xbox_uwp_next` (the log file: appending, rotation, bad folder), `xbox_uwp_pipeline` (export, pack with our packer, read back with our reader and with Python's zip reader, manifest rules, damaged packages refused) and `xbox_uwp_scripts` (every bracket and quote in the five PowerShell scripts balanced, files ASCII, every repo path the scripts name exists, a next-step hint for each of the 9 steps, the switches named in the docs exist).

**Status.** Linux-tested: the log file code, the packer (it now also refuses device names such as `CON.txt`), the script checks. Compiled only by reading (never by Microsoft's compiler): the log wiring in `app/main.cpp`, `app/engine_bridge.cpp` and `app/engine_shell.hpp`. Never run: the PowerShell script changes, anything on an Xbox. Windows PowerShell was not available here, so the script was checked by a static scan only.
