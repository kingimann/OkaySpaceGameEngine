# Xbox One (experimental, Developer Mode) export

Internal. **EXPERIMENTAL and UNVERIFIED.** Owner request 2026-11-18: a hobby target, not for publishing. Branch `claude/xbox-uwp-1`.
The audit that led here is [xbox_uwp_audit.md](xbox_uwp_audit.md).

## What this is, and what it is not

* It makes a **Universal Windows Platform (UWP)** package that an **Xbox One in Developer Mode** can run, using only Microsoft's public
  Windows SDK and public tools. Nothing from the restricted GDKX, no proprietary code.
* It is **not** a native console game. The public SDK does not build native console games, and this does not claim to.
* It was **written on Linux**, which has no Visual Studio, Windows SDK, SignTool or UWP compiler and no Xbox. Nothing that needs those
  has ever run. "Compatibility with any console" is **unknown**.
* The Xbox app shell in this version **does not run your game**. It opens a window, draws a test triangle with Direct3D 11 and reads a
  gamepad (left stick moves the triangle, A centres it, B exits). It exists to prove the packaging pipeline. Making the engine itself
  run inside UWP is the unfinished part (audit, "Concrete compatibility problems").

## Where everything is

| What | File | Verified |
|---|---|---|
| SDK check, pure decision logic (Linux-testable) | `platform/xbox_uwp/XboxSdkCheck.hpp`, real disk and `vswhere`: `XboxSdkProbe.hpp` | logic: tests on Linux with fake folders. Real probe: **never run on Windows** |
| Same check for PowerShell | `scripts/xbox-check-sdk.ps1` | **never run** (no PowerShell here) |
| Manifest (`AppxManifest.xml`), name, version and publisher validation, tile list | `XboxManifest.hpp` | text generated and checked on Linux; **never accepted by a real Windows install** |
| Tile and logo images (14 PNG files from the project icon, or labelled placeholders) | `XboxAssets.hpp` | produced and read back on Linux; looks not reviewed by eye |
| Package folder (what the editor writes) | `XboxExport.hpp` | tested on Linux |
| Our own `.appx` writer and checker (no MakeAppx) | `XboxAppx.hpp`, command line `tool/okay_appx.cpp` (`okay-appx pack`, `okay-appx verify`) | container, content types and every block hash checked by our own reader **and by Python's `zipfile`/`hashlib` and `unzip -t`** on Linux; **never opened by Windows** |
| Build, pack, test certificate, sign | `scripts/build-xbox-uwp.ps1` | **never run** |
| UWP app shell | `platform/xbox_uwp/app/` (`main.cpp`, two HLSL files, CMake) | **uncompiled**. Only brace balance was checked |
| Editor entry | File > Build Settings > platform **Xbox (exp.)** (`editor/main.cpp`, under `#if OKAY_XBOX_UWP`) | the editor code was compiled (syntax only) with g++ on Linux, with and without the option; **never seen on a screen** |
| Tests | `platform/xbox_uwp/tests/test_xbox_uwp.cpp`, ctest name `xbox_uwp` | run on Linux, see below |

Isolation: everything Xbox is in `platform/xbox_uwp/`. The CMake option `OKAY_BUILD_XBOX_UWP` is OFF by default. It adds the test, the
`okay-appx` tool and, in the editor, one define (`OKAY_XBOX_UWP`) and one include path. `scripts/build-windows-dist.sh` turns it on so the
Windows editor you run has the Xbox entry (rule 4a). To remove the feature: delete the folder, the option and its `add_subdirectory` line in
the top `CMakeLists.txt`, the lines in `editor/CMakeLists.txt` and `scripts/build-windows-dist.sh`, and the `#if defined(OKAY_XBOX_UWP)` blocks in
`editor/main.cpp`. Without the define the editor compiles exactly as before (checked). The existing Windows export is untouched.

## What you need on the Windows PC (written from Microsoft's pages; check them)

All free. Pages are named, not linked, so they cannot go stale: search for the name on microsoft.com.

1. **Visual Studio 2022** (the Community edition is enough): the "Visual Studio downloads" page.
2. In the Visual Studio Installer choose **Modify** and tick the workload **Universal Windows Platform development**. Under its optional
   parts tick **C++ (v143) Universal Windows Platform tools** and a **Windows 10 SDK or Windows 11 SDK, 10.0.17763.0 or newer**. This
   gives the compiler's UWP libraries, the Windows SDK's C++/WinRT headers (`cppwinrt`), `WindowsApp.lib`, `d3d11.lib`, `fxc.exe` and `SignTool.exe`.
   (The "Windows SDK Signing Tools for Desktop Apps" feature carries SignTool and MakeAppx.)
