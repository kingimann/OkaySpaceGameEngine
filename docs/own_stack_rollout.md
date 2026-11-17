# Own stack rollout: connecting our own code to the real programs, step by step

> **2026-11-17: the model readers are flipped.** The owner approved ("start switching them so i can test them out"). Every
> `OKAY_OWN_<FMT>` model reader option, `OKAY_OWN_FBX` and `OKAY_PREFER_OWN_READERS` (STL, PLY) are ON by default (NDO stays
> OFF: a seam). Assimp and ufbx stay compiled in as the fallback. Ways back at run time: `OKAY_OWN_<FMT>=0` for one format,
> `OKAY_THIRD_PARTY=1` for all of them (`okay::ThirdPartyForced()`, `engine/include/okay/Core/ThirdParty.hpp`). Details:
> `docs/own_code_roadmap.md`, "Model readers: our own are the default". Tested on Linux only; Windows unverified.

Owner's instruction: "Start slowly connecting so we depend on them less and less." This page is the plan for that, and
it says what is real today. The removal rules stay as written in `docs/own_code_roadmap.md` ("How a library is removed");
this page adds the **stages** between "our code exists" and "the old library is deleted", the **gates** for each stage,
and the order. Nothing on this page flips a default by itself. Every flip below is a proposal that needs the owner's
written approval in the PR.

Numbers are copied from the status pages, some of which live on branches that are not merged yet:
`docs/own_code_roadmap.md`, `docs/own_readers_remaining.md` and `docs/own_http_readiness.md` (all on main), `docs/own_platform_status.md`
(branch `claude/platform-gaps2`) and `docs/okayui_gap_report.md` (branch `claude/okayui-gaps3`). Where a page is on a branch, say so before relying on it.

> **Status update, 2026-11-15.** Which flip comes next, in what order, the checklist for each and what blocks deleting each library are now in
> `docs/own_flip_proposal.md`. Corrections to this page since it was written: the release plan in section 5 is out of date (2.872.0 and 2.874.0 shipped
> without those steps); the CI jobs `own-stack` and `own-http` have run green on GitHub (main run 43754132084, own-stack first green on main in run
> 43656201342); our readers now cover 39 of the 45 importers the Windows build switches on, not 24; `OKAY_OWN_STACK` and the startup line still cover only
> 20 readers (FBX and 19 formats), not the 16 newest readers or WAV; `docs/own_http_readiness.md` is on main and PR #852 is merged.

## 1. The stages

| Stage | Name | What it means | Who sees it |
|---|---|---|---|
| 0 | Off | The code exists and is tested. Nothing selects it unless a developer sets an option by hand. | Nobody |
| 1 | Dogfood | On in CI and for developers by one switch (`OKAY_OWN_STACK`, section 3). Every other build is unchanged. | Developers, CI |
| 2 | Opt-in | A user can turn it on (Settings row, or a documented switch for headless programs). Off until they do. Old path is the default. | Users who ask |
| 3 | Default on | It is the default. There is an escape switch (setting and environment variable) and an automatic fallback to the old path where the old code is still compiled in. | Everyone |
| 4 | Old code removed | The third-party code, its CMake option, its download and its CI step are deleted, **one release after stage 3**. | Everyone |

### Gates

A component moves up only when every gate for that stage holds. "Evidence" means a test or a run that is written down
with its output; "unverified" is said out loud when it is.