3. PowerShell 5.1 (comes with Windows 10/11).
4. For the console: an Xbox One with **Developer Mode** turned on. Microsoft's page "Xbox One Developer Mode" / "Activate Developer Mode on your Xbox"
   describes the **Dev Mode Activation** app from the console's Store and the developer account it needs. Reported cost and account rules
   are on that page; we did not check them.

`scripts/xbox-check-sdk.ps1` and the editor check Visual Studio 2022 (through `vswhere`, version range 17), the UWP toolset (a `VC\Tools\MSVC\<v>\lib\x64\store`
folder), the SDK folders (`Windows Kits\10\Include\<v>\winrt`, `cppwinrt`, `um`, `ucrt`; `Lib\<v>\um\x64\WindowsApp.lib`, `d3d11.lib`) and `signtool.exe`.
`makeappx.exe` is reported but **optional** (our packer replaces it). When something required is missing the editor shows **Xbox export
unavailable**, lists what is missing with the install step, and the Export button is disabled. **These folder names are from memory and unverified**; if the
check says "missing" for something that is installed, that is a bug in the check: send the output of `dir "C:\Program Files (x86)\Windows Kits\10\Include"` and the MSVC folder.

## The pipeline

1. **Editor:** File > Build Settings, platform **Xbox (exp.)**, set name, version and icon (Player page), Export. It writes
   `<out>\package\AppxManifest.xml`, `package\Assets\*.png`, `package\Data\` (your game's data through the existing export code), `README_XBOX.txt`,
   `xbox-export.json` and, if there is no icon, `PLACEHOLDER_ASSETS.txt` (the images are then generated stand-ins, not final art).
2. **PC:** `powershell -ExecutionPolicy Bypass -File scripts\build-xbox-uwp.ps1 -SourceRoot <OkaySpace source> -ExportDir <out>` runs the SDK check,
   compiles `okay-appx` with `cl.exe`, builds the UWP app (`cmake ... -DCMAKE_SYSTEM_NAME=WindowsStore`), compiles the two shaders to `.cso` (build time; `D3DCompile`
   does not exist inside UWP), copies the exe and shaders into `package\`, runs `okay-appx pack` then `okay-appx verify`, makes a test certificate with
   `New-SelfSignedCertificate`, and signs with `SignTool sign /fd SHA256 /sha1 <thumbprint>`. With `-SkipSign` it stops after the unsigned package.
3. **Console:** see "Deploy".

## Our own packer: what it writes and which rules could not be checked

`okay-appx` writes an **unsigned** `.appx`: a ZIP holding the payload files, `AppxManifest.xml`, `AppxBlockMap.xml` (per 64 KiB block SHA-256, base64) and
`[Content_Types].xml`. Written from Microsoft's "App package structure", "App package block map" and "App package manifest" pages, ECMA-376 part 2
(Open Packaging Conventions) and the ZIP application note. The hash is our `okay::crypto::Sha256`; the CRC-32 table, base64 and ZIP code are written in the file.
No existing zip writer was reused: `builder::WriteZip` stamps the current time, deflates, and has no control of order or of the local-header size the block map
needs (`LfhSize`).

**What the tests prove (Linux):** for a package with a manifest, a PNG, a 130,000-byte file (2 blocks), an empty file, a file with no extension and a space in its name:
our reader finds every entry, every CRC-32 matches, every content type is covered (`Default` per extension, `Override` for no-extension files, manifest and block map),
every file is in the block map once with the right `Size`, `LfhSize` and block count, and **every block hash is recomputed and equal**. A flipped payload byte, a wrong block hash, a
renamed block map, a truncated file, a missing manifest, a duplicate name (ignoring case) and the names `..`, `/abs`, `a:b`, `trailing.` are all caught or refused. The same file
was cross-checked outside our code: `unzip -t` is clean and Python's `zipfile` and `hashlib` recompute the same block hashes and sizes. Output is deterministic.

**Rules we could not check without Windows (all unverified, each is a possible reason Windows refuses the package):**

1. Entry order. We write the manifest first, the payload sorted by name, then `AppxBlockMap.xml`, then `[Content_Types].xml`. We do not know whether Windows requires an order.
2. All entries are **stored** (no deflate). We believe a stored package is allowed (MakeAppx has a no-compression switch) and that a stored file's `<Block>` has no `Size`
   attribute (the attribute is for the compressed size). Windows may want it.
3. `LfhSize` is our reading of "size of the local file header": 30 bytes plus the name length (we write no extra field).
4. Names inside the ZIP are percent-encoded except `[Content_Types].xml`; block map names use `\`. The exact set of characters that must be encoded is a guess (we encode everything except letters, digits, `- . _ ~ /`).
5. The DOS time stamp 1980-01-01, `version needed` 20, no data descriptors, no ZIP64 (we refuse a package over 4 GB).
6. Content types for unknown extensions are `application/octet-stream`; the `Default` list is ours.
7. Whether the extra `scale-200` images and the unscaled base images both being present is accepted (MakeAppx does not enforce image sizes, Visual Studio does).
8. The `AppxManifest.xml` itself (identity, `Windows.Universal` device family, `MinVersion` 10.0.17763.0, `EntryPoint`, the `Microsoft.VCLibs.140.00` dependency with its publisher string) is unverified.
   An Xbox in Developer Mode must also be able to find the VCLibs framework package; the Device Portal install page lets you upload dependencies.
9. Package full-name and publisher-id rules (the signer's subject must equal `Identity/@Publisher` exactly) are enforced only by the validator we wrote from the documentation.

The editor and the script both run `okay-appx verify` on the result and refuse to hand out a package that fails it. A MakeAppx `unpack` cross-check runs in the build script when MakeAppx exists.

## Signing: honest assessment

Windows installs only **signed** packages. The signature is the part `AppxSignature.p7x`: from Microsoft's description, the four letters `PKCX` followed by a PKCS#7 / CMS
`SignedData` whose content is an Authenticode "indirect data" structure carrying digests of the package parts (the entry data, the central directory, `[Content_Types].xml`,
`AppxBlockMap.xml`). The signing certificate must carry the code-signing usage and its subject must equal the manifest publisher.

What writing it ourselves would need:

* **ASN.1 DER writing** (lengths, OIDs, sets, the X.509 `Certificate`, `SignedData`, signed attributes): ordinary parsing and writing, no secrets. We could write it.
* **SHA-256**: we have it.
* **RSA signing** (RSASSA-PKCS1-v1_5 with SHA-256, 2048-bit keys, big-number arithmetic, key generation with primes, padding): this is **cryptography**. libsodium has no RSA. CLAUDE.md rule 18:
  cryptography is not replaced on our word and no outside review is planned. **We wrote no RSA code and will not without the owner's explicit approval.** Doing it would also need
  constant-time big-number code; a self-signed test certificate for a hobby console is a low-stakes use, but that is the owner's call, not ours.

Options, in the order we would take them:

1. **Now (built):** our packer makes the unsigned package. SignTool signs it on the PC (Microsoft's tool, public), with a test certificate from PowerShell's
   `New-SelfSignedCertificate`. No cryptography of ours is involved.
2. **Possible next step, needs the owner's decision:** our code writes all the structures (DER, `SignedData`, the digests) and asks the **operating system's** key store to
   produce only the RSA signature value (Windows CNG `NCrypt` / `BCrypt`). The RSA stays Microsoft's; ours is the container. This is not "own crypto", but it still must be checked on Windows.
3. **Only with explicit approval:** our own RSA. Would be labelled "not independently reviewed" like `okay::crypto`.

## Compile step: what it would take for okay-ms to build the app later

For now **Microsoft's UWP compiler builds the app** (`cl.exe` through CMake and MSBuild). `tools/okayms/` (`docs/own_msvc_plan.md`) is our MSVC-style toolchain; it cannot do this today. What it would take,
unverified and from the plan page plus the UWP rules:

* **A language gap that is large.** The plan page says classes, member functions, templates and exceptions are *not started*. The shell is C++/WinRT (templates, classes, lambdas, exceptions). It would have to be
  rewritten in plain C against COM v-tables. That is possible (WinRT is COM), but it is a different, longer program.
* **Our own declarations of every COM/WinRT interface used**, written from Microsoft's documentation of the interface layouts (v-table order, IIDs, `HSTRING` functions): `IFrameworkViewSource`,
  `IFrameworkView`, `ICoreApplication`, `ICoreWindow`, `IDXGIFactory2`, `ID3D11Device`, `ID3D11DeviceContext`, `IDXGISwapChain1`, `IGamepadStatics`. Not copied from the SDK headers (rule 19a).
* **Import libraries for UWP.** UWP apps link the umbrella `WindowsApp.lib` (plus `d3d11.lib`, `dxgi.lib`). `okay-lib`-style import libraries can be generated from the DLL export lists
  (the plan page says import libraries are written: stage 9), so we can make ours from the export tables of the system DLLs on a Windows PC. Which DLL exports are allowed in UWP is Microsoft's allow-list
  (the umbrella's contents); that list must be read from `WindowsApp.lib` on Windows once.
* **The App Container bit.** A UWP exe must be marked as an app-container executable in its PE header and use only allowed APIs. `okay-link` has no such flag today (only NX is mentioned in `link.hpp`).
* **No CRT of Microsoft's.** The shell uses the VC runtime (hence the `Microsoft.VCLibs.140.00` dependency). With okay-ms we would ship our own small runtime and drop that dependency, but the runtime is a
  few functions today.
* **Shaders** still need an HLSL compiler. `fxc.exe` ships with the SDK; our own HLSL compiler does not exist.
* **Debug and unwind:** `.pdata/.xdata` exist; no PDB (only a CodeView directory), so crash debugging on the console would be by hand.

Realistic order: first make the app C (no WinRT classes), then the three missing language features, then the app-container flag, then generated import libraries. None of it is started in this branch.

## Deploy to an Xbox in Developer Mode (reported steps, unverified)

1. On the console, activate Developer Mode (Dev Mode Activation app). It restarts into the Dev Home screen, which shows the console's address.
2. On the PC, open the **Windows Device Portal** of the console in a browser at the address Dev Home shows (HTTPS; the usual port reported for Xbox is 11443). Accept the certificate warning.
3. Apps > **Add** (the page for uploading an app package): choose the signed `.appx`, add the `Microsoft.VCLibs.140.00` framework package as a dependency (the file is in Visual Studio's
   `Common7\IDE\Extensions\Microsoft\...\Appx` folders or the Visual C++ runtime download page for UWP), and install.
4. Start the app from Dev Home / the console's "Dev Mode" apps. Expected: a dark window with a coloured triangle; the background turns green once a pad is connected; the left stick moves the triangle; B exits.
5. Whether a self-signed test certificate is accepted by the console without trusting it first is **unknown**. Microsoft's usual route is Visual Studio's "Remote Machine" deployment; if the Device
   Portal refuses the package, try that route and report what it said.

## Verify it yourself on a Windows PC (no Xbox needed for most of it)

A UWP package also installs on Windows 10/11, so steps 1 to 6 test the whole pipeline without a console.

1. Install Visual Studio 2022 with the pieces above.
2. `powershell -ExecutionPolicy Bypass -File scripts\xbox-check-sdk.ps1` . Paste the whole output.
3. Start the editor, File > Build Settings, choose **Xbox (exp.)**. Paste what the Xbox page says (or a photo; screenshots stay out of the repo). Press Export. Tell us whether `package\` has the manifest and 14 images.
4. `powershell -ExecutionPolicy Bypass -File scripts\build-xbox-uwp.ps1 -SourceRoot <source folder> -ExportDir <export folder>`. Paste the whole output, including the `okay-appx` lines and the MakeAppx unpack exit code (0 means Microsoft's own tool read our container).
5. As administrator: `Import-Certificate -FilePath <out>\OkaySpaceXbox-test.cer -CertStoreLocation Cert:\LocalMachine\TrustedPeople`, then `Add-AppxPackage <out>\<name>.appx`. Paste any error text.
6. Start the app from the Start menu. Tell us what you see (expected: the triangle) and whether a pad moves it.
7. Only with a console in Developer Mode: the Deploy steps. Tell us exactly what each page showed.

Run `ctest -R xbox_uwp` (needs `-DOKAY_BUILD_XBOX_UWP=ON -DOKAY_BUILD_TESTS=ON`) to run the Linux tests anywhere.

## Limits to remember

UWP sandbox (no desktop-only APIs, read-only install folder, smaller budgets than a PC), Developer Mode only, no GDK, no publishing, Direct3D 11 only, the engine is not
inside the app yet, no hardware ever tested, no signing code of ours.