| To enter | Gates |
|---|---|
| 1 | The code builds and its tests pass in CI on Linux. The switch can be turned off again at run time (`OKAY_OWN_<X>=0`). The old path is still compiled in. **This stage is open to everything except cryptography** (and cryptography only in test builds, see section 3.3). |
| 2 | Everything in 1, plus: a real-use corpus passes (real files, real servers, real hardware, not only fixtures our scripts wrote); fuzz under ASan and UBSan with no crash and no hang; no known bug in our code; a Settings row or documented switch exists; a failure falls back to the old path **by itself** where there is one, with a log line saying so; the release note says what to try and how to turn it off. |
| 3 | Everything in 2, plus: a **real Windows run** (nothing in this repository's CI or in these sessions can do that; wine and Xvfb do not count) for anything that runs on Windows; a soak run for anything with state (hours, not minutes); at least one release at stage 2 with no open bug against it; the escape switch exists and is tested; **written owner approval in the PR**; the release note names the change one release ahead. |
| 4 | Stage 3 shipped for one release with no regression report; the old code is deleted in one PR together with its option, FetchContent block, CI download and `docs/THIRD_PARTY.md` row; `docs/own_code_roadmap.md` updated. |

## 2. One row per component

"Ours" is what we wrote. "Replaces" is what stays as the fallback until stage 4. The state column is what the code does
today on main (2.874.0 shipped; 2.875.0 is in PR #914) unless it says otherwise.

| Component | Ours | Replaces | State today | Evidence so far | Needs a real Windows run? |
|---|---|---|---|---|---|
| **FBX reader** | `okay::fbx` (`FbxReader.hpp`, `FbxScene.cpp`) | ufbx (vendored, always compiled) | Stage 0. `OKAY_OWN_FBX` off. A change that makes it the default (with ufbx as fallback) exists on branch `claude/flip-model-readers`, **unmerged**, needs owner approval; the branch is 68 commits behind main and conflicts in four files, so redo it in small pieces (`docs/own_flip_proposal.md`) | 43 real files from Assimp's test directory: 42 agree with ufbx on counts, names, boxes, area, skin weights, inverse binds, clip samples and materials; the 43rd is damaged and rejected by all readers. Eight bugs found and fixed. Clean under ASan, UBSan, float-cast-overflow and valgrind. MinGW build passes under wine. | Yes (wine only so far) |
| **COLLADA, 3DS, BVH readers** | `ColladaReader`, `3dsReader`, `BvhReader` | Assimp (optional; shipped in the Windows distribution) | Stage 0 (same branch as above for the flip) | COLLADA 26 of 26 real files agree with Assimp (six bugs fixed), 3DS 15 of 16 (the 16th is empty, Assimp rejects it too), BVH 3 of 3. Hostile-input tests and mutation fuzz under sanitizers. | Yes |
| **Other readers** (OFF, SMD, MD5, MS3D, ASE, 3MF, AMF, X3D, DXF, LWO, Blender, MD2, MD3, Irrlicht mesh, B3D, DirectX .x) | 15 more readers, one file each | Assimp | Stage 0 for all. With the OKAY_OWN_STACK switch they are stage 1. **The 16 newer readers (AC3D, OGRE, XGL, IRR, NFF, Terragen, MDC, HMP, CSM, Q3D, RAW, IQM, COB, NDO, PMX, LWS) and WAV are not part of `OKAY_OWN_STACK` and not in the startup line** | Each compared with Assimp on the real files of its test directory (for example MD2 5 of 5, MD3 3 of 3, IRRMESH 5 of 5, B3D 3 of 3, X 11 of 11; AMF 11 read, 5 agree and 6 differ where Assimp reads less; X3D 3 of 4 read). SMD and MS3D skeleton paths rest on synthetic fixtures only. | Yes |
| **Formats with no own reader yet** | none | Assimp | Assimp has 51 importers; our Windows build switches on 45; we cover 39 of those 45 (counted 2026-11-15 from the import table); **6 still have no own reader** (OpenGEX, MDL, Q3BSP, SIB, Nendo, glTF 1.0) plus the binary half of OGRE; OpenGEX and MDL exist on the unmerged branch `claude/readers-e4` (`docs/own_readers_remaining.md`). | Not written | n/a |
| **HTTP client** | `okay::ownhttp` (`OwnHttp*.hpp`, wired into `NativeHttp`) | curl (spawned program) on Linux. **Windows already uses WinHTTP, which is the system's own client: nothing to replace there, and our client is not compiled on Windows.** | Stage 0 (`OKAY_OWN_HTTP` build option and `OKAY_OWN_HTTP=1` at run time). Linux only. TLS comes from the system OpenSSL loaded at run time, **not our own TLS** | HTTP/1.1, redirects, proxies with CONNECT, TLS 1.2+ with chain and host checks, ranges, progress and cancel, gzip. `test_own_http` 4,054 checks, `own_http_net` 323, `native_fetch` 54, `playfab_own` 31, `launcher_download_own` 7 (counts from `docs/own_http_readiness.md`, after the review fixes of #852). Parser fuzz 200,000 iterations clean. Readiness page says about 40 percent for the whole flip (the Linux code about 85 percent); **flip criteria 1, 2, 4 and 5 are not met**, 3 partly. Never run against GitHub Releases, Supabase, PlayFab or a CDN. The `own-http` CI job is green on main. Gaps: no proxy credentials, no SOCKS; keep-alive exists since #852 but is unverified over TLS with a real server; every runtime curl use on Linux now tries our client first (the launcher and editor call sites are compiled, not run on a desktop). | Not applicable (not compiled on Windows). A Windows own client would need TLS (Schannel) and the certificate store: that is crypto work and waits for the crypto review |
| **Platform layer** | `okay::platform` own backends: Linux X11/GLX, ALSA, evdev (through dlopen); Win32/WGL | SDL2 | Stage 0. `OKAY_PLATFORM_BACKEND=sdl2` is the default; `own` selects ours. The player's window, events, input and GL context go through the seam; its 2D drawing goes through `okay::platform::Painter` with a software painter on the own backend (off, not ready). The editor and launcher windows still call SDL directly and every app still draws through `SDL_Renderer` | Linux backend tested under Xvfb (no window manager, no real GPU, no real input method, no real file manager). Win32 backend: compiled with MinGW and run under wine 9.0 only, **never on real Windows**. macOS not written. The status page lists 35 kinds of SDL call; remaining SDL tokens: player 183, editor about 600, launcher about 1,000 (370 of them mutex and thread calls), engine about 700. **Missing: an own Dear ImGui platform backend and renderer backend, so the editor and launcher cannot leave SDL yet**; the exclusive video mode switch; touch; real audio on Windows (data path never run) | **Yes, for everything on Windows** |
| **UI toolkit** | OkayUI (`okayui/`) | Dear ImGui (FetchContent, pinned commit) | Stage 0 for users, stage 1 for developers: the launcher and editor can start on OkayUI with `OKAY_UI_BACKEND=okayui` or `OKAY_UI_DEFAULT_BACKEND=okayui`; the Settings row is shown only with `OKAY_DEV`. Only the screens that have an OkayUI port use it | Gap report (branch): of 26,740 `ImGui::` call sites 78 (0.3 percent) have no OkayUI function of that name; of 11,876 flag, type and enum uses 303 (2.6 percent) have no counterpart; 16 feature families Done, 12 Partial, 0 Missing. **All of that is headless: nothing was run in a real window.** Still missing: key ownership, `ImGuiKey` beyond 70 names, whole `ImGuiStyle`, an ImGui-style context for the launcher's second context (reshape, not port) | Yes |
| **Code editor widget** | `OkayUI::CodeEdit` | ImGuiColorTextEdit (vendored `editor/vendor/`) | Stage 0, adapter tested headless, never run in a real window | Fuzz clean under ASan and UBSan, 100k lines fast | Yes |
| **Cryptography** | `okay::crypto` (ChaCha20-Poly1305, XChaCha20, BLAKE2b, SHA-256/512, X25519, Ed25519) | libsodium (and the TweetNaCl port for anti-cheat signatures) | **Stage 0, and it stays there.** `OKAY_OWN_CRYPTO` is off in every shipped build. Never part of `OKAY_OWN_STACK`. Test builds and the CI own-stack job may turn it on with `OKAY_OWN_STACK_CRYPTO_TEST` (section 3.3) | RFC and libsodium test vectors, an independent library on random inputs, 1,100,000 fuzz iterations under sanitizers. **No outside review yet** (`docs/crypto_review_package.md`) | Yes |
| **TrueType rasteriser, embedded fonts, audio decoders** | `TtfFont.hpp`, OKF1 font container, own WAV/FLAC/MP3/Vorbis decoders | stb_truetype, stb font decompressor, dr_mp3, stb_vorbis | Done: stage 4 is already behind us for stb_image, stb_image_write, stb_vorbis, dr_mp3, the stb decompressor, font8x8, tinyfiledialogs (roadmap "Already replaced or removed"). The editor and OkayUI text go through our rasteriser | Unit tests; pinned size and SHA-256 of the embedded fonts | Not needed beyond normal releases |

## 3. The dogfood switch (stage 1 for everything at once)

### 3.1 Build: `-DOKAY_OWN_STACK=ON`

Turns the build defaults of every own backend that is safe to try into "ours first". It changes defaults only; every
run-time switch still wins, and every third-party fallback is still compiled in.

| What | CMake variable it sets | Effect |
|---|---|---|
| Our FBX reader | `OKAY_OWN_FBX=ON` | `okay::fbx` instead of ufbx (run time: `OKAY_OWN_FBX=0` goes back) |
| Our readers for COLLADA, 3DS, BVH, OFF, SMD, MD5, MS3D, ASE, 3MF, AMF, X3D, DXF, LWO, Blender, MD2, MD3, Irrlicht mesh, B3D, DirectX .x | `OKAY_OWN_COLLADA`, `OKAY_OWN_3DS`, `OKAY_OWN_BVH`, `OKAY_OWN_OFF`, `OKAY_OWN_SMD`, `OKAY_OWN_MD5`, `OKAY_OWN_MS3D`, `OKAY_OWN_ASE`, `OKAY_OWN_3MF`, `OKAY_OWN_AMF`, `OKAY_OWN_X3D`, `OKAY_OWN_DXF`, `OKAY_OWN_LWO`, `OKAY_OWN_BLEND`, `OKAY_OWN_MD2`, `OKAY_OWN_MD3`, `OKAY_OWN_IRRMESH`, `OKAY_OWN_B3D`, `OKAY_OWN_X` all `ON` | each format reads with ours first (run time: `OKAY_OWN_<FMT>=0`) |
| Our HTTP client (Linux) | `OKAY_OWN_HTTP=ON`, and the engine gets `OKAY_OWN_HTTP_DEFAULT_ON` | `NativeAvailable()` is true without setting anything; `OKAY_OWN_HTTP=0` or `OKAY_HTTP_CURL=1` goes back to curl. No effect on Windows (WinHTTP stays) or macOS |
| Our platform backend (Linux) | the engine's `okay_platform` gets `OKAY_PLATFORM_DEFAULT_OWN` | `okay::platform::Default()` is the own backend (run time: `OKAY_PLATFORM_BACKEND=sdl2`). **Not set on Windows**: the Win32 backend has never run on real Windows |
| OkayUI as the launcher and editor UI | the launcher and editor get `OKAY_UI_DEFAULT_BACKEND_OKAYUI` (same as `OKAY_UI_DEFAULT_BACKEND=okayui`) | starts on OkayUI where it is compiled in (run time: `OKAY_UI_BACKEND=imgui`). Only the ported screens change; the rest stays Dear ImGui |
| Marker | the engine gets `OKAY_OWN_STACK_BUILD` | tests can tell it is a dogfood build |

Not set by it, ever: `OKAY_OWN_CRYPTO`, `OKAY_USE_SODIUM`, `OKAY_USE_ASSIMP`, `OKAY_PREFER_OWN_READERS`. Assimp, when
you build it, stays the fallback reader.

### 3.2 Run time: `OKAY_OWN_STACK=1`

No rebuild. At the top of `main()` the player, launcher and editor call `okay::ownstack::Startup()`
(`shared/OwnStack.hpp`). With the exact value `1` it sets each of these variables **only if it is not already set**, so a
developer's own `OKAY_OWN_FBX=0` still wins:

| Variable set to | What it switches |
|---|---|
| `OKAY_OWN_FBX=1` | our FBX reader |
| `OKAY_OWN_COLLADA=1`, `OKAY_OWN_3DS=1`, `OKAY_OWN_BVH=1`, `OKAY_OWN_OFF=1`, `OKAY_OWN_SMD=1`, `OKAY_OWN_MD5=1`, `OKAY_OWN_MS3D=1`, `OKAY_OWN_ASE=1`, `OKAY_OWN_3MF=1`, `OKAY_OWN_AMF=1`, `OKAY_OWN_X3D=1`, `OKAY_OWN_DXF=1`, `OKAY_OWN_LWO=1`, `OKAY_OWN_BLEND=1`, `OKAY_OWN_MD2=1`, `OKAY_OWN_MD3=1`, `OKAY_OWN_IRRMESH=1`, `OKAY_OWN_B3D=1`, `OKAY_OWN_X=1` | the other readers (they exist in every build) |
| `OKAY_OWN_HTTP=1` | our HTTP client, **only in a build made with `-DOKAY_OWN_HTTP=ON`** and only on Linux |
| `OKAY_PLATFORM_BACKEND=own` | the own platform backend, **Linux only** |
| `OKAY_UI_BACKEND=okayui` | OkayUI for the launcher and editor, where compiled in |

Cryptography is not on this list: it is a build-time choice (3.3). The environment variable `OKAY_OWN_CRYPTO` does
nothing at run time, and a test checks that.

`OKAY_OWN_STACK=1` in a release build is an unsupported developer switch. It cannot turn on anything that is not compiled
in, but it does switch readers and (on Linux) the window system. Do not tell players about it.

### 3.3 Crypto in test builds only

`-DOKAY_OWN_STACK=ON -DOKAY_OWN_STACK_CRYPTO_TEST=ON` (or `OKAY_OWN_STACK_CRYPTO_TEST=1` in the environment when
configuring) also sets `OKAY_OWN_CRYPTO=ON`, and the startup line says `crypto=own(test)`. Without `OKAY_OWN_STACK` that
variable is a configure error. CMake prints a warning when it is on.

**Releases refuse it, and refuse any build that does not really have libsodium.** Owner decision (2026-11-14, `CLAUDE.md`
rule 18): libsodium stays the default in every release, `okay::crypto` stays off. The danger is not only a switch being
turned on: without libsodium, `shared/AntiCheatCrypto.hpp` quietly falls back to `okay::crypto` for Ed25519 (update
signatures, anti-cheat manifest, host identity) and CMake used to only print a status line. So there are four locks:

1. **CMake: `-DOKAY_REQUIRE_SODIUM=ON`** (default OFF for developer builds). With it, configuring FAILS when libsodium is not
   found, when `OKAY_SODIUM_PREFIX` does not hold `lib/libsodium.a` and `include/sodium.h`, when `OKAY_USE_SODIUM=OFF`, or when
   `OKAY_OWN_CRYPTO` / `OKAY_OWN_STACK_CRYPTO_TEST` is on (also if a toolchain file sets the variable). Passed by
   `scripts/build-windows-dist.sh`, `scripts/release-local.sh` (Linux server) and `release.yml` (Windows and Linux server).
   Without the option, a missing libsodium prints a loud "NOT found ... UNREVIEWED okay::crypto" line and sets the internal
   cache entry `OKAY_SODIUM_IN_BUILD=OFF`.
2. **The download is pinned.** `scripts/fetch-tarball.sh --sha256 <hex>` (or `OKAY_FETCH_SHA256`) fails on a different hash,
   and on a value that is not 64 hex digits (a `TODO` placeholder fails, it never skips). The libsodium MinGW prebuilt is
   fetched by `scripts/fetch-libsodium-mingw.sh`, which reads `scripts/libsodium-mingw.pin`, and then checks that
   `libsodium-win64/lib/libsodium.a` and `include/sodium.h` exist (a tarball with another folder name fails loudly).
   Used by `release.yml`, `ci.yml` (Windows cross build) and `release-local.sh`. **The pin is trust on first use:** the hash is
   of the file `download.libsodium.org` served on 2026-11-14 (1,104,214 bytes, SHA-256
   `19f7e5f814f62f5bfdf6ea2208244adbddb80e73362879ddb0e4844bc1f12c82`, fetched twice with the same result). It was NOT
   checked against a published signature: the project also publishes a `.minisig` next to the file, which was downloaded but
   not verified (no minisign tool here, and no public key obtained from an independent source). The "-stable" name means
   upstream may re-publish the file; if the pin fails, find out what changed before updating the hash.
3. **The guard script checks the built result**, not only names. `scripts/check_release_own_stack.sh` refuses: the three
   `OKAY_OWN_*` names set on in the environment, in a `-DNAME=ON` or `-D NAME=ON` argument (with a space), or in the cache;
   `-DOKAY_USE_SODIUM=OFF` / `-DOKAY_REQUIRE_SODIUM=OFF`; and, with `--cache DIR`, a tree whose `CMakeCache.txt` does not say
   `OKAY_SODIUM_IN_BUILD=ON` and `OKAY_REQUIRE_SODIUM=ON`, or whose `build.ninja` / `compile_commands.json` / Makefile flags
   for the engine and the programs lack `-DOKAY_HAVE_SODIUM` or carry `-DOKAY_OWN_CRYPTO` (this catches
   `set(OKAY_OWN_CRYPTO ON)` in a toolchain file, which leaves no cache entry). Test targets are not scanned (some define
   `OKAY_OWN_CRYPTO` on purpose).
4. **`--binary PATH` asks the program.** It runs the program with `OKAY_PRINT_STACK_AND_EXIT=1`; `Startup()` then prints the
   stack line to stdout and exits 0 before doing anything else; the guard refuses unless the line says exactly
   `crypto=libsodium`. Used by `release.yml` on the Linux server binary (it runs on the runner) and `release-local.sh`.
   **A Windows .exe cannot run on the Linux build host**, so for the Windows programs only the cache and compiler-flag checks
   (lock 3) run after configure; the programs' own line was not asked in CI. A run on real Windows would show it
   (`set OKAY_PRINT_STACK_AND_EXIT=1` then start the exe): unverified.

Called by `scripts/build-windows-dist.sh` (arguments before configure, the tree after), by the `release.yml` Windows job (a
step before the build) and by the Linux server job. Ctest entries: `release_refuses_own_crypto` (the guard's self-test: each
bypass above and a build without libsodium), `fetch_tarball_pin` (the pinning, offline), `own_stack` (the startup word).

## 4. The startup line

Each program writes one line to stderr when it starts, and the editor's About popup shows the same text:

```
[okay] stack: platform=sdl2 http=curl readers=ufbx+assimp crypto=libsodium ui=imgui        (a default Linux build with Assimp)
[okay] stack: platform=own http=own readers=own(fbx,dae,3ds,bvh,...)+assimp crypto=own(test) ui=okayui   (the CI own-stack build)
```

* `platform`: the `okay::platform` default (own or sdl2). The editor and launcher still open their windows with SDL
  directly, so `platform=own` there means the seam parts (timers, audio, gamepads, voice) are ours, not the window.
* `http`: `curl`, `own`, or `winhttp` (Windows).
* `readers`: `own(...)` lists the opt-in formats that read with our code now; `ufbx` is shown while FBX is still ufbx;
  `assimp` is shown when Assimp is compiled in. Our always-on readers (glTF, OBJ, STL, PLY) are not listed.
* `crypto`: what Ed25519 (updates, anti-cheat manifest, host identity) really uses, asked of `okayac::ed25519::UseSodium()` at
  run time: `libsodium` (the only value a release may show), `own(no libsodium)` (libsodium is not in the build or did not
  start: the fallback is `okay::crypto`, our own unreviewed code), `own(test)` (`OKAY_OWN_STACK_CRYPTO_TEST`) or `own`
  (`OKAY_OWN_CRYPTO`). The TweetNaCl port that older text mentions is deleted.
  `OKAY_PRINT_STACK_AND_EXIT=1` prints the line to stdout and exits (the release guard uses it).
* `ui`: `imgui`, `okayui`, or `none` (the player). At the top of `main()` the line cannot know a UI choice saved in the
  settings of a developer build (`OKAY_DEV`); the About popup uses the resolved one.

## 5. The order, with proposed release numbers

These are proposals. The owner decides each flip. A release number is the earliest it could go, not a promise; a gate
that fails moves it back.

**Superseded on 2026-11-15.** 2.871.0 shipped the dogfood switch. The rows for 2.872.0 (readers to default on) and 2.874.0 (own HTTP stage 2) did not
happen: those releases shipped other work. The table is kept as the original plan. The current order, one group per release, is in
`docs/own_flip_proposal.md` section 2.

| Release | Step | Stage | Gate that must hold first | What could go wrong, and how it falls back |
|---|---|---|---|---|
| 2.871.0 | The dogfood switch, the startup line, the CI `own-stack` job (this change). Nothing changes for users | 1 for all except crypto | Tests in section 6 pass | A broken switch only affects people who set it. A default build must print the old line (tested) |
| 2.872.0 | **Own readers FBX, COLLADA, 3DS, BVH to stage 3**: default on, ufbx and Assimp stay compiled as fallback, escape `OKAY_OWN_<FMT>=0` | 3 (skips 2: there is no sensible Settings row for a file reader; the escape switch and the automatic fallback are the safety net) | Owner approval; a real Windows import run of the corpus files (**not possible from CI today**); release note one release ahead (2.871.0); automatic fallback to the old reader when ours fails to read a file (check the flip branch really does this; **unverified here**) | A file our reader rejects or reads differently. Falls back to ufbx or Assimp; `OKAY_OWN_<FMT>=0` forces the old one. Known gaps: Maya/Arnold materials, UV set names, left-handed source axes |
| 2.873.0 | Delete ufbx and `OKAY_OWN_FBX` (one release after the FBX flip) | 4 for FBX | 2.872.0 shipped without a regression report | Revert the PR; ufbx is in git history |
| 2.874.0 | **Own HTTP, stage 2** for Linux builds (the dedicated server and Linux player): documented `OKAY_OWN_HTTP=1`; the Linux server release is built with `-DOKAY_OWN_HTTP=ON`. Windows is not touched (WinHTTP) | 2 (Linux) | A run against the real hosts (GitHub Releases, Supabase, PlayFab, a CDN), the `own-http` job seen green on GitHub, a soak run (hours of downloads, descriptor and thread leak checks), an outside look at the parser | A server answer our client refuses, or a proxy it cannot honour: the call returns "not reached" and curl runs, as today. Slower startup (no connection reuse) |
| 2.875.0 | Remaining readers (OFF, SMD, MD5, MS3D, ASE, 3MF, AMF, X3D, DXF, LWO, Blender, MD2, MD3, Irrlicht mesh, B3D, DirectX .x) to stage 3, in groups of about five | 3 | Same as FBX, per group, plus real files with skeletons for SMD and MS3D, the DXF/AMF/X3D differences from Assimp read and accepted by the owner | Same fallback to Assimp |
| 2.876.0 | **Own HTTP, stage 3** for Linux builds, curl stays as the fallback | 3 (Linux) | Stage 2 for one release; connection reuse or a measured "fast enough"; the remaining curl call sites (launcher rows 2, 4 to 7, 9) moved onto it or left on curl on purpose; owner approval | Same fallback. Windows keeps WinHTTP: nothing planned (own TLS is crypto, rule 18) |
| 2.877.0 | **Platform layer for the player, stage 2**: a Settings row in the player ("Window system: standard or built-in (preview)") on Linux; Windows only after a real Windows run | 2 (Linux) | The software painter is declared ready (it is "off, not ready" today); real X11 desktops (window manager, multiple monitors, a real input method, a real file manager); a real Windows run of the Win32 backend (window, events, input, GL context, audio, gamepad) | The own backend fails to open a display or a GL context: the player switches to SDL2 for that run and logs it. No fallback inside a running game |
| 2.880.0 or later | **Platform layer for the player, stage 3** | 3 | Stage 2 for a release; the Windows run done and written down; owner approval | As above; escape `OKAY_PLATFORM_BACKEND=sdl2` |
| later (no number) | **Launcher and editor UI**: OkayUI as the default | 2 when the ported screens are declared ready, 3 after | OkayUI run in real windows (nothing has been); the 78 missing names and 303 missing flag uses closed or reshaped; an own Dear ImGui platform and renderer backend so the editor and launcher can leave SDL; the code editor widget run in a real window | A screen that behaves differently: switch back in Settings (OKAY_DEV only today) or `OKAY_UI_BACKEND=imgui` |
| later (no number) | **SDL2 removal** from the editor and launcher | 3, 4 | Needs the previous row and the own 2D painter; Web build has no own backend, so SDL2 stays for the browser | Not planned yet |
| after an outside review | **Cryptography** | 1 in tests only until then | An outside expert's written review (`docs/crypto_review_package.md`), then the normal gates | Nothing ships before the review. The review gate is the owner's rule (CLAUDE.md rule 18), not ours to waive |
| later | Remaining Assimp formats (21 with no own reader), then delete Assimp | per format | `docs/own_readers_remaining.md` order | Dropped formats are named one release ahead |

## 6. What this change verifies, and what it cannot

Run on Linux in these sessions (numbers are in the pull request report):

* The text of the startup line, the settings the variable sets, "a variable already set wins", "crypto is never set",
  "a default build prints the old stack", and "an own-stack build prints the all-own stack" (`test_own_stack`, ctest
  `own_stack` and `own_stack_env`).
* The release guard against own crypto (`release_refuses_own_crypto`).
* The own-stack build's own model-reader, HTTP, platform, OkayUI, anti-cheat and `net_crypto_own` tests, in the CI job
  `own-stack` (non-required) and locally.

**Not verified, and not verifiable from here:**

* Anything on real Windows. The Windows builds in CI are MinGW cross builds run under wine; the Win32 platform backend,
  Windows audio, `OKAY_OWN_STACK` with the Windows readers and the `release.yml` guard on a real release run were not
  seen on Windows. `scripts/build-windows-dist.sh` was edited and its guard script tested on its own, but the whole
  script was not run.
* ~~The `own-stack` CI job on GitHub~~ It has since run green on GitHub (main runs 43656201342 and 43754132084). The job builds without Assimp, so it does not test the own-first-then-Assimp fallback.
* Real servers for the HTTP client, a real window manager, input method or file manager for the platform layer, any
  real window for OkayUI, real hardware for gamepads.
* That the readers fall back to the old reader by themselves when ours rejects a file: the dogfood job tests the readers,
  not that fallback.

## 7. Where each switch is

| Switch | Where |
|---|---|
| `OKAY_OWN_STACK`, `OKAY_OWN_STACK_CRYPTO_TEST` (CMake) | `CMakeLists.txt` |
| per-format options | `CMakeLists.txt`, `engine/CMakeLists.txt` |
| platform default | `cmake/OkayPlatform.cmake` |
| HTTP default | `engine/CMakeLists.txt`, `engine/include/okay/Platform/NativeHttp.hpp` |
| `OKAY_OWN_STACK=1`, startup line | `shared/OwnStack.hpp`, called from `player/main.cpp`, `launcher/main.cpp`, `editor/main.cpp` |
| release guard | `scripts/check_release_own_stack.sh`, `scripts/build-windows-dist.sh`, `.github/workflows/release.yml` |
| CI job | `.github/workflows/ci.yml`, job `own-stack` |
| tests | `tests/test_own_stack.cpp` |
